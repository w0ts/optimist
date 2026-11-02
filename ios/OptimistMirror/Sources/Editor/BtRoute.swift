// SPDX-License-Identifier: GPL-3.0-only
// Which way the editor screens reach the FM-1: the USB cable, or Bluetooth when INFO names opti_btproj_1 (firmware/src/felucca.c MBR_PROJ,
// built with the editor and Bluetooth MIDI). Over Bluetooth the firmware takes the backup's 43..48 (the project library's
// reads and writes) and 110 PJ_OP only, one request at a time. Pure values: the tests run them without CoreMIDI.
import Foundation

enum EditorRoute: Equatable {
    case none          // no port
    case usb           // the cable: every screen
    case bluetooth     // Bluetooth, INFO lists opti_btproj_1: the project slots and the project library
    case asking        // Bluetooth, INFO not answered yet
    case needsUSB      // Bluetooth, and the firmware answers the mirror only (no INFO, or no opti_btproj_1)

    static func choose(searching: Bool, bluetooth: Bool, info: DeviceInfo?, infoTried: Bool) -> EditorRoute {
        if searching { return .none }
        if !bluetooth { return .usb }
        if let i = info { return i.has(.btproj) ? .bluetooth : .needsUSB }
        return infoTried ? .needsUSB : .asking
    }

    /// The names of the features that run over Bluetooth: PJ_OP (the slot list and moves) and, with it, the library's BK_* reads
    /// and writes. Sections (PROJECT), patterns, the preset bank, backup and restore, samples and the flash still need USB.
    static let bluetoothFeatures: Set<String> = [Feature.proj.name]

    /// Can this route send what feature `f` needs?
    func carries(_ f: Feature) -> Bool {
        switch self {
        case .usb: return true
        case .bluetooth: return Self.bluetoothFeatures.contains(f.name)
        case .none, .asking, .needsUSB: return false
        }
    }

    /// Bluetooth with the projects: what the Projects page says of the parts that need the cable (one small grey line under
    /// the slots, not a banner: the slots and the library work); nil when nothing is to be said
    static let usbOnlyLine = "Sections, patterns and backup need the USB cable."
    func usbOnlyLine(_ f: Feature) -> String? { self == .bluetooth && !carries(f) ? Self.usbOnlyLine : nil }
}

/// INFO over Bluetooth, asked by the mirror's link once a second beside its 106 (MirrorLink.tick): measured on the FM-1 while
/// the screen streams (2026-10-31), about 1 in 3 INFOs gets no reply (the firmware drops a reply that would start behind 4 KB of
/// screen, ed_mirror.c MIR_BLE_LAG) and 1 in 20 comes damaged (DeviceInfo.intact). So it is asked `tries` times until a whole
/// one comes, and given up `grace` ticks after the last ask (a reply comes up to ~1.4 s late).
enum BtAsk: Equatable {
    case none      // known: nothing to ask
    case ask       // send INFO with this tick's 106
    case wait      // asked enough: the last replies may still come
    case gaveUp    // no whole answer: a firmware that answers the mirror only there

    static let tries = 6
    static let grace = 2

    /// the step for this tick, `ticks` ticks already spent on INFO
    static func next(known: Bool, ticks: Int) -> BtAsk {
        if known { return .none }
        if ticks < tries { return .ask }
        return ticks < tries + grace ? .wait : .gaveUp
    }
}

/// Timeouts and retries over Bluetooth Low Energy: a reply waits behind the screen the firmware streams (up to 0.65 s) and the
/// connection interval, and a request that came while the firmware's single long-request slot was busy is dropped (the
/// client asks again), so each wait is longer and each request that may be repeated gets more tries than over USB.
enum BtPolicy {
    static let replyWait: TimeInterval = 1.5
    static let longReplyWait: TimeInterval = 3.0
    static let minRetries = 2
    /// the firmware's longest request between F0 and F7 (ed_mirror.c MBR_BIGN)
    static let maxRequest = 320

    /// a request the firmware holds in its one long slot (BK_DATA's chunk)
    static func isLong(_ cmd: Cmd) -> Bool { cmd == .bkData }

    /// the timeout and retries to use over Bluetooth for a request asked with (timeout, retries) for USB; retries 0 stays 0 (a
    /// request that must not be sent twice), and a longer timeout than this one is kept
    static func apply(_ cmd: Cmd, timeout: TimeInterval, retries: Int) -> (timeout: TimeInterval, retries: Int) {
        (max(timeout, isLong(cmd) ? longReplyWait : replyWait), retries > 0 ? max(retries, minRetries) : 0)
    }

    /// the bytes between F0 and F7 of a request (the 7D 46 4C header, the command and the arguments)
    static func requestLength(_ cmd: Cmd, _ args: [UInt8]) -> Int { Wire.header.count + 1 + args.count }

    /// the byte ranges BK_DATA sends an object in, `Ops.chunk` bytes at a time
    static func chunks(count: Int, size: Int = Ops.chunk) -> [Range<Int>] {
        stride(from: 0, to: count, by: size).map { $0..<min($0 + size, count) }
    }
}
