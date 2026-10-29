// SPDX-License-Identifier: GPL-3.0-only
// The mirrored screen is sharp: a 1-pixel checkerboard (black and white only) shown at fractional sizes and 2x/3x displays comes
// out with black and white only, no intermediate grey, and every block the same whole number of device pixels.
import XCTest
import SwiftUI
@testable import OptimistMirror

@MainActor
final class PixelScreenTests: XCTestCase {
    private func checker() -> CGImage {
        let s = MirrorDecoder.side
        var px = [UInt32](repeating: 0, count: s * s)
        for y in 0..<s { for x in 0..<s { px[y * s + x] = (x + y) % 2 == 0 ? 0xFFFF_FFFF : 0xFF00_0000 } }
        let data = px.withUnsafeBytes { Data($0) } as CFData
        return CGImage(width: s, height: s, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: s * 4, space: CGColorSpaceCreateDeviceRGB(),
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                       provider: CGDataProvider(data: data)!, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
    }

    /// The colours (as 0xRRGGBB) in a rendering, with how many pixels carry each
    private func colours(_ img: CGImage) -> [UInt32: Int] {
        var px = [UInt8](repeating: 0, count: img.width * img.height * 4)
        let ctx = CGContext(data: &px, width: img.width, height: img.height, bitsPerComponent: 8, bytesPerRow: img.width * 4,
                            space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.draw(img, in: CGRect(x: 0, y: 0, width: img.width, height: img.height))
        var out: [UInt32: Int] = [:]
        for i in stride(from: 0, to: px.count, by: 4) { out[UInt32(px[i]) << 16 | UInt32(px[i + 1]) << 8 | UInt32(px[i + 2]), default: 0] += 1 }
        return out
    }

    private func render<V: View>(_ v: V, scale: CGFloat) throws -> CGImage {
        let r = ImageRenderer(content: v)
        r.scale = scale
        return try XCTUnwrap(r.cgImage)
    }

    private func save(_ img: CGImage, _ name: String) {
        guard let dir = ProcessInfo.processInfo.environment["FM1_SHOTS_DIR"] else { return }
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        let url = URL(fileURLWithPath: dir).appendingPathComponent(name + ".png")
        guard let d = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil) else { return }
        CGImageDestinationAddImage(d, img, nil)
        CGImageDestinationFinalize(d)
    }

    /// Sizes in points that are not whole multiples of 240 device pixels
    private let sizes: [CGFloat] = [301.3, 390, 413.7, 500, 640.5, 777]

    func testSharpAtEverySizeAndScale() throws {
        for scale in [1.0, 2.0, 3.0] as [CGFloat] {
            for side in sizes {
                let img = try render(PixelScreen(image: checker()).frame(width: side, height: side).environment(\.displayScale, scale), scale: scale)
                let c = colours(img)
                if side * scale < 240 { continue }
                XCTAssertEqual(Set(c.keys), [0x000000, 0xFFFFFF], "grey at \(side) pt, \(scale)x: \(c.count) colours")
                save(img, "after-\(Int(scale))x-\(side)")
            }
        }
    }

    func testBlocksAreWholeDevicePixels() throws {
        // 3x, 700 pt = 2100 device pixels: 8 x 240 = 1920 pixels, so blocks of 8; black/white boundaries only every 8 pixels
        let img = try render(PixelScreen(image: checker()).frame(width: 700, height: 700).environment(\.displayScale, 3), scale: 3)
        XCTAssertEqual(img.width, 2100)
        let k = PixelScale.factor(device: 2100, side: 240)
        XCTAssertEqual(k, 8)
        var px = [UInt8](repeating: 0, count: img.width * img.height * 4)
        let ctx = CGContext(data: &px, width: img.width, height: img.height, bitsPerComponent: 8, bytesPerRow: img.width * 4,
                            space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.draw(img, in: CGRect(x: 0, y: 0, width: img.width, height: img.height))
        let mid = img.height / 2
        var runs: [Int] = [], run = 1
        for x in 1..<img.width {
            if px[(mid * img.width + x) * 4] == px[(mid * img.width + x - 1) * 4] { run += 1 } else { runs.append(run); run = 1 }
        }
        // the first and last runs are the black border; every one between is a block of k pixels
        XCTAssertTrue(runs.dropFirst().dropLast().allSatisfy { $0 == k }, "runs \(Set(runs))")
    }

    func testFactor() {
        XCTAssertEqual(PixelScale.factor(device: 239, side: 240), 0)
        XCTAssertEqual(PixelScale.factor(device: 240, side: 240), 1)
        XCTAssertEqual(PixelScale.factor(device: 1199, side: 240), 4)
        XCTAssertEqual(PixelScale.factor(device: .nan, side: 240), 0)
    }

    /// The smaller-than-picture case shrinks, nearest neighbour: still only the two colours of the source
    func testShrunkIsStillNearest() throws {
        let img = try render(PixelScreen(image: checker()).frame(width: 150, height: 150).environment(\.displayScale, 1), scale: 1)
        XCTAssertTrue(Set(colours(img).keys).isSubset(of: [0x000000, 0xFFFFFF]))
    }

    /// Before: the former view (a resizable image, .none, fitted to the free square), for the evidence of what was measured
    func testBaselineOfTheFormerView() throws {
        for scale in [2.0, 3.0] as [CGFloat] {
            for side in sizes {
                let old = Image(decorative: checker(), scale: 1).resizable().interpolation(.none).aspectRatio(1, contentMode: .fit).frame(width: side, height: side)
                let img = try render(old, scale: scale)
                save(img, "before-\(Int(scale))x-\(side)")
                print("BASELINE \(scale)x \(side)pt colours: \(colours(img).count)")
            }
        }
    }
}
