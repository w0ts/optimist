// SPDX-License-Identifier: GPL-3.0-only
// The Firmware tab: the update protocol's frames (against web/fm1ota.js's own output), the package checks, the list's parsing,
// and whole installs against a fake FM-1 that reads the image the way the device does (step 1, its loader, step 2, the return).
import XCTest
@testable import OptimistMirror

final class FlashTests: XCTestCase {
    // MARK: - frames

    func testPackRoundTrip() {
        let data = (0..<300).map { UInt8(truncatingIfNeeded: $0 &* 37) }
        XCTAssertEqual(FlashProtocol.unpack7(ArraySlice(FlashProtocol.pack7(data))).prefix(data.count), ArraySlice(data))
    }

    func testResponseMatchesWebInstaller() {
        // web/fm1ota.js response(0x00ABCDEF, [1, 2, 3, 250, 0x80], 2), printed by node
        let web: [UInt8] = [240, 0, 50, 65, 105, 0, 0, 0, 1, 111, 27, 47, 5, 80, 0, 0, 0, 1, 4, 12, 80, 15, 48, 4, 247]
        XCTAssertEqual(FlashProtocol.response(0x00AB_CDEF, [1, 2, 3, 250, 0x80], flags: 2), web)
    }

    func testRequestAndIdentity() {
        let r = FakeFlashDevice.request(0x1234_5678, 512, flags: 1)
        XCTAssertEqual(FlashProtocol.request(r), FlashProtocol.Request(flags: 1, address: 0x1234_5678, length: 512))
        var bad = r
        bad[9] ^= 1                     // (packed byte 8 = bits 56..62: the address's low byte, under the checksum)
        XCTAssertNil(FlashProtocol.request(bad))
        let id = FlashProtocol.identity(FakeFlashDevice.identity("ota-FM-1_700"))
        XCTAssertEqual(id, FlashProtocol.Identity(text: "ota-FM-1_700", model: "ota-FM-1", version: 700))
        XCTAssertTrue(id!.isLoader && id!.isOurLoader)
        XCTAssertFalse(FlashProtocol.identity(FakeFlashDevice.identity("ota-FM-1_015"))!.isOurLoader)
        XCTAssertFalse(FlashProtocol.identity(FakeFlashDevice.identity("FM-1_700"))!.isLoader)
    }

    // MARK: - packages

    func testPackageCheck() throws {
        let fwsc = FakeFlashDevice.package("FM-1_700", size: 4000)
        let p = try FlashPackage.check(fwsc, title: "t")
        XCTAssertEqual(p.product, "FM-1_700")
        XCTAssertEqual(p.image.count, fwsc.count - 20)
        XCTAssertEqual(Array(p.image[0..<0x2F]), Array(fwsc[0..<0x2F]))
        XCTAssertEqual(Array(p.image[0x2F..<0x5E]), Array(fwsc[0x30..<0x5F]))
        XCTAssertEqual(Array(p.image[(20 * 0x2F)...]), Array(fwsc[(20 * 0x30)...]))
        XCTAssertThrowsError(try FlashPackage.check(FakeFlashDevice.package("FM-1_015", size: 4000), title: "t")) {
            XCTAssertEqual($0 as? FlashPackage.Problem, .notOurs("FM-1_015"))
        }
        XCTAssertThrowsError(try FlashPackage.check(FakeFlashDevice.package("FM-1_700", size: 4000, loader: false), title: "t")) {
            XCTAssertEqual($0 as? FlashPackage.Problem, .noLoader)
        }
        XCTAssertThrowsError(try FlashPackage.check([1, 2, 3], title: "t")) { XCTAssertEqual($0 as? FlashPackage.Problem, .notFM1) }
        XCTAssertThrowsError(try FlashPackage.check([UInt8](repeating: 0x41, count: 4000), title: "t")) {   // (noise as identity)
            XCTAssertEqual($0 as? FlashPackage.Problem, .notFM1)
        }
        XCTAssertEqual(try FlashPackage.check(fwsc, title: "t", file: "optimist-0.1-synth.fwsc").file, "optimist-0.1-synth.fwsc")
        XCTAssertNoThrow(try FlashPackage.check(fwsc, title: "t", expect: ("FM-1_700", p.sha256)))
        XCTAssertNoThrow(try FlashPackage.check(fwsc, title: "t", expect: (nil, p.sha256)))
        XCTAssertThrowsError(try FlashPackage.check(fwsc, title: "t", expect: ("FM-1_701", p.sha256)))
        XCTAssertThrowsError(try FlashPackage.check(fwsc, title: "t", expect: (nil, String(repeating: "0", count: 64))))
    }

    // MARK: - lists

    func testSiteList() {
        let json = """
        {"version": "0.1", "packages": [
          {"id": "default", "title": "Synths", "pkg": "../../firmware/optimist-0.1-default.fwsc", "product": "FM-1_700", "release": "", "sha256": "\(String(repeating: "a", count: 64))"},
          {"id": "r", "title": "Rel", "pkg": "../../firmware/r/v0.2/x.fwsc", "product": "FM-1_700", "release": "v0.2", "date": "2026-10-25", "sha256": "\(String(repeating: "b", count: 64))"},
          {"id": "evil", "title": "Elsewhere", "pkg": "http://example.com/x.fwsc", "product": "FM-1_700", "sha256": "\(String(repeating: "c", count: 64))"},
          {"id": "nosha", "title": "No digest", "pkg": "x.fwsc", "product": "FM-1_700"}
        ]}
        """
        let l = FlashCatalog.siteEntries(Data(json.utf8))
        XCTAssertEqual(l.map(\.title), ["Rel"])                    // (releases only: not the development build)
        XCTAssertEqual(l[0].url.absoluteString, "https://w0ts.github.io/optimist/firmware/r/v0.2/x.fwsc")
        XCTAssertEqual(FlashCatalog.grouped(l).map(\.group), ["Optimist v0.2 · 2026-10-25"])
    }

    func testOwnCloudBuildByKey() {
        let sha = String(repeating: "1", count: 40), id = "4ea7779fc1b73ce0", stem = "optimist-c1-\(id)"
        let other = "4ea7000000000000", stem2 = "optimist-c2-\(other)"
        let json = """
        {"builds": [
          {"id": "\(id)", "stem": "\(stem)", "name": "c1", "fwsc": "\(stem).fwsc", "config": "\(stem).config", "sha256": "\(String(repeating: "d", count: 64))"},
          {"id": "\(other)", "stem": "\(stem2)", "name": "c2", "fwsc": "\(stem2).fwsc", "config": "\(stem2).config", "sha256": "\(String(repeating: "e", count: 64))"},
          {"id": "zz", "stem": "../etc", "fwsc": "x", "config": "y", "sha256": "e"}
        ]}
        """
        XCTAssertEqual(FlashCatalog.key("key 4ea7 779f c1b7 3ce0"), id)
        XCTAssertNil(FlashCatalog.key("c1"))
        let e = FlashCatalog.cloudEntry(Data(json.utf8), sha: sha, key: id)
        XCTAssertEqual(e?.url.absoluteString, "https://raw.githubusercontent.com/w0ts/optimist/\(sha)/\(stem).fwsc")
        XCTAssertNil(e?.product)
        XCTAssertNil(FlashCatalog.cloudEntry(Data(json.utf8), sha: sha, key: "4ea7"))          // (names two)
        XCTAssertNil(FlashCatalog.cloudEntry(Data(json.utf8), sha: "main", key: id))
    }

    func testBuilderEvents() {
        let id = "4ea7779fc1b73ce0"
        XCTAssertEqual(BuilderEvent(["type": "cloud-build", "stage": "building", "id": id, "run": "https://github.com/w0ts/optimist/actions/runs/123"]),
                       .building(id: id, run: URL(string: "https://github.com/w0ts/optimist/actions/runs/123")))
        XCTAssertEqual(BuilderEvent(["type": "cloud-build", "stage": "building", "id": id, "run": "https://evil.example/runs/1"]), .building(id: id, run: nil))
        XCTAssertEqual(BuilderEvent(["type": "cloud-build", "stage": "done", "id": id, "name": "mine"]), .done(id: id, name: "mine"))
        XCTAssertEqual(BuilderEvent(["type": "cloud-build", "stage": "failed", "reason": "too big"]), .stopped(reason: "too big"))
        XCTAssertEqual(BuilderEvent(["type": "open", "where": "install", "build": id]), .open(where: "install", id: id))
        XCTAssertEqual(BuilderEvent(["type": "cloud-build", "stage": "sent", "url": "https://github.com/w0ts/optimist/issues/9"]),
                       .sent(issue: URL(string: "https://github.com/w0ts/optimist/issues/9")))
        XCTAssertEqual(BuilderEvent(["type": "cloud-build", "stage": "sent", "url": "https://evil.example/x"]), .sent(issue: nil))
        XCTAssertNil(BuilderEvent(["type": "cloud-build", "stage": "done", "id": "../etc"]))
        XCTAssertNil(BuilderEvent(["type": "open", "where": "shell", "build": id]))
    }

    // MARK: - installs

    private func updater(_ dev: FakeFlashDevice) -> FlashUpdater {
        let u = FlashUpdater(ports: dev, pause: { _ in await Task.yield() })
        u.timing.loaderWait = 2
        u.timing.returnWait = 2
        u.timing.step1Idle = 2
        u.timing.step2Idle = 2
        return u
    }

    func testInstallWritesTheWholeImage() async throws {
        let pkg = try FlashPackage.check(FakeFlashDevice.package("FM-1_700", size: 5000), title: "t")
        let dev = FakeFlashDevice(.firmware("FM-1_904"), image: pkg.image.count, finalIdentity: "FM-1_700")
        var steps: [FlashStep] = []
        let back = try await updater(dev).install(pkg) { steps.append($0) }
        XCTAssertEqual(back, "FM-1_700")
        XCTAssertEqual(dev.written, pkg.image)
        XCTAssertEqual(steps.first, .start("FM-1_904"))
        XCTAssertTrue(steps.contains(.loader))
        XCTAssertEqual(steps.last, .done("FM-1_700"))
    }

    func testResumeFromOurLoader() async throws {
        let pkg = try FlashPackage.check(FakeFlashDevice.package("FM-1_700", size: 3000), title: "t")
        let dev = FakeFlashDevice(.loader("ota-FM-1_700"), image: pkg.image.count, finalIdentity: "FM-1_700")
        let back = try await updater(dev).install(pkg) { _ in }
        XCTAssertEqual(back, "FM-1_700")
        XCTAssertEqual(dev.written, pkg.image)
    }

    func testForeignLoaderIsNeverWritten() async throws {
        let pkg = try FlashPackage.check(FakeFlashDevice.package("FM-1_700", size: 3000), title: "t")
        let dev = FakeFlashDevice(.loader("ota-FM-1_015"), image: pkg.image.count, finalIdentity: "FM-1_015")
        do {
            _ = try await updater(dev).install(pkg) { _ in }
            XCTFail("wrote to the stock loader")
        } catch let e as FlashError {
            XCTAssertEqual(e.code, .foreign)
        }
        XCTAssertFalse(dev.upgraded)
    }

    func testWrongIdentityAfterTheWrite() async throws {
        let pkg = try FlashPackage.check(FakeFlashDevice.package("FM-1_700", size: 3000), title: "t")
        let dev = FakeFlashDevice(.firmware("FM-1_700"), image: pkg.image.count, finalIdentity: "FM-1_904")
        do {
            _ = try await updater(dev).install(pkg) { _ in }
            XCTFail("a firmware that reports another identity passed")
        } catch let e as FlashError {
            XCTAssertEqual(e.code, .mismatch)
        }
    }

    func testNoDevice() async {
        let dev = FakeFlashDevice(.absent, image: 0, finalIdentity: "")
        do {
            _ = try await updater(dev).install(try FlashPackage.check(FakeFlashDevice.package("FM-1_700", size: 3000), title: "t")) { _ in }
            XCTFail("installed with no FM-1")
        } catch let e as FlashError {
            XCTAssertEqual(e.code, .notFound)
        } catch {
            XCTFail("\(error)")
        }
    }
}

/// A fake FM-1 on USB: answers the handshake with its identity, and after the upgrade command reads the image as the device
/// does (step 1 a few chunks then 0xE0000000, a restart into its loader, step 2 every byte then 0xF0000000, a restart into the
/// new firmware). `written`: what step 2 received.
final class FakeFlashDevice: FlashPorts, FlashChannel {
    enum Mode: Equatable { case absent, firmware(String), loader(String), restarting(next: String) }

    private(set) var mode: Mode
    let imageSize: Int
    let finalIdentity: String
    private(set) var written: [UInt8] = []
    private(set) var upgraded = false
    private var reads: [(UInt32, Int)] = []
    private var finish: UInt32 = 0
    private var pending: (UInt32, Int)?
    private let queue = SysExQueue()

    init(_ mode: Mode, image: Int, finalIdentity: String) {
        self.mode = mode
        self.imageSize = image
        self.finalIdentity = finalIdentity
    }

    // FlashPorts: a restart takes one look at the ports to finish
    func channels() -> [FlashChannel] {
        switch mode {
        case .absent: return []
        case .restarting(let next):
            mode = next.hasPrefix("ota-") ? .loader(next) : .firmware(next)
            return []
        default: return [self]
        }
    }

    // FlashChannel
    var name: String { "Optimist" }
    var lost: Bool { if case .restarting = mode { return true }; return false }
    func drain() { queue.drain() }
    func read(timeout: TimeInterval) async -> [UInt8]? { await queue.read(timeout: min(timeout, 0.5)) }
    func close() {}

    func send(_ bytes: [UInt8]) -> Bool {
        if bytes == FlashProtocol.handshake {
            switch mode {
            case .firmware(let t), .loader(let t): queue.push(Self.identity(t))
            default: break
            }
        } else if bytes == FlashProtocol.upgrade {
            upgraded = true
            switch mode {
            case .firmware:
                reads = [(0, 512), (1024, 300), (UInt32(imageSize - 64), 64)]
                finish = FlashProtocol.step1Done
            case .loader:
                reads = stride(from: 0, to: imageSize, by: 512).map { (UInt32($0), min(512, imageSize - $0)) }
                written = []
                finish = FlashProtocol.step2Done
            default: return false
            }
            next()
        } else if let (addr, data) = Self.answer(bytes) {
            if addr == finish {
                mode = .restarting(next: finish == FlashProtocol.step1Done ? "ota-FM-1_700" : finalIdentity)
                return true
            }
            guard let p = pending, p.0 == addr, p.1 == data.count else { return true }
            if finish == FlashProtocol.step2Done { written += data }
            next()
        }
        return true
    }

    private func next() {
        if reads.isEmpty {
            pending = (finish, 8)
            queue.push(Self.request(finish, 8))
        } else {
            let r = reads.removeFirst()
            pending = r
            queue.push(Self.request(r.0, r.1))
        }
    }

    // the device's frames
    static func request(_ addr: UInt32, _ len: Int, flags: UInt8 = 0) -> [UInt8] {
        var u: [UInt8] = [0, 0x59, 0x30, 8, 0, 0, flags, UInt8(addr & 0xFF), UInt8((addr >> 8) & 0xFF), UInt8((addr >> 16) & 0xFF), UInt8(addr >> 24),
                          UInt8(len & 0xFF), UInt8((len >> 8) & 0xFF), UInt8((len >> 16) & 0xFF)]
        u.append(UInt8(~u[6...].reduce(0) { $0 + Int($1) } & 0xFF))
        return [0xF0] + FlashProtocol.pack7(u) + [0xF7]
    }

    static func identity(_ text: String) -> [UInt8] {
        var d: [UInt8] = [0, 0x59, 0x11, 27, 0, 0] + Array(text.utf8)
        d += [UInt8](repeating: 0, count: 33 - d.count)
        d.append(0)
        return [0xF0] + FlashProtocol.pack7(d) + [0xF7]
    }

    /// the app's answer: its address and data
    static func answer(_ pkt: [UInt8]) -> (UInt32, [UInt8])? {
        guard pkt.count > 2, pkt.first == 0xF0, pkt.last == 0xF7 else { return nil }
        let u = FlashProtocol.unpack7(pkt[1..<(pkt.count - 1)])
        guard u.count >= 15, u[0] == 0, u[1] == 0x59, u[2] == 0x30 else { return nil }
        let len = Int(u[11]) | Int(u[12]) << 8
        guard u.count >= 14 + len + 1 else { return nil }
        let addr = UInt32(u[7]) | UInt32(u[8]) << 8 | UInt32(u[9]) << 16 | UInt32(u[10]) << 24
        return (addr, Array(u[14..<(14 + len)]))
    }

    /// a .fwsc with identity `product` in its marker bytes (and our loader's mark unless `loader` is false)
    static func package(_ product: String, size: Int, loader: Bool = true) -> [UInt8] {
        var f = (0..<size).map { UInt8(truncatingIfNeeded: $0 &* 31 &+ 7) }
        let chars = Array(product.utf8)
        for i in 0..<20 {
            f[i * 0x30 + 0x2F] = i < chars.count ? UInt8(truncatingIfNeeded: Int(chars[i]) + i + 1) : 0x7D
        }
        if loader { f.replaceSubrange(2000..<2016, with: Array("FELUCCA-LOADER-1".utf8)) }
        return f
    }
}
