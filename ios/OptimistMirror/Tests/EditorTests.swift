// SPDX-License-Identifier: GPL-3.0-only
// The editor features' protocol and files: the wire format, the web editor's files (written by web/editor.html itself: Fixtures/web_*,
// made by make_fixtures.mjs) read and written byte for byte, and the operations against a fake FM-1.
import XCTest
@testable import OptimistMirror

final class WireTests: XCTestCase {
    func testCRCKnownValue() {
        XCTAssertEqual(Wire.crc32(Array("123456789".utf8)), 0xCBF4_3926)
    }

    func testPack7RoundTrip() {
        let bytes = (0..<300).map { UInt8(($0 * 37 + 5) & 255) }
        let packed = Wire.pack7(bytes)
        XCTAssertTrue(packed.allSatisfy { $0 < 0x80 })
        XCTAssertEqual(packed.count, 300 / 7 * 8 + 300 % 7 + 1)
        XCTAssertEqual(Wire.unpack7(packed), bytes)
        XCTAssertEqual(Wire.pack7([0x80, 0x01]), [0b01, 0x00, 0x01])    // top bits first, as the web page's pack7
    }

    func testSevenBitValues() {
        XCTAssertEqual(Wire.wr7(300, 3), [44, 2, 0])
        XCTAssertEqual(Wire.rd7([44, 2, 0], 0, 3), 300)
        XCTAssertEqual(Wire.v14enc(-3), [0x7D, 0x3F])
        XCTAssertEqual(Wire.v14dec(0x7D, 0x3F), -3)
        XCTAssertEqual(Wire.v14enc(99999), Wire.v14enc(8191))
        XCTAssertEqual(Wire.strEnc("AB"), [65, 66, 0])
    }

    func testFramesAreSevenBit() throws {
        XCTAssertEqual(try Wire.frame(9, [0, 1]), [0xF0, 0x7D, 0x46, 0x4C, 9, 0, 1, 0xF7])
        XCTAssertThrowsError(try Wire.frame(9, [0x80]))
        XCTAssertEqual(Wire.unframe([0xF0, 0x7D, 0x46, 0x4C, 9, 1, 2, 0xF7])?.args, [1, 2])
        XCTAssertNil(Wire.unframe([0xF0, 0x43, 0x46, 0x4C, 9, 1, 2, 0xF7]))
    }

    func testReplyMatching() {
        XCTAssertTrue(Wire.replyMatches(Cmd.project.rawValue, [2, 3], [2, 3, 1, 0]))
        XCTAssertFalse(Wire.replyMatches(Cmd.project.rawValue, [2, 3], [2, 4, 1, 0]))
        XCTAssertFalse(Wire.replyMatches(Cmd.patRead.rawValue, [0, 1, 0, 0], [0, 1, 0, 1, 0, 0]))
        XCTAssertTrue(Wire.replyMatches(Cmd.bkList.rawValue, [4, 0], [4]))
    }

    // the same INFO replies web/test_web.mjs parses
    func testInfoLiteAndLibrarian() throws {
        let enc = { (s: String) in Array(s.utf8) + [0] }
        let lite = try DeviceInfo.parse(enc("FELUCCA 0.1") + [0, 0, 0, 64, 0, 0, 6, 0x64, 1, 1])
        XCTAssertTrue(lite.lite)
        XCTAssertFalse(lite.lib)
        XCTAssertEqual(lite.nengines, 0)
        let lib = try DeviceInfo.parse(enc("FELUCCA 0.1") + [2, 90, 0, 64, 50] + enc("ANALOG") + enc("FM6") + [4, 6, 0, 9, 0x55, 2, 16, 16, 0x64, 1, 3])
        XCTAssertTrue(lib.lite && lib.lib)
        XCTAssertEqual(lib.pcount, 90)
        XCTAssertEqual(lib.engines, ["ANALOG", "FM6"])
        XCTAssertEqual(lib.patSlots, 16)
        XCTAssertEqual(lib.patScenes, 16)
        XCTAssertThrowsError(try DeviceInfo.parse([65, 0, 1]))
    }
}

final class FileTests: XCTestCase {
    private func fixture(_ name: String, _ ext: String) throws -> Data {
        try Data(contentsOf: XCTUnwrap(Bundle(for: Self.self).url(forResource: name, withExtension: ext)))
    }

    private let created = "2026-01-02T03:04:05.678Z"
    private let caps = BkCaps(eng: 255, kits: 1023, sets: 7, sections: 8, usrCap: [81408, 81408, 81408], engAt: 4, trk: 300, kitAt: 30, e0At: 40,
                              np: 90, pe0: 62, slen: 60, bits: [1, 2, 3])

    private var objects: [BackupObject] {
        [BackupObject(tag: "SETT", kind: "st", fm6: false, data: (0..<300).map { UInt8(($0 * 7 + 3) & 255) }),
         BackupObject(tag: "S01 ", kind: "sec", fm6: false, data: (0..<1000).map { UInt8(($0 * 13) & 255) }),
         BackupObject(tag: "USR1", kind: "usr", fm6: false, data: (0..<700).map { UInt8(($0 * 5 + 1) & 255) })]
    }

    // MARK: backup files

    /// The app writes the file the web editor wrote, byte for byte (same device, objects and date).
    func testBackupFileIsByteIdenticalToTheWebEditors() throws {
        let web = [UInt8](try fixture("web_backup", "optimist-backup"))
        let stamp = DeviceStamp(version: "OPTIMIST 0.9", proto: 6, switches: 0x15, magic: "FUNB", bk: 4, caps: caps)
        XCTAssertEqual(BackupFile.encode(device: stamp, objects: objects, created: created), web)
    }

    func testBackupFileFromTheWebEditorReads() throws {
        let f = try BackupFile.decode([UInt8](try fixture("web_backup", "optimist-backup")))
        XCTAssertEqual(f.objects, objects)
        XCTAssertEqual(f.version, "OPTIMIST 0.9")
        XCTAssertEqual(f.magic, "FUNB")
        XCTAssertEqual(f.created, created)
    }

    func testDamagedBackupFilesAreRefused() throws {
        var b = [UInt8](try fixture("web_backup", "optimist-backup"))
        XCTAssertThrowsError(try BackupFile.decode(Array(b.prefix(10))))
        XCTAssertThrowsError(try BackupFile.decode(Array(b.dropLast(1))))          // the CRC no longer fits
        b[200] ^= 1
        XCTAssertThrowsError(try BackupFile.decode(b)) { XCTAssertTrue("\($0)".contains("CRC") || ($0 as? WireError)?.message.contains("CRC") == true) }
        var notOurs = b
        notOurs[0] = 0
        XCTAssertThrowsError(try BackupFile.decode(notOurs))
    }

    func testRestorePlanSaysWhyNot() {
        let list = BkList(version: 4, n: 3, switches: 0, magic: "FUNB", chunk: 256, first: 0, count: 3, objs: [
            BkObject(i: 0, tag: "SETT", kind: "st", inBuild: true, hasData: true, fm6: false, len: 1, crc: 0),
            BkObject(i: 1, tag: "S01 ", kind: "sec", inBuild: false, hasData: true, fm6: false, len: 1, crc: 0),
            BkObject(i: 2, tag: "USR1", kind: "usr", inBuild: true, hasData: true, fm6: false, len: 1, crc: 0),
        ], caps: BkCaps(eng: 0, kits: 0, sets: 0, sections: 8, usrCap: [100, 100, 100], engAt: 0, trk: 0, kitAt: 0, e0At: 0, np: 0, pe0: 0, slen: 0, bits: []))
        let plan = Restore.plan(objects + [BackupObject(tag: "NOPE", kind: "st", fm6: false, data: [1]), BackupObject(tag: "SNAP", kind: "snp", fm6: false, data: [1])], list)
        XCTAssertEqual(plan.map { $0.why }, [nil, .notInBuild, .tooBig, .unknown, .unknown])
        XCTAssertEqual(Restore.label("S03 "), "section C")
    }

    // MARK: library files

    private let ctx = LibraryContext(keys: ["LEVEL", "CUT", "RES", "E0"], engines: ["ANALOG", "DIGITAL", "FM6"], firmware: "OPTIMIST 0.9", pe0: 3)

    func testLibraryFileFromTheWebEditorReads() throws {
        let r = try LibraryFile.decode(try fixture("web_library", "json"), ctx: ctx)
        XCTAssertEqual(r.patches.count, 2)
        XCTAssertEqual(r.skipped, 0)
        XCTAssertEqual(r.keys, ["LEVEL", "CUT", "RES", "E0"])
        let a = r.patches[0]
        XCTAssertEqual(a.name, "BASS ONE")
        XCTAssertEqual(a.engine, 0)
        XCTAssertEqual(a.p, [10, nil, -3, 127])
        XCTAssertEqual(a.pattern?[0], [60, 1])
        XCTAssertEqual(a.pattern?[2], [0, 4])
        XCTAssertEqual(a.tags, ["bass", "device"])
        XCTAssertEqual(a.slot, 3)
        XCTAssertNil(r.patches[1].pattern)
    }

    /// What the app writes is the same JSON document the web editor writes (compared as data, not as text).
    func testLibraryFileWrittenIsTheWebEditorsDocument() throws {
        let r = try LibraryFile.decode(try fixture("web_library", "json"), ctx: ctx)
        let out = try LibraryFile.encode(kind: "library", patches: r.patches, ctx: ctx, exported: created)
        let a = try JSONSerialization.jsonObject(with: out) as? NSDictionary
        let b = try JSONSerialization.jsonObject(with: try fixture("web_library", "json")) as? NSDictionary
        XCTAssertNotNil(a)
        XCTAssertEqual(a, b)
    }

    func testLibraryRemapsByKeyAndEngineName() throws {
        // the file was written with other keys and engine order: the values land on the device's keys, the engine by name
        let file: [String: Any] = ["format": "felucca-library", "version": 1, "kind": "library", "paramLabels": ["RES", "LEVEL", "NEW"],
                                   "engines": ["DIGITAL", "ANALOG"],
                                   "patches": [["name": "X", "engine": 1, "engineName": "ANALOG", "params": [5, 6, 7], "pattern": NSNull(), "tags": []],
                                               ["name": "GONE", "engine": 0, "engineName": "NOSUCH", "params": [1, 2, 3]]]]
        let r = try LibraryFile.decode(try JSONSerialization.data(withJSONObject: file), ctx: ctx)
        XCTAssertEqual(r.patches.count, 1)
        XCTAssertEqual(r.skipped, 1)
        XCTAssertEqual(r.patches[0].engine, 0)
        XCTAssertEqual(r.patches[0].engineName, "ANALOG")
        XCTAssertEqual(r.patches[0].p, [6, nil, 5, nil])     // LEVEL, CUT (absent), RES, E0 (absent)
    }

    func testLegacyAnalog2KeysAreConverted() {
        let from = ["FATK", "FDEC", "AMT2", "DST2"], to = ["OSC2", "ATK2", "DEC2", "FLT#3", "PIT#3", "SHP#3", "ENV2"]
        let out = LibraryFile.remapParams([11, 22, 33, 1], from: from, to: to)
        XCTAssertEqual(out, [0, 11, 22, 0, 33, 0, 0])      // (the web editor's remapParams gives the same, checked with node)
        let lfo = LibraryFile.remapParams([1, 2, 3, 4, 5], from: ["PIT#2", "FLT#2", "SHP#2", "AMP", "Z"], to: ["DST A", "AMT A", "DST B", "AMT B", "Z"])
        XCTAssertEqual(lfo, [2, 4, 3, 6, 5])
    }

    func testPatchFileOfTheEditorsSaveToFile() throws {
        let r = try LibraryFile.decode(try fixture("web_patch_file", "json"), ctx: ctx)
        XCTAssertEqual(r.patches.count, 1)
        let p = r.patches[0]
        XCTAssertEqual(p.name, "LEAD")
        XCTAssertEqual(p.engine, 1)
        XCTAssertEqual(p.p, [5, 6, 7, 8])                    // cut to the library's 4 keys
        XCTAssertEqual(p.tags, ["file"])
        XCTAssertEqual(p.pattern?[0], [60, 3])
        XCTAssertEqual(p.pattern?[1], [0, 4])                // a tie
        XCTAssertEqual(p.pattern?[2], [0, 0])                // a rest
    }

    func testBadLibraryFilesAreRefused() {
        XCTAssertThrowsError(try LibraryFile.decode(Data("nope".utf8), ctx: ctx))
        XCTAssertThrowsError(try LibraryFile.decode(Data("{\"format\":\"other\"}".utf8), ctx: ctx))
        XCTAssertThrowsError(try LibraryFile.decode(Data("{\"format\":\"felucca-library\",\"version\":1}".utf8), ctx: ctx))
    }

    func testParamKeysNumberRepeatedLabels() {
        XCTAssertEqual(LibraryFile.paramKeys(labels: ["A", "B", "A", "DROP", "X"], pe0: 4, pcount: 6),
                       ["A", "B", "A#2", "PIT", "E0", "E1"])
    }

    func testStoreKeepsTheLibraryAsTheWebFile() throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let store = LibraryStore(url: dir.appendingPathComponent("library.json"))
        XCTAssertEqual(try store.load().patches.count, 0)
        let r = try LibraryFile.decode(try fixture("web_library", "json"), ctx: ctx)
        try store.save(r.patches, layout: ctx)
        let back = try store.load()
        XCTAssertEqual(back.patches.map { $0.name }, ["BASS ONE", "PAD"])
        XCTAssertEqual(back.patches[0].p, [10, nil, -3, 127])
        XCTAssertEqual(back.layout.keys, ctx.keys)
        XCTAssertEqual(back.layout.engines, ctx.engines)
    }

    // MARK: pattern files

    func testPatternFileFromTheWebEditorReads() throws {
        let p = try PatternFile.decode(try fixture("web_pattern", "json"))
        XCTAssertEqual(p.track, 2)
        XCTAssertEqual(p.bytes, [1, 2, 3, 250, 255, 0])
    }

    func testPatternFileWrittenIsTheWebEditorsDocument() throws {
        let out = try PatternFile.encode(track: 2, bytes: [1, 2, 3, 250, 255, 0])
        XCTAssertEqual(try JSONSerialization.jsonObject(with: out) as? NSDictionary, try JSONSerialization.jsonObject(with: try fixture("web_pattern", "json")) as? NSDictionary)
    }

    func testBadPatternFilesAreRefused() {
        XCTAssertThrowsError(try PatternFile.decode(Data("{\"format\":\"optimist-pattern\",\"version\":1,\"bytes\":[256]}".utf8)))
        XCTAssertThrowsError(try PatternFile.decode(Data("{\"format\":\"optimist-pattern\",\"version\":2,\"bytes\":[1]}".utf8)))
        XCTAssertThrowsError(try PatternFile.decode(Data("{\"format\":\"optimist-pattern\",\"version\":1,\"bytes\":[1.5]}".utf8)))
    }

    /// Writes the app's files into $FM1_EXPORT_DIR (when set) for web/make_fixtures.mjs read: the web editor opens what the app wrote.
    func testWriteFilesForTheWebEditor() throws {
        guard let dir = ProcessInfo.processInfo.environment["FM1_EXPORT_DIR"] else { return }
        let r = try LibraryFile.decode(try fixture("web_library", "json"), ctx: ctx)
        let stamp = DeviceStamp(version: "OPTIMIST 0.9", proto: 6, switches: 0x15, magic: "FUNB", bk: 4, caps: caps)
        let base = URL(fileURLWithPath: dir)
        try Data(BackupFile.encode(device: stamp, objects: objects)).write(to: base.appendingPathComponent("app_full.optimist-backup"))
        try Data(BackupFile.encode(device: stamp, objects: [objects[1]])).write(to: base.appendingPathComponent("app_section-A.optimist-backup"))
        try LibraryFile.encode(kind: "library", patches: r.patches, ctx: ctx).write(to: base.appendingPathComponent("app_library.json"))
        try LibraryFile.encode(kind: "bank", patches: r.patches, ctx: ctx).write(to: base.appendingPathComponent("app_bank.json"))
        try LibraryFile.encode(kind: "patch", patches: [r.patches[0]], ctx: ctx).write(to: base.appendingPathComponent("app_patch.json"))
        try PatternFile.encode(track: 1, bytes: [9, 8, 7, 255]).write(to: base.appendingPathComponent("app_pattern.json"))
    }
}

final class OpsTests: XCTestCase {
    private func fake() -> FakeFM1 {
        FakeFM1(objs: [
            .init(tag: "SETT", kind: 0, data: (0..<600).map { UInt8($0 & 255) }),
            .init(tag: "S01 ", kind: 3, data: (0..<300).map { UInt8(($0 * 3) & 255) }),
            .init(tag: "UPR1", kind: 0, data: (0..<20).map { UInt8($0) }),
            .init(tag: "USR1", kind: 1, data: (0..<1000).map { UInt8(($0 * 11) & 255) }),
            .init(tag: "SNAP", kind: 6, data: [1, 2, 3]),
        ])
    }

    func testListComesInPagesWithCaps() async throws {
        let d = fake()
        let l = try await Ops.bkListAll(d)
        XCTAssertEqual(l.objs.map { $0.tag }, ["SETT", "S01 ", "UPR1", "USR1", "SNAP"])
        XCTAssertEqual(l.objs.map { $0.kind }, ["st", "sec", "st", "usr", "snp"])
        XCTAssertEqual(l.version, 4)
        XCTAssertEqual(l.magic, "FUNB")
        XCTAssertEqual(l.caps?.usrCap, [81408, 81408, 81408])
        XCTAssertEqual(l.caps?.pe0, 62)
        XCTAssertEqual(l.objs[1].len, 300)
        XCTAssertEqual(l.objs[1].crc, Int(Wire.crc32(d.objs[1].data)))
    }

    func testBackupReadsEveryObjectAndTheFileRoundTrips() async throws {
        let d = fake()
        var seen = 0.0
        let r = try await Ops.readAll(d, withSamples: true) { v, _ in seen = max(seen, v) }
        XCTAssertEqual(r.objects.map { $0.tag }, ["SETT", "S01 ", "UPR1", "USR1", "SNAP"])
        XCTAssertEqual(r.objects.map { $0.data }, d.objs.map { $0.data })
        XCTAssertEqual(seen, 1.0, accuracy: 1e-9)
        let file = BackupFile.encode(device: DeviceStamp(version: "x", proto: 6, switches: 0x15, magic: r.list.magic, bk: r.list.version, caps: r.list.caps), objects: r.objects)
        XCTAssertEqual(try BackupFile.decode(file).objects, r.objects)
        let noSamples = try await Ops.readAll(d, withSamples: false)
        XCTAssertFalse(noSamples.objects.contains { $0.tag == "USR1" })
    }

    func testAChunkWithABadCRCIsAskedAgain() async throws {
        let d = fake()
        d.corruptNextRead = true
        let l = try await Ops.bkListAll(d)
        let data = try await Ops.readObject(d, l.objs[0])
        XCTAssertEqual(data, d.objs[0].data)
    }

    func testRestoreChecksFirstThenWritesThenEnds() async throws {
        let d = fake()
        let file = [BackupObject(tag: "SETT", kind: "st", fm6: false, data: [9, 8, 7]),
                    BackupObject(tag: "S01 ", kind: "sec", fm6: false, data: [5, 5, 5, 5]),
                    BackupObject(tag: "USR1", kind: "usr", fm6: false, data: (0..<(Sample.dataOffset + 600)).map { UInt8($0 & 255) }),
                    BackupObject(tag: "SNAP", kind: "snp", fm6: false, data: [4, 4])]
        let r = try await Ops.restore(d, file: file, selected: ["SETT", "S01 ", "USR1", "SNAP"])
        XCTAssertEqual(r.written, ["SETT", "S01 ", "USR1"])
        XCTAssertEqual(r.skipped, ["SNAP"])
        XCTAssertEqual(d.objs[0].data, [9, 8, 7])
        XCTAssertEqual(d.objs[1].data, [5, 5, 5, 5])
        XCTAssertEqual(d.objs[4].data, [1, 2, 3])                                  // the snapshots are not touched
        XCTAssertEqual(d.log, ["BEGIN SETT", "CHECK SETT", "BEGIN SETT", "COMMIT SETT", "BEGIN S01 ", "COMMIT S01 ", "SMP_BEGIN 0", "SMP_END 0", "END 1"])
        XCTAssertEqual(d.samples[0]?.data.count, 600)
        XCTAssertEqual(d.samples[0]?.header.count, Sample.headerLength)
    }

    func testRestoreWritesNothingWhenTheDeviceRefusesAnObject() async throws {
        let d = fake()
        d.refuseTag = "UPR1"
        let file = [BackupObject(tag: "SETT", kind: "st", fm6: false, data: [9]), BackupObject(tag: "UPR1", kind: "st", fm6: false, data: [1, 2])]
        do {
            _ = try await Ops.restore(d, file: file, selected: ["SETT", "UPR1"])
            XCTFail("expected a refusal")
        } catch {
            XCTAssertTrue("\(error.localizedDescription)".contains("UPR1"))
        }
        XCTAssertEqual(d.objs[0].data.count, 600)                                  // nothing written
        XCTAssertEqual(d.log.last, "END 0")
        XCTAssertFalse(d.log.contains { $0.hasPrefix("COMMIT") })
    }

    func testSectionExportAndImportUseTheSectionsObject() async throws {
        let d = fake()
        let l = try await Ops.bkListAll(d)
        let o = try XCTUnwrap(Ops.sectionObject(l, 0))
        let data = try await Ops.readObject(d, o)
        let file = BackupFile.encode(device: DeviceStamp(version: nil, proto: 6, switches: 0, magic: "FUNB", bk: 4, caps: nil),
                                     objects: [BackupObject(tag: o.tag, kind: o.kind, fm6: o.fm6, data: data)])
        let back = try BackupFile.decode(file)
        XCTAssertEqual(back.objects.first?.kind, "sec")
        XCTAssertNil(Ops.sectionObject(l, 5))
        try await Ops.writeObject(d, index: o.i, tag: o.tag, data: [7, 7])
        XCTAssertEqual(d.objs[1].data, [7, 7])
    }

    func testProjectsAndBusy() async throws {
        let d = fake()
        let q = try await Ops.project(d, op: 2, slot: 2)
        XCTAssertEqual(q.used, 1)
        let s = try await Ops.project(d, op: 1, slot: 1)
        XCTAssertEqual(s.used, 1)
        d.playing = true
        do { _ = try await Ops.project(d, op: 0, slot: 0); XCTFail("expected busy") } catch { XCTAssertTrue("\(error)".contains("Stop playback") || (error as? WireError)?.message.contains("Stop") == true) }
        _ = try await Ops.project(d, op: 2, slot: 0)      // a query is not refused
    }

    func testPatternsReadWriteCopyClear() async throws {
        let d = fake()
        let rec = (0..<250).map { UInt8(($0 * 3) & 255) }
        try await Ops.patWrite(d, track: 1, slot: 4, bytes: rec)            // (the first write is answered busy: asked again)
        XCTAssertEqual(d.patterns[1][4], rec)
        let back = try await Ops.patRead(d, track: 1, slot: 4)                // (read in chunks of 100)
        XCTAssertEqual(back, rec)
        let none = try await Ops.patRead(d, track: 0, slot: 0)
        XCTAssertEqual(none, [])
        try await Ops.patOp(d, op: 1, track: 1, slot: 4, track2: 2, slot2: 7)
        XCTAssertEqual(d.patterns[2][7], rec)
        try await Ops.patOp(d, op: 2, track: 1, slot: 4)
        XCTAssertNil(d.patterns[1][4])
        let l = try await Ops.patList(d)
        XCTAssertEqual(l.len?[2][7], rec.count & 0x7F)
        let data = try PatternFile.encode(track: 2, bytes: rec)
        XCTAssertEqual(try PatternFile.decode(data).bytes, rec)
    }

    func testUserPresetBank() async throws {
        let d = fake()
        let b0 = try await Ops.bankList(d)
        XCTAssertEqual(b0.total, 32)
        XCTAssertEqual(b0.slots.count, 32)
        XCTAssertFalse(b0.slots[3].used)
        let rc = try await Ops.bankPut(d, slot: 3, engine: 1, name: "  my lead name that is long ", p: [10, -5, 8191, 0], pattern: [[60, 1], [0, 4]])
        XCTAssertEqual(rc, 0)
        let b = try await Ops.bankList(d)
        XCTAssertEqual(b.slots[3].name, "my lead name")                         // 12 characters
        XCTAssertEqual(b.slots[3].engine, 1)
        let u = try await Ops.bankGet(d, slot: 3, pcount: 4)
        XCTAssertEqual(u.p, [10, -5, 8191, 0])
        XCTAssertEqual(u.pattern.count, 16)
        XCTAssertEqual(u.pattern[0], [60, 1])
        XCTAssertEqual(u.pattern[1], [0, 4])
        XCTAssertEqual(u.pattern[5], [0, 0])
        let stored = try await Ops.bankStore(d, slot: 5, name: "")
        XCTAssertEqual(stored, 0)
        let b2 = try await Ops.bankList(d)
        XCTAssertEqual(b2.slots[5].name, "ANALOG 07")
        let loaded = try await Ops.bankLoad(d, slot: 4)                           // empty
        XCTAssertEqual(loaded, 1)
        let erased = try await Ops.bankErase(d, slot: 3)
        XCTAssertEqual(erased, 0)
        let b3 = try await Ops.bankList(d)
        XCTAssertFalse(b3.slots[3].used)
        XCTAssertEqual(Names.upName("\u{7}\n"), "PATCH")
    }

    func testParamLabelsAndDefaults() async throws {
        let l = try await Ops.paramLabels(fake(), pcount: 4)
        XCTAssertEqual(l?.labels, ["LEVEL", "CUT", "CUT", "E0"])
        XCTAssertEqual(l?.defaults, [0, 3, 6, 9])
        XCTAssertEqual(LibraryFile.paramKeys(labels: l!.labels, pe0: 3, pcount: 4), ["LEVEL", "CUT", "CUT#2", "E0"])
    }

    func testInfoAgainstTheFake() async throws {
        let i = try await Ops.info(fake())
        XCTAssertEqual(i.version, "OPTIMIST 0.9")
        XCTAssertEqual(i.engines, ["ANALOG", "DIGITAL"])
        XCTAssertEqual(i.pcount, 4)
        XCTAssertTrue(i.lite && i.lib)
        XCTAssertTrue(i.hasPatterns)
    }
}
