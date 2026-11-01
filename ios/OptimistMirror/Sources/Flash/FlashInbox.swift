// SPDX-License-Identifier: GPL-3.0-only
// The packages that came to the app (Finder's double-click and Open With, Files, AirDrop, Mail: "Open in Optimist Mirror"):
// each is checked, then kept in Documents/Firmware (visible in Files: the app shares its Documents), so that it can be
// picked again from the Firmware screen. Nothing is installed by receiving one.
import Foundation

enum FlashInbox {
    struct Problem: Error, CustomStringConvertible { let description: String }

    static let folderName = "Firmware"

    static var folder: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent(folderName, isDirectory: true)
    }

    /// Checks the file at `url` (security-scoped when it came from outside the container) and copies it into `dir`.
    /// Returns the kept copy and the checked package. An invalid file is not copied: the error says why.
    static func receive(_ url: URL, into dir: URL = folder) throws -> (url: URL, package: FlashPackage) {
        guard url.isFileURL else { throw Problem(description: "Only a file can be opened here.") }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let name = url.lastPathComponent
        guard name.lowercased().hasSuffix(".fwsc") else { throw Problem(description: "\(name) is not a .fwsc firmware package.") }
        let data: Data
        do { data = try Data(contentsOf: url) } catch { throw Problem(description: "\(name) could not be read: \(error.localizedDescription)") }
        let package = try FlashPackage.check([UInt8](data), title: name, file: name)
        let fm = FileManager.default
        try fm.createDirectory(at: dir, withIntermediateDirectories: true)
        let kept = try keep(data, named: name, in: dir)
        return (kept, package)
    }

    /// The same bytes under the same name are one file; other bytes under a taken name get " 2", " 3"…
    private static func keep(_ data: Data, named name: String, in dir: URL) throws -> URL {
        let fm = FileManager.default
        let stem = (name as NSString).deletingPathExtension
        var n = 1
        while true {
            let candidate = dir.appendingPathComponent(n == 1 ? name : "\(stem) \(n).fwsc")
            if !fm.fileExists(atPath: candidate.path) {
                try data.write(to: candidate, options: .atomic)
                return candidate
            }
            if (try? Data(contentsOf: candidate)) == data { return candidate }
            n += 1
        }
    }

    /// The kept packages, newest first.
    static func list(in dir: URL = folder) -> [URL] {
        let keys: [URLResourceKey] = [.contentModificationDateKey]
        let files = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: keys)) ?? []
        func date(_ u: URL) -> Date { (try? u.resourceValues(forKeys: Set(keys)).contentModificationDate) ?? .distantPast }
        return files.filter { $0.pathExtension.lowercased() == "fwsc" }.sorted { date($0) > date($1) }
    }
}

extension FlashPackage {
    /// What the Firmware screen says of a package picked: its file, identity, size and kind (the route to the FM-1 is
    /// chosen at Install, from what answers on the cable).
    var summary: String {
        let kind = InstallRoute.packageKind(product: product, sha256: sha256, flashLen: InstallRoute.flashLen(image)).kind
        let size = ByteCountFormatter.string(fromByteCount: Int64(image.count), countStyle: .file)
        return "\(file.isEmpty ? title : file) · \(product) (\(kind)) · \(size) · sha256 \(sha256.prefix(12))"
    }
}
