# SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0
"""The BLE radio start-up program (tools/ble_rf_capture.py) packed for flash: capture format 3.

The capture makes a format-2 program (one op per register / window / LUT write, docs/BLE-STACK.md section 8.1); its
SHA-256 (with the address, AGC and field tables) is the pinned fingerprint. The firmware stores it packed with two ops
of its own, which hal/fm1_ble_rf.h runs write for write as the ops they replace:

  OP_LUT2 0x91  cmd A, cmd B, first entry, count - 1, nA, nB, A's deltas (nA x 4 bytes), B's (nB x 4), then count
                pairs of delta indices: entry first + i gets A's word, then B's (each word = the command's last word +
                its delta, from 0). Replaces the RF-die LUT load: 512 one-word OP_LUT ops, A and B taking turns.
  OP_WWG  0x92  window, n, n entries, records r, then r x n values: the window writes of the n entries r times over
                (the BBP's per-channel loads: the same entries again and again).

Neither starts right after a trim mark (a mark applies to the next write; the runs hold none), and none crosses a
replay's source range, so a replay's offsets map to op boundaries of the packed program. unpack() turns the packed
program back into the format-2 bytes, and pack() checks that it gives the exact input back.

  tools/ble_rf_pack.py HEADER      print the packed size of a header's program and check the round trip"""
import sys

OP_W_MAX = 0x7F
OP_WIN_W, OP_WIN_R, OP_BBP_W, OP_BBP_R = 0x80, 0x84, 0x88, 0x89
OP_LUT, OP_TRIM, OP_DELAY, OP_SCAN, OP_REPLAY, OP_SKIP, OP_SECT, OP_END = 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0xFF
OP_LUT2, OP_WWG = 0x91, 0x92
MIN_LUT2 = 8              # pairs: below this a run stays one-word OP_LUT ops
MAX_PERIOD = 16           # OP_WWG: entries a record holds at most


class PackError(Exception):
    pass


def op_len(p, pc):
    """the length of the format-2 / format-3 op at pc"""
    op = p[pc]
    if op <= OP_W_MAX or op in (OP_SCAN, OP_REPLAY):
        return 5
    if op & 0xFC in (OP_WIN_W,) or op in (OP_BBP_W, OP_TRIM, OP_DELAY, OP_SKIP):
        return 3
    if op & 0xFC == OP_WIN_R or op in (OP_BBP_R, OP_SECT):
        return 2
    if op == OP_LUT:
        return 4 + 4 * p[pc + 3]
    if op == OP_LUT2:
        return 7 + 4 * (p[pc + 5] + p[pc + 6]) + 2 * (p[pc + 4] + 1)
    if op == OP_WWG:
        n = p[pc + 2]
        return 4 + n + p[pc + 3 + n] * n
    return 1


def ops_of(p):
    """[(offset, bytes)] of a program"""
    out, pc = [], 0
    while pc < len(p):
        n = op_len(p, pc)
        out.append((pc, bytes(p[pc:pc + n])))
        pc += n
    return out


def u32(b, i):
    return b[i] | b[i + 1] << 8 | b[i + 2] << 16 | b[i + 3] << 24


def _lut_run(ops, i, stop):
    """OP_LUT2 bytes for the longest run of one-word OP_LUT pairs from ops[i] (cmd A entry e, cmd B entry e, then e + 1
    ...), and the ops it covers; (None, 0) when there is none worth it"""
    a = ops[i][1]
    if a[0] != OP_LUT or a[3] != 1 or i + 1 >= stop:
        return None, 0
    b = ops[i + 1][1]
    if b[0] != OP_LUT or b[3] != 1 or b[2] != a[2] or b[1] == a[1]:
        return None, 0
    ca, cb, first = a[1], b[1], a[2]
    pairs, k = [], i
    while k + 1 < stop and len(pairs) < 256 and first + len(pairs) <= 0xFF:
        x, y = ops[k][1], ops[k + 1][1]
        e = first + len(pairs)
        if not (x[0] == y[0] == OP_LUT and x[3] == y[3] == 1 and x[1] == ca and y[1] == cb and x[2] == y[2] == e):
            break
        pairs.append((u32(x, 4), u32(y, 4)))
        k += 2
    best = None
    for n in range(len(pairs), MIN_LUT2 - 1, -1):         # the longest run whose delta sets fit a byte each
        da, db, la, lb = {}, {}, 0, 0
        for wa, wb in pairs[:n]:
            da.setdefault((wa - la) & 0xFFFFFFFF, len(da))
            db.setdefault((wb - lb) & 0xFFFFFFFF, len(db))
            la, lb = wa, wb
        if len(da) <= 255 and len(db) <= 255:
            best = (n, da, db)
            break
    if not best:
        return None, 0
    n, da, db = best
    out = bytearray((OP_LUT2, ca, cb, first, n - 1, len(da), len(db)))
    for d in (da, db):
        for v in sorted(d, key=d.get):
            out += v.to_bytes(4, "little")
    la = lb = 0
    for wa, wb in pairs[:n]:
        out += bytes((da[(wa - la) & 0xFFFFFFFF], db[(wb - lb) & 0xFFFFFFFF]))
        la, lb = wa, wb
    return (bytes(out), 2 * n) if len(out) < 8 * 2 * n else (None, 0)


def _ww_run(ops, i, stop):
    """OP_WWG bytes for the best repeat of n window-write entries from ops[i], and the ops it covers"""
    w0 = ops[i][1]
    if w0[0] & 0xFC != OP_WIN_W:
        return None, 0
    best = (0, None, 0)
    for n in range(1, MAX_PERIOD + 1):
        ents = []
        for j in range(n):
            if i + j >= stop or ops[i + j][1][0] != w0[0]:
                break
            ents.append(ops[i + j][1][1])
        if len(ents) < n:
            break
        r = 0
        while r < 255 and i + (r + 1) * n <= stop and \
                all(ops[i + r * n + j][1][0] == w0[0] and ops[i + r * n + j][1][1] == ents[j] for j in range(n)):
            r += 1
        gain = 3 * n * r - (4 + n + n * r)
        if r >= 2 and gain > best[0]:
            best = (gain, n, r)
    gain, n, r = best
    if gain <= 0:
        return None, 0
    out = bytearray((OP_WWG, w0[0] & 3, n)) + bytes(ops[i + j][1][1] for j in range(n)) + bytes((r,))
    out += bytes(ops[i + k][1][2] for k in range(n * r))
    return bytes(out), n * r


def pack(prog):
    """format-2 program -> format-3 bytes (checked: unpack gives prog back)"""
    prog = bytes(prog)
    ops = ops_of(prog)
    at = {pc: k for k, (pc, _) in enumerate(ops)}
    cuts = {0, len(ops)}                                  # op indices no packed op may cross: replay ranges
    replays = []
    for k, (pc, o) in enumerate(ops):
        if o[0] == OP_REPLAY:
            a, n = o[1] | o[2] << 8, o[3] | o[4] << 8
            if a not in at or a + n not in at and a + n != len(prog):
                raise PackError(f"replay {a}+{n} does not start and end at ops")
            cuts |= {at[a], at.get(a + n, len(ops))}
            replays.append(k)
    out, where, k = bytearray(), {}, 0                    # where: format-2 offset -> packed offset (op starts)
    while k < len(ops):
        stop = min(c for c in cuts if c > k)
        after_trim = k and ops[k - 1][1][0] == OP_TRIM
        where[ops[k][0]] = len(out)
        run, m = (None, 0) if after_trim else _lut_run(ops, k, stop)
        if not run and not after_trim:
            run, m = _ww_run(ops, k, stop)
        if run:
            out += run
            k += m
        else:
            out += ops[k][1]
            k += 1
    where[len(prog)] = len(out)
    for k in replays:                                     # the replays' offsets in the packed program
        pc, o = ops[k]
        a, n = o[1] | o[2] << 8, o[3] | o[4] << 8
        q = where[pc]
        out[q + 1:q + 5] = where[a].to_bytes(2, "little") + (where[a + n] - where[a]).to_bytes(2, "little")
    if unpack(bytes(out)) != prog:
        raise PackError("the packed program does not unpack to the captured one")
    if len(out) > 0xFFFF:
        raise PackError("the packed program is longer than a replay offset can reach")
    return bytes(out)


def unpack(packed):
    """format-3 bytes -> the format-2 program they were packed from"""
    out, where, replays = bytearray(), {}, []
    for pc, o in ops_of(packed):
        where[pc] = len(out)
        if o[0] == OP_LUT2:
            ca, cb, first, n, na, nb = o[1], o[2], o[3], o[4] + 1, o[5], o[6]
            da = [u32(o, 7 + 4 * j) for j in range(na)]
            db = [u32(o, 7 + 4 * (na + j)) for j in range(nb)]
            q, wa, wb = 7 + 4 * (na + nb), 0, 0
            for i in range(n):
                wa = (wa + da[o[q + 2 * i]]) & 0xFFFFFFFF
                wb = (wb + db[o[q + 2 * i + 1]]) & 0xFFFFFFFF
                out += bytes((OP_LUT, ca, first + i, 1)) + wa.to_bytes(4, "little")
                out += bytes((OP_LUT, cb, first + i, 1)) + wb.to_bytes(4, "little")
        elif o[0] == OP_WWG:
            w, n = o[1], o[2]
            ents, r = o[3:3 + n], o[3 + n]
            for k in range(r * n):
                out += bytes((OP_WIN_W | w, ents[k % n], o[4 + n + k]))
        else:
            if o[0] == OP_REPLAY:
                replays.append(len(out))
            out += o
    where[len(packed)] = len(out)
    for q in replays:
        a, n = out[q + 1] | out[q + 2] << 8, out[q + 3] | out[q + 4] << 8
        out[q + 1:q + 5] = where[a].to_bytes(2, "little") + (where[a + n] - where[a]).to_bytes(2, "little")
    return bytes(out)


def header_array(text, name):
    """the values of `name`'s initializer in a generated header"""
    import re
    m = re.search(r"\b%s\[\d+\] = \{(.*?)\};" % name, text, re.S)
    if not m:
        raise PackError(f"no {name} in the header")
    return [int(x.rstrip("u"), 0) for x in re.findall(r"0x[0-9A-Fa-f]+u?|\d+", m.group(1))]


def main(argv):
    text = open(argv[1]).read()
    p = bytes(header_array(text, "ble_rf_prog"))
    if "#define BLE_RF_TABLES_FORMAT 3" in text:
        p = unpack(p)
    q = pack(p)
    print(f"ble_rf_pack: {len(p)} B program -> {len(q)} B packed ({len(p) - len(q)} B less), round trip ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
