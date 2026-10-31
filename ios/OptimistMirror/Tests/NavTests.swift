// SPDX-License-Identifier: GPL-3.0-only
// 109 NAV's replies read as the firmware writes them (FakeFM1.nav: ed_mirror.c ed_nav's layout), the favourites' labels and storage.
import XCTest
@testable import OptimistMirror

final class NavTests: XCTestCase {
    func testStateParsed() async throws {
        let fm1 = FakeFM1(objs: [])
        let s = try NavState.parse(try await fm1.exchange(.nav, [0], timeout: 1, retries: 0))
        XCTAssertEqual(s, NavState(screen: Nav.Screen.sound.rawValue, mode: Nav.Mode.lfo.rawValue, row: 1,
                                   rows: ["LFO", "LFO DEST", "LFO 2"], track: 2, knobs: NavState.knobRects(2)))
        XCTAssertEqual(s.knobs[3], CGRect(x: 121, y: 75, width: 116, height: 46))   // (KNOB 4 bottom right)
        XCTAssertEqual(NavState.knobRects(lines: 28, pitch: 48)[1].minY, 76)
        let r = try NavState.parse(try await fm1.exchange(.nav, [1, 2], timeout: 1, retries: 0))
        XCTAssertEqual(r.row, 2)
        XCTAssertFalse(r.refused)
        XCTAssertTrue(try NavState.parse(FakeFM1.nav([9])).refused)
    }

    func testTracksParsed() throws {
        XCTAssertEqual(try NavState.parseTracks(FakeFM1.nav([4])), ["RET A", "RET B", "RET C", "MASTER", "T1", "T2", "T3", "DR", "KICK", "SNARE"])
    }

    func testLaneButtonsAlwaysThereGreyedOffTheDrumTrack() {
        let kit = ["RET A", "RET B", "RET C", "MASTER", "T1", "T2", "T3", "DR", "KICK", "SNARE"]
        let on = NavTracks.lanes(tracks: kit, current: Nav.drumRow + 1)
        XCTAssertEqual(on.names, ["KICK", "SNARE"])
        XCTAssertTrue(on.available)
        XCTAssertTrue(NavTracks.lanes(tracks: kit, current: Nav.drumRow).available)
        let off = NavTracks.lanes(tracks: kit, current: Nav.t1Row + 1)           // (T2: the lanes stay, greyed)
        XCTAssertEqual(off.names, ["KICK", "SNARE"])
        XCTAssertFalse(off.available)
        XCTAssertFalse(NavTracks.lanes(tracks: kit, current: Nav.none).available)
        let bare = NavTracks.lanes(tracks: [], current: Nav.none)    // (no names yet: the default lanes)
        XCTAssertEqual(bare.names, Nav.defaultLanes)
        XCTAssertFalse(bare.available)
    }

    /// The same cases the web editor's navBar is tested on (Tests/Fixtures/nav_track_bar.json): the app and the web always aligned
    func testTrackBarMatchesTheSharedCases() throws {
        let url = Bundle(for: Self.self).url(forResource: "nav_track_bar", withExtension: "json", subdirectory: "Fixtures")
            ?? Bundle(for: Self.self).url(forResource: "nav_track_bar", withExtension: "json")
        let json = try JSONSerialization.jsonObject(with: Data(contentsOf: try XCTUnwrap(url))) as! [String: Any]
        for c in json["cases"] as! [[String: Any]] {
            let names = c["names"] as! [String]
            let bar = Nav.bar(count: names.count)
            XCTAssertEqual(bar.lanes.map { names[$0] }, c["lanes"] as! [String])
            XCTAssertEqual(bar.bottom.map { names[$0] }, c["bottom"] as! [String])
        }
    }

    func testShortReplyThrows() {
        XCTAssertThrowsError(try NavState.parse([0, 0, 1, 3, 1, 3, 0x4C, 0]))
    }

    func testFavoriteLabelAndRoundTrip() throws {
        let page = Favorite(screen: 1, mode: 3, page: "LFO 2", index: 2)
        let onLane = Favorite(screen: 1, mode: 3, page: "LFO", index: 0, track: 6, trackName: "SNARE")
        XCTAssertEqual(page.label, "LFO 2")
        XCTAssertEqual(onLane.label, "SNARE LFO")
        let d = try JSONEncoder().encode([page, onLane])
        XCTAssertEqual(try JSONDecoder().decode([Favorite].self, from: d), [page, onLane])
    }

    @MainActor func testScreenAloneModeDefaultsToButtonsAndIsRemembered() throws {
        let suite = "mirror-alone-test"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
        defer { defaults.removePersistentDomain(forName: suite) }
        let first = NavLayoutStore(defaults: defaults)
        XCTAssertFalse(first.screenAlone)                       // (default: with the buttons)
        first.toggle()
        XCTAssertTrue(first.screenAlone)
        XCTAssertTrue(NavLayoutStore(defaults: defaults).screenAlone)    // (a new instance: remembered)
        first.toggle()
        XCTAssertFalse(NavLayoutStore(defaults: defaults).screenAlone)
    }
}
