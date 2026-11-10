// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// The Optimist installer as a module: mountInstaller(container, options) puts the four steps (Connect, Choose,
// Install, When something goes wrong) into `container`. The same module runs the standalone page
// (web/installer/index.html, webapp/installer/ on the site) and is shipped as webapp/installer/installer.js for the
// one-page app, where the editor mounts it as a tab. The update itself is fm-1-base's web/fm1base/fm1ota.js and fm1pkg.js,
// unchanged (the M-UPGRADE protocol over Web MIDI), with Optimist's extras in web/fm1ota_optimist.js (the loader
// identities, the return to the official V15 and its voice banks). docs/WEB.md.
//
//   mountInstaller(container, {
//     packages,      [{ id, title, description, pkg, product, sha256 }]: the site's flavours (web/flavours.json via
//                    make_site.py); pkg a URL relative to the page
//     stock,         { pkg, sha256 }: M-VAVE's official V15 the site keeps (web/stock/, make_site.py; Apache-2.0), for
//                    the return to stock in step 4 without a download; none: the user's own file only
//     version,       the site's version ("0.1"); shown with the package
//     source,        the tag or commit the site was made from ("" when unknown): the Source link
//     repo,          "owner/name" whose cloud-builds branch holds the builds made with Build (default: REPO's); build:
//                    the id of the user's build to offer first (default: the page's ?build=, which its issue links to,
//                    else the builder's last finished build, localStorage MY_BUILD)
//     onNavigate,    (where, detail) => void, where one of "build" "backup" "editor" "about", or "sim" with { build }
//                    (a cloud build's "try it"), "build" with { cloud: stem } (its configuration): the host opens it (its tab, the
//                    About box); without it those are plain links (links below). The rescue guide is always a link.
//     links,         { build, backup, editor, about, docs }: the hrefs of those (defaults: the site's relative paths)
//     midi,          (options) => Promise<MIDIAccess>: default navigator.requestMIDIAccess (tests pass a mock)
//   }) -> { unmount(), busy }    busy: true while writing (the host must not unload or hide the page then)
import { checkUfw, logicalImage, productOf, sha256hex, ufwEntries } from "../fm1base/fm1pkg.js";
import { STOCK_V15_SHA256, OptimistUpdater, stockBanks, validateStockPackage } from "../fm1ota_optimist.js";
import { DROPPED, SOFT_KEY, fm1Devices, notChosen, resumePlan, askBoot, mbootPresentOf, bootloaderChoice, bootloaderFromConfig, chooseRoute, detectPlatform, headRefused, refusedStep, flashLenOf, installedEndOf, mflasherFor, nextStep, packageKind, runningKind, switchWarning } from "../install_route.js";

const STYLE_ID = "optimist-installer-style";
const BUNDLED_CSS = null;                           /* make_site.py puts web/installer/installer.css here */
const REPO = "https://github.com/w0ts/optimist";
const LOADER_MARK = "FELUCCA-LOADER-1";             /* firmware/loader: in every package this page installs (make_site.py) */
const isOta = (id) => /^ota-/i.test(id.model);
const STEP1_READS = 74;                             /* the reads of step 1 on a real FM-1 (SLOOP 2.0's loader) */
const DEFAULT_LINKS = { build: "../builder/", backup: "../editor/#backup", editor: "../editor/", sim: "../editor/", about: "../../LICENSING.md" };

/* every text on the page (English only). Checked against README.md ("Your first install", "Getting back") and the code (firmware/src/system/recovery.c, bootguard.h, main.c) */
export const TEXT = {
  title: "Install Optimist",
  what: "This page installs Optimist, a free, open-source firmware for the M-VAVE FM-1, from this browser over USB. " +
    "An install writes only the firmware area, not the area where the FM-1 keeps your projects, presets, kits and samples. " +
    "Optimist is a work in progress: an install can fail, its formats change between versions, and coming from another " +
    "firmware it may erase what it cannot read (SLOOP's data is kept). Back up first. No warranty.",
  s3: "Install", s4: "When something goes wrong",
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
  chooseLegend: "Firmware to install", relLabel: "Release", devBuild: "Development build",
  useRelease: "Official release", flavLabel: "Flavour",
  useFile: "A file on this computer (.fwsc)", fileLabel: "Package file",
  useMine: "Your build", mineTitle: "your build ", mineFail: "Could not load your build: ",
  mineGone: "Your build is no longer kept (30 days after it was last asked for): build it again.",
  ownTitle: "Build your own", buildOwn: "Make your build…", buildAnother: "Make another build…",
  buildOwnDesc: "Start from a flavour or one of your saved configurations, choose what goes in, build it in the cloud: it comes back here, ready to install.",
  pkgLoading: "Checking the package…", pkgReady: "Ready: ",
  pkgPickFile: "Choose a .fwsc file.",
  badpkg: "The package is damaged (its checksum does not match). Reload the page.",
  loadfail: "Could not load the package: ",
  notours: "This file is not a package this page can install (an Optimist, SLOOP or Felucca .fwsc, or the official FM-1 V15): ",
  noloaderpkg: "This package has no update loader in it, so it cannot be installed over USB.",
  damaged: "This package is damaged, nothing was sent to the FM-1 (download or build it again): ",
  stockTitle: "Official firmware V15 (your file)",
  stockNote: "The official firmware does not use Optimist's projects, presets, kits or samples: save your work with the editor's Backup first.",
  keep: "Keep the cable in and this page open until it says Done.",
  install: "Install", installFile: "Install ", installStock: "Install the official V15", installAgain: "Install again",
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
  notfound: "No FM-1 found. Check the cable (see under the Install button), close other apps that use MIDI, and press Install again.",
  model: "The connected device is not an FM-1: ",
  stopped: "The FM-1 stopped answering. Replug the USB cable and press Install again.",
  lost: "The USB connection was lost. Replug the cable and press Install again: the FM-1 stays in update mode until it is finished.",
  noloader: "The FM-1 did not come back in update mode. Replug the USB cable and press Install again: it stays in update mode until it is finished.",
  noreturn: "The FM-1 did not come back after writing. Switch it off and on.",
  /* the cable dropped mid-install (install_route.js DROPPED, the app's and fm1_install.py's sentence): Resume / Cancel */
  dropped: DROPPED, dropCancel: "Cancel", checking: "Checking what is already on the FM-1…", noPowerOff: "Do not power off the FM-1.",
  cancelled: "Cancelled. Nothing more was sent. The FM-1 stays in update mode: keep it on and press Install again to finish.", cancelledCheck: "Cancelled. Nothing was written.",
  unexpected: "The FM-1 came back as {id}, not the one being updated: nothing more was sent. Check which FM-1 this is, then press Install again.",
  mismatch: "Written, but the FM-1 reports another firmware: ",
  rescueBack: "Written, but the FM-1 started in USB rescue: the new firmware did not start. Install again, or choose another package.",
  foreign: "The FM-1 is waiting in another firmware's update mode. Complete that update with its own updater (M-UPGRADE), then click Install again:",
  badreq: "The FM-1 asked for data outside the package. Replug the cable and press Install again.",
  open: "This FM-1 runs M-VAVE's Open firmware, which takes only M-VAVE's own firmware: put the official firmware back first (step 4, Back to the official firmware V15), then install this. ",
  bootrefused: "The firmware is installed, but m-bootloader could not be: nothing else was changed.",
  resbad: "The resource file is not one, or is damaged (Optimist keeps no resource region): nothing was written.",
  /* other MIDI ports, when no FM-1 answered: the user picks one to try (a name that is none of the usual ones) */
  portsHead: "Other MIDI ports on this computer. If one is your FM-1, try it:",
  tryPort: "Try", portsWait: "Asking…", portsEmpty: "No other MIDI ports.",
  pickHead: "{n} FM-1s found. Choose one:", pickNeed: "Choose which FM-1 to use first.",
  portNone: "No FM-1 answered on {port}.", portOk: "Found {id} on {port}. This browser remembers it. Press the button again.",
  /* m-bootloader: the build's BOOTLOADER, shown; the page never writes it (its full backup first is M-Flasher's) */
  bootOpt: "Install m-bootloader instead (download mode on a computer, a full backup first). Off: the firmware goes over USB-MIDI and the bootloader stays.",
  bootWhy: "An open bootloader by Julius Vitkauskas (MIT): REC while switching on for download mode, PLAY for a recovery menu. It is installed with a full backup of the flash first, which only M-Flasher on a computer makes: this page installs the firmware over USB-MIDI and keeps the bootloader.",
  bootLater: " m-bootloader was not installed: on a computer, python tools/fm1_install.py with this package, or M-Flasher's Install m-bootloader (a full backup first).",
  mfTitle: "M-Flasher {v} for {os} (MIT, Julius Vitkauskas), checked by its SHA-256 after you download it: ", mfSha: "SHA-256 ", mfOther: "Other systems and all files", mfMobile: "This switch needs a computer: M-Flasher for macOS, Windows or Linux. ", mfNone: "M-Flasher: ",
  keepBoot: "Install over USB-MIDI instead, keeping this bootloader",
  needsDownload: "Not installed. ", softKey: "Switch the FM-1 to download mode (for M-Flasher on this computer)", softSent: "Sent: the FM-1 restarts in download mode (its screen stays as it was). Switch it off and on to leave it.",
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
  /* after the install, a build with Wi-Fi (ed_wifi.c, 112 WIFI): its network, sent once; the password is never kept */
  wifiTitle: "Wi-Fi", wifiWhy: "This build has Wi-Fi. Enter your network: the FM-1 joins it at every start.",
  wifiSsid: "Network name", wifiPass: "Password", wifiShow: "Show", wifiJoin: "Join",
  wifiBadSsid: "The network name is 1 to 32 bytes.", wifiBadPass: "The password is 8 to 63 characters (letters, digits, symbols; no accents).",
  wifiWait: "Waiting for the FM-1…", wifiNoAnswer: "The FM-1 did not answer. Keep the cable in and press Join again.",
  wifiRadio: "Starting the radio…", wifiJoining: "Joining “{n}”…", wifiAddr: "Joined; waiting for an address from the router…",
  wifiUp: "Connected to “{n}”, address {ip}. The FM-1 rejoins it at every start.", wifiSlow: "No connection yet. The FM-1 keeps trying; SYSTEM > WI-FI shows how it goes.",
  wifiRc1: "The FM-1 refused the name or the password.", wifiRc4: "The radio is still starting: press Join again in a few seconds.",
  wifiFail: ["The connection failed.", "The Wi-Fi radio cannot start: this FM-1 has no stored radio calibration.",
    "No network of that name was heard. Check the name, and that the FM-1 is within reach of the router.",
    "The router did not accept the FM-1 (authentication).", "The router refused the connection (association). Try again.",
    "The key exchange did not finish. Try again, closer to the router.", "Wrong password (or the network is not WPA2 personal with AES)."],
};

/* ---- Wi-Fi after the install (web/EDITOR_PROTOCOL.md "Wi-Fi"; the editor's Wi-Fi tab sends the same): F0 7D 46 4C 70 op ... F7 */
export const WIFI_CMD = 112;
const WIFI_HDR = [0x7D, 0x46, 0x4C];
export const WIFI_ST = { OFF: 0, RADIO: 1, UP: 8, FAIL: 9 };
/* 8-bit bytes as 7-bit groups: a byte of the high bits, then the 7 low bits of each (the editor's pack7, not fm1ota.js's) */
export function wifiPack7(src) {
  const out = [];
  for (let at = 0; at < src.length; at += 7) {
    const grp = Array.from(src.slice(at, at + 7));
    out.push(grp.reduce((hi, x, j) => hi | (((x >> 7) & 1) << j), 0), ...grp.map((x) => x & 0x7F));
  }
  return out;
}
export const wifiFrame = (op, data = []) => [0xF0, ...WIFI_HDR, WIFI_CMD, op, ...data, 0xF7];
/* the typed name and password -> { ssid bytes, pass } or { err: a TEXT key } (as the firmware's edwf_join1 checks) */
export function wifiCheckInput(name, pass) {
  const ssid = Array.from(new TextEncoder().encode(String(name || "")));
  if (ssid.length < 1 || ssid.length > 32) return { err: "wifiBadSsid" };
  if (pass.length < 8 || pass.length > 63 || /[^\x20-\x7e]/.test(pass)) return { err: "wifiBadPass" };
  return { ssid, pass };
}
export const wifiJoinData = (ssid, pass) => [ssid.length, pass.length, ...wifiPack7(ssid), ...Array.from(pass, (c) => c.charCodeAt(0))];
/* a reply -> its data bytes (after the op's command), or null when it is not a Wi-Fi reply */
export function wifiReplyOf(m) {
  const n = m ? m.length : 0;
  if (n < 7 || m[0] !== 0xF0 || m[n - 1] !== 0xF7 || m[1] !== WIFI_HDR[0] || m[2] !== WIFI_HDR[1] || m[3] !== WIFI_HDR[2] || m[4] !== WIFI_CMD) return null;
  return Array.from(m.slice(5, n - 1));
}
/* STATE's head: op 0, mode, st, fail, joined, ch, ip (5 x 7 bits, host order) */
export function wifiStateOf(a) {
  if (!a || a[0] !== 0 || a.length < 11) return null;
  let ip = 0;
  for (let k = 0; k < 5; k++) ip += a[6 + k] * 2 ** (7 * k);
  return { mode: a[1], st: a[2], fail: a[3], ip };
}
export const wifiIpText = (n) => [24, 16, 8, 0].map((sh) => Math.floor(n / 2 ** sh) % 256).join(".");

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
  try { checkUfw(logicalImage(bytes)); } catch (e) { throw codeError("damaged", e.message); }   /* (before anything is sent) */
  const sha256 = globalThis.crypto && globalThis.crypto.subtle ? await sha256hex(bytes) : "";
  if (expect && expect.sha256 && sha256 && sha256 !== expect.sha256) throw codeError("badpkg", `sha256 ${sha256}, expected ${expect.sha256}`);
  const image = logicalImage(bytes);
  return { product, sha256, image, stock: false };
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
  else if (k === "checking") { phase = 2; pct = 15 + Math.min(80, (a || 0) * 0.8); }   /* (the fast loader's check, as the write's scale) */
  else if (k === "write") { phase = 2; pct = 15 + Math.min(80, (Math.min(imageLength, (addr || 0) + 512) / Math.max(1, imageLength)) * 80); }
  else if (k === "reboot") { phase = 3; pct = 96; }
  else if (k === "done") { phase = 4; pct = 100; }
  return { phase, pct: Math.max(prev, Math.round(pct * 10) / 10) };
}

/* an error's message, plainly */
export function errorText(e) {
  if (e && e.code === "mismatch" && e.detail === "FM-1_000") return TEXT.rescueBack;
  if (e && e.code === "unexpected") return TEXT.unexpected.replace("{id}", e.detail || "another firmware");
  if (e && e.code === "cancelled" && e.detail === "check") return TEXT.cancelledCheck;
  if (e && e.code && typeof TEXT[e.code] === "string") return TEXT[e.code] + (["model", "mismatch", "foreign", "notours", "loadfail", "open"].includes(e.code) ? e.detail || "" : "");
  return TEXT.error + (e && e.message ? e.message : String(e));
}

/** the site's packages (site.json, make_site.py) by release: [{ key, label, idx }], idx their indices in packages. Each
 * release (its tag) newest first as listed, then this commit's development build (release ""), labelled with the commit */
export function releaseGroups(packages, source = "") {
  const groups = [];
  (packages || []).forEach((p, i) => {
    const key = (p && p.release) || "";
    let g = groups.find((x) => x.key === key);
    if (!g) {
      g = { key, idx: [], label: key ? `${key}${p.date ? ` · ${String(p.date).slice(0, 10)}` : ""}`
        : `${TEXT.devBuild}${/^[0-9a-f]{7,40}$/.test(source) ? ` (${source.slice(0, 7)})` : source ? ` (${source})` : ""}` };
      groups.push(g);
    }
    g.idx.push(i);
  });
  return [...groups.filter((g) => g.key), ...groups.filter((g) => !g.key)];
}

/* ---- the cloud builds (docs/WEB.md): the cloud-builds branch's index.json and files (tools/cloud_build.py
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
      flash: b.sizes && Number.isInteger(b.sizes.flash) ? b.sizes.flash : null,
      sim: b.sim === `${b.stem}.wasm` ? b.sim : null }));   /* (its simulator: web/sim/sim.js) */
}

/* the builder's last finished build (web/builder/cloud.js MY_BUILD, {id, name, issue, t}): offered, and picked when the
   page opens (the Mirror app does the same) */
const MY_BUILD = "optimist-my-build";
export function myBuild() {
  try { const v = JSON.parse(localStorage.getItem(MY_BUILD) || "null"); return v && /^[0-9a-f]{16}$/.test(v.id) ? { id: v.id, name: String(v.name || "").slice(0, 32), t: Number(v.t) || 0 } : null; }
  catch (e) { return null; }
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

/* ports the user picked although their names are none of the usual ones (Try): remembered in this browser (the key
   fm1-base's pages use), asked like the usual ones from then on */
const PICKED_KEY = "fm1-ports";
function pickedPorts() {
  try { return JSON.parse(localStorage.getItem(PICKED_KEY) || "[]").filter((x) => typeof x === "string"); } catch (_) { return []; }
}
function rememberPort(name) {
  const list = pickedPorts();
  if (list.includes(name)) return;
  try { localStorage.setItem(PICKED_KEY, JSON.stringify([...list, name])); } catch (_) { /* until the page is closed */ }
}
/* an updater that asks the remembered ports too */
function newUpdater(midi, log) {
  const up = new OptimistUpdater(midi, log);
  up.setNames(["optimist", "fm1-", "smk-37"]);   /* our names: "FM1-XXXX" since fm1-next, "Optimist" before; fm1ota.js has FM-1, FM1, Felucca */
  pickedPorts().forEach((n) => up.allow(n));
  return up;
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
  const packages = (opts.packages || []).filter((p) => p && p.release);   /* releases only, each with its version and date (the
                                                       development build of main is not offered: the user, 2026-10-25) */
  const version = opts.version || "";
  const source = opts.source || "";
  const links = { ...DEFAULT_LINKS, docs: `${REPO}/blob/${source || "main"}/docs/guide/getting-back.md`, ...(opts.links || {}) };
  const nav = (typeof navigator !== "undefined" && navigator) || {};
  const midiOf = opts.midi || (typeof nav.requestMIDIAccess === "function" ? (o) => nav.requestMIDIAccess(o) : null);
  const secure = typeof isSecureContext === "undefined" || isSecureContext;
  const repo = opts.repo || REPO.replace("https://github.com/", "");
  const fetchFn = opts.fetch || ((...a) => fetch(...a));
  const wanted = opts.build ?? (typeof location !== "undefined" ? new URLSearchParams(location.search).get("build") : null);
  const mine0 = /^[0-9a-f]{16}$/.test(wanted || "") ? { id: wanted, name: "", t: Date.now() } : myBuild();
  const fresh = !!mine0;                               /* (the user's build, when there is one, is picked at once: as the app) */
  addStyle(container.ownerDocument);

  const st = { access: null, choice: fresh ? "mine" : packages.length ? "0" : "file", pkg: null, pkgErr: "", busy: false, asked: false, bootWord: null, gen: 0,
    mine: mine0 };
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
  el.portsList = h("ul.oi-portlist");
  el.portsMsg = h("p.oi-portmsg", { role: "status", "aria-live": "polite" });
  el.pickList = h("ul.oi-portlist");
  el.pick = h("div.oi-ports.oi-pick", { hidden: true }, el.pickList);
  el.ports = h("div.oi-ports", { hidden: true }, h("p.o-dim", {}, TEXT.portsHead), el.portsList, el.portsMsg);
  /* (the FM-1 found, under the Install button: the Mirror app's footer) */
  const connectBox = h("div.oi-conn", {}, el.browser, h("div.oi-row", {}, el.found, el.find), el.pick, h("p.o-dim.oi-how", {}, TEXT.connectHow), el.help, el.ports);

  /* ---- 2 Choose */
  /* a release: its version, then its flavour (two menus in one row, as the Mirror app); st.choice is the package's index */
  const groups = releaseGroups(packages, source), groupOf = (i) => (packages[i] && packages[i].release) || "";
  st.group = groups.length ? groups[0].key : "";
  if (groups.length && st.choice === "0") st.choice = String(groups[0].idx[0]);   /* (the newest release's first flavour) */
  el.rel = h("select.oi-csearch.oi-rel", { "aria-label": TEXT.relLabel, onchange: () => pickGroup(el.rel.value) }, groups.map((g) => h("option", { value: g.key }, g.label)));
  el.flav = h("select.oi-csearch.oi-flav", { "aria-label": TEXT.flavLabel, onchange: () => { st.choice = el.flav.value; fillFlavours(); syncChoice(); loadChoice(); } });   /* (fillFlavours: its description) */
  el.flavDesc = h("span.oi-desc.o-dim");
  const relRow = groups.length ? choiceRow("rel", TEXT.useRelease, null, null,
    h("div.oi-relpick", {}, h("label.o-field", {}, h("span", {}, TEXT.relLabel), el.rel), h("label.o-field", {}, h("span", {}, TEXT.flavLabel), el.flav), el.flavDesc)) : null;
  function fillFlavours() {
    const g = groups.find((x) => x.key === st.group) || groups[0];
    if (!g) return;
    el.rel.value = g.key;
    el.flav.replaceChildren(...g.idx.map((i) => h("option", { value: String(i) }, packages[i].title)));
    el.flav.value = /^\d+$/.test(st.choice) && groupOf(+st.choice) === g.key ? st.choice : String(g.idx[0]);
    const p = packages[Number(el.flav.value)];
    el.flavDesc.textContent = p ? [p.product, p.description].filter(Boolean).join(" · ") : "";
  }
  function pickGroup(key) {
    st.group = key;
    fillFlavours();
    st.choice = el.flav.value;   /* (another release: its flavour of the same place, its first) */
    syncChoice();
    loadChoice();
  }
  fillFlavours();
  el.file = h("input.oi-file", { type: "file", accept: ".fwsc", "aria-label": TEXT.fileLabel, onchange: () => { st.choice = "file"; syncChoice(); loadChoice(); } });
  const fileRow = choiceRow("file", TEXT.useFile, null, null, el.file);
  el.mineInfo = h("span.oi-desc.o-dim");
  el.mineRow = choiceRow("mine", TEXT.useMine, null, null, el.mineInfo);
  el.own = h("button.o-btn", { type: "button", onclick: () => { if (opts.onNavigate) opts.onNavigate("build"); else location.href = links.build; } }, TEXT.buildOwn);
  el.stockRow = choiceRow("stock", TEXT.stockTitle, TEXT.stockNote, "FM-1_015");
  el.stockRow.hidden = true;
  el.pkginfo = h("p.oi-pkg", { role: "status", "aria-live": "polite" });

  /* ---- 3 Install */
  el.boot = h("input.oi-boot", { type: "checkbox", id: "oi-boot", checked: false, onchange: () => render() });
  el.bootLine = h("p.oi-bootline.o-dim", {}, "");
  el.bootOpt = h("div.oi-bootopt", { hidden: true }, h("label", { for: "oi-boot" }, el.boot, " ", TEXT.bootOpt), h("p.o-dim", {}, TEXT.bootWhy));
  el.bootRow = h("div.oi-bootrow", {}, el.bootLine, el.bootOpt);
  el.soft = h("button.o-btn.sm.oi-softkey", { type: "button", hidden: true, onclick: () => sendSoftKey() }, TEXT.softKey);
  el.go = h("button.o-btn.primary.big.oi-go", { type: "button", disabled: true, onclick: () => install() }, TEXT.install);
  el.phases = h("ol.oi-phases", {}, TEXT.phases.map((t) => h("li", { "data-st": "wait" }, t)));
  el.bar = h("progress.o-progress.oi-bar", { max: "100", value: "0", "aria-label": TEXT.s3 });
  el.status = h("p.oi-status", { role: "status", "aria-live": "polite" });
  el.err = h("p.o-msg.err.oi-err", { role: "alert", hidden: true });
  el.dropMsg = h("p.oi-dropmsg", {}, TEXT.dropped);
  el.dropCancel = h("button.o-btn.oi-dropcancel", { type: "button", onclick: () => { st.dropCancel = true; el.dropCancel.disabled = true; } }, TEXT.dropCancel);
  el.drop = h("div.o-msg.warn.oi-drop", { role: "alert", hidden: true }, el.dropMsg, h("div.oi-row", {}, el.dropCancel));
  el.ok = h("div.o-msg.ok.oi-ok", { hidden: true });
  /* Wi-Fi after the install: shown only when the installed build answers 112 WIFI; the password stays in the field until sent */
  el.wfName = h("input.o-input.oi-wfname", { type: "text", autocomplete: "off", autocapitalize: "off", spellcheck: "false", "aria-label": TEXT.wifiSsid, placeholder: TEXT.wifiSsid, maxlength: 32 });
  el.wfPass = h("input.o-input.oi-wfpass", { type: "password", autocomplete: "off", "aria-label": TEXT.wifiPass, placeholder: TEXT.wifiPass, maxlength: 63 });
  el.wfShow = h("input", { type: "checkbox", id: "oi-wfshow", onchange: () => { el.wfPass.type = el.wfShow.checked ? "text" : "password"; } });
  el.wfGo = h("button.o-btn.sm.oi-wfgo", { type: "button", onclick: () => wifiJoin() }, TEXT.wifiJoin);
  el.wfMsg = h("p.oi-wfmsg", { role: "status" });
  el.wifi = h("div.o-msg.oi-wifi", { hidden: true }, h("strong", {}, TEXT.wifiTitle), h("p.o-dim", {}, TEXT.wifiWhy),
    h("div.oi-row", {}, el.wfName, el.wfPass, h("label", { for: "oi-wfshow" }, el.wfShow, " ", TEXT.wifiShow), el.wfGo), el.wfMsg);
  el.log = h("pre.oi-log");
  el.stockWarn = h("p.o-msg.warn", { hidden: true }, TEXT.stockNote);
  /* Install: what to write (an official release's version and flavour, your build, a .fwsc file), then the button, its
     progress and the FM-1 found, together in one card (the Mirror app's Firmware tab, the user 2026-10-25) */
  const installCard = card("install", TEXT.s3,
    h("fieldset.oi-choices", {}, h("legend.o-sr", {}, TEXT.chooseLegend), relRow, el.mineRow, fileRow, el.stockRow),
    el.pkginfo, el.stockWarn, el.bootRow,
    el.go, h("p.o-dim.oi-keep", {}, TEXT.keep),
    el.phases, el.bar, el.status, el.drop, el.err, el.ok, el.wifi,
    connectBox,
    h("details.o-details.oi-logbox", {}, h("summary", {}, TEXT.logTitle), el.log));

  /* ---- Build your own: its own flow (the builder), a card of its own, first (the user, 2026-10-25), Install under it */
  const ownCard = h("section.o-panel.oi-step.oi-own", { "aria-labelledby": "oi-sown" },
    h("div.o-panel-h", {}, h("h2.oi-h", { id: "oi-sown" }, h("span", {}, TEXT.ownTitle))),
    h("div.o-panel-b", {}, h("p.o-dim", {}, TEXT.buildOwnDesc), el.own));

  /* ---- 4 When something goes wrong */
  el.stockFile = h("input.oi-file", { type: "file", accept: ".fwsc", "aria-label": TEXT.stockFile, onchange: () => loadStock() });
  el.stockMsg = h("p.oi-stockmsg", { role: "status", "aria-live": "polite" });
  const rescueBox = h("details.o-details.oi-rescuebox", {}, h("summary", {}, TEXT.s4),
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
    h("div.oi-steps", {}, ownCard, installCard, rescueBox),
    h("footer.oi-foot", {}, el.meta,
      h("p.o-dim", {}, h("a", { href: srcHref, target: "_blank", rel: "noopener" }, TEXT.source), " · ", link("about", TEXT.about))));
  container.replaceChildren(root);

  function card(name, title, ...body) {   /* (neutral: the track colours are the device's, not the installer's) */
    const hid = `oi-s${name}`;
    return h("section.o-panel.oi-step", { "aria-labelledby": hid },
      h("div.o-panel-h", {}, h("h2.oi-h", { id: hid }, h("span", {}, title))),
      h("div.o-panel-b", {}, ...body));
  }
  function choiceRow(value, title, desc, product, extra = null) {
    const id = `oi-c-${value}`;
    const input = h("input.oi-radio", { type: "radio", name: "oi-choice", id, value, onchange: () => { st.choice = value === "rel" ? el.flav.value : value; syncChoice(); loadChoice(); } });
    return h("div.oi-choice", { "data-value": value },
      input,
      h("label", { for: id }, h("b", {}, title), product ? h("span.o-tag.outline", {}, product) : null, desc ? h("span.oi-desc.o-dim", {}, desc) : null),
      extra);
  }
  function syncChoice() {
    for (const r of root.querySelectorAll(".oi-radio")) r.checked = r.value === st.choice || (r.value === "rel" && /^\d+$/.test(st.choice));
    if (/^\d+$/.test(st.choice) && groupOf(+st.choice) !== st.group) { st.group = groupOf(+st.choice); fillFlavours(); }
    el.flav.disabled = el.rel.disabled = st.busy;
    el.own.textContent = st.mine ? TEXT.buildAnother : TEXT.buildOwn;
    el.file.disabled = st.busy;
    el.mineRow.hidden = !st.mine;
    el.mineInfo.textContent = st.mine ? `${st.mine.name ? st.mine.name + " · " : ""}${st.mine.id}` : "";
    for (const r of root.querySelectorAll(".oi-radio")) r.disabled = st.busy;
  }

  /* ---- the package */
  async function loadChoice() {
    const gen = ++st.gen;
    st.pkg = null; st.pkgErr = "";
    render();
    if (st.choice === "stock") { st.pkg = st.stock || null; render(); return; }
    say(el.pkginfo, st.choice === "file" && !(el.file.files && el.file.files[0]) ? TEXT.pkgPickFile : TEXT.pkgLoading);
    try {
      let pkg;
      if (st.choice === "file") {
        const f = el.file.files && el.file.files[0];
        if (!f) return;
        pkg = await checkPackage(new Uint8Array(await f.arrayBuffer()));
        pkg.title = pkg.stock ? TEXT.stockTitle : f.name;
        pkg.file = f.name;
      } else if (st.choice === "mine") {
        if (!st.mine) return;
        let shelf;
        try { shelf = await loadCloud(repo, fetchFn); } catch (e) { throw codeError("loadfail", e.message); }
        const b = shelf.builds.find((x) => x.id === st.mine.id);
        if (!b) { if (gen === st.gen) { st.pkgErr = TEXT.mineGone; render(); } return; }
        if (!st.mine.name) { st.mine = { ...st.mine, name: b.name }; syncChoice(); }
        const r = await fetchFn(cloudFileUrl(repo, shelf.sha, b.fwsc));
        if (!r.ok) throw codeError("loadfail", `HTTP ${r.status}`);
        pkg = await checkPackage(new Uint8Array(await r.arrayBuffer()));
        if (pkg.stock || (pkg.sha256 && pkg.sha256 !== b.sha256)) throw codeError("badpkg", `sha256 ${pkg.sha256}, expected ${b.sha256}`);
        pkg.title = `${b.name} (${TEXT.mineTitle}${b.id})`;
        try {                                        /* (its .config beside it on the shelf: BOOTLOADER) */
          const c = await fetchFn(cloudFileUrl(repo, shelf.sha, b.config));
          pkg.bootloader = c.ok ? bootloaderFromConfig(await c.text()) : null;
        } catch (_) { pkg.bootloader = null; }
        pkg.file = b.fwsc;
      } else {
        const meta = packages[Number(st.choice)];
        const r = await fetch(meta.pkg, { cache: "no-cache" });   /* (Pages lets a browser keep a file 10 min: a newer deploy's package against an older page's checksum read "damaged") */
        if (!r.ok) throw codeError("loadfail", `HTTP ${r.status}`);
        pkg = await checkPackage(new Uint8Array(await r.arrayBuffer()), meta);
        pkg.title = meta.title; pkg.version = meta.release || version;
        pkg.bootloader = meta.bootloader === "m-bootloader" || meta.bootloader === "keep" ? meta.bootloader : null;
        pkg.file = decodeURIComponent(String(meta.pkg).split(/[/?#]/).filter(Boolean).pop() || "");
      }
      if (gen !== st.gen) return;
      if (pkg.bootloader === undefined) pkg.bootloader = null;
      el.boot.checked = false;                       /* (a new package: USB-MIDI again) */
      st.pkg = pkg;
    } catch (e) {
      if (gen !== st.gen) return;
      st.pkgErr = e && e.code === "loadfail" ? TEXT.loadfail + (e.detail || e.message) : errorText(e);
      if (e && e.detail && e.code === "badpkg") log(e.detail);
    }
    render();
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
    /* (the button names the file it writes: nothing is installed blindly; the user, for the app and the page alike) */
    el.go.textContent = p && p.stock ? TEXT.installStock : p && p.file ? TEXT.installFile + p.file : TEXT.install;
    el.go.disabled = !p || st.busy || !midiOf || !secure;
    el.find.disabled = st.busy || !midiOf || !secure;
    el.stockWarn.hidden = !(p && p.stock);
    const bv = p && !p.stock ? p.bootloader : null;                    /* (the build's BOOTLOADER, null: no setting) */
    el.bootRow.hidden = !p || !!p.stock;                                /* (never on the return to the official firmware) */
    el.bootOpt.hidden = st.bootWord === "mboot";                        /* (explicit: only the button installs m-bootloader) */
    el.bootLine.textContent = p ? bootloaderChoice(bv, el.boot.checked).line +
      ({ mboot: ". The FM-1 has m-bootloader already (it says so).", stock: ". The FM-1 has M-VAVE's bootloader (it says so)." }[st.bootWord] || "") : "";
    el.boot.disabled = st.busy;
    el.meta.replaceChildren(...(p ? [h("span", {}, `${p.title}${p.version ? " " + p.version : ""}`), " · ", h("span", {}, p.product),
      source && !p.stock && st.choice !== "file" && st.choice !== "mine" ? [" · ", TEXT.builtFrom, h("span.o-mono", {}, source)] : [],
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
  /* which FM-1 (web/install_route.js fm1Devices): with two or more on the cable the page asks and sends nothing before the
     answer; st.others: the keys of the ones not chosen, kept from the choice on (a loader's port may carry another name) */
  const liveNames = (midi) => [...midi.inputs.values()].filter((p) => p.state !== "disconnected").map((p) => p.name || "");
  function needChoice(midi) {
    const keys = fm1Devices(liveNames(midi));
    if (keys.length < 2) { st.chosen = null; st.others = new Set(); showPick([]); return false; }
    if (st.chosen && keys.includes(st.chosen)) { st.others = new Set(keys.filter((k) => k !== st.chosen)); showPick(keys); return false; }
    st.chosen = null; st.others = new Set();
    showPick(keys);
    return true;
  }
  /* the ports the updater may ask: all, or without the FM-1s not chosen (their keys, and another FM1-XXXX than the chosen one's) */
  function viewOf(midi) {
    if (!st.chosen) return midi;
    const keep = (m) => new Map([...m].filter(([, p]) => !notChosen(p.name, st.chosen, st.others)));
    return { get inputs() { return keep(midi.inputs); }, get outputs() { return keep(midi.outputs); } };
  }
  function showPick(keys) {
    el.pickList.replaceChildren(...keys.map((k) => h("li", {}, h("span", {}, k),
      h("button.o-btn.sm", { type: "button", "aria-pressed": String(k === st.chosen), "aria-label": `${k}${k === st.chosen ? " (chosen)" : ""}`, disabled: st.busy,
        onclick: () => { st.chosen = k; detect(true); } }, k === st.chosen ? "Chosen" : "Use"))));
    el.pick.hidden = keys.length < 2;
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
      const midi = await access();
      if (needChoice(midi)) {
        say(el.found, TEXT.pickHead.replace("{n}", String(fm1Devices(liveNames(midi)).length)), "err");
        el.ports.hidden = true; el.find.textContent = TEXT.findAgain;
        return;
      }
      const dev = await newUpdater(viewOf(midi), log).findAny();
      if (dev) { dev.link.close(); st.running = { text: dev.id.text, port: dev.name || "" }; say(el.found, TEXT.found + describeIdentity(dev.id), "ok"); el.ports.hidden = true; }
      else { say(el.found, TEXT.notfoundShort, "err"); el.help.hidden = false; showPorts(viewOf(midi)); }
      el.find.textContent = TEXT.findAgain;
    } catch (e) {
      if (user || e.code !== "denied") say(el.found, errorText(e), "err");
    }
  }

  /* the ports no name asked (the usual FM-1 names, the remembered ones): each with Try, which asks it and, when an FM-1
     answers, remembers it */
  function showPorts(midi) {
    const up = newUpdater(midi, log);
    const names = up.ports().filter((n) => !up.askable(n));
    el.portsList.replaceChildren(...(names.length ? names.map((n) => h("li", {}, h("span", {}, n),
      h("button.o-btn.sm", { type: "button", "aria-label": `${TEXT.tryPort}: ${n}`, onclick: (ev) => tryPort(midi, n, ev.currentTarget) }, TEXT.tryPort)))
      : [h("li", {}, TEXT.portsEmpty)]));
    el.ports.hidden = false;
  }
  async function tryPort(midi, name, button) {
    if (st.busy) return;
    button.disabled = true;
    say(el.portsMsg, TEXT.portsWait);
    let id = null;
    try { id = await newUpdater(midi, log).probe(name); } catch (_) { /* no answer */ }
    button.disabled = false;
    if (!id) { say(el.portsMsg, TEXT.portNone.replace("{port}", name), "err"); return; }
    rememberPort(name);
    showPorts(midi);
    say(el.portsMsg, TEXT.portOk.replace("{port}", name).replace("{id}", id.text), "ok");
    detect(false);
  }

  /* ---- install */
  function setPhase(i, state) {
    [...el.phases.children].forEach((li, k) => {
      li.dataset.st = state === "fail" && k === i ? "fail" : k < i ? "done" : k === i ? (state === "done" ? "done" : "on") : "wait";
      if (k === i && state !== "fail") li.setAttribute("aria-current", "step"); else li.removeAttribute("aria-current");
    });
  }
  /* the layout warning of this package (web/install_route.js switchWarning): the installed layout from the editor's last list of
     the connected FM-1 (opts.deviceCaps, the one-page app), else not known; "" for M-VAVE's own firmware or the same layout.
     known: true when the installed layout is known to differ (the question is asked again then, whatever was answered before) */
  function layoutWarning(pkg) {
    if (pkg.stock) return { line: "", known: false };
    let flashLen = null;
    try { flashLen = flashLenOf(ufwEntries(pkg.image)); } catch (_) { return { line: "", known: false }; }
    let caps = null;
    try { caps = opts.deviceCaps ? opts.deviceCaps() : null; } catch (_) { caps = null; }
    const installed = installedEndOf(caps);
    return { line: switchWarning(installed, flashLen), known: installed != null };
  }
  function ask(stock, warn = { line: "", known: false }) {
    if (st.asked && !(warn.line && warn.known)) return Promise.resolve("go");
    return new Promise((resolve) => {
      const close = (v) => { dlg.close(); dlg.remove(); resolve(v); };
      const dlg = h("dialog.o-pop.oi-ask", { "aria-labelledby": "oi-ask-h", oncancel: (ev) => { ev.preventDefault(); close("cancel"); } },
        h("h3", { id: "oi-ask-h" }, TEXT.askTitle),
        h("p", {}, stock ? TEXT.askBodyStock : TEXT.askBody),
        warn.line ? h("p.o-msg.warn.oi-layout", {}, warn.line) : null,
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
    try {
      if (needChoice(await access())) { say(el.found, TEXT.pickNeed, "err"); el.pick.scrollIntoView?.({ block: "nearest" }); return; }
    } catch (_) { /* denied: the steps below say so */ }
    const answer = await ask(pkg.stock, layoutWarning(pkg));
    if (answer === "backup") {
      if (opts.onNavigate) opts.onNavigate("backup"); else window.open(links.backup, "_blank", "noopener");
      return;
    }
    if (answer !== "go") return;
    st.asked = true;
    el.err.hidden = el.ok.hidden = el.wifi.hidden = true;
    el.bar.value = 0;
    setPhase(-1);
    let pct = 0, phase = -1, maxAddr = 0;
    const step = (k, a, addr) => {
      log(`${k} ${a ?? ""}`);
      if (!el.drop.hidden) { el.drop.hidden = true; if (phase >= 0) setPhase(Math.min(phase, 3)); }   /* (the FM-1 is back: going on) */
      if (k === "write" && addr !== undefined) maxAddr = Math.max(maxAddr, addr);
      const r = progressOf(k, a, maxAddr, pkg.image.length, pct);
      pct = r.pct; el.bar.value = pct;
      if (r.phase >= 0 && r.phase !== phase) { phase = r.phase; setPhase(Math.min(phase, 3), phase > 3 ? "done" : "on"); }
      /* (from step 2 on, until done: never power off; the user, 2026-11-01) */
      if (r.phase >= 0 && r.phase <= 3) say(el.status, (k === "checking" ? TEXT.checking : TEXT.phases[r.phase] + "…") + (r.phase >= 1 ? " " + TEXT.noPowerOff : ""));
    };
    let midi;
    try { midi = await access(); }
    catch (e) { showError(e, phase); return; }
    st.busy = true; syncChoice(); render();
    el.ports.hidden = true;
    if (st.detecting) await st.detecting;            /* (one handshake at a time on the port) */
    try {
      const up = newUpdater(viewOf(midi), log);
      const dev = await up.findAny();               /* (none: the steps below say so, as before) */
      const route = dev ? await routeOf(pkg, dev) : { method: "loader" };
      if (route.method === "download") { needsDownload(route, pkg); st.busy = false; syncChoice(); render(); return; }
      const onStep = (k, a) => step(k, a, k === "write" ? up.lastAddr : undefined);
      const ota = await up.find(isOta);
      if (ota) ota.link.close();
      /* an interrupted install (or return to V15): finish it; else the whole install. A dropped cable: said, and the install
         goes on by itself when the FM-1 is back (Cancel stops it) */
      const first = ota ? async () => { setPhase(2); return up.resumeOrThrow(pkg.image, onStep, pkg.product); }
        : () => up.install(pkg.image, pkg.product, onStep, pkg.sha256);
      const back = await up.resumable(first, (e) => showDrop(e, phase),
        { image: pkg.image, product: pkg.product, sha256: pkg.sha256, running: dev ? dev.id.text : "", onStep, plan: resumePlan,
          cancelled: () => st.dropCancel });
      setPhase(4, "done");
      el.bar.value = 100;
      say(el.status, "");
      el.ok.replaceChildren(h("span", {}, `${TEXT.done}${pkg.title} (${back}).`), " ",
        pkg.stock ? (pkg.banks ? h("div.oi-banks", {}, h("p.o-dim", {}, TEXT.banksNote),
          h("button.o-btn.sm.oi-banksgo", { type: "button", onclick: () => restoreBanks(pkg.banks) }, TEXT.banksBtn)) : null)
          : h("button.o-btn.sm", { type: "button", onclick: () => (opts.onNavigate ? opts.onNavigate("editor") : (location.href = links.editor)) }, TEXT.openEditor));
      el.ok.hidden = false;
      el.go.textContent = TEXT.installAgain;
      if (!pkg.stock) offerWifi(midi);               /* (not awaited: the FM-1 is still starting) */
    } catch (e) {
      el.drop.hidden = true;
      showError(e, phase);
      if (st.head && headRefused(st.head.running, { kind: st.head.kind }, e && e.code)) { el.err.textContent = refusedStep(pkg.file || "PACKAGE.fwsc"); st.head = null; }
      if (e && e.code === "notfound") showPorts(viewOf(midi));
      log(String(e && e.stack || e));
    }
    st.busy = false; syncChoice(); render();
    if (!el.ok.hidden) el.go.textContent = TEXT.installAgain;
  }
  /* ---- Wi-Fi after the install (the user, 2026-11-10: "an option to enter ssid and password"). Nothing is built into the
     package (a cloud build is public): the name and password go to the FM-1 over USB, as the editor's Wi-Fi tab sends them */
  async function wifiLink(midi, ms) {               /* the running Optimist (not its update loader) -> its link, or null */
    const end = Date.now() + ms;
    do {
      const dev = await newUpdater(viewOf(midi), log).find((id) => !isOta(id)).catch(() => null);
      if (dev) return dev.link;
      await new Promise((r) => setTimeout(r, 1000));
    } while (Date.now() < end);
    return null;
  }
  async function wifiAsk(link, op, data = [], ms = 1500) {   /* one request -> the reply's bytes, or null */
    link.drain();
    if (!link.send(wifiFrame(op, data))) return null;
    const end = Date.now() + ms;
    for (let left = ms; left > 0; left = end - Date.now()) {
      const a = wifiReplyOf(await link.read(left));
      if (a && a[0] === op) return a;
    }
    return null;
  }
  /* one user of the port at a time (as detect): a Link's close clears the port's handler, so two at once would cut each other off */
  async function onePort(fn) {
    while (st.detecting) await st.detecting.catch(() => {});
    st.detecting = fn().finally(() => { st.detecting = null; });
    return st.detecting;
  }
  function offerWifi(midi) {                       /* the FM-1 answered as the package at Done: ask once whether it has Wi-Fi */
    return onePort(async () => {
      const link = await wifiLink(midi, 5000);
      if (!link) return;
      try {
        const s = wifiStateOf(await wifiAsk(link, 0));
        log(`wifi: ${s ? `mode ${s.mode} st ${s.st}` : "none in this build"}`);
        if (s) { el.wifi.hidden = false; el.wfMsg.textContent = ""; }
      } finally { link.close(); }
    }).catch((e) => log(String(e && e.stack || e)));
  }
  async function wifiJoin() {
    const c = wifiCheckInput(el.wfName.value, el.wfPass.value);
    const msg = (t, kind = "") => { el.wfMsg.textContent = t; el.wfMsg.dataset.kind = kind; };
    if (c.err) { msg(TEXT[c.err], "err"); return; }
    el.wfGo.disabled = true;
    msg(TEXT.wifiWait);
    const midi = await access().catch(() => null);
    if (!midi) { el.wfGo.disabled = false; msg(TEXT.wifiNoAnswer, "err"); return; }
    return onePort(() => wifiJoinOn(midi, c, msg));
  }
  async function wifiJoinOn(midi, c, msg) {
    let link = null;
    try {
      link = await wifiLink(midi, 8000);
      let s = link && wifiStateOf(await wifiAsk(link, 0));
      if (!s) { msg(TEXT.wifiNoAnswer, "err"); return; }
      if (s.mode !== 1 && s.mode !== 2) {          /* OFF, JANK or AP: AUTO, so it joins now and at every start */
        if (!(await wifiAsk(link, 1, [1]))) { msg(TEXT.wifiNoAnswer, "err"); return; }
      }
      for (let k = 0; k < 15 && s && (s.st === WIFI_ST.OFF || s.st === WIFI_ST.RADIO); k++) {   /* (JOIN wants the radio up) */
        msg(TEXT.wifiRadio);
        await new Promise((r) => setTimeout(r, 1000));
        s = wifiStateOf(await wifiAsk(link, 0));
      }
      const name = el.wfName.value;
      const r = await wifiAsk(link, 3, wifiJoinData(c.ssid, c.pass));
      el.wfPass.value = "";                          /* (sent once; never kept) */
      if (!r) { msg(TEXT.wifiNoAnswer, "err"); return; }
      if (r[1]) { msg(r[1] === 4 ? TEXT.wifiRc4 : TEXT.wifiRc1, "err"); return; }
      for (let k = 0; k < 60; k++) {                 /* the join: about a minute at most */
        await new Promise((res) => setTimeout(res, 1000));
        s = wifiStateOf(await wifiAsk(link, 0));
        if (!s) continue;
        if (s.st === WIFI_ST.UP && s.ip) { msg(TEXT.wifiUp.replace("{n}", name).replace("{ip}", wifiIpText(s.ip)), "ok"); return; }
        if (s.st === WIFI_ST.FAIL) { msg(TEXT.wifiFail[s.fail] || TEXT.wifiFail[0], "err"); return; }
        msg(s.st === WIFI_ST.UP ? TEXT.wifiAddr : TEXT.wifiJoining.replace("{n}", name));
      }
      msg(TEXT.wifiSlow);
    } catch (e) {
      msg(TEXT.wifiNoAnswer, "err");
      log(String(e && e.stack || e));
    } finally {
      if (link) link.close();
      el.wfGo.disabled = false;
    }
  }
  /* the cable dropped (the user, 2026-11-01: "the flasher should automatically resume"): "Connection dropped. Check the
     cable: the install continues by itself when the FM-1 is back." with Cancel; the next step hides it */
  function showDrop(e, phase) {
    log(`dropped: ${e && e.message || e}`);
    if (phase >= 0) setPhase(Math.min(phase, 3), "fail");
    say(el.status, "");
    st.dropCancel = false;
    el.dropCancel.disabled = false;
    el.dropMsg.textContent = TEXT.dropped;
    el.drop.hidden = false;
  }
  /* the way for this package (web/install_route.js, as tools/fm1_route.py): the running firmware's loader, or the
     chip's download mode, which a browser cannot reach. dev: the FM-1 found now (closed here) */
  async function routeOf(pkg, dev) {
    st.bootWord = await askBoot(dev.link, dev.id.text).catch(() => null);   /* (a running Optimist says which bootloader it has) */
    dev.link.close();
    if (st.bootWord) log(`bootloader: ${st.bootWord} (the firmware says so)`);
    const r = st.running = { text: dev.id.text, port: dev.name || "" };
    st.head = { running: runningKind(r.text, r.port), kind: pkg.stock ? "v15" : "" };
    if (pkg.stock) return { method: "loader" };
    let flashLen = null;
    try { flashLen = flashLenOf(ufwEntries(pkg.image)); } catch (_) { /* (checkPackage parsed it already) */ }
    const info = packageKind(pkg.product, pkg.sha256, flashLen, true);
    const boot = bootloaderChoice(pkg.bootloader, el.boot.checked).choice;
    st.head.kind = info.kind;
    const route = chooseRoute(runningKind(r.text, r.port), info, boot, r.text, mbootPresentOf(st.bootWord));
    log(`route ${route.method}: ${route.why}`);
    return route;
  }
  function needsDownload(route, pkg) {
    say(el.status, "");
    const mf = h("div", { class: "mflasher" });
    /* (m-bootloader asked for, not on the FM-1: the install over USB-MIDI that keeps the bootloader is the other choice, as good) */
    const keep = route.wantsMboot ? h("button.o-btn.sm.oi-keepboot", { type: "button", onclick: () => { el.boot.checked = false; render(); install(); } }, TEXT.keepBoot) : null;
    el.err.replaceChildren(h("p", {}, TEXT.needsDownload + nextStep(route, pkg.file || "PACKAGE.fwsc")), ...(keep ? [keep] : []), mf, ...(route.softKey ? [el.soft] : []));
    mflasherLinks(mf);
    el.soft.hidden = !route.softKey;
    el.err.hidden = false;
  }
  /* M-Flasher for this visitor's system: direct links to the pinned release's files with their SHA-256 (opts.mflasher, from
     tools/mflasher.py), and the release page for the other systems. The page downloads nothing itself. */
  async function mflasherLinks(box) {
    let high = null;
    try { high = nav.userAgentData && nav.userAgentData.getHighEntropyValues ? await nav.userAgentData.getHighEntropyValues(["architecture", "bitness"]) : null; } catch (e) { high = null; }
    const view = mflasherFor(opts.mflasher, detectPlatform(nav, high));
    const other = h("a", { href: view.page, target: "_blank", rel: "noopener" }, TEXT.mfOther);
    if (view.mobile) { box.replaceChildren(h("p", {}, TEXT.mfMobile, other)); return; }
    if (!view.files.length) { box.replaceChildren(h("p", {}, TEXT.mfNone, other)); return; }
    const osName = { macos: "macOS", windows: "Windows", linux: "Linux" }[view.os] || view.os;
    box.replaceChildren(h("p", {}, TEXT.mfTitle.replace("{v}", view.version || "").replace("{os}", osName)), h("ul", {},
      ...view.files.map((f) => h("li", {}, h("a", { href: f.url, rel: "noopener" }, f.name), ` (${(f.size / 1e6).toFixed(1)} MB) `, TEXT.mfSha, h("code", {}, f.sha256)))),
      h("p", {}, other));
  }
  async function sendSoftKey() {                   /* the soft key, F0 22 24 35 7D F7: the FM-1 restarts in download mode */
    try {
      const dev = await newUpdater(await access(), log).find();
      if (!dev) { say(el.status, TEXT.notfoundShort, "err"); return; }
      dev.link.send(SOFT_KEY);
      dev.link.close();
      say(el.status, TEXT.softSent, "ok");
    } catch (e) { showError(e, -1); }
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
      await new Updater(viewOf(midi), log).restoreBanks(banks, (n) => {
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
    /** the user's build chosen from outside (the builder's and the simulator's "Flash it"): offered first and picked */
    openBuild(id) {
      if (st.busy || !/^[0-9a-f]{16}$/.test(id || "")) return;
      st.mine = st.mine && st.mine.id === id ? st.mine : { id, name: "", t: Date.now() };
      st.choice = "mine";
      syncChoice();
      loadChoice();
    },
  };
}
