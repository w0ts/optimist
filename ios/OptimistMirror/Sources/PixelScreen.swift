// SPDX-License-Identifier: GPL-3.0-only
// The mirrored screen with square, sharp pixels at any size (the user, 2026-10-29: "I am not sure I like the interpolated
// rendering... can we keep it sharp?"). The picture is scaled up here, by a whole number of DEVICE pixels, nearest neighbour, and
// laid out 1:1 on the device's pixel grid, so nothing downstream (SwiftUI's resizable image, the Catalyst window's scale, a
// rotation) has a fractional scale left to smooth. Where less than 240 device pixels are free (a tiny window) the picture is
// shrunk, nearest neighbour as well: the one case without whole pixels.
import SwiftUI

enum PixelScale {
    /// The whole multiple of the picture's side that fits `device` pixels (0 when it does not fit even once)
    static func factor(device: CGFloat, side: Int) -> Int {
        guard device.isFinite, device > 0 else { return 0 }
        return min(16, Int(device.rounded(.down)) / side)
    }

    /// `image` drawn `factor` times as large, every pixel a block of factor x factor (nearest neighbour, no smoothing)
    static func enlarged(_ image: CGImage, to pixels: Int) -> CGImage? {
        guard pixels > 0,
              let ctx = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue) else { return nil }
        ctx.interpolationQuality = .none
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: pixels, height: pixels))
        return ctx.makeImage()
    }
}

struct PixelScreen: View {
    let image: CGImage
    @Environment(\.displayScale) private var scale

    var body: some View {
        GeometryReader { g in
            let free = min(g.size.width, g.size.height) * scale          // device pixels free
            let k = PixelScale.factor(device: free, side: image.width)
            if k >= 1, let big = PixelScale.enlarged(image, to: image.width * k) {
                // 1:1: the image's own scale is the display's, so its points are whole device pixels; centred on the grid
                Image(decorative: big, scale: scale)
                    .interpolation(.none)
                    .frame(width: g.size.width, height: g.size.height)
            } else {
                Image(decorative: image, scale: 1)           // (smaller than the picture: shrunk, nearest neighbour)
                    .resizable()
                    .interpolation(.none)
                    .aspectRatio(1, contentMode: .fit)
                    .frame(width: g.size.width, height: g.size.height)
            }
        }
    }
}
