// SPDX-License-Identifier: GPL-3.0-only
// The app's preset library on disk: one felucca-library file (kind "library"), the same file the Library tab exports, so it opens
// in the web editor as it is. Its parameter layout (keys, engines) is the file's own header.
import Foundation

struct LibraryStore {
    let url: URL

    init(url: URL? = nil) {
        if let url { self.url = url; return }
        let base = (try? FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true))
            ?? FileManager.default.temporaryDirectory
        self.url = base.appendingPathComponent("library.json")
    }

    /// the stored library; nothing stored: an empty one
    func load() throws -> (patches: [Patch], layout: LibraryContext) {
        guard FileManager.default.fileExists(atPath: url.path) else { return ([], LibraryContext(keys: nil, engines: [], firmware: "", pe0: nil)) }
        let data = try Data(contentsOf: url)
        let head = try LibraryFile.decode(data, ctx: LibraryContext(keys: nil, engines: []))
        let ctx = LibraryContext(keys: head.keys, engines: head.engines ?? [], firmware: "", pe0: head.pe0)
        return (head.patches, ctx)
    }

    func save(_ patches: [Patch], layout: LibraryContext) throws {
        try LibraryFile.encode(kind: "library", patches: patches, ctx: layout).write(to: url, options: .atomic)
    }
}
