// SPDX-License-Identifier: GPL-3.0-only
// What to tell the user when a Bluetooth connection is started and cannot work (the user, 2026-11-01: "the apps should detect
// when we try to connect over BT and BT is off and let the user know"). Pure: the device's Bluetooth state (CoreBluetooth's
// CBManagerState, mapped by BluetoothWatch) and the platform in, a decision and the words out; unit-tested (BtStateTests).
// The web editor says the same no-FM-1 sentence (web/editor.html, nodevicebt): keep the two aligned.
import Foundation

/// CBManagerState, without CoreBluetooth (so the tests need no radio)
enum BtState: Equatable {
    case unknown, resetting, unsupported, unauthorized, poweredOff, poweredOn
}

enum BtPlatform: Equatable {
    case iPhone, iPad, mac

    /// "this iPhone"
    var device: String {
        switch self {
        case .iPhone: "iPhone"
        case .iPad: "iPad"
        case .mac: "Mac"
        }
    }
    /// the app where Bluetooth is switched and allowed
    var settings: String { self == .mac ? "System Settings" : "Settings" }
}

/// What a Bluetooth connection does in a state
enum BtDecision: Equatable {
    /// the radio is on and allowed: open the connection sheet
    case proceed
    /// the state is not known yet (the system is still asking, or resetting): wait briefly
    case wait
    /// cannot work: say why. `settings`: offer a button to the Settings page where it is fixed.
    case block(title: String, message: String, settings: Bool)

    var message: String? {
        if case .block(_, let m, _) = self { return m }
        return nil
    }
}

enum BtAdvice {
    /// seconds a user-started scan may find no FM-1 before the hint shows
    static let noDeviceAfter: TimeInterval = 5
    /// the longest the state is waited for while it is .unknown / .resetting, when no permission question is on screen
    static let stateWait: TimeInterval = 3

    /// the state of the device's Bluetooth -> what to do
    static func decide(_ state: BtState, on platform: BtPlatform) -> BtDecision {
        switch state {
        case .poweredOn:
            return .proceed
        case .unknown, .resetting:
            return .wait
        case .poweredOff:
            let how = platform == .mac ? "System Settings" : "Control Center or Settings"
            return .block(title: "Bluetooth is off",
                          message: "Bluetooth is off on this \(platform.device). Turn it on in \(how).", settings: false)
        case .unauthorized:
            return .block(title: "Bluetooth not allowed",
                          message: "Optimist Mirror may not use Bluetooth. Allow it in \(platform.settings) > Privacy & Security > Bluetooth.",
                          settings: true)
        case .unsupported:
            return .block(title: "No Bluetooth", message: "This device has no Bluetooth LE.", settings: false)
        }
    }

    static let noFM1 = "No FM-1 found over Bluetooth. Is SYSTEM > BT MIDI on, and the FM-1 close by?"

    /// The hint while the connection sheet scans: after `noDeviceAfter` seconds with no FM-1 seen (advertising, or a port already
    /// online) and the radio on. When the radio is not on, its own message instead. nil: nothing to say (yet).
    static func hint(state: BtState, platform: BtPlatform, scanned: TimeInterval, sawFM1: Bool) -> String? {
        switch decide(state, on: platform) {
        case .block(_, let m, _): return m
        case .wait: return nil
        case .proceed: return !sawFM1 && scanned >= noDeviceAfter ? noFM1 : nil
        }
    }
}
