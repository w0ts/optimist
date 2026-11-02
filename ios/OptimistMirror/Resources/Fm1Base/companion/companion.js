// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// The companion services' client (docs/COMPANION.md, device/companion.c): what any fm1-base firmware built with
// companion = true is and has, its files and its parameters, over a MIDI port pair (Web MIDI's, or the browser
// emulator's: web/emu/parts/port_client.js). tools/fm1_companion.py is the same in Python.
//   const c = new Companion(input, output);   // input: addEventListener("midimessage"), output: send(bytes)
//   await c.hello(); await c.files(); await c.read(i, onProgress); await c.write(i, bytes, onProgress);
//   await c.params(); await c.get(ids); await c.set(id, v); await c.watch(true); c.onchange = (id, v) => ...
// One request at a time (the device keeps one frame): calls queue. A request not answered in `timeout` ms is sent
// again, `tries` times in all.

export const HELLO = 0x60, FILES = 0x61, PARAMS = 0x62;
export const SERVICES = ["capability query", "files", "parameters", "backup", "mirror", "screen reader",
  "Bluetooth MIDI", "WiFi", "USB audio in", "USB audio out", "the app's own SysEx", "the app's own commands",
  "the second core", "USB MIDI host", "Bluetooth audio"];
export const STATUS = ["ok", "busy (the app plays: stop it first)", "no such file", "not allowed", "out of range",
  "CRC-32 mismatch", "the flash refused", "out of order", "a malformed request", "an unknown operation"];
export const KINDS = ["other", "project", "preset", "sample", "bank", "pattern", "settings", "text", "log"];
export const F_READ = 1, F_WRITE = 2, F_DELETE = 4, F_PLAYING = 8;
const OP = { list: 0, read: 1, sum: 2, begin: 3, data: 4, commit: 5, delete: 6 };
const PQ = { info: 0, labels: 1, group: 2, get: 3, set: 4, watch: 5, changed: 6 };

export function pack7(data) {
  const out = [];
  let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb;
    nb += 8;
    while (nb >= 7) { out.push(acc & 0x7f); acc >>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7f);
  return out;
}
export function unpack7(s) {
  const out = [];
  let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7f) << nb;
    nb += 7;
    while (nb >= 8) { out.push(acc & 0xff); acc >>= 8; nb -= 8; }
  }
  return Uint8Array.from(out);
}
// the update's framing: F0 pack7(00 59 cmd len24 body ~sum8(body)) F7
export function frame(cmd, body = []) {
  const n = body.length;
  let s = 0;
  for (const b of body) s += b;
  return Uint8Array.from([0xf0, ...pack7([0, 0x59, cmd, n & 0xff, n >> 8 & 0xff, n >> 16 & 0xff, ...body, ~s & 0xff]), 0xf7]);
}
export function decode(msg) {                 // -> { cmd, body } or null
  if (msg.length < 3 || msg[0] !== 0xf0 || msg[msg.length - 1] !== 0xf7) return null;
  const u = unpack7(msg.subarray(1, msg.length - 1));
  if (u.length < 7 || u[0] !== 0 || u[1] !== 0x59) return null;
  const n = u[3] | u[4] << 8 | u[5] << 16;
  if (u.length < n + 7) return null;
  let s = 0;
  for (let i = 0; i < n; i++) s += u[6 + i];
  if ((~s & 0xff) !== u[6 + n]) return null;
  return { cmd: u[2], body: u.slice(6, 6 + n) };
}

const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();
export function crc32(bytes, c = 0) {          // zlib's
  c = ~c >>> 0;
  for (const b of bytes) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
  return ~c >>> 0;
}

const le = {
  u16: (b, o) => b[o] | b[o + 1] << 8,
  u32: (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0,
  i32: (b, o) => b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24,
  p16: (v) => [v & 0xff, v >> 8 & 0xff],
  p32: (v) => [v & 0xff, v >> 8 & 0xff, v >> 16 & 0xff, v >>> 24 & 0xff],
};
function strings(b, o, k) {                    // k NUL-terminated strings from b at o -> [[...], next]
  const out = [];
  for (let i = 0; i < k; i++) {
    let z = o;
    while (z < b.length && b[z]) z++;
    out.push(new TextDecoder().decode(b.subarray(o, z)));
    o = z + 1;
  }
  return [out, o];
}

export class CompanionError extends Error {
  constructor(code, msg) { super(msg); this.code = code; }
}

export class Companion {
  constructor(input, output, { timeout = 1000, tries = 3 } = {}) {
    this.output = output;
    this.timeout = timeout;
    this.tries = tries;
    this.waiting = null;                       // { cmd, match, resolve }
    this.chain = Promise.resolve();
    this.onchange = null;                      // (id, value): a watched parameter changed on the FM-1
    this.listener = (e) => this.message(e.data);
    input.addEventListener("midimessage", this.listener);
    this.input = input;
  }
  close() { this.input.removeEventListener("midimessage", this.listener); }
  message(data) {
    const a = decode(data instanceof Uint8Array ? data : Uint8Array.from(data));
    if (!a) return;
    if (a.cmd === PARAMS && a.body[0] === PQ.changed) {
      for (let k = 0; k < a.body[2]; k++) {
        const o = 3 + 6 * k;
        if (this.onchange) this.onchange(le.u16(a.body, o), le.i32(a.body, o + 2));
      }
      return;
    }
    const w = this.waiting;
    if (w && a.cmd === w.cmd && (!w.match || w.match(a.body))) {
      this.waiting = null;
      w.resolve(a.body);
    }
  }
  // one request -> its answer's body (queued behind the others)
  request(cmd, body = [], match = null) {
    const run = async () => {
      for (let t = 0; t < this.tries; t++) {
        const got = await new Promise((resolve) => {
          this.waiting = { cmd, match, resolve };
          this.output.send(frame(cmd, body));
          setTimeout(() => { if (this.waiting && this.waiting.resolve === resolve) { this.waiting = null; resolve(null); } },
            this.timeout);
        });
        if (got) return got;
      }
      throw new CompanionError("silent", `no answer to 0x${cmd.toString(16)}: no companion services, or it stopped answering`);
    };
    const p = this.chain.then(run, run);
    this.chain = p.catch(() => {});
    return p;
  }

  async hello() {
    const b = await this.request(HELLO);
    const bits = le.u32(b, 1);
    const [[identity, name, version, extras], at] = strings(b, 13, 4);
    let backup = null;                          // protocol 2: the backup's format at the answer's end (an older answer has none)
    if (b[0] >= 2 && b.length >= at + 7 && (b[at] | b[at + 1] | b[at + 2] | b[at + 3]))
      backup = { magic: String.fromCharCode(b[at], b[at + 1], b[at + 2], b[at + 3]), version: le.u16(b, at + 4),
        ext: strings(b, at + 6, 1)[0][0] };
    return { backup, version: b[0], bits, services: SERVICES.filter((_, i) => bits >> i & 1), readMax: le.u16(b, 5),
      writeMax: le.u16(b, 7), files: le.u16(b, 9), params: le.u16(b, 11), identity, name, appVersion: version,
      extras: Object.fromEntries(extras.split(";").filter(Boolean).map((kv) => kv.includes("=") ? kv.split(/=(.*)/s).slice(0, 2) : [kv, ""])) };
  }

  // ---------------------------------------------------------------- files
  async fileOp(op, i, body = []) {             // -> [status, the answer after op, status, index]
    const b = await this.request(FILES, [op, ...le.p16(i), ...body], (a) => a[0] === op && le.u16(a, 2) === i);
    return [b[1], b.subarray(4)];
  }
  fail(st, what) {
    throw new CompanionError(st === 1 ? "busy" : st === 2 ? "nofile" : "failed", `${what}: ${STATUS[st] || st}`);
  }
  async files() {
    const out = [];
    for (let i = 0, count = 1; i < count; i++) {
      const [st, b] = await this.fileOp(OP.list, i);
      count = le.u16(b, 0);
      if (st) break;
      const [[name]] = strings(b, 12, 1);
      out.push({ index: i, name, kind: KINDS[b[2]] || String(b[2]), flags: b[3], size: le.u32(b, 4), room: le.u32(b, 8) });
    }
    return out;
  }
  async sum(i) {
    const [st, b] = await this.fileOp(OP.sum, i);
    if (st) this.fail(st, "sum");
    return { size: le.u32(b, 0), crc: le.u32(b, 4) };
  }
  async read(i, onProgress) {                  // the whole file, checked against the FM-1's CRC-32 of it
    const { size, crc } = await this.sum(i);
    const data = new Uint8Array(size);
    let at = 0;
    while (at < size) {
      const [st, b] = await this.fileOp(OP.read, i, [...le.p32(at), ...le.p16(256)]);
      if (st) this.fail(st, `read at ${at}`);
      if (b.length <= 4) throw new CompanionError("failed", `read at ${at}: nothing came`);
      if (le.u32(b, 0) === at) { data.set(b.subarray(4), at); at += b.length - 4; }
      if (onProgress) onProgress(at, size);
    }
    if (crc32(data) !== crc) throw new CompanionError("failed", "read: the CRC-32 differs from the FM-1's");
    return data;
  }
  // the whole file: begin (resumes a write cut short), the data in order, commit -> where it resumed (0: from the start)
  async write(i, data, onProgress, { chunk = 512, stopAfter = null } = {}) {
    const crc = crc32(data);
    let [st, b] = await this.fileOp(OP.begin, i, [...le.p32(data.length), ...le.p32(crc)]);
    if (st) this.fail(st, "begin");
    let next = le.u32(b, 0);
    const resumed = next;
    while (next < data.length) {
      if (stopAfter !== null && next >= stopAfter) return resumed;
      [st, b] = await this.fileOp(OP.data, i, [...le.p32(next), ...data.subarray(next, next + chunk)]);
      if ((st && st !== 7) || b.length < 4) this.fail(st, `data at ${next}`);
      next = le.u32(b, 0);
      if (onProgress) onProgress(next, data.length);
    }
    [st] = await this.fileOp(OP.commit, i);
    if (st) this.fail(st, "commit");
    return resumed;
  }
  async remove(i) {
    const [st] = await this.fileOp(OP.delete, i);
    if (st) this.fail(st, "delete");
  }

  // ----------------------------------------------------------- parameters
  async paramOp(op, body = []) {               // -> [status, the answer after op, status]
    const b = await this.request(PARAMS, [op, ...body], (a) => a[0] === op);
    return [b[1], b.subarray(2)];
  }
  async groups() {
    const out = [];
    for (let g = 0, count = 1; g < count; g++) {
      const [st, b] = await this.paramOp(PQ.group, [g]);
      count = b[1];
      if (st) break;
      out.push(strings(b, 2, 1)[0][0]);
    }
    return out;
  }
  async labels(i, total) {
    const out = [];
    while (out.length < total) {
      const [st, b] = await this.paramOp(PQ.labels, [...le.p16(i), ...le.p16(out.length)]);
      if (st || !b[4]) throw new CompanionError("failed", `labels of parameter ${i}: ${STATUS[st] || st}`);
      out.push(...strings(b, 5, b[4])[0]);
    }
    return out;
  }
  async params() {                              // every parameter, described
    const groups = await this.groups(), out = [];
    for (let i = 0, count = 1; i < count; i++) {
      const [st, b] = await this.paramOp(PQ.info, le.p16(i));
      count = le.u16(b, 2);
      if (st) break;
      const nl = le.u16(b, 21), [[name, unit]] = strings(b, 23, 2);
      out.push({ index: i, id: le.u16(b, 4), type: b[6] === 1 ? "enum" : "number", group: groups[b[7]] ?? String(b[7]),
        flags: b[8], min: le.i32(b, 9), max: le.i32(b, 13), def: le.i32(b, 17), name, unit,
        labels: nl ? await this.labels(i, nl) : [] });
    }
    return out;
  }
  async get(ids) {                              // -> Map(id -> value) for the ids the app has
    const out = new Map();
    for (let k = 0; k < ids.length; k += 32) {
      const part = ids.slice(k, k + 32);
      const [, b] = await this.paramOp(PQ.get, part.flatMap(le.p16));
      for (let j = 0; j < b[0]; j++) out.set(le.u16(b, 1 + 6 * j), le.i32(b, 3 + 6 * j));
    }
    return out;
  }
  async set(id, value) {                        // -> the value now (clamped, or as the app took it)
    const [st, b] = await this.paramOp(PQ.set, [...le.p16(id), ...le.p32(value)]);
    if (st) throw new CompanionError(st === 2 ? "noparam" : "failed", `set ${id}: ${STATUS[st] || st}`);
    return le.i32(b, 2);
  }
  async watch(on = true) {
    const [, b] = await this.paramOp(PQ.watch, [on ? 1 : 0]);
    return !!b[0];
  }
}

// the ports of an access (Web MIDI's, the emulator's) that answer the capability query -> [{ name, input, output,
// hello }], each probed in turn (wait ms for an answer)
export async function findCompanions(access, wait = 400) {
  const found = [];
  for (const input of access.inputs.values()) {
    const output = [...access.outputs.values()].find((o) => o.name === input.name) ||
      [...access.outputs.values()].find((o) => o.name.replace(/\s+\d+$/, "") === input.name.replace(/\s+\d+$/, ""));
    if (!output || /bluetooth/i.test(input.name)) continue;
    const c = new Companion(input, output, { timeout: wait, tries: 1 });
    try {
      found.push({ name: input.name, input, output, hello: await c.hello() });
    } catch (e) {
      // (not an fm1-base firmware with companion services: left out)
    } finally {
      c.close();
    }
  }
  return found;
}
