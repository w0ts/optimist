// SPDX-License-Identifier: GPL-3.0-only
// The FM-1's panel, pixel for pixel, as large as the phone allows; a line of status while there is nothing to show. It turns
// with the phone even with Rotation Lock on: the interface stays portrait (project.yml) and the picture is turned in quarter
// turns from gravity (Tilt).
import CoreMotion
import SwiftUI
import UIKit

@main
struct OptimistMirrorApp: App {
    @StateObject private var link = MirrorLink()
    @Environment(\.scenePhase) private var phase

    var body: some Scene {
        WindowGroup {
            MirrorView(link: link)
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

struct MirrorView: View {
    @ObservedObject var link: MirrorLink
    @StateObject private var tilt = Tilt()
    @State private var image: CGImage?
    @State private var seen = 0
    @State private var picking = false

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            content
                .rotationEffect(.degrees(-90 * Double(tilt.quarter)))
                .animation(.easeInOut(duration: 0.25), value: tilt.quarter)
        }
        .overlay(alignment: .bottomTrailing) {
            if link.state == .mirroring {
                Button { picking = true } label: {   // (the link in use; a tap: the Bluetooth sheet)
                    Image(systemName: link.bluetooth ? "dot.radiowaves.left.and.right" : "cable.connector")
                        .font(.footnote)
                        .foregroundStyle(.white.opacity(0.35))
                        .padding(12)
                }
            }
        }
        .sheet(isPresented: $picking) { BluetoothPicker() }
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
        .task { await draw() }
        .onAppear { tilt.start() }
        .onDisappear { tilt.stop() }
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
                        Button("Bluetooth…") { picking = true }
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
        case .searching: "A USB cable from the iPhone to the FM-1 (USB-C, or Lightning with the camera adapter), or Bluetooth: SYSTEM > Bluetooth on the FM-1, then connect it here."
        case .asking: link.bluetooth ? "Waiting for the FM-1 to answer over Bluetooth." : "Waiting for the FM-1 to answer."
        case .noMirror: "Build the firmware with the builder item ED_MIRROR (under the web editor) and install it."
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
