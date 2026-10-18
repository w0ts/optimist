// SPDX-License-Identifier: GPL-3.0-only
// The Simulator tab (docs/SIMULATOR.md): a build's firmware compiled for the browser (tools/sim.py --wasm) runs in an
// AudioWorklet (sim-worklet.js); this module is the page around it, mounted like the installer and the builder:
//
//   mountSim(container, {
//     packages,   the site's flavours [{ id, title, sim }]: sim a URL of the flavour's .wasm (make_site.py)
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

const DB = "optimist-sim", STORE = "state", KEY = "flash";
const STYLE_ID = "optimist-sim-style";
const KEYBOARD = "zsxdcvgbhnjmq2w3er5t6y7ui9o0p";   /* the computer keys, in the FM-1's key order (from its lowest) */

export const TEXT = {
  title: "Simulator",
  what: "A build's firmware running in this browser: its screen, keys, knobs and sound. It is fast, not exact: it runs " +
    "on this computer's processor, on one core, without Bluetooth, so it shows sounds, pages and behaviour, not the " +
    "FM-1's CPU limits. The web editor can connect to it (choose Simulator in its device list), and its Backup and " +
    "Restore move your work between the simulator and an FM-1.",
  build: "Build", site: "The site's build", cloud: "Cloud builds",
  power: "Start", stop: "Stop", starting: "Starting…", running: "Running", stopped: "Stopped.",
  reboot: "Restarting…", fault: "The firmware stopped (a fault). Start again.", noaudio: "This browser has no AudioWorklet: use Chrome, Edge or Firefox.",
  reset: "Reset memory", resetAsk: "Erase the simulator's saved projects, presets and samples? (Every build shares them.)",
  resetDone: "Memory erased.", shared: "The simulator's memory (projects, presets, samples) is kept in this browser and shared by every build.",
  install: "Install this build on my FM-1", keysHelp: "Keys: Z S X D C V … on the keyboard; knobs: drag or scroll.",
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
  const packages = (opts.packages || []).filter((p) => p.sim);
  const repo = opts.repo || "w0ts/optimist";
  const links = { install: "../installer/", ...(opts.links || {}) };
  const wanted = opts.build ?? (typeof location !== "undefined" ? new URLSearchParams(location.search).get("sim") : null);
  addStyle(container.ownerDocument);
  const st = { choice: null, cloud: null, ctx: null, node: null, info: null, busy: false, wasm: null, keysDown: new Set() };
  const el = {};

  el.pick = h("select.oi-csearch.os-pick", { "aria-label": TEXT.build, onchange: () => choose(el.pick.value) });
  el.power = h("button.o-btn.primary", { type: "button", onclick: () => (engine ? stop() : start()) }, TEXT.power);
  el.reset = h("button.o-btn.sm.quiet", { type: "button", onclick: () => resetMemory() }, TEXT.reset);
  el.install = h("button.o-btn.sm", { type: "button", onclick: () => installThis() }, TEXT.install);
  el.status = h("p.os-status.o-dim", { role: "status", "aria-live": "polite" });
  el.canvas = h("canvas.os-screen", { width: "240", height: "240", "aria-label": "The FM-1's screen" });
  el.panel = h("div.os-panel");
  el.keys = h("div.os-keys");
  const root = h("div.os", {},
    h("header", {}, h("h1.oi-title", {}, TEXT.title), h("p.oi-what", {}, TEXT.what)),
    h("div.os-bar", {}, h("label.o-field", {}, h("span", {}, TEXT.build), el.pick), el.power, el.install, el.reset),
    el.status,
    h("div.os-device", {}, el.canvas, el.panel),
    el.keys,
    h("p.o-dim.os-help", {}, TEXT.keysHelp, " ", TEXT.shared));
  container.replaceChildren(root);
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
  }
  function current() {
    if (!st.choice) return null;
    const [kind, id] = st.choice.split(":");
    if (kind === "site") { const p = packages[Number(id)]; return p ? { title: p.title, url: p.sim, id: null } : null; }
    const b = st.cloud && st.cloud.builds.find((x) => x.id === id);
    return b && b.sim ? { title: `${b.name} (${b.id})`, url: cloudFileUrl(repo, st.cloud.sha, b.sim), id: b.id, sha256: b.simSha256 } : null;
  }
  function choose(v) { st.choice = v; st.wasm = null; if (engine) { stop().then(start); } }

  /* ---- running */
  async function start() {
    const b = current();
    if (!b) { say(TEXT.nosim, "err"); return; }
    if (typeof AudioWorkletNode === "undefined") { say(TEXT.noaudio, "err"); return; }
    say(TEXT.starting);
    el.power.disabled = true;
    try {
      if (!st.wasm || st.wasm.url !== b.url) {
        const r = await fetch(b.url);
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        st.wasm = { url: b.url, bytes: await r.arrayBuffer() };
      }
      if (!st.ctx) {
        st.ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
        await st.ctx.audioWorklet.addModule(new URL("sim-worklet.js", import.meta.url).href);
      }
      await st.ctx.resume();
      await boot(b);
    } catch (e) { say(TEXT.loadfail + (e && e.message || e), "err"); }
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
      say(`${TEXT.running}: ${b.title}`, "ok");
      el.power.textContent = TEXT.stop;
      setConnected(true);
    } else if (m.type === "frame") {
      draw(m.bands); leds(m.leds); st.midiLost = m.midiLost;
    } else if (m.type === "leds") {
      leds(m.leds);
    } else if (m.type === "midi") {
      for (const msg of midiIn.feed(m.data)) {
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
    if (status & 4) { say(TEXT.fault, "err"); el.power.textContent = TEXT.power; return; }
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
    el.power.textContent = TEXT.power;
    say(TEXT.stopped);
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

  /* ---- the panel: buttons (named by the firmware), the knobs, MASTER, the keys */
  function buildPanel(info) {
    const btns = (info.buttonNames.length ? info.buttonNames : Array.from({ length: info.buttons }, (_, i) => `B${i + 1}`))
      .map((name, i) => h("button.os-btn", { type: "button", "data-led": String(i), onpointerdown: (e) => press(e, "button", i), onpointerup: () => send("button", i, 0), onpointerleave: () => send("button", i, 0) }, name));
    const knobs = Array.from({ length: info.encoders }, (_, i) => knob(i, (info.encoderNames || [])[i]));
    const master = h("input.os-master", { type: "range", min: "0", max: "1023", value: "700", "aria-label": "MASTER", oninput: (e) => engine && engine.post({ type: "master", value: Number(e.target.value) }) });
    el.panel.replaceChildren(h("div.os-knobs", {}, knobs, h("label.os-mlabel", {}, "MASTER", master)), h("div.os-btns", {}, btns));
    el.keys.replaceChildren(...Array.from({ length: info.keys }, (_, i) =>
      h("button.os-key", { type: "button", "data-led": String(info.buttons + i), onpointerdown: (e) => press(e, "key", i), onpointerup: () => send("key", i, 0), onpointerleave: () => send("key", i, 0) }, KEYBOARD[i] ? KEYBOARD[i].toUpperCase() : "")));
    if (engine) engine.post({ type: "master", value: 700 });
  }
  function press(e, type, i) { e.preventDefault(); send(type, i, 1); }
  function send(type, i, down) { if (engine) engine.post({ type, i, down }); }
  function knob(i, name) {
    const label = name || `Knob ${i + 1}`;
    const k = h("div.os-knob", { role: "slider", tabindex: "0", "aria-label": label, title: `${label}: drag up / down or scroll` }, name ? name.replace(/^KNOB ?/, "K").slice(0, 3) : String(i + 1));
    let y = null, acc = 0;
    const turn = (d) => { if (engine && d) engine.post({ type: "enc", i, delta: d }); };
    k.addEventListener("pointerdown", (e) => { y = e.clientY; acc = 0; k.setPointerCapture(e.pointerId); });
    k.addEventListener("pointermove", (e) => { if (y === null) return; acc += (y - e.clientY) / 6; y = e.clientY; const d = Math.trunc(acc); acc -= d; turn(d); });
    k.addEventListener("pointerup", () => { y = null; });
    k.addEventListener("wheel", (e) => { e.preventDefault(); turn(e.deltaY < 0 ? 1 : -1); }, { passive: false });
    k.addEventListener("keydown", (e) => { if (e.key === "ArrowUp" || e.key === "ArrowRight") turn(1); else if (e.key === "ArrowDown" || e.key === "ArrowLeft") turn(-1); });
    return k;
  }
  /* the computer keyboard: the FM-1's keys, while the tab is shown and no text field has the focus */
  function onKey(e, down) {
    if (!engine || !st.info || e.repeat || e.metaKey || e.ctrlKey || e.altKey) return;
    if (e.target && /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
    const i = KEYBOARD.indexOf(e.key.toLowerCase());
    if (i < 0 || i >= st.info.keys) return;
    e.preventDefault();
    if (down === st.keysDown.has(i)) return;
    if (down) st.keysDown.add(i); else st.keysDown.delete(i);
    send("key", i, down ? 1 : 0);
  }
  const kd = (e) => onKey(e, true), ku = (e) => onKey(e, false);
  const releaseAll = () => { for (const i of st.keysDown) send("key", i, 0); st.keysDown.clear(); };
  let active = false;

  loadBuilds();
  return {
    activate() {
      if (active) return;
      active = true;
      document.addEventListener("keydown", kd); document.addEventListener("keyup", ku); window.addEventListener("blur", releaseAll);
      if (st.node) st.node.port.postMessage({ type: "frame" });
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
