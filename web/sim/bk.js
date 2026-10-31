// SPDX-License-Identifier: GPL-3.0-only
// The editor's backup protocol (web/EDITOR_PROTOCOL.md, firmware/src/io/editor/ed_backup.c), enough to read a device's
// whole user memory (projects, presets, kits, samples) and write it into another: used by the Simulator tab's
// "Load from FM-1" (web/sim/sim.js) to pull a real FM-1's backup over Web MIDI and restore it into the simulator.
// The same frames the sim test drives (tests/sim_test.mjs); here over a transport, with timeouts.
const HDR = [0xF0, 0x7D, 0x46, 0x4C];
const CMD = { LIST: 43, READ: 44, BEGIN: 45, DATA: 46, COMMIT: 47, END: 48 };
/* a refusal's result code in words (ed_backup.c BK_RC_*, as web/editor.html's BK.RC) */
const RC = ["ok", "bad arguments", "CRC", "the transport plays", "no flash", "not in this build", "too long", "flash write failed",
  "not valid here (it would not load)"];
const rcText = (rc) => RC[rc] || `code ${rc}`;

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
  function request(cmd, args = [], ms = timeoutMs) {
    return new Promise((resolve, reject) => {
      if (waiting) { reject(new Error("busy")); return; }
      const timer = setTimeout(() => { waiting = null; reject(new Error(`no reply to command ${cmd}`)); }, ms);
      waiting = { cmd, resolve, reject, timer };
      send(new Uint8Array([...HDR, cmd, ...args, 0xF7]));
    });
  }
  return { feed, request };
}

/* every backup object the device holds: [{ i, tag, kind, flags, len, crc }] (BK_LIST, the editor's command 43) */
export async function listObjects(c) { return (await deviceLayout(c)).objs; }

/* what the installed MEMORY layout keeps (ed_backup.c bk_caps, the page that ends BK_LIST): the app end, the project
   slots, the user sample slots (a capacity of 0 = the layout took the slot away). An older firmware lacks the app end
   and the project count: 16 projects and the slots by capacity, as before. r: the reply body, p: where bk_caps starts */
export function layoutCaps(r, p) {
  const usrCap = [0, 1, 2].map((k) => fromU7(r.slice(p + 11 + 3 * k), 3));
  const q = p + 32 + r[p + 31], known = q + 3 <= r.length;
  const projects = known ? r[q + 2] : 16;
  return { usrCap, slots: usrCap.filter((v) => v > 0).length, appEnd: known ? fromU7(r.slice(q), 2) * 4096 : null,
    projects, known, sdCard: known && projects === 1 };
}
/* the list (every page), the firmware's magic (the project family: "FUNB") and the layout's caps */
export async function deviceLayout(c) {
  const objs = [];
  let magic = "", caps = null;
  for (let first = 0; ;) {
    const r = await c.request(CMD.LIST, [4, first]);
    const n = r[1], f = r[10], count = r[11];
    magic = String.fromCharCode(...r.slice(4, 8));
    for (let k = 0, o = 12; k < count; k++, o += 14)
      objs.push({ i: f + k, tag: String.fromCharCode(...r.slice(o, o + 4)).replace(/\0+$/, ""), kind: r[o + 4], flags: r[o + 5], len: fromU7(r.slice(o + 6), 3), crc: fromU7(r.slice(o + 9), 5) });
    first = f + count;
    if (first >= n || !count) {
      const p = 12 + 14 * count;
      if (r.length >= p + 32) caps = layoutCaps(r, p);
      return { objs, magic, caps: caps || { usrCap: [1, 1, 1], slots: 3, appEnd: null, projects: 16, known: false, sdCard: false } };
    }
  }
}

/* one object's bytes (BK_READ, command 44: CRC-checked chunks) */
export async function readObject(c, obj) {
  let data = [];
  for (let off = 0; off < obj.len;) {
    const r = await c.request(CMD.READ, [obj.i, ...u7n(off, 3)]);
    const chunk = unpack7(r.slice(9));
    if (!chunk.length || crc32(chunk) !== fromU7(r.slice(4), 5)) throw new Error("a damaged read (CRC)");
    data = data.concat(chunk);
    off += chunk.length;
  }
  return data.slice(0, obj.len);
}

/* write one object into a device (BEGIN 45 / DATA 46 / COMMIT 47). Throws on a non-zero result code */
export async function writeObject(c, obj, data) {
  /* (BEGIN and COMMIT can take seconds: a restore's first BEGIN puts what is only in RAM into flash first, ed_backup.c;
     the editor waits 4 s; a simulator writes its flash in real time) */
  const begin = await c.request(CMD.BEGIN, [obj.i, ...u7n(data.length, 3), ...u7n(crc32(data), 5)], 10000);
  if (begin[1]) throw new Error(rcText(begin[1]));
  for (let off = 0; off < data.length; off += 256) {
    const chunk = data.slice(off, off + 256);
    const r = await c.request(CMD.DATA, [obj.i, ...u7n(off, 3), ...u7n(crc32(chunk), 5), ...pack7(chunk)], 5000);
    if (r[4]) throw new Error(rcText(r[4]));
  }
  const commit = await c.request(CMD.COMMIT, [obj.i], 10000);
  if (commit[1]) throw new Error(rcText(commit[1]));
}

/* END (command 48): 1 = restart on the restored memory, 0 = stay. The device reboots on 1 (the sim ends its run) */
export async function end(c, restart) { return c.request(CMD.END, [restart ? 1 : 0]).catch(() => null); }

/* the backup file format (web/editor.html backupFile / readBackupFile): "OPTBKUP" 0x01, u32 LE header length, a JSON
   header {format, version, created, device, objects:[{tag, kind, fm6, len, crc, at}]}, the objects' bytes, u32 LE CRC-32
   of all before it. Interchangeable with the editor's and the real device's backups. */
const FILE_MAGIC = [0x4F, 0x50, 0x54, 0x42, 0x4B, 0x55, 0x50, 0x01];
const KINDS = ["st", "usr", "fm6", "sec", "prj", "log", "snp"];

export function packFile(objects, device = {}, created = new Date().toISOString()) {
  let at = 0;
  const hdr = { format: "optimist-backup", version: 1, created, device,
    objects: objects.map((o) => { const x = { tag: o.tag, kind: o.kind, fm6: !!o.fm6, len: o.data.length, crc: crc32(o.data), at }; at += o.data.length; return x; }) };
  const h = new TextEncoder().encode(JSON.stringify(hdr));
  const out = new Uint8Array(12 + h.length + at + 4), dv = new DataView(out.buffer);
  out.set(FILE_MAGIC, 0); dv.setUint32(8, h.length, true); out.set(h, 12);
  let p = 12 + h.length;
  for (const o of objects) { out.set(o.data, p); p += o.data.length; }
  dv.setUint32(p, crc32(out.subarray(0, p)), true);
  return out;
}

export function unpackFile(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (b.length < 16 || FILE_MAGIC.some((c, i) => b[i] !== c)) throw new Error("not an Optimist backup file");
  const dv = new DataView(b.buffer, b.byteOffset, b.length), n = dv.getUint32(8, true), end = b.length - 4;
  if (12 + n > end || dv.getUint32(end, true) !== crc32(b.subarray(0, end))) throw new Error("the backup file is damaged (CRC)");
  const hdr = JSON.parse(new TextDecoder().decode(b.subarray(12, 12 + n)));
  if (!hdr || hdr.format !== "optimist-backup" || hdr.version !== 1 || !Array.isArray(hdr.objects)) throw new Error("not an Optimist backup file");
  const base = 12 + n;
  const objects = hdr.objects.map((o) => {
    if (base + o.at + o.len > end) throw new Error("the backup file is cut short");
    const data = Array.from(b.slice(base + o.at, base + o.at + o.len));
    if (crc32(data) !== o.crc) throw new Error(`object ${o.tag}: CRC`);
    return { tag: String(o.tag), kind: KINDS.includes(o.kind) ? o.kind : "st", fm6: !!o.fm6, data };
  });
  return { device: hdr.device || {}, created: hdr.created, objects };
}

/* read every non-empty object from a device/sim client into a backup file (bytes) */
export async function readToFile(c, device) {
  const objs = (await listObjects(c)).filter((o) => o.len > 0);
  const objects = [];
  for (const obj of objs) objects.push({ tag: obj.tag, kind: KINDS[obj.kind] || "st", fm6: !!(obj.flags & 0x01), data: await readObject(c, obj) });
  return packFile(objects, device);
}

/* write a backup file's objects into a device/sim client (matched to its current objects by tag), then END(restart).
   Objects the build does not have or refuses are skipped. onProgress({tag,done,total}). */
export async function writeFromFile(c, file, onProgress) {
  const have = await listObjects(c);
  const items = file.objects.map((o) => ({ o, d: have.find((x) => x.tag === o.tag) })).filter((x) => x.d);
  let done = 0;
  const skipped = file.objects.filter((o) => !have.some((x) => x.tag === o.tag)).map((o) => ({ tag: o.tag, why: "not in this build" }));
  for (const { o, d } of items) {
    onProgress && onProgress({ tag: o.tag, done, total: items.length });
    try { await writeObject(c, { i: d.i, tag: o.tag }, o.data); done++; } catch (e) { skipped.push({ tag: o.tag, why: e.message }); }
  }
  onProgress && onProgress({ tag: "", done, total: items.length });
  await end(c, true);
  return { done, skipped };
}

/* read every writable object from `src` and write it into `dst` (two clients), then END(restart) on dst.
   onProgress({ tag, done, total }) is called as it goes. Objects a device cannot give or take are skipped. */
export async function transfer(src, dst, onProgress) {
  const objs = (await listObjects(src)).filter((o) => o.len > 0);   /* empty objects: nothing to move */
  const have = await listObjects(dst);                /* matched by TAG: two builds number their objects differently */
  let done = 0;
  const skipped = [];
  for (const obj of objs) {
    onProgress && onProgress({ tag: obj.tag, done, total: objs.length });
    const d = have.find((x) => x.tag === obj.tag);
    if (!d) { skipped.push({ tag: obj.tag, why: "not in this build" }); continue; }
    let data;
    try { data = await readObject(src, obj); } catch (e) { skipped.push({ tag: obj.tag, why: `read: ${e.message}` }); continue; }
    try { await writeObject(dst, { i: d.i, tag: obj.tag }, data); done++; }
    catch (e) { skipped.push({ tag: obj.tag, why: e.message }); }   /* (a format the build cannot take: skipped, not fatal) */
  }
  onProgress && onProgress({ tag: "", done, total: objs.length });
  await end(dst, true);
  return { done, skipped };
}

/* ---- one project from a device to another (command 110 PJ_OP, firmware/src/io/editor/ed_proj.c; the stream is the
   backup's PJ01..PJ16 object). A firmware offers it when its INFO lists "opti_proj_1" (tag 0x66, ed_opti.h) */
const PJ_OP = 110;
export const PJ_N = 16, PJ_NAME = 12;
const PJ_RC = ["ok", "bad arguments", "", "the transport plays: stop it first", "no flash", "", "", "not written (memory full?)",
  "", "a restore is running"];
const pjErr = (rc) => new Error(PJ_RC[rc] || `code ${rc}`);
const pjTag = (p) => `PJ${String(p + 1).padStart(2, "0")}`;
/* a project name as the device's NAME page takes it: upper case, " A-Z 0-9 -._/#+", 12 at most */
export const pjName = (s) => String(s || "").toUpperCase().replace(/[^ A-Z0-9\-._/#+]/g, "").slice(0, PJ_NAME).trimEnd();

/* does this firmware move projects? (its INFO reply names opti_proj_1) */
export async function hasProjects(c) {
  const r = await c.request(1).catch(() => null);
  return !!r && String.fromCharCode(...r).includes("opti_proj_1");
}
/* -> { open, used: [count bool], names: [count string], count }; count: the project slots the installed layout keeps
   (bk_caps; 16 on a firmware that does not say). The reply always carries 16 places: the rest is not shown. */
export async function projects(c, count = null) {
  const r = await c.request(PJ_OP, [0]);
  if (r[2]) throw pjErr(r[2]);
  const n = Math.min(PJ_N, count == null ? (await deviceLayout(c)).caps.projects : count);
  const used = fromU7(r.slice(4), 3), names = [];
  for (let o = 7, k = 0; k < PJ_N; k++) { const z = r.indexOf(0, o); names.push(String.fromCharCode(...r.slice(o, z < 0 ? r.length : z))); o = z < 0 ? r.length : z + 1; }
  return { open: r[3], count: n, used: Array.from({ length: n }, (_, k) => !!((used >> k) & 1)), names: names.slice(0, n) };
}
/* op 1 save the work as project p, 2 open p, 3 clear p, 4 name p. Throws on a refusal */
export async function projectOp(c, op, p, name = "") {
  const args = [op, p, ...(op === 4 ? Array.from(pjName(name), (ch) => ch.charCodeAt(0)) : [])];
  const r = await c.request(PJ_OP, args, 10000);   /* (a save / clear writes flash) */
  if (r[2]) throw pjErr(r[2]);
}
/* project p's stream (empty: the project holds nothing) */
export async function readProject(c, p) {
  const obj = (await listObjects(c)).find((o) => o.tag === pjTag(p));
  if (!obj) throw new Error("this firmware has no projects to read");
  return obj.len ? readObject(c, obj) : [];
}
/* project p of dst becomes the stream: cleared first (no record of what it held stays), written, named; opened when
   asked, or when it is dst's open project (its work would be the old one's) */
export async function writeProject(c, p, data, name, open) {
  const obj = (await listObjects(c)).find((o) => o.tag === pjTag(p));
  if (!obj || !(obj.flags & 1)) throw new Error(`this memory layout has no project ${p + 1}`);
  const was = await projects(c, 16);
  await projectOp(c, 3, p);
  if (data.length) { await writeObject(c, obj, data); await end(c, false); }
  await projectOp(c, 4, p, name);
  if (open || was.open === p) await projectOp(c, 2, p);
}
/* src's project `from` into dst's project `to`: { save } saves src's work into `from` first (src's open project: its
   current state), { name } names it on dst (default: its name on src), { open } opens it on dst */
export async function moveProject(src, dst, from, to, { save = false, name = null, open = false } = {}) {
  const s = await projects(src, 16);
  if (save) await projectOp(src, 1, from);
  const data = await readProject(src, from);
  if (!data.length) throw new Error(`project ${from + 1} is empty`);
  await writeProject(dst, to, data, name === null ? s.names[from] : name, open);
  return data.length;
}

/* ---- a project as a file (.fm1proj; the web editor and the Optimist Mirror app write and read the same bytes):
   "FM1P", format version u8 (1), slot u8 (0-based, where it came from), the firmware family (4 ASCII: the BK_LIST magic,
   "FUNB"), the name (12 ASCII, 0-padded), the stream's length u32 LE, the stream (the PJnn object, as the device gives it),
   CRC-32 of all of it before, u32 LE */
export const PROJ_FILE = { MAGIC: "FM1P", VERSION: 1, EXT: ".fm1proj", HEAD: 26 };
export function packProject({ slot, family, name, data }) {
  const d = Uint8Array.from(data), out = new Uint8Array(PROJ_FILE.HEAD + d.length + 4), dv = new DataView(out.buffer);
  out.set(Array.from(PROJ_FILE.MAGIC, (ch) => ch.charCodeAt(0)), 0);
  out[4] = PROJ_FILE.VERSION; out[5] = slot;
  out.set(Array.from(family.padEnd(4, "\0").slice(0, 4), (ch) => ch.charCodeAt(0)), 6);
  out.set(Array.from(pjName(name).padEnd(PJ_NAME, "\0"), (ch) => ch.charCodeAt(0)), 10);
  dv.setUint32(22, d.length, true);
  out.set(d, PROJ_FILE.HEAD);
  dv.setUint32(PROJ_FILE.HEAD + d.length, crc32(out.subarray(0, PROJ_FILE.HEAD + d.length)), true);
  return out;
}
export function unpackProject(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (b.length < PROJ_FILE.HEAD + 4 || String.fromCharCode(...b.subarray(0, 4)) !== PROJ_FILE.MAGIC) throw new Error("not an Optimist project file");
  if (b[4] !== PROJ_FILE.VERSION) throw new Error(`project file format ${b[4]} (this page reads ${PROJ_FILE.VERSION})`);
  const dv = new DataView(b.buffer, b.byteOffset, b.length), n = dv.getUint32(22, true), end = PROJ_FILE.HEAD + n;
  if (end + 4 !== b.length) throw new Error("the project file is cut short or has extra bytes");
  if (dv.getUint32(end, true) !== crc32(b.subarray(0, end))) throw new Error("the project file is damaged (CRC)");
  return { slot: b[5], family: String.fromCharCode(...b.subarray(6, 10)).replace(/\0+$/, ""),
    name: String.fromCharCode(...b.subarray(10, 22)).replace(/\0+$/, ""), data: Array.from(b.subarray(PROJ_FILE.HEAD, end)) };
}
/* project p of the device as file bytes */
export async function exportProject(c, p) {
  const lay = await deviceLayout(c), pj = await projects(c, lay.caps.projects);
  if (p >= pj.count) throw new Error(`this memory layout has no project ${p + 1}`);
  const data = await readProject(c, p);
  if (!data.length) throw new Error(`project ${p + 1} is empty`);
  return packProject({ slot: p, family: lay.magic, name: pj.names[p], data });
}
/* a file's project into project p (cleared first, named, and opened when it is the open one or asked). Refuses another
   firmware family, a slot the layout lacks */
export async function importProject(c, p, bytes, { open = false } = {}) {
  const f = unpackProject(bytes), lay = await deviceLayout(c);
  if (f.family !== lay.magic) throw new Error(`the file is from another firmware (${f.family}, this one ${lay.magic})`);
  if (p >= lay.caps.projects) throw new Error(`this memory layout has no project ${p + 1}`);
  await writeProject(c, p, f.data, f.name, open);
  return f;
}
