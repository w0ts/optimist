// SPDX-License-Identifier: GPL-3.0-only
// MIRROR_DEMO (a DEBUG build on a simulator, MirrorLink.init): 109 NAV answered as an FM-1 would (ed_mirror.c ed_nav), so the
// touch columns can be seen and tapped without one. The rows are a typical ANALOG track's; never in a store build.
#if DEBUG
import Foundation

final class NavDemo {
    private var screen = 1, mode = Nav.Mode.lfo.rawValue, row = 1, track = 2
    private let tracks = ["MASTER", "T1", "T2", "T3", "DR", "KICK", "SNARE", "CL HAT", "OP HAT", "CLAP", "TOM", "RIM", "COW",
                          "CYMB", "SHAKE", "ACCENT"]
    private let fams: [Int: [String]] = [
        Nav.Mode.env.rawValue: ["ENV", "ENV 1 MOD", "ENV2", "ENV2 DEST"],
        Nav.Mode.lfo.rawValue: ["LFO", "LFO DEST", "LFO 2", "LFO2 DEST"],
        Nav.Mode.fx.rawValue: ["INSERTS", "DYN", "EQ3", "EQ3 FREQ", "SENDS"],
        Nav.Mode.scl.rawValue: ["SCALE"],
        Nav.Mode.edit.rawValue: ["SOUND", "MAIN", "OSC", "OSC 2", "FILTER", "FLT 2", "AMP"],
        Nav.Mode.arp.rawValue: ["ARP", "ARP 2"],
    ]
    private var rows: [String] {
        switch screen {
        case 1: mode == Nav.none ? fams.keys.sorted().flatMap { fams[$0]! } : fams[mode] ?? []
        case 0: ["MIX", "INSERT", "SENDS", "SOUND"]
        case 2: ["INSERT", "RETURNS", "REVERB", "DELAY", "CHORUS", "LIMIT"]
        case 3: ["BANK", "OPERATION", "ITEM"]
        case 5: ["PATTERN", "TOOLS", "GEN", "GEN OUT"]
        case 6: ["PATTERNS", "SCENES"]
        default: []
        }
    }
    private var hasTrack: Bool { [0, 1, 2, 5].contains(screen) }

    func reply(_ a: [UInt8]) -> [UInt8] {
        let op = a.first ?? 0
        var ok = true
        switch op {
        case 1 where a.count > 1 && Int(a[1]) < rows.count: row = Int(a[1])
        case 2 where a.count > 1 && hasTrack: track = Int(a[1]); screen = track == 0 ? 2 : screen == 2 ? 1 : screen
        case 3 where a.count > 2: screen = Int(a[1]); mode = screen == 1 ? Int(a[2]) : Nav.none; row = 0
        case 0, 4: break
        default: ok = false
        }
        if op == 4 { return [4, 0, UInt8(tracks.count)] + tracks.flatMap { Array($0.utf8) + [0] } }
        let r = rows
        return [op, ok ? 0 : 1, UInt8(screen), UInt8(mode), UInt8(min(row, max(r.count - 1, 0))), UInt8(r.count)]
            + r.flatMap { Array($0.utf8) + [0] } + [UInt8(hasTrack ? track : Nav.none), screen == 5 ? 0 : screen == 0 ? 1 : 2]
    }
}
#endif
