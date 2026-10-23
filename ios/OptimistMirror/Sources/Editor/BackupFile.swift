// SPDX-License-Identifier: GPL-3.0-only
// The .optimist-backup file, byte for byte as the web editor writes it (web/editor.html backupFile / readBackupFile, and
// web/EDITOR_PROTOCOL.md "The backup file"): "OPTBKUP" 0x01, u32 LE header length, the JSON header, the objects' bytes, u32 LE
// CRC-32 of everything before it. A section's file is the same format with its one object.
import Foundation

struct BackupObject: Equatable {
    var tag: String
    var kind: String
    var fm6: Bool
    var data: [UInt8]
}

/// What the header says about the device that wrote the file.
struct DeviceStamp: Equatable {
    var version: String?
    var proto: Int
    var switches: Int
    var magic: String
    var bk: Int
    var caps: BkCaps?
}

struct BackupContents {
    var version: String
    var magic: String
    var created: String
    var objects: [BackupObject]
}

enum BackupFile {
    static let magic: [UInt8] = [0x4F, 0x50, 0x54, 0x42, 0x4B, 0x55, 0x50, 0x01]
    static let readKinds: Set<String> = ["usr", "fm6", "sec", "prj", "log", "snp", "pjs"]

    private static func js(_ s: String) -> String {
        var out = "\""
        for u in s.unicodeScalars {
            switch u {
            case "\"": out += "\\\""
            case "\\": out += "\\\\"
            case "\n": out += "\\n"
            case "\r": out += "\\r"
            case "\t": out += "\\t"
            default: out += u.value < 0x20 ? String(format: "\\u%04x", u.value) : String(u)
            }
        }
        return out + "\""
    }

    private static func capsJSON(_ c: BkCaps) -> String {
        "{\"eng\":\(c.eng),\"kits\":\(c.kits),\"sets\":\(c.sets),\"sections\":\(c.sections),\"usrCap\":[\(c.usrCap.map(String.init).joined(separator: ","))],"
            + "\"engAt\":\(c.engAt),\"trk\":\(c.trk),\"kitAt\":\(c.kitAt),\"e0At\":\(c.e0At),\"np\":\(c.np),\"pe0\":\(c.pe0),\"slen\":\(c.slen),"
            + "\"bits\":[\(c.bits.map(String.init).joined(separator: ","))]}"
    }

    /// The date as JavaScript's toISOString writes it (2026-10-23T12:34:56.789Z)
    static func isoNow(_ d: Date = Date()) -> String {
        let f = ISO8601DateFormatter()
        f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return f.string(from: d)
    }

    static func encode(device: DeviceStamp, objects: [BackupObject], created: String = isoNow()) -> [UInt8] {
        var at = 0
        var objs: [String] = []
        for o in objects {
            objs.append("{\"tag\":\(js(o.tag)),\"kind\":\(js(o.kind)),\"fm6\":\(o.fm6),\"len\":\(o.data.count),\"crc\":\(Wire.crc32(o.data)),\"at\":\(at)}")
            at += o.data.count
        }
        var dev = "{"
        if let v = device.version { dev += "\"version\":\(js(v)),"}
        dev += "\"proto\":\(device.proto),\"switches\":\(device.switches),\"magic\":\(js(device.magic)),\"bk\":\(device.bk)"
        if let c = device.caps { dev += ",\"caps\":\(capsJSON(c))" }
        dev += "}"
        let hdr = Array("{\"format\":\"optimist-backup\",\"version\":1,\"created\":\(js(created)),\"device\":\(dev),\"objects\":[\(objs.joined(separator: ","))]}".utf8)
        var out = magic
        out += le32(UInt32(hdr.count))
        out += hdr
        for o in objects { out += o.data }
        out += le32(Wire.crc32(out))
        return out
    }

    private static func le32(_ v: UInt32) -> [UInt8] { (0..<4).map { UInt8((v >> (8 * UInt32($0))) & 0xFF) } }
    private static func rd32(_ b: [UInt8], _ o: Int) -> Int { (0..<4).reduce(0) { $0 | Int(b[o + $1]) << (8 * $1) } }

    static func decode(_ b: [UInt8]) throws -> BackupContents {
        guard b.count >= 16, Array(b[0..<8]) == magic else { throw WireError("not an Optimist backup") }
        let n = rd32(b, 8), end = b.count - 4
        guard 12 + n <= end, rd32(b, end) == Int(Wire.crc32(b[0..<end])) else { throw WireError("the backup file is damaged (CRC)") }
        guard let root = try? JSONSerialization.jsonObject(with: Data(b[12..<(12 + n)])) as? [String: Any],
              root["format"] as? String == "optimist-backup", (root["version"] as? NSNumber)?.intValue == 1,
              let list = root["objects"] as? [[String: Any]] else { throw WireError("not an Optimist backup") }
        let base = 12 + n
        var objects: [BackupObject] = []
        for o in list {
            guard let at = (o["at"] as? NSNumber)?.intValue, let len = (o["len"] as? NSNumber)?.intValue,
                  let crc = (o["crc"] as? NSNumber)?.intValue, at >= 0, len >= 0 else { throw WireError("not an Optimist backup") }
            let tag = o["tag"].map { "\($0)" } ?? ""
            guard base + at + len <= end else { throw WireError("the backup file is cut short") }
            let data = Array(b[(base + at)..<(base + at + len)])
            guard Int(Wire.crc32(data)) == crc else { throw WireError("object \(tag): CRC") }
            let kind = o["kind"] as? String ?? "st"
            objects.append(BackupObject(tag: tag, kind: readKinds.contains(kind) ? kind : "st", fm6: (o["fm6"] as? NSNumber)?.boolValue ?? false, data: data))
        }
        let dev = root["device"] as? [String: Any] ?? [:]
        return BackupContents(version: dev["version"] as? String ?? "", magic: dev["magic"] as? String ?? "",
                              created: root["created"] as? String ?? "", objects: objects)
    }
}

// MARK: - what a restore may write

/// Why an object of a file is not written to this device.
enum SkipReason: String, Equatable {
    case unknown = "not a kind this firmware has"
    case notInBuild = "not in this build (not written)"
    case tooBig = "larger than this build's slot (not written)"
    case snapshots = "snapshots are restored in the web editor"
}

struct RestoreItem: Equatable {
    var object: BackupObject
    var device: BkObject?
    var why: SkipReason?
}

enum Restore {
    /// The objects of a file against the device's list: what can be written, and why not (web: bkPlan; the snapshots area
    /// goes through the snapshot import, which the app does not have)
    static func plan(_ file: [BackupObject], _ list: BkList) -> [RestoreItem] {
        file.map { o in
            let d = list.objs.first { $0.tag == o.tag }
            var cap = 0
            if o.kind == "usr", o.tag.count == 4, o.tag.hasPrefix("USR"), let n = Int(String(o.tag.last!)), (1...3).contains(n),
               let caps = list.caps { cap = caps.usrCap[n - 1] }
            var why: SkipReason?
            if let d {
                if !d.inBuild || (o.kind == "usr" && o.fm6) { why = .notInBuild }
                else if d.kind != o.kind && !(d.kind == "prj" && o.kind == "st") { why = .unknown }
                else if cap > 0 && o.data.count > cap { why = .tooBig }
                else if o.kind == "snp" { why = .snapshots }
            } else { why = .unknown }
            return RestoreItem(object: o, device: d, why: why)
        }
    }

    static let names: [String: String] = [
        "SETT": "settings, panel, song", "DLNS": "drum sounds of the projects", "PRJ1": "project / section A", "PRJ2": "project / section B",
        "PRJ3": "project / section C", "PRJ4": "project / section D", "AUTO": "working project", "UPR1": "user presets 1-16",
        "UPR2": "user presets 17-32", "UPF6": "FM6 voices of the user presets", "UKIT": "user drum kits",
        "FM6B": "FM6 user bank (not in any build any more)", "CZBK": "CZ tones U01-U26", "USR1": "sample slot USR1",
        "USR2": "sample slot USR2", "USR3": "sample slot USR3", "SNG1": "song chain past 16 parts", "SNAP": "snapshots (every slot)",
        "DLFO": "drum lane LFOs of the sections and the working project (older backups)", "DLF1": "drum lane LFOs, LFO 2, ENV2 (sections A..I)",
    ]

    static func label(_ tag: String) -> String {
        if let n = names[tag] { return n }
        if tag.count == 4, tag.hasPrefix("S"), let k = Int(tag.dropFirst().prefix(2)), (1...16).contains(k) {
            return "section \(Character(UnicodeScalar(UInt8(64 + k))))"
        }
        return tag
    }
}
