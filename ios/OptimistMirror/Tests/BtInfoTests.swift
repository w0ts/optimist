// SPDX-License-Identifier: GPL-3.0-only
// INFO over Bluetooth: the replies an FM-1 sent there (Fixtures/bt_info_frames.json, recorded 2026-10-31 on fm1-next 56a99f36 with
// BT_PROJ), whole and damaged; the link takes the whole ones only (MirrorLink.takeInfo) and asks INFO beside the mirror's request
// a few times before it gives up (BtAsk). The web editor reads the same fixture (web/test_web.mjs, btInfoOk).
import XCTest
@testable import OptimistMirror

final class BtInfoTests: XCTestCase {
    private struct Frames: Decodable { let whole: [String]; let damaged: [String] }

    private func frames() throws -> Frames {
        let url = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "bt_info_frames", withExtension: "json"))
        return try JSONDecoder().decode(Frames.self, from: Data(contentsOf: url))
    }

    /// an F0 .. F7 frame in hex -> INFO's arguments
    private func args(_ hex: String) throws -> [UInt8] {
        let bytes = stride(from: 0, to: hex.count, by: 2).map { k -> UInt8 in
            let a = hex.index(hex.startIndex, offsetBy: k)
            return UInt8(hex[a..<hex.index(a, offsetBy: 2)], radix: 16)!
        }
        let u = try XCTUnwrap(Wire.unframe(bytes))
        XCTAssertEqual(u.cmd, 1)
        return u.args
    }

    func testWholeRepliesAreTakenAndListBtProj() throws {
        for hex in try frames().whole {
            let i = try DeviceInfo.parse(try args(hex))
            XCTAssertTrue(i.intact)
            XCTAssertTrue(i.wholeOverBluetooth)
            XCTAssertTrue(i.has(.mirror))
            XCTAssertTrue(i.has(.btproj))
            XCTAssertTrue(i.has(.mirbt))
            let taken = try XCTUnwrap(MirrorLink.takeInfo(try args(hex), bluetooth: true))
            XCTAssertEqual(EditorRoute.choose(searching: false, bluetooth: true, info: taken, infoTried: true), .bluetooth)
        }
    }

    /// a damaged reply is refused, or (zeros that left every block length whole, outside the names) taken with the whole
    /// reply's features: never a feature list that lost opti_mirror or opti_btproj
    func testDamagedRepliesNeverGiveOtherFeatures() throws {
        let f = try frames()
        let whole = try DeviceInfo.parse(try args(f.whole[0])).features
        var refused = 0
        for hex in f.damaged {
            if let i = MirrorLink.takeInfo(try args(hex), bluetooth: true) {
                XCTAssertEqual(i.features, whole)
            } else {
                refused += 1
            }
        }
        XCTAssertGreaterThan(refused, f.damaged.count / 2)
    }

    func testABlockLongerThanTheReplyIsNotIntact() throws {
        let head: [UInt8] = Array("FM 1".utf8) + [0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 7]
        XCTAssertTrue(try DeviceInfo.parse(head + [0x65, 1, 2]).intact)
        XCTAssertFalse(try DeviceInfo.parse(head + [0x65, 5, 2]).intact)      // (length past the end)
        XCTAssertFalse(try DeviceInfo.parse(head + [0x65, 1, 2, 9]).intact)   // (a byte left over)
        // over USB a reply is taken as before (the cable loses nothing); over Bluetooth one without opti_mirror is refused
        XCTAssertNotNil(MirrorLink.takeInfo(head + [0x65, 1, 2], bluetooth: false))
        XCTAssertNil(MirrorLink.takeInfo(head + [0x65, 1, 2], bluetooth: true))
    }

    func testInfoIsAskedAFewTimesThenGivenUp() {
        let steps = (0..<(BtAsk.tries + BtAsk.grace + 2)).map { BtAsk.next(known: false, ticks: $0) }
        XCTAssertEqual(steps.filter { $0 == .ask }.count, BtAsk.tries)
        XCTAssertEqual(Array(steps.prefix(BtAsk.tries)), Array(repeating: .ask, count: BtAsk.tries))
        XCTAssertEqual(steps[BtAsk.tries], .wait)
        XCTAssertEqual(steps.last, .gaveUp)
        XCTAssertEqual(BtAsk.next(known: true, ticks: 0), BtAsk.none)
        XCTAssertEqual(BtAsk.next(known: true, ticks: 99), BtAsk.none)
    }
}
