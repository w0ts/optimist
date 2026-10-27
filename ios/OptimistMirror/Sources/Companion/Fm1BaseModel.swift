// SPDX-License-Identifier: GPL-3.0-only
// The native side of an fm1-base app: its files (listed, saved one by one or all together as a backup) and its parameters
// (listed by group, read, set), through the companion services (CompanionClient). One operation at a time.
import Foundation

/// A backup of the app's readable files in one JSON file: what the FM-1 said (identity, name, version) and each file's bytes with
/// the CRC-32 the FM-1 checked them against.
struct CompanionBackup: Codable, Equatable {
    struct Entry: Codable, Equatable {
        let name: String
        let kind: String
        let size: Int
        let crc32: UInt32
        let data: String          // base64
    }
    static let format = "fm1base-companion-backup"
    var format = CompanionBackup.format
    let identity: String
    let name: String
    let version: String
    let files: [Entry]

    static func make(info: CompanionInfo, files: [(CompanionFile, [UInt8])]) -> CompanionBackup {
        CompanionBackup(identity: info.identity, name: info.name, version: info.appVersion,
                        files: files.map { Entry(name: $0.0.name, kind: $0.0.kind, size: $0.1.count, crc32: CompanionWire.crc32($0.1), data: Data($0.1).base64EncodedString()) })
    }

    func json() throws -> Data {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try e.encode(self)
    }
}

@MainActor
final class Fm1BaseModel: ObservableObject {
    struct Note { let text: String; let error: Bool }
    struct Export: Identifiable { let id = UUID(); let url: URL }

    private let link: MirrorLink
    private let client: CompanionClient
    @Published var files: [CompanionFile]?
    @Published var params: [CompanionParam]?
    @Published var values: [Int: Int] = [:]
    @Published var note: Note?
    @Published var progress: Double?
    @Published var busy = false
    @Published var export: Export?

    init(link: MirrorLink) {
        self.link = link
        client = CompanionClient(transport: link)
    }

    private func run(_ what: String, _ body: () async throws -> Void) async {
        guard !busy else { return }
        busy = true
        link.hold()
        defer { busy = false; progress = nil; link.release() }
        do { try await body() } catch { note = Note(text: "\(what): \(error.localizedDescription)", error: true) }
    }

    func loadFiles() async {
        await run("Files") { files = try await client.files(); note = nil }
    }

    func loadParams() async {
        await run("Parameters") {
            let ps = try await client.params()
            params = ps
            values = try await client.get(ps.map(\.id))
            note = nil
        }
    }

    func refreshValues() async {
        guard let ps = params, !busy else { return }
        if let v = try? await client.get(ps.map(\.id)) { values = v }
    }

    func set(_ p: CompanionParam, _ v: Int) async {
        guard !p.readOnly else { return }
        do { values[p.id] = try await client.set(p.id, v) } catch { note = Note(text: "\(p.name): \(error.localizedDescription)", error: true) }
    }

    private func write(_ data: Data, _ name: String) throws -> URL {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent(name.filter { $0.isLetter || $0.isNumber || ".-_".contains($0) })
        try data.write(to: url)
        return url
    }

    /// one file, as the device holds it
    func save(_ f: CompanionFile) async {
        await run(f.name) {
            let bytes = try await client.read(f.index) { [weak self] at, size in Task { @MainActor in self?.progress = size == 0 ? 1 : Double(at) / Double(size) } }
            export = Export(url: try write(Data(bytes), f.name))
            note = Note(text: "\(f.name): \(bytes.count) bytes, CRC-32 checked.", error: false)
        }
    }

    /// every readable file, in one backup file
    func backUp(_ info: CompanionInfo) async {
        await run("Backup") {
            let list = try await client.files()
            files = list
            var got: [(CompanionFile, [UInt8])] = []
            for (n, f) in list.enumerated() where f.readable {
                got.append((f, try await client.read(f.index) { [weak self] at, size in
                    Task { @MainActor in self?.progress = (Double(n) + (size == 0 ? 1 : Double(at) / Double(size))) / Double(list.count) }
                }))
            }
            let stamp = ISO8601DateFormatter().string(from: Date()).replacingOccurrences(of: ":", with: "-")
            export = Export(url: try write(try CompanionBackup.make(info: info, files: got).json(), "\(info.name)-\(stamp).fm1base-backup.json"))
            note = Note(text: "\(got.count) files saved.", error: false)
        }
    }
}
