// SPDX-License-Identifier: GPL-3.0-only
// Watches the device's own Bluetooth (CoreBluetooth) for a connection the user starts (the user, 2026-11-01: "the apps should
// detect when we try to connect over BT and BT is off and let the user know"). The words and the decisions are BtAdvice
// (BluetoothState.swift); this file only feeds it the live state and shows the answer:
//  - the radio is off / not allowed / missing: an alert says so (updated live; gone once Bluetooth is on: nothing connects by
//    itself, the user presses Connect again: "NO AUTOCONNECT"), the connection sheet is not opened;
//  - the radio is on: the sheet opens, and while it is up the manager scans (names only) for an FM-1; none within 5 s -> the
//    sheet's prompt line says "No FM-1 found over Bluetooth ...", the scan goes on and the line goes when one shows.
// The manager is made at the first press (that is when the system asks to allow Bluetooth), never at launch.
import CoreBluetooth
import UIKit

@MainActor
final class BluetoothWatch: NSObject, CBCentralManagerDelegate {
    static let shared = BluetoothWatch()

    private var manager: CBCentralManager?
    private(set) var state: BtState = .unknown
    private var alert: UIAlertController?
    private var waiting: [String]?              // a press waiting for the state: the ports online then
    private var waitTimer: Timer?
    private var scanStart: Date?
    private var sawFM1 = false
    private var hintTimer: Timer?

    static var platform: BtPlatform {
#if targetEnvironment(macCatalyst)
        .mac
#else
        UIDevice.current.userInterfaceIdiom == .pad ? .iPad : .iPhone
#endif
    }

    static func map(_ s: CBManagerState) -> BtState {
        switch s {
        case .poweredOn: .poweredOn
        case .poweredOff: .poweredOff
        case .unauthorized: .unauthorized
        case .unsupported: .unsupported
        case .resetting: .resetting
        default: .unknown
        }
    }

    /// The user pressed a Bluetooth connect button: the sheet when Bluetooth works, else why not. `ports`: the MIDI ports online.
    func connect(ports: [String]) {
        if manager == nil {
            // (no system "turn on Bluetooth" alert: ours says it, with the right words per device)
            manager = CBCentralManager(delegate: self, queue: .main, options: [CBCentralManagerOptionShowPowerAlertKey: false])
            state = Self.map(manager?.state ?? .unknown)
        }
        resolve(ports: ports, fresh: true)
    }

    private func resolve(ports: [String], fresh: Bool) {
        switch BtAdvice.decide(state, on: Self.platform) {
        case .proceed:
            clearWait()
            dismissAlert()
            if !BluetoothPicker.isShown { BluetoothPicker.showSheet(ports: ports); startScan(ports: ports) }
        case .wait:
            guard fresh else { return }
            waiting = ports
            // (while the system's own Bluetooth question is on screen the answer comes when it is answered)
            if CBCentralManager.authorization != .notDetermined {
                waitTimer?.invalidate()
                waitTimer = Timer.scheduledTimer(withTimeInterval: BtAdvice.stateWait, repeats: false) { [weak self] _ in
                    MainActor.assumeIsolated { self?.waitOver() }
                }
            }
        case .block(let title, let message, let settings):
            clearWait()
            show(title: title, message: message, settings: settings)
        }
    }

    /// the state did not come in time: let the system's sheet show what it can
    private func waitOver() {
        guard let ports = waiting else { return }
        clearWait()
        if !BluetoothPicker.isShown { BluetoothPicker.showSheet(ports: ports); startScan(ports: ports) }
    }

    private func clearWait() {
        waiting = nil
        waitTimer?.invalidate()
        waitTimer = nil
    }

    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        MainActor.assumeIsolated {
            state = Self.map(central.state)
            if let ports = waiting { resolve(ports: ports, fresh: false) }
            else if alert != nil { refreshAlert() }
            if BluetoothPicker.isShown { updateHint() }
            if state != .poweredOn { stopScan() } else if BluetoothPicker.isShown, scanStart == nil { startScan(ports: []) }
        }
    }

    // MARK: alert

    private func show(title: String, message: String, settings: Bool) {
        if let alert { alert.title = title; alert.message = message; return }
        let a = UIAlertController(title: title, message: message, preferredStyle: .alert)
        if settings {
            a.addAction(UIAlertAction(title: Self.platform == .mac ? "Open System Settings" : "Open Settings", style: .default) { [weak self] _ in
                self?.alert = nil
                if let url = Self.settingsURL { UIApplication.shared.open(url) }
            })
        }
        a.addAction(UIAlertAction(title: "OK", style: .cancel) { [weak self] _ in self?.alert = nil })
        guard let scene = UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }).first,
              var top = scene.keyWindow?.rootViewController else { return }
        while let p = top.presentedViewController { top = p }
        top.present(a, animated: true)
        alert = a
    }

    /// the alert follows the state: the new words, or gone when Bluetooth is on (the user connects again)
    private func refreshAlert() {
        if case .block(let title, let message, _) = BtAdvice.decide(state, on: Self.platform) {
            alert?.title = title
            alert?.message = message
        } else {
            dismissAlert()
        }
    }

    private func dismissAlert() {
        alert?.dismiss(animated: true)
        alert = nil
    }

    private static var settingsURL: URL? {
#if targetEnvironment(macCatalyst)
        URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Bluetooth")
#else
        URL(string: UIApplication.openSettingsURLString)
#endif
    }

    // MARK: scan hint

    /// The sheet is up: look for an FM-1 advertising (the sheet lists it by itself; this only knows whether one is there)
    private func startScan(ports: [String]) {
        guard let manager, state == .poweredOn else { return }
        scanStart = Date()
        sawFM1 = ports.contains(where: MirrorLink.isFM1Name)
        manager.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        hintTimer?.invalidate()
        hintTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self else { return }
                if BluetoothPicker.isShown { self.updateHint() } else { self.stopScan() }
            }
        }
    }

    private func stopScan() {
        manager?.stopScan()
        hintTimer?.invalidate()
        hintTimer = nil
        scanStart = nil
    }

    private func updateHint() {
        let scanned = scanStart.map { Date().timeIntervalSince($0) } ?? 0
        BluetoothPicker.setPrompt(BtAdvice.hint(state: state, platform: Self.platform, scanned: scanned, sawFM1: sawFM1))
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                                    advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name ?? ""
        guard MirrorLink.isFM1Name(name) else { return }
        MainActor.assumeIsolated {
            sawFM1 = true
            if BluetoothPicker.isShown { updateHint() }
        }
    }
}
