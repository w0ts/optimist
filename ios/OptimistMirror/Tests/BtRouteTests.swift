// SPDX-License-Identifier: GPL-3.0-only
// The editor's way over Bluetooth: the transport choice (EditorRoute), the waits and retries (BtPolicy) and the chunking of a
// project written through BK_DATA (every request under the firmware's 320 bytes, ed_mirror.c MBR_BIGN).
import XCTest
@testable import OptimistMirror

final class BtRouteTests: XCTestCase {
    private func info(_ names: [String]) -> DeviceInfo {
        var i = DeviceInfo()
        i.features = Dictionary(uniqueKeysWithValues: names.map { n in
            let u = n.lastIndex(of: "_")!
            return (String(n[..<u]), Int(n[n.index(after: u)...])!)
        })
        return i
    }

    func testNoPortIsNone() {
        XCTAssertEqual(EditorRoute.choose(searching: true, bluetooth: false, info: nil, infoTried: false), .none)
        XCTAssertEqual(EditorRoute.choose(searching: true, bluetooth: true, info: nil, infoTried: false), .none)
    }

    func testUsbCarriesEverything() {
        let r = EditorRoute.choose(searching: false, bluetooth: false, info: nil, infoTried: false)
        XCTAssertEqual(r, .usb)
        for f in [Feature.backup, .library, .projects, .patterns, .proj] { XCTAssertTrue(r.carries(f), f.name) }
    }

    func testBluetoothWithBtprojCarriesTheProjectsOnly() {
        let i = info(["opti_backup_4", "opti_proj_1", "opti_projects_1", "opti_library_1", "opti_mirbt_1", "opti_btproj_1"])
        let r = EditorRoute.choose(searching: false, bluetooth: true, info: i, infoTried: true)
        XCTAssertEqual(r, .bluetooth)
        XCTAssertTrue(r.carries(.proj))
        for f in [Feature.backup, .library, .projects, .patterns] { XCTAssertFalse(r.carries(f), f.name) }   // the USB message stays for these
    }

    /// The Projects page over Bluetooth with opti_btproj_1: its slots carried, no banner; the parts that need the cable said in
    /// one grey line under them (the user, 2026-10-31: the page read as an error over a working Bluetooth link)
    func testProjectsPageOverBluetoothHasNoBanner() {
        let i = info(["opti_proj_1", "opti_projects_1", "opti_mirbt_1", "opti_btproj_1"])
        let r = EditorRoute.choose(searching: false, bluetooth: true, info: i, infoTried: true)
        XCTAssertTrue(r.carries(.proj))
        XCTAssertNil(r.usbOnlyLine(.proj))
        XCTAssertEqual(r.usbOnlyLine(.projects), EditorRoute.usbOnlyLine)
        XCTAssertNil(EditorRoute.usb.usbOnlyLine(.projects))
        XCTAssertNil(EditorRoute.needsUSB.usbOnlyLine(.projects))
    }

    func testBluetoothWithoutBtprojNeedsUsb() {
        let i = info(["opti_backup_4", "opti_proj_1", "opti_mirbt_1"])
        XCTAssertEqual(EditorRoute.choose(searching: false, bluetooth: true, info: i, infoTried: true), .needsUSB)
        XCTAssertFalse(EditorRoute.needsUSB.carries(.proj))
        // another version of the feature is not the one this app reads
        XCTAssertEqual(EditorRoute.choose(searching: false, bluetooth: true, info: info(["opti_btproj_2"]), infoTried: true), .needsUSB)
    }

    func testBluetoothAsksFirstThenNeedsUsbWhenInfoIsSilent() {
        XCTAssertEqual(EditorRoute.choose(searching: false, bluetooth: true, info: nil, infoTried: false), .asking)
        XCTAssertEqual(EditorRoute.choose(searching: false, bluetooth: true, info: nil, infoTried: true), .needsUSB)
        XCTAssertFalse(EditorRoute.asking.carries(.proj))
    }

    func testInfoTag67ListsBtproj() throws {
        let names = Array("opti_clip_1 opti_btproj_1 ".utf8)
        let f = DeviceInfo.features(names[...])
        XCTAssertEqual(f[Feature.btproj.name], Feature.btproj.version)
    }

    func testPolicyLengthensWaitsAndKeepsZeroRetries() {
        let usb = (timeout: 0.3, retries: 1)
        let a = BtPolicy.apply(.pjOp, timeout: usb.timeout, retries: usb.retries)
        XCTAssertEqual(a.timeout, BtPolicy.replyWait)
        XCTAssertEqual(a.retries, BtPolicy.minRetries)
        let save = BtPolicy.apply(.pjOp, timeout: 10, retries: 0)          // a flash write: its own long wait, never sent twice
        XCTAssertEqual(save.timeout, 10)
        XCTAssertEqual(save.retries, 0)
        let data = BtPolicy.apply(.bkData, timeout: 1.5, retries: 1)       // the chunk waits for the firmware's one long slot
        XCTAssertEqual(data.timeout, BtPolicy.longReplyWait)
        XCTAssertEqual(data.retries, BtPolicy.minRetries)
        XCTAssertEqual(BtPolicy.apply(.bkRead, timeout: 1.5, retries: 3).retries, 3)
    }

    func testChunksCoverTheObjectInOrder() {
        let c = BtPolicy.chunks(count: 600)
        XCTAssertEqual(c, [0..<256, 256..<512, 512..<600])
        XCTAssertTrue(BtPolicy.chunks(count: 0).isEmpty)
        XCTAssertEqual(BtPolicy.chunks(count: 256), [0..<256])
    }

    func testABkDataChunkFitsTheFirmwaresLongSlot() throws {
        let chunk = [UInt8](repeating: 0xFF, count: Ops.chunk)            // 0xFF: the most 7-bit groups' top bits
        let args = [UInt8(5)] + Wire.wr7(0, 3) + Wire.wr7(Int(Wire.crc32(chunk)), 5) + Wire.pack7(chunk)
        XCTAssertEqual(BtPolicy.requestLength(.bkData, args), 306)
        XCTAssertLessThanOrEqual(BtPolicy.requestLength(.bkData, args), BtPolicy.maxRequest)
        XCTAssertEqual(try Wire.frame(Cmd.bkData.rawValue, args).count, 308)   // (F0 .. F7)
    }

    /// a transport that records each request's size and that no two run at once (the firmware has one long slot)
    private final class Recorder: Transport {
        let inner: FakeFM1
        var sizes: [(Cmd, Int)] = []
        var running = 0, overlap = false
        init(_ f: FakeFM1) { inner = f }
        func exchange(_ cmd: Cmd, _ args: [UInt8], timeout: TimeInterval, retries: Int) async throws -> [UInt8] {
            running += 1
            if running > 1 { overlap = true }
            defer { running -= 1 }
            sizes.append((cmd, BtPolicy.requestLength(cmd, args)))
            return try await inner.exchange(cmd, args, timeout: timeout, retries: retries)
        }
    }

    func testAProjectWrittenInChunksStaysUnderTheLimitOneAtATime() async throws {
        let fake = FakeFM1(objs: [.init(tag: "P01 ", kind: 3, data: [1, 2, 3])])
        let rec = Recorder(fake)
        let data = (0..<1000).map { UInt8(($0 &* 7) & 255) }
        let l = try await Ops.bkListAll(rec)
        try await Ops.writeObject(rec, index: l.objs[0].i, tag: l.objs[0].tag, data: data)
        let chunks = rec.sizes.filter { $0.0 == .bkData }
        XCTAssertEqual(chunks.count, BtPolicy.chunks(count: data.count).count)
        XCTAssertTrue(chunks.allSatisfy { $0.1 <= BtPolicy.maxRequest })
        XCTAssertFalse(rec.overlap)
        XCTAssertEqual(fake.objs[0].data, data)
    }

    // MARK: - which port the link takes (MirrorLink.pick / launchPort)

    private func port(_ n: String, bt: Bool, on: Bool = true) -> MirrorLink.PortSeen { .init(name: n, bluetooth: bt, online: on) }

    /// The cable wins: the FM-1 on USB while the app held its Bluetooth port (picked in the sheet) stayed on Bluetooth (the user,
    /// 2026-10-31: ioreg showed FM1-E040 on USB, Projects said Bluetooth). Ports as the Mac lists them then.
    func testUSBWinsOverBluetoothPicked() {
        let ports = [port("Optimist", bt: false), port("FM1-E040 Bluetooth", bt: true)]
        XCTAssertEqual(MirrorLink.pick(ports, chosen: "FM1-E040 Bluetooth"), 0)
        XCTAssertEqual(MirrorLink.pick(ports, chosen: ""), 0)
        XCTAssertEqual(MirrorLink.pick(Array(ports.reversed()), chosen: "FM1-E040 Bluetooth"), 1)
        XCTAssertEqual(MirrorLink.pick([ports[1]], chosen: "FM1-E040 Bluetooth"), 0)                 // (no cable: Bluetooth)
        XCTAssertEqual(MirrorLink.pick([port("FM1-E040", bt: false, on: false), ports[1]], chosen: "FM1-E040 Bluetooth"), 1)
        XCTAssertEqual(MirrorLink.pick(ports + [port("Launchkey", bt: false)], chosen: "Launchkey"), 2)   // (other gear by hand)
    }

    /// At launch a port already connected in the system is taken; a Bluetooth port the Mac keeps listed offline is not
    func testLaunchTakesOnlyAPortConnected() {
        XCTAssertEqual(MirrorLink.launchPort([port("IAC Driver Bus 1", bt: false), port("FM1-E040 Bluetooth", bt: true)], chosen: ""), 1)
        XCTAssertNil(MirrorLink.launchPort([port("FM1-E040 Bluetooth", bt: true, on: false)], chosen: ""))
        XCTAssertNil(MirrorLink.launchPort([port("IAC Driver Bus 1", bt: false)], chosen: ""))
        XCTAssertEqual(MirrorLink.launchPort([port("FM1-E040 Bluetooth", bt: true), port("FM1-E040", bt: false)], chosen: ""), 1)
    }
}
