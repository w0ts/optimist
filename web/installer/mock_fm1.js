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
  // plugged: false starts with no port (plug() adds it).
  constructor(image, opts = {}) {
    const { unplugAfter = Infinity, finalIdentity = "FM-1_900", identity = "FM-1_015", loaderIdentity = "ota-FM-1_900",
            writeBlocks = 6, plugged = true, name = "FM-1" } = opts;
    Object.assign(this, { image, unplugAfter, finalIdentity, loaderIdentity, writeBlocks, name, identity });
    this.served = 0;
    this.bad = 0;
    this.access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
    if (plugged) this.boot(identity, name);
  }
  boot(identity, name) {
    this.unplug(false);
    Object.assign(this, { identity, waiting: null, queue: [] });
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
    if (this.identity.startsWith("ota-")) {
      const blocks = [];
      for (let k = 0; k < this.writeBlocks; k++) blocks.push([k * 512, 512]);
      return [...blocks, [0xF0000000, 8]];
    }
    return [[0, 64], [0x40, 160], [0x1000, 512], [FLASH_TOP, 8]];
  }
  rx(msg) {
    if (sameBytes(msg, HANDSHAKE)) return this.reportIdentity();
    if (sameBytes(msg, UPGRADE_REQ)) {
      this.queue = this.readPlan();
      return this.next();
    }
    const payload = unpack7(msg.slice(1, -1));
    if (!this.waiting && this.identity === "FM-1_015" && (payload[2] === 33 || payload[2] === 34)) return this.bankCmd(payload);
    if (this.waiting) this.checkAnswer(payload);
  }
  checkAnswer(payload) {
    const [addr, len] = this.waiting;
    const isToken = addr >= FLASH_TOP;
    const got = payload.slice(14, 14 + (isToken ? 8 : len));
    const expected = isToken
      ? [...new TextEncoder().encode("success"), 0]
      : Array.from(this.image.subarray(addr, addr + len));
    if (!sameBytes(got, expected)) this.bad++;
    this.waiting = null;
    this.served++;
    if (this.served >= this.unplugAfter) {
      this.input.state = "disconnected";
      this.output.state = "disconnected";
      return;
    }
    if (addr === FLASH_TOP) later(() => this.boot(this.loaderIdentity, "Felucca Update"), LOADER_WAIT_MS);
    else if (addr === 0xF0000000) later(() => this.boot(this.finalIdentity, "Felucca"), LOADER_WAIT_MS);
    else this.next();
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
    const [addr, len] = req;
    const addrBytes = [0, 8, 16, 24].map((sh) => (addr >>> sh) & 0xFF);
    const frame = [0, 0x59, 0x30, 0, 0, 0, 0, ...addrBytes, len & 0xFF, len >> 8, 0];
    const sum = frame.slice(6, 14).reduce((acc, v) => acc + v, 0);
    frame.push(~sum & 0xFF);
    this.sysex(frame);
  }
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
