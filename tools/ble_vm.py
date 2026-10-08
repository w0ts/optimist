# SPDX-License-Identifier: GPL-3.0-only
"""The JieLi SDK "VM" key/value store as stock V15 keeps it in flash: a reader and, for tests and the RF capture
only, a writer. Written from docs/BLE-HW-FACTS.md §14 (branch feat/ble-facts, d907ce3), not from vendor code.

  area     V15: A at 0x093000, B at 0x095000, 8 KiB each; the first 4 bytes 55 AA AA 55 mark the live one (both
           marked: A; neither: no VM) (§14.1, §14.3)
  record   4-byte header then the data, packed from area + 4 (§14.2):
             byte 0  the low byte of CRC-16/XMODEM over the data bytes only (the check)
             byte 1  id bits [7:0]
             byte 2  [3:0] id bits [11:8], [7:4] length bits [3:0]
             byte 3  length bits [11:4]
           the log ends at the first header whose check fails (an erased header always does) or at a record that
           would end past the area; for each id the last valid record wins (§14.3)
  187      64-byte payload + CRC-16/XMODEM of it, little-endian, + 00 00 (§14.5)

The firmware's reader is firmware/src/ble/ble_vm.c (the same rules, tested in tests/ble_vm_test.c). The firmware
never writes the VM; this writer exists to build test images and the capture's perturbed second boot."""

VM_BASE = 0x093000
AREA_SIZE = 0x2000                      # V15's 8 KiB halves (§14.1)
AREAS = (VM_BASE, VM_BASE + AREA_SIZE)
MAGIC = bytes((0x55, 0xAA, 0xAA, 0x55))
RF_IDS = (106, 107, 108, 187)           # the RF trims (§14.5)
RF_LEN = {106: 2, 107: 7, 108: 20, 187: 68}


def crc16_xmodem(data, crc=0):
    """CRC-16/XMODEM: polynomial 0x1021, initial value 0, no reflection, no final XOR"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
        crc &= 0xFFFF
    return crc


def header(rid, data):
    n = len(data)
    if not 0 <= rid < 0x1000 or not 0 < n < 0x1000:
        raise ValueError(f"VM record id {rid} / length {n} out of range")
    return bytes((crc16_xmodem(data) & 0xFF, rid & 0xFF, (rid >> 8) & 0x0F | (n & 0x0F) << 4, n >> 4))


def rec187(payload64):
    """record 187's data: the 64-byte payload, its CRC-16/XMODEM little-endian, 00 00"""
    if len(payload64) != 64:
        raise ValueError("record 187 has a 64-byte payload")
    c = crc16_xmodem(payload64)
    return bytes(payload64) + bytes((c & 0xFF, c >> 8, 0, 0))


def rec187_ok(data):
    return len(data) == 68 and crc16_xmodem(data[:64]) == data[64] | data[65] << 8


def walk(area):
    """the records of one area (bytes from the area start) -> (records [(offset, id, data)], end offset); [] when the
    area has no magic"""
    if bytes(area[:4]) != MAGIC:
        return [], 0
    out, off = [], 4
    while off + 4 <= len(area):
        h = area[off:off + 4]
        rid = h[1] | (h[2] & 0x0F) << 8
        n = h[2] >> 4 | h[3] << 4
        if off + 4 + n > len(area):
            break
        data = bytes(area[off + 4:off + 4 + n])
        if crc16_xmodem(data) & 0xFF != h[0]:
            break
        out.append((off, rid, data))
        off += 4 + n
    return out, off


def read(image, base=0):
    """a raw flash image (image[0] = flash `base`: 0 for a whole-flash dump, VM_BASE for the VM alone) -> dict with
    'area' (the live area's flash offset or None), 'records' (that area's walk), 'end' (its log end, area-relative)
    and 'latest' {id: data} (last valid record per id)"""
    def area_at(a):
        return image[a - base:a - base + AREA_SIZE]
    live = None
    for a in AREAS:
        if bytes(area_at(a)[:4]) == MAGIC:
            live = a
            break
    if live is None:
        return {"area": None, "records": [], "end": 0, "latest": {}}
    recs, end = walk(area_at(live))
    latest = {}
    for _, rid, data in recs:
        latest[rid] = data
    return {"area": live, "records": recs, "end": end, "latest": latest}


def build_area(records, size=AREA_SIZE):
    """an area image (magic, the records in order, the rest erased) from [(id, data)]"""
    out = bytearray(MAGIC)
    for rid, data in records:
        out += header(rid, data) + bytes(data)
    if len(out) > size:
        raise ValueError("records do not fit the area")
    return bytes(out) + b"\xff" * (size - len(out))


def rewrite(image, area, changes):
    """a copy of a whole-flash image whose live VM records of the ids in changes {id: new data, same length} are
    replaced in place (the check bytes fixed): the capture's perturbed second boot"""
    out = bytearray(image)
    recs, _ = walk(image[area:area + AREA_SIZE])
    seen = set()
    for off, rid, data in recs:
        if rid in changes:
            new = bytes(changes[rid])
            if len(new) != len(data):
                raise ValueError(f"VM record {rid}: a change must keep the length")
            at = area + off
            out[at:at + 4] = header(rid, new)
            out[at + 4:at + 4 + len(new)] = new
            seen.add(rid)
    missing = set(changes) - seen
    if missing:
        raise ValueError(f"VM records {sorted(missing)} not in the log")
    return bytes(out)
