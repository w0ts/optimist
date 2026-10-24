// SPDX-License-Identifier: GPL-3.0-only
// The clip editor in place of the mirrored screen while the FM-1 shows STEPS on a synth track (the user, 2026-10-24: "editing of
// the clip, by touch. Add note, remove note, move note. Stretch note", "scroll up and down and page left right", "synths
// only"). 16 steps a page, the notes' rows scrolled by a vertical drag. A tap on an empty cell adds a note one step long, a tap
// on a note removes it, a drag of a note moves it (steps and semitones), a drag of its right end stretches it (quarter steps).
// A horizontal swipe on empty cells turns the page.
import SwiftUI
import UIKit

struct ClipView: View {
    @ObservedObject var clip: ClipModel
    @State private var page = 0
    @State private var top = 76                       // the highest pitch shown (E5)
    @State private var placed = -1                    // the track `top` was set for
    @State private var grab: Grab?

    private enum Kind { case pending, pan(vertical: Bool, top: Int), move, stretch }
    private struct Grab {
        var kind: Kind
        var note: Clip.Note?                          // the note under the finger at the start
        var preview: Clip.Note?                       // where it goes
    }

    private static let keyW: CGFloat = 40
    private var pages: Int { max(1, (clip.len + Clip.page - 1) / Clip.page) }

    var body: some View {
        VStack(spacing: 6) {
            header
            GeometryReader { g in roll(g.size) }
                .background(Color(white: 0.06), in: RoundedRectangle(cornerRadius: 8))
                .clipShape(RoundedRectangle(cornerRadius: 8))
        }
        .padding(8)
        .onChange(of: clip.len) { _, _ in page = min(page, pages - 1) }
        .onChange(of: clip.notes) { _, n in place(n) }
    }

    private var header: some View {
        HStack(spacing: 8) {
            pageButton("chevron.left", -1)
            Text(clip.len > 0 ? "Steps \(page * Clip.page + 1)–\(min(clip.len, (page + 1) * Clip.page)) of \(clip.len)" : "Reading the clip…")
                .font(.system(.callout, design: .rounded).weight(.semibold))
                .monospacedDigit()
                .foregroundStyle(.white)
            pageButton("chevron.right", 1)
            Spacer(minLength: 4)
            if let p = clip.problem {
                Text(p).font(.footnote).foregroundStyle(.orange).lineLimit(2).minimumScaleFactor(0.7)
            }
        }
    }

    private func pageButton(_ icon: String, _ d: Int) -> some View {
        Button { page = max(0, min(pages - 1, page + d)) } label: {
            Image(systemName: icon)
                .font(.body.weight(.semibold))
                .frame(width: 44, height: 36)
                .background(Color.white.opacity(0.12), in: RoundedRectangle(cornerRadius: 8))
        }
        .buttonStyle(.plain)
        .foregroundStyle(.white)
        .disabled(d < 0 ? page == 0 : page >= pages - 1)
        .accessibilityLabel(d < 0 ? "Previous Page" : "Next Page")
    }

    /// The highest pitch shown: above the track's highest note, once a track (C4 + 16 with none)
    private func place(_ n: [Clip.Note]) {
        guard clip.track != placed, clip.len > 0 else { return }
        placed = clip.track
        if let hi = n.map({ Int($0.pitch) }).max() { top = min(127, hi + 4) } else { top = 76 }
    }

    // MARK: - the roll

    private struct Geo {
        let size: CGSize, rowH: CGFloat, colW: CGFloat, rows: Int, first: Int, top: Int
        func x(_ tick: Int) -> CGFloat { ClipView.keyW + CGFloat(tick - first * Clip.tick) / CGFloat(Clip.tick) * colW }
        func y(_ pitch: Int) -> CGFloat { CGFloat(top - pitch) * rowH }
        func step(_ p: CGPoint) -> Int { first + Int(floor((p.x - ClipView.keyW) / colW)) }
        func pitch(_ p: CGPoint) -> Int { top - Int(floor(p.y / rowH)) }
        func rect(_ n: Clip.Note) -> CGRect {
            CGRect(x: x(n.start * Clip.tick), y: y(Int(n.pitch)) + 1, width: max(6, x(n.end) - x(n.start * Clip.tick) - 1), height: rowH - 2)
        }
    }

    private func geo(_ size: CGSize) -> Geo {
        let rowH = max(22, min(34, size.height / 24))
        return Geo(size: size, rowH: rowH, colW: (size.width - Self.keyW) / CGFloat(Clip.page), rows: Int(size.height / rowH) + 1,
                   first: page * Clip.page, top: top)
    }

    private func roll(_ size: CGSize) -> some View {
        let g = geo(size)
        return Canvas { ctx, _ in draw(ctx, g) }
            .contentShape(Rectangle())
            .gesture(touch(g))
            .accessibilityLabel("Piano roll, \(clip.notes.count) notes")
    }

    private func draw(_ ctx: GraphicsContext, _ g: Geo) {
        let black: Set<Int> = [1, 3, 6, 8, 10]
        for r in 0..<g.rows {
            let p = g.top - r
            guard p >= 0 else { break }
            let y = g.y(p)
            if black.contains(p % 12) {
                ctx.fill(Path(CGRect(x: 0, y: y, width: g.size.width, height: g.rowH)), with: .color(.white.opacity(0.04)))
            }
            if p % 12 == 0 {
                ctx.fill(Path(CGRect(x: 0, y: y + g.rowH - 1, width: g.size.width, height: 1)), with: .color(.white.opacity(0.18)))
            }
            ctx.draw(Text(Clip.name(p)).font(.system(size: 11, weight: p % 12 == 0 ? .bold : .regular, design: .rounded))
                        .foregroundColor(.white.opacity(p % 12 == 0 ? 0.9 : 0.45)),
                     at: CGPoint(x: Self.keyW / 2, y: y + g.rowH / 2))
        }
        for c in 0...Clip.page {
            let s = g.first + c
            let x = Self.keyW + CGFloat(c) * g.colW
            ctx.fill(Path(CGRect(x: x, y: 0, width: 1, height: g.size.height)), with: .color(.white.opacity(s % 4 == 0 ? 0.22 : 0.08)))
            if c < Clip.page && s >= clip.len {
                ctx.fill(Path(CGRect(x: x, y: 0, width: g.colW, height: g.size.height)), with: .color(.black.opacity(0.55)))
            }
        }
        let moving = grab?.note
        for n in clip.notes where n != moving {
            note(ctx, g, n, lit: false)
        }
        if let m = moving {
            note(ctx, g, grab?.preview ?? m, lit: true)
        }
    }

    private func note(_ ctx: GraphicsContext, _ g: Geo, _ n: Clip.Note, lit: Bool) {
        let r = g.rect(n)
        guard r.maxX > Self.keyW, r.minX < g.size.width, r.maxY > 0, r.minY < g.size.height else { return }
        let shape = Path(roundedRect: r, cornerRadius: 4)
        let c: Color = lit ? .white : n.flags & Clip.accent != 0 ? .orange : Color(red: 0.55, green: 0.8, blue: 1)
        ctx.fill(shape, with: .color(c.opacity(lit ? 0.95 : 0.85)))
        ctx.fill(Path(CGRect(x: r.maxX - 4, y: r.minY + 4, width: 2, height: max(0, r.height - 8))), with: .color(.black.opacity(0.35)))
    }

    // MARK: - touch

    /// The note at p (the last drawn wins), and whether p is on its right end (a stretch)
    private func hit(_ p: CGPoint, _ g: Geo) -> (Clip.Note, Bool)? {
        for n in clip.notes.reversed() {
            let r = g.rect(n)
            if r.insetBy(dx: 0, dy: -2).contains(p) {
                let edge = max(14, min(28, r.width * 0.3))
                return (n, p.x >= r.maxX - edge && r.width >= 18)
            }
        }
        return nil
    }

    private func touch(_ g: Geo) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { v in changed(v, g) }
            .onEnded { v in ended(v, g) }
    }

    private func changed(_ v: DragGesture.Value, _ g: Geo) {
        if grab == nil {
            clip.touching = true
            if let (n, edge) = hit(v.startLocation, g) {
                grab = Grab(kind: edge ? .stretch : .pending, note: n, preview: n)
            } else {
                grab = Grab(kind: .pending, note: nil, preview: nil)
            }
        }
        guard var k = grab else { return }
        let dx = v.translation.width, dy = v.translation.height
        if case .pending = k.kind, hypot(dx, dy) > 8 {
            k.kind = k.note != nil ? .move : .pan(vertical: abs(dy) >= abs(dx), top: top)
            if k.note != nil { UIImpactFeedbackGenerator(style: .light).impactOccurred() }
        }
        switch k.kind {
        case .move:
            guard let n = k.note else { break }
            var m = n
            m.start = max(0, min(clip.len - 1, n.start + Int((dx / g.colW).rounded())))
            m.pitch = UInt8(max(0, min(127, Int(n.pitch) - Int((dy / g.rowH).rounded()))))
            m.length = min(n.length, (clip.len - m.start) * Clip.tick)
            k.preview = m
        case .stretch:
            guard let n = k.note else { break }
            let q = Clip.tick / 4
            let end = Int(((v.location.x - Self.keyW) / g.colW * CGFloat(Clip.tick) / CGFloat(q)).rounded()) * q + g.first * Clip.tick
            var m = n
            m.length = max(q, min(clip.len * Clip.tick, end) - n.start * Clip.tick)
            k.preview = m
        case .pan(let vertical, let t0):
            if vertical { top = max(11, min(127, t0 + Int((dy / g.rowH).rounded()))) }
        case .pending:
            break
        }
        grab = k
    }

    private func ended(_ v: DragGesture.Value, _ g: Geo) {
        defer { grab = nil; clip.touching = false }
        guard let k = grab else { return }
        switch k.kind {
        case .pending:
            if let n = k.note {
                edit(clip.notes.filter { $0 != n })
            } else {
                let s = g.step(v.location), p = g.pitch(v.location)
                guard s >= 0, s < clip.len, (0...127).contains(p), v.location.x > Self.keyW else { return }
                edit(clip.notes + [Clip.Note(start: s, pitch: UInt8(p), length: Clip.tick)])
            }
        case .move, .stretch:
            guard let n = k.note, let m = k.preview, m != n else { return }
            edit(clip.notes.map { $0 == n ? m : $0 })
        case .pan(let vertical, _):
            if !vertical, abs(v.translation.width) > 60 {
                page = max(0, min(pages - 1, page + (v.translation.width < 0 ? 1 : -1)))
            }
        }
    }

    private func edit(_ after: [Clip.Note]) {
        if clip.apply(after) {
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
        } else {
            UINotificationFeedbackGenerator().notificationOccurred(.warning)
        }
    }
}
