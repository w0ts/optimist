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
#if targetEnvironment(macCatalyst)
        return                          // (a Mac window: no orientation; the Mac has no gravity either)
#endif
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
                    Label(link.portName, systemImage: link.bluetooth ? "dot.radiowaves.left.and.right" : "cable.connector")
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
    @State private var image: CGImage?
    @State private var seen = 0

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            content
                .rotationEffect(.degrees(-90 * Double(upright)))
        }
        .contentShape(Rectangle())
        .onTapGesture { withAnimation { bars.toggle() } }
        .statusBarHidden()              // (shown, iOS's Bluetooth MIDI sheet laid its table out NaN high: a crash, 2026-10-23)
        .persistentSystemOverlays(bars ? .automatic : .hidden)
        .task { await draw() }
    }

    @ViewBuilder private var content: some View {
        ZStack {
            if let image {
                Image(decorative: image, scale: 1)
                    .resizable()
                    .interpolation(.none)
                    .aspectRatio(1, contentMode: .fit)
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
