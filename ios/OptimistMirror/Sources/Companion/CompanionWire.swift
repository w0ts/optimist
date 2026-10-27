// SPDX-License-Identifier: GPL-3.0-only
// The fm1-base companion services' wire format (fm1-base docs/COMPANION.md, written new from that description): the update's
// SysEx framing `F0 pack7(00 59 cmd len24 body ~sum8(body)) F7`, the capability query (0x60), the files (0x61) and the
// parameters (0x62). Pure functions, no CoreMIDI: the tests replay a transcript the fm1-base host simulator recorded.
import Foundation

enum CompanionWire {
    static let hello: UInt8 = 0x60, files: UInt8 = 0x61, params: UInt8 = 0x62

    static func frame(_ cmd: UInt8, _ body: [UInt8] = []) -> [UInt8] {
        let n = body.count
        let sum = body.reduce(0) { $0 + Int($1) }
        let raw: [UInt8] = [0, 0x59, cmd, UInt8(n & 0xFF), UInt8((n >> 8) & 0xFF), UInt8((n >> 16) & 0xFF)] + body + [UInt8(~sum & 0xFF)]
        return [0xF0] + FlashProtocol.pack7(raw) + [0xF7]
    }

    /// An answer (or push) in the update's framing with a good checksum, else nil. Optimist's own frames (F0 7D ..) are not.
    static func decode(_ f: [UInt8]) -> (cmd: UInt8, body: [UInt8])? {
        guard f.count >= 10, f.first == 0xF0, f.last == 0xF7, f[1] == 0x00 else { return nil }
        let u = FlashProtocol.unpack7(f[1..<(f.count - 1)])
        guard u.count >= 7, u[0] == 0, u[1] == 0x59 else { return nil }
        let n = Int(u[3]) | Int(u[4]) << 8 | Int(u[5]) << 16
        guard u.count >= n + 7 else { return nil }
        let body = Array(u[6..<(6 + n)])
        guard UInt8(~body.reduce(0) { $0 + Int($1) } & 0xFF) == u[6 + n] else { return nil }
        return (u[2], body)
    }

    /// Does `answer` answer the request (cmd, body)? (the device answers every request with the same cmd; files and parameters
    /// echo the operation, files the index too)
    static func matches(_ cmd: UInt8, _ request: [UInt8], _ answer: [UInt8]) -> Bool {
        switch cmd {
        case files: return answer.count >= 4 && request.count >= 3 && answer[0] == request[0] && answer[2] == request[1] && answer[3] == request[2]
        case params: return answer.first != 6 && answer.first == request.first
        default: return true
        }
    }

    static func u16(_ b: [UInt8], _ o: Int) -> Int { Int(b[o]) | Int(b[o + 1]) << 8 }
    static func u32(_ b: [UInt8], _ o: Int) -> UInt32 { UInt32(b[o]) | UInt32(b[o + 1]) << 8 | UInt32(b[o + 2]) << 16 | UInt32(b[o + 3]) << 24 }
    static func i32(_ b: [UInt8], _ o: Int) -> Int { Int(Int32(bitPattern: u32(b, o))) }
    static func p16(_ v: Int) -> [UInt8] { [UInt8(v & 0xFF), UInt8((v >> 8) & 0xFF)] }
    static func p32(_ v: UInt32) -> [UInt8] { (0..<4).map { UInt8((v >> (8 * UInt32($0))) & 0xFF) } }

    /// `k` NUL-terminated strings from `o`, and where the next thing starts
    static func strings(_ b: [UInt8], _ o: Int, _ k: Int) -> ([String], Int) {
        var out: [String] = [], at = o
        for _ in 0..<k {
            var z = min(at, b.count)
            while z < b.count, b[z] != 0 { z += 1 }
            out.append(String(decoding: b[min(at, b.count)..<z], as: UTF8.self))
            at = z + 1
        }
        return (out, at)
    }

    /// CRC-32, zlib's
    static func crc32(_ b: [UInt8]) -> UInt32 { Wire.crc32(b) }
}

enum CompanionError: Error, Equatable, LocalizedError {
    case silent(UInt8)
    case status(String, Int)
    case malformed(String)

    static let statuses = ["ok", "busy (the app plays: stop it first)", "no such file", "not allowed", "out of range", "CRC-32 mismatch",
                           "the flash refused", "out of order", "a malformed request", "an unknown operation"]

    var errorDescription: String? {
        switch self {
        case .silent(let c): return "No answer to 0x\(String(c, radix: 16)): the firmware has no companion services, or it stopped answering."
        case .status(let what, let s): return "\(what): \(s < Self.statuses.count ? Self.statuses[s] : "status \(s)")"
        case .malformed(let what): return "\(what): an answer the app cannot read."
        }
    }
}

/// 0x60: what this firmware is and has
struct CompanionInfo: Equatable {
    static let serviceNames = ["capability query", "files", "parameters", "backup", "mirror", "screen reader", "Bluetooth MIDI", "WiFi",
                               "USB audio in", "USB audio out", "the app's own SysEx", "the app's own commands", "the second core",
                               "USB MIDI host", "Bluetooth audio"]
    let version: Int
    let services: UInt32
    let readMax: Int, writeMax: Int
    let files: Int, params: Int
    let identity: String, name: String, appVersion: String
    let extras: [String: String]

    var serviceList: [String] { Self.serviceNames.enumerated().filter { services >> UInt32($0.offset) & 1 == 1 }.map(\.element) }
    var hasFiles: Bool { services & 2 != 0 }
    var hasParams: Bool { services & 4 != 0 }
    var hasBackup: Bool { services & 8 != 0 }
    var hasMirror: Bool { services & 16 != 0 }
    /// the package identity's prefix (fm1.toml id_prefix): the app's own `id_prefix` tag when it lists one; else the identity
    /// without its running digits (a development identity "FM-1_1400" is the prefix "FM-1_14" and "00"; a release one has six)
    var idPrefix: String {
        if let p = extras["id_prefix"], !p.isEmpty { return p }
        guard let us = identity.lastIndex(of: "_") else { return identity }
        let digits = identity[identity.index(after: us)...]
        guard digits.allSatisfy(\.isNumber) else { return identity }
        return String(identity.dropLast(digits.count >= 8 ? 6 : digits.count == 4 ? 2 : 0))
    }

    static func parse(_ b: [UInt8]) throws -> CompanionInfo {
        guard b.count >= 14, b[0] >= 1 else { throw CompanionError.malformed("capability query") }
        let (s, _) = CompanionWire.strings(b, 13, 4)
        var extras: [String: String] = [:]
        for kv in s[3].split(separator: ";") {
            let p = kv.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            extras[String(p[0])] = p.count > 1 ? String(p[1]) : ""
        }
        return CompanionInfo(version: Int(b[0]), services: CompanionWire.u32(b, 1), readMax: CompanionWire.u16(b, 5), writeMax: CompanionWire.u16(b, 7),
                             files: CompanionWire.u16(b, 9), params: CompanionWire.u16(b, 11), identity: s[0], name: s[1], appVersion: s[2], extras: extras)
    }
}

/// 0x61 op 0: one of the app's files
struct CompanionFile: Equatable, Identifiable {
    static let kinds = ["other", "project", "preset", "sample", "bank", "pattern", "settings", "text", "log"]
    let index: Int
    let name: String
    let kind: String
    let flags: Int
    let size: Int, room: Int
    var id: Int { index }
    var readable: Bool { flags & 1 != 0 }
    var writable: Bool { flags & 2 != 0 }
    var deletable: Bool { flags & 4 != 0 }
}

/// 0x62 op 0: one of the app's parameters
struct CompanionParam: Equatable, Identifiable {
    let index: Int
    let id: Int
    let name: String
    let unit: String
    let isEnum: Bool
    let group: String
    let readOnly: Bool
    let min: Int, max: Int, def: Int
    var labels: [String]
}
