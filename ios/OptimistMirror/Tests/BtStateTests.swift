// SPDX-License-Identifier: GPL-3.0-only
// The words and decisions for a Bluetooth connection the device cannot make (BluetoothState.swift): each state, each platform,
// and the 5 s no-FM-1 hint. The web editor says the same no-FM-1 sentence (web/test_web.mjs checks it).
import XCTest
@testable import OptimistMirror

final class BtStateTests: XCTestCase {
    private let all: [BtPlatform] = [.iPhone, .iPad, .mac]

    func testPoweredOnProceedsOnEveryPlatform() {
        for p in all { XCTAssertEqual(BtAdvice.decide(.poweredOn, on: p), .proceed) }
    }

    func testUnknownAndResettingWait() {
        for p in all {
            XCTAssertEqual(BtAdvice.decide(.unknown, on: p), .wait)
            XCTAssertEqual(BtAdvice.decide(.resetting, on: p), .wait)
        }
    }

    func testOffNamesTheDevice() {
        XCTAssertEqual(BtAdvice.decide(.poweredOff, on: .iPhone).message,
                       "Bluetooth is off on this iPhone. Turn it on in Control Center or Settings.")
        XCTAssertEqual(BtAdvice.decide(.poweredOff, on: .iPad).message,
                       "Bluetooth is off on this iPad. Turn it on in Control Center or Settings.")
        XCTAssertEqual(BtAdvice.decide(.poweredOff, on: .mac).message,
                       "Bluetooth is off on this Mac. Turn it on in System Settings.")
    }

    func testOffHasNoSettingsButton() {
        for p in all {
            guard case .block(_, _, let settings) = BtAdvice.decide(.poweredOff, on: p) else { return XCTFail() }
            XCTAssertFalse(settings)
        }
    }

    func testUnauthorizedPointsToPrivacyAndOffersSettings() {
        XCTAssertEqual(BtAdvice.decide(.unauthorized, on: .iPhone).message,
                       "Optimist Mirror may not use Bluetooth. Allow it in Settings > Privacy & Security > Bluetooth.")
        XCTAssertEqual(BtAdvice.decide(.unauthorized, on: .iPad).message,
                       "Optimist Mirror may not use Bluetooth. Allow it in Settings > Privacy & Security > Bluetooth.")
        XCTAssertEqual(BtAdvice.decide(.unauthorized, on: .mac).message,
                       "Optimist Mirror may not use Bluetooth. Allow it in System Settings > Privacy & Security > Bluetooth.")
        for p in all {
            guard case .block(_, _, let settings) = BtAdvice.decide(.unauthorized, on: p) else { return XCTFail() }
            XCTAssertTrue(settings)
        }
    }

    func testUnsupported() {
        for p in all {
            XCTAssertEqual(BtAdvice.decide(.unsupported, on: p).message, "This device has no Bluetooth LE.")
        }
    }

    func testOnlyBlockedStatesCarryAMessage() {
        for s in [BtState.poweredOn, .unknown, .resetting] { XCTAssertNil(BtAdvice.decide(s, on: .iPhone).message) }
    }

    func testNoFM1HintComesAtFiveSeconds() {
        XCTAssertNil(BtAdvice.hint(state: .poweredOn, platform: .iPhone, scanned: 0, sawFM1: false))
        XCTAssertNil(BtAdvice.hint(state: .poweredOn, platform: .iPhone, scanned: 4.9, sawFM1: false))
        XCTAssertEqual(BtAdvice.hint(state: .poweredOn, platform: .iPhone, scanned: 5, sawFM1: false),
                       "No FM-1 found over Bluetooth. Is SYSTEM > BT MIDI on, and the FM-1 close by?")
        XCTAssertEqual(BtAdvice.hint(state: .poweredOn, platform: .mac, scanned: 30, sawFM1: false), BtAdvice.noFM1)
    }

    func testHintGoesWhenAnFM1Shows() {
        XCTAssertNil(BtAdvice.hint(state: .poweredOn, platform: .iPad, scanned: 30, sawFM1: true))
    }

    func testHintFollowsTheRadioWhenItIsNotOn() {
        XCTAssertEqual(BtAdvice.hint(state: .poweredOff, platform: .iPhone, scanned: 1, sawFM1: false),
                       BtAdvice.decide(.poweredOff, on: .iPhone).message)
        XCTAssertNil(BtAdvice.hint(state: .unknown, platform: .iPhone, scanned: 30, sawFM1: false))
    }
}
