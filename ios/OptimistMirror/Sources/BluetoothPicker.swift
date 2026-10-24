// SPDX-License-Identifier: GPL-3.0-only
// iOS's Bluetooth MIDI sheet (CoreAudioKit): lists the MIDI peripherals advertising nearby (the FM-1 with SYSTEM > Bluetooth on)
// and connects one; its port then appears in CoreMIDI and MirrorLink takes it when no USB cable is plugged in. Presented by UIKit
// over whatever is on top; closed by itself once the FM-1 mirrors over Bluetooth (RootView; the user, 2026-10-24: "close the bt
// window when the app successfully connect"). Opening or closing it connects nothing: the app takes a port only when one
// appears while the sheet is up, a device picked there ("it connects automatically. i did not ask for that"; "NO AUTOCONNECT").
import CoreAudioKit
import UIKit

enum BluetoothPicker {
    @MainActor private static weak var shown: UINavigationController?
    /// the MIDI ports online when it opened: one more online is a device connected in it
    @MainActor private(set) static var before: Set<String> = []
    @MainActor static var isShown: Bool { shown != nil }

    /// Done, Esc, or connected (RootView)
    @MainActor static func dismiss() {
        shown?.dismiss(animated: true)
        shown = nil
    }

    /// ports: the MIDI ports online now (MirrorLink.online)
    @MainActor static func present(ports: [String] = []) {
        guard let scene = UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }).first,
              var top = scene.keyWindow?.rootViewController else { return }
        while let p = top.presentedViewController { top = p }
        let central = Central()
        let nav = Sheet(rootViewController: central)
        // (on the left: the sheet puts its Edit button on the right, and on the Mac that hid a Done there)
        central.navigationItem.leftBarButtonItem = UIBarButtonItem(systemItem: .done, primaryAction: UIAction { _ in dismiss() })
        nav.isModalInPresentation = true                // (closed by Done or Esc only)
        nav.modalPresentationStyle = .formSheet
        top.present(nav, animated: true)
        shown = nav
        before = Set(ports)
    }

    /// Apple's list, its rows' spinners ("Connecting...") medium: on the Mac Apple's large one is drawn 32 x 32 in an 11-point frame
    /// and stuck out of the row's top (measured: frame 540,8 32x11, native view 540,-3 32x32; the user, 2026-10-24: "the spinner
    /// in the bluetooth connection is not vertically centered"). Apple sets its rows when they change: checked 4 times a second.
    private final class Central: CABTMIDICentralViewController {
        private var timer: Timer?
        override func viewDidAppear(_ animated: Bool) {
            super.viewDidAppear(animated)
            timer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
                MainActor.assumeIsolated { self?.tableView.visibleCells.forEach(Self.medium) }
            }
        }
        override func viewWillDisappear(_ animated: Bool) {
            super.viewWillDisappear(animated)
            timer?.invalidate()
        }
        override func viewDidLayoutSubviews() {
            super.viewDidLayoutSubviews()
            tableView.visibleCells.forEach(Self.medium)
        }
        private static func medium(_ v: UIView) {
            for s in v.subviews {
                if let a = s as? UIActivityIndicatorView {
                    if a.style != .medium { a.style = .medium }
                } else {
                    medium(s)
                }
            }
        }
    }

    /// Esc closes it like Done (a Mac keyboard, an iPad's)
    private final class Sheet: UINavigationController {
        override var keyCommands: [UIKeyCommand]? {
            [UIKeyCommand(input: UIKeyCommand.inputEscape, modifierFlags: [], action: #selector(escape))]
        }
        @objc private func escape() { BluetoothPicker.dismiss() }
    }
}
