// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 USB-MIDI update protocol (M-UPGRADE compatible), after web/fm1ota.js (Leo Kuroshita, GPL-3.0-only): the frames.
// Step 1: the running firmware reads parts of the package and stages the update loader. Step 2: the loader reads the whole
// image and writes it. In both steps the device sends cmd 0x30 read requests on the logical image and the app answers.
import Foundation

enum FlashProtocol {
    static let handshake: [UInt8] = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7]
    static let upgrade: [UInt8] = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7]
    static let maxData = 512
    /// the read addresses that end step 1 and step 2: answered "success"
    static let step1Done: UInt32 = 0xE000_0000
    static let step2Done: UInt32 = 0xF000_0000

    struct Identity: Equatable {
        let text: String          // "FM-1_700", "ota-FM-1_700"
        let model: String
        let version: Int
        var isLoader: Bool { model.lowercased().hasPrefix("ota-") }
        /// the update loader of Optimist ("ota-FM-1_7XX") and of SLOOP / Felucca ("ota-FM-1_9XX"): the same loader and
        /// protocol. A device left in another firmware's update mode (the stock loader, "ota-FM-1_0XX") is never written.
        var isOurLoader: Bool { text.range(of: #"^ota-FM-1_[79]\d\d$"#, options: [.regularExpression, .caseInsensitive]) != nil }
    }

    struct Request: Equatable {
        let flags: UInt8
        let address: UInt32
        let length: Int
    }

    static func pack7(_ data: [UInt8]) -> [UInt8] {
        var out: [UInt8] = []
        var acc: UInt32 = 0, nb = 0
        for b in data {
            acc |= UInt32(b) << nb; nb += 8
            while nb >= 7 { out.append(UInt8(acc & 0x7F)); acc >>= 7; nb -= 7 }
        }
        if nb > 0 { out.append(UInt8(acc & 0x7F)) }
        return out
    }

    static func unpack7(_ s: ArraySlice<UInt8>) -> [UInt8] {
        var out: [UInt8] = []
        var acc: UInt32 = 0, nb = 0
        for b in s {
            acc |= UInt32(b & 0x7F) << nb; nb += 7
            while nb >= 8 { out.append(UInt8(acc & 0xFF)); acc >>= 8; nb -= 8 }
        }
        return out
    }

    /// The answer to a read request: `data` from `address`.
    static func response(_ address: UInt32, _ data: ArraySlice<UInt8>, flags: UInt8 = 0) -> [UInt8] {
        let len = data.count
        var body: [UInt8] = [0x00, 0x59, 0x30, UInt8((len + 8) & 0xFF), UInt8(((len + 8) >> 8) & 0xFF), 0, flags,
                             UInt8(address & 0xFF), UInt8((address >> 8) & 0xFF), UInt8((address >> 16) & 0xFF), UInt8(address >> 24),
                             UInt8(len & 0xFF), UInt8((len >> 8) & 0xFF), 0]
        body += data
        var s = 0
        for b in body[6...] { s += Int(b) }
        body.append(UInt8(~s & 0xFF))
        return [0xF0] + pack7(body) + [0xF7]
    }

    static func success(_ address: UInt32) -> [UInt8] {
        response(address, ArraySlice(Array("success".utf8) + [0]))
    }

    static func request(_ pkt: [UInt8]) -> Request? {
        guard pkt.count >= 2, pkt.first == 0xF0, pkt.last == 0xF7 else { return nil }
        let u = unpack7(pkt[1..<(pkt.count - 1)])
        guard u.count == 15, u[0] == 0, u[1] == 0x59, u[2] == 0x30 else { return nil }
        let s = u[6..<14].reduce(0) { $0 + Int($1) }
        guard UInt8(~s & 0xFF) == u[14] else { return nil }
        let addr = UInt32(u[7]) | UInt32(u[8]) << 8 | UInt32(u[9]) << 16 | UInt32(u[10]) << 24
        return Request(flags: u[6], address: addr, length: Int(u[11]) | Int(u[12]) << 8 | Int(u[13]) << 16)
    }

    // MARK: - the fast install (docs/INSTALL-SPEED.md, as tools/fm1_install.py and web/fm1ota_optimist.js)
    // Before step 2's upgrade command the app says it can serve 4 KiB reads and the per-sector table (cmd 0x31 [1, 4096 LE]);
    // a loader that can answers the same frame, then asks only the sectors that differ, 4 KiB at a time, after the table at
    // `table` + flash.bin's offset (per 4 KiB sector: CRC16 XMODEM, CRC-32 zlib, little-endian). An older loader says
    // nothing and gets today's protocol.
    static let fastCommand: UInt8 = 0x31
    static let fastChunk = 4096
    static let sector = 4096
    static let table: UInt32 = 0xD000_0000

    /// a host -> device command as the firmware's ota.c reads it: 00 59 cmd len24 body ~sum(body), 7-bit packed
    static func frame(_ cmd: UInt8, _ body: [UInt8]) -> [UInt8] {
        let s = body.reduce(0) { $0 + Int($1) }
        return [0xF0] + pack7([0x00, 0x59, cmd, UInt8(body.count & 0xFF), UInt8((body.count >> 8) & 0xFF), 0] + body + [UInt8(~s & 0xFF)]) + [0xF7]
    }

    static let fastHello = frame(fastCommand, [1, UInt8(fastChunk & 0xFF), UInt8(fastChunk >> 8)])

    /// the loader's answer to `fastHello` -> its read size
    static func fastReply(_ pkt: [UInt8]) -> Int? {
        guard pkt.count >= 2, pkt.first == 0xF0, pkt.last == 0xF7 else { return nil }
        let u = unpack7(pkt[1..<(pkt.count - 1)])
        guard u.count == 10, Array(u[0..<6]) == [0, 0x59, fastCommand, 3, 0, 0], u[6] == 1 else { return nil }
        let s = u[6..<9].reduce(0) { $0 + Int($1) }
        guard UInt8(~s & 0xFF) == u[9] else { return nil }
        return Int(u[7]) | Int(u[8]) << 8
    }

    static func crc16(_ d: ArraySlice<UInt8>) -> UInt16 {
        var c: UInt16 = 0
        for b in d {
            c ^= UInt16(b) << 8
            for _ in 0..<8 { c = c & 0x8000 != 0 ? (c << 1) ^ 0x1021 : c << 1 }
        }
        return c
    }

    static func crc32(_ d: ArraySlice<UInt8>) -> UInt32 {
        var c: UInt32 = 0xFFFF_FFFF
        for b in d {
            c ^= UInt32(b)
            for _ in 0..<8 { c = c & 1 != 0 ? (c >> 1) ^ 0xEDB8_8320 : c >> 1 }
        }
        return ~c
    }

    /// the table for `count` sectors of `image` from `offset`: 6 bytes each
    static func sectorTable(_ image: [UInt8], offset: Int, count: Int) -> [UInt8] {
        var out: [UInt8] = []
        for k in 0..<count {
            let s = image[(offset + k * sector)..<(offset + (k + 1) * sector)]
            let a = crc16(s), b = crc32(s)
            out += [UInt8(a & 0xFF), UInt8(a >> 8), UInt8(b & 0xFF), UInt8((b >> 8) & 0xFF), UInt8((b >> 16) & 0xFF), UInt8(b >> 24)]
        }
        return out
    }

    /// the bytes a read request asks for (the fast install's table included; reads up to `maxData`), nil outside the package
    static func answer(_ image: [UInt8], _ r: Request, maxData: Int) -> ArraySlice<UInt8>? {
        if r.address >= table && r.address < step1Done {
            let off = Int(r.address - table)
            guard maxData >= fastChunk, r.length % 6 == 0, r.length <= maxData, off + r.length / 6 * sector <= image.count else { return nil }
            return ArraySlice(sectorTable(image, offset: off, count: r.length / 6))
        }
        guard r.length <= maxData, Int(r.address) + r.length <= image.count else { return nil }
        return image[Int(r.address)..<(Int(r.address) + r.length)]
    }

    static func identity(_ pkt: [UInt8]) -> Identity? {
        guard pkt.count >= 2, pkt.first == 0xF0, pkt.last == 0xF7 else { return nil }
        let d = unpack7(pkt[1..<(pkt.count - 1)])
        guard d.count == 34, d[0] == 0, d[1] == 0x59, d[2] == 0x11 else { return nil }
        let raw = d[6..<33]
        let text = String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self)
        guard let us = text.lastIndex(of: "_"), us != text.startIndex,
              let v = Int(text[text.index(after: us)...]), text[text.index(after: us)...].allSatisfy(\.isNumber) else { return nil }
        return Identity(text: text, model: String(text[..<us]), version: v)
    }
}
