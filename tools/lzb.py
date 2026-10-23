#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LZB: the coder of what the boot copies into RAM (tools/build.py -> one stream after .text; decoded by
firmware/src/system/lzb.h lzb_expand from main.c fm1_cstart). Two layers:

1. The parse, compress(): LZ4's block shape (tools/lz4blk.py, the loader's), tuned for machine code decoded once: a
   3-byte minimum match and 1-byte offsets up to 256 (code repeats short sequences at short distances). A sequence: a
   token byte (bits 7..4 the literal count, 15 = more in the bytes after, each added, 255 = another byte; bit 3 = a
   2-byte offset; bits 2..0 the match length - 3, 7 = more, the same way), the literals, the offset (1 byte:
   distance - 1, 1..256; or 2 bytes little-endian, 1..65535), then the length bytes. The match is copied byte by byte
   from distance back (it may overlap). A block ends with its last literal or its last match, no empty token after,
   and the next block starts there: the decoder stops at the block's plain size. An optimal parse: the longest match
   at each position, from hash chains of `depth` earlier positions, and the longest with a 1-byte offset; a backward
   dynamic programme over the sequence prices, so a shorter match now may leave a longer one later.
2. The entropy coding, stream(): the parse's bytes are not stored, they are coded by an adaptive binary range coder
   (LZMA's: 11-bit probabilities, moved by a 32nd a bit; the decoder reads 5 bytes first, the coder's flush writes 5),
   each byte as 8 bits down a tree of 255 probabilities, a tree per byte class: the tokens, the length bytes, the
   1-byte offsets, the low and the high byte of a 2-byte offset, and the literals in a context of the previous
   byte's top LC bits and the position's low PB bits (the block's; 16-bit instruction halves: PB 1). One model and
   one coder for the whole stream, block after block (a block starts with prev 0, position 0). 55.8 KB of RAM code
   and tables: the parse's bytes alone 44.9 KB (LZ4 46.5), coded 38.9 KB (zlib -9 40.0, lzma 36.9 with a decoder
   ten times the size), 2026-10-23.

walk(): the decoder as lzb.h has it, blob by blob from one stream (the round-trip check at every build,
tests/lzb_test.c's vectors). Pure Python, a few seconds for 55 KB."""
MINM = 3
LC, PB = 1, 1                       # (lzb.h LZB_LC, LZB_PB: the literal contexts; LC 2: 69 B less for 2 KB more of boot stack, 2026-10-23)
NBITS, SHIFT, TOP, PINIT = 11, 5, 1 << 24, 1024
TOK, LEN, OFF1, OFF2LO, OFF2HI, LIT = 0, 256, 512, 768, 1024, 1280   # (lzb.h: the trees' bases in the model)
NPROBS = LIT + (256 << (LC + PB))
# the parse's prices, in bits, as the coder spends them on the boot stream (measured 2026-10-23: a literal 6.99, a
# token 6.83, a 1-byte offset 7.30, a 2-byte one 13.10, a length byte 5.58; priced in bytes the stream was 173 B more)
BITS_LIT, BITS_TOK, BITS_OFF1, BITS_OFF2, BITS_LEN = 7.0, 6.8, 7.3, 13.1, 5.6


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


def compress(src: bytes, depth: int = 64) -> bytes:
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
    cost, pick = [0.0] * (n + 1), [None] * (n + 1)
    for i in range(n - 1, -1, -1):
        c, p = cost[i + 1] + BITS_LIT, None         # a literal byte
        for b, off in cand[i]:
            oc = BITS_OFF1 if off <= 256 else BITS_OFF2
            lens = list(range(MINM, min(b, MINM + 10) + 1)) + ([b] if b > MINM + 10 else [])
            for L in lens:
                v = BITS_TOK + oc + BITS_LEN * _extra(L - MINM) + cost[i + L]
                if v < c:
                    c, p = v, (L, off)
        cost[i], pick[i] = c, p
    out, i, anchor = bytearray(), 0, 0
    while i < n:
        if pick[i] is None:
            i += 1
            continue
        L, off = pick[i]
        lit, ml = src[anchor:i], L - MINM
        out.append((min(len(lit), 15) << 4) | (8 if off > 256 else 0) | min(ml, 7))
        if len(lit) >= 15:
            out += _len_bytes(len(lit) - 15)
        out += lit
        out += bytes((off & 0xFF, off >> 8)) if off > 256 else bytes((off - 1,))
        if ml >= 7:
            out += _len_bytes(ml - 7)
        i += L
        anchor = i
    lit = src[anchor:]
    if lit:                                         # (a blob that ends on a match ends there: the decoder stops at the
        out.append(min(len(lit), 15) << 4)         # plain size, so no empty token after it)
        if len(lit) >= 15:
            out += _len_bytes(len(lit) - 15)
        out += lit
    return bytes(out)


def sequences(block, size):
    """a parse block (compress) -> [(literals, offset or None, match length)], as the token shape reads"""
    seqs, s, d = [], 0, 0
    while d < size:
        t = block[s]
        s += 1
        n = t >> 4
        if n == 15:
            while True:
                b = block[s]
                s += 1
                n += b
                if b != 255:
                    break
        lits = block[s:s + n]
        s += n
        d += n
        if d >= size:
            seqs.append((lits, None, 0))
            break
        if t & 8:
            off = block[s] | block[s + 1] << 8
            s += 2
        else:
            off = block[s] + 1
            s += 1
        n = (t & 7) + MINM
        if (t & 7) == 7:
            while True:
                b = block[s]
                s += 1
                n += b
                if b != 255:
                    break
        seqs.append((lits, off, n))
        d += n
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


def lit_base(prev, pos):
    """the literal tree for a byte after prev at the block position pos"""
    return LIT + 256 * (((prev >> (8 - LC)) << PB) | (pos & ((1 << PB) - 1)))


def _lens(coder, probs, v):
    while v >= 255:
        coder.byte(probs, LEN, 255)
        v -= 255
    coder.byte(probs, LEN, v)


def encode_block(raw, coder, probs):
    """raw as one block of the stream: its parse's bytes through the coder, class by class"""
    prev, pos = 0, 0
    for lits, off, mlen in sequences(compress(raw), len(raw)):
        n, ml = len(lits), 0 if off is None else mlen - MINM
        coder.byte(probs, TOK, (min(n, 15) << 4) | (8 if off is not None and off > 256 else 0) | min(ml, 7))
        if n >= 15:
            _lens(coder, probs, n - 15)
        for b in lits:
            coder.byte(probs, lit_base(prev, pos), b)
            prev, pos = b, pos + 1
        if off is None:
            break
        if off > 256:
            coder.byte(probs, OFF2LO, off & 255)
            coder.byte(probs, OFF2HI, off >> 8)
        else:
            coder.byte(probs, OFF1, off - 1)
        if ml >= 7:
            _lens(coder, probs, ml - 7)
        pos += mlen
        prev = raw[pos - 1]


def decode_block(dec, size):
    """lzb.h lzb_expand in Python: one block of size bytes from the decoder's stream"""
    d = bytearray()
    prev = 0
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
            b = dec.byte(lit_base(prev, len(d)))
            d.append(b)
            prev, n = b, n - 1
        if len(d) >= size:
            break
        if t & 8:
            off = dec.byte(OFF2LO) | dec.byte(OFF2HI) << 8
        else:
            off = dec.byte(OFF1) + 1
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
        prev = d[-1]
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
    offset of 256, a 2-byte one, overlapping copies, long literal and match runs, several blocks in one stream"""
    import random
    rnd = random.Random(7)
    cases = [bytes(), b"a", b"abc", b"abcabcabcabc", bytes(300) + b"x" + bytes(300),
             bytes(rnd.randrange(256) for _ in range(40)) * 3,
             bytes([1, 2, 3, 4] * 70 + [9] * 300 + list(range(256)) * 2),
             b"".join(bytes([i & 7, i >> 3 & 7, 0x80 | i & 3]) for i in range(600))]
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
