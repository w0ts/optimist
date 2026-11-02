// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Copyright (C) 2026 Chris Farrell (finding the FM-1 under any port name, M-VAVE's Open firmware's identities, resume's
//   return check: from fm-static, github.com/seajaysec/fm-static, by his permission under MPL-2.0:
//   seajaysec/fm-static#1)
//
// The FM-1 USB-MIDI update protocol (M-UPGRADE compatible) over WebMIDI. Ports are found with fm1ports.js.
// Step 1: the running firmware reads parts of the package and stages the update
// loader. Step 2: the loader reads the whole image and writes it. In both steps
// the device sends cmd 0x30 read requests on the logical image and we answer.

import { PORT_NAMES, usualName, livePorts, outputFor, pairs, askAll, IS_BLUETOOTH } from "./fm1ports.js";

const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const MAXDATA = 512;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const IS_OTA = (id) => /^ota-/i.test(id.model);

// M-VAVE's FM1 Open firmware (V10, 2026-10): its updater takes only a package it has been told about
// first (cmd 49 reads its state, cmd 50 arms it with the image length and the package's SHA-256); then
// the update runs as usual. Only M-VAVE's own FM-1 firmware is installed over it: its loader rewrites the
// whole flash, boot head included, which returns the FM-1 to stock.
const OPEN_RE = /^FM1_(OFFICIAL_OPEN|COMMUNITY_\d+)$/;
// M-VAVE's own Open firmware reports "FM-1_80000" and two digits (V14: FM-1_8000014; M-VAVE's Workbench treats it as
// open too); its update loader names itself "ota-FM1C_" and a build id in hex, not digits
const MVAVE_OPEN_RE = /^FM-1_80000\d\d$/;
const OPEN_LOADER_RE = /^ota-FM1C_[0-9a-f]+$/;
// The ports asked (fm1ports.js): a usual name (PORT_NAMES), a port the page allows (allow), one that appears during an
// update (see watch), or one that answers the screen reader's HELLO (answering: every fm1-base firmware does)
// M-VAVE's FM-1 firmware, any version: "FM-1_0" and two digits ("FM-1_016"; fm1-base's dev builds are
// "FM-1_x00", its releases longer; m-bootloader's installer is FM-1_099). Whether a package is safe to write is the device's to check
// (felucca-bsp chip/ota.c: a well-made writer, a bootable boot area), not a list of known files
const IS_STOCK = (product) => /^FM-1_0\d\d$/.test(product) && product !== "FM-1_099";

// the update loader of M-VAVE's own firmware: the stock one ("ota-FM-1_0" and two digits) and the Open firmware's (OPEN_LOADER_RE).
// Their layout of the image may differ from the one this package was made for, so a device found waiting in one of them is
// written only a package of M-VAVE's own firmware (IS_STOCK: the return to the stock firmware), never another firmware's
export const IS_MVAVE_LOADER = (id) => /^ota-FM-1_0\d\d$/i.test(id.text) || OPEN_LOADER_RE.test(id.text);

// errors carry a code the pages translate: notfound, model, stopped, noloader, lost, noreturn,
// mismatch (detail: the identity the device reports), badreq, foreign (detail: the loader's identity)
function fail(code, msg, detail = "") { const e = new Error(msg); e.code = code; e.detail = detail; return e; }

export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

function response(addr, data, fl = 0) {
  const len = data.length;
  const body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    len & 0xFF, (len >> 8) & 0xFF, 0, ...data];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

function parseRequest(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

function parseIdentity(pkt) {
  const d = unpack7(pkt.slice(1, -1));
  if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  const txt = String.fromCharCode(...d.slice(6, 33)).replace(/\0+$/, "");
  if (OPEN_RE.test(txt)) return { text: txt, model: "FM-1", version: 0, open: true };
  if (OPEN_LOADER_RE.test(txt)) return { text: txt, model: "ota-FM1C", version: 0, open: true };
  const m = /^([^_]+)_(\d+)$/.exec(txt);
  if (!m) return null;
  return { text: txt, model: m[1], version: parseInt(m[2], 10), ...(MVAVE_OPEN_RE.test(txt) ? { open: true } : {}) };
}

// the resource region (device/res.c; fm1.toml resources): NAME-res.bin, written after an update as a restore is
// (web/backup): RES_REQ with the payload's length and CRC-32, then the FM-1's reads (the payload's offsets) until it
// reads RES_OK, or RES_FAIL | its code. After Melodee's web/fm1res.js and its installer step (Kerem Kilic, Ellic
// Studio; used in fm1-base under MPL-2.0 with permission)
const RES_REQ = 0x44, RES_MAGIC = 0x31534552, RES_HEAD = 32, RES_OK = 0xC0000000, RES_FAIL = 0xCF000000;
const CRC32_T = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
export function crc32(d) {                                // zlib's
  let c = 0xFFFFFFFF;
  for (let i = 0; i < d.length; i++) c = CRC32_T[(c ^ d[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}
const rd32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const le32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];
// a NAME-res.bin, its header checked (magic, the header's CRC, the payload's length and CRC) -> {payload, crc};
// throws (code "resbad") on anything else (tools/fm1_resources.py payload_of)
export function resPayload(region) {
  const bad = () => fail("resbad", "not a resource file, or a damaged one");
  if (region.length < RES_HEAD || rd32(region, 0) !== RES_MAGIC || rd32(region, 28) !== crc32(region.subarray(0, 28))) throw bad();
  const len = rd32(region, 4), crc = rd32(region, 8), payload = region.subarray(RES_HEAD, RES_HEAD + len);
  if (payload.length !== len || crc32(payload) !== crc) throw bad();
  return { payload, crc };
}

// one MIDI in/out pair with a SysEx queue
class Link {
  constructor(input, output) {
    this.input = input; this.output = output; this.q = []; this.waiter = null;
    input.onmidimessage = (ev) => {
      if (ev.data[0] !== 0xF0) return;
      this.q.push(Array.from(ev.data));
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  // false when the port is gone (unplugged): WebMIDI throws on a disconnected output
  send(bytes) {
    try { this.output.send(bytes); return true; } catch (_) { return false; }
  }
  drain() { this.q.length = 0; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, ms); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  close() { this.input.onmidimessage = null; }
}

async function handshake(link, tries = 3) {
  link.drain();
  for (let t = 0; t < tries; t++) {
    if (!link.send(HS_QUERY)) return null;
    const end = Date.now() + 1000;
    while (Date.now() < end) {
      const p = await link.read(end - Date.now());
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

function frame(cmd, payload = []) {
  let s = 0;
  for (const b of payload) s += b;
  return [0xF0, ...pack7([0x00, 0x59, cmd, payload.length & 0xFF, payload.length >> 8, 0, ...payload, ~s & 0xFF]), 0xF7];
}

// send a command -> the payload of its reply, or null
async function query(link, cmd, payload = []) {
  link.drain();
  if (!link.send(frame(cmd, payload))) return null;
  const end = Date.now() + 1500;
  while (Date.now() < end) {
    const p = await link.read(end - Date.now());
    if (!p) break;
    if (p[0] !== 0xF0 || p[p.length - 1] !== 0xF7) continue;
    const u = unpack7(p.slice(1, -1));
    if (u.length < 7 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== cmd) continue;
    let s = 0;
    for (let i = 6; i < u.length; i++) s += u[i];
    if ((s & 0xFF) === 0xFF) return u.slice(6, 6 + (u[3] | (u[4] << 8)));
  }
  return null;
}

export class Updater {
  constructor(access, log) {
    this.access = access;
    this.log = log || (() => {});
    this.names = PORT_NAMES.slice();        // the usual names (setNames adds the user's own)
    this.allowed = new Set();               // ports the user picked although their names are none of the usual ones
    this.baseline = null;                   // the ports when an update started (watch)
    this.answered = new Set();              // ports found by answering() (a page may remember them, as the user's picks)
  }

  setNames(list) { this.names = [...PORT_NAMES, ...list.map((x) => String(x).trim().toLowerCase()).filter(Boolean)]; }
  allow(name) { this.allowed.add(name); }
  usual(name) { return usualName(name, this.names); }

  // the port names now: an FM-1 restarting into its update loader and back can come back under another name (the loader
  // has another USB id, and macOS names a port from its own cache, by USB id and socket). Until unwatch(), a port that
  // was not there now is asked too
  watch() { this.baseline = new Set(livePorts(this.access.inputs).map((i) => i.name || "")); }
  unwatch() { this.baseline = null; }

  // the port names that have an input and an output, whatever they are called (for a list to pick from)
  ports() { return [...new Set(pairs(this.access).map((p) => p.name))]; }

  // never probe other gear: a usual name, a port the user allowed or that answered a HELLO, or one that appeared during
  // an update
  askable(name) { return !IS_BLUETOOTH(name) && (this.usual(name) || this.allowed.has(name) || (!!this.baseline && !this.baseline.has(name || ""))); }

  // FM-1s on ports of any name: the ones that answer the screen reader's HELLO (fm1ports.js askAll; every fm1-base
  // firmware does), allowed from then on -> their port names (also kept in this.answered)
  async answering() {
    const got = await askAll(this.access, { skip: (n) => this.askable(n) });
    for (const g of got) { this.allow(g.name); this.answered.add(g.name); }
    return got.map((g) => g.name);
  }

  // find(), and when nothing answers on the ports asked, again after asking the others for a HELLO
  async findAny(filter) {
    return (await this.find(filter)) || ((await this.answering()).length ? await this.find(filter) : null);
  }

  // ask one port who it is (allowing it): -> its identity, or null
  async probe(name) {
    this.allow(name);
    const dev = await this.find(null, name);
    if (!dev) return null;
    dev.link.close();
    return dev.id;
  }

  // every in/out pair with the same port name that answers the handshake (only: the port called only)
  async find(filter, only) {
    for (const input of livePorts(this.access.inputs)) {
      if (only != null ? input.name !== only : !this.askable(input.name)) continue;
      const output = outputFor(this.access, input);
      if (!output) continue;
      try { await input.open(); await output.open(); } catch (_) { continue; }
      const link = new Link(input, output);
      const id = await handshake(link, 2);
      if (id && (!filter || filter(id))) return { link, id, name: input.name };
      link.close();
    }
    return null;
  }

  async waitFor(filter, ms) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const dev = await this.find(filter);
      if (dev) return dev;
      await sleep(1000);
    }
    return null;
  }

  // serve read requests until the device asks for one of the finish addresses; stops early
  // when the port disappears (lost: true) instead of waiting out idleMs
  async serve(link, image, finishAddr, idleMs, onProgress) {
    let served = 0, bytes = 0, last = Date.now();
    for (;;) {
      const pkt = await link.read(1000);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (Date.now() - last > idleMs) return { served, finished: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = Date.now();
      if (r.addr === 0xE0000000 || r.addr === 0xF0000000) {
        link.send(response(r.addr, [...new TextEncoder().encode("success"), 0]));
        if (r.addr === finishAddr) return { served, finished: true };
        continue;
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length) throw fail("badreq", `bad request ${r.addr.toString(16)}+${r.len}`);
      await sleep(10);
      if (!link.send(response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.len)), r.fl))) {
        return { served, finished: false, lost: true };
      }
      served++;
      bytes += r.len;
      if (onProgress) onProgress(served, bytes);
    }
  }

  // full install: running firmware -> loader -> new firmware
  async install(image, product, onStep, sha256) {
    const step = onStep || (() => {});
    const dev = await this.findAny((id) => !IS_OTA(id));
    if (!dev) throw fail("notfound", "FM-1 not found (USB cable, and no other app using it?)");
    const [model] = product.split("_");
    if (dev.id.model !== model) { dev.link.close(); throw fail("model", `the device is ${dev.id.text}, the package is for ${product}`); }
    if (dev.id.open) await this.arm(dev, image, product, sha256);
    try { return await this.stages(dev, image, product, step); } finally { this.unwatch(); }
  }

  async stages(dev, image, product, step) {
    this.watch();
    step("start", dev.id.text);
    dev.link.send(UPGRADE);
    await sleep(2000);
    const s1 = await this.serve(dev.link, image, 0xE0000000, 8000, (n, b) => step("verify", n, b));
    dev.link.close();
    if (!s1.finished) throw fail(s1.lost ? "lost" : "stopped", `the device ${s1.lost ? "was disconnected" : "stopped"} after ${s1.served} requests: nothing was written`);
    step("loader");
    await sleep(3000);
    const ota = await this.waitFor(IS_OTA, 30000);
    if (!ota) throw fail("noloader", "the update loader did not appear. Replug the USB cable and press Install again: the device stays in update mode until it is finished.");
    const s2 = await this.write(ota, image, step);
    if (!s2.finished) throw fail(s2.lost ? "lost" : "stopped", `the loader ${s2.lost ? "was disconnected" : "stopped"} after ${s2.served} requests. Replug and press Install again to resume.`);
    step("reboot");
    await sleep(3000);
    const back = await this.waitFor((id) => !IS_OTA(id), 40000);
    if (!back) throw fail("noreturn", "the device did not come back: power-cycle it");
    back.link.close();
    if (back.id.text !== product) throw fail("mismatch", `installed, but the device reports ${back.id.text}`, back.id.text);
    step("done", back.id.text);
    return back.id.text;
  }

  // m-bootloader (github.com/jvitkauskas/m-bootloader, MIT) after an install: the running firmware (product)
  // stages the package's installer (no flash.bin: any well-made writer, felucca-bsp chip/ota.c) and resets; the bootloader
  // runs it, it rewrites the boot area [0, 0x4000) only and restarts into the firmware. It never talks to us:
  // we wait for the firmware to come back. A firmware that does not know it refuses it, and nothing is written.
  async installBoot(image, product, onStep) {
    this.watch();
    try { return await this.bootStages(image, product, onStep || (() => {})); } finally { this.unwatch(); }
  }

  async bootStages(image, product, step) {
    const dev = await this.waitFor((id) => !IS_OTA(id) && id.text === product, 40000);
    if (!dev) throw fail("noreturn", "the device did not come back: power-cycle it");
    step("bootstart");
    dev.link.send(UPGRADE);
    await sleep(2000);
    const s = await this.serve(dev.link, image, 0xE0000000, 8000);
    dev.link.close();
    if (!s.finished) throw fail(s.lost ? "lost" : "bootrefused", `m-bootloader: the device stopped after ${s.served} requests: nothing was written`);
    step("bootwrite");
    await sleep(3000);
    const back = await this.waitFor((id) => !IS_OTA(id), 40000);
    if (!back) throw fail("noreturn", "the device did not come back: power-cycle it");
    back.link.close();
    step("bootdone", back.id.text);
    return back.id.text;
  }

  // M-VAVE's Open firmware: allow the update of M-VAVE's own firmware (sha256: the package file's, hex)
  async arm(dev, image, product, sha256) {
    if (!IS_STOCK(product) || !/^[0-9a-f]{64}$/.test(sha256 || "")) {
      dev.link.close();
      throw fail("open", `This FM-1 runs M-VAVE's ${dev.id.text} firmware. Put the stock firmware back first ("Back to the stock firmware"), then install this.`, dev.id.text);
    }
    const cap = await query(dev.link, 49);
    if (!cap || cap.length !== 47 || String.fromCharCode(...cap.slice(0, 4)) !== "FMC1" || cap[6] !== 0) {
      dev.link.close();
      throw fail("stopped", `${dev.id.text} did not accept the update: nothing was written`);
    }
    const n = image.length;
    const tag = sha256.match(/../g).map((h) => parseInt(h, 16));
    const ack = await query(dev.link, 50, [1, cap[5], n & 0xFF, (n >>> 8) & 0xFF, (n >>> 16) & 0xFF, (n >>> 24) & 0xFF, ...tag]);
    if (!ack || ack.length !== 1 || ack[0] !== 0) {
      dev.link.close();
      throw fail("stopped", `${dev.id.text} refused the package: nothing was written`);
    }
  }

  // the resource region after an install (resPayload's file): once the FM-1 runs product, RES_REQ, then its reads
  // answered -> 0 written (or there already), else the FM-1's code (device/res.c: 2 transfer, 7 erase, 8 write,
  // 10 playing, 20 another build's, 21 the store's move not done, 22 the check). Throws noreturn, lost, stopped, badreq
  async installRes(region, product, onProgress) {
    const { payload, crc } = resPayload(region);
    const dev = await this.waitFor((id) => !IS_OTA(id) && id.text === product, 40000);
    if (!dev) throw fail("noreturn", "the device did not come back: power-cycle it");
    const { link } = dev;
    try {
      link.drain();
      link.send(frame(RES_REQ, [...le32(payload.length), ...le32(crc)]));
      let last = Date.now(), idle = 10000;                /* (the first read after the region's erase) */
      for (;;) {
        const pkt = await link.read(500);
        if (!pkt) {
          if (link.lost) throw fail("lost", "the device was disconnected");
          if (Date.now() - last > idle) throw fail("stopped", "the device stopped asking for the resources");
          continue;
        }
        const r = parseRequest(pkt);
        if (!r) continue;
        last = Date.now(); idle = 10000;
        const top = (r.addr & 0xFF000000) >>> 0;
        if (top === RES_OK || top === RES_FAIL) {
          link.send(response(r.addr, [...new TextEncoder().encode("success"), 0], r.fl));
          return top === RES_OK ? 0 : (r.addr & 0xFFFF) || 99;
        }
        if (r.len > MAXDATA || r.addr + r.len > payload.length) throw fail("badreq", `bad request ${r.addr.toString(16)}+${r.len}`);
        if (!link.send(response(r.addr, Array.from(payload.subarray(r.addr, r.addr + r.len)), r.fl))) throw fail("lost", "the device was disconnected");
        if (onProgress) onProgress(((r.addr + r.len) * 100) / payload.length);
      }
    } finally { link.close(); }
  }

  // step 2 with the loader found by find()/waitFor()
  async write(ota, image, step) {
    ota.link.send(UPGRADE);
    await sleep(2000);
    const s2 = await this.serve(ota.link, image, 0xF0000000, 180000, (n, b) => step("write", n, b));
    ota.link.close();
    return s2;
  }

  // resume: the device is already in update mode (loader) -> true when the write finished. With the product, a loader of
  // M-VAVE's own firmware is refused unless the product is M-VAVE's too (foreign), and the
  // FM-1 has to come back running it (as at the end of install)
  async resume(image, onStep, product) {
    const step = onStep || (() => {});
    const ota = await this.find(IS_OTA);
    if (!ota) return false;
    if (product && !IS_STOCK(product) && IS_MVAVE_LOADER(ota.id)) {
      ota.link.close();
      throw fail("foreign", `the FM-1 is in the update mode of M-VAVE's own firmware (${ota.id.text}) and this package is not for it: nothing was written`, ota.id.text);
    }
    this.watch();
    try {
      if (!(await this.write(ota, image, step)).finished) return false;
      if (product) {
        step("reboot");
        await sleep(3000);
        const back = await this.waitFor((id) => !IS_OTA(id), 40000);
        if (!back) throw fail("noreturn", "the device did not come back: power-cycle it");
        back.link.close();
        if (back.id.text !== product) throw fail("mismatch", `written, but the device reports ${back.id.text}`, back.id.text);
        step("done", back.id.text);
      }
      return true;
    } finally { this.unwatch(); }
  }
}
