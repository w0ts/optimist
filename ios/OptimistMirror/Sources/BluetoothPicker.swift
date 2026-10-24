// SPDX-License-Identifier: GPL-3.0-only
// iOS's Bluetooth MIDI sheet (CoreAudioKit): lists the MIDI peripherals advertising nearby (the FM-1 with SYSTEM > Bluetooth on)
// and connects one; its port then appears in CoreMIDI and MirrorLink takes it when no USB cable is plugged in. Presented by UIKit
// over whatever is on top; closed by itself once the FM-1 mirrors over Bluetooth (RootView; the user, 2026-10-24: "close the bt
// window when the app successfully connect"). Opening it connects nothing: after Disconnect the app takes a port again only
// when one appears while the sheet is up (a device picked there) or when Done closes it ("it connects automatically. i did
// not ask for that").
import CoreAudioKit
import UIKit

enum BluetoothPicker {
    @MainActor private static weak var shown: UINavigationController?
    @MainActor private static var onDone: (() -> Void)?
    /// the MIDI ports there when it opened: one more is a device connected in it
    @MainActor private(set) static var before: Set<String> = []
    @MainActor static var isShown: Bool { shown != nil }

    @MainActor static func dismiss() {
        shown?.dismiss(animated: true)
        shown = nil
        onDone = nil
    }

    /// ports: the MIDI ports now; done: run when the user closes it with Done
    @MainActor static func present(ports: [String] = [], done: (() -> Void)? = nil) {
        guard let scene = UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }).first,
              var top = scene.keyWindow?.rootViewController else { return }
        while let p = top.presentedViewController { top = p }
        let central = CABTMIDICentralViewController()
        let nav = UINavigationController(rootViewController: central)
        central.navigationItem.rightBarButtonItem = UIBarButtonItem(systemItem: .done, primaryAction: UIAction { [weak nav] _ in
            nav?.dismiss(animated: true)
            let d = onDone
            onDone = nil
            d?()
        })
        nav.isModalInPresentation = true                // (closed by Done only: its action runs)
        nav.modalPresentationStyle = .formSheet
        top.present(nav, animated: true)
        shown = nav
        onDone = done
        before = Set(ports)
    }
}
