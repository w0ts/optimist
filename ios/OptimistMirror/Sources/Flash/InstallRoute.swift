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
    static let ownSlots = [0x93000, 0xD0000]
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

    static func loaderTakes(_ running: String, _ p: Package, runningText: String = "") -> (Bool, String) {
        if p.kind == "mboot" { return (false, "m-bootloader is installed through download mode (a full backup first)") }
        switch running {
        case "loader": return (true, "the update loader finishes the cut-off install")
        case "felucca", "sloop":
            return p.kind == "fm1base" ? (false, "its updater is assumed to refuse fm1-base packages") : (true, "its own update loader")
        case "optimist", "ambig000":
            if p.kind == "v15" { return (true, "the way back to the official firmware") }
            guard let f = p.flashLen, ownSlots.contains(f) else { return (false, "Optimist's updater takes the layouts 0x93000 and 0xD0000 only") }
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

    static func choose(_ running: String, _ p: Package, bootloader: String = "keep", runningText: String = "") -> Route {
        let want = bootloader == "m-bootloader" && p.kind != "mboot"
        let (takes, why) = loaderTakes(running, p, runningText: runningText)
        let soft = softKeyOK.contains(running), k = keys[running] ?? ""
        if !want && takes { return Route(method: "loader", why: why, softKey: soft, keys: k) }
        return Route(method: "download", why: want ? "the build's BOOTLOADER is m-bootloader" : why, softKey: soft, keys: k)
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
