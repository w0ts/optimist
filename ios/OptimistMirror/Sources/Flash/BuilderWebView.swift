// SPDX-License-Identifier: GPL-3.0-only
// The web builder inside the app (the user, 2026-10-25): its page for the app (web/builder/app.html: no site header or menu)
// in a WKWebView, started on a flavour, with the app's look put in as a style sheet (look). Its rules are the web's own
// (tools/builder in Pyodide), and its Build in the cloud goes through the relay as from a browser: the page is w0ts.github.io's,
// so the relay's Origin and Turnstile checks hold (Cloudflare: Turnstile in a native app = a WebView). The page posts its
// build's stages and its links to the handler "optimist" (BuilderEvent). Its storage (the last and the saved configurations)
// is the default website data store's: kept across launches, under the same origin.
import SwiftUI
import WebKit

struct BuilderWebView: UIViewRepresentable {
    let url: URL
    let event: (BuilderEvent) -> Void

    static let handler = "optimist"

    /// The app's look over the site's style (web/style/optimist.css's tokens): iOS's dark grouped colours, its font and
    /// sizes, rounded controls
    static let look = """
    :root { color-scheme: light dark;
      --bg: #f2f2f7; --fg: #000; --panel: #fff; --dim: #6c6c70; --line: #c6c6c8;
      --t1: #007aff; --ok: #248a3d; --warn: #b25000; --err: #d70015; --on: #fff;
      --font-ui: -apple-system, system-ui, sans-serif; --font-mono: ui-monospace, "SF Mono", monospace;
      --fs-1: 13px; --fs-2: 15px; --fs-3: 15px; --fs-4: 17px; --fs-5: 20px; --fs-6: 28px;
      --r: 10px; --ctl-h: 36px; }
    @media (prefers-color-scheme: dark) { :root {
      --bg: #000; --fg: #fff; --panel: #1c1c1e; --dim: #98989f; --line: #38383a;
      --t1: #0a84ff; --ok: #30d158; --warn: #ffd60a; --err: #ff453a;
      --on: #000; } }   /* (text on a status fill: black on the dark scheme's bright yellow, white was unreadable) */
    html, body { background: var(--bg); -webkit-text-size-adjust: 100%; }
    button, input, select, textarea { border-radius: var(--r); }
    """

    func makeCoordinator() -> Coordinator { Coordinator(event) }

    func makeUIView(context: Context) -> WKWebView {
        let config = WKWebViewConfiguration()
        config.websiteDataStore = .default()          // (Turnstile wants cookies and DOM storage that last the session)
        config.defaultWebpagePreferences.allowsContentJavaScript = true
        config.userContentController.add(context.coordinator, name: Self.handler)
        let css = Self.look.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "`", with: "\\`")
        config.userContentController.addUserScript(WKUserScript(source: """
            const s = document.createElement("style"); s.id = "optimist-app-look"; s.textContent = `\(css)`;
            document.head.append(s);
            """, injectionTime: .atDocumentEnd, forMainFrameOnly: true))
        let view = WKWebView(frame: .zero, configuration: config)
        view.navigationDelegate = context.coordinator
        view.load(URLRequest(url: url))
        return view
    }

    func updateUIView(_ view: WKWebView, context: Context) {}

    static func dismantleUIView(_ view: WKWebView, coordinator: Coordinator) {
        view.configuration.userContentController.removeScriptMessageHandler(forName: handler)
    }

    final class Coordinator: NSObject, WKScriptMessageHandler, WKNavigationDelegate {
        let event: (BuilderEvent) -> Void
        init(_ event: @escaping (BuilderEvent) -> Void) { self.event = event }

        /// The page's messages (builder.js, app.html), read as untrusted: a build id is 16 hex digits, a link one to this
        /// repository's issues
        func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {
            guard message.frameInfo.isMainFrame, message.frameInfo.securityOrigin.host == FlashCatalog.builder.host,
                  let m = message.body as? [String: Any], let e = BuilderEvent(m) else { return }
            event(e)
        }

        /// The builder's own pages stay here; any other link (the GitHub issue, the docs) opens in the browser
        func webView(_ view: WKWebView, decidePolicyFor action: WKNavigationAction, decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
            guard let url = action.request.url, action.targetFrame?.isMainFrame ?? false || action.targetFrame == nil else {
                decisionHandler(.allow)
                return
            }
            if url.host == FlashCatalog.builder.host || url.scheme == "about" {
                decisionHandler(action.targetFrame == nil ? .cancel : .allow)
                if action.targetFrame == nil { view.load(URLRequest(url: url)) }
            } else {
                decisionHandler(.cancel)
                UIApplication.shared.open(url)
            }
        }
    }
}

/// What the builder page tells the app (the relay session's contract, 2026-10-25): its cloud build's stages, and its links
/// "Flash it" / "Try it in the simulator".
enum BuilderEvent: Equatable {
    case sent(issue: URL?)
    case building(id: String, run: URL?)  // run: the Actions run (its log), once the workflow checked the configuration
    case done(id: String, name: String)
    case stopped(reason: String)          // refused, failed, error
    case open(where: String, id: String)  // "install" | "sim"

    init?(_ m: [String: Any]) {
        let id = (m["id"] as? String).flatMap { $0.range(of: #"^[0-9a-f]{16}$"#, options: .regularExpression) != nil ? $0 : nil }
        switch (m["type"] as? String, m["stage"] as? String) {
        case ("cloud-build", "sent"?):
            self = .sent(issue: (m["url"] as? String).flatMap(URL.init).flatMap { $0.absoluteString.hasPrefix("https://github.com/\(FlashCatalog.repo)/issues/") ? $0 : nil })
        case ("cloud-build", "building"?):
            guard let id else { return nil }
            self = .building(id: id, run: (m["run"] as? String).flatMap(URL.init).flatMap {
                $0.absoluteString.range(of: #"^https://github\.com/w0ts/optimist/actions/runs/[0-9]+$"#, options: .regularExpression) != nil ? $0 : nil })
        case ("cloud-build", "done"?):
            guard let id else { return nil }
            self = .done(id: id, name: String(((m["name"] as? String) ?? "custom").prefix(32)))
        case ("cloud-build", let st?) where ["refused", "failed", "error"].contains(st):
            self = .stopped(reason: String(((m["reason"] as? String) ?? st).prefix(200)))
        case ("open", _):
            guard let w = m["where"] as? String, ["install", "sim"].contains(w),
                  let b = (m["build"] as? String), b.range(of: #"^[0-9a-f]{16}$"#, options: .regularExpression) != nil else { return nil }
            self = .open(where: w, id: b)
        default:
            return nil
        }
    }
}

/// The builder as a page of its own: Done closes it. Its events go to the Firmware page (FlashModel.builder(_:)); "Flash it"
/// closes it with that build picked.
struct BuilderSheet: View {
    let url: URL
    let event: (BuilderEvent) -> Void
    let close: () -> Void

    var body: some View {
        NavigationStack {
            BuilderWebView(url: url, event: event)
                .ignoresSafeArea(edges: .bottom)
                .background(Color(.systemGroupedBackground))   // (seamless: the page's own background, app.html + look)
                .navigationTitle("Build")
                .navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done", action: close) } }
        }
    }
}
