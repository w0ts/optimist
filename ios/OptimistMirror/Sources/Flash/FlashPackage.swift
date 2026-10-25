// SPDX-License-Identifier: GPL-3.0-only
// FM-1 .fwsc packages: the package identity and the image the device reads during an update (web/fm1pkg.js productOf,
// logicalImage), and the check the web installer makes before an install (web/installer/installer.js checkPackage): an
// Optimist, SLOOP or Felucca package with our update loader. The return to the official V15 stays in the web installer.
import CryptoKit
import Foundation

struct FlashPackage: Equatable {
    let title: String
    let file: String              // the .fwsc's name ("optimist-0.1-synth.fwsc"): Install names it, nothing is written blindly
    let product: String           // "FM-1_700"
    let sha256: String
    let image: [UInt8]            // the logical image (the .fwsc without its 20 marker bytes)

    private static let blocks = 20, blk = 0x30, keep = 0x2F
    /// firmware/loader: in every package the installer writes (make_site.py)
    private static let loaderMark = Array("FELUCCA-LOADER-1".utf8)

    enum Problem: Error, Equatable, CustomStringConvertible {
        case notFM1                   // too short to be a package
        case notOurs(String)          // another identity (the official firmware, another project)
        case noLoader                 // no update loader of ours in it
        case mismatch(String)         // not the package the list promised (identity or SHA-256)

        var description: String {
            switch self {
            case .notFM1: return "This file is not an FM-1 package."
            case .notOurs(let p): return "This file is not a package the app can install (an Optimist, SLOOP or Felucca .fwsc): \(p.isEmpty ? "no identity" : p)."
            case .noLoader: return "This package has no update loader the app knows: install it with its own updater."
            case .mismatch(let d): return "The download is not the package the list names (\(d)): try again."
            }
        }
    }

    /// The package identity: one marker byte after each of the first 20 blocks.
    static func product(_ fwsc: [UInt8]) throws -> String {
        guard fwsc.count >= blocks * blk else { throw Problem.notFM1 }
        var s = ""
        for i in 0..<blocks {
            let m = fwsc[i * blk + keep]
            if m != 0x7D { s.append(Character(UnicodeScalar(UInt8(truncatingIfNeeded: Int(m) - i - 1)))) }
        }
        return s
    }

    /// The image the device addresses during an update.
    static func logicalImage(_ fwsc: [UInt8]) -> [UInt8] {
        var out = [UInt8]()
        out.reserveCapacity(fwsc.count - blocks)
        for i in 0..<blocks { out += fwsc[(i * blk)..<(i * blk + keep)] }
        out += fwsc[(blocks * blk)...]
        return out
    }

    static func sha256(_ bytes: [UInt8]) -> String {
        SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
    }

    /// expect: the list's identity (nil: check it as a file of the user's) and digest, which a download must match;
    /// none for the user's own file.
    static func check(_ fwsc: [UInt8], title: String, file: String = "", expect: (product: String?, sha256: String)? = nil) throws -> FlashPackage {
        let product = try product(fwsc)
        guard product.range(of: #"^[A-Za-z0-9_-]{1,20}$"#, options: .regularExpression) != nil else { throw Problem.notFM1 }   // (another file: its marker bytes are noise)
        if let want = expect?.product {
            guard product == want else { throw Problem.mismatch(product) }
        } else if product.range(of: #"^FM-1_[79]\d\d$"#, options: .regularExpression) == nil {
            throw Problem.notOurs(product)
        }
        guard contains(fwsc, loaderMark) else { throw Problem.noLoader }
        let sha = sha256(fwsc)
        if let expect, !expect.sha256.isEmpty, sha != expect.sha256 { throw Problem.mismatch("sha256 \(sha.prefix(12))…") }
        return FlashPackage(title: title, file: file, product: product, sha256: sha, image: logicalImage(fwsc))
    }

    private static func contains(_ hay: [UInt8], _ needle: [UInt8]) -> Bool {
        guard let first = needle.first, hay.count >= needle.count else { return false }
        var i = 0
        while i <= hay.count - needle.count {
            if hay[i] == first, Array(hay[i..<(i + needle.count)]) == needle { return true }
            i += 1
        }
        return false
    }
}
