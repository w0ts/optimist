// SPDX-License-Identifier: GPL-3.0-only
// A fake FM-1 for the protocol tests: answers the editor commands the app sends (INFO, PROJECT, BK_*, SMP_*, PAT_*, UP_*, DESC)
// the way web/EDITOR_PROTOCOL.md says, byte for byte, so the app's functions run against real frames.
import Foundation
@testable import OptimistMirror

final class FakeFM1: Transport {
    struct Obj {
        var tag: String
        var kind: Int
        var inBuild = true
        var hasData = true
        var data: [UInt8]
    }
    struct Preset { var engine: Int; var name: String; var p: [Int]; var pattern: [UInt8] }

    var objs: [Obj]
    var log: [String] = []
    var corruptNextRead = false
    var refuseTag: String?
    var busyOnce = true
    var playing = false
    var sections = [1, 0, 1, 0]
    var patterns: [[Int: [UInt8]]] = [[:], [:], [:], [:]]
    var presets: [Int: Preset] = [:]
    var samples: [Int: (header: [UInt8], data: [UInt8])] = [:]
    var info: [UInt8]
    let pcount = 4
    let pageSize = 3
    private var open: [Int: (len: Int, crc: Int, data: [UInt8])] = [:]
    private var patOpen: [UInt8] = []

    init(objs: [Obj]) {
        self.objs = objs
        info = Array("OPTIMIST 0.9".utf8) + [0, 2, 4, 0, 16, 3] + Array("ANALOG".utf8) + [0] + Array("DIGITAL".utf8) + [0, 4, 6, 0, 1,
                                                                                    0x55, 2, 16, 16, 0x64, 1, 3]
        let opti = Array("opti_backup_4 opti_library_1 opti_projects_1 opti_patterns_1 ".utf8)   // (INFO's tag 66, ed_opti.h)
        info += [0x66, UInt8(opti.count)] + opti
    }

    static func ascii(_ s: String) -> [UInt8] { Array(s.utf8) }

    func exchange(_ cmd: Cmd, _ a: [UInt8], timeout: TimeInterval, retries: Int) async throws -> [UInt8] {
        switch cmd {
        case .info: return info
        case .panel: return [a.first ?? 0, 0]
        case .project:
            let k = Int(a[1])
            if a[0] == 1 { sections[k] = 1; log.append("SAVE \(k)") }
            return [a[0], a[1], UInt8(k < sections.count ? sections[k] : 0), playing ? 1 : 0]
        case .desc:
            let i = Int(a[1])
            return [0, a[1], 0] + Wire.v14enc(0) + Wire.v14enc(127) + Wire.v14enc(i * 3) + Fake.cstr(["LEVEL", "CUT", "CUT", "E0"][i]) + [0]
        case .bkList: return bkList(first: Int(a[1]))
        case .bkRead: return bkRead(a)
        case .bkBegin:
            let i = Int(a[0])
            guard i < objs.count else { return [a[0], 1] }
            open[i] = (Wire.rd7(a, 1, 3), Wire.rd7(a, 4, 5), [])
            log.append("BEGIN \(objs[i].tag)")
            return [a[0], 0]
        case .bkData:
            let i = Int(a[0]), off = Wire.rd7(a, 1, 3)
            let chunk = Wire.unpack7(Array(a[9...]))
            guard var s = open[i], off == s.data.count else { return [a[0]] + Wire.wr7(off, 3) + [1] }
            if Int(Wire.crc32(chunk)) != Wire.rd7(a, 4, 5) { return [a[0]] + Wire.wr7(off, 3) + [2] }
            s.data += chunk
            open[i] = s
            return [a[0]] + Wire.wr7(off, 3) + [0]
        case .bkCommit:
            let i = Int(a[0]), dry = a.count > 1 && a[1] == 1
            guard let s = open[i], s.data.count == s.len, Int(Wire.crc32(s.data)) == s.crc else { return [a[0], 2] }
            if objs[i].tag == refuseTag { open[i] = nil; return [a[0], 8] }
            if !dry { objs[i].data = s.data; log.append("COMMIT \(objs[i].tag)") } else { log.append("CHECK \(objs[i].tag)") }
            open[i] = nil
            return [a[0], 0]
        case .bkEnd:
            log.append("END \(a[0])")
            return [0]
        case .smpBegin: log.append("SMP_BEGIN \(a[0])"); samples[Int(a[0])] = ([], []); return [a[0], 0]
        case .smpWrite:
            samples[Int(a[0])]!.data += Wire.unpack7(Array(a[4...]))
            return [a[0], a[1], a[2], a[3], 0]
        case .smpEnd: samples[Int(a[0])]!.header = Wire.unpack7(Array(a[1...])); log.append("SMP_END \(a[0])"); return [a[0], 0]
        case .patList:
            var r: [UInt8] = [0, 0] + [UInt8](repeating: 0, count: 12)
            for k in 0..<4 { for s in 0..<16 { r.append(UInt8(patterns[k][s]?.count ?? 0) & 0x7F) } }
            return r + [UInt8](repeating: 127, count: 64)
        case .patRead:
            let k = Int(a[0]), s = Int(a[1]), off = Int(a[2]) | Int(a[3]) << 7
            let rec = patterns[k][s] ?? []
            let chunk = Array(rec[min(off, rec.count)..<min(off + 100, rec.count)])
            return [a[0], a[1], a[2], a[3], UInt8(rec.count & 0x7F), UInt8(rec.count >> 7)] + Wire.pack7(chunk)
        case .patWrite:
            let k = Int(a[0]), s = Int(a[1]), off = Int(a[2]) | Int(a[3]) << 7, total = Int(a[4]) | Int(a[5]) << 7
            if busyOnce { busyOnce = false; return [a[0], a[1], a[2], a[3], 3] }
            if off == 0 { patOpen = [] }
            patOpen += Wire.unpack7(Array(a[6...]))
            if patOpen.count >= total { patterns[k][s] = patOpen }
            return [a[0], a[1], a[2], a[3], 0]
        case .patOp:
            let op = a[0], k = Int(a[1]), s = Int(a[2]), k2 = Int(a[3]), s2 = Int(a[4])
            if op == 1 { patterns[k2][s2] = patterns[k][s] } else if op == 2 { patterns[k][s] = nil }
            return [a[0], 0]
        case .upList:
            let start = Int(a[0]), n = min(Int(a[1]), 32 - start)
            var r: [UInt8] = [a[0], UInt8(n), 32]
            for i in start..<(start + n) {
                let p = presets[i]
                r += [p == nil ? 0 : 1, UInt8(p?.engine ?? 0)] + Fake.cstr(p?.name ?? "")
            }
            return r
        case .upGet:
            let i = Int(a[0])
            guard let p = presets[i] else { return [a[0], 0, 0, 0] + [UInt8](repeating: 0, count: pcount * 2 + 32) }
            return [a[0], 1, UInt8(p.engine)] + Fake.cstr(p.name) + p.p.flatMap { Wire.v14enc($0) } + p.pattern
        case .upPut:
            var r = Reader(Array(a.dropFirst(1)))
            let engine = Int(try r.b()), name = try r.s()
            let p = try (0..<pcount).map { _ in try r.v() }
            let pat = Array(a[(a.count - 32)...])
            presets[Int(a[0])] = Preset(engine: engine, name: name, p: p, pattern: pat)
            return [a[0], 0]
        case .upStore:
            var r = Reader(Array(a.dropFirst(1)))
            let name = try r.s()
            presets[Int(a[0])] = Preset(engine: 0, name: name.isEmpty ? "ANALOG 07" : name, p: [1, 2, 3, 4], pattern: [UInt8](repeating: 0, count: 32))
            return [a[0], 0]
        case .upLoad: return [a[0], presets[Int(a[0])] == nil ? 1 : 0]
        case .upErase: presets[Int(a[0])] = nil; return [a[0], 0]
        }
    }

    private func bkList(first: Int) -> [UInt8] {
        let n = objs.count, count = min(pageSize, n - first)
        var r: [UInt8] = [4, UInt8(n), 0x15, 0] + Fake.ascii("FUNB") + [0, 2, UInt8(first), UInt8(count)]
        for k in first..<(first + count) {
            let o = objs[k]
            r += Fake.ascii(o.tag) + [UInt8(o.kind), (o.inBuild ? 1 : 0) | (o.hasData ? 2 : 0)] + Wire.wr7(o.data.count, 3) + Wire.wr7(Int(Wire.crc32(o.data)), 5)
        }
        if first + count >= n {
            r += Wire.wr7(255, 2) + Wire.wr7(1023, 5) + [0] + Wire.wr7(7, 2) + [8] + Wire.wr7(81408, 3) + Wire.wr7(81408, 3) + Wire.wr7(81408, 3)
            r += Wire.wr7(4, 2) + Wire.wr7(300, 2) + Wire.wr7(30, 2) + Wire.wr7(40, 2) + [90, 62, 60, 0]
        }
        return r
    }

    private func bkRead(_ a: [UInt8]) -> [UInt8] {
        let i = Int(a[0]), off = Wire.rd7(a, 1, 3)
        let d = objs[i].data
        let chunk = Array(d[off..<min(off + 256, d.count)])
        var crc = Int(Wire.crc32(chunk))
        if corruptNextRead { corruptNextRead = false; crc ^= 1 }
        return [a[0]] + Wire.wr7(off, 3) + Wire.wr7(crc, 5) + Wire.pack7(chunk)
    }
}

enum Fake {
    static func ascii(_ s: String) -> [UInt8] { Array(s.utf8) }
    static func cstr(_ s: String) -> [UInt8] { Array(s.utf8) + [0] }
}
