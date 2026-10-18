# SPDX-License-Identifier: GPL-3.0-only
"""FM6 pack voices compressed (tools/gen_fm6_packs.py; firmware/src/engines/fm6/eng_fm6.c fm6_pk_decode decodes them):
each voice's 118 bytes of DX7 VMEM (its name is kept apart, uncompressed) as LZ4-style sequences over a shared
dictionary that every pack voice may copy from, so a voice decodes alone into 118 bytes.

A sequence: a token (high nibble the literal count, low nibble the match length - 4; 15 = more in the bytes after it,
255 each, LZ4's), the literals, then, unless the voice is complete, the match: its distance back (2 bytes, low first)
counted over the dictionary followed by the voice decoded so far, and its extra length bytes. The voice ends when its
118th byte is written, after the literals or after a match.
"""

VMEM = 118                                      # the bytes compressed (the name, 10 more, is not)
MINM = 4


def _ext(n):
    out = bytearray()
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)
    return out


def _seq(lits, mlen, dist):
    ll, ml = len(lits), (mlen - MINM if mlen else 0)
    out = bytearray([(min(ll, 15) << 4) | (min(ml, 15) if mlen else 0)])
    if ll >= 15:
        out += _ext(ll - 15)
    out += lits
    if mlen:
        out += bytes((dist & 255, dist >> 8))
        if ml >= 15:
            out += _ext(ml - 15)
    return out


def _best(hist, i):
    """the longest match for hist[i:] in hist[:i] (at least MINM), the nearest of equals: (length, distance)"""
    n, best = len(hist), (0, 0)
    for j in range(i - 1, -1, -1):
        k = 0
        while i + k < n and hist[j + k] == hist[i + k]:
            k += 1
        if k > best[0]:
            best = (k, i - j)
    return best if best[0] >= MINM else (0, 0)


def compress(voice: bytes, dictionary: bytes) -> bytes:
    """voice (VMEM bytes) as sequences over dictionary; greedy with one step of lazy matching"""
    assert len(voice) == VMEM
    hist, d = dictionary + voice, len(dictionary)
    out, lits, i = bytearray(), bytearray(), d
    while i < len(hist):
        m, dist = _best(hist, i)
        if m and i + 1 < len(hist):
            m2, _ = _best(hist, i + 1)
            if m2 > m + 1:                              # (a longer match one byte on: this byte a literal)
                m = 0
        if not m:
            lits.append(hist[i])
            i += 1
            continue
        out += _seq(lits, m, dist)
        lits, i = bytearray(), i + m
    if lits:
        out += _seq(lits, 0, 0)
    return bytes(out)


def decompress(blk: bytes, dictionary: bytes) -> bytes:
    """compress's inverse, as eng_fm6.c fm6_pk_decode does it (the generator checks every voice with it)"""
    out, i = bytearray(), 0
    while len(out) < VMEM:
        t = blk[i]
        i += 1
        ll = t >> 4
        if ll == 15:
            while True:
                b = blk[i]
                i += 1
                ll += b
                if b != 255:
                    break
        out += blk[i:i + ll]
        i += ll
        if len(out) >= VMEM:
            break
        dist = blk[i] | blk[i + 1] << 8
        i += 2
        ml = (t & 15) + MINM
        if t & 15 == 15:
            while True:
                b = blk[i]
                i += 1
                ml += b
                if b != 255:
                    break
        for _ in range(ml):
            k = len(out) - dist
            out.append(out[k] if k >= 0 else dictionary[len(dictionary) + k])
    assert len(out) == VMEM and i == len(blk), (len(out), i, len(blk))
    return bytes(out)


def pick_dictionary(voices, n=4):
    """the n voices (in list order, consecutive) whose bytes as the dictionary make the smallest total"""
    best = None
    for s in range(0, len(voices) - n + 1, 4):
        d = b"".join(voices[s:s + n])
        tot = len(d) + sum(len(compress(v, d)) for v in voices)
        if best is None or tot < best[0]:
            best = (tot, s)
    return b"".join(voices[best[1]:best[1] + n])
