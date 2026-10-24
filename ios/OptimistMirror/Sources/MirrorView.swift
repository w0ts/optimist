// SPDX-License-Identifier: GPL-3.0-only
// The app: the FM-1's panel, pixel for pixel, as large as the device allows (a line of status while there is nothing to show),
// and the web editor's Projects, Library and Backup, as tabs. The whole interface turns with the device even with Rotation
// Lock on (Tilt: gravity; Turn: the interface asked to that orientation); a tap on the screen hides the tab bar.
import CoreMotion
import SwiftUI
import UIKit

@main
struct OptimistMirrorApp: App {
    @UIApplicationDelegateAdaptor(Turn.self) private var turn
    @StateObject private var link = MirrorLink()
    @Environment(\.scenePhase) private var phase

    var body: some Scene {
        WindowGroup {
            RootView(link: link)
                .onChange(of: phase) { _, now in
                    UIApplication.shared.isIdleTimerDisabled = now == .active
                    if now == .active { link.refresh() }
                }
        }
    }
}

/// The quarter turns (0...3, counter-clockwise) that keep the picture upright, from the accelerometer's gravity: a new one
/// only well inside its quarter (35° of margin), none while the phone lies flat.
@MainActor
final class Tilt: ObservableObject {
    @Published private(set) var quarter = 0
    private let motion = CMMotionManager()

    func start() {
        guard motion.isDeviceMotionAvailable, !motion.isDeviceMotionActive else { return }
        motion.deviceMotionUpdateInterval = 0.1
        motion.startDeviceMotionUpdates(to: .main) { [weak self] m, _ in
            guard let self, let g = m?.gravity, abs(g.z) < 0.8 else { return }
            let deg = atan2(g.x, -g.y) * 180 / .pi            // 0 upright, 90 turned clockwise, ±180 upside down
            let q = (Int((deg / 90).rounded()) % 4 + 4) % 4
            let off = abs(deg - Double(q) * 90).truncatingRemainder(dividingBy: 360)
            if q != self.quarter, min(off, 360 - off) < 45 - 35 / 2 { self.quarter = q }
        }
    }

    func stop() { motion.stopDeviceMotionUpdates() }
}

/// The interface's orientation: the tilt's.
final class Turn: NSObject, UIApplicationDelegate {
    @MainActor static var allowed: UIInterfaceOrientationMask = .portrait

    func application(_ application: UIApplication, supportedInterfaceOrientationsFor window: UIWindow?) -> UIInterfaceOrientationMask {
        MainActor.assumeIsolated { Self.allowed }
    }

    /// The quarter the interface takes for a tilt (not upside down on an iPhone, where iOS refuses it: the picture turns instead).
    @MainActor static func quarter(_ tilt: Int) -> Int {
        tilt != 2 || UIDevice.current.userInterfaceIdiom == .pad ? tilt : 0
    }

    @MainActor static func to(_ quarter: Int) {
#if !targetEnvironment(macCatalyst)                   // (a Mac window: no orientation; the Mac has no gravity either)
        let mask: UIInterfaceOrientationMask = [.portrait, .landscapeLeft, .portraitUpsideDown, .landscapeRight][quarter]
        guard mask != allowed else { return }
        allowed = mask
        for case let scene as UIWindowScene in UIApplication.shared.connectedScenes {
            var vc = scene.keyWindow?.rootViewController
            while let c = vc {
                c.setNeedsUpdateOfSupportedInterfaceOrientations()
                vc = c.presentedViewController
            }
            scene.requestGeometryUpdate(.iOS(interfaceOrientations: mask))
        }
#endif
    }
}

struct RootView: View {
    @ObservedObject var link: MirrorLink
    @StateObject private var session: EditorSession
    @StateObject private var tilt = Tilt()
    @State private var bars = true

    init(link: MirrorLink) {
        self.link = link
        _session = StateObject(wrappedValue: EditorSession(link: link))
    }

    var body: some View {
        Group {
            if link.state == .searching {
                ConnectView(link: link)
            } else {
                tabs
            }
        }
        .task(id: "\(link.portName)|\(link.bluetooth)") { await session.connect() }
        .sheet(item: $session.export) { ActivityView(url: $0.url) }
        .onAppear {
            tilt.start()
            Turn.to(Turn.quarter(tilt.quarter))
#if DEBUG
            if let q = ProcessInfo.processInfo.environment["MIRROR_QUARTER"].flatMap(Int.init) { Turn.to(q % 4) }   // (a simulator has no gravity)
#endif
        }
        .onDisappear { tilt.stop() }
        .onChange(of: tilt.quarter) { _, q in Turn.to(Turn.quarter(q)) }
        .preferredColorScheme(.dark)
    }

    private var tabs: some View {
        TabView {
            MirrorView(link: link, upright: tilt.quarter - Turn.quarter(tilt.quarter), bars: $bars)
                .toolbar(bars ? .visible : .hidden, for: .tabBar)
                .tabItem { Label("Screen", systemImage: "display") }
            ProjectsView(s: session)
                .tabItem { Label("Projects", systemImage: "folder") }
            LibraryView(s: session)
                .tabItem { Label("Presets", systemImage: "slider.horizontal.3") }
            BackupView(s: session)
                .tabItem { Label("Backup", systemImage: "externaldrive") }
            ConnectionView(link: link)
                .tabItem { Label("Connection", systemImage: "cable.connector") }
        }
    }
}

/// The port in use, another one, Bluetooth, Disconnect (back to the welcome screen).
struct ConnectionView: View {
    @ObservedObject var link: MirrorLink

    var body: some View {
        NavigationStack {
            Form {
                Section("Connected To") {
                    LabeledContent("Link") {
                        Label(link.bluetooth ? "Bluetooth" : "USB Cable",
                              systemImage: link.bluetooth ? "dot.radiowaves.left.and.right" : "cable.connector")
                    }
                    LabeledContent("Port", value: link.portName)
                }
                Section {
                    Picker("MIDI Port", selection: $link.chosen) {
                        Text("Automatic").tag("")
                        ForEach(link.ports, id: \.self) { Text($0).tag($0) }
                    }
                } footer: {
                    Text("Automatic uses the USB cable when it is plugged in, Bluetooth otherwise.")
                }
                Section {
                    Button("Connect with Bluetooth…") { BluetoothPicker.present() }
                    Button("Disconnect", role: .destructive) { link.disconnect() }
                }
            }
            .navigationTitle("Connection")
        }
    }
}

/// No FM-1 port: the app's icon, then the ways to connect (no tabs: there is nothing to show yet).
struct ConnectView: View {
    @ObservedObject var link: MirrorLink

    private var bluetooth: some View {
        Button { link.resume(); BluetoothPicker.present() } label: {
            Label("Connect with Bluetooth", systemImage: "dot.radiowaves.left.and.right")
                .frame(maxWidth: .infinity)
        }
    }

    var body: some View {
        VStack(spacing: 20) {
            Image("Logo")
                .resizable()
                .frame(width: 160, height: 160)
                .clipShape(RoundedRectangle(cornerRadius: 36, style: .continuous))
                .accessibilityHidden(true)
            Text("Connect Your FM-1")
                .font(.title2.bold())
            Text("Plug it in with a USB cable, or turn on Bluetooth in the FM-1's SYSTEM page.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            if link.paused {
                Button { link.resume() } label: {
                    Label("Connect", systemImage: "cable.connector").frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
            }
            if link.paused {
                bluetooth.buttonStyle(.bordered).controlSize(.large)
            } else {
                bluetooth.buttonStyle(.borderedProminent).controlSize(.large)
            }
            if !link.ports.isEmpty || !link.chosen.isEmpty {
                Menu {
                    Picker("MIDI Port", selection: $link.chosen) {
                        Text("Automatic").tag("")
                        ForEach(link.ports, id: \.self) { Text($0).tag($0) }
                    }
                } label: {
                    Label(link.chosen.isEmpty ? "Choose MIDI Port" : link.chosen, systemImage: "cable.connector")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .controlSize(.large)
            }
        }
        .padding(32)
        .frame(maxWidth: 420)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.black.ignoresSafeArea())
        .statusBarHidden()              // (shown, iOS's Bluetooth MIDI sheet crashes: MirrorView)
    }
}

struct MirrorView: View {
    @ObservedObject var link: MirrorLink
    let upright: Int                    // quarter turns the picture still needs (an iPhone upside down: 2)
    @Binding var bars: Bool
    @StateObject private var nav: NavModel

    init(link: MirrorLink, upright: Int, bars: Binding<Bool>) {
        self.link = link
        self.upright = upright
        _bars = bars
        _nav = StateObject(wrappedValue: NavModel(link: link))
    }
    @State private var image: CGImage?
    @FocusState private var keyboard: Bool
    @State private var held: [String: [UInt8]] = [:]   // the keys down, what each pressed
    @State private var seen = 0
    @State private var drag: (knob: Int, across: Bool, sent: Int, per: CGFloat)?   // the knob a drag turns, its axis, the detents sent, points a detent
    /// a detent every so many points: the 2x2 or 1x4 cards 6; the lines, 240 wide, 15 (the user, 2026-10-24: "the scrub on the
    /// 4 lines is too sensitive")
    private static func pointsPerDetent(_ r: CGRect) -> CGFloat { r.width >= 200 ? 15 : 6 }

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            GeometryReader { g in laid(g.size) }
                .rotationEffect(.degrees(-90 * Double(upright)))
        }
        .contentShape(Rectangle())
        .onTapGesture { withAnimation { bars.toggle() } }
        .focusable()
        .focusEffectDisabled()
        .focused($keyboard)
        .onAppear { keyboard = true }
        .onKeyPress(phases: [.down, .repeat, .up]) { k in key(k) }
        .statusBarHidden()              // (shown, iOS's Bluetooth MIDI sheet laid its table out NaN high: a crash, 2026-10-23)
        .persistentSystemOverlays(bars ? .automatic : .hidden)
        .task { await draw() }
        .task { await nav.run() }
    }

    /// With the touch navigation (109 NAV, Nav/): wide, the pages left, the favourites and modes right, the tracks under
    /// the screen; tall, the tracks then the pages and the modes under it; without it the screen alone
    @ViewBuilder private func laid(_ size: CGSize) -> some View {
        let strip = nav.on ? NavTracks.height + NavMetrics.gap : 0
        if nav.on && size.width - (size.height - strip) >= 2 * 150 {
            let side = min(size.height - strip, size.width - 2 * 150 - 4 * NavMetrics.gap)
            let col = (size.width - side) / 2 - 2 * NavMetrics.gap
            HStack(alignment: .top, spacing: NavMetrics.gap) {
                NavPages(nav: nav).frame(width: col)
                VStack(spacing: NavMetrics.gap) {
                    content.frame(width: side, height: side)
                    NavTracks(nav: nav)
                }
                .frame(width: side)
                NavSide(nav: nav).frame(width: col)
            }
            .padding(.horizontal, NavMetrics.gap)
            .frame(width: size.width, height: size.height)
        } else if nav.on && size.height - size.width >= 3 * NavMetrics.button {
            VStack(spacing: NavMetrics.gap) {
                content.frame(width: size.width, height: size.width)
                NavTracks(nav: nav).padding(.horizontal, NavMetrics.gap)
                HStack(alignment: .top, spacing: NavMetrics.gap) {
                    NavPages(nav: nav)
                    NavSide(nav: nav)
                }
                .padding(.horizontal, NavMetrics.gap)
            }
        } else {
            content.frame(width: size.width, height: size.height)
                .overlay(alignment: .bottom) {
                    if let why = link.navMissing, size.width > 500 {
                        Text(why)
                            .font(.footnote)
                            .foregroundStyle(.white.opacity(0.7))
                            .padding(8)
                            .background(.black.opacity(0.7), in: RoundedRectangle(cornerRadius: 8))
                            .padding(.bottom, 8)
                    }
                }
        }
    }

    /// A drag that starts on KNOB k's card or line (NavState.knobs, the panel's 240 x 240) turns knob k: up or right is
    /// clockwise, a detent every pointsPerDetent points (the lines slower), along the axis the drag took first
    private func knobDrag(_ size: CGSize) -> some Gesture {
        DragGesture(minimumDistance: 6)
            .onChanged { g in
                if drag == nil {
                    let scale = 240 / max(size.width, 1)
                    let p = CGPoint(x: g.startLocation.x * scale, y: g.startLocation.y * scale)
                    guard let knobs = nav.state?.knobs, let k = knobs.firstIndex(where: { $0.contains(p) }) else { return }
                    drag = (k, abs(g.translation.width) > abs(g.translation.height), 0, Self.pointsPerDetent(knobs[k]))
                    UIImpactFeedbackGenerator(style: .light).impactOccurred()
                }
                guard let d = drag else { return }
                let steps = Int((d.across ? g.translation.width : -g.translation.height) / d.per)
                if steps != d.sent {
                    link.turn(UInt8(3 + d.knob), steps - d.sent)
                    drag?.sent = steps
                }
            }
            .onEnded { _ in drag = nil }
    }

    /// A key of the computer's keyboard: a button or note key held while it is, a knob turned a detent a press (PanelKeys)
    private func key(_ k: KeyPress) -> KeyPress.Result {
        guard link.canPress, !k.modifiers.contains(.command) else { return .ignored }
        let name = PanelKeys.name(k)
        if let t = PanelKeys.knobs[name] {
            if k.phase != .up { link.press([2, t.role, UInt8(64 + t.step)]) }
            return .handled
        }
        switch k.phase {
        case .down:
            guard held[name] == nil, let a = PanelKeys.held(name, shift: k.modifiers.contains(.shift)) else { return held[name] == nil ? .ignored : .handled }
            held[name] = a
            link.press(a + [1])
        case .up:
            guard let a = held.removeValue(forKey: name) else { return .ignored }
            link.press(a + [0])
        default:
            return held[name] == nil ? .ignored : .handled
        }
        return .handled
    }

    @ViewBuilder private var content: some View {
        ZStack {
            if let image {
                Image(decorative: image, scale: 1)
                    .resizable()
                    .interpolation(.none)
                    .aspectRatio(1, contentMode: .fit)
                    .overlay {
                        if nav.on {
                            GeometryReader { g in
                                Color.clear.contentShape(Rectangle()).gesture(knobDrag(g.size))
                            }
                        }
                    }
                    .padding(8)
            }
            if link.state != .mirroring {
                VStack(spacing: 8) {
                    Text(title).font(.headline)
                    Text(detail).font(.footnote).multilineTextAlignment(.center)
                    if link.state == .searching || link.bluetooth {
                        Button("Bluetooth…") { BluetoothPicker.present() }
                            .buttonStyle(.bordered)
                            .tint(.white)
                    }
                }
                .foregroundStyle(.white)
                .padding()
                .background(.black.opacity(0.7), in: RoundedRectangle(cornerRadius: 12))
                .padding()
            }
        }
    }

    private var title: String {
        switch link.state {
        case .searching: "Connect the FM-1"
        case .asking: "Asking \(link.portName)…"
        case .noMirror: "No mirror in this firmware"
        case .mirroring: ""
        }
    }

    private var detail: String {
        switch link.state {
        case .searching: "A USB cable from this \(UIDevice.current.model) to the FM-1 (USB-C, or Lightning with the camera adapter), or Bluetooth: SYSTEM > Bluetooth on the FM-1, then connect it here."
        case .asking: link.bluetooth ? "Waiting for the FM-1 to answer over Bluetooth." : "Waiting for the FM-1 to answer."
        case .noMirror: "This firmware does not offer the screen mirror."
        case .mirroring: ""
        }
    }

    /// At the display's pace: a new picture only when the panel changed.
    @MainActor
    private func draw() async {
        while !Task.isCancelled {
            if let s = link.snapshot(after: seen) {
                seen = s.generation
                image = Self.picture(s.pixels)
            }
            try? await Task.sleep(for: .milliseconds(16))
        }
    }

    private static func picture(_ px: [UInt16]) -> CGImage? {
        let side = MirrorDecoder.side
        var rgba = [UInt32](repeating: 0, count: px.count)
        for (k, c) in px.enumerated() {
            let r = UInt32(c >> 11) & 31, g = UInt32(c >> 5) & 63, b = UInt32(c) & 31
            rgba[k] = (r << 3 | r >> 2) | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2) << 16 | 0xFF00_0000
        }
        let data = rgba.withUnsafeBytes { Data($0) } as CFData
        guard let provider = CGDataProvider(data: data) else { return nil }
        return CGImage(width: side, height: side, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: side * 4,
                       space: CGColorSpaceCreateDeviceRGB(),
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),   // (bytes R G B x: r is the low byte)
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }
}
