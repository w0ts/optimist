// SPDX-License-Identifier: GPL-3.0-only
// A project as a file (.fm1proj; the web editor's web/sim/bk.js packProject / editor.html pjfPack write and read the same bytes):
// "FM1P", format version u8 (1), slot u8 (0-based, where it came from), the firmware family (4 ASCII: BK_LIST's magic, "FUNB"),
// the name (12 ASCII, 0-padded), the stream's length u32 LE, the stream (the PJnn object as the device gives it), the CRC-32 of
// all of it before, u32 LE.
import Foundation

enum ProjectFile {
    static let magic = "FM1P"
    static let version: UInt8 = 1
    static let head = 26
    static let ext = "fm1proj"

    struct Contents: Equatable {
        var slot: Int
        var family: String
        var name: String
        var data: [UInt8]
    }

    static func tag(_ p: Int) -> String { "PJ" + String(format: "%02d", p + 1) }

    private static func ascii(_ s: String, _ n: Int) -> [UInt8] {
        Array((s.unicodeScalars.map { UInt8(truncatingIfNeeded: $0.value & 0x7F) } + [UInt8](repeating: 0, count: n)).prefix(n))
    }

    private static func le32(_ v: Int) -> [UInt8] { (0..<4).map { UInt8((v >> (8 * $0)) & 0xFF) } }

    static func encode(slot: Int, family: String, name: String, data: [UInt8]) -> [UInt8] {
        var out = ascii(magic, 4) + [version, UInt8(slot & 0x7F)] + ascii(family, 4) + ascii(Ops.projName(name), 12) + le32(data.count) + data
        out += le32(Int(Wire.crc32(out)))
        return out
    }

    static func decode(_ b: [UInt8]) throws -> Contents {
        func str(_ a: Int, _ z: Int) -> String { String(b[a..<z].filter { $0 != 0 }.map { Character(UnicodeScalar($0)) }) }
        guard b.count >= head + 4, str(0, 4) == magic else { throw WireError("not an Optimist project file") }
        guard b[4] == version else { throw WireError("project file format \(b[4]) (this app reads \(version))") }
        let n = (0..<4).reduce(0) { $0 | Int(b[22 + $1]) << (8 * $1) }
        guard head + n + 4 == b.count else { throw WireError("the project file is cut short") }
        let crc = (0..<4).reduce(0) { $0 | Int(b[head + n + $1]) << (8 * $1) }
        guard crc == Int(Wire.crc32(b[0..<(head + n)])) else { throw WireError("the project file is damaged (CRC)") }
        return Contents(slot: Int(b[5]), family: str(6, 10), name: str(10, 22), data: Array(b[head..<(head + n)]))
    }
}
