// SPDX-License-Identifier: GPL-3.0-only
// The editor screens' state and actions: one operation at a time (a flash write or a restore must not overlap another request),
// the mirror's requests paused meanwhile. Projects, backup and restore here; the preset bank and library in EditorSession+Library.
import Combine
import Foundation
import SwiftUI

struct ExportItem: Identifiable {
    let id = UUID()
    let url: URL
}

enum FileIO {
    static func read(_ url: URL) throws -> Data {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        return try Data(contentsOf: url)
    }

    /// the data in a file of this name in a fresh temporary folder (the share sheet keeps the name)
    static func temp(_ name: String, _ data: Data) throws -> URL {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent(name)
        try data.write(to: url)
        return url
    }

    static func today() -> String { String(BackupFile.isoNow().prefix(10)) }

    static func slug(_ s: String) -> String {
        let t = s.lowercased().map { $0.isASCII && ($0.isLetter || $0.isNumber) ? String($0) : "-" }.joined()
        let parts = t.split(separator: "-", omittingEmptySubsequences: true)
        return parts.isEmpty ? "patch" : parts.joined(separator: "-")
    }
}

@MainActor
final class EditorSession: ObservableObject {
    struct Note: Equatable {
        var text: String
        var error: Bool
    }

    let link: MirrorLink
    @Published private(set) var info: DeviceInfo?
    @Published private(set) var busy = false
    @Published private(set) var progress: Double?
    @Published var note: Note?
    @Published var export: ExportItem?

    // projects
    @Published private(set) var projects: ProjList?
    @Published private(set) var sectionUsed: [Int?] = []
    @Published private(set) var bkList: BkList?
    @Published private(set) var patList: PatList?
    @Published var patSel: (track: Int, slot: Int)?

    // backup
    @Published var withSamples = false
    @Published private(set) var restoreName = ""
    @Published private(set) var restoreFile: BackupContents?
    @Published var restoreSel: Set<String> = []

    // the preset bank and the library (EditorSession+Library)
    @Published var bank: (total: Int, slots: [UpSlot])?
    @Published var bankSel: Int?
    @Published var library: [Patch] = []
    @Published var libSel: UUID?
    var layout = LibraryContext(keys: nil, engines: [], firmware: "", pe0: nil)
    var defaults: [Int] = []
    let store = LibraryStore()

    private var bag = Set<AnyCancellable>()

    init(link: MirrorLink) {
        self.link = link
        loadLibrary()
        link.objectWillChange.sink { [weak self] _ in self?.objectWillChange.send() }.store(in: &bag)
    }

    /// The USB link is there and the FM-1 answered INFO.
    var ready: Bool { link.editorPossible && info != nil }
    /// Ready, and the firmware offers f (INFO's tag 66)
    func ready(_ f: Feature) -> Bool { ready && info?.has(f) == true }
    var idle: Bool { ready && !busy }

    // MARK: running an operation

    /// a callback for the transfer loops (they run off the main thread)
    func tick(_ text: String? = nil) -> @Sendable (Double) -> Void {
        { [weak self] v in Task { @MainActor in self?.progress = v; if let text { self?.note = Note(text: text, error: false) } } }
    }

    func say(_ text: String, error: Bool = false) { note = Note(text: text, error: error) }

    private var queue: [(String, () async throws -> Void)] = []

    /// One operation at a time: buttons off, the mirror's requests paused, the error (if any) said. One asked while another runs
    /// (a screen opening) waits its turn.
    func run(_ label: String, _ work: @escaping () async throws -> Void) {
        queue.append((label, work))
        if !busy { next() }
    }

    private func next() {
        guard !queue.isEmpty else { return }
        let (label, work) = queue.removeFirst()
        busy = true
        progress = nil
        note = Note(text: label, error: false)
        link.hold()
        Task {
            do { try await work() }
            catch is CancellationError { note = nil }
            catch { note = Note(text: error.localizedDescription, error: true) }
            busy = false
            progress = nil
            link.release()
            next()
        }
    }

    // MARK: the device

    /// INFO, then what the screens show. Called when the port changes (the FM-1 restarts after a restore).
    func connect() async {
        info = nil
        projects = nil; sectionUsed = []; bkList = nil; patList = nil; bank = nil
        guard link.editorPossible else { return }
        link.hold()
        defer { link.release() }
        do {
            info = try await Ops.info(link)
            note = nil
        } catch {
            note = Note(text: "The FM-1 did not answer the editor's INFO (\(error.localizedDescription)).", error: true)
        }
    }

    func device() -> DeviceStamp? {
        guard let l = bkList else { return nil }
        return DeviceStamp(version: info?.version, proto: info?.proto ?? 0, switches: l.switches, magic: l.magic, bk: l.version, caps: l.caps)
    }

    // MARK: projects (sections)

    var sectionCount: Int {
        guard let i = info else { return 0 }
        if !i.lite { return 4 }
        if i.patScenes > 0 { return i.patScenes }
        let n = bkList?.objs.filter { $0.kind == "sec" }.count ?? 0
        return n > 0 ? n : 4
    }

    func loadProjects() {
        run("Reading the projects…") { [self] in try await readProjects() }
    }

    private func readProjects() async throws {
        if info?.has(.proj) == true { projects = try await Ops.projList(link) }
        if info?.has(.projects) == true {
            bkList = try? await Ops.bkListAll(link)
            var used: [Int?] = []
            for k in 0..<sectionCount {
                do { used.append(try await Ops.project(link, op: 2, slot: k).used) }
                catch let e as WireError where e.isTimeout { used.append(nil) }
            }
            sectionUsed = used
            if info?.hasPatterns == true { patList = try? await Ops.patList(link) }
        }
        note = nil
    }

    /// PJ_OP on project p: 1 save the work there, 2 open it, 3 clear it, 4 name it; then everything read again (an open
    /// changes the sections and patterns, a save to another project makes it the open one)
    func projOp(_ op: Int, _ p: Int, name: String = "") {
        let label = ["", "Saving…", "Opening…", "Clearing…", "Naming…"][op]
        run(label) { [self] in
            try await Ops.projOp(link, op: op, p, name: name)
            try await readProjects()
            say(["", "Saved as project \(p + 1).", "Opened project \(p + 1).", "Cleared project \(p + 1).", "Named."][op])
        }
    }

    func projectOp(_ op: Int, _ k: Int) {
        run(op == 1 ? "Saving…" : "Loading…") { [self] in
            let r = try await Ops.project(link, op: op, slot: k)
            if k < sectionUsed.count { sectionUsed[k] = r.used }
            let n = Names.sectionLetter(k)
            if op == 1 { say("Saved to section \(n).") }
            else if r.used == 1 { say("Loaded section \(n).") }
            else if r.used > 1 { say("Not loaded: another firmware's project is kept in section \(n).") }
            else { say("Empty section \(n).") }
            bkList = try? await Ops.bkListAll(link)      // (a save changes the section's object)
        }
    }

    func exportSection(_ k: Int) {
        run("Reading section \(Names.sectionLetter(k))…") { [self] in
            let l = try await Ops.bkListAll(link)
            bkList = l
            guard let o = Ops.sectionObject(l, k), o.hasData else { say("Empty section \(Names.sectionLetter(k)).", error: true); return }
            let data = try await Ops.readObject(link, o)
            let stamp = DeviceStamp(version: info?.version, proto: info?.proto ?? 0, switches: l.switches, magic: l.magic, bk: l.version, caps: l.caps)
            let file = BackupFile.encode(device: stamp, objects: [BackupObject(tag: o.tag, kind: o.kind, fm6: o.fm6, data: data)])
            export = ExportItem(url: try FileIO.temp("optimist-section-\(Names.sectionLetter(k)).optimist-backup", Data(file)))
            say("Exported: section \(Names.sectionLetter(k)).")
        }
    }

    func importSection(_ k: Int, _ url: URL) {
        let file: BackupContents
        do { file = try BackupFile.decode(Array(try FileIO.read(url))) } catch { say("Not a file of this kind: \(error.localizedDescription)", error: true); return }
        guard let x = file.objects.first(where: { $0.kind == "sec" }) else { say("Not a file of this kind: \(url.lastPathComponent)", error: true); return }
        run("Importing…") { [self] in
            let l = try await Ops.bkListAll(link)
            bkList = l
            guard let o = Ops.sectionObject(l, k) else { throw WireError("Not a file of this kind: section \(Names.sectionLetter(k))") }
            try await Ops.writeObject(link, index: o.i, tag: o.tag, data: x.data)
            _ = try await Ops.end(link, done: true)      // (the FM-1 restarts and loads it, as after a restore)
            say("Imported: section \(Names.sectionLetter(k)).")
        }
    }

    // MARK: patterns

    func refreshPatterns() async { patList = try? await Ops.patList(link) }

    func exportPattern(_ k: Int, _ s: Int) {
        run("Reading the pattern…") { [self] in
            let bytes = try await Ops.patRead(link, track: k, slot: s)
            if bytes.isEmpty { say("Empty slot: \(s + 1)", error: true); return }
            export = ExportItem(url: try FileIO.temp("optimist-pattern-t\(k + 1)-\(s + 1).json", try PatternFile.encode(track: k, bytes: bytes)))
            say("Exported: \(ProjectsView.trackName(k)) · pattern \(s + 1).")
        }
    }

    func importPattern(_ k: Int, _ s: Int, _ url: URL) {
        let bytes: [UInt8]
        do { bytes = try PatternFile.decode(try FileIO.read(url)).bytes } catch { say("Not a file of this kind: \(url.lastPathComponent)", error: true); return }
        run("Writing the pattern…") { [self] in
            try await Ops.patWrite(link, track: k, slot: s, bytes: bytes)
            patList = try await Ops.patList(link)
            say("Imported: \(ProjectsView.trackName(k)) · pattern \(s + 1).")
        }
    }

    func copyPattern(from a: (Int, Int), to b: (Int, Int)) {
        run("Copying…") { [self] in
            try await Ops.patOp(link, op: 1, track: a.0, slot: a.1, track2: b.0, slot2: b.1)
            patList = try await Ops.patList(link)
            patSel = (b.0, b.1)
            note = nil
        }
    }

    func clearPattern(_ k: Int, _ s: Int) {
        run("Clearing…") { [self] in
            try await Ops.patOp(link, op: 2, track: k, slot: s)
            patList = try await Ops.patList(link)
            note = nil
        }
    }

    // MARK: backup

    func loadBackupList() {
        run("Reading the object list…") { [self] in
            bkList = try await Ops.bkListAll(link)
            note = nil
        }
    }

    func backup() {
        let samples = withSamples
        run("Reading…") { [self] in
            let tickLabel = tick()
            let r = try await Ops.readAll(link, withSamples: samples) { v, tag in
                tickLabel(v)
                Task { @MainActor in self.say("Reading: \(Restore.label(tag))") }
            }
            bkList = r.list
            let stamp = DeviceStamp(version: info?.version, proto: info?.proto ?? 0, switches: r.list.switches, magic: r.list.magic,
                                    bk: r.list.version, caps: r.list.caps)
            let file = BackupFile.encode(device: stamp, objects: r.objects)
            export = ExportItem(url: try FileIO.temp("optimist-\(FileIO.today()).optimist-backup", Data(file)))
            say("Backup saved: \(r.objects.count) items, \(file.count / 1024) KB")
        }
    }

    func openBackup(_ url: URL) {
        do {
            let f = try BackupFile.decode(Array(try FileIO.read(url)))
            restoreFile = f
            restoreName = url.lastPathComponent
            if let l = bkList { restoreSel = Set(Restore.plan(f.objects, l).filter { $0.why == nil }.map { $0.object.tag }) }
            note = nil
        } catch {
            restoreFile = nil
            say("Cannot read the backup file: \(error.localizedDescription)", error: true)
        }
    }

    var restorePlan: [RestoreItem] {
        guard let f = restoreFile, let l = bkList else { return [] }
        return Restore.plan(f.objects, l)
    }

    func restore() {
        guard let f = restoreFile, !restoreSel.isEmpty else { return }
        let sel = restoreSel
        run("Restoring…") { [self] in
            let tickLabel = tick()
            let r = try await Ops.restore(link, file: f.objects, selected: sel) { tickLabel($0) }
            say("Restored (the FM-1 restarts): \(r.written.count)" + (r.skipped.isEmpty ? "" : " (skipped: \(r.skipped.joined(separator: " ")))"))
            restoreFile = nil
        }
    }
}
