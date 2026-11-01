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
    static let denseButton: CGFloat = 38         // iPhone landscape
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
    let action: () -> Void
    @Environment(\.navDense) private var dense

    private var minHeight: CGFloat { compact || dense ? (compact ? NavMetrics.compact : NavMetrics.denseButton) : NavMetrics.button }

    var body: some View {
        Button {
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
            action()
        } label: {
            HStack(spacing: 6) {
                if star { Image(systemName: "star.fill").font(.caption) }
                VStack(spacing: 0) {
                    Text(title)
                        .font(.system(size: compact ? 11 : dense ? 14 : 17, weight: lit ? .bold : .semibold, design: .rounded))
                        .lineLimit(1)
                        .minimumScaleFactor(compact || dense ? 0.5 : 0.6)
                    if let caption {
                        Text(caption)
                            .font(.system(size: dense ? 9 : 11, weight: .medium, design: .rounded))
                            .lineLimit(1)
                            .minimumScaleFactor(0.6)
                            .opacity(0.65)
                    }
                }
            }
            .frame(maxWidth: .infinity, minHeight: minHeight, maxHeight: compact ? NavMetrics.compact : nil)
            .padding(.horizontal, dense ? 1 : compact ? 3 : 8)
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
            .frame(maxWidth: .infinity, minHeight: dense ? 34 : nil, alignment: .leading)
            .padding(.leading, dense && text == "BUTTONS" ? 40 : 0)   // (the layout button sits left of it, the app's Disconnect right of the pages' heading)
            .padding(.trailing, dense && text != "BUTTONS" && text != "FAVORITES" ? 40 : 0)
            .padding(.top, dense ? 0 : 4)
    }
}

/// Left: the rows of the screen shown, the cursor's lit; hold one to keep it as a favourite
struct NavPages: View {
    @ObservedObject var nav: NavModel

    private var heading: String {
        guard let s = nav.state else { return "" }
        if let m = Nav.Mode(rawValue: s.mode), s.mode != Nav.none { return m.title }
        return Nav.Screen(rawValue: s.screen).map { $0.name == $0.title ? $0.name : "\($0.title) · \($0.name)" } ?? ""
    }

    var body: some View {
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
    private enum Item: Hashable { case mode(Nav.Mode), screen(Nav.Screen), patterns }
    private let items: [Item] = [.mode(.fx), .mode(.scl), .mode(.env), .mode(.lfo), .mode(.edit),
                                 .screen(.global), .screen(.system), .screen(.mix), .screen(.project), .mode(.arp),
                                 .screen(.steps), .screen(.song), .patterns]
    private let grid = [GridItem(.flexible(), spacing: NavMetrics.gap), GridItem(.flexible(), spacing: NavMetrics.gap)]

    var body: some View {
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
    @Environment(\.navDense) private var dense

    /// Always room for the lanes' line (the user, 2026-10-24: the screen must not change size between T1..T3 and DR)
    static let height = NavMetrics.button * 2 + NavMetrics.gap
    static let denseHeight = NavMetrics.compact * 2 + 4

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
        let rowH = dense ? NavMetrics.compact : NavMetrics.button
        VStack(spacing: dense ? 4 : NavMetrics.gap) {
            HStack(spacing: dense ? 2 : 4) {
                ForEach(Array(lanes.names.enumerated()), id: \.offset) { k, name in
                    NavButton(title: name, lit: lanes.available && Nav.drumRow + 1 + k == cur, dim: !lanes.available) { nav.pick(track: Nav.drumRow + 1 + k) }
                }
            }
            .frame(height: rowH)
            // (T1 T2 T3 DR and MASTER at the full touch size as before 2026-10-31; only the returns RET A B C are the smaller buttons)
            HStack(spacing: dense ? 2 : 4) {
                ForEach(bar.bottom, id: \.self) { p in
                    NavButton(title: nav.tracks[p], compact: dense || p <= Nav.retRow + 2, lit: p == cur || (p == Nav.drumRow && cur > Nav.drumRow && cur != Nav.none), dim: dim) {
                        nav.pick(track: p)
                    }
                }
            }
            .frame(height: rowH)
        }
        .frame(height: dense ? Self.denseHeight : Self.height, alignment: .bottom)
    }
}
