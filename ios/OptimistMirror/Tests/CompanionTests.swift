// SPDX-License-Identifier: GPL-3.0-only
// The fm1-base companion services against frames the fm1-base host simulator recorded (Fixtures/companion_transcript.txt: the
// CPTEST app of tests/companion_app, driven by tools/fm1_companion.py at fm1-base 786fa65): every request the app encodes must be
// one the recorded client sent, byte for byte, and every recorded answer must read as what the client saw. No device.
import XCTest
import WebKit
@testable import OptimistMirror

private func hex(_ s: Substring) -> [UInt8] {
    stride(from: 0, to: s.count, by: 2).map { UInt8(s.dropFirst($0).prefix(2), radix: 16)! }
}

/// Answers a request with the recorded answer to the same request frame.
private final class Replay: CompanionTransport {
    private var answers: [[UInt8]: [UInt8]] = [:]
    var unknown: [[UInt8]] = []
    var sent = 0

    init(_ transcript: String) {
        var tx: [UInt8]?
        for line in transcript.split(separator: "\n") where !line.hasPrefix("#") {
            let p = line.split(separator: " ")
            if p[0] == "tx" { tx = hex(p[1]) } else if p[0] == "rx", let t = tx { answers[t] = hex(p[1]); tx = nil }
        }
    }

    func companion(_ cmd: UInt8, _ body: [UInt8], timeout: TimeInterval) async throws -> [UInt8] {
        let f = CompanionWire.frame(cmd, body)
        sent += 1
        guard let a = answers[f], let d = CompanionWire.decode(a) else { unknown.append(f); throw CompanionError.silent(cmd) }
        XCTAssertEqual(d.cmd, cmd)
        return d.body
    }
}

final class CompanionTests: XCTestCase {
    private func replay() throws -> Replay {
        let url = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "companion_transcript", withExtension: "txt"))
        return Replay(try String(contentsOf: url, encoding: .utf8))
    }

    private func client(_ r: Replay) -> CompanionClient { CompanionClient(transport: r, timeout: 0.1, tries: 1) }

    func testFrameRoundTrip() throws {
        let f = CompanionWire.frame(0x61, [1, 2, 0xFF, 0x80])
        let d = try XCTUnwrap(CompanionWire.decode(f))
        XCTAssertEqual(d.cmd, 0x61)
        XCTAssertEqual(d.body, [1, 2, 0xFF, 0x80])
        XCTAssertEqual(f.first, 0xF0)
        XCTAssertTrue(f.dropFirst().dropLast().allSatisfy { $0 < 0x80 })
    }

    func testDecodeRefusesWhatIsNotACompanionAnswer() {
        XCTAssertNil(CompanionWire.decode([0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7]))     // Optimist's INFO
        XCTAssertNil(CompanionWire.decode(Array(Wire.header) + [0x6A]))
        var f = CompanionWire.frame(0x60, [1, 2, 3])
        f[f.count - 2] ^= 0x01                                                         // a damaged checksum
        XCTAssertNil(CompanionWire.decode(f))
    }

    func testCapabilityQueryAnswer() async throws {
        let r = try replay()
        let i = try await client(r).hello()
        XCTAssertEqual(i.name, "CPTEST")
        XCTAssertEqual(i.appVersion, "1.2")
        XCTAssertEqual(i.identity, "FM-1_1400")
        XCTAssertEqual(i.idPrefix, "FM-1_14")
        XCTAssertEqual(i.version, 2)
        XCTAssertEqual(i.services, 1831 | 8)
        XCTAssertTrue(i.hasFiles && i.hasParams && i.hasBackup && !i.hasMirror)
        XCTAssertEqual(i.backup, CompanionInfo.BackupFormat(magic: "CPTB", version: 2, ext: "cpb"))
        XCTAssertEqual(i.serviceList, ["capability query", "files", "parameters", "backup", "screen reader", "USB audio in", "USB audio out", "the app's own SysEx"])
        XCTAssertEqual(i.readMax, 256)
        XCTAssertEqual(i.writeMax, 512)
        XCTAssertEqual(i.files, 3)
        XCTAssertEqual(i.params, 6)
        XCTAssertEqual(i.extras, ["editor": "cptest/1", "site": "https://example.invalid/cptest"])
        XCTAssertTrue(r.unknown.isEmpty)
    }

    func testCapabilityQueryRefusesATruncatedAnswer() {
        XCTAssertThrowsError(try CompanionInfo.parse([1, 0, 0]))
    }

    func testFilesList() async throws {
        let r = try replay()
        let f = try await client(r).files()
        XCTAssertEqual(f.map(\.name), ["notes.txt", "sample.raw", "info"])
        XCTAssertEqual(f.map(\.kind), ["text", "sample", "settings"])
        XCTAssertEqual(f.map(\.flags), [15, 7, 1])
        XCTAssertEqual(f.map(\.size), [0, 0, 300])
        XCTAssertEqual(f.map(\.room), [4064, 65504, 300])
        XCTAssertTrue(f[2].readable && !f[2].writable && !f[2].deletable)
        XCTAssertTrue(r.unknown.isEmpty, "every request the app encoded is one the recorded client sent: \(r.unknown.map { $0.map { String($0, radix: 16) } })")
    }

    func testReadAFileWholeCheckedByItsCRC() async throws {
        let r = try replay()
        var seen = 0
        let d = try await client(r).read(2) { at, _ in seen = at }
        XCTAssertEqual(d.count, 300)
        XCTAssertEqual(d, (0..<300).map { UInt8(truncatingIfNeeded: $0 * 7 + 3) })
        XCTAssertEqual(seen, 300)
        XCTAssertTrue(r.unknown.isEmpty)
    }

    func testParametersDescribedGotAndSet() async throws {
        let r = try replay()
        let c = client(r)
        let ps = try await c.params()
        XCTAssertEqual(ps.map(\.name), ["TEMPO", "SWING", "WAVE", "CUTOFF", "LOAD", "MODE"])
        XCTAssertEqual(ps.map(\.id), [10, 11, 20, 21, 30, 40])
        XCTAssertEqual(ps.map(\.group), ["GLOBAL", "GLOBAL", "SOUND", "SOUND", "INFO", "SOUND"])
        XCTAssertEqual(ps[0].unit, "BPM")
        XCTAssertEqual([ps[0].min, ps[0].max, ps[0].def], [40, 240, 120])
        XCTAssertTrue(ps[2].isEnum)
        XCTAssertEqual(ps[2].labels, ["SAW", "SQR", "TRI", "SIN"])
        XCTAssertTrue(ps[4].readOnly)
        XCTAssertEqual(ps[5].min, -20)                         // (a negative minimum: i32)
        XCTAssertEqual(ps[5].labels.count, 40)
        XCTAssertEqual(ps[5].labels.last, "MODE 39")
        let v = try await c.get([10, 11])
        XCTAssertEqual(v, [10: 120, 11: 0])
        let now = try await c.set(10, 5)
        XCTAssertEqual(now, 40, "set 5: the app clamped it to its minimum, as recorded")
        XCTAssertTrue(r.unknown.isEmpty)
    }

    func testAnUnknownRequestIsSilence() async throws {
        let r = try replay()
        do { _ = try await client(r).read(9); XCTFail("no answer expected") } catch let e as CompanionError { XCTAssertEqual(e, .silent(0x61)) }
    }

    func testBackupFileHoldsTheFilesAndTheirChecksums() throws {
        let info = try CompanionInfo.parse(CompanionWire.decode(hex(try XCTUnwrap(try replayRx(0))))!.body)
        let file = CompanionFile(index: 2, name: "info", kind: "settings", flags: 1, size: 3, room: 3)
        let b = CompanionBackup.make(info: info, files: [(file, [1, 2, 3])])
        let back = try JSONDecoder().decode(CompanionBackup.self, from: try b.json())
        XCTAssertEqual(back, b)
        XCTAssertEqual(back.files[0].crc32, CompanionWire.crc32([1, 2, 3]))
        XCTAssertEqual(Data(base64Encoded: back.files[0].data), Data([1, 2, 3]))
        XCTAssertEqual(back.identity, "FM-1_1400")
    }

    private func replayRx(_ n: Int) throws -> Substring? {
        let url = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "companion_transcript", withExtension: "txt"))
        let lines = try String(contentsOf: url, encoding: .utf8).split(separator: "\n").filter { $0.hasPrefix("rx ") }
        return lines[n].dropFirst(3)
    }

    // MARK: the pages

    private func site(_ extras: String = "", services: UInt32 = 1831, version: Int = 1, backup: CompanionInfo.BackupFormat? = nil) -> Fm1BaseSite {
        let info = CompanionInfo(version: version, services: services, readMax: 256, writeMax: 512, files: 3, params: 6, identity: "FM-1_1400", name: "CP\"<TEST",
                                 appVersion: "1.2", extras: Dictionary(uniqueKeysWithValues: extras.split(separator: ";").map { kv in
                                     let p = kv.split(separator: "=", maxSplits: 1).map(String.init); return (p[0], p[1]) }),
                                 backup: backup)
        return Fm1BaseSite(info: info, package: nil, root: Bundle(for: MirrorLink.self).url(forResource: "Fm1Base", withExtension: nil)!)
    }

    private func text(_ s: Fm1BaseSite, _ path: String) throws -> String {
        String(decoding: try XCTUnwrap(s.response(path), path).0, as: UTF8.self)
    }

    func testBundledPagesAreFilledInForTheApp() throws {
        let s = site(services: 1831 | 16)
        for path in ["/mirror/", "/installer/", "/companion/"] {
            let t = try text(s, path)
            XCTAssertNil(t.range(of: #"\{\{[A-Z_]+\}\}"#, options: .regularExpression), "\(path) still has a placeholder")
            XCTAssertFalse(t.contains("/*LIB*/") || t.contains("/*META*/") || t.contains("/*APP*/"), path)
            XCTAssertTrue(t.contains("SPDX-License-Identifier: MPL-2.0"), "\(path): the MPL header is kept")
        }
        XCTAssertTrue(try text(s, "/mirror/").contains("CPTEST"))              // (the name with its quote and tag characters taken out)
        XCTAssertFalse(try text(s, "/mirror/").contains("CP\"<TEST"))
        XCTAssertTrue(try text(s, "/installer/").contains("\"product\":\"FM-1_1400\""))
        XCTAssertNotNil(s.response("/companion/companion.js"))
        XCTAssertNil(s.response("/nothing"))
    }

    func testBackupPageTakesTheFormatFromTheCapabilityAnswer() throws {
        XCTAssertNil(site(services: 1831 | 8, version: 2).response("/backup/"))               // protocol 2 without a format: no page
        XCTAssertFalse(site(services: 1831 | 8, version: 2).pages.contains(.backup))
        XCTAssertNil(site(services: 1831, version: 2, backup: .init(magic: "CPTB", version: 2, ext: "cpb")).backupHints)   // no backup service
        // the tags are not read from a protocol 2 firmware
        XCTAssertNil(site("backup_magic=CPBK;backup_version=3", services: 1831 | 8, version: 2).backupHints)
        let s = site(services: 1831 | 8, version: 2, backup: .init(magic: "CPTB", version: 2, ext: "cpb"))
        let t = try text(s, "/backup/")
        XCTAssertNil(t.range(of: #"\{\{[A-Z_]+\}\}"#, options: .regularExpression))
        XCTAssertTrue(t.contains("magic: \"CPTB\""))
        XCTAssertTrue(t.contains("version: Number(\"2\")"))
        XCTAssertTrue(t.contains("ext: \"cpb\""))
        XCTAssertTrue(t.contains("id: \"FM-1_14\""))
        XCTAssertTrue(s.pages.contains(.backup))
    }

    func testBackupPageFallsBackToTheExtrasTagsOfAProtocol1Firmware() throws {
        XCTAssertNil(site(services: 1831 | 8).response("/backup/"))
        XCTAssertFalse(site(services: 1831 | 8).pages.contains(.backup))
        let s = site("backup_magic=CPBK;backup_version=3;backup_ext=cpb", services: 1831 | 8)
        let t = try text(s, "/backup/")
        XCTAssertNil(t.range(of: #"\{\{[A-Z_]+\}\}"#, options: .regularExpression))
        XCTAssertTrue(t.contains("magic: \"CPBK\""))
        XCTAssertTrue(t.contains("version: Number(\"3\")"))
        XCTAssertTrue(s.pages.contains(.backup))
        XCTAssertNil(site("backup_magic=TOOLONG;backup_version=3", services: 1831 | 8).backupHints)
    }

    func testCapabilityAnswerOfProtocol1HasNoBackupFormat() throws {
        var b: [UInt8] = [1, 0x27, 0x07, 0, 0, 0, 1, 0, 2, 3, 0, 0, 0]
        b += Array("FM-1_1400\0X\01\0\0".utf8)
        let i = try CompanionInfo.parse(b)
        XCTAssertEqual(i.version, 1)
        XCTAssertNil(i.backup)
    }

    func testTheChosenPackageIsServedAndNamedForTheInstaller() throws {
        var fwsc = [UInt8](repeating: 0, count: 20 * 0x30 + 16)
        for (i, c) in "FM-1_1401".utf8.enumerated() { fwsc[i * 0x30 + 0x2F] = UInt8(truncatingIfNeeded: Int(c) + i + 1) }
        for i in 9..<20 { fwsc[i * 0x30 + 0x2F] = 0x7D }
        var s = site()
        s.package = fwsc
        XCTAssertEqual(Fm1BaseSite.product(fwsc), "FM-1_1401")
        XCTAssertTrue(try text(s, "/installer/").contains("\"product\":\"FM-1_1401\""))
        XCTAssertEqual(s.response("/package.fwsc")?.0, Data(fwsc))
        XCTAssertNil(site().response("/package.fwsc"))
    }

    func testStripModuleRemovesImportAndExport() {
        XCTAssertEqual(Fm1BaseSite.stripModule("import a from \"b\";\nexport function f() {}\nexport const x = 1;\nlet y;"), "function f() {}\nconst x = 1;\nlet y;")
    }

    /// the scheme handler and the module script in a real web view: the companion page's module runs (its Connect button works and
    /// says that no FM-1 answered), with Web MIDI from the bridge's shim
    @MainActor
    func testCompanionPageRunsInTheWebView() async throws {
        let web = Fm1BaseWeb()
        web.use(site().info, package: nil)
        web.show(.companion)
        var said = ""
        for _ in 0..<40 {
            _ = try? await web.view.evaluateJavaScript("document.getElementById('connect') && document.getElementById('connect').click()")
            try await Task.sleep(nanoseconds: 300_000_000)
            said = (try? await web.view.evaluateJavaScript("(document.getElementById('alert') || {}).textContent || ''") as? String) ?? ""
            if !said.isEmpty { break }
        }
        XCTAssertTrue(said.hasPrefix("No FM-1 with companion services answered"), "the page said: \(said)")
    }
}
