// SPDX-License-Identifier: GPL-3.0-only
// A simulated FM-1 on Web MIDI, for the tests only (never shipped: make_site.py does not copy it): its identity, then the
// update's "device asks, host answers" reads of the image (step 1: the running firmware; step 2: the update loader),
// then the new identity. web/test_web.mjs drives web/fm1ota.js with it in Node; web/e2e_installer.mjs puts it behind
// navigator.requestMIDIAccess in a headless Chrome.
import { pack7, unpack7 } from "../fm1ota.js";

const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

export class FakeFM1 {
  // identity: what the running firmware reports; loaderIdentity / finalIdentity: after step 1 / step 2.
  // unplugAfter: the port disappears after that many answered reads. writeBlocks: the 512 B reads of step 2 (default 6).
  // plugged: false starts with no port (plug() adds it).
  constructor(image, { unplugAfter = Infinity, finalIdentity = "FM-1_900", identity = "FM-1_015", loaderIdentity = "ota-FM-1_900",
                       writeBlocks = 6, plugged = true, name = "FM-1" } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0; this.writeBlocks = writeBlocks;
    this.finalIdentity = finalIdentity; this.loaderIdentity = loaderIdentity;
    this.access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
    this.identity = identity; this.name = name;
    if (plugged) this.boot(identity, name);
  }
  boot(identity, name) {
    this.unplug(false);
    this.identity = identity; this.waiting = null; this.queue = [];
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
    this.changed();
  }
  plug(identity = this.identity, name = this.name) { this.boot(identity, name); }
  unplug(notify = true) {
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    if (notify) this.changed();
  }
  changed() { const f = this.access.onstatechange; if (f) setTimeout(() => f({}), 1); }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: this.writeBlocks }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (!this.waiting && this.identity === "FM-1_015" && [33, 34].includes(unpack7(d.slice(1, -1))[2])) {
      this.bankCmd(unpack7(d.slice(1, -1)));
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot(this.loaderIdentity, "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.finalIdentity, "Felucca"), 300);
      else this.next();
    }
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
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

/* a .fwsc that make_site.py and checkPackage accept: the identity in the 20 marker bytes, the loader's marker, filler */
/* a package laid out as tools/fm1pkg_make.py ufw() writes one: the scrambled header and parts list with their CRC-16s,
   flash.bin (type 0) and ota.bin (type 100) with theirs (fm1pkg.js checkUfw passes it), the loader's mark at raw 0x1000,
   the identity in the 20 marker bytes */
const T16 = Array.from({ length: 256 }, (_, i) => {
  let c = i << 8;
  for (let k = 0; k < 8; k++) c = c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1;
  return c & 0xFFFF;
});
const crc16 = (d) => d.reduce((c, b) => ((c << 8) & 0xFFFF) ^ T16[((c >> 8) ^ b) & 0xFF], 0);
const scramble = (p) => { for (let i = 0, k = 0xFFFF; i < p.length; i++) { p[i] ^= k & 0xFF; k = ((k << 1) ^ (k & 0x8000 ? 0x1021 : 0)) & 0xFFFF; } return p; };
export function fakePackage(product = "FM-1_720", size = 0x10000) {
  const img = new Uint8Array(size - 20);
  for (let i = 0; i < img.length; i++) img[i] = (i * 13 + 7) & 0xFF;
  img.set(new TextEncoder().encode("FELUCCA-LOADER-1"), 0x1000 - 20);   /* (raw 0x1000: past the 20 marker bytes) */
  const parts = [[0, 0x400, 0xA000], [100, 0xA400, img.length - 0xA400]];
  const list = new Uint8Array(0x50 * parts.length), lv = new DataView(list.buffer);
  parts.forEach(([type, off, len], i) => {
    lv.setUint16(i * 0x50, type, true); lv.setUint16(i * 0x50 + 2, i, true);
    lv.setUint16(i * 0x50 + 4, crc16(img.subarray(off, off + len)), true);
    lv.setUint32(i * 0x50 + 8, off, true); lv.setUint32(i * 0x50 + 12, len, true);
    scramble(list.subarray(i * 0x50, (i + 1) * 0x50));
  });
  const hdr = new Uint8Array(0x40), hv = new DataView(hdr.buffer);
  hv.setUint32(4, img.length, true); hv.setUint16(8, parts.length, true);
  hv.setUint16(2, crc16(list), true);
  hv.setUint16(0, crc16(hdr.subarray(2)), true);
  img.set(scramble(hdr), 0); img.set(list, 0x40);
  const raw = new Uint8Array(size);
  for (let i = 0; i < 20; i++) {
    raw.set(img.subarray(i * 0x2F, (i + 1) * 0x2F), i * 0x30);
    raw[i * 0x30 + 0x2F] = i < product.length ? (product.charCodeAt(i) + i + 1) & 0xFF : 0x7D;
  }
  raw.set(img.subarray(20 * 0x2F), 20 * 0x30);
  return raw;
}
