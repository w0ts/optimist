// SPDX-License-Identifier: GPL-3.0-only
// A simulated FM-1 on Web MIDI, for the tests only (never shipped: make_site.py does not copy it): its identity, then the
// update's "device asks, host answers" reads of the image (step 1: the running firmware; step 2: the update loader),
// then the new identity. web/test_web.mjs drives web/fm1base/fm1ota.js with it in Node; web/e2e_installer.mjs puts it behind
// navigator.requestMIDIAccess in a headless Chrome.
import { pack7, unpack7 } from "../fm1base/fm1ota.js";

// The host's two requests: the identity handshake and the "upgrade" switch into the update loader.
const HANDSHAKE = Uint8Array.of(0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7);
const UPGRADE_REQ = Uint8Array.of(0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7);
const sameBytes = (x, y) => x.length === y.length && x.every((v, k) => v === y[k]);
const later = (fn, ms = 1) => setTimeout(fn, ms);
const LOADER_WAIT_MS = 300;
const FLASH_TOP = 0xE0000000;   // addresses from here on are the loader's "success" token reads

export class FakeFM1 {
  // identity: what the running firmware reports; loaderIdentity / finalIdentity: after step 1 / step 2.
  // unplugAfter: the port disappears after that many answered reads. writeBlocks: the 512 B reads of step 2 (default 6).
  // plugged: false starts with no port (plug() adds it). fast: the loader answers the fast install's word and reads the
  // table, then every whole 4 KiB sector (docs/INSTALL-SPEED.md); fastWords counts the words it heard.
  constructor(image, opts = {}) {
    const { unplugAfter = Infinity, finalIdentity = "FM-1_900", identity = "FM-1_015", loaderIdentity = "ota-FM-1_900",
            writeBlocks = 6, plugged = true, name = "FM-1", fast = false, comesBack = [], checkFrames = false, checkTime = 0,
            keepsSession = false } = opts;
    Object.assign(this, { image, unplugAfter, finalIdentity, loaderIdentity, writeBlocks, name, identity, fast });
    /* comesBack: [[identity, name, ms, more reads before the next drop or 0]], one per drop: the cable back by itself.
       checkTime: ms the fast loader checks each sector before it asks one (or the end); checkFrames: it says so with 0x32
       frames [checked, total] (checkSeen). keepsSession: the loader stays in its fast session over a replug (the FM-1,
       2026-11-01): a new hello and upgrade command mid-session go unheard (unheard), it asks its read again */
    Object.assign(this, { comesBack: [...comesBack], checkFrames, checkTime, keepsSession, inSession: false, unheard: 0, checked: 0, checkSeen: [] });
    this.fastWords = 0;
    this.fastOn = false;
    this.written = new Set();   /* the fast loader's sectors already right: a resumed write skips them (sectorReads: 4 KiB reads answered) */
    this.sectorReads = 0;
    this.served = 0;
    this.bad = 0;
    this.access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
    if (plugged) this.boot(identity, name);
  }
  boot(identity, name) {
    Object.assign(this, { waiting: null, queue: [] });
    this.ports(identity, name);
  }
  ports(identity, name) {
    this.unplug(false);
    this.identity = identity;
    const tag = Math.random().toString(36).slice(2);
    const connected = (prefix) => ({ id: prefix + tag, name, state: "connected", open: async () => {} });
    this.input = { ...connected("i"), onmidimessage: null };
    this.output = {
      ...connected("o"),
      send: (data) => {
        if (this.output.state === "disconnected") throw new Error("InvalidStateError");
        later(() => this.rx(Array.from(data)));
      },
    };
    for (const [group, port] of [[this.access.inputs, this.input], [this.access.outputs, this.output]]) group.set(port.id, port);
    this.changed();
  }
  plug(identity = this.identity, name = this.name) { this.boot(identity, name); }
  unplug(notify = true) {
    for (const group of [this.access.inputs, this.access.outputs]) {
      group.forEach((port) => { port.state = "disconnected"; });
      group.clear();
    }
    if (notify) this.changed();
  }
  changed() {
    const handler = this.access.onstatechange;
    if (handler) later(() => handler({}));
  }
  tx(bytes) {
    const port = this.input;
    later(() => {
      if (port.state === "connected" && port.onmidimessage) port.onmidimessage({ data: Uint8Array.from(bytes) });
    });
  }
  sysex(body) { this.tx([0xF0, ...pack7(body), 0xF7]); }
  reportIdentity() {
    const text = Array.from(new TextEncoder().encode(this.identity));
    const padded = text.concat(new Array(28 - text.length).fill(0));
    this.sysex([0, 0x59, 0x11, 0, 0, 0, ...padded]);
  }
  readPlan() {
    if (this.identity.startsWith("ota-") && this.fastOn) {
      const n = Math.floor(this.image.length / 4096), plan = [[0xD0000000, n * 6]];
      for (let k = 0; k < n; k++) if (!this.written.has(k * 4096)) plan.push([k * 4096, 4096]);
      return [...plan, [0xF0000000, 8]];
    }
    if (this.identity.startsWith("ota-")) {
      const blocks = [];
      for (let k = 0; k < this.writeBlocks; k++) blocks.push([k * 512, 512]);
      return [...blocks, [0xF0000000, 8]];
    }
    return [[0, 64], [0x40, 160], [0x1000, 512], [FLASH_TOP, 8]];
  }
  rx(msg) {
    if (sameBytes(msg, [0xFC])) { this.stops = (this.stops || 0) + 1; if (!this.upgradesSeen) this.stopsFirst = (this.stopsFirst || 0) + 1; return; }   /* MIDI STOP: counted, and whether it came before the first upgrade command */
    if (sameBytes(msg, [0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])) { this.softKeys = (this.softKeys || 0) + 1; return; }   /* the download-mode soft key: counted, nothing else */
    if (sameBytes(msg, HANDSHAKE)) return this.reportIdentity();
    const word = msg.length > 2 && unpack7(msg.slice(1, -1));
    if (this.inSession && this.keepsSession && (sameBytes(msg, UPGRADE_REQ) || (word.length === 10 && word[2] === 0x31))) { this.unheard++; return; }
    if (sameBytes(msg, UPGRADE_REQ)) {
      this.upgradesSeen = (this.upgradesSeen || 0) + 1;
      this.checked = 0;
      this.inSession = this.identity.startsWith("ota-") && this.fastOn;
      this.queue = this.readPlan();
      return this.next();
    }
    const payload = unpack7(msg.slice(1, -1));
    if (payload.length === 10 && payload[2] === 0x31) {          /* the fast install's word: a fast loader answers */
      this.fastWords++;
      if (this.fast && this.identity.startsWith("ota-")) {
        this.fastOn = true;
        this.sysex([0, 0x59, 0x31, 3, 0, 0, 1, 0, 16, ~17 & 0xFF]);
      }
      return;
    }    if (!this.waiting && this.identity === "FM-1_015" && (payload[2] === 33 || payload[2] === 34)) return this.bankCmd(payload);
    if (this.waiting) this.checkAnswer(payload);
  }
  checkAnswer(payload) {
    const [addr, len] = this.waiting;
    if (payload[2] === 0x30 && payload.length > 11 && ((payload[7] | (payload[8] << 8) | (payload[9] << 16) | (payload[10] << 24)) >>> 0) !== addr) return;   /* (an answer to a read asked before: ignored, as ota_read does) */
    const isToken = addr >= FLASH_TOP;
    const got = payload.slice(14, 14 + (isToken ? 8 : len));
    const expected = isToken
      ? [...new TextEncoder().encode("success"), 0]
      : addr >= 0xD0000000 ? fakeTable(this.image, addr - 0xD0000000, len / 6)
      : Array.from(this.image.subarray(addr, addr + len));
    if (!sameBytes(got, expected)) this.bad++;
    this.waiting = null;
    this.served++;
    if (this.fastOn && addr < 0xD0000000 && len === 4096) { this.sectorReads++; this.written.add(addr); }
    if (this.served >= this.unplugAfter) {
      this.input.state = "disconnected";
      this.output.state = "disconnected";
      const b = this.comesBack.shift();
      if (b) later(() => this.back(...b), b[2]);
      return;
    }
    if (addr === FLASH_TOP) later(() => this.boot(this.loaderIdentity, "Felucca Update"), LOADER_WAIT_MS);
    else if (addr === 0xF0000000) { this.inSession = false; later(() => this.boot(this.finalIdentity, "Felucca"), LOADER_WAIT_MS); }
    else this.advance();
  }
  // the cable back by itself; a loader that keeps its session asks its read again (ota_read's tries), else it starts afresh
  back(identity, name, ms, more) {
    this.unplugAfter = more ? this.served + more : Infinity;
    if (!(this.keepsSession && this.inSession)) return this.boot(identity, name);
    this.ports(identity, name);
    this.reask(null, 0);
  }
  reask(req, tries) {
    if (!this.inSession || tries > 40 || this.input.state !== "connected") return;
    if (req === null && !this.waiting) this.advance();
    else if (req !== null && this.waiting === req) this.request(req);
    else if (req !== null) return;
    later(() => this.reask(this.waiting, tries + 1), 200);
  }
  // the fast loader checks the sectors up to the next one it asks (all of them before the end), then asks
  advance() {
    if (!(this.fastOn && this.identity.startsWith("ota-") && this.queue.length && (this.checkTime || this.checkFrames))) return this.next();
    const addr = this.queue[0][0], total = Math.floor(this.image.length / 4096);
    const upto = addr < 0xD0000000 ? Math.floor(addr / 4096) : total, port = this.input;
    const step = (c) => {
      if (port.state !== "connected") return;
      if (c >= upto) { this.checked = upto + 1; this.next(); return; }
      if (this.checkFrames) {
        this.checkSeen.push(c + 1);
        const body = [(c + 1) & 0xFF, (c + 1) >> 8, total & 0xFF, total >> 8];
        this.sysex([0, 0x59, 0x32, 4, 0, 0, ...body, ~body.reduce((x, y) => x + y, 0) & 0xFF]);
      }
      later(() => step(c + 1), this.checkTime);
    };
    later(() => step(this.checked), this.checkTime);
  }
  // the stock V15's voice-bank commands (fm1ota.js restoreBanks): 33 erases a 4 KiB bank (0xFF), 34 writes 512 B;
  // each acknowledged with command 0, data 0. banks: the 16 KiB as written; bankOps: the commands in order
  bankCmd(u) {
    this.banks = this.banks || new Uint8Array(16384).fill(0x5A);
    this.bankOps = this.bankOps || [];
    const p = u.slice(6, -1), off = p[1] | (p[2] << 8);
    this.bankOps.push(u[2]);
    if (u[2] === 33) this.banks.fill(0xFF, off, off + 4096);
    else this.banks.set(p.slice(8, 520), off);
    this.tx([0xF0, ...pack7([0, 0x59, 0, 1, 0, 0, 0, 0xFF]), 0xF7]);
  }
  next() {
    const req = this.queue.shift();
    if (!req) return;
    this.waiting = req;
    this.request(req);
  }
  request(req) {
    const [addr, len] = req;
    const addrBytes = [0, 8, 16, 24].map((sh) => (addr >>> sh) & 0xFF);
    const frame = [0, 0x59, 0x30, 0, 0, 0, 0, ...addrBytes, len & 0xFF, len >> 8, 0];
    const sum = frame.slice(6, 14).reduce((acc, v) => acc + v, 0);
    frame.push(~sum & 0xFF);
    this.sysex(frame);
  }
}

/* the fast install's table as a loader expects it: per 4 KiB sector CRC16 (XMODEM) and CRC-32 (zlib), little-endian */
function fakeTable(image, off, n) {
  const c32 = (d) => { let c = 0xFFFFFFFF; for (const b of d) { c ^= b; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xEDB88320 : c >>> 1; } return (c ^ 0xFFFFFFFF) >>> 0; };
  const out = [];
  for (let k = 0; k < n; k++) {
    const s = image.subarray(off + k * 4096, off + (k + 1) * 4096), a = crc16(s), b = c32(s);
    out.push(a & 0xFF, a >> 8, b & 0xFF, (b >>> 8) & 0xFF, (b >>> 16) & 0xFF, b >>> 24);
  }
  return out;
}

/* a .fwsc that make_site.py and checkPackage accept: the identity in the 20 marker bytes, the loader's marker, filler, and
   one UFW part with the header and checksums fm1base's checkUfw wants (the header and the entry list ciphered with the
   key 0xFFFF; CRC-16 0x1021) */
const crc16 = (d) => { let c = 0; for (const b of d) { c ^= b << 8; for (let k = 0; k < 8; k++) c = (c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1) & 0xFFFF; } return c; };
const cipher = (b) => { for (let i = 0, k = 0xFFFF; i < b.length; i++) { b[i] ^= k & 0xFF; k = ((k << 1) ^ (k & 0x8000 ? 0x1021 : 0)) & 0xFFFF; } return b; };
export function fakePackage(product = "FM-1_720", size = 0x10000) {
  const img = new Uint8Array(size - 20);
  for (let i = 0; i < img.length; i++) img[i] = (i * 13 + 7) & 0xFF;
  img.set(new TextEncoder().encode("FELUCCA-LOADER-1"), 0x1000 - 20);
  const off = 0x90, part = img.subarray(off);
  const entry = new Uint8Array(0x50);
  new DataView(entry.buffer).setUint16(4, crc16(part), true);
  new DataView(entry.buffer).setUint32(8, off, true);
  new DataView(entry.buffer).setUint32(12, part.length, true);
  img.set(cipher(entry), 0x40);
  const head = new Uint8Array(0x40), hv = new DataView(head.buffer);
  hv.setUint16(2, crc16(img.subarray(0x40, 0x90)), true);
  hv.setUint16(8, 1, true);
  hv.setUint16(0, crc16(head.subarray(2)), true);
  img.set(cipher(head), 0);
  const raw = new Uint8Array(size);
  for (let i = 0; i < 20; i++) { raw.set(img.subarray(i * 0x2F, i * 0x2F + 0x2F), i * 0x30); raw[i * 0x30 + 0x2F] = i < product.length ? (product.charCodeAt(i) + i + 1) & 0xFF : 0x7D; }
  raw.set(img.subarray(20 * 0x2F), 20 * 0x30);
  return raw;
}
