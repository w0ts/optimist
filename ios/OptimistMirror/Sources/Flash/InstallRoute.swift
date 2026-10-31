// SPDX-License-Identifier: GPL-3.0-only
// The Firmware tab's half of tools/fm1_route.py and web/install_route.js (docs/INSTALL-PATHS.md): what runs, what the
// package is, whether the running firmware's update loader takes it, and the builder's BOOTLOADER with its KEEP-only
// override. The app cannot reach the chip's download mode (a USB mass-storage device: no raw USB on iOS; on the Mac the
// disk driver holds it and only root takes it, which a sandboxed app cannot), so where the route is download mode it says
// what to do on a computer and can send the soft key. It never writes m-bootloader (its full backup first is M-Flasher's).
import Foundation

enum InstallRoute {
    static let softKey: [UInt8] = [0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7]
    static let v15SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
    static let ownSlots = [0x93000, 0xA5000, 0xA6000, 0xAA000, 0xB5000, 0xBA000, 0xBE000, 0xCA000, 0xD0000]   // (every MEMORY step's flash.bin: tools/flash_layout.py own_slots, tests/flash_layout_test.py checks)
    static let mflasher = URL(string: "https://github.com/jvitkauskas/m-flasher/releases")!

    struct Package: Equatable {
        let kind: String          // v15 stock mboot optimist felucca fm1base other
        let product: String
        let flashLen: Int?
        let ownLoader: Bool
    }

    struct Route: Equatable {
        let method: String        // "loader" | "download"
        let why: String
        let softKey: Bool
        let keys: String
        var wantsMboot = false    // download mode only for the build's m-bootloader: the update keeping the bootloader is as good
    }

    // the running Optimist's word on its bootloader (tools/fm1_route.py boot_of_info, web/install_route.js bootOfInfo): the
    // editor's INFO, tag 0x68 "boot=mboot", "boot=stock" or "boot=unknown" (firmware/src/system/bootid.h)
    static let edInfo: [UInt8] = [0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7]

    /// whether this identity is asked (Optimist's, its release's)
    static func asksBoot(_ identity: String) -> Bool { m(identity, #"^FM-1_(7(00|[2-6]\d)|000|79\d{2,})$"#) }

    /// the word of INFO's tag 0x68 in an INFO reply, nil without (an older Optimist)
    static func bootOfInfo(_ b: [UInt8]) -> String? {
        var i = 0
        while i + 2 < b.count {
            if b[i] == 0x68 {
                let n = Int(b[i + 1])
                if i + 2 + n <= b.count, let t = String(bytes: b[(i + 2)..<(i + 2 + n)], encoding: .ascii),
                   ["boot=mboot", "boot=stock", "boot=unknown"].contains(t) { return String(t.dropFirst(5)) }
            }
            i += 1
        }
        return nil
    }

    /// true: m-bootloader is there; false: M-VAVE's bootloader is; nil: not known
    static func mbootPresent(_ word: String?) -> Bool? { word == "mboot" ? true : word == "stock" ? false : nil }

    /// ask a running Optimist which bootloader it has, over the channel its identity came on. Writes nothing
    static func askBoot(_ ch: FlashChannel, identity: String, timeout: TimeInterval = 1.0) async -> String? {
        guard asksBoot(identity) else { return nil }
        ch.drain()
        guard ch.send(edInfo) else { return nil }
        let end = Date().addingTimeInterval(timeout)
        while Date() < end {
            guard let p = await ch.read(timeout: end.timeIntervalSinceNow) else { return nil }
            if p.count > 5 && p[1] == 0x7D && p[2] == 0x46 && p[3] == 0x4C && p[4] == 1 { return bootOfInfo(p) }
        }
        return nil
    }

    private static let keys: [String: String] = [
        "felucca": "hold OCT- and OCT+ for 5 s", "sloop": "hold OCT- and OCT+ for 5 s",
        "optimist": "hold OCT- and OCT+ for 5 s while stopped, or while switching on for 3 s",
        "ambig000": "hold OCT- and OCT+ while switching on for 3 s", "fm1base": "hold REC while switching on"]
    private static let softKeyOK: Set<String> = ["stock", "felucca", "optimist", "ambig000", "fm1base"]

    private static func m(_ s: String, _ re: String) -> Bool { s.range(of: re, options: .regularExpression) != nil }

    static func runningKind(_ identity: String?, port: String = "") -> String {
        let t = (identity ?? "").trimmingCharacters(in: .whitespaces)
        if t.isEmpty { return "none" }
        if t.lowercased().hasPrefix("ota-") { return "loader" }
        if m(t, #"^FM1_(OFFICIAL_OPEN|COMMUNITY_\d+)$"#) { return "open" }
        if t == "FM-1_000" { return "ambig000" }
        if m(t, #"^FM-1_0(0[1-9]|1\d)$"#) { return "stock" }
        if m(t, #"^FM-1_0[2-9]\d$"#) && t != "FM-1_099" { return "baudgirl" }
        if m(t, #"^FM-1_7(00|[2-6]\d)$"#) { return "optimist" }
        if m(t, #"^FM-1_9\d\d$"#) { return port.lowercased().contains("sloop") ? "sloop" : "felucca" }
        if m(t, #"^FM-1_\d{4,}$"#) { return "fm1base" }
        return "other"
    }

    static func packageKind(product: String, sha256: String, flashLen: Int?, ownLoader: Bool = true) -> Package {
        let kind: String
        if sha256 == v15SHA256 { kind = "v15" }
        else if product == "FM-1_099" { kind = "mboot" }
        else if m(product, #"^FM-1_0(0[1-9]|1\d)$"#) { kind = "stock" }
        else if m(product, #"^FM-1_7(00|[2-6]\d)$"#), let f = flashLen, ownSlots.contains(f), ownLoader { kind = "optimist" }
        else if m(product, #"^FM-1_9\d\d$"#), let f = flashLen, ownSlots.contains(f) { kind = "felucca" }
        else if m(product, #"^FM-1_(0\d{6}|79\d{2,})$"#) || m(product, #"^FM-1_\d{4,}$"#) { kind = "fm1base" }
        else { kind = "other" }
        return Package(kind: kind, product: product, flashLen: flashLen, ownLoader: ownLoader)
    }

    static func loaderTakes(_ running: String, _ p: Package, runningText: String = "", mbootPresent: Bool? = nil) -> (Bool, String) {
        if p.kind == "mboot" { return (false, "m-bootloader is installed through download mode (a full backup first)") }
        if mbootPresent == true && (running == "optimist" || running == "ambig000") && (p.kind == "v15" || p.kind == "stock") {
            return (false, "m-bootloader is installed (the firmware says so): Optimist's updater refuses M-VAVE's package (-5, its head differs)")
        }
        switch running {
        case "loader": return (true, "the update loader finishes the cut-off install")
        case "felucca", "sloop":
            return p.kind == "fm1base" ? (false, "its updater is assumed to refuse fm1-base packages") : (true, "its own update loader")
        case "optimist", "ambig000":
            if p.kind == "v15" { return (true, "the way back to the official firmware (not with m-bootloader installed: the updater stops with -5, then download mode)") }
            guard let f = p.flashLen, ownSlots.contains(f) else { return (false, "Optimist's updater takes the layouts 0x93000, 0xA5000, 0xA6000, 0xAA000, 0xB5000, 0xBA000, 0xBE000, 0xCA000 and 0xD0000 only") }
            if p.kind == "fm1base" { return (false, "Optimist's updater refuses fm1-base packages") }
            if !p.ownLoader { return (false, "Optimist's updater takes the Felucca family's loader or M-VAVE's only") }
            return (true, "its own update loader")
        case "open":
            return p.kind == "v15" || p.kind == "stock" ? (true, "the Open firmware takes M-VAVE's own firmware")
                : (false, "the Open firmware's updater takes M-VAVE's own firmware only")
        case "stock":
            return p.product == runningText ? (false, "the stock updater refuses the identity it runs (\(runningText))")
                : (true, "the stock updater takes any other package")
        case "fm1base": return (true, "fm1-base's updater takes any package with a bootable head")
        case "baudgirl", "other": return (true, "its updater is assumed stock-like")
        default: return (false, "nothing answers on USB-MIDI")
        }
    }

    /// mbootPresent: the firmware's word (true: m-bootloader is there already, nothing to install; nil: not known, as before)
    static func choose(_ running: String, _ p: Package, bootloader: String = "keep", runningText: String = "",
                       mbootPresent: Bool? = nil) -> Route {
        let want = bootloader == "m-bootloader" && p.kind != "mboot" && mbootPresent != true
        var (takes, why) = loaderTakes(running, p, runningText: runningText, mbootPresent: mbootPresent)
        if bootloader == "m-bootloader" && mbootPresent == true && p.kind != "mboot" {
            why = "m-bootloader is on the FM-1 already (the firmware says so), kept; " + why
        }
        let soft = softKeyOK.contains(running)
        let k = mbootPresent == true ? "hold REC while switching on (m-bootloader)" + ((keys[running] ?? "").isEmpty ? "" : "; or " + keys[running]!)
            : keys[running] ?? ""
        if !want && takes { return Route(method: "loader", why: why, softKey: soft, keys: k) }
        return Route(method: "download", why: want ? "the build's BOOTLOADER is m-bootloader" : why, softKey: soft, keys: k,
                     wantsMboot: want && takes)
    }

    /// (choice, line): keep (the override) only turns m-bootloader into keep; no build setting keeps the bootloader
    static func bootloaderChoice(_ value: String?, keep: Bool) -> (String, String) {
        if value == "m-bootloader" && keep { return ("keep", "bootloader: keep (turned off for this install; the build says m-bootloader)") }
        if value == "m-bootloader" { return ("m-bootloader", "bootloader: m-bootloader (from the build): needs a full backup first, which M-Flasher makes on a computer") }
        if value == "keep" { return ("keep", "bootloader: keep (from the build): not touched") }
        return ("keep", "bootloader: keep (no build setting with this package): not touched")
    }

    /// BOOTLOADER in a builder .config ("BOOTLOADER=1": m-bootloader; absent: keep)
    static func bootloader(config text: String) -> String {
        for line in text.split(whereSeparator: \.isNewline) {
            let t = line.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: " ", with: "")
            if t.hasPrefix("BOOTLOADER=") { return t == "BOOTLOADER=1" ? "m-bootloader" : "keep" }
        }
        return "keep"
    }

    /// true when the running Optimist's updater may have refused an M-VAVE package (-5: the boot area differs, as with
    /// m-bootloader installed): the install stopped before anything was erased
    static func headRefused(_ running: String, _ p: Package, code: String) -> Bool {
        (running == "optimist" || running == "ambig000") && (p.kind == "v15" || p.kind == "stock") && (code == "noLoader" || code == "stopped")
    }

    static func refusedStep(file: String) -> String {
        "The FM-1 refused this package (error -5): its bootloader is not the one in the package, as when m-bootloader is installed. "
            + "Nothing was written. Use download mode: hold OCT- and OCT+ for 5 s while stopped, or hold REC while switching on (with m-bootloader), "
            + "then M-Flasher (\(mflasher.absoluteString)): Install firmware with \(file.isEmpty ? "PACKAGE.fwsc" : file), or on a computer python tools/fm1_install.py \(file.isEmpty ? "PACKAGE.fwsc" : file) --route download."
    }

    static func nextStep(_ r: Route, file: String) -> String {
        let enter = r.softKey ? "the button below sends the FM-1 into download mode" + (r.keys.isEmpty ? "" : " (or: \(r.keys))")
            : (r.keys.isEmpty ? "hold REC while switching on, with m-bootloader installed (else the USB_KEY dongle)" : r.keys)
        return "This install needs the chip's download mode (\(r.why)), which this app cannot reach. On a computer, run "
            + "python tools/fm1_install.py \(file.isEmpty ? "PACKAGE.fwsc" : file) (a full backup first), or M-Flasher (\(mflasher.absoluteString)): "
            + "Install firmware, with \"Also erase settings and Bluetooth pairings\" unticked for Optimist to Optimist. Download mode: \(enter)."
    }

    /// flash.bin's size in a logical package image (the UFW list, ENC 0xFFFF), nil without one
    static func flashLen(_ image: [UInt8]) -> Int? {
        func dec(_ off: Int, _ n: Int) -> [UInt8]? {
            guard off + n <= image.count else { return nil }
            var key = 0xFFFF, out = [UInt8](repeating: 0, count: n)
            for i in 0..<n {
                out[i] = image[off + i] ^ UInt8(key & 0xFF)
                key = ((key << 1) ^ ((key & 0x8000) != 0 ? 0x1021 : 0)) & 0xFFFF
            }
            return out
        }
        func rd(_ b: [UInt8], _ o: Int, _ n: Int) -> Int { (0..<n).reduce(0) { $0 | Int(b[o + $1]) << (8 * $1) } }
        guard let head = dec(0, 0x40) else { return nil }
        for i in 0..<min(rd(head, 8, 2), 11) {
            guard let e = dec(0x40 + i * 0x50, 0x50) else { return nil }
            if rd(e, 0, 2) == 0 { return rd(e, 12, 4) }
        }
        return nil
    }
}
