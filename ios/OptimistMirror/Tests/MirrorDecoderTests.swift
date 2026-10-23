// SPDX-License-Identifier: GPL-3.0-only
// The decoder against the firmware's encoder: Fixtures/screen.sysex is the frames tests/mirror_test.c sent for a panel drawn
// whole (MIRROR_FIXTURE), screen.rgb565 that panel's pixels; decoded, they are the same. Also the UMP SysEx round trip.
import XCTest
@testable import OptimistMirror

final class MirrorDecoderTests: XCTestCase {
    private func fixture(_ name: String) throws -> [UInt8] {
        let url = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "screen", withExtension: name))
        return [UInt8](try Data(contentsOf: url))
    }

    private func frames(_ bytes: [UInt8]) -> [[UInt8]] {
        var out: [[UInt8]] = [], cur: [UInt8] = []
        for b in bytes {
            if b == 0xF0 { cur = [] }
            cur.append(b)
            if b == 0xF7 { out.append(cur) }
        }
        return out
    }

    func testFixtureDecodesToThePanel() throws {
        let raw = try fixture("rgb565")
        let want = (0..<raw.count / 2).map { UInt16(raw[2 * $0]) | UInt16(raw[2 * $0 + 1]) << 8 }
        var dec = MirrorDecoder()
        var applied = 0
        for f in frames(try fixture("sysex")) where dec.apply(f) { applied += 1 }
        XCTAssertGreaterThan(applied, 40)
        XCTAssertEqual(dec.pixels.count, want.count)
        XCTAssertEqual(zip(dec.pixels, want).filter { $0 != $1 }.count, 0)
    }

    func testFixtureThroughUMP() throws {
        // every frame cut into Data 64 packets and put back, as CoreMIDI hands them over
        var asm = SysExAssembler()
        for f in frames(try fixture("sysex")) {
            let words = SysExAssembler.packets(Array(f.dropFirst().dropLast()))
            var got: [UInt8]?
            for k in stride(from: 0, to: words.count, by: 2) {
                if let g = asm.take(words[k], words[k + 1]) { got = g }
            }
            XCTAssertEqual(got, f)
        }
    }

    func testReplyIsNotAWrite() {
        let reply: [UInt8] = [0xF0, 0x7D, 0x46, 0x4C, 106, 1, 1, 112, 1, 112, 1, 0xF7]
        XCTAssertEqual(MirrorDecoder.reply(reply)?.on, true)
        var dec = MirrorDecoder()
        XCTAssertFalse(dec.apply(reply))
    }

    func testFillClipsAndLongRun() {
        var dec = MirrorDecoder()
        // FILL 240 x 240 of 0xFFFF
        XCTAssertTrue(dec.apply([0xF0, 0x7D, 0x46, 0x4C, 106, 0, 0, 0, 0, 0, 112, 1, 112, 1, 127, 127, 3, 0xF7]))
        XCTAssertTrue(dec.pixels.allSatisfy { $0 == 0xFFFF })
        // BLIT 240 x 2 at y 238: one long run of slot 0 (black), 480 = 16 + 464 (80 + 3 x 128)
        XCTAssertTrue(dec.apply([0xF0, 0x7D, 0x46, 0x4C, 106, 1, 0, 0, 110, 1, 112, 1, 2, 0, 15 << 3, 80, 3, 0xF7]))
        XCTAssertEqual(dec.pixels[237 * 240], 0xFFFF)
        XCTAssertEqual(dec.pixels[238 * 240], 0)
        XCTAssertEqual(dec.pixels[240 * 240 - 1], 0)
    }

    func testLinkFrame() {
        let f: [UInt8] = [0xF0, 0x7D, 0x46, 0x4C, 106, 3, 251 & 127, 251 >> 7, 185 & 127, 185 >> 7, 9, 0, 0xF7]
        let l = MirrorDecoder.link(f)
        XCTAssertEqual(l?.octets, 251)
        XCTAssertEqual(l?.mtu, 185)
        XCTAssertEqual(l?.interval, 9)
        XCTAssertEqual(l?.central, [])
        let g: [UInt8] = [0xF0, 0x7D, 0x46, 0x4C, 106, 3, 50, 0, 119, 1, 6, 0, 123, 1, 72, 16, 50, 0, 0, 4, 0xF7]
        XCTAssertEqual(MirrorDecoder.link(g)?.central, [251, 2120, 50, 512])
        XCTAssertNil(MirrorDecoder.reply(f))
        var dec = MirrorDecoder()
        XCTAssertFalse(dec.apply(f))
    }
}
