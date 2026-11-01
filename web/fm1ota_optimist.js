// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// What Optimist adds to fm-1-base's update code (web/fm1base/fm1ota.js and fm1pkg.js, MPL-2.0, used as they are): the
// identities of the update loaders (ours, M-VAVE's stock one), the return to M-VAVE's official V15 (its pinned file, its four
// voice banks written back to a running V15), and an Updater that keeps the address of the last read for the progress bar.
// Everything else (finding the FM-1 under any port name, the Open firmware's arming, resume's return check, the CRC
// check, m-bootloader) is fm1base's and is called, not copied. docs/WEB.md.
//
// One file for the page and for Node: the imports are single lines so that web/make_site.py can inline the three files in
// one script (their names are not repeated here: no top-level name of fm1base's files is declared again).
import { Updater, IS_MVAVE_LOADER, pack7, unpack7 } from "./fm1base/fm1ota.js";
import { STOCK_FILES, productOf, logicalImage, sha256hex } from "./fm1base/fm1pkg.js";

// the update loader of Optimist ("ota-FM-1_7XX") and of SLOOP / Felucca packages ("ota-FM-1_9XX"): the same loader and
// protocol. A device left in the update mode of another firmware is never written an image of ours: its loader may
// lay the image out differently (fm1base refuses that for M-VAVE's own loaders, this refuses every other one too)
export const OUR_LOADER = (id) => /^ota-FM-1_[79]\d\d$/i.test(id.text);
// the stock update loader (M-VAVE's usb_hid_ota.bin, "ota-FM-1_0XX"): only for writing the official V15 image on a return
// to stock (installer page), never for our packages
export const OFFICIAL_LOADER = (id) => /^ota-FM-1_0\d\d$/i.test(id.text);
// the loaders a return to the official V15 may find: the stock one, or the Open firmware's ("ota-FM1C_"): fm1base's check
export const STOCK_RETURN_LOADER = (id) => OFFICIAL_LOADER(id) || IS_MVAVE_LOADER(id);

const optFail = (code, msg, detail = "") => Object.assign(new Error(msg), { code, detail });

// the fast install's word to the loader -> true when it answers (an older loader ignores the frame)
async function optFastHello(link) {
  link.drain();
  if (!link.send(OPT_FAST_HELLO)) return false;
  const end = Date.now() + 500;
  while (Date.now() < end) {
    const p = await link.read(end - Date.now());
    if (!p) return false;
    if (optParseFast(p) === OPT_FAST_CHUNK) return true;
  }
  return false;
}

/* ------------------------------------------------------------------ the official firmware, for a return to stock */
// After Felucca 1.0.1 web/fm1pkg.js (validateStockPackage; Leo Kuroshita, GPL-3.0-only): only the user's unmodified M-VAVE
// FM-1 V15 file is accepted, and its SHA-256 pins every byte (files, loader, layout), so nothing else needs parsing. The pin
// is fm1base's own (STOCK_FILES): the same digest as tools/fm1_rescue.py.
export const STOCK_V15_SHA256 = STOCK_FILES["FM-1_015"].sha256;
export const STOCK_V15_SIZE = STOCK_FILES["FM-1_015"].size;

export async function validateStockPackage(bytes) {
  if (bytes.length !== STOCK_V15_SIZE || await sha256hex(bytes) !== STOCK_V15_SHA256)
    throw new Error("select the unmodified official FM-1 V15 file (FM-1.fwsc)");
  const product = productOf(bytes);
  if (product !== "FM-1_015") throw new Error("the official file does not say FM-1_015");
  return { product, image: logicalImage(bytes), sha256: STOCK_V15_SHA256 };
}

// ---- the official voice banks, for after a return to stock. After M-VAVE's FM1 Open V13 updater (sdk/WebUpdate.js,
// inspectPackage and restoreFactory; Copyright (c) 2026 Zhuhai Shengke / M-VAVE, GPL-3.0-only): the four DX7 voice banks
// of V15 (16 KiB) are the first bytes of the package's USR entry (type 50), stored scrambled in 32-byte runs. Optimist
// writes its own data where V15 keeps them; the installer writes these back to a running V15 (restoreBanks below).
export const STOCK_BANKS_SHA256 = "ecad0b8620f9d784d46c8da1e3c5de02c40214649ed8dec218026431046aad25";
export const STOCK_BANKS_SIZE = 16384;

const optLe = (p, at, n) => { let v = 0; for (let i = 0; i < n; i++) v += p[at + i] * 2 ** (i * 8); return v; };
// the package's header scrambling: each byte XOR the low byte of a CRC-16 (0x1021) shift register
function optUnscramble(bytes, key = 0xFFFF) {
  const p = Uint8Array.from(bytes);
  let k = key;
  for (let i = 0; i < p.length; i++) { p[i] ^= k & 0xFF; k = ((k << 1) ^ (k & 0x8000 ? 0x1021 : 0)) & 0xFFFF; }
  return p;
}

// the four banks of the official V15 file (validateStockPackage's bytes), checked by their SHA-256
export async function stockBanks(fwsc) {
  const { image } = await validateStockPackage(fwsc);
  const count = optLe(optUnscramble(image.subarray(0, 64)), 8, 2);
  for (let i = 0; i < count; i++) {
    const e = optUnscramble(image.subarray(64 + i * 80, 144 + i * 80));
    const name = String.fromCharCode(...e.subarray(64)).replace(/\0.*$/s, "");
    if (optLe(e, 0, 2) !== 50 || name !== "USR") continue;
    const off = optLe(e, 8, 4), out = new Uint8Array(STOCK_BANKS_SIZE);
    for (let j = 0; j < STOCK_BANKS_SIZE; j += 32)
      out.set(optUnscramble(image.subarray(off + j, off + j + 32), 0x980F ^ ((off + j) >>> 2)), j);
    if (await sha256hex(out) !== STOCK_BANKS_SHA256) break;
    return out;
  }
  throw new Error("the official file's voice banks did not check out");
}

/* ------------------------------------------------------------------ the fast install */
// docs/INSTALL-SPEED.md, the same as tools/fm1_install.py (fast_hello, sector_table) and the app's FlashProtocol: before
// step 2's upgrade command the page says it can serve 4 KiB reads and the per-sector table (cmd 0x31 [1, 4096 LE]); a
// loader that can answers the same frame and then asks only the sectors that differ, 4 KiB at a time, after the table
// at OPT_TABLE + flash.bin's offset (per 4 KiB sector: CRC16 XMODEM, CRC-32 zlib, little-endian). An older loader
// says nothing and gets fm1base's write (today's protocol: 512-byte reads of the whole package, twice).
const OPT_FAST_CMD = 0x31, OPT_FAST_CHUNK = 4096, OPT_SECTOR = 4096, OPT_TABLE = 0xD0000000;
const optUpgrade = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const optSleep = (ms) => new Promise((r) => setTimeout(r, ms));
export const OPT_FAST_HELLO = optFrame(OPT_FAST_CMD, [1, OPT_FAST_CHUNK & 0xFF, OPT_FAST_CHUNK >> 8]);
// the loader's answer to OPT_FAST_HELLO -> its read size, or 0
export function optParseFast(pkt) {
  if (!pkt || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return 0;
  const r = optParseReply(pkt);
  return r && r.cmd === OPT_FAST_CMD && r.data.length === 3 && r.data[0] === 1 ? r.data[1] | (r.data[2] << 8) : 0;
}
const OPT_T16 = Array.from({ length: 256 }, (_, i) => { let c = i << 8; for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xFFFF : (c << 1) & 0xFFFF; return c; });
const OPT_T32 = Array.from({ length: 256 }, (_, i) => { let c = i; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xEDB88320 : c >>> 1; return c >>> 0; });
export function optCrc16(d) { let c = 0; for (let i = 0; i < d.length; i++) c = ((c << 8) & 0xFFFF) ^ OPT_T16[((c >> 8) ^ d[i]) & 0xFF]; return c; }
export function optCrc32(d) { let c = 0xFFFFFFFF; for (let i = 0; i < d.length; i++) c = (c >>> 8) ^ OPT_T32[(c ^ d[i]) & 0xFF]; return (c ^ 0xFFFFFFFF) >>> 0; }
// the table for n sectors of image from off: 6 bytes each
export function optSectorTable(image, off, n) {
  const out = new Uint8Array(n * 6);
  for (let k = 0; k < n; k++) {
    const s = image.subarray(off + k * OPT_SECTOR, off + (k + 1) * OPT_SECTOR), c16 = optCrc16(s), c32 = optCrc32(s);
    out.set([c16 & 0xFF, c16 >> 8, c32 & 0xFF, (c32 >>> 8) & 0xFF, (c32 >>> 16) & 0xFF, c32 >>> 24], k * 6);
  }
  return out;
}
// the answer to a read request (cmd 0x30): fm1base's response, any length
export function optResponse(addr, data, fl = 0) {
  const len = data.length, body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, (len >> 8) & 0xFF, 0];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  for (const b of data) s += b;
  return [0xF0, ...pack7([...body, ...data, ~s & 0xFF]), 0xF7];
}
function optRequest(pkt) {
  if (!pkt || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}
// the bytes a fast read asks for (the table included), or null outside the package
function optFastData(image, addr, len) {
  if (addr >= OPT_TABLE && addr < 0xE0000000) {
    const off = addr - OPT_TABLE;
    if (len % 6 || len > OPT_FAST_CHUNK || off + (len / 6) * OPT_SECTOR > image.length) return null;
    return optSectorTable(image, off, len / 6);
  }
  return len > OPT_FAST_CHUNK || addr + len > image.length ? null : image.subarray(addr, addr + len);
}

/* ------------------------------------------------------------------ the updater */
// a command to the running stock firmware and its reply (M-VAVE's FM1 Open V13 updater, sdk/WebUpdate.js frame / parse,
// GPL-3.0-only): 00 59, the command, a 24-bit length, the payload, the payload's checksum (~sum), 7-bit packed
function optFrame(cmd, payload) {
  let s = 0;
  for (const b of payload) s += b;
  return [0xF0, ...pack7([0x00, 0x59, cmd, payload.length & 0xFF, payload.length >> 8, 0, ...payload, ~s & 0xFF]), 0xF7];
}
function optParseReply(pkt) {
  const u = unpack7(pkt.slice(1, pkt.length - 1));
  if (u.length < 7 || u[0] !== 0 || u[1] !== 0x59 || (u[3] | (u[4] << 8) | (u[5] << 16)) !== u.length - 7) return null;
  let s = 0;
  for (let i = 6; i < u.length; i++) s += u[i];
  return (s & 0xFF) === 0xFF ? { cmd: u[2], data: u.slice(6, -1) } : null;
}
// the address of a read request the device sent (cmd 0x30), or -1
function optRequestAddr(pkt) {
  if (!pkt || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return -1;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return -1;
  const addr = (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0;
  return addr >= 0xE0000000 ? -1 : addr;
}

// fm1base's Updater, unchanged, with
//  - lastAddr: the address of the last data read the device asked for (fm1base's serve() hands its progress callback the
//    count and the bytes; the page's bar follows the address, as it always did)
//  - resume(image, onStep, product, accept): a loader `accept` does not name is refused (foreign) before anything is written
//  - restoreBanks: the official voice banks back into a running V15
export class OptimistUpdater extends Updater {
  // step 2: the fast install when the loader answers the word (docs/INSTALL-SPEED.md), else fm1base's write. this.fast says
  // which one ran
  async write(ota, image, step) {
    this.fast = await optFastHello(ota.link);
    if (!this.fast) return super.write(ota, image, step);
    ota.link.send(optUpgrade);
    await optSleep(50);
    const s2 = await this.serveFast(ota.link, image, 0xF0000000, 180000, (n, b) => step && step("write", n, b));
    ota.link.close();
    return s2;
  }

  // fm1base's serve for the fast install: 4 KiB reads and the table, no pause before an answer
  async serveFast(link, image, finishAddr, idleMs, onProgress) {
    let served = 0, bytes = 0, last = Date.now();
    for (;;) {
      const pkt = await link.read(1000);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (Date.now() - last > idleMs) return { served, finished: false };
        continue;
      }
      const r = optRequest(pkt);
      if (!r) continue;
      last = Date.now();
      if (r.addr === 0xE0000000 || r.addr === 0xF0000000) {
        link.send(optResponse(r.addr, [...new TextEncoder().encode("success"), 0]));
        if (r.addr === finishAddr) return { served, finished: true };
        continue;
      }
      const data = optFastData(image, r.addr, r.len);
      if (!data) throw optFail("badreq", `bad request ${r.addr.toString(16)}+${r.len}`);
      if (!link.send(optResponse(r.addr, data, r.fl))) return { served, finished: false, lost: true };
      served++;
      if (r.addr < OPT_TABLE) {
        this.lastAddr = r.addr;
        bytes += r.len;
        if (onProgress) onProgress(served, bytes);
      }
    }
  }

  async serve(link, image, finishAddr, idleMs, onProgress) {   // (wraps link.read to remember the last address asked)
    const read = link.read.bind(link);
    link.read = async (ms) => { const p = await read(ms); const a = optRequestAddr(p); if (a >= 0) this.lastAddr = a; return p; };
    try { return await super.serve(link, image, finishAddr, idleMs, onProgress); } finally { link.read = read; }
  }

  // accept: the loaders this image may go to (default: ours, OUR_LOADER; a return to the official V15 -- a product
  // "FM-1_0NN" -- STOCK_RETURN_LOADER). fm1base's own checks follow (M-VAVE's loader and a package that is not M-VAVE's,
  // the FM-1 has to come back running the product)
  async resume(image, onStep, product, accept = /^FM-1_0\d\d$/.test(product || "") ? STOCK_RETURN_LOADER : OUR_LOADER) {
    const ota = await this.find((id) => /^ota-/i.test(id.model));
    if (!ota) return false;
    ota.link.close();
    if (!accept(ota.id)) {
      throw optFail("foreign", `the FM-1 is in the update mode of another firmware (${ota.id.text}): finish that update with its own updater`, ota.id.text);
    }
    return super.resume(image, onStep, product);
  }

  // the official voice banks (stockBanks, 16 KiB) written back to a running V15, as M-VAVE's updater does it
  // (restoreFactory): per 4 KiB bank an erase (command 33), then eight 512 B writes (command 34), each acknowledged
  // (command 0, data 0). onProgress(bytes written). Only an FM-1 that reports FM-1_015.
  async restoreBanks(banks, onProgress) {
    if (!banks || banks.length !== STOCK_BANKS_SIZE) throw optFail("badreq", "not the official voice banks");
    const dev = await this.find((id) => id.text === "FM-1_015");
    if (!dev) throw optFail("notfound", "no FM-1 running the official V15 found");
    const ack = async (cmd, payload) => {
      dev.link.drain();
      if (!dev.link.send(optFrame(cmd, payload))) throw optFail("lost", "the FM-1 was disconnected");
      const end = Date.now() + 5000;
      while (Date.now() < end) {
        const p = await dev.link.read(Math.min(250, end - Date.now()));
        if (!p) { if (dev.link.lost) throw optFail("lost", "the FM-1 was disconnected"); continue; }
        const r = optParseReply(p);
        if (r && r.cmd === 0) {
          if (r.data.length !== 1 || r.data[0] !== 0) throw optFail("stopped", "the FM-1 refused the voice banks");
          return;
        }
      }
      throw optFail("stopped", "the FM-1 stopped answering while the voice banks were written: run it again");
    };
    try {
      for (let bank = 0; bank < STOCK_BANKS_SIZE; bank += 4096) {
        await ack(33, [5, 0, bank >> 8, 0, 0]);
        for (let off = bank; off < bank + 4096; off += 512) {
          await ack(34, [5, off & 0xFF, off >> 8, 0, 0, 0, 2, 0, ...banks.subarray(off, off + 512)]);
          if (onProgress) onProgress(off + 512);
        }
      }
    } finally { dev.link.close(); }
  }
}
