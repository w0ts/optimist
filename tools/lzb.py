#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LZB: the coder of what the boot copies into RAM (tools/build.py lz4_stream -> lzb_stream; decoded by
firmware/src/system/main.c lzb_expand). LZ4's block shape (tools/lz4blk.py, the loader's), tuned for machine code
decoded once: a 3-byte minimum match and 1-byte offsets up to 256 (code repeats short sequences at short distances):
52 KB of RAM code and tables -> 44.1 KB, LZ4 45.8 KB, 2026-10-23.

A sequence: a token byte (bits 7..4 the literal count, 15 = more in the bytes after, each added, 255 = another
byte; bit 3 = a 2-byte offset; bits 2..0 the match length - 3, 7 = more, the same way), the literals, the offset
(1 byte: distance - 1, 1..256; or 2 bytes little-endian, 1..65535), then the length bytes. The match is copied byte
by byte from distance back (it may overlap). The decoder stops when the block's plain size is reached: a block ends
with its last literal or its last match, no empty token after, and the next block starts there.

compress(): an optimal parse (the longest match at each position, from hash chains of `depth` earlier positions, and
the longest with a 1-byte offset; a backward dynamic programme over the sequence prices, so a shorter match now may
leave a longer one later). walk(): the decoder as main.c has it, blob by blob from one stream (the round-trip check,
tests/lzb_test.c's vectors). Pure Python, seconds for 50 KB."""
MINM = 3


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
    cost, pick = [0] * (n + 1), [None] * (n + 1)
    for i in range(n - 1, -1, -1):
        c, p = cost[i + 1] + 1, None                # a literal byte
        for b, off in cand[i]:
            oc = 1 if off <= 256 else 2
            lens = list(range(MINM, min(b, MINM + 10) + 1)) + ([b] if b > MINM + 10 else [])
            for L in lens:
                v = 1 + oc + _extra(L - MINM) + cost[i + L]
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


def stream(blobs):
    """-> (the stream, the plain bytes): the blobs as consecutive blocks in the order main.c decodes them (an empty
    blob is no bytes at all), decoded again here as main.c does and compared"""
    out, total = bytearray(), 0
    for raw in blobs:
        if raw:
            out += compress(raw)
            total += len(raw)
    if walk(out, [len(b) for b in blobs]) != [bytes(b) for b in blobs]:
        raise ValueError("lzb: the stream does not round-trip")
    return bytes(out), total


def walk(stream, sizes):
    """main.c lzb_expand in Python: the blobs of these sizes decoded from one stream, block after block; the
    stream must end with the last"""
    blobs, s = [], 0
    for size in sizes:
        d = bytearray()
        while len(d) < size:
            t = stream[s]
            s += 1
            n = t >> 4
            if n == 15:
                while True:
                    b = stream[s]
                    s += 1
                    n += b
                    if b != 255:
                        break
            d += stream[s:s + n]
            s += n
            if len(d) >= size:
                break
            if t & 8:
                off = stream[s] | stream[s + 1] << 8
                s += 2
            else:
                off = stream[s] + 1
                s += 1
            n = (t & 7) + MINM
            if (t & 7) == 7:
                while True:
                    b = stream[s]
                    s += 1
                    n += b
                    if b != 255:
                        break
            for _ in range(n):
                d.append(d[-off])
        blobs.append(bytes(d))
    if s != len(stream):
        raise ValueError(f"lzb: {len(stream) - s} B left after the last block")
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
