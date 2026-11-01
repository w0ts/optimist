// SPDX-License-Identifier: GPL-3.0-only
// fm1-base's own pages (installer, backup, mirror, companion) in a web view of their own, with Web MIDI from the app's own
// bridge (WebMIDIBridge): the pages are served from the app's bundle by a URL scheme (fm1base://app/<page>/), filled in for the
// connected app (Fm1BaseSite). WKWebView has no Web MIDI and loads no ES module from file://, so a scheme handler it is.
import SwiftUI
import WebKit

/// Serves Fm1BaseSite's pages; the site changes when the connected app does.
final class Fm1BaseSchemeHandler: NSObject, WKURLSchemeHandler, @unchecked Sendable {
    static let scheme = "fm1base"
    private let lock = NSLock()
    private var current: Fm1BaseSite?

    var site: Fm1BaseSite? {
        get { lock.lock(); defer { lock.unlock() }; return current }
        set { lock.lock(); current = newValue; lock.unlock() }
    }

    func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
        guard let url = task.request.url else { return }
        let found = site?.response(url.path.isEmpty ? "/" : url.path)
        let status = found == nil ? 404 : 200
        let body = found?.0 ?? Data("not found".utf8)
        let headers = ["Content-Type": found?.1 ?? "text/plain", "Content-Length": String(body.count), "Cache-Control": "no-store"]
        guard let r = HTTPURLResponse(url: url, statusCode: status, httpVersion: "HTTP/1.1", headerFields: headers) else { return }
        task.didReceive(r)
        task.didReceive(body)
        task.didFinish()
    }

    func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) {}
}

@MainActor
final class Fm1BaseWeb: ObservableObject {
    let view: WKWebView
    private let bridge = WebMIDIBridge()
    private let handler = Fm1BaseSchemeHandler()
    @Published private(set) var page: Fm1BasePage?

    init() {
        let config = WKWebViewConfiguration()
        config.websiteDataStore = .default()
        config.defaultWebpagePreferences.allowsContentJavaScript = true
        config.setURLSchemeHandler(handler, forURLScheme: Fm1BaseSchemeHandler.scheme)
        config.userContentController.add(bridge, name: WebMIDIBridge.handler)
        config.userContentController.addUserScript(WKUserScript(source: WebMIDIBridge.shim, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        view = WKWebView(frame: .zero, configuration: config)
        bridge.webView = view
        view.isInspectable = true
    }

    /// The app's connection, shared with the pages: the one port they may see ("": none)
    func follow(port: String, bluetooth: Bool) {
        bridge.allowedBluetooth = bluetooth
        bridge.allowed = port
    }

    /// The connected app, and the package the user chose for the installer
    func use(_ info: CompanionInfo?, package: [UInt8]?) {
        let before = handler.site
        handler.site = info.map { Fm1BaseSite(info: $0, package: package) }
        if page == .installer, before?.package != package { show(.installer, force: true) }
    }

    func show(_ p: Fm1BasePage, force: Bool = false) {
        guard force || page != p else { return }
        page = p
        view.load(URLRequest(url: URL(string: "\(Fm1BaseSchemeHandler.scheme)://app\(p.path)")!))
    }

    /// load a page again after the app changed (another firmware connected)
    func reset() {
        page = nil
        view.load(URLRequest(url: URL(string: "about:blank")!))
    }
}

struct Fm1BaseWebView: UIViewRepresentable {
    @ObservedObject var web: Fm1BaseWeb

    func makeUIView(context: Context) -> UIView { UIView() }

    func updateUIView(_ host: UIView, context: Context) {
        let v = web.view
        guard v.superview !== host else { return }
        v.removeFromSuperview()
        v.translatesAutoresizingMaskIntoConstraints = false
        host.addSubview(v)
        NSLayoutConstraint.activate([v.leadingAnchor.constraint(equalTo: host.leadingAnchor), v.trailingAnchor.constraint(equalTo: host.trailingAnchor),
                                     v.topAnchor.constraint(equalTo: host.topAnchor), v.bottomAnchor.constraint(equalTo: host.bottomAnchor)])
    }
}
