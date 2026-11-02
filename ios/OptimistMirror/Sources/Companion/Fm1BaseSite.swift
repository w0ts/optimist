// SPDX-License-Identifier: GPL-3.0-only
// fm1-base's own web pages (Resources/Fm1Base, MPL-2.0, unchanged) filled in for the connected app, the way fm1-base's
// tools/make_site.py fills them in for a site: {{NAME}} and the like from what the FM-1 says (CompanionInfo), the installer's
// library and metadata inlined. Pure text functions: the tests run them without a web view.
import Foundation

enum Fm1BasePage: String, CaseIterable, Identifiable {
    case mirror, backup, installer, companion
    var id: String { rawValue }
    var title: String { rawValue.capitalized }
    var path: String { "/\(rawValue)/" }
}

struct Fm1BaseSite {
    let info: CompanionInfo
    /// the package the user chose for the installer (.fwsc bytes), if any
    var package: [UInt8]?
    var root: URL = Bundle(for: MirrorLink.self).url(forResource: "Fm1Base", withExtension: nil) ?? Bundle(for: MirrorLink.self).bundleURL

    /// A value put into a page: printable ASCII without what could end a string, a tag or an entity (the FM-1 is not trusted with the page)
    static func safe(_ s: String) -> String {
        String(s.unicodeScalars.filter { $0.value >= 0x20 && $0.value < 0x7F && !"<>&\"'\\`$".unicodeScalars.contains($0) })
    }

    /// The backup page needs the app's magic (4 letters), version and file extension: the firmware says them in its capability
    /// answer (protocol 2). An older firmware (protocol 1) does not: the app's own extras tags (backup_magic, backup_version,
    /// backup_ext) are read then, as a fallback only.
    var backupHints: (magic: String, version: Int, ext: String)? {
        guard info.hasBackup else { return nil }
        if let b = info.backup { return (Self.safe(b.magic), b.version, Self.safe(b.ext.isEmpty ? "bak" : b.ext)) }
        guard info.version < 2, let m = info.extras["backup_magic"], m.utf8.count == 4, m.allSatisfy(\.isASCII),
              let v = info.extras["backup_version"].flatMap(Int.init) else { return nil }
        return (Self.safe(m), v, Self.safe(info.extras["backup_ext"] ?? "bak"))
    }

    /// Pages this app can be offered
    var pages: [Fm1BasePage] {
        Fm1BasePage.allCases.filter { $0 != .backup || backupHints != nil }.filter { $0 != .mirror || info.hasMirror }
    }

    /// the package identity (fm1pkg.js productOf): one marker byte after each of the first 20 blocks
    static func product(_ fwsc: [UInt8]) -> String? {
        guard fwsc.count >= 20 * 0x30 else { return nil }
        var s = ""
        for i in 0..<20 {
            let m = fwsc[i * 0x30 + 0x2F]
            if m != 0x7D { s.append(Character(UnicodeScalar(UInt8(truncatingIfNeeded: Int(m) - i - 1)))) }
        }
        return s
    }

    /// an ES module as plain script: no export, no import (make_site.py strip_module)
    static func stripModule(_ src: String) -> String {
        var out: [Substring] = []
        for line in src.split(separator: "\n", omittingEmptySubsequences: false) {
            if line.hasPrefix("import "), line.hasSuffix(";") { continue }
            out.append(line.hasPrefix("export ") ? line.dropFirst(7).drop { $0 == " " || $0 == "\t" } : line)
        }
        return out.joined(separator: "\n")
    }

    private func read(_ path: String) -> String? {
        (try? Data(contentsOf: root.appendingPathComponent(path))).flatMap { String(data: $0, encoding: .utf8) }
    }

    private func fill(_ html: String, _ values: [String: String]) -> String {
        values.reduce(html) { $0.replacingOccurrences(of: "{{\($1.key)}}", with: $1.value) }
    }

    private var common: [String: String] {
        let repo = Self.safe(info.extras["repo"] ?? "")
        return ["NAME": Self.safe(info.name), "REPO": repo, "REPO_SHORT": repo.replacingOccurrences(of: "https://", with: "").replacingOccurrences(of: "http://", with: ""),
                "LICENSE": Self.safe(info.extras["licence"] ?? "MPL-2.0"), "CREDITS": "",
                "PORTS_JS": read("fm1ports.js").map(Self.stripModule) ?? ""]   // (finding FM-1s on Web MIDI, inlined as make_site.py does)
    }

    /// The page at `path` ("/installer/", "/companion/companion.js" ...) as (bytes, MIME type), nil when there is none
    func response(_ path: String) -> (Data, String)? {
        let js = "text/javascript; charset=utf-8", html = "text/html; charset=utf-8"
        switch path {
        case "/mirror/", "/mirror":
            return read("mirror/index.html").map { (Data(fill($0, common).utf8), html) }
        case "/companion/", "/companion":
            return read("companion/index.html").map { (Data($0.utf8), html) }
        case "/companion/companion.js":
            return read("companion/companion.js").map { (Data($0.utf8), js) }
        case "/backup/", "/backup":
            guard let h = backupHints, let page = read("backup/index.html") else { return nil }
            let v = common.merging(["ID": Self.safe(info.idPrefix), "MAGIC": h.magic, "BACKUP_VERSION": String(h.version), "EXT": h.ext,
                                    "HOLDS": "everything \(Self.safe(info.name)) saves", "EXTRA": "", "EXTRAS_JS": "const EXTRAS = [];", "TERMINAL": ""]) { $1 }
            return (Data(fill(page, v).utf8), html)
        case "/installer/", "/installer":
            return installer().map { (Data($0.utf8), html) }
        case "/package.fwsc":
            return package.map { (Data($0), "application/octet-stream") }
        case "/":
            let links = pages.map { "<li><a href=\"\($0.path)\">\($0.title)</a></li>" }.joined()
            return (Data("<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>\(Self.safe(info.name))</title><h1>\(Self.safe(info.name))</h1><ul>\(links)</ul>".utf8), html)
        default:
            return font(path)
        }
    }

    /// "/fonts/fonts.css" and the font files beside it (Resources/Fm1Base/fonts: Barlow, SIL OFL), which the pages link as ../fonts/:
    /// they come from the bundle, so no page asks an outside host for a font
    func font(_ path: String) -> (Data, String)? {
        let name = String(path.dropFirst("/fonts/".count))
        guard path.hasPrefix("/fonts/"), name.range(of: #"^[A-Za-z0-9._-]+\.(css|woff2|txt)$"#, options: .regularExpression) != nil,
              let data = try? Data(contentsOf: root.appendingPathComponent("fonts/\(name)")) else { return nil }
        let type = name.hasSuffix(".css") ? "text/css; charset=utf-8" : name.hasSuffix(".woff2") ? "font/woff2" : "text/plain; charset=utf-8"
        return (data, type)
    }

    private func installer() -> String? {
        guard let page = read("installer.html"), let pkg = read("fm1pkg.js"), let ota = read("fm1ota.js"), let ports = read("fm1ports.js") else { return nil }
        let product = package.flatMap(Self.product) ?? info.identity
        let meta = "{\"version\":\"\(Self.safe(info.appVersion))\",\"product\":\"\(Self.safe(product))\",\"pkg\":\"../package.fwsc\",\"backup\":false,\"boot\":null}"
        let lib = Self.stripModule(ports) + "\n" + Self.stripModule(pkg) + "\n" + Self.stripModule(ota)
        // (the library and metadata are put in last: nothing in them is a {{key}})
        return fill(page, common).replacingOccurrences(of: "/*LIB*/", with: lib).replacingOccurrences(of: "/*META*/", with: meta)
            .replacingOccurrences(of: "/*APP*/", with: "null")
    }
}
