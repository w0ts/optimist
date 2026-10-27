// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Copyright (C) 2026 Chris Farrell (checkUfw: from fm-static, github.com/seajaysec/fm-static, MPL-2.0 by his permission, seajaysec/fm-static#1)
//
// FM-1 .fwsc packages in the browser: the package identity and the image the
// device reads during an update.

const BLOCKS = 20, BLK = 0x30, KEEP = 0x2F;

// the package identity ("FM-1_904"): one marker byte after each of the first 20 blocks
export function productOf(fwsc) {
  if (fwsc.length < BLOCKS * BLK) throw new Error("not an FM-1 package (too short)");
  let s = "";
  for (let i = 0; i < BLOCKS; i++) {
    const m = fwsc[i * BLK + KEEP];
    if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF);
  }
  return s;
}

// the image the device addresses during an update: the .fwsc without the 20 marker bytes
export function logicalImage(fwsc) {
  const out = new Uint8Array(fwsc.length - BLOCKS);
  for (let i = 0; i < BLOCKS; i++) out.set(fwsc.subarray(i * BLK, i * BLK + KEEP), i * KEEP);
  out.set(fwsc.subarray(BLOCKS * BLK), BLOCKS * KEEP);
  return out;
}

const T16 = new Uint16Array(256);
for (let i = 0; i < 256; i++) {
  let c = i << 8;
  for (let k = 0; k < 8; k++) c = c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1;
  T16[i] = c & 0xFFFF;
}
const crc16 = (d) => { let c = 0; for (let i = 0; i < d.length; i++) c = ((c << 8) & 0xFFFF) ^ T16[((c >> 8) ^ d[i]) & 0xFF]; return c; };
const ufwDec = (b, n) => { for (let i = 0, k = 0xFFFF; i < n; i++) { b[i] ^= k & 0xFF; k = ((k << 1) ^ (k & 0x8000 ? 0x1021 : 0)) & 0xFFFF; } return b; };

// a damaged or wrong file, caught before anything is sent: the UFW header's CRC, its entry list's, and each part's
// (bsp/tools/fm1pkg.py ufw() writes them). Throws an Error saying which; returns nothing when all match
export function checkUfw(image) {
  const rd16 = (b, o) => b[o] | (b[o + 1] << 8);
  const rd32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
  if (image.length < 0x40) throw new Error("not an FM-1 package (too short)");
  const h = ufwDec(image.slice(0, 0x40), 0x40);
  if (crc16(h.subarray(2, 0x40)) !== rd16(h, 0)) throw new Error("not an FM-1 package (header checksum)");
  const n = rd16(h, 8), end = 0x40 + n * 0x50;
  if (!n || end > image.length || crc16(image.subarray(0x40, end)) !== rd16(h, 2)) throw new Error("the package's list of parts is damaged");
  for (let i = 0; i < n; i++) {
    const e = ufwDec(image.slice(0x40 + i * 0x50, 0x90 + i * 0x50), 0x50);
    const off = rd32(e, 8), len = rd32(e, 12);
    if (off + len > image.length || crc16(image.subarray(off, off + len)) !== rd16(e, 4))
      throw new Error(`the package is damaged (part ${rd16(e, 0)} checksum)`);
  }
}

// the UFW entries of a logical image, by type (0 flash.bin, 100 ota.bin): {off, len}. The header and the
// entry list are ciphered with key 0xFFFF (felucca-bsp chip/ota.c ota_jl_enc); the CRCs are not checked here
export function ufwEntries(image) {
  const dec = (off, n) => ufwDec(image.slice(off, off + n), n);
  const rd16 = (b, o) => b[o] | (b[o + 1] << 8);
  const rd32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
  const out = {};
  const n = Math.min(rd16(dec(0, 0x40), 8), 11);
  for (let i = 0; i < n; i++) {
    const e = dec(0x40 + i * 0x50, 0x50);
    out[rd16(e, 0)] = { off: rd32(e, 8), len: rd32(e, 12) };
  }
  return out;
}

// M-VAVE's stock files (the .fwsc as downloaded) whose SHA-256 is pinned, by the product they carry. A file that says it is
// one of these must be that very file, byte for byte: the digest covers every file in it, the loader and the layout, so
// nothing else needs parsing. Other versions of M-VAVE's firmware are not pinned (checkUfw and the FM-1 itself check them)
export const STOCK_FILES = {
  "FM-1_015": { size: 699956, sha256: "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a" },
};

export async function sha256hex(bytes) {
  const d = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}

// a stock file by its checksum: true when its product is pinned and the size and SHA-256 are the pinned ones, false when
// the product has no pinned checksum; throws an Error (code "modified") when it is pinned and the file differs
export function stockChecksum(product, size, sha256) {
  const pin = STOCK_FILES[product];
  if (!pin) return false;
  if (size !== pin.size || sha256 !== pin.sha256) {
    const e = new Error(`the file says it is ${product} but it is not M-VAVE's file (size or SHA-256 differs)`);
    e.code = "modified";
    throw e;
  }
  return true;
}
