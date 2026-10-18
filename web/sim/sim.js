// SPDX-License-Identifier: GPL-3.0-only
// The Simulator tab (docs/SIMULATOR.md): a build's firmware compiled for the browser (tools/sim.py --wasm) runs in an
// AudioWorklet (sim-worklet.js); this module is the page around it, mounted like the installer and the builder:
//
//   mountSim(container, {
//     packages,   the site's flavours [{ id, title, sim }]: sim a URL of the flavour's .wasm (make_site.py)
//     sims,       simulators that come with no package [{ id, title, sim }] (the local one: make web)
//     repo,       "owner/name": the cloud builds with a simulator (the cloud-builds branch, installer.js loadCloud)
//     build,      a cloud build id to open (default: the page's ?sim=)
//     onNavigate, (where, detail) => void: "install" with { build } (Install this build); without it a link
//     links,      { install }: the installer's href (default ../installer/)
//   }) -> { activate(), deactivate(), unmount(), busy }
//
// simAccess(): a Web MIDI access-like object with one port pair, "Optimist (sim)": the editor connects to it as to an
// FM-1 (web/editor.html: the device picker). The firmware's MIDI goes through it both ways, so the editor's pages,
// Backup and Restore work with the simulator as with the FM-1: a backup made on one restores on the other.
//
// The simulator's flash is kept in this browser (IndexedDB), one state for every build (the user's choice,
// 2026-10-18): a build that cannot read what another left handles it as the FM-1 would.
import { cloudFileUrl, loadCloud, matchCloud } from "../installer/installer.js";
import { client as bkClient, transfer as bkTransfer, readToFile as bkReadToFile, writeFromFile as bkWriteFromFile, unpackFile as bkUnpackFile } from "./bk.js";

const DB = "optimist-sim", STORE = "state", KEY = "flash";
const STYLE_ID = "optimist-sim-style";
/* the computer-key shortcuts, exactly the chip emulator's (fm1-emulator rust-emulator/src/ui.rs):
   - the 13 note keys of the lower octave; the same with Shift held play the octave above (the firmware's OCT shifts it) */
const NOTE_KEYS = ["a", "w", "s", "e", "d", "r", "f", "g", "t", "h", "y", "j", "k"];
/*   - the 14 buttons, by the firmware's button name: the arrows for OCT, Space for PLAY / STOP, letters for the rest */
const BUTTON_KEYS = { "OCT-": "arrowleft", "OCT+": "arrowright", FX: "x", SCL: "b", SEL: "b", ENV: "v", LFO: "l", EDIT: "i",
  GLO: "o", HOME: "u", SAVE: "z", ARP: "p", SEQ: "q", PLAY: " ", "PLAY/STOP": " ", REC: "c" };
const btnKeyCap = { arrowleft: "←", arrowright: "→", " ": "␣" };   /* ← → ␣ shown on the keys */

/* the FM-1's 27-key keyboard: the black keys carry the function legends (the device, and the chip emulator's panel:
   fm1-emulator rust-emulator/src/ui.rs). The 11 black keys are these note indices, in this label order */
const BLACK_NOTES = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
const BLACK_LABELS = ["OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLO", "MONO", "POLY", ""];

/* the FM-1's colour editions, as panel themes, ported from the chip emulator (fm1-emulator rust-emulator/src/ui_theme.rs,
   GPL-3.0): the body, bezel, keys and knobs. The screen inside keeps the firmware's own palette (its COLOR menu) */
const THEMES = [
  { name: "Classic", body: "#222222", bodyLine: "#303030", edge: "#171717", rim: "#555555", label: "#bec2c1", bezel: "#080808", keybed: "#282828", keybedRim: "#0c0c0c", key: "#262626", keyEdge: "#565656", keyLabel: "#bec2c1", keyPressed: "#4b412a", pressed: "#e7c15b", knobShadow: "#0c0c0c", knobRim: "#424242", knobSkirt: "#121212", knobTick: "#878787", knobCap: "#232323", knobPointer: "#bec2c1", octRecess: "#111111", buttonRecess: "#101010" },
  { name: "Black", body: "#5d5d5d", bodyLine: "#686868", edge: "#3a3a3a", rim: "#7a7a7a", label: "#eaeaea", bezel: "#0a0a0a", keybed: "#383838", keybedRim: "#2a2a2a", key: "#2a2a2a", keyEdge: "#484848", keyLabel: "#dcdcdc", keyPressed: "#4b412a", pressed: "#e7c15b", knobShadow: "#262626", knobRim: "#303030", knobSkirt: "#141414", knobTick: "#444444", knobCap: "#1c1c1c", knobPointer: "#f2f2f2", octRecess: "#232323", buttonRecess: "#232323" },
  { name: "Lilac", body: "#c298d4", bodyLine: "#cda7de", edge: "#a27cb6", rim: "#d8b8e6", label: "#3a2c4c", bezel: "#0a0a0b", keybed: "#7a6fb5", keybedRim: "#5c5094", key: "#6e62a8", keyEdge: "#8c82c4", keyLabel: "#1a1628", keyPressed: "#52478a", pressed: "#f6d77a", knobShadow: "#8e6ca2", knobRim: "#2c2a30", knobSkirt: "#141416", knobTick: "#3e3c44", knobCap: "#1c1c1f", knobPointer: "#f4f4f4", octRecess: "#6a5ea2", buttonRecess: "#6a5ea6" },
  { name: "Orange", body: "#be6d49", bodyLine: "#c87a56", edge: "#9a5136", rim: "#d2845f", label: "#ffffff", bezel: "#0a0a0a", keybed: "#8e5448", keybedRim: "#6c3c32", key: "#844d43", keyEdge: "#a06456", keyLabel: "#f6e4dc", keyPressed: "#63362c", pressed: "#ffd27a", knobShadow: "#86462e", knobRim: "#2c2624", knobSkirt: "#161212", knobTick: "#40383a", knobCap: "#1c1717", knobPointer: "#f4f4f4", octRecess: "#7c4838", buttonRecess: "#854e42" },
  { name: "Mint", body: "#4b4b4d", bodyLine: "#575759", edge: "#2e2e30", rim: "#666668", label: "#e6e6e8", bezel: "#0a0a0a", keybed: "#79d1c6", keybedRim: "#4f9e92", key: "#6fcabd", keyEdge: "#96e2d8", keyLabel: "#1d3633", keyPressed: "#4b9f93", pressed: "#ffffff", knobShadow: "#1e1e1f", knobRim: "#303032", knobSkirt: "#141415", knobTick: "#444447", knobCap: "#1d1d1f", knobPointer: "#f2f2f2", octRecess: "#62b7aa", buttonRecess: "#68c0b3" },
  { name: "Cream", body: "#e1d7cc", bodyLine: "#d7ccc1", edge: "#c2b7ab", rim: "#f0e8df", label: "#544e4c", bezel: "#0a0a0d", keybed: "#40464a", keybedRim: "#313538", key: "#36393b", keyEdge: "#52575b", keyLabel: "#a4aaae", keyPressed: "#4b412a", pressed: "#e7c15b", knobShadow: "#b4a99f", knobRim: "#2c2c2c", knobSkirt: "#141414", knobTick: "#424242", knobCap: "#1c1c1c", knobPointer: "#f2f2f2", octRecess: "#3a3d40", buttonRecess: "#3d4044" },
  { name: "Blue", body: "#f0ece6", bodyLine: "#e5e0d8", edge: "#d2ccc3", rim: "#fbf9f5", label: "#5f7290", bezel: "#0a0a0b", keybed: "#557392", keybedRim: "#3e5874", key: "#4a6888", keyEdge: "#6a88a8", keyLabel: "#0e1520", keyPressed: "#39516c", pressed: "#ffe08a", knobShadow: "#c8c1b6", knobRim: "#2c2c2c", knobSkirt: "#141414", knobTick: "#424242", knobCap: "#1c1c1c", knobPointer: "#f2f2f2", octRecess: "#4e6884", buttonRecess: "#557290" },
];
const THEME_KEY = "optimist-sim-theme";

export const TEXT = {
  title: "Simulator",
  what: "",
  build: "Build", site: "The site's build", cloud: "Cloud builds",
  themeLabel: "Theme",
  power: "Start", stop: "Stop", starting: "Starting…", clickToStart: "Click anywhere (or press a key) to start: the browser lets the sound start only after one.", stopped: "Stopped.",
  reboot: "Restarting…", fault: "The firmware stopped (a fault). Start again.", noaudio: "This browser has no AudioWorklet: use Chrome, Edge or Firefox.",
  reset: "Reset memory", resetAsk: "Erase the simulator's saved projects, presets and samples? (Every build shares them.)",
  resetDone: "Memory erased.", shared: "The simulator's memory (projects, presets, samples) is kept in this browser and shared by every build.",
  install: "Install this build on my FM-1", config: "Configuration", configTip: "Open this build's configuration in the builder (to change it and build again)", keysHelp: "Keys: the device's own — A W S E D … play notes (Shift for the octave above), the other keys hold the buttons; the arrows are OCT, Space is PLAY.",
  load: "Load from FM-1", write: "Write to FM-1", saveFile: "Save to file", loadFile: "Load file",
  writeAsk: "Write the simulator's memory onto the FM-1? This replaces the projects, presets, kits and samples on the device.",
  writeDoing: "Writing", writeDone: "Written to the FM-1: objects",
  saving: "Saving…", savedFile: "Saved the simulator's memory to a file.", saveFail: "Could not save: ",
  loadingFile: "Loading the file…", loadedFile: "Loaded from the file: objects", loadFileFail: "Could not load the file: ",
  loadNeedsRun: "Start the simulator first, then load into it.",
  loadNoMidi: "This browser has no Web MIDI: use Chrome or Edge to read the FM-1.",
  loadFinding: "Looking for the FM-1 over USB…",
  loadAllow: "Allow MIDI for this page: the browser asks in a small bubble by the address bar (\"Use your MIDI devices\", with SysEx).",
  loadNoAllow: "No answer to the browser's MIDI question (the bubble by the address bar): allow it, then press the button again.",
  loadMidiDenied: "MIDI is blocked for this page: allow it in the site settings (the icon at the left of the address bar), then press the button again.", loadNoDevice: "No FM-1 found over USB. Connect it (Chrome or Edge), and run Optimist on it.",
  loadDoing: "Reading", loadFinishing: "Writing it in…", loadDone: "Loaded from the FM-1: objects", loadSkipped: "skipped, this build cannot read",
  loadNoAnswer: "The FM-1 did not answer. Check it is the right device and runs Optimist (the web editor should connect to it too).",
  loadNoBackup: "This FM-1's firmware has no editor Backup, so its memory cannot be read. Flash a current build (with the editor) first.",
  loadFail: "Could not load from the FM-1: ",
  nosim: "This build has no simulator (built before the simulator existed: ask for it again in the builder).",
  loadfail: "Could not load the simulator: ",
};

/* ------------------------------------------------------------------ the saved state (IndexedDB) */
function idb() {
  return new Promise((resolve, reject) => {
    const r = indexedDB.open(DB, 1);
    r.onupgradeneeded = () => r.result.createObjectStore(STORE);
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error);
  });
}
export async function loadFlash() {
  try {
    const db = await idb();
    return await new Promise((resolve) => { const q = db.transaction(STORE).objectStore(STORE).get(KEY); q.onsuccess = () => resolve(q.result || null); q.onerror = () => resolve(null); });
  } catch (e) { return null; }
}
export async function saveFlash(buf) {
  const db = await idb();
  return new Promise((resolve, reject) => { const t = db.transaction(STORE, "readwrite"); t.objectStore(STORE).put(buf, KEY); t.oncomplete = resolve; t.onerror = () => reject(t.error); });
}
export async function clearFlash() {
  const db = await idb();
  return new Promise((resolve) => { const t = db.transaction(STORE, "readwrite"); t.objectStore(STORE).delete(KEY); t.oncomplete = resolve; t.onerror = resolve; });
}

/* ------------------------------------------------------------------ the virtual MIDI device */
/* one for the page: the editor may connect before or after the simulator starts (its port is "disconnected" then) */
const PORT_NAME = "Optimist (sim)";
let engine = null;                       /* the running simulator: { post(msg) } */
const access = (() => {
  const input = { id: "optimist-sim-in", name: PORT_NAME, manufacturer: "Optimist", type: "input", state: "disconnected", connection: "closed",
    onmidimessage: null, open() { return Promise.resolve(this); }, close() { return Promise.resolve(this); } };
  const output = { id: "optimist-sim-out", name: PORT_NAME, manufacturer: "Optimist", type: "output", state: "disconnected", connection: "closed",
    send(data) { if (engine) engine.post({ type: "midi", data: new Uint8Array(data) }); },
    open() { return Promise.resolve(this); }, close() { return Promise.resolve(this); } };
  const a = { inputs: new Map(), outputs: new Map(), sysexEnabled: true, onstatechange: null, input, output };
  return a;
})();
function setConnected(on) {
  const st = on ? "connected" : "disconnected";
  for (const p of [access.input, access.output]) { p.state = st; p.connection = on ? "open" : "closed"; }
  access.inputs.clear(); access.outputs.clear();
  if (on) { access.inputs.set(access.input.id, access.input); access.outputs.set(access.output.id, access.output); }
  for (const p of [access.input, access.output]) if (typeof access.onstatechange === "function") access.onstatechange({ port: p });
}
/** the firmware's MIDI out, a byte stream in chunks of any size -> whole messages, as Web MIDI delivers them (a SysEx
 * F0..F7 in one event, a channel message in one, real-time bytes on their own even inside a SysEx) */
export function midiParser() {
  let sx = null, run = 0, need = 0, cur = [];
  const len = (s) => ((s & 0xf0) === 0xc0 || (s & 0xf0) === 0xd0 || s === 0xf1 || s === 0xf3 ? 1 : s === 0xf6 ? 0 : 2);
  return {
    feed(bytes) {
      const out = [];
      for (const b of bytes) {
        if (b >= 0xf8) { out.push(new Uint8Array([b])); continue; }
        if (b === 0xf0) { sx = [b]; continue; }
        if (sx) { sx.push(b); if (b === 0xf7) { out.push(new Uint8Array(sx)); sx = null; } else if (b & 0x80) sx = null; if (sx || b === 0xf7) continue; }
        if (b & 0x80) { if (b === 0xf7) continue; run = b < 0xf0 ? b : 0; need = len(b); cur = [b]; if (!need) { out.push(new Uint8Array(cur)); cur = []; } continue; }
        if (!cur.length && run) { cur = [run]; need = len(run); }
        if (!cur.length) continue;
        cur.push(b);
        if (cur.length === need + 1) { out.push(new Uint8Array(cur)); cur = []; }
      }
      return out;
    },
  };
}
const midiIn = midiParser();

/** the simulator as a MIDI access (web/editor.html's device picker); its ports appear while a simulator runs */
export function simAccess() { return access; }
export const simRunning = () => !!engine;

/* ------------------------------------------------------------------ the page */
function h(tag, attrs = {}, ...kids) {
  const [name, ...cls] = tag.split(".");
  const el = document.createElement(name);
  if (cls.length) el.className = cls.join(" ");
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === null || v === undefined || v === false) continue;
    if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
    else el.setAttribute(k, v === true ? "" : v);
  }
  for (const c of kids.flat()) if (c !== null && c !== undefined && c !== false) el.append(c.nodeType ? c : String(c));
  return el;
}
function addStyle(doc) {
  if (doc.getElementById(STYLE_ID)) return;
  doc.head.append(Object.assign(doc.createElement("link"), { id: STYLE_ID, rel: "stylesheet", href: new URL("sim.css", import.meta.url).href }));
}

export function mountSim(container, opts = {}) {
  const packages = [...(opts.packages || []).filter((p) => p.sim), ...(opts.sims || []).filter((p) => p && p.sim)];   /* (sims: the local one) */
  const repo = opts.repo || "w0ts/optimist";
  const links = { install: "../installer/", ...(opts.links || {}) };
  const wanted = opts.build ?? (typeof location !== "undefined" ? new URLSearchParams(location.search).get("sim") : null);
  addStyle(container.ownerDocument);
  let active = false;                                  /* (the tab shown: activate / deactivate) */
  const st = { choice: null, cloud: null, ctx: null, node: null, info: null, busy: false, wasm: null, keysDown: new Map(), buttonKeys: null };
  const el = {};

  el.pick = h("select.oi-csearch.os-pick", { "aria-label": TEXT.build, onchange: () => choose(el.pick.value) });
  el.power = h("button.o-btn.primary", { type: "button",
    onclick: () => { if (engine) { st.userStopped = true; stop(); } else { st.userStopped = false; start(); } } }, TEXT.power);
  el.reset = h("button.o-btn.sm.quiet", { type: "button", onclick: () => resetMemory() }, TEXT.reset);
  el.install = h("button.o-btn.sm", { type: "button", onclick: () => installThis() }, TEXT.install);
  el.config = h("button.o-btn.sm", { type: "button", title: TEXT.configTip, onclick: () => configThis() }, TEXT.config);
  el.load = h("button.o-btn.sm", { type: "button", onclick: () => loadFromDevice() }, TEXT.load);
  el.write = h("button.o-btn.sm", { type: "button", onclick: () => writeToDevice() }, TEXT.write);
  el.save = h("button.o-btn.sm", { type: "button", onclick: () => saveToFile() }, TEXT.saveFile);
  el.loadf = h("button.o-btn.sm", { type: "button", onclick: () => el.file.click() }, TEXT.loadFile);
  el.file = h("input", { type: "file", accept: ".optimist-backup,application/octet-stream", style: "display:none",
    onchange: () => { const f = el.file.files && el.file.files[0]; el.file.value = ""; if (f) loadFromFile(f); } });
  let themeIx = 0;
  try { const v = THEMES.findIndex((t) => t.name === localStorage.getItem(THEME_KEY)); if (v >= 0) themeIx = v; } catch (e) { /* no storage */ }
  el.theme = h("select.oi-csearch.os-pick", { "aria-label": TEXT.themeLabel, onchange: () => applyTheme(el.theme.selectedIndex) },
    ...THEMES.map((t) => h("option", {}, t.name)));
  el.theme.selectedIndex = themeIx;
  el.status = h("p.os-status.o-dim", { role: "status", "aria-live": "polite" });
  el.canvas = h("canvas.os-screen", { width: "240", height: "240", "aria-label": "The FM-1's screen" });
  el.body = h("div.os-body");                 /* the device, drawn in a fixed 1160x700 space (the chip emulator's), scaled to fit */
  el.stage = h("div.os-stage", {}, el.body);
  const root = h("div.os", {},
    h("header", {}, h("h1.oi-title", {}, TEXT.title), TEXT.what ? h("p.oi-what", {}, TEXT.what) : null),
    h("div.os-bar", {}, h("label.o-field", {}, h("span", {}, TEXT.build), el.pick),
      h("label.o-field", {}, h("span", {}, TEXT.themeLabel), el.theme), el.power, el.save, el.loadf, el.load, el.write, el.install, el.config, el.reset, el.file),
    el.status,
    el.stage,
    h("p.o-dim.os-help", {}, TEXT.keysHelp, " ", TEXT.shared));
  container.replaceChildren(root);
  const fit = () => { const s = Math.min(1, el.stage.clientWidth / 1160); el.body.style.setProperty("--os-scale", s); el.stage.style.height = `${700 * s}px`; };
  const ro = typeof ResizeObserver !== "undefined" ? new ResizeObserver(fit) : null;
  if (ro) ro.observe(el.stage); else if (typeof window !== "undefined") window.addEventListener("resize", fit);
  st.fitCleanup = () => { if (ro) ro.disconnect(); else if (typeof window !== "undefined") window.removeEventListener("resize", fit); };
  setTimeout(fit, 0);
  function applyTheme(ix) {
    const t = THEMES[ix] || THEMES[0];
    el.theme.selectedIndex = ix;
    const v = { body: t.body, "body-line": t.bodyLine, edge: t.edge, rim: t.rim, label: t.label, bezel: t.bezel,
      keybed: t.keybed, "keybed-rim": t.keybedRim, key: t.key, "key-edge": t.keyEdge, "key-label": t.keyLabel,
      "key-pressed": t.keyPressed, pressed: t.pressed, "knob-shadow": t.knobShadow, "knob-rim": t.knobRim,
      "knob-skirt": t.knobSkirt, "knob-tick": t.knobTick, "knob-cap": t.knobCap, "knob-pointer": t.knobPointer,
      "oct-recess": t.octRecess, "button-recess": t.buttonRecess };
    for (const [k, val] of Object.entries(v)) el.body.style.setProperty("--os-" + k, val);
    try { localStorage.setItem(THEME_KEY, t.name); } catch (e) { /* no storage */ }
  }
  applyTheme(themeIx);
  const g = el.canvas.getContext("2d");
  const img = g.createImageData(240, 240);
  const say = (t, kind = "") => { el.status.textContent = t; el.status.dataset.kind = kind; };

  /* ---- the builds: the site's, then the cloud builds that have a simulator */
  function options() {
    const opt = (value, text) => h("option", { value }, text);
    const cloud = st.cloud ? st.cloud.builds.filter((b) => b.sim) : [];
    el.pick.replaceChildren(
      packages.length ? h("optgroup", { label: TEXT.site }, packages.map((p, i) => opt(`site:${i}`, p.title))) : null,
      cloud.length ? h("optgroup", { label: TEXT.cloud }, cloud.map((b) => opt(`cloud:${b.id}`, `${b.name} · ${b.id} · ${b.date.slice(0, 10)}`))) : null);
    if (st.choice) el.pick.value = st.choice;
  }
  async function loadBuilds() {
    if (st.loading) return st.loading;
    st.loading = loadBuildsNow();
    return st.loading;
  }
  async function loadBuildsNow() {
    options();
    try { st.cloud = await loadCloud(repo); } catch (e) { st.cloud = { sha: "", builds: [] }; }
    const hit = wanted ? matchCloud(st.cloud.builds.filter((b) => b.sim), wanted) : [];
    st.choice = hit.length === 1 ? `cloud:${hit[0].id}` : st.choice || (packages.length ? "site:0" : null);
    options();
    if (!st.choice) say(TEXT.nosim);
    autoStart();
  }
  /* the simulator starts by itself when its tab is shown and a build is there (the user, 2026-10-18: "the emu should auto
     start"), unless the user stopped it in this page; the sound waits for the page's first click or key when the browser
     wants one (its autoplay rule: a tab opened by a click counts) */
  function autoStart() {
    if (active && !engine && !st.starting && !st.userStopped && current()) start();
  }
  function waitGesture() {
    say(TEXT.clickToStart);
    const go = () => {
      document.removeEventListener("pointerdown", go, true);
      document.removeEventListener("keydown", go, true);
      if (st.ctx) st.ctx.resume().then(() => { if (st.ctx.state === "running" && el.status.textContent === TEXT.clickToStart) say(""); }, () => {});
    };
    document.addEventListener("pointerdown", go, true);
    document.addEventListener("keydown", go, true);
  }
  function current() {
    if (!st.choice) return null;
    const [kind, id] = st.choice.split(":");
    if (kind === "site") { const p = packages[Number(id)]; return p ? { title: p.title, url: p.sim, id: null, flavour: p.id } : null; }
    const b = st.cloud && st.cloud.builds.find((x) => x.id === id);
    return b && b.sim ? { title: `${b.name} (${b.id})`, url: cloudFileUrl(repo, st.cloud.sha, b.sim), id: b.id, stem: b.stem, sha256: b.simSha256 } : null;
  }
  function choose(v) { st.choice = v; st.wasm = null; if (engine) { stop().then(start); } }

  /* ---- running */
  async function start() {
    const b = current();
    if (!b) { say(TEXT.nosim, "err"); return; }
    if (typeof AudioWorkletNode === "undefined") { say(TEXT.noaudio, "err"); return; }
    if (st.starting) return;
    st.starting = true;
    say(TEXT.starting);
    el.power.disabled = true;
    try {
      if (!st.wasm || st.wasm.url !== b.url) {
        const r = await fetch(b.url, { cache: "no-cache" });
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        st.wasm = { url: b.url, bytes: await r.arrayBuffer() };
      }
      if (!st.ctx) {
        st.ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
        await st.ctx.audioWorklet.addModule(new URL("sim-worklet.js", import.meta.url).href);
      }
      /* (without a gesture the browser keeps the context suspended and resume() may never settle: wait a moment only) */
      await Promise.race([st.ctx.resume().catch(() => {}), new Promise((r) => setTimeout(r, 400))]);
      await boot(b);
      if (st.ctx.state !== "running") waitGesture();
    } catch (e) { say(TEXT.loadfail + (e && e.message || e), "err"); }
    st.starting = false;
    el.power.disabled = false;
  }
  async function boot(b) {
    const flash = await loadFlash();
    const node = new AudioWorkletNode(st.ctx, "optimist-sim", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
      processorOptions: { wasm: st.wasm.bytes.slice(0), flash } });
    node.connect(st.ctx.destination);
    node.port.onmessage = (e) => onMessage(e.data, b);
    st.node = node;
    engine = { post: (m) => node.port.postMessage(m, m.data && m.data.buffer ? [] : []) };
  }
  function onMessage(m, b) {
    if (m.type === "ready") {
      st.info = m.info; buildPanel(m.info);
      say(st.ctx && st.ctx.state !== "running" ? TEXT.clickToStart : "");   /* (running: the Stop button says it) */
      el.power.textContent = TEXT.stop;
      setConnected(true);
    } else if (m.type === "frame") {
      draw(m.bands); leds(m.leds); st.midiLost = m.midiLost;
    } else if (m.type === "leds") {
      leds(m.leds);
    } else if (m.type === "midi") {
      for (const msg of midiIn.feed(m.data)) {
        if (st.midiTap) st.midiTap(msg);               /* "Load from FM-1": the restore client reads the sim's replies here */
        if (typeof access.input.onmidimessage === "function") access.input.onmidimessage({ data: msg, timeStamp: performance.now() });
      }
    } else if (m.type === "flash") {
      saveFlash(m.flash).catch((err) => say(`The simulator's memory was not saved: ${err && err.message}`, "err"));
    } else if (m.type === "status") {
      restart(m.status, b);
    } else if (m.type === "error") {
      if (!m.fault) { say(TEXT.loadfail + m.message, "err"); stop(); }   /* (a fault: the status message follows) */
    }
  }
  async function restart(status, b) {   /* bit 0 reboot (a restore, a firmware restart), 1 update mode, 2 fault */
    detach();
    if (status & 4) { say(TEXT.fault, "err"); clearScreen(); el.power.textContent = TEXT.power; return; }
    say(TEXT.reboot);
    await new Promise((r) => setTimeout(r, 300));   /* (the last flash message saved first) */
    await boot(b);
  }
  function detach() {
    if (st.node) { st.node.port.onmessage = null; st.node.disconnect(); st.node = null; }
    engine = null;
    setConnected(false);
  }
  async function stop() {
    if (st.node) {   /* the flash as it is now, then the instance goes */
      const node = st.node;
      await new Promise((resolve) => {
        const t = setTimeout(resolve, 1000);
        node.port.onmessage = (e) => { if (e.data.type === "flash") { saveFlash(e.data.flash).finally(() => { clearTimeout(t); resolve(); }); } };
        node.port.postMessage({ type: "flash" });
      });
    }
    detach();
    clearScreen();                                   /* the device is off: a black screen */
    el.power.textContent = TEXT.power;
    say("");                                         /* (the Start button already says it is stopped) */
  }
  async function resetMemory() {
    if (!confirmReset()) return;
    const was = !!engine;
    if (was) { detach(); }
    await clearFlash();
    say(TEXT.resetDone, "ok");
    if (was) await start();
  }
  function confirmReset() { return typeof window === "undefined" || window.confirm(TEXT.resetAsk); }
  /* a backup client talking to the running simulator over its own virtual port (the worklet's MIDI in and out) */
  function simBkClient() {
    const c = bkClient((bytes) => engine && engine.post({ type: "midi", data: bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes) }));
    st.midiTap = (msg) => c.feed(msg);
    return c;
  }
  function download(name, bytes) {
    const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
    const a = Object.assign(document.createElement("a"), { href: url, download: name });
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
  /* Save to file: the simulator's whole memory as a backup file (the editor's format, so a real FM-1 restores it too) */
  async function saveToFile() {
    if (!engine) { say(TEXT.loadNeedsRun, "err"); return; }
    el.save.disabled = true; say(TEXT.saving);
    try {
      const bytes = await bkReadToFile(simBkClient(), { product: "Optimist", simulator: true });
      st.midiTap = null;
      const b = current();
      const name = `${(b && b.id ? b.id : "optimist")}-${new Date().toISOString().slice(0, 10)}.optimist-backup`;
      download(name, bytes);
      say(TEXT.savedFile, "ok");
    } catch (e) { st.midiTap = null; say(TEXT.saveFail + (e && e.message || e), "err"); }
    el.save.disabled = false;
  }
  /* Load file: restore a backup file (from the editor, a real FM-1, or a saved sim) into the running simulator */
  async function loadFromFile(f) {
    if (!engine) { say(TEXT.loadNeedsRun, "err"); return; }
    el.loadf.disabled = true; say(TEXT.loadingFile);
    try {
      const file = bkUnpackFile(new Uint8Array(await f.arrayBuffer()));
      const r = await bkWriteFromFile(simBkClient(), file, (p) => say(p.tag ? `${TEXT.loadDoing} ${p.tag} (${p.done + 1}/${p.total})` : TEXT.loadFinishing));
      st.midiTap = null;
      say(`${TEXT.loadedFile} ${r.done}.${skippedText(r.skipped)}`, r.done ? "ok" : "err");
    } catch (e) { st.midiTap = null; say(TEXT.loadFileFail + (e && e.message || e), "err"); }
    el.loadf.disabled = false;
  }
  /* a backup client talking to a real FM-1 over Web MIDI: open the ports, confirm the link (INFO), -> { client, close }.
     Throws with a user message if there is no Web MIDI, no device, or the device does not answer (old firmware). */
  async function openDevice() {
    if (typeof navigator === "undefined" || typeof navigator.requestMIDIAccess !== "function") throw new Error(TEXT.loadNoMidi);
    /* the browser asks once per site address (a new one: the local site's port) with a small bubble by the address bar;
       till it is answered the request just waits: say so, and give up after a minute instead of looking stuck */
    let perm = null;
    try { perm = (await navigator.permissions.query({ name: "midi", sysex: true })).state; } catch (e) { perm = null; }
    if (perm === "denied") throw new Error(TEXT.loadMidiDenied);
    if (perm === "prompt") say(TEXT.loadAllow);
    let timer = 0;
    const acc = await Promise.race([
      navigator.requestMIDIAccess({ sysex: true }).catch(() => { throw new Error(TEXT.loadMidiDenied); }),
      new Promise((_, reject) => { timer = setTimeout(() => reject(new Error(TEXT.loadNoAllow)), 60000); }),
    ]).finally(() => clearTimeout(timer));
    say(TEXT.loadFinding);
    const port = (m) => [...m.values()].find((p) => /felucca|optimist/i.test(p.name || "") && p.state !== "disconnected");
    const input = port(acc.inputs), output = port(acc.outputs);
    if (!input || !output) throw new Error(TEXT.loadNoDevice);
    if (input.open) await input.open().catch(() => {});
    if (output.open) await output.open().catch(() => {});         /* some browsers drop send() on an unopened output */
    const c = bkClient((bytes) => output.send(bytes));
    const frame = midiParser();
    input.onmidimessage = (e) => { for (const msg of frame.feed(e.data)) c.feed(msg); };
    try { await c.request(1); }                                   /* INFO: tells a dead port from a build with no editor */
    catch (e) { input.onmidimessage = null; throw new Error(TEXT.loadNoAnswer); }
    return { client: c, close: () => { input.onmidimessage = null; } };
  }
  function deviceError(e) {
    const msg = e && e.message || String(e);
    say(/command 43/.test(msg) ? TEXT.loadNoBackup : [TEXT.loadNoMidi, TEXT.loadNoDevice, TEXT.loadNoAnswer, TEXT.loadMidiDenied, TEXT.loadNoAllow].includes(msg)
      ? msg : TEXT.loadFail + msg, "err");
  }
  /* "Load from FM-1": read the real device's whole user memory into the running simulator (the sim restarts on it). */
  /* "6 skipped" -> which and why ("SETT: not valid here (it would not load)") */
  const skippedText = (sk) => (sk.length ? ` Skipped ${sk.length}: ${sk.map((x) => (typeof x === "string" ? x : `${x.tag.trim()} (${x.why})`)).join(", ")}.` : "");
  async function loadFromDevice() {
    if (!engine) { say(TEXT.loadNeedsRun, "err"); return; }
    el.load.disabled = true; say(TEXT.loadFinding);
    let dev = null;
    try {
      dev = await openDevice();
      const sim = simBkClient();                                  /* NB: openDevice already set the device's input tap; the sim uses st.midiTap */
      const r = await bkTransfer(dev.client, sim, (p) => say(p.tag ? `${TEXT.loadDoing} ${p.tag} (${p.done + 1}/${p.total})` : TEXT.loadFinishing));
      say(`${TEXT.loadDone} ${r.done}.${skippedText(r.skipped)}`, r.done ? "ok" : "err");
    } catch (e) { deviceError(e); }
    if (dev) dev.close(); st.midiTap = null;
    el.load.disabled = false;
  }
  /* "Write to FM-1": the reverse — write the simulator's whole memory onto the real device (overwrites its memory).
     Confirmed first. This is how a device with only the backup responder (no live editor) gets edits made in the sim. */
  async function writeToDevice() {
    if (!engine) { say(TEXT.loadNeedsRun, "err"); return; }
    if (typeof window !== "undefined" && !window.confirm(TEXT.writeAsk)) return;
    el.write.disabled = true; say(TEXT.loadFinding);
    let dev = null;
    try {
      dev = await openDevice();
      const sim = simBkClient();
      const r = await bkTransfer(sim, dev.client, (p) => say(p.tag ? `${TEXT.writeDoing} ${p.tag} (${p.done + 1}/${p.total})` : TEXT.loadFinishing));
      say(`${TEXT.writeDone} ${r.done}.${skippedText(r.skipped)}`, r.done ? "ok" : "err");
    } catch (e) { deviceError(e); }
    if (dev) dev.close(); st.midiTap = null;
    el.write.disabled = false;
  }
  /* the picked build's configuration in the builder, to look at it or change it and build again */
  function configThis() {
    const b = current();
    if (!b) return;
    const detail = b.stem ? { cloud: b.stem } : b.flavour ? { preset: `flavour:${b.flavour}` } : null;
    if (!detail) return;
    if (opts.onNavigate) opts.onNavigate("build", detail);
    else location.href = `../builder/${detail.cloud ? `?cloud=${detail.cloud}` : ""}`;
  }
  function installThis() {
    const b = current();
    if (opts.onNavigate) opts.onNavigate("install", { build: b && b.id });
    else location.href = links.install + (b && b.id ? `?build=${b.id}` : "");
  }

  /* ---- the screen and the LEDs */
  function draw(bands) {
    const d = img.data;
    for (const [band, px] of bands) {
      for (let i = 0; i < px.length; i++) {
        const v = px[i], o = (band * 16 * 240 + i) * 4;
        d[o] = ((v >> 11) & 31) * 255 / 31; d[o + 1] = ((v >> 5) & 63) * 255 / 63; d[o + 2] = (v & 31) * 255 / 31; d[o + 3] = 255;
      }
      g.putImageData(img, 0, 0, 0, band * 16, 240, 16);
    }
  }
  function leds(l) {
    if (!l) return;
    /* (sim.c: the 14 buttons in label order, then the 27 keys; 0 off, 1 dim, 2 lit) */
    root.querySelectorAll("[data-led]").forEach((n) => { const v = l[Number(n.dataset.led)]; n.dataset.on = v === 2 ? "2" : v === 1 ? "1" : ""; });
  }

  /* ---- the device: reproduced from the chip emulator's panel (fm1-emulator rust-emulator/src/ui.rs), its own
     coordinates in a 1160x700 space (content offset 20,12). Every element is placed with those exact rects; the
     whole stage is scaled to the available width. Buttons and keys keep their firmware index in data-led for the LEDs.
     The knob cluster order and positions, the button grid and the keyboard legends are the emulator's. */
  const OFF_X = 20, OFF_Y = 12;
  const at = (x, y, w, hgt) => `position:absolute;left:${x + OFF_X}px;top:${y + OFF_Y}px;width:${w}px;height:${hgt}px`;
  const box = (x, y, w, hgt, r, varname) => h("div.os-box", { style: `${at(x, y, w, hgt)};border-radius:${r}px;background:var(--os-${varname})` });
  /* the drawn knobs: x, y (centre), label, encoder index (null = MASTER); the emulator's KNOBS table */
  const KNOB_LAYOUT = [
    [91, 106, "MASTER", null], [208, 106, "SELECT", "SELECT"], [91, 224, "PRESETS", "PRESETS"], [208, 224, "ALGORITHM", "ALGORITHM"],
    [632, 106, "KNOB1", "KNOB1"], [758, 106, "KNOB2", "KNOB2"], [884, 106, "KNOB3", "KNOB3"], [1010, 106, "KNOB4", "KNOB4"],
  ];
  const BTN_LABELS = ["FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY\nSTOP", "REC"];
  function buildPanel(info) {
    const names = info.buttonNames.length ? info.buttonNames : Array.from({ length: info.buttons }, (_, i) => `B${i + 1}`);
    const findBtn = (label) => { const i = names.findIndex((n) => n.replace(/[^A-Z0-9]/gi, "") === label.replace(/[^A-Z0-9]/gi, "")); return i >= 0 ? i : names.findIndex((n) => n.startsWith(label.slice(0, 3))); };
    const encNames = (info.encoderNames || []);
    const findEnc = (label) => { const i = encNames.findIndex((n) => n.replace(/\s/g, "").toUpperCase() === label); return i >= 0 ? i : encNames.findIndex((n) => n.replace(/\s/g, "").toUpperCase().startsWith(label.slice(0, 3))); };
    const kids = [
      h("div.os-box.os-rim", { style: `${at(0, 0, 1120, 660)};border-radius:65px;background:var(--os-rim)` }),
      box(3, 3, 1114, 651, 62, "edge"), box(7, 6, 1106, 640, 58, "body"),
      h("div.os-bodyline", { style: `${at(11, 10, 1098 - 2, 631 - 2)};border-radius:53px` }),
      box(290, 52, 270, 272, 34, "bezel"),
    ];
    // the screen: the glass, then the live canvas at [315,77,220,220]
    kids.push(h("div.os-glass", { style: `${at(310, 72, 230, 230)};border-radius:3px` }));
    el.canvas.style.cssText = `${at(315, 77, 220, 220)};border-radius:2px`;
    kids.push(el.canvas);
    // the knobs
    for (const [x, y, label, enc] of KNOB_LAYOUT) {
      const idx = enc === null ? null : findEnc(enc);
      kids.push(knob(x, y, label, idx));
    }
    // OCT recess + the two OCT buttons
    kids.push(box(65, 286, 181, 56, 12, "oct-recess"));
    const octNames = ["OCT-", "OCT+"];
    octNames.forEach((label, id) => kids.push(buttonEl(findBtn(label), label.replace("-", "−"), 75 + id * 82, 296, 70, 35)));
    // the button recess + the 12 buttons, 6 x 2
    kids.push(box(598, 186, 448, 157, 23, "button-recess"));
    BTN_LABELS.forEach((label, i) => {
      kids.push(buttonEl(findBtn(label), label, 615 + (i % 6) * 71, 201 + Math.floor(i / 6) * 70, 57, 55));
    });
    // the keybed + the keys
    kids.push(box(40, 384, 1040, 238, 40, "keybed-rim"), box(43, 387, 1034, 232, 38, "keybed"));
    kids.push(...keyEls(info.keys, info.buttons));
    el.body.replaceChildren(...kids);
    st.buttonKeys = {};                                  /* the key-char -> firmware button index map for onKey */
    names.forEach((n, i) => { const key = BUTTON_KEYS[n.replace(/\s/g, "").toUpperCase()]; if (key) st.buttonKeys[key] = i; });
    if (engine) engine.post({ type: "master", value: 700 });
    fit();
  }
  function clearScreen() { g.fillStyle = "#000"; g.fillRect(0, 0, 240, 240); }
  function buttonEl(i, label, x, y, w, hgt) {
    const norm = label.replace(/−/g, "-").replace(/\n/g, "/");
    const key = BUTTON_KEYS[norm] || BUTTON_KEYS[norm.toUpperCase()];
    return h("button.os-btn", { type: "button", "data-led": i >= 0 ? String(i) : null, style: at(x, y, w, hgt),
      title: key ? `${label.replace(/\n/g, " / ")} · ${btnKeyCap[key] || key.toUpperCase()}` : label,
      onpointerdown: (e) => press(e, "button", i), onpointerup: () => send("button", i, 0), onpointerleave: () => send("button", i, 0) },
      ...String(label).split("\n").map((s, k) => h(k ? "small" : "span", {}, s)));
  }
  function keyEls(n, nbtn) {
    const black = {};
    BLACK_NOTES.forEach((note, j) => { black[note] = BLACK_LABELS[j]; });
    const out = [];
    let wi = 0;
    for (let note = 0; note < n; note++) {
      const isBlack = note in black;
      const x = isBlack ? 58 + (wi - 0.5) * 62.5 : 58 + wi * 62.5;
      const y = isBlack ? 405 : 510;
      if (!isBlack) wi++;
      const label = isBlack ? black[note] : "";
      const lower = note < NOTE_KEYS.length ? NOTE_KEYS[note] : null;           /* the lower octave's letter */
      const upper = note >= NOTE_KEYS.length && note - NOTE_KEYS.length < NOTE_KEYS.length ? NOTE_KEYS[note - NOTE_KEYS.length] : null;   /* Shift + letter */
      const cap = lower ? lower.toUpperCase() : upper ? `⇧${upper.toUpperCase()}` : "";
      out.push(h(`button.os-key.${isBlack ? "os-black" : "os-white"}`, { type: "button", "data-led": String(nbtn + note), style: at(x, y, 49, 94),
        onpointerdown: (e) => press(e, "key", note), onpointerup: () => send("key", note, 0), onpointerleave: () => send("key", note, 0) },
        label ? h("span.os-kleg", {}, label) : null, cap ? h("span.os-kcap", {}, cap) : null));
    }
    return out;
  }
  function press(e, type, i) { e.preventDefault(); send(type, i, 1); }
  function send(type, i, down) { if (engine) engine.post({ type, i, down }); }
  /* a knob at centre (cx, cy): rim 26, skirt 22, cap 19, a pointer and 24 ticks (the emulator's c.knob). enc is the
     encoder index, or null for the MASTER pot (absolute 0..1023). Drag up / down or scroll to turn */
  function knob(cx, cy, label, enc) {
    const r = 26;
    const face = h("div.os-knobface", { role: "slider", tabindex: "0", "aria-label": label, title: `${label}: drag up / down or scroll`,
      style: `position:absolute;left:${cx + OFF_X - r}px;top:${cy + OFF_Y - r}px;width:${r * 2}px;height:${r * 2}px` }, h("span.os-knobdot"));
    const name = h("div.os-klabel", { style: `position:absolute;left:${cx + OFF_X - 60}px;top:${cy + OFF_Y - 62}px;width:120px` }, label);
    const wrap = h("div.os-knobw", {}, name, face);
    let py = null, acc = 0, master = 700;
    const setAng = (a) => face.style.setProperty("--a", `${a}deg`);
    if (enc === null) setAng(master / 1023 * 300 - 150);
    const turn = (d) => {
      if (!d) return;
      if (enc === null) { master = Math.max(0, Math.min(1023, master + d * 12)); setAng(master / 1023 * 300 - 150); if (engine) engine.post({ type: "master", value: master }); }
      else { setAng((parseFloat(face.style.getPropertyValue("--a")) || 0) + d * 15); if (engine) engine.post({ type: "enc", i: enc, delta: d }); }   /* an encoder: endless, no end stop */
    };
    face.addEventListener("pointerdown", (e) => { py = e.clientY; acc = 0; face.setPointerCapture(e.pointerId); });
    face.addEventListener("pointermove", (e) => { if (py === null) return; acc += (py - e.clientY) / 5; py = e.clientY; const d = Math.trunc(acc); acc -= d; turn(d); });
    face.addEventListener("pointerup", () => { py = null; });
    face.addEventListener("wheel", (e) => { e.preventDefault(); turn(e.deltaY < 0 ? 1 : -1); }, { passive: false });
    face.addEventListener("keydown", (e) => { if (e.key === "ArrowUp" || e.key === "ArrowRight") turn(1); else if (e.key === "ArrowDown" || e.key === "ArrowLeft") turn(-1); });
    return wrap;
  }
  /* the computer keyboard: the FM-1's keys, while the tab is shown and no text field has the focus */
  function onKey(e, down) {
    if (!engine || !st.info || e.repeat || e.metaKey || e.ctrlKey || e.altKey) return;
    if (e.target && /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
    const k = e.key.toLowerCase();
    // a button? (its firmware index, from the name -> key map)
    const bi = st.buttonKeys ? st.buttonKeys[k] : undefined;
    if (bi !== undefined) {
      e.preventDefault();
      const id = `b${bi}`;
      if (down === st.keysDown.has(id)) return;
      if (down) st.keysDown.set(id, 1); else st.keysDown.delete(id);
      send("button", bi, down ? 1 : 0);
      return;
    }
    // a note? the lower octave, or the octave above with Shift held
    const ni = NOTE_KEYS.indexOf(k);
    if (ni < 0) return;
    const note = ni + (e.shiftKey ? NOTE_KEYS.length : 0);
    if (note >= st.info.keys) return;
    e.preventDefault();
    const id = `k${k}`;                                  /* track by the physical key, so a release with Shift changed still lifts the right note */
    if (down) {
      if (st.keysDown.has(id)) return;
      st.keysDown.set(id, note); send("key", note, 1);
    } else {
      if (!st.keysDown.has(id)) return;
      send("key", st.keysDown.get(id), 0); st.keysDown.delete(id);
    }
  }
  const kd = (e) => onKey(e, true), ku = (e) => onKey(e, false);
  const releaseAll = () => { for (const [id, v] of st.keysDown) { if (id[0] === "k") send("key", v, 0); else send("button", Number(id.slice(1)), 0); } st.keysDown.clear(); };

  loadBuilds();
  return {
    activate() {
      if (active) return;
      active = true;
      document.addEventListener("keydown", kd); document.addEventListener("keyup", ku); window.addEventListener("blur", releaseAll);
      if (st.node) st.node.port.postMessage({ type: "frame" });
      autoStart();
    },
    deactivate() { active = false; releaseAll(); document.removeEventListener("keydown", kd); document.removeEventListener("keyup", ku); window.removeEventListener("blur", releaseAll); },
    async unmount() { this.deactivate(); await stop(); if (st.ctx) await st.ctx.close(); container.replaceChildren(); },
    /** a cloud build chosen from outside (the installer's "try it"): picked, and started if the simulator runs */
    async openBuild(id) {
      if (!/^[0-9a-f]{16}$/.test(id || "")) return;
      if (!st.cloud) await loadBuilds();
      const b = st.cloud.builds.find((x) => x.id === id && x.sim);
      if (!b) { say(TEXT.nosim, "err"); return; }
      st.choice = `cloud:${id}`; st.wasm = null; options();
      if (engine) { await stop(); }
      await start();
    },
    get busy() { return false; },
    get running() { return !!engine; },
    get stats() { return { midiLost: st.midiLost || 0 }; },
    get audio() { return st.node && st.ctx ? { ctx: st.ctx, node: st.node } : null; },   /* (the e2e: an analyser on the output) */
  };
}
