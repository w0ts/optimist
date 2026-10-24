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
    static let gap: CGFloat = 8
    static let corner: CGFloat = 10
}

/// One touch button: lit (the place the FM-1 is now), dim (nothing to do there now)
struct NavButton: View {
    let title: String
    var lit = false
    var dim = false
    var star = false
    let action: () -> Void

    var body: some View {
        Button {
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
            action()
        } label: {
            HStack(spacing: 6) {
                if star { Image(systemName: "star.fill").font(.caption) }
                Text(title)
                    .font(.system(size: 17, weight: lit ? .bold : .semibold, design: .rounded))
                    .lineLimit(1)
                    .minimumScaleFactor(0.6)
            }
            .frame(maxWidth: .infinity, minHeight: NavMetrics.button)
            .padding(.horizontal, 8)
            .foregroundStyle(lit ? Color.black : dim ? Color.white.opacity(0.35) : Color.white)
            .background(lit ? Color.white : Color.white.opacity(dim ? 0.05 : 0.12),
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
    var body: some View {
        Text(text)
            .font(.caption.weight(.semibold))
            .foregroundStyle(.white.opacity(0.5))
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.top, 4)
    }
}

/// Left: the rows of the screen shown, the cursor's lit; hold one to keep it as a favourite
struct NavPages: View {
    @ObservedObject var nav: NavModel

    private var heading: String {
        guard let s = nav.state else { return "" }
        if let m = Nav.Mode(rawValue: s.mode), s.mode != Nav.none { return m.title }
        return Nav.Screen(rawValue: s.screen)?.title ?? ""
    }

    var body: some View {
        VStack(spacing: NavMetrics.gap) {
            Heading(text: heading)
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(spacing: NavMetrics.gap) {
                        ForEach(Array((nav.state?.rows ?? []).enumerated()), id: \.offset) { r, name in
                            NavButton(title: name.uppercased(), lit: r == nav.state?.row) { nav.pick(row: r) }
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
                    .foregroundStyle(.white.opacity(0.5))
                Spacer()
            }
        }
    }

    @ViewBuilder private func menu(_ r: Int) -> some View {
        Button { nav.addFavorite(row: r, withTrack: false) } label: { Label("Add to Favorites", systemImage: "star") }
        if let s = nav.state, s.track != Nav.none, let t = nav.trackName(s.track) {
            Button { nav.addFavorite(row: r, withTrack: true) } label: { Label("Add to Favorites on \(t)", systemImage: "star.square") }
        }
    }
}

/// Right: the favourites (hold one: move, remove), then every mode and screen
struct NavSide: View {
    @ObservedObject var nav: NavModel
    private let modes: [Nav.Mode] = [.env, .lfo, .fx, .scl, .edit, .arp]
    private let screens: [Nav.Screen] = [.mix, .sound, .steps, .song, .project, .master, .global, .system]
    private let grid = [GridItem(.flexible(), spacing: NavMetrics.gap), GridItem(.flexible(), spacing: NavMetrics.gap)]

    var body: some View {
        ScrollView {
            VStack(spacing: NavMetrics.gap) {
                HStack {
                    Heading(text: "FAVORITES")
                    addMenu
                }
                if nav.favorites.isEmpty {
                    Text("Hold a page on the left to add it.")
                        .font(.footnote)
                        .foregroundStyle(.white.opacity(0.5))
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
                Heading(text: "MODES")
                LazyVGrid(columns: grid, spacing: NavMetrics.gap) {
                    ForEach(modes, id: \.title) { m in
                        NavButton(title: m.title, lit: nav.state?.screen == Nav.Screen.sound.rawValue && nav.state?.mode == m.rawValue) {
                            nav.open(m)
                        }
                    }
                    ForEach(screens, id: \.title) { s in
                        NavButton(title: s.title, lit: nav.state?.screen == s.rawValue && (s != .sound || nav.state?.mode == Nav.none)) {
                            nav.open(s)
                        }
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
                if s.track != Nav.none, let t = nav.trackName(s.track) {
                    Button { nav.addFavorite(row: s.row, withTrack: true) } label: {
                        Label("Add \(s.rows[s.row]) on \(t)", systemImage: "star.square")
                    }
                }
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

/// Under the screen: MASTER T1 T2 T3 DR; the drum lanes as a second line while the drum track or a lane is selected. Dim where
/// ALGORITHM walks nothing (PROJECT, SONG, GLOBAL, SYSTEM)
struct NavTracks: View {
    @ObservedObject var nav: NavModel

    /// Always room for the lanes' line (the user, 2026-10-24: the screen must not change size between T1..T3 and DR)
    static let height = NavMetrics.button * 2 + NavMetrics.gap

    var body: some View {
        let cur = nav.state?.track ?? Nav.none, dim = cur == Nav.none, n = nav.tracks.count
        VStack(spacing: NavMetrics.gap) {
            HStack(spacing: NavMetrics.gap) {
                ForEach(0..<min(n, Nav.drumRow + 1), id: \.self) { p in
                    NavButton(title: nav.tracks[p], lit: p == cur || (p == Nav.drumRow && cur > Nav.drumRow && cur != Nav.none), dim: dim) {
                        nav.pick(track: p)
                    }
                }
            }
            HStack(spacing: 4) {
                if !dim, cur >= Nav.drumRow, n > Nav.drumRow + 1 {
                    ForEach(Nav.drumRow + 1..<n, id: \.self) { p in
                        NavButton(title: nav.tracks[p], lit: p == cur) { nav.pick(track: p) }
                    }
                }
            }
            .frame(height: NavMetrics.button)
        }
        .frame(height: Self.height, alignment: .top)
    }
}
