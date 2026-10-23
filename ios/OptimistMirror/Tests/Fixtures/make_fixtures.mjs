// SPDX-License-Identifier: GPL-3.0-only
// The files the app's tests read, written by the web editor's own functions (web/editor.html, the PROTO section), and the
// check that the web editor reads what the app wrote. Run from the repo root:
//   node ios/OptimistMirror/Tests/Fixtures/make_fixtures.mjs write          (regenerates Tests/Fixtures/web_*)
//   node ios/OptimistMirror/Tests/Fixtures/make_fixtures.mjs read <folder>  (reads the app's files in <folder> with the web functions)
import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const HERE = dirname(fileURLToPath(import.meta.url));
const html = readFileSync(join(HERE, "../../../../web/editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + `;({ backupFile, readBackupFile, libraryFile, readLibraryFile, bkPlan, crc32, BK, LBR_FORMAT: "optimist-pattern" })`,
  { setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder });

const caps = { eng: 255, kits: 1023, sets: 7, sections: 8, usrCap: [81408, 81408, 81408], engAt: 4, trk: 300, kitAt: 30, e0At: 40, np: 90, pe0: 62, slen: 60, bits: [1, 2, 3] };
const device = { version: "OPTIMIST 0.9", proto: 6, switches: 0x15, magic: "FUNB", bk: 4, caps };
const objects = [
  { tag: "SETT", kind: "st", fm6: false, data: Uint8Array.from({ length: 300 }, (_, i) => (i * 7 + 3) & 255) },
  { tag: "S01 ", kind: "sec", fm6: false, data: Uint8Array.from({ length: 1000 }, (_, i) => (i * 13) & 255) },
  { tag: "USR1", kind: "usr", fm6: false, data: Uint8Array.from({ length: 700 }, (_, i) => (i * 5 + 1) & 255) },
];
const created = "2026-01-02T03:04:05.678Z";
const patches = [
  { name: "BASS ONE", engine: 0, engineName: "ANALOG", p: [10, null, -3, 127], pattern: [[60, 1], [0, 0], [0, 4], [64, 0], ...Array(12).fill([0, 0])], tags: ["bass", "device"], created, modified: created, slot: 3 },
  { name: "PAD", engine: 1, engineName: "DIGITAL", p: [0, 1, 2, 3], pattern: null, tags: [], created, modified: created },
];
const ctx = { keys: ["LEVEL", "CUT", "RES", "E0"], engines: ["ANALOG", "DIGITAL", "FM6"], firmware: "OPTIMIST 0.9", pe0: 3 };

if (process.argv[2] === "write") {
  writeFileSync(join(HERE, "web_backup.optimist-backup"), E.backupFile(device, objects, created));
  writeFileSync(join(HERE, "web_library.json"), JSON.stringify(E.libraryFile("library", patches, ctx), null, 1).replace(/"exported": "[^"]*"/, `"exported": "${created}"`));
  writeFileSync(join(HERE, "web_pattern.json"), JSON.stringify({ format: "optimist-pattern", version: 1, track: 2, bytes: [1, 2, 3, 250, 255, 0] }));
  writeFileSync(join(HERE, "web_patch_file.json"), JSON.stringify({ format: "felucca-patch", presetName: "LEAD", engine: 1, engineName: "DIGITAL", p: [5, 6, 7, 8, 9],
    steps: Array.from({ length: 16 }, (_, i) => ({ time: i === 1 ? 1 : i === 2 ? 2 : 0, n: i === 2 ? 0 : 1, notes: [60 + i, 0, 0, 0], flags: i === 0 ? 3 : 0 })) }));
  console.log("written");
} else if (process.argv[2] === "read") {
  const dir = process.argv[3];
  let bad = 0;
  const ok = (c, what) => { console.log(`${what.padEnd(70)} ${c ? "ok" : "FAIL"}`); if (!c) bad++; };
  for (const f of readdirSync(dir)) {
    const path = join(dir, f);
    if (f.endsWith(".optimist-backup")) {
      const r = E.readBackupFile(new Uint8Array(readFileSync(path)));
      ok(r.objects.length >= 1 && r.objects.every((o) => o.data.length > 0), `web reads ${f}: ${r.objects.map((o) => o.tag).join(" ")}`);
    } else if (f.startsWith("app_library") || f.startsWith("app_bank") || f.startsWith("app_patch")) {
      const r = E.readLibraryFile(JSON.parse(readFileSync(path, "utf8")), ctx);
      ok(r.patches.length >= 1 && r.skipped === 0, `web reads ${f}: ${r.patches.map((p) => p.name).join(", ")}`);
    } else if (f.startsWith("app_pattern")) {
      const x = JSON.parse(readFileSync(path, "utf8"));
      ok(x.format === E.LBR_FORMAT && x.version === 1 && Array.isArray(x.bytes) && x.bytes.every((b) => Number.isInteger(b) && b >= 0 && b <= 255), `web accepts ${f}`);
    }
  }
  process.exit(bad ? 1 : 0);
}
