// SPDX-License-Identifier: GPL-3.0-only
// A synth track's clip as notes (111 CLIP, firmware io/editor/ed_clip.c; web/EDITOR_PROTOCOL.md "The clip"). The FM-1 keeps
// steps, up to 4 notes each; a note longer than its step goes on through TIE steps (every note sounding) or a carry step that
// names it (SF_CARRY, one note of a chord alone), its own end in 1/128 of a step (0: the step's GATE). Read as seq.c note_span
// reads them; written back as the recorder writes them. Pure functions: the tests run them.
import Foundation

enum Clip {
    static let tick = 128                       // a step, in 1/128 (the own ends' unit)
    static let page = 16                        // steps a page (CLIP 1 reads one at a time)
    static let maxNotes = 4                     // notes a step

    enum Time: UInt8 { case note = 0, tie = 1, rest = 2 }
    static let accent: UInt8 = 1, slide: UInt8 = 2, carry: UInt8 = 0x80

    /// One step as CLIP / TRACK_STEP give it (15 bytes): n, note0..3, time, flags, vel, lvl, hi, rat, e0..e3
    struct Step: Equatable {
        var notes: [UInt8] = []                 // n of them
        var time = Time.rest
        var flags: UInt8 = 0                    // accent, slide, the chance bits (chance.c), carry (bit 7)
        var vel: UInt8 = 100
        var lvl: UInt8 = 0, rat: UInt8 = 0      // 2 bits a note
        var ends: [UInt8] = [0, 0, 0, 0]        // each note's own end, 1..127 / 128; 0 the step's GATE

        var isCarry: Bool { flags & Clip.carry != 0 }
        /// note k's own end where the firmware plays it (seq/ngate.h ngate_of: a NOTE step, no ratchet)
        func end(_ k: Int) -> Int { time == .note && k < notes.count && rat == 0 ? Int(ends[k]) : 0 }

        init() {}

        init(_ b: [UInt8]) throws {
            guard b.count >= 15 else { throw WireError("a step of \(b.count) bytes") }
            let n = min(Int(b[0]), 4)
            notes = Array(b[1..<(1 + n)])
            time = Time(rawValue: b[5]) ?? .rest
            flags = b[6] | (b[9] >> 2 & 1) << 7
            vel = b[7]
            lvl = b[8] | (b[9] & 1) << 7
            rat = b[10] | (b[9] >> 1 & 1) << 7
            ends = Array(b[11..<15])
        }

        var bytes: [UInt8] {
            var b = [UInt8](repeating: 0, count: 15)
            b[0] = UInt8(notes.count)
            for (k, x) in notes.prefix(4).enumerated() { b[1 + k] = x & 0x7F }
            b[5] = time.rawValue
            b[6] = flags & 0x7F
            b[7] = vel & 0x7F
            b[8] = lvl & 0x7F
            b[9] = lvl >> 7 | (rat >> 7) << 1 | (flags >> 7) << 2
            b[10] = rat & 0x7F
            for k in 0..<4 { b[11 + k] = k < notes.count ? ends[k] & 0x7F : 0 }
            return b
        }

        /// nothing in it the clip editor shows
        var silent: Bool { time != .tie && (time == .rest || notes.isEmpty) }
    }

    /// A note: where it is struck, its length (1/128 of a step), what its step gives it
    struct Note: Equatable, Identifiable {
        var start: Int
        var pitch: UInt8
        var length: Int
        var vel: UInt8 = 100
        var flags: UInt8 = 0                    // its step's accent, slide, chance (not carry)
        var lvl: UInt8 = 0, rat: UInt8 = 0      // its own 2 bits each
        var id: Int { start << 7 | Int(pitch) }
        var end: Int { start * Clip.tick + length }
    }

    /// The notes of steps 0..<len (a note goes on as note_span walks it; one that would wrap past the end stops there)
    static func notes(_ steps: [Step], len: Int) -> [Note] {
        let len = min(len, steps.count)
        var carried = Set<Int>()               // step << 7 | pitch: a note a carry step goes on with, not struck there
        var out: [Note] = []
        for i in 0..<len where steps[i].time == .note {
            let s = steps[i]
            for (k, x) in s.notes.enumerated() where !carried.contains(i << 7 | Int(x)) {
                var j = i, kk = k, n = 0, length = 0
                while true {
                    let g = steps[j].end(kk)
                    if g > 0 { length = n * tick + g; break }
                    var t = 1
                    while j + t < len && steps[j + t].time == .tie { t += 1 }
                    n += t
                    guard j + t < len else { length = n * tick; break }
                    let c = steps[j + t]
                    guard c.time == .note, c.isCarry, c.rat == 0, let ck = c.notes.firstIndex(of: x) else { length = n * tick; break }
                    carried.insert((j + t) << 7 | Int(x))
                    j += t
                    kk = ck
                }
                out.append(Note(start: i, pitch: x, length: length, vel: s.vel, flags: s.flags & 0x7F,
                                lvl: s.lvl >> (2 * UInt8(k)) & 3, rat: s.rat >> (2 * UInt8(k)) & 3))
            }
        }
        return out
    }

    enum EditError: Error, Equatable { case tooMany(step: Int), overlap }

    /// The steps that play `notes`, as the recorder writes them: a step with notes struck is a NOTE step (a carry step when
    /// others sound on through it); notes going on alone a TIE when all of them go on through the step and no other ends at its
    /// start (a TIE holds every note sounding there), else a carry step naming them; a note's end inside a step its own end there. Steps with nothing: `old`'s when it had nothing either.
    static func steps(_ notes: [Note], len: Int, old: [Step]) throws -> [Step] {
        let sorted = notes.sorted { ($0.start, $0.pitch) < ($1.start, $1.pitch) }
        for a in sorted.indices {
            for b in sorted.indices where b > a && sorted[b].pitch == sorted[a].pitch && sorted[b].start * tick < sorted[a].end {
                throw EditError.overlap
            }
        }
        var out = old
        for i in 0..<min(len, old.count) {
            let lo = i * tick, hi = lo + tick
            let struck = sorted.filter { $0.start == i }
            let held = sorted.filter { $0.start < i && $0.end > lo }
            if struck.isEmpty && held.isEmpty {
                out[i] = old[i].silent ? old[i] : Step()
                continue
            }
            // a TIE goes on with every note sounding to the step's start: none may end there (it would go on too)
            if struck.isEmpty && held.allSatisfy({ $0.end >= hi }) && !sorted.contains(where: { $0.end == lo }) {
                var s = Step()
                s.time = .tie
                out[i] = old[i].time == .tie ? old[i] : s
                continue
            }
            let all = held + struck
            guard all.count <= maxNotes else { throw EditError.tooMany(step: i) }
            var s = Step()
            s.time = .note
            s.notes = all.map(\.pitch)
            s.vel = struck.first?.vel ?? old[i].vel
            s.flags = (struck.first?.flags ?? 0) | (held.isEmpty ? 0 : carry)
            for (k, x) in all.enumerated() {
                s.ends[k] = x.end < hi ? UInt8(x.end - lo) : 0
                if x.start == i {
                    s.lvl |= x.lvl << (2 * UInt8(k))
                    if held.isEmpty { s.rat |= x.rat << (2 * UInt8(k)) }   // (a carry step plays no ratchet: note_span)
                }
            }
            out[i] = s
        }
        return out
    }

    /// The steps an edit from `before` to `after` may change: from the first step either touches to the step after the last
    /// one either sounds in (a TIE or carry there may have to go)
    static func range(_ before: [Note], _ after: [Note], len: Int) -> Range<Int> {
        let changed = Set(before.map(\.id)).symmetricDifference(after.map(\.id))
        let touched = (before + after).filter { n in
            changed.contains(n.id) || before.first(where: { $0.id == n.id }) != after.first(where: { $0.id == n.id })
        }
        guard let first = touched.map(\.start).min(),
              let last = touched.map({ ($0.end + tick - 1) / tick }).max() else { return 0..<0 }
        return first..<min(len, last + 1)
    }

    /// The FM-1's note name: 60 = C4 (op_step.c note_name)
    static func name(_ p: Int) -> String {
        ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][p % 12] + "\(p / 12 - 1)"
    }
}
