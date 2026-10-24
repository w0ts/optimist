// SPDX-License-Identifier: GPL-3.0-only
// MIRROR_DEMO (a DEBUG build on a simulator, MirrorLink.init): 109 NAV answered as an FM-1 would (ed_mirror.c ed_nav), so the
// touch columns can be seen and tapped without one; 111 CLIP too, a 32-step clip on T2 (the clip editor). The rows are a typical ANALOG track's; never in a store build.
#if DEBUG
import Foundation

final class NavDemo {
    private var screen = 1, mode = Nav.Mode.lfo.rawValue, row = 1, track = 2
    private let tracks = ["MASTER", "T1", "T2", "T3", "DR", "KICK", "SNARE", "CL HAT", "OP HAT", "CLAP", "TOM", "RIM", "COW",
                          "CYMB", "SHAKE", "ACCENT"]
    private let fams: [Int: [String]] = [
        Nav.Mode.env.rawValue: ["ENV", "ENV mod", "ENV2", "ENV2 to"],
        Nav.Mode.lfo.rawValue: ["LFO", "LFO to", "LFO2", "LFO2 to"],
        Nav.Mode.fx.rawValue: ["Insert", "Dyn", "EQ3", "EQ3 frq", "Sends"],
        Nav.Mode.scl.rawValue: ["Scale"],
        Nav.Mode.edit.rawValue: ["Sound", "Main", "Osc", "Osc 2", "Filter", "Filter 2", "Amp"],
        Nav.Mode.arp.rawValue: ["ARP", "ARP 2"],
    ]
    private var rows: [String] {
        switch screen {
        case 1: mode == Nav.none ? fams.keys.sorted().flatMap { fams[$0]! } : fams[mode] ?? []
        case 0: ["Mix", "Insert", "Sends", "Sound"]
        case 2: ["Insert", "Returns", "Reverb", "Delay", "Chorus", "Limit"]
        case 3: ["Bank", "Operation", "Item"]
        case 5: ["Pattern", "Tools", "Gen", "Gen out"]
        case 6: ["Scenes", "Song", "Settings"]
        default: []
        }
    }
    private var hasTrack: Bool { [0, 1, 2, 5].contains(screen) }

    /// MIRROR_DEMO_NAV = "screen,mode,row,track": the columns as the demo picture's page (a store screenshot)
    init() {
        let v = (ProcessInfo.processInfo.environment["MIRROR_DEMO_NAV"] ?? "").split(separator: ",").compactMap { Int($0) }
        if v.count == 4 { (screen, mode, row, track) = (v[0], v[1], v[2], v[3]) }
    }

    private lazy var steps: [Clip.Step] = {
        let notes = [Clip.Note(start: 0, pitch: 48, length: 512), Clip.Note(start: 0, pitch: 60, length: 128), Clip.Note(start: 0, pitch: 64, length: 256),
                     Clip.Note(start: 4, pitch: 67, length: 64), Clip.Note(start: 6, pitch: 65, length: 192), Clip.Note(start: 8, pitch: 50, length: 384),
                     Clip.Note(start: 10, pitch: 62, length: 128), Clip.Note(start: 12, pitch: 69, length: 96, flags: Clip.accent),
                     Clip.Note(start: 14, pitch: 67, length: 128), Clip.Note(start: 16, pitch: 45, length: 768), Clip.Note(start: 18, pitch: 57, length: 256),
                     Clip.Note(start: 24, pitch: 72, length: 128)]
        return (try? Clip.steps(notes, len: 32, old: Array(repeating: Clip.Step(), count: 64))) ?? []
    }()

    /// 111 CLIP as ed_clip.c answers it: T2 (track 1) a synth of 32 steps, the drums track 3
    func clip(_ a: [UInt8]) -> [UInt8] {
        let t = Int(a.count > 1 ? a[1] : 0), i = Int(a.count > 2 ? a[2] : 0)
        switch a.first ?? 127 {
        case 0: return [0, 0, UInt8(max(0, min(2, track - 1))), 4, 32, 0, 32, 0, 32, 0, 16, 1]
        case 1:
            guard t < 3, i < 64, a.count >= 4 else { return [1, 1, UInt8(t), UInt8(i), 0] }
            let n = min(16, Int(a[3]), 64 - i)
            return [1, 0, UInt8(t), UInt8(i), UInt8(n)] + (i..<(i + n)).flatMap { steps[$0].bytes }
        case 2:
            guard t < 3, i < 64, a.count >= 18, let s = try? Clip.Step(Array(a[3..<18])) else { return [2, 1, UInt8(t), UInt8(i)] }
            steps[i] = s
            return [2, 0, UInt8(t), UInt8(i)] + s.bytes
        default: return []
        }
    }

    func reply(_ a: [UInt8]) -> [UInt8] {
        let op = a.first ?? 0
        var ok = true
        switch op {
        case 1 where a.count > 1 && (Int(a[1]) < rows.count || (screen == 6 && Int(a[1]) == Nav.songPatterns)): row = Int(a[1])
        case 2 where a.count > 1 && hasTrack: track = Int(a[1]); screen = track == 0 ? 2 : screen == 2 ? 1 : screen
        case 3 where a.count > 2: screen = Int(a[1]); mode = screen == 1 ? Int(a[2]) : Nav.none; row = 0
        case 0, 4: break
        default: ok = false
        }
        if op == 4 { return [4, 0, UInt8(tracks.count)] + tracks.flatMap { Array($0.utf8) + [0] } }
        let r = rows
        return [op, ok ? 0 : 1, UInt8(screen), UInt8(mode), UInt8(screen == 6 && row == Nav.songPatterns ? row : min(row, max(r.count - 1, 0))), UInt8(r.count)]
            + r.flatMap { Array($0.utf8) + [0] } + [UInt8(hasTrack ? track : Nav.none), screen == 5 ? 0 : screen == 0 ? 1 : 2]
    }
}
#endif
