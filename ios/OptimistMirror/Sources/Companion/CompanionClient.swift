// SPDX-License-Identifier: GPL-3.0-only
// The companion services' client (fm1-base docs/COMPANION.md): the capability query, the app's files (list, read whole, checked by the
// FM-1's CRC-32) and its parameters (described, get, set), over any transport that sends one request and returns its answer's body.
import Foundation

protocol CompanionTransport: AnyObject {
    /// One request in the update's framing, its answer's body (after the cmd); throws CompanionError.silent on no answer.
    func companion(_ cmd: UInt8, _ body: [UInt8], timeout: TimeInterval) async throws -> [UInt8]
}

struct CompanionClient {
    let transport: CompanionTransport
    var timeout: TimeInterval = 1.0
    var tries = 3
    /// the largest read chunk the device allows (256)
    var chunk = 256

    private func request(_ cmd: UInt8, _ body: [UInt8] = []) async throws -> [UInt8] {
        var last: Error = CompanionError.silent(cmd)
        for _ in 0..<tries {
            do { return try await transport.companion(cmd, body, timeout: timeout) } catch CompanionError.silent(let c) { last = CompanionError.silent(c) }
        }
        throw last
    }

    func hello() async throws -> CompanionInfo { try CompanionInfo.parse(try await request(CompanionWire.hello)) }

    // MARK: files

    /// (status, the answer after op, status, index)
    private func fileOp(_ op: UInt8, _ i: Int, _ body: [UInt8] = []) async throws -> (Int, [UInt8]) {
        let b = try await request(CompanionWire.files, [op] + CompanionWire.p16(i) + body)
        guard b.count >= 4 else { throw CompanionError.malformed("files") }
        return (Int(b[1]), Array(b[4...]))
    }

    func files() async throws -> [CompanionFile] {
        var out: [CompanionFile] = []
        var i = 0, count = 1
        while i < count {
            let (st, b) = try await fileOp(0, i)
            guard b.count >= 2 else { throw CompanionError.malformed("file list") }
            count = CompanionWire.u16(b, 0)
            if st != 0 || i >= count { break }
            guard b.count >= 13 else { throw CompanionError.malformed("file list") }
            let kind = Int(b[2])
            out.append(CompanionFile(index: i, name: CompanionWire.strings(b, 12, 1).0[0],
                                     kind: kind < CompanionFile.kinds.count ? CompanionFile.kinds[kind] : String(kind), flags: Int(b[3]),
                                     size: Int(CompanionWire.u32(b, 4)), room: Int(CompanionWire.u32(b, 8))))
            i += 1
        }
        return out
    }

    /// The whole file, checked against the FM-1's CRC-32 of it (the sum op reads it from the flash now)
    func read(_ i: Int, progress: ((Int, Int) -> Void)? = nil) async throws -> [UInt8] {
        let (st, s) = try await fileOp(2, i)
        guard st == 0 else { throw CompanionError.status("sum", st) }
        guard s.count >= 8 else { throw CompanionError.malformed("sum") }
        let size = Int(CompanionWire.u32(s, 0)), crc = CompanionWire.u32(s, 4)
        var data = [UInt8](repeating: 0, count: size)
        var at = 0
        while at < size {
            let (rs, b) = try await fileOp(1, i, CompanionWire.p32(UInt32(at)) + CompanionWire.p16(chunk))
            guard rs == 0 else { throw CompanionError.status("read at \(at)", rs) }
            guard b.count > 4 else { throw CompanionError.malformed("read at \(at)") }
            if Int(CompanionWire.u32(b, 0)) == at {
                let n = min(b.count - 4, size - at)
                data.replaceSubrange(at..<(at + n), with: b[4..<(4 + n)])
                at += n
            }
            progress?(at, size)
        }
        guard CompanionWire.crc32(data) == crc else { throw CompanionError.status("read", 5) }
        return data
    }

    // MARK: parameters

    private func paramOp(_ op: UInt8, _ body: [UInt8] = []) async throws -> (Int, [UInt8]) {
        let b = try await request(CompanionWire.params, [op] + body)
        guard b.count >= 2 else { throw CompanionError.malformed("parameters") }
        return (Int(b[1]), Array(b[2...]))
    }

    func groups() async throws -> [String] {
        var out: [String] = []
        var g = 0, count = 1
        while g < count {
            let (st, b) = try await paramOp(2, [UInt8(g)])
            guard b.count >= 2 else { throw CompanionError.malformed("group") }
            count = Int(b[1])
            if st != 0 || g >= count { break }
            out.append(CompanionWire.strings(b, 2, 1).0[0])
            g += 1
        }
        return out
    }

    private func labels(_ i: Int, _ total: Int) async throws -> [String] {
        var out: [String] = []
        while out.count < total {
            let (st, b) = try await paramOp(1, CompanionWire.p16(i) + CompanionWire.p16(out.count))
            guard st == 0, b.count >= 5, b[4] > 0 else { throw CompanionError.status("labels of parameter \(i)", st) }
            out += CompanionWire.strings(b, 5, Int(b[4])).0
        }
        return out
    }

    func params() async throws -> [CompanionParam] {
        let groups = try await groups()
        var out: [CompanionParam] = []
        var i = 0, count = 1
        while i < count {
            let (st, b) = try await paramOp(0, CompanionWire.p16(i))
            guard b.count >= 4 else { throw CompanionError.malformed("parameter list") }
            count = CompanionWire.u16(b, 2)
            if st != 0 || i >= count { break }
            guard b.count >= 24 else { throw CompanionError.malformed("parameter \(i)") }
            let nl = CompanionWire.u16(b, 21)
            let (names, _) = CompanionWire.strings(b, 23, 2)
            let g = Int(b[7])
            out.append(CompanionParam(index: i, id: CompanionWire.u16(b, 4), name: names[0], unit: names[1], isEnum: b[6] == 1,
                                      group: g < groups.count ? groups[g] : String(g), readOnly: b[8] & 1 != 0,
                                      min: CompanionWire.i32(b, 9), max: CompanionWire.i32(b, 13), def: CompanionWire.i32(b, 17),
                                      labels: nl > 0 ? try await labels(i, nl) : []))
            i += 1
        }
        return out
    }

    /// id -> value, for the ids the app has
    func get(_ ids: [Int]) async throws -> [Int: Int] {
        var out: [Int: Int] = [:]
        for k in stride(from: 0, to: ids.count, by: 32) {
            let (_, b) = try await paramOp(3, ids[k..<min(k + 32, ids.count)].flatMap(CompanionWire.p16))
            guard let n = b.first, b.count >= 1 + 6 * Int(n) else { throw CompanionError.malformed("get") }
            for j in 0..<Int(n) { out[CompanionWire.u16(b, 1 + 6 * j)] = CompanionWire.i32(b, 3 + 6 * j) }
        }
        return out
    }

    /// the value now (clamped, or as the app took it)
    func set(_ id: Int, _ value: Int) async throws -> Int {
        let (st, b) = try await paramOp(4, CompanionWire.p16(id) + CompanionWire.p32(UInt32(bitPattern: Int32(truncatingIfNeeded: value))))
        guard st == 0 else { throw CompanionError.status("set", st) }
        guard b.count >= 6 else { throw CompanionError.malformed("set") }
        return CompanionWire.i32(b, 2)
    }
}
