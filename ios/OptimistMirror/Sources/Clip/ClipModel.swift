// SPDX-License-Identifier: GPL-3.0-only
// The clip editor's state: the selected track and its steps, read with 111 CLIP (a page of 16 a request) while the FM-1 shows
// STEPS; read again when the panel was drawn again (the device's own edits; at most every 1.5 s, never while a finger is on a
// note or a write waits); an edit written as the steps it changes, one CLIP 2 each.
import Foundation

@MainActor
final class ClipModel: ObservableObject {
    @Published private(set) var track = Nav.none
    @Published private(set) var len = 0
    @Published private(set) var drum = false
    @Published private(set) var steps: [Clip.Step] = []
    @Published private(set) var notes: [Clip.Note] = []
    @Published private(set) var problem: String?
    /// a finger on the roll: no reading again under it
    var touching = false
    /// the FM-1 shows STEPS and the roll is chosen (MirrorView): read while it is
    var wanted = false

    private let link: MirrorLink
    private var seen = -1
    private var read = Date.distantPast
    private var writing = 0

    init(link: MirrorLink) { self.link = link }

    var on: Bool { link.canClip }

    /// While the screen tab shows: reads while wanted
    func run() async {
        while !Task.isCancelled {
            if wanted, link.canClip, !touching, writing == 0 {
                let g = link.generation
                if steps.isEmpty || (g != seen && Date().timeIntervalSince(read) > 1.5) {
                    seen = g
                    await load()
                }
            }
            try? await Task.sleep(for: .milliseconds(300))
        }
    }

    /// CLIP 0, then the selected track's steps a page at a time
    func load() async {
        read = Date()
        guard let h = try? await link.exchange(.clip, [0], timeout: 0.5, retries: 1), h.count >= 4 else { return }
        let sel = Int(h[2]), n = Int(h[3])
        guard sel < n, h.count >= 4 + 2 * n else { return }
        let l = max(1, min(64, Int(h[4 + 2 * sel]))), isDrum = h[5 + 2 * sel] != 0
        if sel != track { steps = [] }
        track = sel
        len = l
        drum = isDrum
        guard !isDrum else { steps = []; notes = []; return }
        var got: [Clip.Step] = []
        var first = 0
        while first < l {
            let count = min(Clip.page, l - first)
            guard let a = try? await link.exchange(.clip, [1, UInt8(sel), UInt8(first), UInt8(count)], timeout: 0.8, retries: 1),
                  a.count >= 5, a[1] == 0, Int(a[4]) == count, a.count >= 5 + 15 * count else { return }
            for k in 0..<count { got.append((try? Clip.Step(Array(a[(5 + 15 * k)..<(20 + 15 * k)]))) ?? Clip.Step()) }
            first += count
        }
        guard writing == 0, !touching else { return }   // (an edit came meanwhile: its own steps win)
        steps = got
        notes = Clip.notes(got, len: l)
        problem = nil
    }

    /// The clip becomes `after`: the steps it changes written, here at once and on the FM-1 one by one
    @discardableResult
    func apply(_ after: [Clip.Note]) -> Bool {
        guard !drum, len > 0, steps.count >= len else { return false }
        let new: [Clip.Step]
        do { new = try Clip.steps(after, len: len, old: steps) } catch Clip.EditError.tooMany(let s) {
            problem = "Step \(s + 1) would hold more than 4 notes."
            return false
        } catch {
            problem = "Notes of one pitch cannot overlap."
            return false
        }
        let r = Clip.range(notes, after, len: len)
        let changed = r.filter { new[$0] != steps[$0] }
        var merged = steps                      // (only the edit's steps change: the rest stays as the FM-1 has it)
        for i in changed { merged[i] = new[i] }
        steps = merged
        notes = Clip.notes(merged, len: len)
        problem = nil
        guard !changed.isEmpty else { return true }
        writing += 1
        let t = UInt8(track)
        Task { @MainActor in
            var failed = false
            for i in changed {
                let a = try? await link.exchange(.clip, [2, t, UInt8(i)] + merged[i].bytes, timeout: 0.8, retries: 1)
                if a == nil || a!.count < 2 || a![1] != 0 { failed = true }
            }
            writing -= 1
            if failed {
                problem = "The FM-1 did not take every change: read again."
                await load()
            }
        }
        return true
    }
}
