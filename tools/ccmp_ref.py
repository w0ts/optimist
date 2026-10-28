# SPDX-License-Identifier: GPL-3.0-only
# Independent pure-Python 802.11 CCMP (AES-CCM) reference, to produce a test vector for the C wc_ccmp_encrypt.
# Written from the spec (IEEE 802.11-2016 12.5.3 / RFC 3610), separately from the C, so transcription bugs show up.

SBOX = []


def _init_sbox():
    p = q = 1
    sbox = [0] * 256
    while True:
        p = p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)
        p &= 0xFF
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 0xFF
        if q & 0x80:
            q ^= 0x09
        q &= 0xFF
        xf = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4))
        sbox[p] = (xf ^ 0x63) & 0xFF
        if p == 1:
            break
    sbox[0] = 0x63
    return sbox


def xtime(a):
    return ((a << 1) ^ 0x1B) & 0xFF if a & 0x80 else (a << 1)


def aes128_expand(key):
    rk = list(key)
    rcon = 1
    for i in range(4, 44):
        t = rk[(i - 1) * 4:(i - 1) * 4 + 4]
        if i % 4 == 0:
            t = [SBOX[t[1]] ^ rcon, SBOX[t[2]], SBOX[t[3]], SBOX[t[0]]]
            rcon = xtime(rcon)
        w0 = rk[(i - 4) * 4:(i - 4) * 4 + 4]
        rk += [w0[j] ^ t[j] for j in range(4)]
    return rk


def aes128_encrypt(key, blk):
    rk = aes128_expand(key)
    s = [blk[i] ^ rk[i] for i in range(16)]
    for rnd in range(1, 11):
        s = [SBOX[x] for x in s]
        # ShiftRows (column-major state: index = col*4 + row)
        t = [0] * 16
        for r in range(4):
            for c in range(4):
                t[c * 4 + r] = s[((c + r) % 4) * 4 + r]
        s = t
        if rnd != 10:
            u = [0] * 16
            for c in range(4):
                a = s[c * 4:c * 4 + 4]
                u[c * 4 + 0] = xtime(a[0]) ^ (xtime(a[1]) ^ a[1]) ^ a[2] ^ a[3]
                u[c * 4 + 1] = a[0] ^ xtime(a[1]) ^ (xtime(a[2]) ^ a[2]) ^ a[3]
                u[c * 4 + 2] = a[0] ^ a[1] ^ xtime(a[2]) ^ (xtime(a[3]) ^ a[3])
                u[c * 4 + 3] = (xtime(a[0]) ^ a[0]) ^ a[1] ^ a[2] ^ xtime(a[3])
            s = [x & 0xFF for x in u]
        s = [s[i] ^ rk[rnd * 16 + i] for i in range(16)]
    return bytes(s)


def xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b))


def ccm_encrypt(key, nonce, aad, payload, M=8):
    # Generic CCM (RFC 3610), L = 15 - len(nonce). Returns ciphertext || MIC(M bytes).
    L = 15 - len(nonce)
    # CBC-MAC
    flags = (0x40 if aad else 0) | (((M - 2) // 2) << 3) | (L - 1)
    b0 = bytes([flags]) + nonce + len(payload).to_bytes(L, "big")
    t = aes128_encrypt(key, b0)
    if aad:
        enc = len(aad).to_bytes(2, "big") + aad
        enc += bytes((-len(enc)) % 16)
        for off in range(0, len(enc), 16):
            t = aes128_encrypt(key, xor(t, enc[off:off + 16]))
    pp = payload + bytes((-len(payload)) % 16)
    for off in range(0, len(pp), 16):
        t = aes128_encrypt(key, xor(t, pp[off:off + 16]))
    mic = t[:M]

    def ctr(i):
        a = bytes([L - 1]) + nonce + i.to_bytes(L, "big")
        return aes128_encrypt(key, a)
    s0 = ctr(0)
    ct = bytearray()
    for n, off in enumerate(range(0, len(payload), 16), start=1):
        blk = payload[off:off + 16]
        ct += xor(blk, ctr(n)[:len(blk)])
    return bytes(ct) + xor(mic, s0[:M])


def check_rfc3610():
    # RFC 3610 Packet Vector #1 (M=8, L=2)
    key = bytes.fromhex("C0C1C2C3C4C5C6C7C8C9CACBCCCDCECF")
    nonce = bytes.fromhex("00000003020100A0A1A2A3A4A5")
    aad = bytes.fromhex("0001020304050607")
    payload = bytes.fromhex("08090A0B0C0D0E0F101112131415161718191A1B1C1D1E")
    want = "588C979A61C663D2F066D0C2C0F989806D5F6B61DAC38417E8D12CFDF926E0"
    got = ccm_encrypt(key, nonce, aad, payload, 8).hex().upper()
    assert got == want, "RFC3610 #1 FAIL\n got=%s\nwant=%s" % (got, want)
    print("RFC 3610 #1 OK")


def ccmp_encrypt(tk, a2, pn, keyid, hdr, body):
    # AAD (non-QoS, 3-addr), length 22
    fc = hdr[0] | (hdr[1] << 8)
    fc &= ~0x3800
    fc |= 0x4000
    sc = (hdr[22] | (hdr[23] << 8)) & 0x000F
    ad = bytes([fc & 0xFF, (fc >> 8) & 0xFF]) + hdr[4:22] + bytes([sc & 0xFF, (sc >> 8) & 0xFF])
    assert len(ad) == 22
    nonce = bytes([0]) + a2 + bytes([pn[5], pn[4], pn[3], pn[2], pn[1], pn[0]])
    # CBC-MAC
    b0 = bytes([0x59]) + nonce + bytes([(len(body) >> 8) & 0xFF, len(body) & 0xFF])
    t = aes128_encrypt(tk, b0)
    ab = bytes([0, 22]) + ad[:14]
    t = aes128_encrypt(tk, xor(t, ab))
    ab = ad[14:] + bytes(16 - (len(ad) - 14))
    t = aes128_encrypt(tk, xor(t, ab))
    for off in range(0, len(body), 16):
        blk = body[off:off + 16]
        blk = blk + bytes(16 - len(blk))
        t = aes128_encrypt(tk, xor(t, blk))
    mic = t[:8]
    # CTR
    def ctr(i):
        a = bytes([0x01]) + nonce + bytes([(i >> 8) & 0xFF, i & 0xFF])
        return aes128_encrypt(tk, a)
    s0 = ctr(0)
    ct = bytearray()
    for n, off in enumerate(range(0, len(body), 16), start=1):
        blk = body[off:off + 16]
        ct += xor(blk, ctr(n)[:len(blk)])
    ccmp_hdr = bytes([pn[0], pn[1], 0, (keyid << 6) | 0x20, pn[2], pn[3], pn[4], pn[5]])
    return hdr + ccmp_hdr + bytes(ct) + xor(mic, s0[:8])


def main():
    global SBOX
    SBOX = _init_sbox()
    # self-check AES against FIPS-197 vector
    k = bytes(range(16))
    assert aes128_encrypt(k, bytes(range(16))).hex() == "0a940bb5416ef045f1c39458c653ea5a", aes128_encrypt(k, bytes(range(16))).hex()
    check_rfc3610()
    tk = bytes.fromhex("c97c1f67ce371185514a8a19f2bdd52f")
    a2 = bytes.fromhex("0020e0a69f08")
    pn = bytes([0x01, 0x00, 0x00, 0x00, 0x00, 0x00])  # PN=1, pn[0]=LSB
    hdr = bytes.fromhex("08410000") + bytes.fromhex("112233445566") + a2 + bytes.fromhex("aabbccddeeff") + bytes([0x00, 0x10])
    body = bytes.fromhex("aaaa03000000080045000012") + bytes(range(20))
    out = ccmp_encrypt(tk, a2, pn, 0, hdr, body)
    print("HDR_LEN", len(hdr))
    print("BODY_LEN", len(body))
    print("TK", tk.hex())
    print("A2", a2.hex())
    print("BODY", body.hex())
    print("OUT", out.hex())
    print("OUT_LEN", len(out))


if __name__ == "__main__":
    main()
