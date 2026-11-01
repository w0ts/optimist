// SPDX-License-Identifier: GPL-3.0-only
// The screen mirrored: the FM-1's panel writes (cmd 106 pushes, firmware/src/io/editor/ed_mirror_enc.h) applied to a
// 240 x 240 RGB565 frame buffer. The same decoder as tests/mirror_dec.h (web/EDITOR_PROTOCOL.md "The screen mirrored").

struct MirrorDecoder {
    static let side = 240
    static let cmd: UInt8 = 106
    static let version = 2        // the token codes below (ed_mirror_enc.h MIR_VERSION); a firmware with another is refused

    private(set) var pixels = [UInt16](repeating: 0, count: side * side)
#if DEBUG
    /// The store screenshots (DEBUG builds only): a panel as given
    mutating func show(_ px: [UInt16]) { if px.count == pixels.count { pixels = px } }
#endif
    private var rect = (x: 0, y: 0, w: 0, h: 0)
    private var at = 0
    private var slots = [UInt16](repeating: 0, count: 7)
    private var next = 1
    /// Checked frames (ops 8, 9, 10: asked with 106 bit 3, ed_mirror_enc.h): the next seq, one seen, a MORE now would go astray
    private var seq = 0, haveSeq = false, broken = false
    /// Checked frames dropped or lost (a sum failed, a gap in seq): the picture is not whole, the link asks for it again
    private(set) var lost = 0
    /// The kinds (the link's log): a sum failed, a gap in seq, a MORE dropped after one, a frame had already
    private(set) var lostSum = 0, lostGap = 0, lostMore = 0, again = 0
    /// The runs of seqs lost since `takeLost`, each asked for alone (106 bit 4 with opti_mirfix_1: only their rectangles again;
    /// merging two runs would ask for the frames between them, which came whole)
    private var fixRuns: [(lo: Int, hi: Int)] = []
    mutating func takeLost() -> [(lo: Int, hi: Int)] {
        defer { fixRuns = [] }
        return fixRuns
    }
    private mutating func lostRange(_ lo: Int, _ hi: Int) {
        if let l = fixRuns.last, lo & 127 == (l.hi + 1) & 127 {
            fixRuns[fixRuns.count - 1].hi = hi & 127
        } else if fixRuns.count >= 8 {
            fixRuns[fixRuns.count - 1].hi = hi & 127   // (many: the last reaches over, more than lost)
        } else {
            fixRuns.append((lo & 127, hi & 127))
        }
    }
    static let checkedBit: UInt8 = 8
    static let fixBit: UInt8 = 16     // 106 bit 4: seq lo, hi follow (lost frames, opti_mirfix_1)
    static let ackBit: UInt8 = 32     // 106 bit 5: the losses (7 bit, free-running), the next seq follow (opti_miraimd_1)
    static let quietBit: UInt8 = 64   // 106 bit 6 with bit 5: an ACK alone, the FM-1 sends no reply (about 5 a second)
    /// The keep-alive's report (106 bit 5): checked frames lost so far and the next seq expected, 7 bit each
    var ack: [UInt8] { [UInt8(lost & 127), UInt8(seq & 127)] }

    /// The reply to a MIRROR request (F0 7D 46 4C 6A on version w w h h F7), or nil for anything else.
    static func reply(_ f: [UInt8]) -> (on: Bool, version: Int)? {
        guard f.count == 12, isMirror(f), f[5] <= 1 else { return nil }
        return (f[5] == 1, Int(f[6]))
    }

    /// Op 3 LINK, after a Bluetooth reply: the radio's data length (octets), ATT MTU, interval (x 1.25 ms), then the
    /// central's LL_LENGTH values (MaxRxOctets, MaxRxTime, MaxTxOctets, MaxTxTime; empty from a firmware before them).
    static func link(_ f: [UInt8]) -> (octets: Int, mtu: Int, interval: Int, central: [Int])? {
        guard f.count == 13 || f.count == 21, isMirror(f), f[5] == 3 else { return nil }
        let v = { (i: Int) in Int(f[i]) | Int(f[i + 1]) << 7 }
        return (v(6), v(8), v(10), f.count == 21 ? [v(12), v(14), v(16), v(18)] : [])
    }

    /// Op 5 STATS, after LINK with checked frames: whole panels for a lag, an ask, a repair's seq no longer known; the UI's pages
    /// drawn whole; repaints in place for a lag; repairs; bytes sent; bytes within 100 ms of a repair (3 x 7 bit each, free-running)
    static func stats(_ f: [UInt8]) -> [Int]? {
        guard f.count == 31, isMirror(f), f[5] == 5 else { return nil }
        var v: [Int] = []
        for k in 0..<8 {
            let lo = Int(f[6 + 3 * k]), mid = Int(f[7 + 3 * k]), hi = Int(f[8 + 3 * k])
            v.append(lo | mid << 7 | hi << 14)
        }
        return v
    }

    /// Op 6 PACE, after STATS with opti_miraimd_1: the FM-1's budget for the Bluetooth stream (bytes a second), its cuts (a
    /// loss: halved), its raises, the frames it refused (the screen repainted in place later instead), then (since the ACK's
    /// delay) the last ACK's delay in ms and the cuts for a delay; 3 x 7 bit each, free-running
    static func pace(_ f: [UInt8]) -> [Int]? {
        guard f.count == 19 || f.count == 25, isMirror(f), f[5] == 6 else { return nil }
        var v: [Int] = []
        for k in 0..<(f.count - 7) / 3 {
            let lo = Int(f[6 + 3 * k]), mid = Int(f[7 + 3 * k]), hi = Int(f[8 + 3 * k])
            v.append(lo | mid << 7 | hi << 14)
        }
        return v
    }

    static func isMirror(_ f: [UInt8]) -> Bool {
        f.count >= 7 && f[0] == 0xF0 && f[1] == 0x7D && f[2] == 0x46 && f[3] == 0x4C && f[4] == cmd && f.last == 0xF7
    }

    /// One SysEx frame (F0 .. F7). True when it was a panel write and was applied.
    @discardableResult
    mutating func apply(_ f: [UInt8]) -> Bool {
        guard Self.isMirror(f), Self.reply(f) == nil else { return false }
        var n = f.count - 1
        var op = f[5]
        if op >= 8 && op <= 10 {        // checked: seq, then Fletcher sums A and B of 7D .. seq, before F7
            guard n >= 9 else { lost += 1; lostSum += 1; broken = true; return false }
            var a = 0, b = 0
            for k in 1..<(n - 2) { a += Int(f[k]); b += a }
            guard a & 127 == Int(f[n - 2]), b & 127 == Int(f[n - 1]) else { lost += 1; lostSum += 1; broken = true; return false }
            let q = Int(f[n - 3])
            if haveSeq && q != seq {
                if ((seq - q) & 127) - 1 < 32 { again += 1; return false }   // (a frame again, already had: nothing)
                lost += 1; lostGap += 1; broken = true                         // (frames lost before this one)
                lostRange(seq, q - 1)
            }
            seq = (q + 1) & 127
            haveSeq = true
            if op == 10 && broken {                              // (its BLIT lost: it would paint into another rectangle)
                lost += 1; lostMore += 1
                lostRange(q, q)
                return false
            }
            broken = false
            op -= 8
            n -= 3
        }
        var i = 6
        func two(_ k: Int) -> Int { Int(f[k]) | Int(f[k + 1]) << 7 }
        func colour(_ k: Int) -> UInt16 { UInt16(truncatingIfNeeded: two(k) | Int(f[k + 2]) << 14) }
        if op <= 1 {
            guard i + 8 <= n else { return false }
            rect = (two(i), two(i + 2), two(i + 4), two(i + 6))
            at = 0
            i += 8
        }
        switch op {
        case 0:
            guard i + 3 <= n else { return false }
            fill(colour(i), rect.w * rect.h)
            return true
        case 1:
            slots = [UInt16](repeating: 0, count: 7)
            next = 1
        case 2:
            break
        default:
            return false
        }
        while i < n {
            let t = Int(f[i]), code = t >> 3
            i += 1
            var len = code + 1
            var s = t & 7
            if code == 13 || (code == 15 && s == 7) {   // a length: 1 byte below 64, else (L & 63) | 64, L >> 6
                guard i < n, f[i] & 64 == 0 || i + 1 < n else { return false }
                let l = f[i] & 64 == 0 ? Int(f[i]) : Int(f[i] & 63) | Int(f[i + 1]) << 6
                len = (code == 13 ? 14 : 8) + l
                i += f[i] & 64 == 0 ? 1 : 2
            }
            if code == 15 {                             // the pixels one row up
                guard at >= rect.w else { return false }
                up(s < 7 ? s + 1 : len)
                continue
            }
            if s == 7 {
                guard i + 3 <= n else { return false }
                slots[next] = colour(i)
                s = next
                next = next < 6 ? next + 1 : 1
                i += 3
            }
            if code == 14 {                             // a bitmap: g << 3 | the 0s' slot, g + 1 bytes of 7 pixels, bit 6 first
                guard i < n, f[i] & 7 != 7 else { return false }
                let g = Int(f[i] >> 3) + 1, bg = slots[Int(f[i] & 7)], fg = slots[s]
                guard i + 1 + g <= n else { return false }
                for k in 0..<g {
                    for j in 0..<7 { fill((f[i + 1 + k] >> (6 - j)) & 1 == 1 ? fg : bg, 1) }
                }
                i += 1 + g
                continue
            }
            fill(slots[s], len)
        }
        return true
    }

    /// count pixels each as the one a row up (inside the rectangle: its second row on)
    private mutating func up(_ count: Int) {
        let end = min(at + count, rect.w * rect.h)
        pixels.withUnsafeMutableBufferPointer { fb in
            while at < end {
                let x = rect.x + at % rect.w, y = rect.y + at / rect.w
                if x < Self.side, y < Self.side, y >= 1 { fb[y * Self.side + x] = fb[(y - 1) * Self.side + x] }
                at += 1
            }
        }
    }

    private mutating func fill(_ c: UInt16, _ count: Int) {
        let end = min(at + count, rect.w * rect.h)
        guard rect.w > 0, at < end else { return }
        pixels.withUnsafeMutableBufferPointer { fb in
            var k = at
            while k < end {
                let row = k / rect.w, col = k % rect.w
                let x = rect.x + col, y = rect.y + row
                let span = min(rect.w - col, end - k)   // the rest of this row
                if y < Self.side {
                    let x1 = min(x + span, Self.side)
                    if x < x1 {
                        let base = y * Self.side
                        for p in (base + x)..<(base + x1) { fb[p] = c }
                    }
                }
                k += span
            }
        }
        at = end
    }
}

/// SysEx out of UMP: Data 64 (message type 3) packets back into F0 .. F7 frames.
struct SysExAssembler {
    private var frame: [UInt8] = []
    private var open = false

    /// Two words of one Data 64 packet; returns a frame when one is complete.
    mutating func take(_ w0: UInt32, _ w1: UInt32) -> [UInt8]? {
        guard w0 >> 28 == 3 else { return nil }
        let status = (w0 >> 20) & 15, n = Int((w0 >> 16) & 15)
        let bytes = [UInt8(truncatingIfNeeded: w0 >> 8), UInt8(truncatingIfNeeded: w0),
                     UInt8(truncatingIfNeeded: w1 >> 24), UInt8(truncatingIfNeeded: w1 >> 16),
                     UInt8(truncatingIfNeeded: w1 >> 8), UInt8(truncatingIfNeeded: w1)].prefix(min(n, 6))
        switch status {
        case 0:                               // complete in one
            open = false
            return [0xF0] + bytes + [0xF7]
        case 1:                               // start
            frame = [0xF0] + bytes
            open = true
        case 2:                               // continue
            if open { frame += bytes }
        case 3:                               // end
            guard open else { return nil }
            open = false
            return frame + bytes + [0xF7]
        default:
            break
        }
        return nil
    }

    /// A SysEx message's bytes between F0 and F7 as Data 64 packets (group 0).
    static func packets(_ payload: [UInt8]) -> [UInt32] {
        var words: [UInt32] = []
        var k = 0
        repeat {
            let n = min(6, payload.count - k)
            let status: UInt32 = payload.count <= 6 ? 0 : k == 0 ? 1 : k + n >= payload.count ? 3 : 2
            var b = [UInt8](repeating: 0, count: 6)
            for j in 0..<n { b[j] = payload[k + j] }
            words.append(3 << 28 | status << 20 | UInt32(n) << 16 | UInt32(b[0]) << 8 | UInt32(b[1]))
            words.append(UInt32(b[2]) << 24 | UInt32(b[3]) << 16 | UInt32(b[4]) << 8 | UInt32(b[5]))
            k += n
        } while k < payload.count
        return words
    }
}
