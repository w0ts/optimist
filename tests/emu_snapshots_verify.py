"""tests/emu_snapshots_e2e.sh: the snapshot area of an emulator flash state (SNAPSHOTS 4: 8 sectors up to 0xD8000), parsed as snap_store.c scans it;
slot 3 must equal slot 1 and slot 4 slot 2 byte for byte but the save counter (info bytes 40..43); sections A and B must differ
between slot 1 and slot 2 (stored again over the used ones at the panel)"""
import struct
import sys
import zlib

nor = open(sys.argv[1], "rb").read()
BASE, N, PAY = 0xD8000 - 8 * 4096, 8, 4096 - 32
heads = []
for s in range(N):
    o = BASE + s * 4096
    h = nor[o:o + 32]
    magic, slot, part, parts, ver, seq, ln, crc, total, tcrc, hcrc = struct.unpack("<IBBBBIIIIII", h)
    if magic == 0x31534E53 and ver == 1 and hcrc == zlib.crc32(h[:28]):
        heads.append(dict(o=o, slot=slot, part=part, parts=parts, seq=seq, len=ln, crc=crc, total=total, tcrc=tcrc))
streams = {}
for k in range(9):
    p0 = sorted([h for h in heads if h["slot"] == k and h["part"] == 0], key=lambda h: -h["seq"])
    if not p0:
        continue
    p0 = p0[0]
    data = b""
    for p in range(p0["parts"]):
        h = next(x for x in heads if x["slot"] == k and x["seq"] == p0["seq"] and x["part"] == p)
        d = nor[h["o"] + 32:h["o"] + 32 + h["len"]]
        assert zlib.crc32(d) == h["crc"], f"slot {k} part {p} CRC"
        data += d
    assert zlib.crc32(data) == p0["tcrc"], f"slot {k} stream CRC"
    streams[k] = data


def info(s):
    name = s[8:20].split(b"\0")[0].decode()
    bpm, = struct.unpack("<H", s[30:32])
    mask, = struct.unpack("<I", s[32:36])
    recs, p = [], 48
    while p < len(s):
        kind, rid, n = s[p], s[p + 1], s[p + 2] | s[p + 3] << 8
        recs.append(f"{['?', 'WORK', 'SEC', 'SONG', 'TAIL'][kind] if kind < 5 else kind}"
                    f"{'' if kind != 2 else chr(65 + rid)}:{n}")
        p += 4 + n
    return f'"{name}" {bpm} BPM, sections {"".join(chr(65 + i) for i in range(16) if mask >> i & 1)}, {len(s)} B: ' + " ".join(recs)


G_UI = (12, 14, 16, 18, 19)   # G_MIDI, G_VIEW, G_SLOT, G_LOAD, G_SAVE: device state a load never applies (project.c)


def norm(s):
    """the stream without its save counter, the WORK record without the globals a load does not apply"""
    out, p = bytearray(s[:40] + b"\0\0\0\0" + s[44:48]), 48
    while p < len(s):
        kind, rid, n = s[p], s[p + 1], s[p + 2] | s[p + 3] << 8
        r = bytearray(s[p + 4:p + 4 + n])
        if kind == 1 and not r[0] & 1:                     # a compressed WORK: flags, [motion chunk], globals mask, values
            q = 1 + (2 + 3 * r[1] if r[0] & 4 else 0)
            mask = int.from_bytes(r[q:q + 4], "little")
            vals, v = {}, q + 4
            for g in range(32):
                if mask >> g & 1:
                    vals[g] = r[v:v + 2]
                    v += 2
            keep = {g: x for g, x in vals.items() if g not in G_UI}
            m2 = sum(1 << g for g in keep)
            r = r[:q] + m2.to_bytes(4, "little") + b"".join(keep[g] for g in sorted(keep)) + r[v:]
        out += bytes([kind, rid]) + len(r).to_bytes(2, "little") + r
        p += 4 + n
    return bytes(out)


for k, s in sorted(streams.items()):
    print(f"slot {'B' if k == 8 else k + 1}: {info(s)}")
bad = 0
for a, b in ((0, 2), (1, 3)):
    if a not in streams or b not in streams:
        print(f"FAIL: slot {a + 1} or {b + 1} missing")
        bad += 1
        continue
    x, y = streams[a], streams[b]
    same = norm(x) == norm(y)
    print(f"  raw bytes: {'identical' if len(x) == len(y) and x[:40] == y[:40] and x[44:] == y[44:] else 'differ only in the globals a load never applies' if same else 'differ'} (but the save counter)")
    print(f"slot {b + 1} == slot {a + 1} (loaded, saved again; G_SLOT G_VIEW.. aside): {'PASS' if same else 'FAIL'}")
    if not same:
        bad += 1
        n = min(len(x), len(y))
        i = next((i for i in range(n) if x[i] != y[i] and not 40 <= i < 44), n)
        print(f"  first difference at byte {i} (lengths {len(x)} / {len(y)})")
def sections(s):
    """the SEC records of a stream: section index -> its bytes"""
    out, p = {}, 48
    while p < len(s):
        kind, rid, n = s[p], s[p + 1], s[p + 2] | s[p + 3] << 8
        if kind == 2:
            out[rid] = s[p + 4:p + 4 + n]
        p += 4 + n
    return out


if 0 in streams and 1 in streams:                          # A and B stored again at the panel (SAVE + key, AGAIN, the key)
    s1, s2 = sections(streams[0]), sections(streams[1])
    for i in (0, 1):
        ok = i in s1 and i in s2 and s1[i] != s2[i]
        print(f"section {chr(65 + i)} overwritten between slot 1 and slot 2 (SAVE + key, then the key again): {'PASS' if ok else 'FAIL'}")
        bad += not ok
sys.exit(1 if bad else 0)
