// SPDX-License-Identifier: GPL-3.0-only
// The touch navigation's state: 109 NAV asked again when the screen changed (at most every 0.2 s, else every 2 s), the
// mixer rows' names when the track changed (a kit names its lanes); the jumps a tap makes, and the favourites.
import Foundation

@MainActor
final class NavModel: ObservableObject {
    @Published private(set) var state: NavState?
    @Published private(set) var tracks: [String] = []
    @Published private(set) var favorites = FavoriteStore.load()

    private let link: MirrorLink
    private var seen = -1
    private var asked = Date.distantPast
    private var tracksFor = -2
    private var busy = false

    init(link: MirrorLink) { self.link = link }

    var on: Bool { link.canNavigate && state != nil }

    /// While the view shows: STATE when the panel was drawn again since the last ask
    func run() async {
        while !Task.isCancelled {
            if link.canNavigate, !busy {
                let g = link.generation
                if g != seen || Date().timeIntervalSince(asked) > 2 {
                    seen = g
                    asked = Date()
                    await ask(.state, [])
                }
            } else if !link.canNavigate {
                state = nil
                tracksFor = -2
            }
            try? await Task.sleep(for: .milliseconds(200))
        }
    }

    @discardableResult
    private func ask(_ op: Nav.Op, _ args: [UInt8]) async -> NavState? {
        guard let a = try? await link.exchange(.nav, [op.rawValue] + args, timeout: 0.5, retries: 0),
              let s = try? NavState.parse(a) else { return nil }
        state = s
        if tracks.isEmpty || (s.track != Nav.none && s.track != tracksFor) {
            tracksFor = s.track
            if let t = try? await link.exchange(.nav, [Nav.Op.tracks.rawValue], timeout: 0.5, retries: 0),
               let names = try? NavState.parseTracks(t) { tracks = names }
        }
        return s
    }

    // MARK: - the jumps

    func pick(row r: Int) { act { _ = await self.ask(.row, [UInt8(r)]) } }
    func pick(track p: Int) { act { _ = await self.ask(.track, [UInt8(p)]) } }
    func open(_ s: Nav.Screen) { act { _ = await self.ask(.open, [UInt8(s.rawValue), UInt8(Nav.none)]) } }
    func open(_ m: Nav.Mode) { act { _ = await self.ask(.open, [UInt8(Nav.Screen.sound.rawValue), UInt8(m.rawValue)]) } }

    /// A favourite: its screen (and family), its track when it keeps one, then its row by name
    func go(_ f: Favorite) {
        act {
            guard var s = await self.ask(.open, [UInt8(f.screen), UInt8(f.mode)]), !s.refused else { return }
            if let t = f.track, s.track != Nav.none, s.track != t {
                guard let n = await self.ask(.track, [UInt8(t)]) else { return }
                s = n
            }
            let r = s.rows.firstIndex(of: f.page)       // (the page gone on this track: the screen and track only)
            if let r, r != s.row { await self.ask(.row, [UInt8(r)]) }
        }
    }

    private func act(_ body: @escaping @MainActor () async -> Void) {
        guard link.canNavigate, !busy else { return }
        busy = true
        Task { @MainActor in
            await body()
            self.seen = self.link.generation
            self.busy = false
        }
    }

    // MARK: - favourites

    func trackName(_ p: Int) -> String? { p >= 0 && p < tracks.count ? tracks[p] : nil }

    /// Row r of the screen shown as a favourite; withTrack: the track it is on too
    func addFavorite(row r: Int, withTrack: Bool) {
        guard let s = state, r < s.rows.count else { return }
        let t = withTrack && s.track != Nav.none ? s.track : nil
        let f = Favorite(screen: s.screen, mode: s.mode, page: s.rows[r], index: r, track: t, trackName: t.flatMap(trackName))
        guard !favorites.contains(where: { $0.screen == f.screen && $0.mode == f.mode && $0.page == f.page && $0.track == f.track })
        else { return }
        favorites.append(f)
        FavoriteStore.save(favorites)
    }

    func remove(_ f: Favorite) {
        favorites.removeAll { $0.id == f.id }
        FavoriteStore.save(favorites)
    }

    func move(_ f: Favorite, by d: Int) {
        guard let i = favorites.firstIndex(of: f), favorites.indices.contains(i + d) else { return }
        favorites.swapAt(i, i + d)
        FavoriteStore.save(favorites)
    }

    /// The favourite is where the FM-1 is now
    func isHere(_ f: Favorite) -> Bool {
        guard let s = state, s.screen == f.screen, s.mode == f.mode, s.row < s.rows.count, s.rows[s.row] == f.page else { return false }
        return f.track == nil || f.track == s.track
    }
}
