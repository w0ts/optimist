// SPDX-License-Identifier: GPL-3.0-only
// The web editor, bundled (the user, 2026-10-29: "bundle the editor"): web/editor.html and what it loads sit in the app
// (OptimistEditor/, copied in by tools/sync_ios_editor.py as a build phase) and are served by the URL scheme optimistedit://app/.
// WKWebView has no Web MIDI and loads no ES module from file://, so a scheme handler it is (as for Fm1BaseSite). The Web tab works
// with no network; only the Install / Build / Simulator parts the editor imports on demand (not bundled) need the site.
import Foundation
import WebKit

final class EditorSchemeHandler: NSObject, WKURLSchemeHandler, @unchecked Sendable {
    static let scheme = "optimistedit"
    static let entry = URL(string: "\(scheme)://app/index.html")!
    let root: URL
    private let lock = NSLock()
    private var served: [String] = []

    init(root: URL = EditorSchemeHandler.bundled) { self.root = root }

    /// the folder tools/sync_ios_editor.py filled in the app's bundle
    static var bundled: URL {
        let b = Bundle(for: MirrorLink.self)
        return b.url(forResource: "OptimistEditor", withExtension: nil) ?? b.bundleURL
    }

    /// every path asked of the handler, in order (the tests read it: nothing else leaves the page)
    var requests: [String] { lock.lock(); defer { lock.unlock() }; return served }

    private static let types = ["html": "text/html; charset=utf-8", "js": "text/javascript; charset=utf-8", "css": "text/css; charset=utf-8",
                                "json": "application/json", "ttf": "font/ttf", "svg": "image/svg+xml", "txt": "text/plain; charset=utf-8"]

    /// The file at `path` ("/" is the editor) as (bytes, MIME type); nil when there is none. Plain names only: no "..", nothing
    /// outside the folder.
    func response(_ path: String) -> (Data, String)? {
        let rel = path == "/" || path.isEmpty ? "index.html" : String(path.drop { $0 == "/" })
        guard rel.range(of: #"^[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*$"#, options: .regularExpression) != nil, !rel.contains(".."),
              let data = try? Data(contentsOf: root.appendingPathComponent(rel)) else { return nil }
        return (data, Self.types[(rel as NSString).pathExtension] ?? "application/octet-stream")
    }

    func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
        guard let url = task.request.url else { return }
        lock.lock(); served.append(url.path); lock.unlock()
        let found = response(url.path)
        let body = found?.0 ?? Data("not found".utf8)
        let headers = ["Content-Type": found?.1 ?? "text/plain", "Content-Length": String(body.count), "Cache-Control": "no-store"]
        guard let r = HTTPURLResponse(url: url, statusCode: found == nil ? 404 : 200, httpVersion: "HTTP/1.1", headerFields: headers) else { return }
        task.didReceive(r)
        task.didReceive(body)
        task.didFinish()
    }

    func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) {}
}
