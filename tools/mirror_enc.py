#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The screen mirrored: the device's encoder in Python (firmware/src/io/editor/ed_mirror_enc.h; web/EDITOR_PROTOCOL.md "The screen
mirrored"), plain frames (ops 0..2), byte for byte the firmware's: runs, bitmaps, the pixels above and, asked for (aa=True,
opti_miraa_1), the AA token. tests/mirror_aa_test.py checks it against the firmware's own stream; tools/fm1_mirror.py decodes.
  Encoder(aa).fill(x, y, w, h, colour) / .blit(x, y, w, h, pixels) -> the frames (F0 .. F7 each) as bytes; pixels RGB565, row
  by row (not byte-swapped)."""

CUT, SLOTS, LIT, LONG, BITS, UP = 250, 7, 7, 13, 14, 15
SHORT, LMAX, BMAX = 13, 8191, 16
AA_MAX, AA_UP, AA_BG, AA_LOOK, AA_NC = 127, 16, 6, 16, 8
HDR = [0xF0, 0x7D, 0x46, 0x4C, 106]


def blend(f: int, b: int, a: int) -> int:
    """fm1-base's text over its ground (device/gfx.c cv_text): per channel (f * a + b * (15 - a)) / 15, rounded down"""
    return (((f >> 11) * a + (b >> 11) * (15 - a)) // 15 << 11 | (((f >> 5) & 63) * a + ((b >> 5) & 63) * (15 - a)) // 15 << 5
            | ((f & 31) * a + (b & 31) * (15 - a)) // 15)


def ramp(f: int, b: int) -> dict[int, int]:
    """each shade of f over b -> its level: 15 or 0 first (the shortest codes), else the lowest a"""
    r = {blend(f, b, 15): 15}
    for a in range(15):
        r.setdefault(blend(f, b, a), a)
    return r


def lv_bits(a: int) -> int:
    return 1 if a == 15 else 2 if a == 0 else 6


def run_cost(k: int) -> int:
    return 1 if k <= SHORT else 2 if k - 14 < 64 else 3


class Encoder:
    def __init__(self, aa: bool = False) -> None:
        self.aa = aa
        self.out = bytearray()
        self.frame: list[int] = []
        self.slot = [0] * SLOTS
        self.rr = 1

    # ---- frames
    def _b(self, v: int) -> None:
        self.frame.append(v)

    def _open(self, op: int) -> None:
        self.frame = HDR + [op]

    def _close(self) -> None:
        self.out += bytes(self.frame + [0xF7])
        self.frame = []

    def _b2(self, v: int) -> None:
        self._b(v & 127)
        self._b(v >> 7 & 127)

    def _col(self, c: int) -> None:
        self._b2(c)
        self._b(c >> 14)

    def _cut(self, k: int = 0) -> None:
        if len(self.frame) > CUT or len(self.frame) + k > CUT + 21:
            self._close()
            self._open(2)

    def _slot(self, c: int) -> int:
        return self.slot.index(c) if c in self.slot else SLOTS

    def _tok(self, n: int, c: int) -> int:
        s = self._slot(c)
        self._b(n << 3 | (s if s < SLOTS else LIT))
        return s

    def _lit(self, s: int, c: int) -> None:
        if s < SLOTS:
            return
        self._col(c)
        self.slot[self.rr] = c
        self.rr = self.rr + 1 if self.rr < SLOTS - 1 else 1

    def _len(self, v: int) -> None:
        if v < 64:
            self._b(v)
        else:
            self._b((v & 63) | 64)
            self._b(v >> 6)

    def fill(self, x: int, y: int, w: int, h: int, c: int) -> bytes:
        self.out = bytearray()
        self._open(0)
        for v in (x, y, w, h):
            self._b2(v)
        self._col(c)
        self._close()
        return bytes(self.out)

    # ---- the tokens
    def _run(self, c: int, n: int) -> None:
        while n:
            self._cut()
            k = min(n, 14 + LMAX)
            s = self._tok(k - 1 if k <= SHORT else LONG, c)
            if k > SHORT:
                self._len(k - 14)
            self._lit(s, c)
            n -= k

    @staticmethod
    def _up_at(px: list[int], i: int, n: int, w: int) -> int:
        if i < w:
            return 0
        u = 0
        while i + u < n and u < 8 + LMAX and px[i + u] == px[i + u - w]:
            u += 1
        if u < 2:
            return 0
        runs, j = 0, i
        while j < i + u:
            r = j + 1
            while r < i + u and px[r] == px[j]:
                r += 1
            runs += run_cost(r - j)
            j = r
        return u if (1 if u <= 7 else 2 if u - 8 < 64 else 3) < runs else 0

    def _bits_at(self, px: list[int], i: int, n: int) -> tuple[int, int, int]:
        c0 = c1 = px[i]
        j = i + 1
        while j < n and j <= i + SHORT and px[j] == c0:
            j += 1
        if j > i + SHORT:
            return 0, 0, 0
        j = i + 1
        while j < n and j < i + 7 * BMAX:
            c = px[j]
            if c != c0 and c1 == c0:
                c1 = c
            elif c != c0 and c != c1:
                break
            j += 1
        g = (j - i) // 7
        if g < 2 or c1 == c0:
            return 0, 0, 0
        runs = 1 + sum(px[k] != px[k - 1] for k in range(i + 1, i + 7 * g))
        b = c0 if self._slot(c0) < SLOTS else c1
        f = c1 if b == c0 else c0
        lit = 0 if self._slot(f) < SLOTS else 3
        if self._slot(b) >= SLOTS or (lit and self._slot(b) == self.rr):
            return 0, 0, 0
        return (g, f, b) if 2 + g + lit < runs + lit else (0, 0, 0)

    def _bits(self, px: list[int], i: int, g: int, f: int, b: int) -> None:
        self._cut()
        s = self._tok(BITS, f)
        self._lit(s, f)
        self._b((g - 1) << 3 | self._slot(b))
        for k in range(g):
            v = 0
            for j in range(7):
                v = v << 1 | (px[i + 7 * k + j] == f)
            self._b(v)

    def _aa_at(self, px: list[int], i: int, n: int, w: int) -> tuple[int, int, int, int]:
        """an AA span at i: (L, fg, ground, its bytes); L 0: none"""
        lim = min(n, i + AA_MAX)
        if self._slot(px[i]) < SLOTS and i + 2 < n and px[i + 1] == px[i] and px[i + 2] == px[i]:
            return 0, 0, 0, 0
        cand: list[int] = []
        for j in range(i, min(lim, i + AA_LOOK)):
            if len(cand) == AA_NC:
                break
            if px[j] not in cand:
                cand.append(px[j])
        best = (0, 0, 0, 0)
        for s in range(SLOTS):
            bg = self.slot[s]
            if self._slot(bg) != s:
                continue
            for fg in cand:
                lit = 0 if self._slot(fg) < SLOTS else 3
                if fg == bg or (lit and s == self.rr):
                    continue
                r = ramp(fg, bg)
                L = bits = 0
                while i + L < lim and px[i + L] in r:
                    q = i + L
                    if L and q >= w and q + AA_UP <= n and px[q:q + AA_UP] == px[q - w:q - w + AA_UP]:
                        break                   # (the row above takes over here)
                    if L and q + AA_BG <= n and px[q:q + AA_BG] == [bg] * AA_BG:
                        break                   # (bare ground: a run)
                    bits += lv_bits(r[px[q]])
                    L += 1
                if L < 3:
                    continue
                while L > 3 and px[i + L - 1] == bg and px[i + L - 2] == bg:
                    L, bits = L - 1, bits - 2
                c = 3 + lit + (bits + 6) // 7
                if not best[0] or c * best[0] < best[3] * L:
                    best = (L, fg, bg, c)
        L = best[0]
        if not L:
            return best
        alt, j = 0, i
        while j < i + L:
            q = j + 1
            while q < i + L and px[q] == px[j]:
                q += 1
            alt += run_cost(q - j)
            if self._slot(px[j]) >= SLOTS and px[j] not in px[i:j]:
                alt += 3
            j = q
        return best if best[3] < alt else (0, 0, 0, 0)

    def _aa(self, px: list[int], i: int, L: int, f: int, b: int, cost: int) -> None:
        r = ramp(f, b)
        self._cut(cost)
        s = self._tok(BITS, f)
        self._lit(s, f)
        self._b(self._slot(b) << 3 | 7)
        self._b(L)
        acc = nb = 0
        for j in range(L):
            a = r[px[i + j]]
            v, k = (0, 1) if a == 15 else (2, 2) if a == 0 else (48 | a, 6)
            while k:
                k -= 1
                acc = acc << 1 | (v >> k & 1)
                nb += 1
                if nb == 7:
                    self._b(acc)
                    acc = nb = 0
        if nb:
            self._b(acc << (7 - nb))

    def blit(self, x: int, y: int, w: int, h: int, px: list[int]) -> bytes:
        self.out = bytearray()
        self._open(1)
        for v in (x, y, w, h):
            self._b2(v)
        self.slot, self.rr = [0] * SLOTS, 1
        n, i = w * h, 0
        while i < n:
            u = self._up_at(px, i, n, w)
            if u:
                self._cut()
                if u <= 7:
                    self._b(UP << 3 | (u - 1))
                else:
                    self._b(UP << 3 | 7)
                    self._len(u - 8)
                i += u
                continue
            g, f, b = self._bits_at(px, i, n)
            r = i + 1
            while r < n and px[r] == px[i]:
                r += 1
            if not self.aa:
                if g:
                    self._bits(px, i, g, f, b)
                    i += 7 * g
                else:
                    self._run(px[i], r - i)
                    i = r
                continue
            L, f2, b2, cost = self._aa_at(px, i, n, w)
            bp = bn = 0
            if g:
                bp, bn = 7 * g, 2 + g + (0 if self._slot(f) < SLOTS else 3)
            if L and (not bp or cost * bp < bn * L):
                bp, bn, g = L, cost, 0
            rc = run_cost(r - i) + (0 if self._slot(px[i]) < SLOTS else 3)
            if not bp or rc * bp <= bn * (r - i):
                self._run(px[i], r - i)
                i = r
            elif g:
                self._bits(px, i, g, f, b)
                i += 7 * g
            else:
                self._aa(px, i, L, f2, b2, cost)
                i += L
        self._close()
        return bytes(self.out)
