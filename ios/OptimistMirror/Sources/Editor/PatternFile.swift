// SPDX-License-Identifier: GPL-3.0-only
// A stored pattern's file, as the web editor's librarian writes it: {"format":"optimist-pattern","version":1,"track":k,"bytes":[..]}
// (the record PAT_READ returns, up to 1400 bytes).
import Foundation

enum PatternFile {
    static let format = "optimist-pattern"
    static let version = 1

    static func encode(track: Int, bytes: [UInt8]) throws -> Data {
        let o: [String: Any] = ["format": format, "version": version, "track": track, "bytes": bytes.map { Int($0) }]
        return try JSONSerialization.data(withJSONObject: o, options: [.sortedKeys])
    }

    static func decode(_ data: Data) throws -> (track: Int?, bytes: [UInt8]) {
        guard let o = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { throw WireError("not JSON") }
        guard o["format"] as? String == format, LibraryFile.int(o["version"]) == version, let raw = o["bytes"] as? [Any], raw.count <= Ops.patMax else {
            throw WireError("not an Optimist pattern file")
        }
        var out: [UInt8] = []
        for x in raw {
            guard let n = LibraryFile.int(x), (0...255).contains(n) else { throw WireError("not an Optimist pattern file") }
            out.append(UInt8(n))
        }
        return (LibraryFile.int(o["track"]), out)
    }
}
