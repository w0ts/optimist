// SPDX-License-Identifier: GPL-3.0-only
// What 109 NAV says (firmware io/editor/ed_mirror.c ed_nav, ui/optimist/op_nav.c; web/EDITOR_PROTOCOL.md "The touch
// navigation"): the screen, the page button whose family SOUND shows, the rows as SELECT walks them, the cursor row, the
// track ALGORITHM is on; and the mixer rows' names (MASTER, T1..T3, DR, the lanes, ACCENT). The favourites, kept on this device.
import CoreGraphics
import Foundation

enum Nav {
    /// The firmware's screens (op_state.c SCR_*), the numbers 109 NAV OPEN takes
    enum Screen: Int, CaseIterable {
        case mix = 0, sound, master, project, system, steps, song, tempo, scope, global
        var title: String {
            ["MIX", "SOUND", "MASTER", "PROJECT", "SYSTEM", "STEPS", "SONG", "TEMPO", "SCOPE", "GLOBAL"][rawValue]
        }
    }
    /// panel.c's labels of the page buttons (B_*): a SOUND family each
    enum Mode: Int, CaseIterable {
        case fx = 0, scl = 1, env = 2, lfo = 3, edit = 4, arp = 8
        var title: String {
            switch self {
            case .fx: "FX"
            case .scl: "SCL"
            case .env: "ENV"
            case .lfo: "LFO"
            case .edit: "EDIT"
            case .arp: "ARP"
            }
        }
    }
    static let none = 127
    /// Mixer row numbers (op_mixer.c MXR_*): 0 MASTER, 1..3 T1..T3, 4 DR, 5.. the drum lanes, then ACCENT
    static let drumRow = 4

    enum Op: UInt8 { case state = 0, row, track, open, tracks }
}

struct NavState: Equatable {
    var screen: Int
    var mode: Int                 // a page button (Nav.Mode), Nav.none: every row / another screen
    var row: Int
    var rows: [String]
    var track: Int                // a mixer row, Nav.none: no track on this screen
    var refused = false
    /// KNOB 1..4's places on the 240 x 240 panel (empty: none here): a drag there turns that knob
    var knobs: [CGRect] = []

    /// A reply to 0..3: op, refused, screen, mode, row, n, n names, track
    static func parse(_ a: [UInt8]) throws -> NavState {
        var r = Reader(a)
        _ = try r.b()
        let refused = try r.b() != 0
        let screen = Int(try r.b()), mode = Int(try r.b()), row = Int(try r.b()), n = Int(try r.b())
        var rows: [String] = []
        for _ in 0..<n { rows.append(try r.s()) }
        var s = NavState(screen: screen, mode: mode, row: row, rows: rows, track: Int(try r.b()), refused: refused)
        if !r.atEnd {
            let layout = try r.b()
            s.knobs = layout == 3 ? knobRects(lines: CGFloat(try r.b()), pitch: CGFloat(try r.b())) : knobRects(layout)
        }
        return s
    }

    /// KNOB 1..4's places for layout 1 (the 1x4 cards) or 2 (the 2x2 cards), as op_draw.c draws them (op_graph.c CARD_X CARD_W
    /// OY_CARD OH_CARD, op_draw.c BIG_W BIG_H); any other: none
    static func knobRects(_ layout: UInt8) -> [CGRect] {
        switch layout {
        case 1: (0..<4).map { CGRect(x: 3 + 59 * CGFloat($0), y: 28, width: 57, height: 45) }
        case 2: (0..<4).map { CGRect(x: $0 & 1 == 1 ? 121 : 3, y: 28 + ($0 & 2 == 2 ? 47 : 0), width: 116, height: 46) }
        default: []
        }
    }
    /// ... layout 3: the four lines across the panel, the first from `top`, `pitch` apart (op_gldraw.c)
    static func knobRects(lines top: CGFloat, pitch: CGFloat) -> [CGRect] {
        (0..<4).map { CGRect(x: 0, y: top + CGFloat($0) * pitch, width: 240, height: pitch) }
    }

    /// A reply to 4 TRACKS: op, refused, n, n names
    static func parseTracks(_ a: [UInt8]) throws -> [String] {
        var r = Reader(a)
        _ = try r.b()
        _ = try r.b()
        let n = Int(try r.b())
        return try (0..<n).map { _ in try r.s() }
    }
}

/// A page kept for one tap: where it is (a screen, on SOUND a family), its row by name (the index when the name is gone), and
/// with `track` set the track or lane too (a mixer row; nil: the page on whatever track is selected).
struct Favorite: Codable, Equatable, Identifiable {
    var id = UUID()
    var screen: Int
    var mode: Int
    var page: String
    var index: Int
    var track: Int?
    var trackName: String?

    var label: String { trackName.map { "\($0) \(page)" } ?? page }
}

/// The favourites in UserDefaults (this device only: nothing goes to the FM-1)
enum FavoriteStore {
    private static let key = "favorites"
    static func load() -> [Favorite] {
        guard let d = UserDefaults.standard.data(forKey: key) else { return [] }
        return (try? JSONDecoder().decode([Favorite].self, from: d)) ?? []
    }
    static func save(_ f: [Favorite]) {
        if let d = try? JSONEncoder().encode(f) { UserDefaults.standard.set(d, forKey: key) }
    }
}
