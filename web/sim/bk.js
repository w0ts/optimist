// SPDX-License-Identifier: GPL-3.0-only
// The editor's backup protocol (web/EDITOR_PROTOCOL.md, firmware/src/io/editor/ed_backup.c), enough to read a device's
// whole user memory (projects, presets, kits, samples) and write it into another: used by the Simulator tab's
// "Load from FM-1" (web/sim/sim.js) to pull a real FM-1's backup over Web MIDI and restore it into the simulator.
// The same frames the sim test drives (tests/sim_test.mjs); here over a transport, with timeouts.
const HDR = [0xF0, 0x7D, 0x46, 0x4C];
const CMD = { LIST: 43, READ: 44, BEGIN: 45, DATA: 46, COMMIT: 47, END: 48 };

const u7n = (v, n) => Array.from({ length: n }, (_, k) => (v >>> (7 * k)) & 0x7F);
const fromU7 = (a, n) => a.slice(0, n).reduce((s, v, k) => s + v * 2 ** (7 * k), 0);
const pack7 = (d) => { const o = []; for (let i = 0; i < d.length; i += 7) { const g = d.slice(i, i + 7); o.push(g.reduce((t, v, j) => t | ((v >> 7) << j), 0), ...g.map((v) => v & 0x7F)); } return o; };
const unpack7 = (p) => { const o = []; for (let i = 0; i < p.length; i += 8) { const t = p[i]; for (let j = 1; j < 8 && i + j < p.length; j++) o.push(p[i + j] | (((t >> (j - 1)) & 1) << 7)); } return o; };
const CRCT = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
export const crc32 = (d) => { let c = 0xFFFFFFFF; for (const v of d) c = CRCT[(c ^ v) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };

/* a SysEx request/reply client over a transport: send(bytes) puts a message out, and every incoming whole MIDI message
   is handed to feed(bytes). request(cmd,args) resolves with the reply frame's body (after the 4-byte header), or
   rejects on timeout. One request at a time (the firmware serves them from its main loop). */
export function client(send, timeoutMs = 2500) {
  let waiting = null;                                  /* { cmd, resolve, reject, timer } */
  function feed(bytes) {
    if (!waiting || bytes.length < 5 || !HDR.every((hv, i) => bytes[i] === hv)) return;
    if (bytes[4] !== waiting.cmd) return;
    const w = waiting; waiting = null; clearTimeout(w.timer);
    /* the reply body after the 4-byte header AND the command byte (as tests/sim_test.mjs: op first) */
    w.resolve(Array.from(bytes.slice(5, bytes[bytes.length - 1] === 0xF7 ? -1 : undefined)));
  }
  function request(cmd, args = []) {
    return new Promise((resolve, reject) => {
      if (waiting) { reject(new Error("busy")); return; }
      const timer = setTimeout(() => { waiting = null; reject(new Error(`no reply to command ${cmd}`)); }, timeoutMs);
      waiting = { cmd, resolve, reject, timer };
      send(new Uint8Array([...HDR, cmd, ...args, 0xF7]));
    });
  }
  return { feed, request };
}

/* every backup object the device holds: [{ i, tag, kind, flags, len, crc }] (BK_LIST, the editor's command 43) */
export async function listObjects(c) {
  const objs = [];
  for (let first = 0; ;) {
    const r = await c.request(CMD.LIST, [4, first]);
    const n = r[1], f = r[10], count = r[11];
    for (let k = 0, o = 12; k < count; k++, o += 14)
      objs.push({ i: f + k, tag: String.fromCharCode(...r.slice(o, o + 4)).replace(/\0+$/, ""), kind: r[o + 4], flags: r[o + 5], len: fromU7(r.slice(o + 6), 3), crc: fromU7(r.slice(o + 9), 5) });
    first = f + count;
    if (first >= n || !count) return objs;
  }
}

/* one object's bytes (BK_READ, command 44: CRC-checked chunks) */
export async function readObject(c, obj) {
  let data = [];
  for (let off = 0; off < obj.len;) {
    const r = await c.request(CMD.READ, [obj.i, ...u7n(off, 3)]);
    const chunk = unpack7(r.slice(9));
    if (!chunk.length || crc32(chunk) !== fromU7(r.slice(4), 5)) throw new Error(`backup read of ${obj.tag} went wrong`);
    data = data.concat(chunk);
    off += chunk.length;
  }
  return data.slice(0, obj.len);
}

/* write one object into a device (BEGIN 45 / DATA 46 / COMMIT 47). Throws on a non-zero result code */
export async function writeObject(c, obj, data) {
  const begin = await c.request(CMD.BEGIN, [obj.i, ...u7n(data.length, 3), ...u7n(crc32(data), 5)]);
  if (begin[1]) throw new Error(`${obj.tag}: the device refused the restore (${begin[1]})`);
  for (let off = 0; off < data.length; off += 256) {
    const chunk = data.slice(off, off + 256);
    const r = await c.request(CMD.DATA, [obj.i, ...u7n(off, 3), ...u7n(crc32(chunk), 5), ...pack7(chunk)]);
    if (r[4]) throw new Error(`${obj.tag}: a block was refused (${r[4]})`);
  }
  const commit = await c.request(CMD.COMMIT, [obj.i]);
  if (commit[1]) throw new Error(`${obj.tag}: the restore did not commit (${commit[1]})`);
}

/* END (command 48): 1 = restart on the restored memory, 0 = stay. The device reboots on 1 (the sim ends its run) */
export async function end(c, restart) { return c.request(CMD.END, [restart ? 1 : 0]).catch(() => null); }

/* read every writable object from `src` and write it into `dst` (two clients), then END(restart) on dst.
   onProgress({ tag, done, total }) is called as it goes. Objects a device cannot give or take are skipped. */
export async function transfer(src, dst, onProgress) {
  const objs = (await listObjects(src)).filter((o) => o.len > 0);   /* empty objects: nothing to move */
  let done = 0;
  const skipped = [];
  for (const obj of objs) {
    onProgress && onProgress({ tag: obj.tag, done, total: objs.length });
    try {
      const data = await readObject(src, obj);
      await writeObject(dst, obj, data);
      done++;
    } catch (e) {
      skipped.push(obj.tag);                         /* an object the build cannot take (a format it lacks): skipped, not fatal */
    }
  }
  onProgress && onProgress({ tag: "", done, total: objs.length });
  await end(dst, true);
  return { done, skipped };
}
