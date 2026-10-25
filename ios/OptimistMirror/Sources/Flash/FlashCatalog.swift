// SPDX-License-Identifier: GPL-3.0-only
// Where the packages come from: the site's list (GitHub Pages, webapp/installer/site.json, web/make_site.py: each release's
// packages; its development build of main is not offered) and one's own cloud build, by its key (the cloud-builds branch's index.json,
// read at the branch's commit as web/installer/installer.js loadCloud does). Read as untrusted: a download must match the
// list's SHA-256.
import Foundation

struct FlashEntry: Identifiable, Equatable, Hashable {
    let id: String
    let group: String             // "Optimist v0.1 · 2026-10-18", "Cloud build"
    let title: String             // the flavour
    let flavour: String           // its web/flavours.json id ("default"); "" for a cloud build
    let url: URL
    let product: String?          // the site names it; a cloud build does not
    let sha256: String
}

enum FlashCatalog {
    static let repo = "w0ts/optimist"
    static let site = URL(string: "https://w0ts.github.io/optimist/webapp/installer/site.json")!
    /// the web builder's page for this app (web/builder/app.html: no site header or menu; the app adds its look), started on
    /// a flavour; its Build in the cloud goes through the relay, which opens the issue
    static let builder = URL(string: "https://w0ts.github.io/optimist/webapp/builder/app.html")!
    static func builder(flavour: String) -> URL {
        var c = URLComponents(url: builder, resolvingAgainstBaseURL: false)!
        if !flavour.isEmpty { c.queryItems = [URLQueryItem(name: "flavour", value: flavour)] }
        return c.url!
    }

    private static let hex64 = #"^[0-9a-f]{64}$"#
    private static let hex40 = #"^[0-9a-f]{40}$"#

    private static func matches(_ s: String, _ re: String) -> Bool { s.range(of: re, options: .regularExpression) != nil }

    // MARK: - the site's packages

    private struct Site: Decodable {
        struct Package: Decodable {
            let id: String?, title: String?, description: String?, pkg: String?, product: String?, sha256: String?
            let release: String?, date: String?
        }
        let version: String?
        let packages: [Package]?
    }

    static func siteEntries(_ data: Data, base: URL = site) -> [FlashEntry] {
        guard let s = try? JSONDecoder().decode(Site.self, from: data) else { return [] }
        return (s.packages ?? []).compactMap { p in
            guard let rel = p.release, !rel.isEmpty,     // (releases only: the development build of main is not offered)
                  let pkg = p.pkg, pkg.hasSuffix(".fwsc"), let url = URL(string: pkg, relativeTo: base)?.absoluteURL,
                  url.scheme == "https", let product = p.product, matches(product, #"^FM-1_[79]\d\d$"#),
                  let sha = p.sha256, matches(sha, hex64) else { return nil }
            let date = String((p.date ?? "").prefix(10))
            let group = date.isEmpty ? "Optimist \(rel)" : "Optimist \(rel) · \(date)"
            return FlashEntry(id: "site:\(p.release ?? ""):\(p.id ?? url.lastPathComponent)", group: group,
                              title: String((p.title ?? url.lastPathComponent).prefix(80)),
                              flavour: (p.id ?? "").range(of: #"^[A-Za-z0-9_-]{1,64}$"#, options: .regularExpression) != nil ? p.id! : "",
                              url: url, product: product, sha256: sha)
        }
    }

    /// The groups in the list's order (the releases newest first, make_site.py), each with its flavours.
    static func grouped(_ list: [FlashEntry]) -> [(group: String, entries: [FlashEntry])] {
        var out: [(group: String, entries: [FlashEntry])] = []
        for e in list {
            if let i = out.firstIndex(where: { $0.group == e.group }) { out[i].entries.append(e) } else { out.append((e.group, [e])) }
        }
        return out
    }

    // MARK: - one's own cloud build

    private struct Index: Decodable {
        struct Build: Decodable {
            let id: String?, stem: String?, name: String?, sha256: String?, fwsc: String?, config: String?
        }
        let builds: [Build]?
    }

    static func rawURL(_ sha: String, _ name: String) -> URL? {
        URL(string: "https://raw.githubusercontent.com/\(repo)/\(sha)/\(name.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? name)")
    }

    /// A key as typed (spaces, backticks and a "key" word dropped) -> its 16 digits' start, nil when it is not one (8+ hex digits).
    static func key(_ typed: String) -> String? {
        let k = typed.lowercased().replacingOccurrences(of: #"^\s*(build|key|id)\s*"#, with: "", options: .regularExpression)
            .replacingOccurrences(of: #"[\s`]+"#, with: "", options: .regularExpression)
        return matches(k, #"^[0-9a-f]{8,64}$"#) ? String(k.prefix(16)) : nil
    }

    /// The build of `key` in index.json at commit `sha`, when it is well formed (installer.js cloudBuilds) and the key names one.
    static func cloudEntry(_ data: Data, sha: String, key: String) -> FlashEntry? {
        guard matches(sha, hex40), let ix = try? JSONDecoder().decode(Index.self, from: data) else { return nil }
        let found = (ix.builds ?? []).compactMap { b -> FlashEntry? in
            guard let id = b.id, matches(id, #"^[0-9a-f]{16}$"#), id.hasPrefix(key), let stem = b.stem,
                  matches(stem, #"^optimist-[a-z0-9-]{1,32}-[0-9a-f]{16}$"#), stem.hasSuffix(id), b.fwsc == "\(stem).fwsc",
                  b.config == "\(stem).config", let digest = b.sha256, matches(digest, hex64), let url = rawURL(sha, "\(stem).fwsc") else { return nil }
            return FlashEntry(id: "cloud:\(id)", group: "Cloud build", title: "\(String((b.name ?? "custom").prefix(32))) (key \(id))",
                              flavour: "", url: url, product: nil, sha256: digest)
        }
        return found.count == 1 ? found[0] : nil      // (a short key that names two: none)
    }

    // MARK: - network

    struct NetError: Error, CustomStringConvertible { let description: String }

    private static func get(_ url: URL, json: Bool = false) async throws -> (Data, Int) {
        var r = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 20)
        if json { r.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept") }
        let (data, resp) = try await URLSession.shared.data(for: r)
        return (data, (resp as? HTTPURLResponse)?.statusCode ?? 0)
    }

    static func loadSite() async throws -> [FlashEntry] {
        let (data, code) = try await get(site)
        guard code == 200 else { throw NetError(description: "The site's list: HTTP \(code).") }
        return siteEntries(data)
    }

    static func loadCloud(key: String) async throws -> FlashEntry {
        let (b, code) = try await get(URL(string: "https://api.github.com/repos/\(repo)/branches/cloud-builds")!, json: true)
        guard code == 200 || code == 404 else {
            throw NetError(description: code == 403 ? "GitHub's limit of requests from this network: try again in an hour." : "The cloud builds: HTTP \(code).")
        }
        struct Branch: Decodable { struct Commit: Decodable { let sha: String? }; let commit: Commit? }
        if code == 200, let sha = (try? JSONDecoder().decode(Branch.self, from: b))?.commit?.sha, let ixURL = rawURL(sha, "index.json") {
            let (ix, c2) = try await get(ixURL)
            guard c2 == 200 else { throw NetError(description: "The cloud builds' index: HTTP \(c2).") }
            if let e = cloudEntry(ix, sha: sha, key: key) { return e }
        }
        throw NetError(description: "No cloud build with key \(key): not finished yet, expired, or a key that names two (type more of it).")
    }

    static func download(_ e: FlashEntry) async throws -> FlashPackage {
        let (data, code) = try await get(e.url)
        guard code == 200 else { throw NetError(description: "The download: HTTP \(code).") }
        return try FlashPackage.check([UInt8](data), title: e.flavour.isEmpty ? e.title : "\(e.group) · \(e.title)",
                                      file: e.url.lastPathComponent, expect: (product: e.product, sha256: e.sha256))
    }
}
