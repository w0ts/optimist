// SPDX-License-Identifier: GPL-3.0-only
// The clip editor's steps <-> notes (Sources/Clip/Clip.swift) against what the firmware reads (seq.c note_span) and the
// recorder writes; the CLIP wire (ed_clip.c).
import XCTest
@testable import OptimistMirror

final class ClipTests: XCTestCase {
    private func empty(_ n: Int = 16) -> [Clip.Step] { Array(repeating: Clip.Step(), count: n) }
    private func note(_ s: Int, _ p: UInt8, _ l: Int) -> Clip.Note { Clip.Note(start: s, pitch: p, length: l) }

    func testStepBytesRoundTrip() throws {
        var s = Clip.Step()
        s.notes = [60, 64, 67]
        s.time = .note
        s.flags = Clip.accent | Clip.carry
        s.vel = 99
        s.lvl = 0b1100_0011
        s.rat = 0b1000_0001
        s.ends = [32, 0, 127, 0]
        let b = s.bytes
        XCTAssertEqual(b.count, 15)
        XCTAssertTrue(b.allSatisfy { $0 < 0x80 })
        XCTAssertEqual(b[9], 1 | 2 | 4)                 // (hi: lvl's bit 7, rat's, carry)
        XCTAssertEqual(try Clip.Step(b), s)
    }

    /// The firmware's own forms: a chord with one note carried over the next step alone, a TIE holding every note
    func testNotesAsNoteSpanReadsThem() throws {
        var st = empty()
        st[0].time = .note; st[0].notes = [60, 64]
        st[1].time = .note; st[1].notes = [64]; st[1].flags = Clip.carry; st[1].ends[0] = 64
        st[4].time = .note; st[4].notes = [48]
        st[5].time = .tie
        st[6].time = .tie
        st[8].time = .note; st[8].notes = [50]; st[8].ends[0] = 32
        let n = Clip.notes(st, len: 16)
        XCTAssertEqual(n.map(\.length), [128, 192, 384, 32])
        XCTAssertEqual(n.map(\.pitch), [60, 64, 48, 50])
        XCTAssertEqual(Clip.notes(st, len: 6).first { $0.pitch == 48 }?.length, 256, "stops at the clip's end")
    }

    func testAddOneStep() throws {
        let st = try Clip.steps([note(3, 62, 128)], len: 16, old: empty())
        XCTAssertEqual(st[3].time, .note)
        XCTAssertEqual(st[3].notes, [62])
        XCTAssertEqual(st[3].ends[0], 0, "a whole step: the step's GATE")
        XCTAssertEqual(st[4], Clip.Step())
    }

    /// 2.5 steps: struck, a TIE, a carry step ending it half way
    func testStretchAcrossSteps() throws {
        let st = try Clip.steps([note(0, 60, 320)], len: 16, old: empty())
        XCTAssertEqual(st[0].notes, [60])
        XCTAssertEqual(st[1].time, .tie)
        XCTAssertEqual(st[2].time, .note)
        XCTAssertTrue(st[2].isCarry)
        XCTAssertEqual(st[2].ends[0], 64)
        XCTAssertEqual(Clip.notes(st, len: 16), [note(0, 60, 320)])
    }

    /// One note of a chord longer than the other: no TIE (it would hold both), a carry step of that note alone
    func testChordOneNoteStretched() throws {
        let want = [note(0, 60, 128), note(0, 64, 256)]
        let st = try Clip.steps(want, len: 16, old: empty())
        XCTAssertEqual(st[1].time, .note)
        XCTAssertTrue(st[1].isCarry)
        XCTAssertEqual(st[1].notes, [64])
        XCTAssertEqual(Clip.notes(st, len: 16), want)
    }

    func testStruckOverAHeldNote() throws {
        let want = [note(0, 48, 512), note(2, 60, 64)]
        let st = try Clip.steps(want, len: 16, old: empty())
        XCTAssertEqual(st[2].notes, [48, 60])
        XCTAssertTrue(st[2].isCarry)
        XCTAssertEqual(st[2].ends, [0, 64, 0, 0])
        XCTAssertEqual(Clip.notes(st, len: 16), want)
    }

    func testMoveAndRemove() throws {
        let old = try Clip.steps([note(0, 60, 256)], len: 16, old: empty())
        let moved = try Clip.steps([note(5, 67, 256)], len: 16, old: old)
        XCTAssertEqual(moved[0], Clip.Step())
        XCTAssertEqual(moved[1], Clip.Step())
        XCTAssertEqual(Clip.notes(moved, len: 16), [note(5, 67, 256)])
        XCTAssertEqual(Clip.range([note(0, 60, 256)], [note(5, 67, 256)], len: 16), 0..<8)
        let removed = try Clip.steps([], len: 16, old: moved)
        XCTAssertTrue(Clip.notes(removed, len: 16).isEmpty)
    }

    func testRefused() {
        let five = (0..<5).map { note(0, UInt8(60 + $0), 128) }
        XCTAssertThrowsError(try Clip.steps(five, len: 16, old: empty())) { XCTAssertEqual($0 as? Clip.EditError, .tooMany(step: 0)) }
        XCTAssertThrowsError(try Clip.steps([note(0, 60, 256), note(1, 60, 128)], len: 16, old: empty())) {
            XCTAssertEqual($0 as? Clip.EditError, .overlap)
        }
    }

    /// What the edit leaves alone stays as the FM-1 had it (an empty step with its own bytes, a step past the edit)
    func testUntouchedStepsKept() throws {
        var old = empty()
        old[9].vel = 33                                  // (a rest with bytes of its own)
        let st = try Clip.steps([note(0, 60, 128)], len: 16, old: old)
        XCTAssertEqual(st[9], old[9])
        XCTAssertEqual(Clip.range([], [note(0, 60, 128)], len: 16), 0..<2)
    }

    func testNames() {
        XCTAssertEqual(Clip.name(60), "C4")
        XCTAssertEqual(Clip.name(61), "C#4")
        XCTAssertEqual(Clip.name(0), "C-1")
    }

    func testFeatureFromTag67AndReplyMatching() throws {
        XCTAssertEqual(DeviceInfo.features("opti_clip_1 ".utf8.map { $0 }[...])["opti_clip"], 1)
        XCTAssertTrue(Wire.replyMatches(111, [0], [0, 0, 1, 4]))
        XCTAssertTrue(Wire.replyMatches(111, [1, 2, 16, 16], [1, 0, 2, 16, 16]))
        XCTAssertFalse(Wire.replyMatches(111, [1, 2, 16, 16], [1, 0, 2, 0, 16]))
        XCTAssertTrue(Wire.replyMatches(111, [2, 0, 5] + Clip.Step().bytes, [2, 0, 0, 5]))
        XCTAssertFalse(Wire.replyMatches(111, [2, 0, 5] + Clip.Step().bytes, [1, 0, 0, 5]))
    }
}
