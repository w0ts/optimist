// SPDX-License-Identifier: GPL-3.0-only
//
// The web installer's half of tools/fm1_route.py (docs/INSTALL-PATHS.md): what runs, what the package is, and whether the
// running firmware's update loader takes it. A browser cannot reach the chip's download mode (a USB mass-storage device:
// WebUSB refuses that class, it is not HID, Web MIDI reaches a running firmware only), so where the route is download mode
// the page says what to do on a computer, and can send the soft key. The builder's BOOTLOADER is shown with its KEEP-only
// override; the page never writes m-bootloader (its full backup first is M-Flasher's). tests/anyflash_test.py checks that
// this file and fm1_route.py choose the same way for every pair of the matrix.

export const SOFT_KEY = [0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7];
export const V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a";
export const OWN_SLOTS = [0x93000, 0xB5000, 0xCA000, 0xD0000];   // (every MEMORY step's flash.bin: tools/flash_layout.py own_slots, tests/flash_layout_test.py checks)
// every app end an installed firmware of ours may have: [end, layout, user sample slots kept, MEMORY's name] (tools/flash_layout.py
// known_ends with its STEPS: tests/flash_layout_test.py checks this copy; the release's L1 ends at 0xCE000, the same step)
export const LAYOUT_ENDS = [[0x93000, 0, 2, "normal"], [0xB5000, 3, 0, "no user samples"], [0xCA000, 8, 0, "no user samples, 4 projects"], [0xCE000, 1, 0, "more flash"], [0xD0000, 1, 0, "more flash"]];
const BACKUP_WHERE = "Back up first: web editor > Backup.";
/** what installing the app ending at newEnd erases after the app ending at oldEnd (null: not known), by the switch rule
 * (docs/SD-MODE-DESIGN.md 9.7.2; tools/flash_layout.py switch_loss, the same words): [] the same step */
export function switchLoss(oldEnd, newEnd) {
  const row = (e) => LAYOUT_ENDS.find((r) => r[0] === e);
  const nw = row(newEnd);
  if (!nw) return [];
  const up = oldEnd == null ? null : Math.ceil(oldEnd / 0x1000) * 0x1000;
  const od = (up != null && row(up)) || LAYOUT_ENDS[0];
  if (up != null && row(up) && od[1] === nw[1]) return [];
  const out = ["the projects in flash", "the FM6 voices of the user presets"];
  const usr2 = od[2] >= 2 && nw[2] < 2, usr3 = od[2] >= 1 && nw[2] < 1;
  if (usr2 && usr3) out.push("the user samples (USR2, USR3) and the snapshots");
  else if (usr3) out.push("the user samples in USR3 and the snapshots");
  else if (usr2) out.push("the user samples in USR2");
  return out;
}
/** the one line before a layout change (the user, 2026-10-31: "when we flash a different layout, we warn the user to backup their
 * data first"); "" when nothing is erased; oldEnd null: the installed layout is not known, the line says so */
export function switchWarning(oldEnd, newEnd) {
  const loss = switchLoss(oldEnd, newEnd);
  if (!loss.length) return "";
  const what = loss.length > 1 ? `${loss.slice(0, -1).join(", ")} and ${loss[loss.length - 1]}` : loss[0];
  const name = LAYOUT_ENDS.find((r) => r[0] === newEnd)[3];
  return oldEnd == null ? `If the FM-1 has another memory layout than this firmware's (${name}), installing it erases ${what}. ${BACKUP_WHERE}`
    : `New memory layout (${name}): installing it erases ${what}. ${BACKUP_WHERE}`;
}
/** the installed app's end from the editor's bk_caps (ed_backup.c): a firmware of 2026-10-31 on says it (appEnd); an older one
 * built normal says so by its LAYOUT bit (410) off; otherwise null (not known) */
export function installedEndOf(caps) {
  if (!caps) return null;
  if (caps.appEnd) return caps.appEnd;
  const bits = caps.bits || [];
  const layoutOn = Math.floor((bits[Math.floor(410 / 7)] || 0) / 2 ** (410 % 7)) % 2 === 1;
  return bits.length > Math.floor(410 / 7) && !layoutOn ? 0x93000 : null;
}
export const MFLASHER_URL ="https://github.com/jvitkauskas/m-flasher/releases";   // (the fallback: the pinned release page is in the site's data, MFLASHER)
export const PATHS_URL = "https://github.com/w0ts/optimist/blob/main/docs/INSTALL-PATHS.md";

const KEYS = {
  stock: "", baudgirl: "", open: "", felucca: "hold OCT- and OCT+ for 5 s", sloop: "hold OCT- and OCT+ for 5 s",
  optimist: "hold OCT- and OCT+ for 5 s while stopped, or while switching on for 3 s",
  ambig000: "hold OCT- and OCT+ while switching on for 3 s", fm1base: "hold REC while switching on", other: "", none: "",
};
const SOFT_KEY_OK = new Set(["stock", "felucca", "optimist", "ambig000", "fm1base"]);

export function runningKind(identity, port = "") {
  const t = String(identity || "").trim();
  if (!t) return "none";
  if (/^ota-/i.test(t)) return "loader";
  if (/^FM1_(OFFICIAL_OPEN|COMMUNITY_\d+)$/.test(t)) return "open";
  if (t === "FM-1_000") return "ambig000";
  if (/^FM-1_0(0[1-9]|1\d)$/.test(t)) return "stock";
  if (/^FM-1_0[2-9]\d$/.test(t) && t !== "FM-1_099") return "baudgirl";
  if (/^FM-1_7(00|[2-6]\d)$/.test(t)) return "optimist";
  if (/^FM-1_9\d\d$/.test(t)) return /sloop/i.test(port) ? "sloop" : "felucca";
  if (/^FM-1_\d{4,}$/.test(t)) return "fm1base";
  return "other";
}

/** { kind, product, flashLen, ownLoader } from what the page knows of a package */
export function packageKind(product, sha256 = "", flashLen = null, ownLoader = true) {
  let kind;
  if (sha256 === V15_SHA256) kind = "v15";
  else if (product === "FM-1_099") kind = "mboot";
  else if (/^FM-1_0(0[1-9]|1\d)$/.test(product)) kind = "stock";
  else if (/^FM-1_7(00|[2-6]\d)$/.test(product) && OWN_SLOTS.includes(flashLen) && ownLoader) kind = "optimist";
  else if (/^FM-1_9\d\d$/.test(product) && OWN_SLOTS.includes(flashLen)) kind = "felucca";
  else if (/^FM-1_(0\d{6}|79\d{2,})$/.test(product) || /^FM-1_\d{4,}$/.test(product)) kind = "fm1base";
  else kind = "other";
  return { kind, product, flashLen, ownLoader };
}

/** [takes, why]: fm1_route.loader_takes */
export function loaderTakes(running, pkg, runningText = "") {
  if (pkg.kind === "mboot") return [false, "m-bootloader is installed through download mode (a full backup first)"];
  if (running === "loader") return [true, "the update loader finishes the cut-off install"];
  if (running === "felucca" || running === "sloop") {
    if (pkg.kind === "fm1base") return [false, "its updater is assumed to refuse fm1-base packages"];
    return [true, "its own update loader"];
  }
  if (running === "optimist" || running === "ambig000") {
    if (pkg.kind === "v15") return [true, "the way back to the official firmware"];
    if (!OWN_SLOTS.includes(pkg.flashLen)) return [false, `Optimist's updater takes the layouts ${OWN_SLOTS.map((s) => "0x" + s.toString(16)).join(", ")} only`];
    if (pkg.kind === "fm1base") return [false, "Optimist's updater refuses fm1-base packages"];
    if (!pkg.ownLoader) return [false, "Optimist's updater takes the Felucca family's loader or M-VAVE's only"];
    return [true, "its own update loader"];
  }
  if (running === "open") {
    if (pkg.kind === "v15" || pkg.kind === "stock") return [true, "the Open firmware takes M-VAVE's own firmware"];
    return [false, "the Open firmware's updater takes M-VAVE's own firmware only"];
  }
  if (running === "stock") {
    if (pkg.product === runningText) return [false, `the stock updater refuses the identity it runs (${runningText})`];
    return [true, "the stock updater takes any other package"];
  }
  if (running === "fm1base") return [true, "fm1-base's updater takes any package with a bootable head"];
  if (running === "baudgirl" || running === "other") return [true, "its updater is assumed stock-like"];
  return [false, "nothing answers on USB-MIDI"];
}

/** { method: "loader" | "download", why, softKey, keys }: fm1_route.choose */
export function chooseRoute(running, pkg, bootloader = "keep", runningText = "") {
  const want = bootloader === "m-bootloader" && pkg.kind !== "mboot";
  const [takes, why] = loaderTakes(running, pkg, runningText);
  const softKey = SOFT_KEY_OK.has(running), keys = KEYS[running] || "";
  if (!want && takes) return { method: "loader", why, softKey, keys };
  return { method: "download", why: want ? "the build's BOOTLOADER is m-bootloader" : why, softKey, keys };
}

/** { choice, line }: the build's BOOTLOADER as this install takes it; keep (the override) only turns m-bootloader into
    keep, never the other way; no build setting keeps the bootloader */
export function bootloaderChoice(value, keep = false) {
  if (value === "m-bootloader" && keep) return { choice: "keep", line: "bootloader: keep (unticked for this install; the build says m-bootloader)" };
  if (value === "m-bootloader") return { choice: "m-bootloader", line: "bootloader: m-bootloader (from the build): needs a full backup first, which M-Flasher makes on a computer" };
  if (value === "keep") return { choice: "keep", line: "bootloader: keep (from the build): not touched" };
  return { choice: "keep", line: "bootloader: keep (no build setting with this package): not touched" };
}

/** BOOTLOADER in a builder .config: "m-bootloader" / "keep" (an absent line is the default, keep) */
export function bootloaderFromConfig(text) {
  const m = /^\s*BOOTLOADER\s*=\s*(-?\d+)\s*$/m.exec(String(text || ""));
  return m && Number(m[1]) === 1 ? "m-bootloader" : "keep";
}

/** the next step when the route is download mode: the words the page and the Mirror app show */
export function nextStep(route, file = "PACKAGE.fwsc") {
  const enter = route.softKey ? "the button below sends the FM-1 into download mode" + (route.keys ? ` (or: ${route.keys})` : "")
    : (route.keys ? route.keys : "hold REC while switching on, with m-bootloader installed (else the USB_KEY dongle)");
  return `This install needs the chip's download mode (${route.why}), which a browser cannot reach. On a computer, run ` +
    `python tools/fm1_install.py ${file} (a full backup first), or M-Flasher (${MFLASHER_URL}): Install firmware, with ` +
    `"Also erase settings and Bluetooth pairings" unticked for Optimist to Optimist. Download mode: ${enter}.`;
}

/** the flash.bin size of a logical package image (fm1pkg.js ufwEntries' list), null without one */
export function flashLenOf(entries) {   // (fm1pkg.js ufwEntries: {0: flash.bin, 100: ota.bin})
  const e = entries && entries[0];
  return e ? e.len : null;
}

/** { os: "macos" | "windows" | "linux" | "ios" | "android" | "other", arch: "x86_64" | "aarch64" | "" (unknown) } of the visitor.
    nav: navigator (userAgent, userAgentData.platform); high: userAgentData.getHighEntropyValues(["architecture", "bitness"])
    when the page could ask it ({ architecture: "arm" | "x86", bitness: "64" }), else the user agent string alone */
export function detectPlatform(nav, high = null) {
  const ua = String((nav && nav.userAgent) || ""), hint = String((nav && nav.userAgentData && nav.userAgentData.platform) || "");
  let os = "other";
  if (/iPhone|iPad|iPod/.test(ua) || /^iOS$/i.test(hint)) os = "ios";
  else if (/Android/i.test(ua) || /^Android$/i.test(hint)) os = "android";
  else if (/^macOS$/i.test(hint) || (!hint && /Macintosh|Mac OS X/.test(ua))) os = "macos";
  else if (/^Windows$/i.test(hint) || (!hint && /Windows/.test(ua))) os = "windows";
  else if (/^(Linux|Chrome OS|Chromium OS)$/i.test(hint) || (!hint && /Linux|X11/.test(ua))) os = "linux";
  else if (/Macintosh|Mac OS X/.test(ua)) os = "macos";
  else if (/Windows/.test(ua)) os = "windows";
  else if (/Linux|X11/.test(ua)) os = "linux";
  if ((os === "macos" && nav && nav.maxTouchPoints > 1)) os = "ios";     // (an iPad asking for the desktop site)
  let arch = "";
  const a = high && String(high.architecture || "").toLowerCase();
  if (a === "arm") arch = "aarch64";
  else if (a === "x86") arch = "x86_64";
  else if (/aarch64|arm64|armv8/i.test(ua)) arch = "aarch64";
  else if (/x86_64|x86-64|x64|amd64|Win64|WOW64|Intel/i.test(ua)) arch = "x86_64";
  return { os, arch };
}

/** { mobile, os, files, version, page } from the site's data (tools/mflasher.py site_data(), generated into site.json):
    files: the release's files for this visitor (the dmg for a Mac, the zip for Windows, deb / rpm / tarball for the CPU for
    Linux; both CPUs' when the CPU is unknown), each { name, url, sha256, size, kind, arch };
    A phone or tablet cannot run M-Flasher: mobile, files empty. The page links these and downloads nothing itself. */
export function mflasherFor(data, platform) {
  const files = (data && data.files) || [];
  const os = platform.os;
  const mobile = os === "ios" || os === "android";
  const mine = mobile ? [] : files.filter(f => f.os === os && (f.arch === "any" || !platform.arch || f.arch === platform.arch));
  const order = { dmg: 0, zip: 0, deb: 1, rpm: 2, "tar.gz": 3 };
  mine.sort((a, b) => (a.arch === b.arch ? 0 : a.arch === "x86_64" ? -1 : 1) || order[a.kind] - order[b.kind]);
  return { mobile, os, files: mine, version: data && data.version, page: (data && data.page) || MFLASHER_URL };
}
