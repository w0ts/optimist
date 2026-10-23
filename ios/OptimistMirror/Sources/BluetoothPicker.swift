// SPDX-License-Identifier: GPL-3.0-only
// iOS's Bluetooth MIDI sheet (CoreAudioKit): lists the MIDI peripherals advertising nearby (the FM-1 with SYSTEM > Bluetooth on)
// and connects one; its port then appears in CoreMIDI and MirrorLink takes it when no USB cable is plugged in.
import CoreAudioKit
import SwiftUI

struct BluetoothPicker: UIViewControllerRepresentable {
    @Environment(\.dismiss) private var dismiss

    func makeUIViewController(context: Context) -> UINavigationController {
        let central = CABTMIDICentralViewController()
        central.navigationItem.rightBarButtonItem = UIBarButtonItem(systemItem: .done, primaryAction: UIAction { _ in
            context.coordinator.done()
        })
        return UINavigationController(rootViewController: central)
    }

    func updateUIViewController(_ controller: UINavigationController, context: Context) {}

    func makeCoordinator() -> Coordinator { Coordinator(dismiss: dismiss) }

    final class Coordinator {
        private let dismiss: DismissAction
        init(dismiss: DismissAction) { self.dismiss = dismiss }
        func done() { dismiss() }
    }
}
