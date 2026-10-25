// SPDX-License-Identifier: GPL-3.0-only
// The web editor's own tabs in the app (the user, 2026-10-25: "web + MIDI bridge"): one web view, kept for the app's life
// (its connection and its pages' state stay), holding the site's editor (webapp/editor/) without its menu bar: the app's tab
// bar picks the page (#mixer, #samples ...). Web MIDI comes from WebMIDIBridge. While a web page is shown the mirror holds
// its own requests (the page talks to the FM-1 over the same port).
import SwiftUI
import WebKit

@MainActor
final class WebEditor: ObservableObject {
    static let site = URL(string: "https://w0ts.github.io/optimist/webapp/editor/")!
    let view: WKWebView
    private let bridge = WebMIDIBridge()
    private var loaded = false

    /// the app's look over the editor: no menu bar of its own (the app's tab bar is it), the system's theme
    private static let css = """
    #side, .menubar { display: none !important; }
    /* the device is the app's connection: the connect card keeps its loading progress (#cstatus) and loses what picks or
       connects a device (its "connect" button misled; the user, 2026-10-25) */
    #connectcard p:has(#connect), #connectcard p:has(#devsrc), #connectcard [data-t=connectHint],
    #connectcard p:has([data-t=installerLink]) { display: none !important; }
    :root { --menubar-h: 0px; }
    body { padding-top: 0 !important; }
    """

    init() {
        let config = WKWebViewConfiguration()
        config.websiteDataStore = .default()            // (the editor's saved settings and libraries: kept across launches)
        config.defaultWebpagePreferences.allowsContentJavaScript = true
        config.allowsInlineMediaPlayback = true
        config.mediaTypesRequiringUserActionForPlayback = []   // (the Simulator's sound)
        config.userContentController.add(bridge, name: WebMIDIBridge.handler)
        view = WKWebView(frame: .zero, configuration: config)
        bridge.webView = view
        scripts()
        view.isInspectable = true                       // (Safari's Develop menu: the page's console)
    }

    /// the page's scripts: Web MIDI, the app's look, and which link the editor takes (USB or Bluetooth: the app's)
    private func scripts() {
        let uc = view.configuration.userContentController
        uc.removeAllUserScripts()
        uc.addUserScript(WKUserScript(source: WebMIDIBridge.shim, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        uc.addUserScript(WKUserScript(source: "try { localStorage.setItem('optimist-editor-device', '\(bridge.allowedBluetooth ? "bt" : "usb")'); } catch (e) {}",
                                      injectionTime: .atDocumentStart, forMainFrameOnly: true))
        let css = Self.css.replacingOccurrences(of: "`", with: "\\`")
        uc.addUserScript(WKUserScript(source: "const s = document.createElement('style'); s.textContent = `\(css)`; document.head.append(s);",
                                      injectionTime: .atDocumentEnd, forMainFrameOnly: true))
    }

    /// The app's connection, shared with the page: the port it may see, and a link change (USB <-> Bluetooth) loads the
    /// page again on that link. port "": the app disconnected, the page has no port.
    func follow(port: String, bluetooth: Bool) {
        let relink = bluetooth != bridge.allowedBluetooth
        bridge.allowedBluetooth = bluetooth
        bridge.allowed = port
        guard relink else { return }
        scripts()
        if loaded { view.reload() }
    }

    /// the page `tab` (the editor's data-tab: mixer, samples, drumkit, snapshots, device, settings, sim)
    /// the build the Simulator runs (a cloud build's id: ?sim=); a new one loads the page again
    var simBuild: String?
    private var loadedBuild: String?

    func show(_ tab: String) {
        if !loaded || loadedBuild != simBuild {
            loaded = true
            loadedBuild = simBuild
            var c = URLComponents(url: Self.site, resolvingAgainstBaseURL: false)!
            if let b = simBuild { c.queryItems = [URLQueryItem(name: "sim", value: b)] }
            c.fragment = tab
            view.load(URLRequest(url: c.url!))
        } else {
            view.evaluateJavaScript("location.hash = '#\(tab)'")
        }
    }
}

/// One of the web tabs: the shared web view, moved into this tab when it is shown.
struct WebTabView: UIViewRepresentable {
    @ObservedObject var editor: WebEditor
    let tab: String
    let link: MirrorLink
    var shown = 0                                   // (bumped each time the tab appears: the shared view moved back in)

    func makeUIView(context: Context) -> UIView { UIView() }

    func updateUIView(_ host: UIView, context: Context) {
        let v = editor.view
        if v.superview !== host {
            v.removeFromSuperview()
            v.translatesAutoresizingMaskIntoConstraints = false
            host.addSubview(v)
            NSLayoutConstraint.activate([v.leadingAnchor.constraint(equalTo: host.leadingAnchor), v.trailingAnchor.constraint(equalTo: host.trailingAnchor),
                                         v.topAnchor.constraint(equalTo: host.safeAreaLayoutGuide.topAnchor), v.bottomAnchor.constraint(equalTo: host.bottomAnchor)])
        }
        editor.show(tab)
    }
}

/// A web tab as the app shows it: the page, the mirror held while it is on screen.
struct WebTab: View {
    @ObservedObject var editor: WebEditor
    let tab: String
    let link: MirrorLink
    @State private var shown = 0

    var body: some View {
        WebTabView(editor: editor, tab: tab, link: link, shown: shown)
            .ignoresSafeArea(edges: .bottom)
            .onAppear { shown += 1; link.hold() }
            .onDisappear { link.release() }
    }
}
