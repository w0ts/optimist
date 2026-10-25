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
