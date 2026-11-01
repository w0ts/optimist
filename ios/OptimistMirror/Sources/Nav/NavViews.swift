// SPDX-License-Identifier: GPL-3.0-only
// The touch navigation beside the screen (the user, 2026-10-24: "left and right i had buttons allowing me to access all the sub
// pages of the current mode directly", "add specific pages to favorites", "and select the track and drum lanes", "basically just
// speeding up navigation", "touch will make the difference"): left the rows of the screen shown (on SOUND the family a page
// button narrowed it to), right the favourites then the modes, under the screen the tracks, and the drum lanes while the drum
// track is selected. Touch-sized: no button under 52 points high. A press and hold on a page or a favourite: its menu.
import SwiftUI
import UIKit

enum NavMetrics {
    static let button: CGFloat = 52
    static let compact: CGFloat = 30             // the track bar's smaller buttons, all one height
    static let denseMin: CGFloat = 20          // iPhone landscape: the smallest row before a stack scrolls
    static let gap: CGFloat = 8
    static let corner: CGFloat = 10
}

private struct NavDenseKey: EnvironmentKey { static let defaultValue = false }
extension EnvironmentValues {
    /// iPhone landscape (compact height): the touch buttons shrink so the columns, the screen and the track bar share ~390 points
    var navDense: Bool { get { self[NavDenseKey.self] } set { self[NavDenseKey.self] = newValue } }
}

/// One touch button: lit (the place the FM-1 is now), dim (nothing to do there now)
struct NavButton: View {
    let title: String
    var caption: String? = nil            // under the title, small: a screen's own name under its button's
    var compact = false                   // the track bar's small button
    var lit = false
    var dim = false
    var star = false
    var height: CGFloat? = nil            // iPhone landscape: the stack divides its height among its rows, font and caption follow
    let action: () -> Void

    private var minHeight: CGFloat { height ?? (compact ? NavMetrics.compact : NavMetrics.button) }
    private var titleSize: CGFloat { height.map { min(17, max(9, $0 * 0.42)) } ?? (compact ? 11 : 17) }

    var body: some View {
        Button {
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
            action()
        } label: {
            HStack(spacing: 6) {
                if star { Image(systemName: "star.fill").font(.caption) }
                VStack(spacing: 0) {
                    Text(title)
                        .font(.system(size: titleSize, weight: lit ? .bold : .semibold, design: .rounded))
                        .lineLimit(1)
                        .minimumScaleFactor(height != nil || compact ? 0.5 : 0.6)
                    if let caption, height.map({ $0 >= 34 }) ?? true {
                        Text(caption)
                            .font(.system(size: height.map { min(11, $0 * 0.26) } ?? 11, weight: .medium, design: .rounded))
                            .lineLimit(1)
                            .minimumScaleFactor(0.6)
                            .opacity(0.65)
                    }
                }
            }
            .frame(maxWidth: .infinity, minHeight: minHeight, maxHeight: height ?? (compact ? NavMetrics.compact : nil))
            .padding(.horizontal, height != nil ? 2 : compact ? 3 : 8)
            .foregroundStyle(lit ? Color(.systemBackground) : dim ? Color.primary.opacity(0.35) : Color.primary)   // (the system's theme)
            .background(lit ? Color.primary : Color.primary.opacity(dim ? 0.05 : 0.12),
                        in: RoundedRectangle(cornerRadius: NavMetrics.corner, style: .continuous))
            .contentShape(RoundedRectangle(cornerRadius: NavMetrics.corner, style: .continuous))
        }
        .buttonStyle(.plain)
        .focusable(false)                // (the Mac's keyboard stays the panel's: MirrorView)
        .disabled(dim)
    }
}

private struct Heading: View {
    let text: String
    @Environment(\.navDense) private var dense
    var body: some View {
        Text(text)
            .font(.caption.weight(.semibold))
            .foregroundStyle(.secondary)
            .frame(maxWidth: .infinity, minHeight: dense ? 34 : nil, maxHeight: dense ? 34 : nil, alignment: .leading)
            .padding(.leading, dense && text == "BUTTONS" ? 40 : 0)   // (the layout button sits left of it, the app's Disconnect right of the pages' heading)
            .padding(.trailing, dense && text != "BUTTONS" && text != "FAVORITES" ? 40 : 0)
            .padding(.top, dense ? 0 : 4)
    }
}

/// Left: the rows of the screen shown, the cursor's lit; hold one to keep it as a favourite
struct NavPages: View {
    @ObservedObject var nav: NavModel
    @Environment(\.navDense) private var dense
    /// iPhone landscape: the height left for the rows (the heading's and the tracks' taken off); the rows share it, no scrolling
    var room: CGFloat = 0

    private var heading: String {
        guard let s = nav.state else { return "" }
        if let m = Nav.Mode(rawValue: s.mode), s.mode != Nav.none { return m.title }
        return Nav.Screen(rawValue: s.screen).map { $0.name == $0.title ? $0.name : "\($0.title) · \($0.name)" } ?? ""
    }

    private var denseBody: some View {
        let rows = nav.state?.rows ?? []
        let fit = (room - 34 - 4 * CGFloat(rows.count)) / CGFloat(max(rows.count, 1))
        let h = max(NavMetrics.denseMin, min(38, fit))
        let list = ForEach(Array(rows.enumerated()), id: \.offset) { r, name in
            NavButton(title: name, lit: r == nav.state?.row, height: h) { nav.pick(row: r) }
                .contextMenu { menu(r) }
        }
        return VStack(spacing: 4) {
            Heading(text: heading)
            if fit < NavMetrics.denseMin {          // (more rows than fit even at the smallest size: only then they scroll)
                ScrollView { VStack(spacing: 4) { list } }.scrollIndicators(.hidden)
            } else {
                list
                Spacer(minLength: 0)
            }
        }
    }

    var body: some View {
        if dense { denseBody } else { scrolling }
    }

    private var scrolling: some View {
        VStack(spacing: NavMetrics.gap) {
            Heading(text: heading)
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(spacing: NavMetrics.gap) {
                        ForEach(Array((nav.state?.rows ?? []).enumerated()), id: \.offset) { r, name in
                            NavButton(title: name, lit: r == nav.state?.row) { nav.pick(row: r) }
                                .id(r)
                                .contextMenu { menu(r) }
                        }
                    }
                }
                .scrollIndicators(.hidden)
                .onChange(of: nav.state?.row) { _, r in
                    if let r { withAnimation { proxy.scrollTo(r, anchor: .center) } }
                }
            }
            if nav.state?.rows.isEmpty ?? true {
                Text("No pages to list here: use the panel's knobs.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                Spacer()
            }
        }
    }

    @ViewBuilder private func menu(_ r: Int) -> some View {
        Button { nav.addFavorite(row: r, withTrack: false) } label: { Label("Add to Favorites", systemImage: "star") }
        let t = nav.state.flatMap { $0.track != Nav.none ? nav.trackName($0.track) : nil }
        Button { nav.addFavorite(row: r, withTrack: true) } label: { Label(t.map { "Add to Favorites on \($0)" } ?? "Add to Favorites on a track", systemImage: "star.square") }
            .disabled(t == nil)
    }
}

/// Left: every mode and screen, in the panel's order (FX SCL ENV LFO EDIT GLOBAL HOME SAVE ARP SEQ: the user, 2026-10-24: "in
/// the order they are on the device"), each screen after the button that opens it (left out, the user, 2026-10-24: SOUND's whole list, which no button
/// opens; the master FX, which is FX with MASTER selected); then the
/// favourites under them (hold one: move, remove) (the user, 2026-10-24: "favorites below")
struct NavSide: View {
    @ObservedObject var nav: NavModel
    @Environment(\.navDense) private var dense
    var height: CGFloat = 0           // iPhone landscape: the column's height, shared by its rows (no scrolling)
    private enum Item: Hashable { case mode(Nav.Mode), screen(Nav.Screen), patterns }
    private let items: [Item] = [.mode(.fx), .mode(.scl), .mode(.env), .mode(.lfo), .mode(.edit),
                                 .screen(.global), .screen(.system), .screen(.mix), .screen(.project), .mode(.arp),
                                 .screen(.steps), .screen(.song), .patterns]
    private let grid = [GridItem(.flexible(), spacing: NavMetrics.gap), GridItem(.flexible(), spacing: NavMetrics.gap)]

    @ViewBuilder private func button(_ item: Item, height h: CGFloat?) -> some View {
        switch item {
        case .mode(let m):
            let lit = nav.state?.screen == Nav.Screen.sound.rawValue && nav.state?.mode == m.rawValue
                || (m == .fx && nav.state?.screen == Nav.Screen.master.rawValue)
            NavButton(title: m.title, lit: lit, height: h) { lit ? nav.again(m) : nav.open(m) }
        case .patterns:
            let lit = nav.state?.screen == Nav.Screen.song.rawValue && nav.state?.row == Nav.songPatterns
            NavButton(title: "PATTERN", lit: lit, height: h) { if !lit { nav.openPatterns() } }
        case .screen(let s):
            let lit = nav.state?.screen == s.rawValue && (s != .sound || nav.state?.mode == Nav.none)
                && (s != .song || nav.state?.row != Nav.songPatterns)
            NavButton(title: s.title, caption: s.name == s.title ? nil : s.name, lit: lit, height: h) { lit ? nav.again(s) : nav.open(s) }
        }
    }

    /// Every button and favourite in view at once: the rows share the column's height
    private var denseBody: some View {
        let pairs = stride(from: 0, to: items.count, by: 2).map { Array(items[$0..<min($0 + 2, items.count)]) }
        let count = CGFloat(pairs.count + nav.favorites.count)
        let fit = (height - 34 - 4 * count) / count
        let h = max(NavMetrics.denseMin, min(38, fit))
        let rows = Group {
            ForEach(pairs, id: \.self) { pair in
                HStack(spacing: 4) { ForEach(pair, id: \.self) { button($0, height: h) } }
            }
            ForEach(nav.favorites) { f in
                NavButton(title: f.label, lit: nav.isHere(f), star: true, height: h) { nav.go(f) }
                    .contextMenu {
                        Button { nav.move(f, by: -1) } label: { Label("Move Up", systemImage: "arrow.up") }
                        Button { nav.move(f, by: 1) } label: { Label("Move Down", systemImage: "arrow.down") }
                        Button(role: .destructive) { nav.remove(f) } label: { Label("Remove", systemImage: "trash") }
                    }
            }
        }
        return VStack(spacing: 4) {
            HStack(spacing: 0) {
                Heading(text: "BUTTONS")
                addMenu.frame(width: 40).scaleEffect(0.8)
            }
            if fit < NavMetrics.denseMin {          // (only with more favourites than fit even at the smallest size)
                ScrollView { VStack(spacing: 4) { rows } }.scrollIndicators(.hidden)
            } else {
                rows
                Spacer(minLength: 0)
            }
        }
    }

    var body: some View {
        if dense { denseBody } else { scrolling }
    }

    private var scrolling: some View {
        ScrollView {
            VStack(spacing: NavMetrics.gap) {
                Heading(text: "BUTTONS")
                LazyVGrid(columns: grid, spacing: NavMetrics.gap) {
                    ForEach(items, id: \.self) { item in
                        switch item {
                        case .mode(let m):
                            let lit = nav.state?.screen == Nav.Screen.sound.rawValue && nav.state?.mode == m.rawValue
                                || (m == .fx && nav.state?.screen == Nav.Screen.master.rawValue)   // (FX on MASTER: the master FX)
                            NavButton(title: m.title, lit: lit) { lit ? nav.again(m) : nav.open(m) }   // (lit: its next page, as the FM-1)
                        case .patterns:                 // (SONG's PATTERNS row, its own button: the user, 2026-10-24)
                            let lit = nav.state?.screen == Nav.Screen.song.rawValue && nav.state?.row == Nav.songPatterns
                            NavButton(title: "PATTERN", lit: lit) { if !lit { nav.openPatterns() } }
                        case .screen(let s):
                            let lit = nav.state?.screen == s.rawValue && (s != .sound || nav.state?.mode == Nav.none)
                                && (s != .song || nav.state?.row != Nav.songPatterns)
                            NavButton(title: s.title, caption: s.name == s.title ? nil : s.name, lit: lit) { lit ? nav.again(s) : nav.open(s) }
                        }
                    }
                }
                HStack {
                    Heading(text: "FAVORITES")
                    addMenu
                }
                if nav.favorites.isEmpty {
                    Text("Hold a page on the right to add it.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                ForEach(nav.favorites) { f in
                    NavButton(title: f.label, lit: nav.isHere(f), star: true) { nav.go(f) }
                        .contextMenu {
                            Button { nav.move(f, by: -1) } label: { Label("Move Up", systemImage: "arrow.up") }
                            Button { nav.move(f, by: 1) } label: { Label("Move Down", systemImage: "arrow.down") }
                            Button(role: .destructive) { nav.remove(f) } label: { Label("Remove", systemImage: "trash") }
                        }
                }
            }
        }
        .scrollIndicators(.hidden)
    }

    /// The page shown now as a favourite, alone or on its track
    private var addMenu: some View {
        Menu {
            if let s = nav.state, s.row < s.rows.count {
                Button { nav.addFavorite(row: s.row, withTrack: false) } label: { Label("Add \(s.rows[s.row])", systemImage: "star") }
                let t = s.track != Nav.none ? nav.trackName(s.track) : nil
                Button { nav.addFavorite(row: s.row, withTrack: true) } label: {
                    Label(t.map { "Add \(s.rows[s.row]) on \($0)" } ?? "Add \(s.rows[s.row]) on a track", systemImage: "star.square")
                }
                .disabled(t == nil)
            }
        } label: {
            Image(systemName: "star.badge.plus")
                .font(.title3)
                .frame(width: NavMetrics.button, height: 36)
                .foregroundStyle(.white)
        }
        .disabled(nav.state.map { $0.row >= $0.rows.count } ?? true)
    }
}

/// Under the screen: the drum lanes' line (its room always kept) over MASTER T1 T2 T3 DR (the user, 2026-10-24: "put the drum
/// buttons above the track buttons"); the lanes show while the drum track or a lane is selected. Dim where
/// ALGORITHM walks nothing (PROJECT, SONG, GLOBAL, SYSTEM)
struct NavTracks: View {
    @ObservedObject var nav: NavModel

    /// Always room for the lanes' line (the user, 2026-10-24: the screen must not change size between T1..T3 and DR)
    static let height = NavMetrics.button * 2 + NavMetrics.gap

    /// The lanes' buttons: always drawn (the kit's names, else the default lanes'), available only while the drum track or a lane
    /// is the selection (the user, 2026-10-29: "the buttons that are not available should be greyed out")
    static func lanes(tracks: [String], current: Int) -> (names: [String], available: Bool) {
        let kit = tracks.count > Nav.drumRow + 1
        return (kit ? Array(tracks[(Nav.drumRow + 1)...]) : Nav.defaultLanes, kit && current != Nav.none && current >= Nav.drumRow)
    }

    var body: some View {
        let cur = nav.state?.track ?? Nav.none, dim = cur == Nav.none
        let lanes = Self.lanes(tracks: nav.tracks, current: cur)
        let bar = Nav.bar(count: nav.tracks.count)
        VStack(spacing: NavMetrics.gap) {
            HStack(spacing: 4) {
                ForEach(Array(lanes.names.enumerated()), id: \.offset) { k, name in
                    NavButton(title: name, lit: lanes.available && Nav.drumRow + 1 + k == cur, dim: !lanes.available) { nav.pick(track: Nav.drumRow + 1 + k) }
                }
            }
            .frame(height: NavMetrics.button)
            // (T1 T2 T3 DR and MASTER at the full touch size as before 2026-10-31; only the returns RET A B C are the smaller buttons)
            HStack(spacing: 4) {
                ForEach(bar.bottom, id: \.self) { p in
                    NavButton(title: nav.tracks[p], compact: p <= Nav.retRow + 2, lit: p == cur || (p == Nav.drumRow && cur > Nav.drumRow && cur != Nav.none), dim: dim) {
                        nav.pick(track: p)
                    }
                }
            }
            .frame(height: NavMetrics.button)
        }
        .frame(height: Self.height, alignment: .bottom)
    }
}

/// iPhone landscape: the tracks as a block under the pages, in the column right of the screen (the screen keeps the whole height):
/// the drum lanes only while the drum track or a lane is selected (3 across, names readable), the tracks 4 across
struct NavTracksDense: View {
    @ObservedObject var nav: NavModel
    private func grid(_ n: Int) -> [GridItem] { Array(repeating: GridItem(.flexible(), spacing: 3), count: n) }
    private static let row: CGFloat = 24
    private static let lanesAcross = 4, tracksAcross = 4

    /// Its height now, for the pages above it to share what is left
    @MainActor static func height(_ nav: NavModel) -> CGFloat {
        let cur = nav.state?.track ?? Nav.none
        let lanes = NavTracks.lanes(tracks: nav.tracks, current: cur)
        let bar = Nav.bar(count: nav.tracks.count)
        func rows(_ n: Int, _ across: Int) -> CGFloat { CGFloat((n + across - 1) / across) }
        let r = rows(bar.bottom.count, tracksAcross) + (lanes.available ? rows(lanes.names.count, lanesAcross) : 0)
        return r * row + (r - 1) * 3 + (lanes.available ? 3 : 0)
    }

    var body: some View {
        let cur = nav.state?.track ?? Nav.none, dim = cur == Nav.none
        let lanes = NavTracks.lanes(tracks: nav.tracks, current: cur)
        let bar = Nav.bar(count: nav.tracks.count)
        VStack(spacing: 3) {
            if lanes.available {
                LazyVGrid(columns: grid(Self.lanesAcross), spacing: 3) {
                    ForEach(Array(lanes.names.enumerated()), id: \.offset) { k, name in
                        NavButton(title: name, compact: true, lit: Nav.drumRow + 1 + k == cur, height: Self.row) { nav.pick(track: Nav.drumRow + 1 + k) }
                    }
                }
            }
            LazyVGrid(columns: grid(Self.tracksAcross), spacing: 3) {
                ForEach(bar.bottom, id: \.self) { p in
                    NavButton(title: nav.tracks[p], compact: true, lit: p == cur || (p == Nav.drumRow && cur > Nav.drumRow && cur != Nav.none), dim: dim, height: Self.row) {
                        nav.pick(track: p)
                    }
                }
            }
        }
    }
}
