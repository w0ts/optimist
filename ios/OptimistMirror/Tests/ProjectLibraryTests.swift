// SPDX-License-Identifier: GPL-3.0-only
// The project library store and the swap order.
import XCTest
@testable import OptimistMirror

final class ProjectLibraryTests: XCTestCase {
    private func lib() throws -> ProjectLibrary {
        let d = FileManager.default.temporaryDirectory.appendingPathComponent("projlib-" + UUID().uuidString, isDirectory: true)
        return ProjectLibrary(dir: d)
    }

    private func proj(_ name: String, _ b: UInt8 = 7) -> ProjectFile.Contents { ProjectFile.Contents(slot: 0, family: "FUNB", name: name, data: [UInt8](repeating: b, count: 40)) }

    func testAddListReadRenameDelete() throws {
        let l = try lib()
        XCTAssertTrue(l.list().isEmpty)
        let a = try l.add(proj("SONG A"))
        let b = try l.add(proj("SONG A", 9))             // the same name: a second file, never an overwrite
        XCTAssertNotEqual(a.file, b.file)
        XCTAssertEqual(l.list().count, 2)
        XCTAssertEqual(try l.read(a).data.first, 7)
        XCTAssertEqual(try l.read(b).data.first, 9)
        XCTAssertGreaterThan(a.size, 40)
        try l.rename(a, to: "live set")
        XCTAssertTrue(l.list().contains { $0.name == "LIVE SET" })
        try l.delete(b)
        XCTAssertEqual(l.list().map(\.name), ["LIVE SET"])
    }

    func testForeignFilesAreIgnoredAndRefused() throws {
        let l = try lib()
        try FileManager.default.createDirectory(at: l.dir, withIntermediateDirectories: true)
        try Data("junk".utf8).write(to: l.dir.appendingPathComponent("x.fm1proj"))
        XCTAssertTrue(l.list().isEmpty)
        let u = l.dir.appendingPathComponent("y.fm1proj")
        XCTAssertThrowsError(try l.add(file: u))
    }

    func testSwapSavesBeforeLoading() async throws {
        var order: [String] = []
        try await ProjectSwap.run(saveSlot: { order.append("save") }, load: { order.append("load") })
        XCTAssertEqual(order, ["save", "load"])
    }

    func testFailedSaveNeverLoads() async {
        struct Boom: Error {}
        var loaded = false
        do { try await ProjectSwap.run(saveSlot: { throw Boom() }, load: { loaded = true }); XCTFail("expected a throw") } catch {}
        XCTAssertFalse(loaded)
    }
}
