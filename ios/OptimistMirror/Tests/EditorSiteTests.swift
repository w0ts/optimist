// SPDX-License-Identifier: GPL-3.0-only
// The bundled web editor (Sources/Web/EditorSite.swift): the app's OptimistEditor/ folder holds web/editor.html and what it
// loads (tools/sync_ios_editor.py as a build phase), and the editor starts from it with no network.
import XCTest
import WebKit
@testable import OptimistMirror

@MainActor
final class EditorSiteTests: XCTestCase {
    private final class Loaded: NSObject, WKNavigationDelegate {
        var done: CheckedContinuation<Void, Never>?
        var failure: Error?
        func webView(_ w: WKWebView, didFinish n: WKNavigation!) { done?.resume(); done = nil }
        func webView(_ w: WKWebView, didFailProvisionalNavigation n: WKNavigation!, withError e: Error) { failure = e; done?.resume(); done = nil }
        func webView(_ w: WKWebView, didFail n: WKNavigation!, withError e: Error) { failure = e; done?.resume(); done = nil }
    }

    private func eval(_ w: WKWebView, _ js: String) async throws -> Any? {
        try await w.callAsyncJavaScript(js, arguments: [:], in: nil, contentWorld: .page)
    }

    func testBundleHoldsTheEditorAndWhatItLoads() throws {
        let h = EditorSchemeHandler()
        let page = try XCTUnwrap(h.response("/"), "OptimistEditor/index.html is not in the bundle (the build phase did not run)")
        XCTAssertTrue(String(decoding: page.0, as: UTF8.self).contains("<title>"))
        XCTAssertEqual(page.1, "text/html; charset=utf-8")
        for path in ["/style/optimist.css", "/style/terminus.ttf", "/fukiai.ttf", "/FUKIAI-LICENSE.txt", "/TERMINUS-LICENSE.txt", "/icon.svg"] {
            XCTAssertNotNil(h.response(path), path)
        }
        // not bundled: the online-only modules, and nothing outside the folder
        XCTAssertNil(h.response("/builder/builder.js"))
        XCTAssertNil(h.response("/sim/sim.js"))
        XCTAssertNil(h.response("/../Info.plist"))
        XCTAssertNil(h.response("/%2e%2e/Info.plist"))
    }

    func testBundledCopyIsTheEditorOfThisCommit() throws {
        // (the build phase copies web/editor.html; the repository's own copy is read from the source tree when it is reachable)
        let src = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent("web/editor.html")
        guard let original = try? Data(contentsOf: src) else { throw XCTSkip("the source tree is not reachable from here") }
        XCTAssertEqual(try XCTUnwrap(EditorSchemeHandler().response("/")).0, original)
    }

    func testEditorStartsFromTheBundleWithNoOutsideRequest() async throws {
        let handler = EditorSchemeHandler()
        let config = WKWebViewConfiguration()
        config.setURLSchemeHandler(handler, forURLScheme: EditorSchemeHandler.scheme)
        let web = WKWebView(frame: CGRect(x: 0, y: 0, width: 800, height: 1200), configuration: config)
        let delegate = Loaded()
        web.navigationDelegate = delegate
        await withCheckedContinuation { (c: CheckedContinuation<Void, Never>) in
            delegate.done = c
            web.load(URLRequest(url: URL(string: EditorSchemeHandler.entry.absoluteString + "#mixer")!))
        }
        XCTAssertNil(delegate.failure)
        // (the page's script ran: it fills the translated labels and sets the page attribute)
        var ready = false
        for _ in 0..<50 where !ready {
            ready = try await eval(web, "return document.querySelectorAll('[data-t]').length > 0 && document.querySelector('#tabs') !== null") as? Bool ?? false
            if !ready { try await Task.sleep(nanoseconds: 100_000_000) }
        }
        XCTAssertTrue(ready, "the editor's page did not come up")
        // the shared style and the icon font come from the local scheme
        let css = try await eval(web, "const r = await fetch('style/optimist.css'); return r.status + ' ' + (await r.text()).includes('--bg');") as? String
        XCTAssertEqual(css, "200 true")
        let font = try await eval(web, "const r = await fetch('fukiai.ttf'); return r.status + ' ' + ((await r.arrayBuffer()).byteLength > 0);") as? String
        XCTAssertEqual(font, "200 true")
        // every resource the page asked for is the bundle's, and the handler served each one
        let names = try await eval(web, "return performance.getEntriesByType('resource').map(e => e.name);") as? [String] ?? []
        XCTAssertFalse(names.contains { !$0.hasPrefix("\(EditorSchemeHandler.scheme)://app/") }, "outside request: \(names)")
        XCTAssertEqual(handler.requests.first, "/index.html")
        XCTAssertTrue(handler.requests.contains("/style/optimist.css"))
        // the Install / Build / Simulator modules are not bundled: the page says so instead of reaching for the site
        let sim = try await eval(web, "location.hash = '#sim'; await new Promise(r => setTimeout(r, 1500)); return document.body.innerText.length > 0 && location.origin;") as? String
        XCTAssertEqual(sim, "optimistedit://app")
        XCTAssertFalse(handler.requests.contains { $0.hasPrefix("/sim/") && handler.response($0) != nil })
    }
}
