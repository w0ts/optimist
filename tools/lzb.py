#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LZB: the coder of what the boot copies into RAM (tools/build.py -> one stream after .text; decoded by
firmware/src/system/lzb.h lzb_expand from main.c fm1_cstart). Two layers:

1. The parse, parse(): literals and matches, a 2-byte minimum match, offsets up to 65535, and a match may repeat the
   sequence before it's offset (a rep match: nothing to say about the distance; machine code repeats the same
   distance often). A sequence: a token (bits 7..4 the literal count, 15 = more in length bytes after, each added,
   255 = another; bit 3 = the offset is the last one again; bits 2..0 the match length - 2, 7 = more, the same
   way), the literals, then for a new offset one bit (0: a 1-byte offset, distance - 1, 1..256; 1: two bytes
   little-endian, 1..65535), then the length bytes. The match is copied byte by byte from distance back (it may
   overlap). A block ends with its last literal or its last match, no empty token after, and the next block starts
   there: the decoder stops at the block's plain size; the last offset and the literal context start at 0 in every
   block. The parse is a forward dynamic programme over the sequence prices (the token, the offset or its absence,
   the length bytes, a literal) with the last offset carried along each path, the candidates from hash chains of
   `depth` earlier positions (the longest match and the longest with a 1-byte offset) and the match at the last
   offset; so a shorter match now may leave a longer or a rep one later.
2. The entropy coding, stream(): the parse's symbols are not stored, they are coded by an adaptive binary range coder
   (LZMA's: 11-bit probabilities, moved by a 32nd a bit; the decoder reads 5 bytes first, the coder's flush writes
   5), each byte as 8 bits down a tree of 255 probabilities, a tree per byte class: the tokens, the length bytes, the
   1-byte offsets, the low and the high byte of a 2-byte offset, and the literals in a context of the two previous
   bytes' top bits and the position's parity (16-bit instruction halves: LZB_PB 1), one probability for the offset's
   size bit. One model and one coder for the whole stream, block after block. 58.4 KB of RAM code and tables on the
   user's configuration: coded 38.9 KB with the 3-byte LZ4 shape and no rep matches (round 5), 38.2 KB this way
   (lzma 37.7 with a decoder many times the size), 2026-10-24.

walk(): the decoder as lzb.h has it, blob by blob from one stream (the round-trip check at every build,
tests/lzb_test.c's vectors). Pure Python, under a second for 58 KB."""
MINM = 2
LC, PB = 2, 1                       # (lzb.h: the literal contexts: the two previous bytes' top bits, the position's parity)
NBITS, SHIFT, TOP, PINIT = 11, 5, 1 << 24, 1024
TOK, LEN, OFF1, OFF2LO, OFF2HI, LONG, LIT = 0, 256, 512, 768, 1024, 1280, 1281   # (lzb.h: the trees' bases in the model;
NPROBS = LIT + (256 << (LC + PB))                                                 #  LONG one probability, where no tree reaches)
# the parse's prices, in bits, as the coder spends them on the boot stream (measured 2026-10-24 on the user's
# configuration: a literal 7.0, a token 6.6, a 1-byte offset 7.3 and its size bit 1.3, a 2-byte one 12.9 and its bit
# 0.7, a length byte 5.1; a rep match costs its token alone)
BITS_LIT, BITS_TOK, BITS_OFF1, BITS_OFF2, BITS_LEN = 7.0, 6.6, 7.3 + 1.3, 12.9 + 0.7, 5.1


def _len_bytes(n):
    out = bytearray()
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)
    return out


def _match_len(src, a, b, lim):
    """the common length of src[a:] and src[b:] up to lim (slices first: C speed, then the bytes)"""
    m, step = 0, 16
    while m + step <= lim and src[a + m:a + m + step] == src[b + m:b + m + step]:
        m += step
    while m < lim and src[a + m] == src[b + m]:
        m += 1
    return m


def _extra(v):
    """the length bytes past the token's 7"""
    return 0 if v < 7 else 1 + (v - 7) // 255


def _candidates(src, depth):
    """at each position: the longest match (length, offset) from the hash chain and the longest with a 1-byte offset"""
    n = len(src)
    cand, table = [[] for _ in range(n)], {}
    for i in range(n - MINM + 1):
        key = src[i:i + MINM]
        lst = table.get(key)
        if lst is None:
            table[key] = [i]
            continue
        lim, bl, bo, bs, so = n - i, 0, 0, 0, 0
        for c in reversed(lst[-depth:]):
            off = i - c
            if off > 65535:
                break
            m = _match_len(src, c, i, lim)
            if m > bl:
                bl, bo = m, off
            if off <= 256 and m > bs:
                bs, so = m, off
            if m == lim:
                break
        if bl >= MINM:
            cand[i].append((bl, bo))
        if bs >= MINM and (bs, so) != (bl, bo):
            cand[i].append((bs, so))
        lst.append(i)
    return cand


def parse(src: bytes, depth: int = 256):
    """-> [(literals, offset, match length, rep)] as the decoder reads them; the last one (literals, None, 0, False)
    when the block ends on literals. The best path by the prices above, the last offset carried along each path"""
    n = len(src)
    cand = _candidates(src, depth)
    INF = float("inf")
    cost, rep, back = [INF] * (n + 1), [0] * (n + 1), [None] * (n + 1)   # back: (from, length or 0, offset, rep)
    cost[0] = 0.0
    for i in range(n):
        ci = cost[i]
        if ci == INF:
            continue
        r = rep[i]
        v = ci + BITS_LIT
        if v < cost[i + 1]:
            cost[i + 1], rep[i + 1], back[i + 1] = v, r, (i, 0, 0, False)
        cands = cand[i]
        if r and src[i] == src[i - r]:
            m = _match_len(src, i - r, i, n - i)
            if m >= MINM:
                cands = cands + [(m, r)]
        for b, off in cands:
            isrep = off == r
            oc = 0.0 if isrep else BITS_OFF1 if off <= 256 else BITS_OFF2
            lens = list(range(MINM, min(b, MINM + 12) + 1)) + ([b] if b > MINM + 12 else [])
            for L in lens:
                v = ci + BITS_TOK + oc + BITS_LEN * _extra(L - MINM)
                if v < cost[i + L]:
                    cost[i + L], rep[i + L], back[i + L] = v, off, (i, L, off, isrep)
    steps, j = [], n
    while j > 0:
        f, L, off, isrep = back[j]
        steps.append((L, off, isrep))
        j = f
    steps.reverse()
    seqs, lits, pos = [], bytearray(), 0
    for L, off, isrep in steps:
        if L == 0:
            lits.append(src[pos])
            pos += 1
        else:
            seqs.append((bytes(lits), off, L, isrep))
            lits, pos = bytearray(), pos + L
    if lits:
        seqs.append((bytes(lits), None, 0, False))
    return seqs


class _Coder:
    """the range coder (LZMA's), with carry propagation through a cached byte"""
    def __init__(self):
        self.low, self.range, self.cache, self.csize, self.out = 0, 0xFFFFFFFF, 0, 1, bytearray()

    def _shift_low(self):
        if self.low < 0xFF000000 or self.low >= (1 << 32):
            carry, c = self.low >> 32, self.cache
            while True:
                self.out.append((c + carry) & 0xFF)
                c = 0xFF
                self.csize -= 1
                if self.csize == 0:
                    break
            self.cache = (self.low >> 24) & 0xFF
        self.csize += 1
        self.low = (self.low & 0x00FFFFFF) << 8

    def bit(self, probs, i, b):
        p = probs[i]
        bound = (self.range >> NBITS) * p
        if b == 0:
            self.range = bound
            probs[i] = p + (((1 << NBITS) - p) >> SHIFT)
        else:
            self.low += bound
            self.range -= bound
            probs[i] = p - (p >> SHIFT)
        while self.range < TOP:
            self.range <<= 8
            self._shift_low()

    def byte(self, probs, base, v):
        m = 1
        for i in range(7, -1, -1):
            b = (v >> i) & 1
            self.bit(probs, base + m, b)
            m = (m << 1) | b

    def finish(self):
        for _ in range(5):
            self._shift_low()
        return bytes(self.out)


class _Decoder:
    """lzb.h's side: the state fm1_cstart keeps across the blocks (s, range, code, the model)"""
    def __init__(self, data):
        self.d, self.s, self.range, self.code = data, 0, 0xFFFFFFFF, 0
        self.probs = [PINIT] * NPROBS
        for _ in range(5):
            self.code = ((self.code << 8) | self.d[self.s]) & 0xFFFFFFFF
            self.s += 1

    def bit(self, i):
        p = self.probs[i]
        bound = (self.range >> NBITS) * p
        if self.code < bound:
            self.range = bound
            self.probs[i] = p + (((1 << NBITS) - p) >> SHIFT)
            b = 0
        else:
            self.code -= bound
            self.range -= bound
            self.probs[i] = p - (p >> SHIFT)
            b = 1
        while self.range < TOP:
            self.range = (self.range << 8) & 0xFFFFFFFF
            self.code = ((self.code << 8) | self.d[self.s]) & 0xFFFFFFFF
            self.s += 1
        return b

    def byte(self, base):
        m = 1
        while m < 256:
            m = (m << 1) | self.bit(base + m)
        return m & 255


def lit_base(p1, p2, pos):
    """the literal tree for a byte after p1 (the byte before it) and p2 (the one before that) at the block position
    pos; 0 for the bytes a block has not got yet"""
    return LIT + 256 * ((((p1 >> 7) << 1) | (p2 >> 7)) << PB | (pos & ((1 << PB) - 1)))


def _lens(coder, probs, v):
    while v >= 255:
        coder.byte(probs, LEN, 255)
        v -= 255
    coder.byte(probs, LEN, v)


def encode_block(raw, coder, probs):
    """raw as one block of the stream: its parse's symbols through the coder, class by class"""
    pos = 0
    for lits, off, mlen, isrep in parse(raw):
        n, ml = len(lits), 0 if off is None else mlen - MINM
        coder.byte(probs, TOK, (min(n, 15) << 4) | (8 if isrep else 0) | min(ml, 7))
        if n >= 15:
            _lens(coder, probs, n - 15)
        for b in lits:
            coder.byte(probs, lit_base(raw[pos - 1] if pos else 0, raw[pos - 2] if pos > 1 else 0, pos), b)
            pos += 1
        if off is None:
            break
        if not isrep:
            coder.bit(probs, LONG, 1 if off > 256 else 0)
            if off > 256:
                coder.byte(probs, OFF2LO, off & 255)
                coder.byte(probs, OFF2HI, off >> 8)
            else:
                coder.byte(probs, OFF1, off - 1)
        if ml >= 7:
            _lens(coder, probs, ml - 7)
        pos += mlen


def decode_block(dec, size):
    """lzb.h lzb_expand in Python: one block of size bytes from the decoder's stream"""
    d = bytearray()
    p1 = p2 = rep = 0
    while len(d) < size:
        t = dec.byte(TOK)
        n = t >> 4
        if n == 15:
            while True:
                b = dec.byte(LEN)
                n += b
                if b != 255:
                    break
        while n and len(d) < size:
            b = dec.byte(lit_base(p1, p2, len(d)))
            d.append(b)
            p2, p1, n = p1, b, n - 1
        if len(d) >= size:
            break
        if t & 8:
            off = rep
        elif dec.bit(LONG):
            off = dec.byte(OFF2LO) | dec.byte(OFF2HI) << 8
        else:
            off = dec.byte(OFF1) + 1
        rep = off
        n = (t & 7) + MINM
        if (t & 7) == 7:
            while True:
                b = dec.byte(LEN)
                n += b
                if b != 255:
                    break
        while n and len(d) < size:
            d.append(d[-off])
            n -= 1
        p1, p2 = d[-1], d[-2]
    return bytes(d)


def stream(blobs):
    """-> (the stream, the plain bytes): the blobs as consecutive blocks in the order main.c decodes them (an empty
    blob is no bytes at all), decoded again here as lzb.h does and compared"""
    coder, probs, total = _Coder(), [PINIT] * NPROBS, 0
    for raw in blobs:
        if raw:
            encode_block(raw, coder, probs)
            total += len(raw)
    out = coder.finish()
    if walk(out, [len(b) for b in blobs]) != [bytes(b) for b in blobs]:
        raise ValueError("lzb: the stream does not round-trip")
    return out, total


def walk(stream, sizes):
    """lzb.h in Python: the blobs of these sizes decoded from one stream, block after block; the decoder ends within
    the coder's 5 flush bytes of the stream's end (it reads ahead by up to 4)"""
    dec = _Decoder(stream)
    blobs = [decode_block(dec, size) if size else b"" for size in sizes]
    if not len(stream) - 4 <= dec.s <= len(stream):
        raise ValueError(f"lzb: the decoder stopped {len(stream) - dec.s} B before the stream's end")
    return blobs


def c_vectors():
    """-> C source: test vectors for tests/lzb_test.c (plain, coded) over patterns a decoder can get wrong: a 1-byte
    offset of 256, a 2-byte one, overlapping copies, 2-byte matches, rep matches, long literal and match runs,
    several blocks in one stream"""
    import random
    rnd = random.Random(7)
    cases = [bytes(), b"a", b"ab", b"abab", b"abcabcabcabc", bytes(300) + b"x" + bytes(300),
             bytes(rnd.randrange(256) for _ in range(40)) * 3,
             bytes([1, 2, 3, 4] * 70 + [9] * 300 + list(range(256)) * 2),
             b"".join(bytes([i & 7, i >> 3 & 7, 0x80 | i & 3]) for i in range(600)),
             b"".join(bytes([i & 255, 7, i >> 8, 9]) for i in range(300))]   # (the same distance again and again)
    big = bytearray()
    for _ in range(50):
        if rnd.random() < 0.5 and big:
            k = rnd.randrange(1, 300)
            start = rnd.randrange(max(1, len(big) - 70000), len(big)) if len(big) > 1 else 0
            big += big[start:start + k] if start + k <= len(big) else bytes(k)
        else:
            big += bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 60)))
    cases.append(bytes(big))
    coded, _ = stream(cases)
    lines = ["/* generated by tools/lzb.py c_vectors (tools/build.py generate): plain blobs and their one stream */",
             f"#define LZB_NCASES {len(cases)}u",
             "static const uint32_t LZB_SIZES[LZB_NCASES] = {" + ", ".join(f"{len(c)}u" for c in cases) + "};",
             "static const uint8_t LZB_PLAIN[] = {" + ", ".join(str(b) for b in b"".join(cases)) + ", 0};",
             "static const uint8_t LZB_CODED[] = {" + ", ".join(str(b) for b in coded) + "};",
             f"#define LZB_CODED_N {len(coded)}u"]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    import sys
    from pathlib import Path
    Path(sys.argv[1]).write_text(c_vectors())
