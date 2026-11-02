// SPDX-License-Identifier: GPL-3.0-only
// iOS's Bluetooth MIDI sheet (CoreAudioKit): lists the MIDI peripherals advertising nearby (the FM-1 with SYSTEM > Bluetooth on)
// and connects one; its port then appears in CoreMIDI and MirrorLink takes it when no USB cable is plugged in. Presented by UIKit
// over whatever is on top; closed by itself once the app has taken the port connected in it (done(...); the user, 2026-10-24:
// "close the bt window when the app successfully connect", 2026-10-29: "the BT connection window should CLOSE on successful
// connect"), whatever its name and whatever the FM-1 answers then: it waited for the mirrored screen, so a port whose name the
// app did not know, or an FM-1 that did not answer, left it open. Opening or closing it connects nothing: the app takes a port
// only when one appears while the sheet is up, a device picked there ("it connects automatically. i did not ask for that";
// "NO AUTOCONNECT").
import CoreAudioKit
import UIKit

enum BluetoothPicker {
    @MainActor private static weak var shown: UINavigationController?
    /// the MIDI ports online when it opened: one more online is a device connected in it
    @MainActor private(set) static var before: Set<String> = []
    @MainActor static var isShown: Bool { shown != nil }
    /// the port that came online while the sheet was up (the device connected in it), "" before one did
    @MainActor private(set) static var picked = ""

    /// Done, Esc, or connected (RootView)
    @MainActor static func dismiss() {
        shown?.dismiss(animated: true)
        shown = nil
        picked = ""
    }

    /// RootView: a port came online while the sheet is up (the link is then told to take it). -> that port, nil when the
    /// sheet is not up or nothing new came
    @MainActor static func connected(old: [String], now: [String]) -> String? {
        guard isShown, let p = newPort(old: old, now: now) else { return nil }
        picked = p
        return p
    }

    /// the first port online now that was not before
    static func newPort(old: [String], now: [String]) -> String? { now.first { !old.contains($0) } }

    /// The sheet's work is done: the link took the port connected in it (`picked`, any name: "FM1-XXXX", or one iOS kept from
    /// older firmware, "Optimist XXXX", "FM-1 XXXX"), asking or mirroring; or it mirrors over Bluetooth anyway (a port the
    /// link took by itself). Not while it still searches (the port gone again, the link paused).
    static func done(picked: String, port: String, bluetooth: Bool, state: MirrorLink.State) -> Bool {
        guard state != .searching, !port.isEmpty else { return false }
        return (!picked.isEmpty && port == picked) || (bluetooth && state == .mirroring)
    }

    /// The user pressed a Bluetooth connect button. ports: the MIDI ports online now (MirrorLink.online). The sheet opens when the
    /// device's Bluetooth is on and allowed, else BluetoothWatch says why not (BluetoothState.swift has the words).
    @MainActor static func present(ports: [String] = []) { BluetoothWatch.shared.connect(ports: ports) }

    /// the line under the sheet's title: the Bluetooth hint (nil: none)
    @MainActor static func setPrompt(_ text: String?) {
        guard let c = central, c.navigationItem.prompt != text else { return }
        c.navigationItem.prompt = text
    }

    @MainActor private static weak var central: UIViewController?

    /// the sheet itself (BluetoothWatch: Bluetooth is on)
    @MainActor static func showSheet(ports: [String]) {
        guard let scene = UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }).first,
              var top = scene.keyWindow?.rootViewController else { return }
        while let p = top.presentedViewController { top = p }
        let central = Central()
        self.central = central
        let nav = Sheet(rootViewController: central)
        // (on the left: the sheet puts its Edit button on the right, and on the Mac that hid a Done there)
        central.navigationItem.leftBarButtonItem = UIBarButtonItem(systemItem: .done, primaryAction: UIAction { _ in dismiss() })
        nav.isModalInPresentation = true                // (closed by Done or Esc only)
        nav.modalPresentationStyle = .formSheet
        top.present(nav, animated: true)
        shown = nav
        picked = ""
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
