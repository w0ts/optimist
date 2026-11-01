// SPDX-License-Identifier: GPL-3.0-only
// The project library: the projects kept on the phone / tablet / Mac, one .fm1proj (ProjectFile, FM1P v1) per project in the app's
// Documents/Projects folder (shown in the Files app). The web editor keeps the same library in the browser (editor.html, IndexedDB).
import Foundation

struct ProjectLibrary {
    struct Entry: Identifiable, Equatable {
        let file: String        // the file's name in the folder: the entry's identity
        let name: String        // the project's name inside the file
        let date: Date
        let size: Int
        var id: String { file }
    }

    let dir: URL

    /// Documents/Projects (iCloud Drive is not used: the app has no iCloud entitlement)
    static var standard: ProjectLibrary {
        ProjectLibrary(dir: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Projects", isDirectory: true))
    }

    private func ensure() throws { try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true) }

    /// newest first; files that are not valid projects are left out
    func list() -> [Entry] {
        let keys: [URLResourceKey] = [.contentModificationDateKey, .fileSizeKey]
        let urls = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: keys)) ?? []
        return urls.filter { $0.pathExtension == ProjectFile.ext }.compactMap { u -> Entry? in
            guard let d = try? Data(contentsOf: u), let c = try? ProjectFile.decode(Array(d)) else { return nil }
            let v = try? u.resourceValues(forKeys: Set(keys))
            return Entry(file: u.lastPathComponent, name: c.name, date: v?.contentModificationDate ?? .distantPast, size: v?.fileSize ?? d.count)
        }.sorted { $0.date > $1.date }
    }

    private func freeName(_ base: String) -> String {
        var n = 1
        var f = "\(base).\(ProjectFile.ext)"
        while FileManager.default.fileExists(atPath: dir.appendingPathComponent(f).path) { n += 1; f = "\(base)-\(n).\(ProjectFile.ext)" }
        return f
    }

    /// a copy of the project in the library (never replaces another file)
    @discardableResult func add(_ c: ProjectFile.Contents) throws -> Entry {
        try ensure()
        let file = freeName(FileIO.slug(c.name.isEmpty ? "project" : c.name))
        try Data(ProjectFile.encode(slot: c.slot, family: c.family, name: c.name, data: c.data)).write(to: dir.appendingPathComponent(file), options: .atomic)
        return list().first { $0.file == file } ?? Entry(file: file, name: c.name, date: Date(), size: c.data.count)
    }

    /// a file the user picked (checked: a damaged or foreign file is refused)
    @discardableResult func add(file url: URL) throws -> Entry { try add(try ProjectFile.decode(Array(try FileIO.read(url)))) }

    func read(_ e: Entry) throws -> ProjectFile.Contents {
        try ProjectFile.decode(Array(try Data(contentsOf: dir.appendingPathComponent(e.file))))
    }

    func url(_ e: Entry) -> URL { dir.appendingPathComponent(e.file) }

    func rename(_ e: Entry, to name: String) throws {
        var c = try read(e)
        c.name = Ops.projName(name)
        let old = dir.appendingPathComponent(e.file)
        try Data(ProjectFile.encode(slot: c.slot, family: c.family, name: c.name, data: c.data)).write(to: old, options: .atomic)
        let new = freeName(FileIO.slug(c.name.isEmpty ? "project" : c.name))
        if new != e.file { try FileManager.default.moveItem(at: old, to: dir.appendingPathComponent(new)) }
    }

    func delete(_ e: Entry) throws { try FileManager.default.removeItem(at: dir.appendingPathComponent(e.file)) }
}

/// "Swap with slot N": the slot's project goes into the library first, and only when that worked is the chosen one written over it
/// (a failed save must never lose the slot's project).
enum ProjectSwap {
    static func run(saveSlot: () async throws -> Void, load: () async throws -> Void) async throws {
        try await saveSlot()
        try await load()
    }
}
