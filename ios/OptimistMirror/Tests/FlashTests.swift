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

    /// The FM-1 is found by its port's name (ruling 2026-10-29): "FM1-XXXX", still "Optimist" / "Felucca" (older firmware); never other gear.
    func testFM1PortNames() {
        for n in ["FM1-1A2B", "fm1-1a2b", "FM1-1A2B Bluetooth", "Optimist", "Optimist 112D Bluetooth", "Felucca"] {
            XCTAssertTrue(MirrorLink.isFM1Name(n), n)
        }
        for n in ["Launchkey Mini MK3", "IAC Driver Bus 1", "FM1-12", "FM1-ZZZZ", "FM-1 Synth", ""] {
            XCTAssertFalse(MirrorLink.isFM1Name(n), n)
        }
        for n in ["FM1-1A2B", "Optimist", "FM-1", "Melodee", "SLOOP 2.4", "ota-FM-1"] {
            XCTAssertTrue(CoreMIDIFlashPorts.mayBeFM1(n), n)
        }
        for n in ["Launchkey Mini MK3", "IAC Driver Bus 1", "FM1-1A2B Bluetooth", "Optimist 112D Bluetooth"] {
            XCTAssertFalse(CoreMIDIFlashPorts.mayBeFM1(n), n)
        }
    }

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

    /// the web pages' MIDI through the app (WebMIDIBridge): their bytes as Universal MIDI Packets
    func testWebMIDIBytesToUMP() {
        XCTAssertEqual(WebMIDIBridge.ump([0x90, 60, 100]), [0x2090_3C64])                 // note on: type 2
        XCTAssertEqual(WebMIDIBridge.ump([0xC1, 5]), [0x20C1_0500])                       // program change: two bytes
        XCTAssertEqual(WebMIDIBridge.ump([0xF8]), [0x10F8_0000])                          // clock: type 1
        XCTAssertEqual(WebMIDIBridge.ump([0xF0, 0x7D, 0x46, 0x4C, 1, 0xF7]), SysExAssembler.packets([0x7D, 0x46, 0x4C, 1]))
        XCTAssertEqual(WebMIDIBridge.ump([0x80, 60, 0, 0xF0, 1, 2, 0xF7]).count, 1 + 2)   // two messages in one send
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
        XCTAssertEqual(dev.stopsBeforeUpgrade, 1)          // one MIDI STOP (0xFC) before the first update command
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

    // MARK: - the fast install (docs/INSTALL-SPEED.md)

    func testFastWordAndTableMatchTheOtherHosts() {
        // tools/fm1_install.py FAST_HELLO / sector_table, web/fm1ota_optimist.js, the loaders' tests: the same vectors
        XCTAssertEqual(FlashProtocol.fastHello, [0xF0, 0x00, 0x32, 0x45, 0x19, 0x00, 0x00, 0x40, 0x00, 0x00, 0x20, 0x38, 0x07, 0xF7])
        let v = (0..<8192).map { UInt8(truncatingIfNeeded: ($0 &* 31) ^ ($0 >> 8)) }
        XCTAssertEqual(FlashProtocol.sectorTable(v, offset: 0, count: 2), [98, 4, 162, 95, 146, 95, 92, 197, 155, 40, 53, 131])
        XCTAssertEqual(FlashProtocol.fastReply(FlashProtocol.frame(FlashProtocol.fastCommand, [1, 0, 16])), 4096)
        XCTAssertNil(FlashProtocol.answer(v, FlashProtocol.Request(flags: 0, address: 0, length: 4096), maxData: FlashProtocol.maxData))
    }

    func testFastLoaderGetsTheTableAnd4KReads() async throws {
        let pkg = try FlashPackage.check(FakeFlashDevice.package("FM-1_700", size: 0x6000), title: "t")
        let dev = FakeFlashDevice(.loader("ota-FM-1_700"), image: pkg.image.count, finalIdentity: "FM-1_700", fast: true)
        let back = try await updater(dev).install(pkg) { _ in }
        let n = pkg.image.count / 4096
        XCTAssertEqual(back, "FM-1_700")
        XCTAssertTrue(dev.fastOn)
        XCTAssertEqual(dev.written, Array(pkg.image[0..<(n * 4096)]))
        XCTAssertEqual(dev.table, FlashProtocol.sectorTable(pkg.image, offset: 0, count: n))
    }

    func testOlderLoaderIgnoresTheWord() async throws {
        let pkg = try FlashPackage.check(FakeFlashDevice.package("FM-1_700", size: 3000), title: "t")
        let dev = FakeFlashDevice(.loader("ota-FM-1_700"), image: pkg.image.count, finalIdentity: "FM-1_700")
        _ = try await updater(dev).install(pkg) { _ in }
        XCTAssertEqual(dev.fastWords, 1)
        XCTAssertFalse(dev.fastOn)
        XCTAssertEqual(dev.written, pkg.image)
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
    // MARK: - the route and the build's BOOTLOADER (docs/INSTALL-PATHS.md; the same pairs as tools/fm1_route.py)

    func testRouteMatrix() {
        let own = InstallRoute.Package(kind: "optimist", product: "FM-1_700", flashLen: 0x93000, ownLoader: true)
        let rel = InstallRoute.Package(kind: "fm1base", product: "FM-1_79000200", flashLen: 0xCE000, ownLoader: true)
        let mb = InstallRoute.Package(kind: "mboot", product: "FM-1_099", flashLen: nil, ownLoader: false)
        let cases: [(String, InstallRoute.Package, String, String)] = [
            ("optimist", own, "keep", "loader"), ("optimist", rel, "keep", "download"), ("fm1base", own, "keep", "loader"),
            ("stock", own, "keep", "loader"), ("open", own, "keep", "download"), ("none", own, "keep", "download"),
            ("fm1base", mb, "keep", "download"), ("optimist", own, "m-bootloader", "download"), ("loader", own, "keep", "loader")]
        for (r, p, b, want) in cases {
            XCTAssertEqual(InstallRoute.choose(r, p, bootloader: b, runningText: r == "stock" ? "FM-1_016" : "").method, want, "\(r) \(p.kind) \(b)")
        }
        XCTAssertEqual(InstallRoute.runningKind("FM-1_906", port: "SLOOP 2.7"), "sloop")
        XCTAssertEqual(InstallRoute.runningKind("FM-1_7900"), "fm1base")
        XCTAssertEqual(InstallRoute.runningKind("FM1_OFFICIAL_OPEN"), "open")
        XCTAssertEqual(InstallRoute.packageKind(product: "FM-1_700", sha256: "", flashLen: 0xCE000).kind, "other")
        XCTAssertNil(InstallRoute.flashLen([1, 2, 3]))
    }

    /// the running Optimist's word on its bootloader (INFO tag 0x68), as tools/fm1_route.py and web/install_route.js read it
    func testBootWordAndTheRoute() {
        let head: [UInt8] = [0xF0, 0x7D, 0x46, 0x4C, 1] + Array("FELUCCA 0.2".utf8) + [0x66, 2, 0x61, 0x62]
        for w in ["mboot", "stock", "unknown"] {
            XCTAssertEqual(InstallRoute.bootOfInfo(head + [0x68, UInt8(5 + w.count)] + Array("boot=\(w)".utf8) + [0xF7]), w)
        }
        XCTAssertNil(InstallRoute.bootOfInfo(head + [0xF7]))
        XCTAssertNil(InstallRoute.bootOfInfo([0x68, 3] + Array("boot=mboot".utf8)))
        XCTAssertTrue(InstallRoute.asksBoot("FM-1_7900") && InstallRoute.asksBoot("FM-1_700") && !InstallRoute.asksBoot("FM-1_905"))
        let own = InstallRoute.Package(kind: "optimist", product: "FM-1_700", flashLen: 0x93000, ownLoader: true)
        let v15 = InstallRoute.Package(kind: "v15", product: "FM-1_015", flashLen: 0x93000, ownLoader: false)
        XCTAssertEqual(InstallRoute.choose("optimist", own, bootloader: "m-bootloader", mbootPresent: true).method, "loader")
        let stock = InstallRoute.choose("optimist", own, bootloader: "m-bootloader", mbootPresent: false)
        XCTAssertEqual(stock.method, "download")
        XCTAssertTrue(stock.wantsMboot)
        XCTAssertEqual(InstallRoute.choose("optimist", own, bootloader: "m-bootloader", mbootPresent: nil).method, "download")    // (explicit m-bootloader)
        for w in [nil, false, true] {      // (the build's BOOTLOADER=1 is keep: no token, boot=stock, boot=mboot all go over USB-MIDI)
            let k = InstallRoute.bootloaderChoice("m-bootloader", install: false).0
            XCTAssertEqual(InstallRoute.choose("optimist", own, bootloader: k, runningText: "FM-1_700", mbootPresent: w).method, "loader")
        }
        XCTAssertEqual(InstallRoute.choose("optimist", v15, mbootPresent: true).method, "download")    // (-5 known before)
        XCTAssertEqual(InstallRoute.choose("optimist", v15, mbootPresent: false).method, "loader")
    }

    func testMbootOnlyWhenAskedFor() {
        for v in ["m-bootloader", "keep", nil] as [String?] {
            XCTAssertEqual(InstallRoute.bootloaderChoice(v, install: false).0, "keep")
            XCTAssertEqual(InstallRoute.bootloaderChoice(v, install: true).0, "m-bootloader")
        }
        XCTAssertTrue(InstallRoute.bootloaderChoice(nil, install: false).1.contains("no build setting"))
        XCTAssertEqual(InstallRoute.bootloader(config: "# x\nBOOTLOADER=1\n"), "m-bootloader")
        XCTAssertEqual(InstallRoute.bootloader(config: "ENG_FM6=1\n"), "keep")
    }

    @MainActor
    func testDownloadModeIsSaidNeverWritten() async throws {
        var pkg = try FlashPackage.check(FakeFlashDevice.package("FM-1_700", size: 3000), title: "t", file: "p.fwsc")
        pkg.bootloader = "m-bootloader"
        let dev = FakeFlashDevice(.firmware("FM-1_700"), image: pkg.image.count, finalIdentity: "FM-1_700")
        let model = FlashModel(link: MirrorLink(), ports: dev)
        let r = model.route(for: pkg, running: "FM-1_7900", port: "")
        XCTAssertEqual(r.method, "loader")                        // (the build's BOOTLOADER=1: USB-MIDI, the bootloader kept)
        model.installMboot = true                                // (asked for explicitly: M-Flasher's backup first)
        let d = model.route(for: pkg, running: "FM-1_700", port: "FM1-1A2B")
        XCTAssertEqual(d.method, "download")
        XCTAssertTrue(InstallRoute.nextStep(d, file: "p.fwsc").contains("fm1_install.py p.fwsc"))
        model.installMboot = false
        XCTAssertEqual(model.route(for: pkg, running: "FM-1_7900", port: "").method, "loader")    // (fm1-base takes it: the loader)
        XCTAssertFalse(dev.upgraded)
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
    private(set) var stopsBeforeUpgrade = 0   // MIDI STOPs received before the first upgrade command
    let fast: Bool                  // the loader answers the fast install's word (docs/INSTALL-SPEED.md)
    private(set) var fastOn = false
    private(set) var fastWords = 0
    private var reads: [(UInt32, Int)] = []
    private var finish: UInt32 = 0
    private var pending: (UInt32, Int)?
    private(set) var table: [UInt8] = []
    private let queue = SysExQueue()

    init(_ mode: Mode, image: Int, finalIdentity: String, fast: Bool = false) {
        self.mode = mode
        self.fast = fast
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
        if bytes == FlashProtocol.midiStop {
            if !upgraded { stopsBeforeUpgrade += 1 }
        } else if bytes == FlashProtocol.handshake {
            switch mode {
            case .firmware(let t), .loader(let t): queue.push(Self.identity(t))
            default: break
            }
        } else if bytes == FlashProtocol.fastHello {
            fastWords += 1
            if fast, case .loader = mode {
                fastOn = true
                queue.push(FlashProtocol.frame(FlashProtocol.fastCommand, [1, 0, 16]))
            }
        } else if bytes == FlashProtocol.upgrade {
            upgraded = true
            switch mode {
            case .firmware:
                reads = [(0, 512), (1024, 300), (UInt32(imageSize - 64), 64)]
                finish = FlashProtocol.step1Done
            case .loader where fastOn:
                let n = imageSize / FlashProtocol.sector
                reads = [(FlashProtocol.table, n * 6)] + (0..<n).map { (UInt32($0 * FlashProtocol.sector), FlashProtocol.sector) }
                written = []
                finish = FlashProtocol.step2Done
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
            if finish == FlashProtocol.step2Done, addr < FlashProtocol.table { written += data }
            if addr == FlashProtocol.table { table = data }
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

    // MARK: - M-Flasher's release (tools/mflasher.py, bundled as mflasher.json by tools/sync_mflasher.py)

    func testBundledMFlasherReleaseAndLinks() throws {
        let r = try XCTUnwrap(MFlasherRelease.bundled(), "mflasher.json is not in the bundle (the build phase did not run)")
        XCTAssertFalse(r.version.isEmpty)
        XCTAssertEqual(r.page.lastPathComponent, "v\(r.version)")
        for f in r.files {
            XCTAssertEqual(f.sha256.count, 64, f.name)
            XCTAssertEqual(f.url.lastPathComponent, f.name)
            XCTAssertEqual(f.url.deletingLastPathComponent().lastPathComponent, "v\(r.version)")
        }
        // iPhone / iPad: a computer is needed, the link is the release page, no file
        let phone = r.advice(catalyst: false)
        XCTAssertTrue(phone.text.hasPrefix("This switch needs a computer: M-Flasher for macOS / Windows / Linux"))
        XCTAssertEqual(phone.link, r.page)
        XCTAssertNil(phone.sha256)
        // Mac Catalyst: the macOS disk image itself, with its SHA-256
        let mac = r.advice(catalyst: true)
        let dmg = try XCTUnwrap(r.files.first { $0.kind == "dmg" })
        XCTAssertEqual(mac.link, dmg.url)
        XCTAssertEqual(mac.sha256, dmg.sha256)
        XCTAssertTrue(mac.link.lastPathComponent.hasSuffix("macos.dmg"))
        XCTAssertTrue(mac.text.contains(dmg.sha256))
    }

    func testMFlasherAdviceWithoutData() {
        let empty = Bundle(for: FlashTests.self)       // (the test bundle carries no mflasher.json)
        let a = MFlasherRelease.advice(catalyst: true, bundle: empty)
        XCTAssertEqual(a.link, InstallRoute.mflasher)
        XCTAssertNil(a.sha256)
    }
}
