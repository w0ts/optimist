// SPDX-License-Identifier: GPL-3.0-only
// The screen mirrored: the FM-1's panel writes (cmd 106 pushes, firmware/src/io/editor/ed_mirror_enc.h) applied to a
// 240 x 240 RGB565 frame buffer. The same decoder as tests/mirror_dec.h (web/EDITOR_PROTOCOL.md "The screen mirrored").

struct MirrorDecoder {
    static let side = 240
    static let cmd: UInt8 = 106

    private(set) var pixels = [UInt16](repeating: 0, count: side * side)
    private var rect = (x: 0, y: 0, w: 0, h: 0)
    private var at = 0
    private var slots = [UInt16](repeating: 0, count: 7)
    private var next = 1

    /// The reply to a MIRROR request (F0 7D 46 4C 6A on version w w h h F7), or nil for anything else.
    static func reply(_ f: [UInt8]) -> (on: Bool, version: Int)? {
        guard f.count == 12, isMirror(f), f[5] <= 1 else { return nil }
        return (f[5] == 1, Int(f[6]))
    }

    static func isMirror(_ f: [UInt8]) -> Bool {
        f.count >= 7 && f[0] == 0xF0 && f[1] == 0x7D && f[2] == 0x46 && f[3] == 0x4C && f[4] == cmd && f.last == 0xF7
    }

    /// One SysEx frame (F0 .. F7). True when it was a panel write and was applied.
    @discardableResult
    mutating func apply(_ f: [UInt8]) -> Bool {
        guard Self.isMirror(f), Self.reply(f) == nil else { return false }
        let n = f.count - 1
        let op = f[5]
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
            let t = Int(f[i])
            i += 1
            var len = (t >> 3) + 1
            var s = t & 7
            if t >> 3 == 15 {
                guard i + 2 <= n else { return false }
                len = 16 + two(i)
                i += 2
            }
            if s == 7 {
                guard i + 3 <= n else { return false }
                slots[next] = colour(i)
                s = next
                next = next < 6 ? next + 1 : 1
                i += 3
            }
            fill(slots[s], len)
        }
        return true
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
