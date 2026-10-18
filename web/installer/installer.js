// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// The Optimist installer as a module: mountInstaller(container, options) puts the four steps (Connect, Choose,
// Install, When something goes wrong) into `container`. The same module runs the standalone page
// (web/installer/index.html, webapp/installer/ on the site) and is shipped as webapp/installer/installer.js for the
// one-page app, where the editor mounts it as a tab. The update itself is web/fm1ota.js and web/fm1pkg.js, unchanged
// (the M-UPGRADE protocol over Web MIDI; the return to the official V15 after Felucca 1.0.1). docs/PAGES.md.
//
//   mountInstaller(container, {
//     packages,      [{ id, title, description, pkg, product, sha256 }]: the site's flavours (web/flavours.json via
//                    make_site.py); pkg a URL relative to the page
//     stock,         { pkg, sha256 }: M-VAVE's official V15 the site keeps (web/stock/, make_site.py; Apache-2.0), for
//                    the return to stock in step 4 without a download; none: the user's own file only
//     version,       the site's version ("0.1"); shown with the package
//     source,        the tag or commit the site was made from ("" when unknown): the Source link
//     repo,          "owner/name" whose cloud-builds branch lists the cloud builds (default: REPO's); build: a build
//                    id to open that list on (default: the page's ?build=, which a cloud build's issue links to)
//     onNavigate,    (where) => void, where one of "build" "backup" "editor" "about": the host opens it (its tab, the
//                    About box); without it those are plain links (links below). The rescue guide is always a link.
//     links,         { build, backup, editor, about, docs }: the hrefs of those (defaults: the site's relative paths)
//     midi,          (options) => Promise<MIDIAccess>: default navigator.requestMIDIAccess (tests pass a mock)
//   }) -> { unmount(), busy }    busy: true while writing (the host must not unload or hide the page then)
import { OFFICIAL_LOADER, OUR_LOADER, Updater } from "../fm1ota.js";
import { STOCK_V15_SHA256, logicalImage, productOf, sha256hex, stockBanks, validateStockPackage } from "../fm1pkg.js";

const STYLE_ID = "optimist-installer-style";
const BUNDLED_CSS = null;                           /* make_site.py puts web/installer/installer.css here */
const REPO = "https://github.com/w0ts/optimist";
const LOADER_MARK = "FELUCCA-LOADER-1";             /* firmware/loader: in every package this page installs (make_site.py) */
const isOta = (id) => /^ota-/i.test(id.model);
const STEP1_READS = 74;                             /* the reads of step 1 on a real FM-1 (SLOOP 2.0's loader) */
const DEFAULT_LINKS = { build: "../builder/", backup: "../editor/#backup", editor: "../editor/", about: "../../LICENSING.md" };

/* every text on the page (English only). Checked against OPTIMIST.md ("Install", "Rescue, going back, credits"),
   README.md ("Your data and SLOOP 2.4") and the code (firmware/src/system/recovery.c, bootguard.h, main.c) */
export const TEXT = {
  title: "Install Optimist",
  what: "This page installs Optimist, a free, open-source firmware for the M-VAVE FM-1, from this browser over USB. " +
    "An install writes only the firmware area, not the area where the FM-1 keeps your projects, presets, kits and samples. " +
    "Optimist is a work in progress: an install can fail, its formats change between versions, and coming from another " +
    "firmware it may erase what it cannot read (SLOOP 2.4's data is kept). Back up first. No warranty.",
  s1: "Connect", s2: "Choose", s3: "Install", s4: "When something goes wrong",
  connectHow: "Use Chrome or Edge. Connect the FM-1 with a USB data cable, straight into the computer (no hub), and switch it on.",
  find: "Find the FM-1", findAgain: "Find again",
  unchecked: "Not looked for yet.", looking: "Looking for the FM-1…",
  found: "Found: ", notfoundShort: "No FM-1 found.",
  notfoundHelp: [
    "Use a data cable: some USB cables only charge.",
    "Plug it into the computer itself, not a hub.",
    "Close other apps that use MIDI (a DAW, M-VAVE's updater).",
    "If the FM-1 runs Optimist but does not start: hold OCT− alone while switching it on. The screen says OPTIMIST USB RESCUE. Then press Find again.",
  ],
  nomidi: "This browser has no Web MIDI, so it cannot install. Open this page in Chrome or Edge. Safari and the browsers on iPhone and iPad do not have Web MIDI.",
  insecure: "Web MIDI works only on a secure page: open the installer over https, or from localhost.",
  denied: "MIDI access was not allowed. Allow MIDI in the site settings (the icon at the left of the address bar), then try again.",
  idOptimist: "Optimist", idRescue: "Optimist in USB rescue", idOfficial: "the official firmware V", idSloop: "SLOOP, Felucca or X0X",
  idUpdate: "in update mode", idOther: "another firmware",
  chooseLegend: "Firmware to install",
  useFile: "A file on this computer (.fwsc)", fileLabel: "Package file",
  useCloud: "A cloud build",
  cloudDesc: "Made by the builder's Build in the cloud; each is kept 30 days after it was last asked for.",
  cloudSearch: "Build id or name", cloudLoading: "Loading the cloud builds…", cloudNone: "No cloud builds at the moment.",
  cloudNoMatch: "No cloud build matches this.", cloudFail: "Could not load the cloud builds: ",
  cloudPick: "Choose a build in the list.", cloudConfig: "configuration", cloudCopy: "Copy the build id",
  cloudTitle: "cloud build ",
  buildOwn: "Or build your own firmware: ", buildLink: "open the builder",
  pkgLoading: "Checking the package…", pkgReady: "Ready: ",
  pkgPickFile: "Choose a .fwsc file.",
  badpkg: "The package is damaged (its checksum does not match). Reload the page.",
  loadfail: "Could not load the package: ",
  notours: "This file is not a package this page can install (an Optimist, SLOOP or Felucca .fwsc, or the official FM-1 V15): ",
  noloaderpkg: "This package has no update loader in it, so it cannot be installed over USB.",
  stockTitle: "Official firmware V15 (your file)",
  stockNote: "The official firmware does not use Optimist's projects, presets, kits or samples: save your work with the editor's Backup first.",
  keep: "Keep the cable in and this page open until it says Done.",
  install: "Install", installStock: "Install the official V15", installAgain: "Install again",
  phases: ["The FM-1 checks the package", "Switching to update mode", "Writing", "Restarting"],
  done: "Done. The FM-1 runs ", doneResumed: "Done. The update finished.",
  openEditor: "Open the editor",
  logTitle: "Details (log)",
  askTitle: "Back up first?",
  askBody: "An install keeps your data in the FM-1, but it can fail. The editor's Backup saves your projects, presets, kits and samples in one file.",
  askBodyStock: "The official firmware does not use Optimist's data. The editor's Backup saves your projects, presets, kits and samples in one file.",
  askBackup: "Open the Backup", askGo: "Install now", askCancel: "Cancel",
  leave: "Writing. Do not close the page.",
  /* errors (fm1ota.js codes) */
  error: "Error: ",
  notfound: "No FM-1 found. Check the cable (step 1), close other apps that use MIDI, and press Install again.",
  model: "The connected device is not an FM-1: ",
  stopped: "The FM-1 stopped answering. Replug the USB cable and press Install again.",
  lost: "The USB connection was lost. Replug the cable and press Install again: the FM-1 stays in update mode until it is finished.",
  noloader: "The FM-1 did not come back in update mode. Replug the USB cable and press Install again: it stays in update mode until it is finished.",
  noreturn: "The FM-1 did not come back after writing. Switch it off and on.",
  mismatch: "Written, but the FM-1 reports another firmware: ",
  rescueBack: "Written, but the FM-1 started in USB rescue: the new firmware did not start. Install again, or choose another package.",
  foreign: "The FM-1 is in the update mode of another firmware. Finish that update with its own updater (M-UPGRADE), then press Install again: ",
  badreq: "The FM-1 asked for data outside the package. Replug the cable and press Install again.",
  /* step 4 */
  r1h: "The install stopped halfway", r1: "The FM-1 stays in update mode. Press Install again: it finishes.",
  r2h: "Optimist is installed but does not start", r2: "Hold OCT− alone while switching the FM-1 on. The screen says OPTIMIST USB RESCUE; install again from here. After two start-ups that crash, the FM-1 starts in USB rescue by itself.",
  r3h: "Not even the rescue starts", r3: "Hold OCT− and OCT+ together while switching on, and keep them held for 3 seconds: the FM-1 enters the chip's own update mode (UBOOT; the screen stays dark). This page cannot write in that mode: use tools/fm1_rescue.sh on a Mac, M-VAVE's M-UPGRADE, or an ",
  r4h: "Back to the official firmware V15",
  r4own: "This page keeps M-VAVE's official V15 file, unchanged (Apache-2.0, from M-VAVE's FM1 Open kit); its checksum is checked before anything is written. Then install it in step 3.",
  r4ownBtn: "Use the official V15 kept here",
  r4a: "Or download FM-1 V15 from ", r4: " (PC Firmware, FM-1 V15) and choose the file here, unchanged. This browser checks its checksum and sends it nowhere; then install it in step 3. Or use M-VAVE's own updater, M-UPGRADE (Downloads, PC Software).",
  r4aOnly: "Download FM-1 V15 from ",
  stockFile: "Official FM-1.fwsc", stockBad: "Not the official file: ",
  /* after the return to V15: its voice banks */
  banksBtn: "Restore the official voice banks",
  banksNote: "Optimist used the flash where V15 keeps its four voice banks. This writes M-VAVE's banks back (a few seconds); voices you stored in V15 since are replaced.",
  banksDoing: "Writing the official voice banks… ", banksDone: "The official voice banks are back.",
  untested: "These rescue paths have not been tried on a real FM-1 yet.",
  docs: "The full rescue guide", source: "Source", about: "About and licences", builtFrom: "built from ",
};

/* ------------------------------------------------------------------ the logic (no page needed: web/test_web.mjs) */

/* what an identity ("FM-1_015", "ota-FM-1_720") means, in words */
export function describeIdentity(id) {
  if (!id) return "";
  if (isOta(id)) return `${TEXT.idUpdate} (${id.text})`;
  const m = /^FM-1_(\d+)$/.exec(id.text);
  const n = m ? m[1] : "";
  let what = TEXT.idOther;
  if (n === "000") what = TEXT.idRescue;                          /* firmware/src/felucca.c OTA_IDENTITY */
  else if (/^7\d\d$/.test(n)) what = TEXT.idOptimist;
  else if (/^9\d\d$/.test(n)) what = TEXT.idSloop;
  else if (/^0[01]\d$/.test(n)) what = TEXT.idOfficial + String(parseInt(n, 10));   /* M-VAVE: up to V15, FM-1_015 */
  return `${what} (${id.text})`;
}

function has(bytes, text) {
  const t = [...new TextEncoder().encode(text)];
  outer: for (let i = 0; i + t.length <= bytes.length; i++) {
    for (let j = 0; j < t.length; j++) if (bytes[i + j] !== t[j]) continue outer;
    return true;
  }
  return false;
}

function codeError(code, detail = "") { const e = new Error(code); e.code = code; e.detail = detail; return e; }

/* a package to install: { product, sha256, image, stock }. expect: the site's { product, sha256 } (the download must
   match them), or none for a file of the user's: then the official V15 (checked by its SHA-256) or a package with our
   loader (the identities make_site.py accepts) */
export async function checkPackage(bytes, expect = null) {
  let product;
  try { product = productOf(bytes); } catch (e) { throw codeError("notours", e.message); }
  if (!expect) {
    const stock = await validateStockPackage(bytes).catch(() => null);
    if (stock) return { product: stock.product, sha256: stock.sha256, image: stock.image, stock: true, banks: await stockBanks(bytes) };
    if (!/^FM-1_[79]\d\d$/.test(product)) throw codeError("notours", product || "no identity");
  } else if (product !== expect.product) throw codeError("badpkg");
  if (!has(bytes, LOADER_MARK)) throw codeError("noloaderpkg");
  const sha256 = globalThis.crypto && globalThis.crypto.subtle ? await sha256hex(bytes) : "";
  if (expect && expect.sha256 && sha256 && sha256 !== expect.sha256) throw codeError("badpkg", `sha256 ${sha256}, expected ${expect.sha256}`);
  return { product, sha256, image: logicalImage(bytes), stock: false };
}

/* the official V15 file of step 4 (yours, or the copy the site keeps: opts.stock), with its voice banks (fm1pkg.js
   stockBanks: written back after the install, restoreBanks) */
export async function checkStock(bytes) {
  const s = await validateStockPackage(bytes);
  return { product: s.product, sha256: s.sha256 || STOCK_V15_SHA256, image: s.image, stock: true, banks: await stockBanks(bytes) };
}

/* fm1ota.js's steps -> the phase (0..3) and the bar (0..100): the check 0-10, the switch 10-15, the write 15-95 by the
   highest address read, the restart 95-100 */
export function progressOf(k, a, addr, imageLength, prev = 0) {
  let phase = -1, pct = prev;
  if (k === "start") { phase = 0; pct = Math.max(prev, 1); }
  else if (k === "verify") { phase = 0; pct = Math.min(10, (a / STEP1_READS) * 10); }
  else if (k === "loader") { phase = 1; pct = 12; }
  else if (k === "write") { phase = 2; pct = 15 + Math.min(80, (Math.min(imageLength, (addr || 0) + 512) / Math.max(1, imageLength)) * 80); }
  else if (k === "reboot") { phase = 3; pct = 96; }
  else if (k === "done") { phase = 4; pct = 100; }
  return { phase, pct: Math.max(prev, Math.round(pct * 10) / 10) };
}

/* an error's message, plainly */
export function errorText(e) {
  if (e && e.code === "mismatch" && e.detail === "FM-1_000") return TEXT.rescueBack;
  if (e && e.code && typeof TEXT[e.code] === "string") return TEXT[e.code] + (["model", "mismatch", "foreign", "notours", "loadfail"].includes(e.code) ? e.detail || "" : "");
  return TEXT.error + (e && e.message ? e.message : String(e));
}

/* ---- the cloud builds (docs/CLOUD-BUILD.md): the cloud-builds branch's index.json and files (tools/cloud_build.py
   shelf). Read at the branch's commit (the API, which sends CORS), then from raw.githubusercontent.com by that commit:
   release downloads send no CORS header, and a branch name there is cached for minutes */
export const CLOUD_BRANCH = "cloud-builds";
const CLOUD_ID = /^[0-9a-f]{16}$/, CLOUD_STEM = /^optimist-[a-z0-9-]{1,32}-[0-9a-f]{16}$/;

/** the index's builds that are well formed (the branch is written by the workflow, but read as untrusted) */
export function cloudBuilds(index) {
  const list = index && Array.isArray(index.builds) ? index.builds : [];
  return list.filter((b) => b && CLOUD_ID.test(b.id) && CLOUD_STEM.test(b.stem) && b.stem.endsWith(b.id)
    && b.fwsc === `${b.stem}.fwsc` && b.config === `${b.stem}.config` && /^[0-9a-f]{64}$/.test(b.sha256 || ""))
    .map((b) => ({ id: b.id, stem: b.stem, name: String(b.name || "custom").slice(0, 32), date: String(b.date || "").slice(0, 20),
      commit: /^[0-9a-f]{40}$/.test(b.commit || "") ? b.commit : "", sha256: b.sha256, fwsc: b.fwsc, config: b.config,
      flash: b.sizes && Number.isInteger(b.sizes.flash) ? b.sizes.flash : null }));
}

/** the builds a search keeps: a build id (or its start, 4+ digits, spaces and a "key" word ignored) or part of a name */
export function matchCloud(builds, query) {
  const text = String(query || "").trim().toLowerCase(), id = text.replace(/^(build|key|id)\s*/, "").replace(/[\s`]+/g, "");
  if (!text) return builds;
  if (/^[0-9a-f]{4,64}$/.test(id)) {
    const byId = builds.filter((b) => b.id.startsWith(id.slice(0, 16)));
    if (byId.length) return byId;
  }
  return builds.filter((b) => b.name.toLowerCase().includes(text) || b.stem.includes(text));
}

export const cloudFileUrl = (repo, sha, name) => `https://raw.githubusercontent.com/${repo}/${sha}/${encodeURIComponent(name)}`;

/** -> { sha, builds }: the branch's commit and its builds; no branch (none built, or all expired): no builds */
export async function loadCloud(repo, fetchFn = fetch) {
  const r = await fetchFn(`https://api.github.com/repos/${repo}/branches/${CLOUD_BRANCH}`, { headers: { Accept: "application/vnd.github+json" } });
  if (r.status === 404) return { sha: "", builds: [] };
  if (!r.ok) throw new Error(r.status === 403 ? "GitHub's limit of requests from this network, try again in an hour" : `HTTP ${r.status}`);
  const sha = ((await r.json()).commit || {}).sha || "";
  if (!/^[0-9a-f]{40}$/.test(sha)) throw new Error("no commit on the cloud-builds branch");
  const ix = await fetchFn(cloudFileUrl(repo, sha, "index.json"));
  if (!ix.ok) throw new Error(`index.json: HTTP ${ix.status}`);
  return { sha, builds: cloudBuilds(await ix.json()) };
}

/* fm1ota.js's Updater, unchanged, that also keeps the address of the last read (its serve() hands it to onProgress;
   install() and write() pass only the count on), for a bar that follows the write */
class TrackedUpdater extends Updater {
  serve(link, image, finishAddr, idleMs, onProgress) {
    return super.serve(link, image, finishAddr, idleMs, (n, addr) => { this.lastAddr = addr; if (onProgress) onProgress(n, addr); });
  }
}

/* ------------------------------------------------------------------ the page */

/* h("div.cls", { attr }, children...): an element (text children are text nodes: the manifest's words never become HTML) */
function h(tag, attrs = {}, ...kids) {
  const [name, ...cls] = tag.split(".");
  const el = document.createElement(name);
  if (cls.length) el.className = cls.join(" ");
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === null || v === undefined || v === false) continue;
    if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
    else if (k === "style") el.style.cssText = v;
    else el.setAttribute(k, v === true ? "" : v);
  }
  for (const c of kids.flat()) if (c !== null && c !== undefined && c !== false) el.append(c.nodeType ? c : String(c));
  return el;
}

function addStyle(doc) {
  if (doc.getElementById(STYLE_ID)) return;
  if (BUNDLED_CSS) doc.head.append(Object.assign(doc.createElement("style"), { id: STYLE_ID, textContent: BUNDLED_CSS }));
  else doc.head.append(Object.assign(doc.createElement("link"), { id: STYLE_ID, rel: "stylesheet", href: new URL("installer.css", import.meta.url).href }));
}

export function mountInstaller(container, opts = {}) {
  const packages = opts.packages || [];
  const version = opts.version || "";
  const source = opts.source || "";
  const links = { ...DEFAULT_LINKS, docs: `${REPO}/blob/${source || "main"}/OPTIMIST.md#rescue-going-back-credits`, ...(opts.links || {}) };
  const nav = (typeof navigator !== "undefined" && navigator) || {};
  const midiOf = opts.midi || (typeof nav.requestMIDIAccess === "function" ? (o) => nav.requestMIDIAccess(o) : null);
  const secure = typeof isSecureContext === "undefined" || isSecureContext;
  const repo = opts.repo || REPO.replace("https://github.com/", "");
  const fetchFn = opts.fetch || ((...a) => fetch(...a));
  const wanted = opts.build ?? (typeof location !== "undefined" ? new URLSearchParams(location.search).get("build") : null);
  addStyle(container.ownerDocument);

  const st = { access: null, choice: wanted ? "cloud" : packages.length ? "0" : "file", pkg: null, pkgErr: "", busy: false, asked: false, gen: 0,
    cloud: null, cloudPick: null };
  const el = {};
  const link = (where, text) => h("a", { href: links[where], ...(/^https?:/.test(links[where]) ? { target: "_blank", rel: "noopener" } : {}),
    onclick: (ev) => { if (opts.onNavigate && where !== "docs") { ev.preventDefault(); opts.onNavigate(where); } } }, text);
  const say = (node, text, kind = "") => { node.textContent = text; node.dataset.kind = kind; };
  const log = (t) => { el.log.textContent += t + "\n"; };

  /* ---- 1 Connect */
  el.find = h("button.o-btn", { type: "button", onclick: () => detect(true) }, TEXT.find);
  el.found = h("p.oi-found", { role: "status", "aria-live": "polite" }, TEXT.unchecked);
  el.help = h("ul.oi-help", { hidden: true }, TEXT.notfoundHelp.map((t) => h("li", {}, t)));
  el.browser = h("p.o-msg.err.oi-browser", { hidden: true, role: "alert" });
  const step1 = card(1, TEXT.s1,
    h("p", {}, TEXT.connectHow),
    el.browser,
    h("div.oi-row", {}, el.find, el.found),
    el.help);

  /* ---- 2 Choose */
  const radios = packages.map((p, i) => choiceRow(String(i), p.title, p.description, p.product));
  el.file = h("input.oi-file", { type: "file", accept: ".fwsc", "aria-label": TEXT.fileLabel, onchange: () => { st.choice = "file"; syncChoice(); loadChoice(); } });
  const fileRow = choiceRow("file", TEXT.useFile, null, null, el.file);
  el.csearch = h("input.oi-csearch", { type: "search", placeholder: TEXT.cloudSearch, "aria-label": TEXT.cloudSearch, value: wanted || "",
    spellcheck: "false", autocomplete: "off", oninput: () => renderCloud(), onfocus: () => { if (st.choice !== "cloud") { st.choice = "cloud"; syncChoice(); loadChoice(); } } });
  el.clist = h("ul.oi-clist");
  el.cmsg = h("p.oi-cmsg.o-dim", { role: "status", "aria-live": "polite" });
  const cloudRow = choiceRow("cloud", TEXT.useCloud, TEXT.cloudDesc, null, h("div.oi-cloud", {}, el.csearch, el.cmsg, el.clist));
  el.stockRow = choiceRow("stock", TEXT.stockTitle, TEXT.stockNote, "FM-1_015");
  el.stockRow.hidden = true;
  el.pkginfo = h("p.oi-pkg", { role: "status", "aria-live": "polite" });
  const step2 = card(2, TEXT.s2,
    h("fieldset.oi-choices", {}, h("legend.o-sr", {}, TEXT.chooseLegend), radios, cloudRow, fileRow, el.stockRow),
    el.pkginfo,
    h("p.o-dim", {}, TEXT.buildOwn, link("build", TEXT.buildLink)));

  /* ---- 3 Install */
  el.go = h("button.o-btn.primary.big.oi-go", { type: "button", disabled: true, onclick: () => install() }, TEXT.install);
  el.phases = h("ol.oi-phases", {}, TEXT.phases.map((t) => h("li", { "data-st": "wait" }, t)));
  el.bar = h("progress.o-progress.oi-bar", { max: "100", value: "0", "aria-label": TEXT.s3 });
  el.status = h("p.oi-status", { role: "status", "aria-live": "polite" });
  el.err = h("p.o-msg.err.oi-err", { role: "alert", hidden: true });
  el.ok = h("div.o-msg.ok.oi-ok", { hidden: true });
  el.log = h("pre.oi-log");
  el.stockWarn = h("p.o-msg.warn", { hidden: true }, TEXT.stockNote);
  const step3 = card(3, TEXT.s3,
    h("p", {}, TEXT.keep), el.stockWarn, el.go, el.phases, el.bar, el.status, el.err, el.ok,
    h("details.o-details.oi-logbox", {}, h("summary", {}, TEXT.logTitle), el.log));

  /* ---- 4 When something goes wrong */
  el.stockFile = h("input.oi-file", { type: "file", accept: ".fwsc", "aria-label": TEXT.stockFile, onchange: () => loadStock() });
  el.stockMsg = h("p.oi-stockmsg", { role: "status", "aria-live": "polite" });
  const step4 = card(4, TEXT.s4,
    h("dl.oi-rescue", {},
      h("dt", {}, TEXT.r1h), h("dd", {}, TEXT.r1),
      h("dt", {}, TEXT.r2h), h("dd", {}, TEXT.r2),
      h("dt", {}, TEXT.r3h), h("dd", {}, TEXT.r3, h("a", { href: "https://github.com/kurogedelic/FM-1-transporter", target: "_blank", rel: "noopener" }, "FM-1-transporter"), "."),
      h("dt", {}, TEXT.r4h), h("dd", {},
        opts.stock ? h("p", {}, TEXT.r4own, " ", h("button.o-btn.sm.oi-stockown", { type: "button", onclick: () => loadStock(true) }, TEXT.r4ownBtn)) : null,
        opts.stock ? TEXT.r4a : TEXT.r4aOnly, h("a", { href: "https://www.m-vave.com/download", target: "_blank", rel: "noopener" }, "www.m-vave.com/download"), TEXT.r4,
        h("label.o-field.oi-stockfield", {}, h("span", {}, TEXT.stockFile), el.stockFile), el.stockMsg)),
    h("p.o-dim", {}, TEXT.untested, " ", link("docs", TEXT.docs)));

  /* ---- the page */
  const srcHref = source ? `${REPO}/tree/${source}` : REPO;
  el.meta = h("p.oi-meta.o-dim");
  const root = h("div.oi", {},
    h("header.oi-intro", {}, h("h1.oi-title", {}, TEXT.title), h("p.oi-what", {}, TEXT.what)),
    h("ol.oi-steps", {}, [step1, step2, step3, step4].map((s) => h("li", {}, s))),
    h("footer.oi-foot", {}, el.meta,
      h("p.o-dim", {}, h("a", { href: srcHref, target: "_blank", rel: "noopener" }, TEXT.source), " · ", link("about", TEXT.about))));
  container.replaceChildren(root);

  function card(n, title, ...body) {   /* (neutral: the track colours are the device's, not the installer's) */
    const hid = `oi-s${n}`;
    return h("section.o-panel.oi-step", { "aria-labelledby": hid },
      h("div.o-panel-h", {}, h("h2.oi-h", { id: hid }, h("span.oi-num", { "aria-hidden": "true" }, String(n)), h("span", {}, title))),
      h("div.o-panel-b", {}, ...body));
  }
  function choiceRow(value, title, desc, product, extra = null) {
    const id = `oi-c-${value}`;
    const input = h("input.oi-radio", { type: "radio", name: "oi-choice", id, value, onchange: () => { st.choice = value; syncChoice(); loadChoice(); } });
    return h("div.oi-choice", { "data-value": value },
      input,
      h("label", { for: id }, h("b", {}, title), product ? h("span.o-tag.outline", {}, product) : null, desc ? h("span.oi-desc.o-dim", {}, desc) : null),
      extra);
  }
  function syncChoice() {
    for (const r of root.querySelectorAll(".oi-radio")) r.checked = r.value === st.choice;
    el.file.disabled = el.csearch.disabled = st.busy;
    for (const b of el.clist.querySelectorAll("button")) b.disabled = st.busy;
    for (const r of root.querySelectorAll(".oi-radio")) r.disabled = st.busy;
  }

  /* ---- the package */
  async function loadChoice() {
    const gen = ++st.gen;
    st.pkg = null; st.pkgErr = "";
    render();
    if (st.choice === "stock") { st.pkg = st.stock || null; render(); return; }
    if (st.choice === "cloud" && !st.cloud) loadCloudList();
    say(el.pkginfo, st.choice === "file" && !(el.file.files && el.file.files[0]) ? TEXT.pkgPickFile
      : st.choice === "cloud" && !st.cloudPick ? TEXT.cloudPick : TEXT.pkgLoading);
    try {
      let pkg;
      if (st.choice === "file") {
        const f = el.file.files && el.file.files[0];
        if (!f) return;
        pkg = await checkPackage(new Uint8Array(await f.arrayBuffer()));
        pkg.title = pkg.stock ? TEXT.stockTitle : f.name;
      } else if (st.choice === "cloud") {
        const b = st.cloudPick;
        if (!b) return;
        const r = await fetchFn(cloudFileUrl(repo, st.cloud.sha, b.fwsc));
        if (!r.ok) throw codeError("loadfail", `HTTP ${r.status}`);
        pkg = await checkPackage(new Uint8Array(await r.arrayBuffer()));
        if (pkg.stock || (pkg.sha256 && pkg.sha256 !== b.sha256)) throw codeError("badpkg", `sha256 ${pkg.sha256}, expected ${b.sha256}`);
        pkg.title = `${b.name} (${TEXT.cloudTitle}${b.id})`;
      } else {
        const meta = packages[Number(st.choice)];
        const r = await fetch(meta.pkg);
        if (!r.ok) throw codeError("loadfail", `HTTP ${r.status}`);
        pkg = await checkPackage(new Uint8Array(await r.arrayBuffer()), meta);
        pkg.title = meta.title; pkg.version = version;
      }
      if (gen !== st.gen) return;
      st.pkg = pkg;
    } catch (e) {
      if (gen !== st.gen) return;
      st.pkgErr = e && e.code === "loadfail" ? TEXT.loadfail + (e.detail || e.message) : errorText(e);
      if (e && e.detail && e.code === "badpkg") log(e.detail);
    }
    render();
  }
  /* ---- the cloud builds: the list (loaded once, when the choice is first made), the search, the pick */
  async function loadCloudList() {
    st.cloud = { loading: true, builds: [] };
    renderCloud();
    try { st.cloud = await loadCloud(repo, fetchFn); }
    catch (e) { st.cloud = { err: e.message, builds: [] }; renderCloud(); st.cloud = null; return; }
    const hit = wanted ? matchCloud(st.cloud.builds, wanted) : [];
    if (hit.length === 1 && st.choice === "cloud") { pickCloud(hit[0]); return; }
    renderCloud();
  }
  function pickCloud(b) {
    if (st.busy) return;
    st.cloudPick = b; st.choice = "cloud";
    syncChoice(); renderCloud(); loadChoice();
  }
  function renderCloud() {
    const c = st.cloud;
    if (!c) { el.cmsg.textContent = ""; el.clist.replaceChildren(); return; }
    const shown = matchCloud(c.builds, el.csearch.value);
    el.cmsg.textContent = c.loading ? TEXT.cloudLoading : c.err ? TEXT.cloudFail + c.err : !c.builds.length ? TEXT.cloudNone : !shown.length ? TEXT.cloudNoMatch : "";
    const cfg = (b) => `${links.build}${links.build.includes("?") ? "&" : "?"}cloud=${b.stem}`;
    el.clist.replaceChildren(...shown.map((b) => h("li.oi-cbuild", { "data-on": st.cloudPick && st.cloudPick.id === b.id ? "1" : null },
      h("button.oi-cpick", { type: "button", "aria-pressed": String(!!(st.cloudPick && st.cloudPick.id === b.id)), disabled: st.busy, onclick: () => pickCloud(b) },
        h("b", {}, b.name), " ", h("code.oi-cid", {}, b.id)),
      h("span.oi-cmeta.o-dim", {}, [b.date.slice(0, 10), b.commit ? `from ${b.commit.slice(0, 7)}` : null, b.flash ? `flash ${b.flash.toLocaleString("en")} B` : null].filter(Boolean).join(" · ")),
      h("span.oi-cacts", {},
        h("button.o-btn.sm.quiet", { type: "button", title: TEXT.cloudCopy, onclick: () => copyId(b.id) }, "copy id"),
        h("a", { href: cfg(b), target: "_blank", rel: "noopener" }, TEXT.cloudConfig)))));
  }
  async function copyId(id) {
    try { await navigator.clipboard.writeText(id); el.cmsg.textContent = `Copied ${id}`; }
    catch (e) { el.cmsg.textContent = `Build id: ${id}`; }     // (no clipboard here: shown instead)
  }
  async function loadStock(own = false) {   /* own: the copy the site keeps (opts.stock), else the file chosen */
    const f = !own && el.stockFile.files && el.stockFile.files[0];
    if (!own && !f) return;
    try {
      let bytes;
      if (own) {
        say(el.stockMsg, TEXT.pkgLoading);
        const r = await fetch(opts.stock.pkg);
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        bytes = new Uint8Array(await r.arrayBuffer());
      } else bytes = new Uint8Array(await f.arrayBuffer());
      st.stock = await checkStock(bytes);
      st.stock.title = TEXT.stockTitle;
      say(el.stockMsg, TEXT.pkgReady + TEXT.stockTitle, "ok");
      el.stockRow.hidden = false;
      st.choice = "stock";
      syncChoice();
      await loadChoice();
      el.go.focus();
    } catch (e) { st.stock = null; say(el.stockMsg, TEXT.stockBad + e.message, "err"); }
  }

  function render() {
    const p = st.pkg;
    if (p) say(el.pkginfo, `${TEXT.pkgReady}${p.title}${p.version ? " " + p.version : ""} · ${p.product}`, "ok");
    else if (st.pkgErr) say(el.pkginfo, st.pkgErr, "err");
    el.go.textContent = p && p.stock ? TEXT.installStock : TEXT.install;
    el.go.disabled = !p || st.busy || !midiOf || !secure;
    el.find.disabled = st.busy || !midiOf || !secure;
    el.stockWarn.hidden = !(p && p.stock);
    el.meta.replaceChildren(...(p ? [h("span", {}, `${p.title}${p.version ? " " + p.version : ""}`), " · ", h("span", {}, p.product),
      source && !p.stock && st.choice !== "file" && st.choice !== "cloud" ? [" · ", TEXT.builtFrom, h("span.o-mono", {}, source)] : [],
      p.sha256 ? [" · sha256 ", h("code.oi-sha", {}, p.sha256)] : []] : []).flat());
  }

  /* ---- MIDI */
  async function access() {
    if (st.access) return st.access;
    try { st.access = await midiOf({ sysex: true }); }
    catch (e) { throw codeError("denied"); }
    st.access.onstatechange = () => { if (!st.busy) { clearTimeout(st.recheck); st.recheck = setTimeout(() => detect(false), 600); } };
    return st.access;
  }
  function detect(user) {
    if (st.busy || st.detecting) return st.detecting;
    st.detecting = detectNow(user).finally(() => { st.detecting = null; });
    return st.detecting;
  }
  async function detectNow(user) {
    say(el.found, TEXT.looking);
    el.help.hidden = true;
    try {
      const dev = await new Updater(await access(), log).find();
      if (dev) { dev.link.close(); say(el.found, TEXT.found + describeIdentity(dev.id), "ok"); }
      else { say(el.found, TEXT.notfoundShort, "err"); el.help.hidden = false; }
      el.find.textContent = TEXT.findAgain;
    } catch (e) {
      if (user || e.code !== "denied") say(el.found, errorText(e), "err");
    }
  }

  /* ---- install */
  function setPhase(i, state) {
    [...el.phases.children].forEach((li, k) => {
      li.dataset.st = state === "fail" && k === i ? "fail" : k < i ? "done" : k === i ? (state === "done" ? "done" : "on") : "wait";
      if (k === i && state !== "fail") li.setAttribute("aria-current", "step"); else li.removeAttribute("aria-current");
    });
  }
  function ask(stock) {
    if (st.asked) return Promise.resolve("go");
    return new Promise((resolve) => {
      const close = (v) => { dlg.close(); dlg.remove(); resolve(v); };
      const dlg = h("dialog.o-pop.oi-ask", { "aria-labelledby": "oi-ask-h", oncancel: (ev) => { ev.preventDefault(); close("cancel"); } },
        h("h3", { id: "oi-ask-h" }, TEXT.askTitle),
        h("p", {}, stock ? TEXT.askBodyStock : TEXT.askBody),
        h("div.oi-row", {},
          h("button.o-btn.primary", { type: "button", onclick: () => close("go") }, TEXT.askGo),
          h("button.o-btn", { type: "button", onclick: () => close("backup") }, TEXT.askBackup),
          h("button.o-btn.quiet", { type: "button", onclick: () => close("cancel") }, TEXT.askCancel)));
      root.append(dlg);
      dlg.showModal();
    });
  }
  async function install() {
    const pkg = st.pkg;
    if (!pkg || st.busy) return;
    const answer = await ask(pkg.stock);
    if (answer === "backup") {
      if (opts.onNavigate) opts.onNavigate("backup"); else window.open(links.backup, "_blank", "noopener");
      return;
    }
    if (answer !== "go") return;
    st.asked = true;
    el.err.hidden = el.ok.hidden = true;
    el.bar.value = 0;
    setPhase(-1);
    let pct = 0, phase = -1, maxAddr = 0;
    const step = (k, a, addr) => {
      log(`${k} ${a ?? ""}`);
      if (k === "write" && addr !== undefined) maxAddr = Math.max(maxAddr, addr);
      const r = progressOf(k, a, maxAddr, pkg.image.length, pct);
      pct = r.pct; el.bar.value = pct;
      if (r.phase >= 0 && r.phase !== phase) { phase = r.phase; setPhase(Math.min(phase, 3), phase > 3 ? "done" : "on"); }
      if (r.phase >= 0 && r.phase <= 3) say(el.status, TEXT.phases[r.phase] + "…");
    };
    let midi;
    try { midi = await access(); }
    catch (e) { showError(e, phase); return; }
    st.busy = true; syncChoice(); render();
    if (st.detecting) await st.detecting;            /* (one handshake at a time on the port) */
    try {
      const up = new TrackedUpdater(midi, log);
      const onStep = (k, a) => step(k, a, k === "write" ? up.lastAddr : undefined);
      const ota = await up.find(isOta);
      let back;
      if (ota) {                                     /* an interrupted install (or return to V15): finish it */
        ota.link.close();
        setPhase(2);
        const finished = await up.resume(pkg.image, onStep, pkg.stock ? OFFICIAL_LOADER : OUR_LOADER, pkg.product);
        if (!finished) throw codeError("stopped");
        back = pkg.product;
      } else {
        back = await up.install(pkg.image, pkg.product, onStep);
      }
      setPhase(4, "done");
      el.bar.value = 100;
      say(el.status, "");
      el.ok.replaceChildren(h("span", {}, `${TEXT.done}${pkg.title} (${back}).`), " ",
        pkg.stock ? (pkg.banks ? h("div.oi-banks", {}, h("p.o-dim", {}, TEXT.banksNote),
          h("button.o-btn.sm.oi-banksgo", { type: "button", onclick: () => restoreBanks(pkg.banks) }, TEXT.banksBtn)) : null)
          : h("button.o-btn.sm", { type: "button", onclick: () => (opts.onNavigate ? opts.onNavigate("editor") : (location.href = links.editor)) }, TEXT.openEditor));
      el.ok.hidden = false;
      el.go.textContent = TEXT.installAgain;
    } catch (e) {
      showError(e, phase);
      log(String(e && e.stack || e));
    }
    st.busy = false; syncChoice(); render();
    if (!el.ok.hidden) el.go.textContent = TEXT.installAgain;
  }
  /* the official voice banks back into the V15 just installed (fm1ota.js restoreBanks) */
  async function restoreBanks(banks) {
    if (st.busy) return;
    el.err.hidden = true;
    let midi;
    try { midi = await access(); } catch (e) { showError(e, -1); return; }
    st.busy = true; syncChoice(); render();
    const btn = el.ok.querySelector(".oi-banksgo");
    if (btn) btn.disabled = true;
    try {
      await new Updater(midi, log).restoreBanks(banks, (n) => {
        el.bar.value = Math.round(n / banks.length * 100);
        say(el.status, `${TEXT.banksDoing}${Math.round(n / banks.length * 100)} %`);
      });
      say(el.status, TEXT.banksDone, "ok");
      if (btn) btn.remove();
    } catch (e) {
      showError(e, -1);
      log(String(e && e.stack || e));
      if (btn) btn.disabled = false;
    }
    st.busy = false; syncChoice(); render();
  }
  function showError(e, phase) {
    if (phase >= 0) setPhase(Math.min(phase, 3), "fail");
    say(el.status, "");
    el.err.textContent = errorText(e);
    el.err.hidden = false;
  }

  /* ---- start */
  const beforeUnload = (e) => { if (st.busy) { e.preventDefault(); e.returnValue = TEXT.leave; } };
  window.addEventListener("beforeunload", beforeUnload);
  if (!midiOf || !secure) {                          /* nothing to look for: say why, and no Find */
    el.browser.textContent = midiOf ? TEXT.insecure : TEXT.nomidi;
    el.browser.hidden = false;
    el.find.parentElement.hidden = true;
  }
  syncChoice();
  loadChoice();
  return {
    unmount() { window.removeEventListener("beforeunload", beforeUnload); if (st.access) st.access.onstatechange = null; container.replaceChildren(); },
    get busy() { return st.busy; },
  };
}
