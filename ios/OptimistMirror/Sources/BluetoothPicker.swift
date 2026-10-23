// SPDX-License-Identifier: GPL-3.0-only
// iOS's Bluetooth MIDI sheet (CoreAudioKit): lists the MIDI peripherals advertising nearby (the FM-1 with SYSTEM > Bluetooth on)
// and connects one; its port then appears in CoreMIDI and MirrorLink takes it when no USB cable is plugged in. Presented by UIKit
// over whatever is on top.
import CoreAudioKit
import UIKit

enum BluetoothPicker {
    @MainActor static func present() {
        guard let scene = UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }).first,
              var top = scene.keyWindow?.rootViewController else { return }
        while let p = top.presentedViewController { top = p }
        let central = CABTMIDICentralViewController()
        let nav = UINavigationController(rootViewController: central)
        central.navigationItem.rightBarButtonItem = UIBarButtonItem(systemItem: .done, primaryAction: UIAction { [weak nav] _ in
            nav?.dismiss(animated: true)
        })
        nav.modalPresentationStyle = .formSheet
        top.present(nav, animated: true)
    }
}
