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
        let reply: [UInt8] = [0xF0, 0x7D, 0x46, 0x4C, 106, 1, 2, 112, 1, 112, 1, 0xF7]
        XCTAssertEqual(MirrorDecoder.reply(reply)?.version, MirrorDecoder.version)
        XCTAssertEqual(MirrorDecoder.reply(reply)?.on, true)
        var dec = MirrorDecoder()
        XCTAssertFalse(dec.apply(reply))
    }

    func testFillClipsAndLongRun() {
        var dec = MirrorDecoder()
        // FILL 240 x 240 of 0xFFFF
        XCTAssertTrue(dec.apply([0xF0, 0x7D, 0x46, 0x4C, 106, 0, 0, 0, 0, 0, 112, 1, 112, 1, 127, 127, 3, 0xF7]))
        XCTAssertTrue(dec.pixels.allSatisfy { $0 == 0xFFFF })
        // BLIT 240 x 2 at y 238: one long run of slot 0 (black), 480 = 14 + 466 (466 & 63 = 18, | 64; 466 >> 6 = 7)
        XCTAssertTrue(dec.apply([0xF0, 0x7D, 0x46, 0x4C, 106, 1, 0, 0, 110, 1, 112, 1, 2, 0, 13 << 3, 82, 7, 0xF7]))
        XCTAssertEqual(dec.pixels[237 * 240], 0xFFFF)
        XCTAssertEqual(dec.pixels[238 * 240], 0)
        XCTAssertEqual(dec.pixels[240 * 240 - 1], 0)
    }

    func testRowAboveAndBitmap() {
        var dec = MirrorDecoder()
        // BLIT 4 x 2 at 0, 0: 4 pixels of 0x1234 (a new colour: slot 1), then 4 pixels as the row above (15 << 3 | 3)
        XCTAssertTrue(dec.apply([0xF0, 0x7D, 0x46, 0x4C, 106, 1, 0, 0, 0, 0, 4, 0, 2, 0, 3 << 3 | 7, 52, 36, 0, 15 << 3 | 3, 0xF7]))
        XCTAssertEqual(dec.pixels[0], 0x1234)
        XCTAssertEqual(dec.pixels[240], 0x1234)
        XCTAssertEqual(dec.pixels[243], 0x1234)
        XCTAssertEqual(dec.pixels[244], 0)
        // BLIT 14 x 1 at 0, 10: a bitmap, 1s 0xFFFF (new), 0s slot 0, 2 bytes: 1010101 0000001
        XCTAssertTrue(dec.apply([0xF0, 0x7D, 0x46, 0x4C, 106, 1, 0, 0, 10, 0, 14, 0, 1, 0, 14 << 3 | 7, 127, 127, 3, 1 << 3, 85, 1, 0xF7]))
        let row = Array(dec.pixels[2400..<2414])
        XCTAssertEqual(row, [0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0, 0, 0, 0, 0, 0xFFFF])
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

final class FeatureTests: XCTestCase {
    /// INFO's tag 66 (ed_opti.h): what the build answers, by name and version; a feature only when its exact version is listed
    func testFeaturesFromInfo() throws {
        let list = Array("opti_backup_4 opti_mirror_2 opti_library_1 opti_projects_1 ".utf8)
        var a: [UInt8] = Array("FELUCCA 0.1".utf8) + [0, 0, 0, 0, 64, 0, 0, 6]   // no engine, no track, protocol 6
        a += [0x66, UInt8(list.count)] + list
        let i = try DeviceInfo.parse(a)
        XCTAssertTrue(i.has(.backup) && i.has(.mirror) && i.has(.library) && i.has(.projects))
        XCTAssertFalse(i.has(.patterns))
        XCTAssertEqual(i.features["opti_backup"], 4)
        let other = try DeviceInfo.parse(Array("FELUCCA 1.0.3".utf8) + [0, 0, 0, 0, 64, 0, 0, 6])
        XCTAssertTrue(other.features.isEmpty)
        XCTAssertFalse(other.has(.backup))
        XCTAssertFalse(try DeviceInfo.parse(Array("X".utf8) + [0, 0, 0, 0, 64, 0, 0, 6, 0x66, 13] + Array("opti_backup_9".utf8)).has(.backup))
    }
}
