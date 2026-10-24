// SPDX-License-Identifier: GPL-3.0-only
// The editor protocol's wire format (web/EDITOR_PROTOCOL.md, web/editor.html "PROTO-BEGIN"): frames, 7-bit values, CRC-32,
// the replies the app reads. Pure functions, no CoreMIDI: the tests run them against a fake device.
import Foundation

enum Cmd: UInt8 {
    case info = 1, desc = 5, project = 9
    case smpBegin = 11, smpWrite = 12, smpEnd = 13
    case upList = 16, upGet = 17, upPut = 18, upStore = 19, upLoad = 20, upErase = 21
    case bkList = 43, bkRead = 44, bkBegin = 45, bkData = 46, bkCommit = 47, bkEnd = 48
    case patList = 79, patOp = 82, patRead = 83, patWrite = 84
    case panel = 108, nav = 109, pjOp = 110, clip = 111
}

struct WireError: Error, LocalizedError, Equatable {
    let message: String
    init(_ message: String) { self.message = message }
    var errorDescription: String? { message }
    static func timeout(_ cmd: UInt8) -> WireError { WireError("timeout (cmd \(cmd))") }
    var isTimeout: Bool { message.hasPrefix("timeout") }
}

enum Wire {
    static let header: [UInt8] = [0x7D, 0x46, 0x4C]

    /// The request's bytes after the header: the command and its arguments, every one a 7-bit value.
    static func body(_ cmd: UInt8, _ args: [UInt8]) throws -> [UInt8] {
        if let bad = args.first(where: { $0 > 0x7F }) { throw WireError("bad data byte \(bad)") }
        return [cmd] + args
    }

    /// F0 7D 46 4C cmd args F7
    static func frame(_ cmd: UInt8, _ args: [UInt8]) throws -> [UInt8] {
        [0xF0] + header + (try body(cmd, args)) + [0xF7]
    }

    /// A reply frame -> (cmd, bytes after the command), nil when it is not an editor frame.
    static func unframe(_ f: [UInt8]) -> (cmd: UInt8, args: [UInt8])? {
        guard f.count >= 6, f[0] == 0xF0, f.last == 0xF7, Array(f[1...3]) == header else { return nil }
        return (f[4], Array(f[5..<(f.count - 1)]))
    }

    /// Does the reply `a` answer the request (cmd, args)? (web: replyMatches, for the commands the app sends)
    static func replyMatches(_ cmd: UInt8, _ args: [UInt8], _ a: [UInt8]) -> Bool {
        func same(_ n: Int) -> Bool { a.count >= n && args.count >= n && (0..<n).allSatisfy { a[$0] == args[$0] } }
        switch Cmd(rawValue: cmd) {
        case .desc: return same(3)                       // (scope, id 2 x 7 bit)
        case .project: return same(2)
        case .pjOp: return same(args.first == 0 ? 1 : 2)  // (op 0 info: 0, 0, ..)
        case .smpBegin, .smpEnd, .upList, .upGet, .upPut, .upStore, .upLoad, .upErase, .patList, .patOp, .nav: return same(1)
        case .smpWrite, .patRead, .patWrite: return same(4)
        case .clip:                                      // (op 0: 0, 0, ..; 1, 2: op, rc, track, index)
            return args.first == 0 ? same(1) : a.count >= 4 && args.count >= 3 && a[0] == args[0] && a[2] == args[1] && a[3] == args[2]
        default: return true
        }
    }

    // MARK: values

    /// groups of up to 7 bytes, each preceded by their top bits
    static func pack7(_ b: [UInt8]) -> [UInt8] {
        var out: [UInt8] = []
        out.reserveCapacity(b.count + b.count / 7 + 1)
        var i = 0
        while i < b.count {
            let g = b[i..<min(i + 7, b.count)]
            var top: UInt8 = 0
            for (j, x) in g.enumerated() { top |= ((x >> 7) & 1) << UInt8(j) }
            out.append(top)
            out.append(contentsOf: g.map { $0 & 0x7F })
            i += 7
        }
        return out
    }

    static func unpack7(_ a: [UInt8]) -> [UInt8] {
        var out: [UInt8] = []
        out.reserveCapacity(a.count)
        var i = 0
        while i < a.count {
            let top = a[i]
            var j = 0
            while j < 7 && i + 1 + j < a.count {
                out.append(a[i + 1 + j] | (((top >> UInt8(j)) & 1) << 7))
                j += 1
            }
            i += 8
        }
        return out
    }

    /// k 7-bit groups, least significant first
    static func wr7(_ v: Int, _ k: Int) -> [UInt8] { (0..<k).map { UInt8((v >> (7 * $0)) & 0x7F) } }

    /// k 7-bit groups read back (as the web page: truncated to 32 bits)
    static func rd7(_ a: [UInt8], _ o: Int, _ k: Int) -> Int {
        var v = 0
        for i in stride(from: k - 1, through: 0, by: -1) { v = v * 128 + Int(a[o + i] & 0x7F) }
        return v & 0xFFFF_FFFF
    }

    /// signed 14 bit, value + 8192, LSB first
    static func v14enc(_ v: Int) -> [UInt8] {
        let u = max(-8192, min(8191, v)) + 8192
        return [UInt8(u & 0x7F), UInt8((u >> 7) & 0x7F)]
    }

    static func v14dec(_ lo: UInt8, _ hi: UInt8) -> Int { (Int(lo) | Int(hi) << 7) - 8192 }

    /// ASCII, 7 bit, zero-terminated
    static func strEnc(_ s: String) -> [UInt8] { s.unicodeScalars.map { UInt8(truncatingIfNeeded: $0.value) & 0x7F } + [0] }

    private static let crcTable: [UInt32] = (0..<256).map { n in
        var c = UInt32(n)
        for _ in 0..<8 { c = c & 1 == 1 ? 0xEDB8_8320 ^ (c >> 1) : c >> 1 }
        return c
    }

    static func crc32<S: Sequence>(_ b: S) -> UInt32 where S.Element == UInt8 {
        var c: UInt32 = 0xFFFF_FFFF
        for x in b { c = crcTable[Int((c ^ UInt32(x)) & 0xFF)] ^ (c >> 8) }
        return c ^ 0xFFFF_FFFF
    }
}

/// A reply's bytes, read in order.
struct Reader {
    let a: [UInt8]
    var i = 0
    init(_ a: [UInt8]) { self.a = a }
    var atEnd: Bool { i >= a.count }
    mutating func b() throws -> UInt8 {
        guard i < a.count else { throw WireError("short reply") }
        defer { i += 1 }
        return a[i]
    }
    mutating func v() throws -> Int {
        let lo = try b()
        return Wire.v14dec(lo, try b())
    }
    /// a parameter id or count, 2 x 7 bit (protocol 7)
    mutating func id() throws -> Int {
        let lo = Int(try b())
        return lo | Int(try b()) << 7
    }
    mutating func s() throws -> String {
        var out = ""
        while true {
            let c = try b()
            if c == 0 { return out }
            out.unicodeScalars.append(UnicodeScalar(c))
        }
    }
}

// MARK: - the replies

/// The features the app uses, by the names and versions INFO's tag 66 lists.
struct Feature: Equatable {
    let name: String, version: Int
    static let backup = Feature(name: "opti_backup", version: 4)
    static let mirror = Feature(name: "opti_mirror", version: 2)
    static let library = Feature(name: "opti_library", version: 1)
    static let projects = Feature(name: "opti_projects", version: 1)
    static let patterns = Feature(name: "opti_patterns", version: 1)
    static let panel = Feature(name: "opti_panel", version: 1)
    static let nav = Feature(name: "opti_nav", version: 1)
    static let proj = Feature(name: "opti_proj", version: 1)
    static let mirbt = Feature(name: "opti_mirbt", version: 1)   // INFO, PANEL and NAV over Bluetooth too (ed_mirror.c)
    static let clip = Feature(name: "opti_clip", version: 1)     // 111 CLIP, a synth track's steps (ed_clip.c; INFO tag 67)
}

struct DeviceInfo: Equatable {
    var version = ""
    var nengines = 0, pcount = 0, gcount = 0, nstep = 0, pe0 = 0
    var engines: [String] = []
    var proto = 0
    var lite = false          // tag 64: a backup-only firmware
    var lib = false           // tag 64 bit 1: and the librarian
    var patSlots = 0, patScenes = 0   // tag 55: the per-track patterns (0: none)
    var hasPatterns: Bool { patSlots > 0 && has(.patterns) }
    var features: [String: Int] = [:]   // tags 66 and 67: "opti_backup_4 opti_mirror_2 ...", name -> version (none: not optimist's way)

    /// The firmware offers f in the version this app reads (firmware/src/io/editor/ed_opti.h): every Felucca-family
    /// firmware answers INFO, but numbers its other commands its own way.
    func has(_ f: Feature) -> Bool { features[f.name] == f.version }

    static func features(_ bytes: ArraySlice<UInt8>) -> [String: Int] {
        var out: [String: Int] = [:]
        for w in String(decoding: bytes, as: UTF8.self).split(separator: " ") {
            guard let u = w.lastIndex(of: "_"), let v = Int(w[w.index(after: u)...]) else { continue }
            out[String(w[..<u])] = v
        }
        return out
    }

    static func parse(_ a: [UInt8]) throws -> DeviceInfo {
        var r = Reader(a)
        var o = DeviceInfo()
        o.version = try r.s()
        o.nengines = Int(try r.b()); o.pcount = try r.id(); o.gcount = try r.id()   // (protocol 7: 2 x 7 bit)
        o.nstep = Int(try r.b()); o.pe0 = try r.id()
        for _ in 0..<o.nengines { o.engines.append(try r.s()) }
        if !r.atEnd { _ = try r.b() }                    // v3: tracks
        o.proto = r.atEnd ? 0 : Int(try r.b())
        if o.proto >= 6 { for _ in 0..<o.nengines where !r.atEnd { _ = try r.b() } }   // v6: each engine's UID
        while o.proto >= 6 && r.i + 2 <= a.count {
            let id = try r.b(), n = Int(try r.b())
            if r.i + n > a.count { break }
            if id == 0x55 && n >= 2 { o.patSlots = Int(a[r.i]); o.patScenes = Int(a[r.i + 1]) }
            if id == 0x64 && n >= 1 { o.lite = a[r.i] & 1 != 0; o.lib = a[r.i] & 2 != 0 }
            if id == 0x66 || id == 0x67 { o.features.merge(features(a[r.i..<r.i + n])) { $1 } }   // (67: more names, 66 full)
            r.i += n
        }
        return o
    }
}

struct ProjectReply: Equatable {
    var op: Int, slot: Int, used: Int, busy: Bool
    static func parse(_ a: [UInt8]) throws -> ProjectReply {
        var r = Reader(a)
        return ProjectReply(op: Int(try r.b()), slot: Int(try r.b()), used: Int(try r.b()), busy: a.count > 3 && a[3] == 1)
    }
}

/// PJ_OP 0 (ed_proj.c): the FM-1's 16 projects, the open one, which hold something, their names ("" none)
struct ProjList: Equatable {
    static let count = 16
    var rc = 0, open = 0
    var used: [Bool] = []
    var names: [String] = []
    static func parse(_ a: [UInt8]) throws -> ProjList {
        var r = Reader(a)
        guard try r.b() == 0 else { throw WireError("PJ_OP: not an info reply") }
        _ = try r.b()
        var o = ProjList()
        o.rc = Int(try r.b()); o.open = Int(try r.b())
        guard r.i + 3 <= a.count else { throw WireError("short reply") }
        let bits = Wire.rd7(a, r.i, 3)
        r.i += 3
        o.used = (0..<count).map { bits >> $0 & 1 == 1 }
        for _ in 0..<count { o.names.append(try r.s()) }
        return o
    }
}

/// A one-byte status after the slot (SMP_BEGIN, UP_PUT ..): slot, rc
struct SlotRc: Equatable {
    var slot: Int, rc: Int
    static func parse(_ a: [UInt8]) throws -> SlotRc {
        var r = Reader(a)
        return SlotRc(slot: Int(try r.b()), rc: Int(try r.b()))
    }
}

struct BkObject: Equatable {
    var i: Int
    var tag: String
    var kind: String
    var inBuild: Bool, hasData: Bool, fm6: Bool
    var len: Int, crc: Int
}

struct BkCaps: Equatable {
    var eng: Int, kits: Int, sets: Int, sections: Int
    var usrCap: [Int]
    var engAt: Int, trk: Int, kitAt: Int, e0At: Int
    var np: Int, pe0: Int, slen: Int
    var bits: [Int]
}

struct BkList: Equatable {
    static let kinds = ["st", "usr", "fm6", "sec", "prj", "log", "snp", "pjs"]
    var version = 0, n = 0, switches = 0
    var magic = ""
    var chunk = 0, first = 0, count = 0
    var objs: [BkObject] = []
    var caps: BkCaps?

    /// One page of BK_LIST (v3: first, count; the rest by asking again; v2: the whole list)
    static func parse(_ a: [UInt8]) throws -> BkList {
        guard a.count >= 10 else { throw WireError("short reply") }
        var o = BkList()
        o.version = Int(a[0]); o.n = Int(a[1])
        o.switches = Int(a[2]) | Int(a[3]) << 7
        o.magic = String(a[4..<8].map { Character(UnicodeScalar($0)) })
        o.chunk = Int(a[8]) | Int(a[9]) << 7
        let v3 = a[0] >= 3
        if v3 && a.count < 12 { throw WireError("short reply") }
        o.first = v3 ? Int(a[10]) : 0
        o.count = v3 ? Int(a[11]) : Int(a[1])
        var p = v3 ? 12 : 10
        var k = 0
        while k < o.count && p + 14 <= a.count {
            let f = a[p + 5]
            let kind = Int(a[p + 4]) < kinds.count ? kinds[Int(a[p + 4])] : "usr"
            o.objs.append(BkObject(i: o.first + k, tag: String(a[p..<(p + 4)].map { Character(UnicodeScalar($0)) }), kind: kind,
                                   inBuild: f & 1 != 0, hasData: f & 2 != 0, fm6: f & 8 != 0,
                                   len: Wire.rd7(a, p + 6, 3), crc: Wire.rd7(a, p + 9, 5)))
            k += 1
            p += 14
        }
        if a[0] >= 2 && o.first + o.count >= o.n && p + 32 <= a.count {
            var bits: [Int] = []
            var i = 0
            while i < Int(a[p + 31]) && p + 32 + i < a.count { bits.append(Int(a[p + 32 + i])); i += 1 }
            o.caps = BkCaps(eng: Wire.rd7(a, p, 2), kits: Wire.rd7(a, p + 2, 5) + Wire.rd7(a, p + 7, 1) * (1 << 35),
                            sets: Wire.rd7(a, p + 8, 2), sections: Int(a[p + 10]),
                            usrCap: [Wire.rd7(a, p + 11, 3), Wire.rd7(a, p + 14, 3), Wire.rd7(a, p + 17, 3)],
                            engAt: Wire.rd7(a, p + 20, 2), trk: Wire.rd7(a, p + 22, 2), kitAt: Wire.rd7(a, p + 24, 2),
                            e0At: Wire.rd7(a, p + 26, 2), np: Int(a[p + 28]), pe0: Int(a[p + 29]), slen: Int(a[p + 30]), bits: bits)
        }
        return o
    }
}

struct BkChunk: Equatable {
    var i: Int, off: Int, crc: Int, data: [UInt8]
    static func parse(_ a: [UInt8]) throws -> BkChunk {
        guard a.count >= 9 else { throw WireError("short reply") }
        return BkChunk(i: Int(a[0]), off: Wire.rd7(a, 1, 3), crc: Wire.rd7(a, 4, 5), data: Wire.unpack7(Array(a[9...])))
    }
}

struct UpSlot: Equatable {
    var slot: Int, used: Bool, engine: Int, name: String
}

struct UpList: Equatable {
    var start = 0, count = 0, total = 0
    var slots: [UpSlot] = []
    static func parse(_ a: [UInt8]) throws -> UpList {
        var r = Reader(a)
        var o = UpList(start: Int(try r.b()), count: Int(try r.b()), total: Int(try r.b()))
        for i in 0..<o.count { o.slots.append(UpSlot(slot: o.start + i, used: try r.b() != 0, engine: Int(try r.b()), name: try r.s())) }
        return o
    }
}

struct UpPatch: Equatable {
    var slot: Int, used: Bool, engine: Int, name: String
    var p: [Int], pattern: [[Int]]
    static let patSteps = 16
    static func parse(_ a: [UInt8], pcount: Int) throws -> UpPatch {
        var r = Reader(a)
        var o = UpPatch(slot: Int(try r.b()), used: try r.b() != 0, engine: Int(try r.b()), name: try r.s(), p: [], pattern: [])
        for _ in 0..<pcount { o.p.append(try r.v()) }
        for _ in 0..<patSteps { o.pattern.append([Int(try r.b()), Int(try r.b())]) }
        return o
    }
}

struct PatList: Equatable {
    var op = 0, changed = 0
    var tracks: [(cur: Int, req: Int, when: Int)] = []
    var len: [[Int]]?          // 4 tracks x 16 slots: the stored pattern's LEN, 0 empty (op 0)
    var scenes: [[Int]]?       // 16 scenes x 4 patterns
    static func parse(_ a: [UInt8]) throws -> PatList {
        guard a.count >= 14 else { throw WireError("short reply") }
        var o = PatList(op: Int(a[0]), changed: Int(a[1]))
        for k in 0..<4 { o.tracks.append((Int(a[2 + 3 * k]), Int(a[3 + 3 * k]), Int(a[4 + 3 * k]))) }
        if a[0] == 0 && a.count >= 14 + 64 + 64 {
            o.len = (0..<4).map { k in a[(14 + 16 * k)..<(30 + 16 * k)].map { Int($0) } }
            o.scenes = (0..<16).map { s in a[(78 + 4 * s)..<(82 + 4 * s)].map { Int($0) } }
        }
        return o
    }
    static func == (l: PatList, r: PatList) -> Bool { l.op == r.op && l.changed == r.changed && l.len == r.len && l.scenes == r.scenes }
}

struct PatChunk: Equatable {
    var track: Int, slot: Int, off: Int, total: Int, data: [UInt8]
    static func parse(_ a: [UInt8]) throws -> PatChunk {
        guard a.count >= 6 else { throw WireError("short reply") }
        return PatChunk(track: Int(a[0]), slot: Int(a[1]), off: Int(a[2]) | Int(a[3]) << 7, total: Int(a[4]) | Int(a[5]) << 7,
                        data: Wire.unpack7(Array(a[6...])))
    }
}
