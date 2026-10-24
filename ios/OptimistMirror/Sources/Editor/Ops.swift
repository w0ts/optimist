// SPDX-License-Identifier: GPL-3.0-only
// What the web editor's Projects, Library and Backup tabs ask of the FM-1, as functions over a Transport (MirrorLink in the app,
// a fake device in the tests): INFO, PROJECT, BK_LIST / READ / BEGIN / DATA / COMMIT / END, SMP_BEGIN / WRITE / END (a
// sample slot), PAT_LIST / OP / READ / WRITE, UP_LIST / GET / PUT / STORE / LOAD / ERASE. The same order, timeouts and
// retries as web/editor.html (bkReadAll, bkRestore, bkWriteObj, bkCheckAll, smpSend, patReadAll, patWriteAll, bank).
import Foundation

protocol Transport: AnyObject {
    /// One request, its reply's bytes after the command; retries on a timeout.
    func exchange(_ cmd: Cmd, _ args: [UInt8], timeout: TimeInterval, retries: Int) async throws -> [UInt8]
}

extension Transport {
    func ask(_ cmd: Cmd, _ args: [UInt8] = [], timeout: TimeInterval = 0.3, retries: Int = 1) async throws -> [UInt8] {
        try await exchange(cmd, args, timeout: timeout, retries: retries)
    }
}

enum Sample {
    static let dataOffset = 512
    static let headerLength = 32 + 16 * 28
}

enum Ops {
    static let bkRc = ["ok", "arguments", "CRC", "the transport plays", "no flash", "not in this build", "too long",
                       "flash write failed", "not a valid object (it would not load)"]
    static let chunk = 256
    static let stopFirst = "Stop playback before loading or saving."

    static func rcText(_ rc: Int) -> String { rc < bkRc.count ? bkRc[rc] : "rc \(rc)" }

    static func info(_ t: Transport) async throws -> DeviceInfo {
        try DeviceInfo.parse(await t.ask(.info, [], timeout: 1.0, retries: 2))
    }

    // MARK: projects (PROJECT)

    /// op 0 load, 1 save, 2 query
    static func project(_ t: Transport, op: Int, slot: Int) async throws -> ProjectReply {
        let long = op == 1
        let r = try ProjectReply.parse(await t.ask(.project, [UInt8(op), UInt8(slot & 0x7F)], timeout: long ? 4 : 0.5, retries: long ? 0 : 1))
        if r.busy && op != 2 { throw WireError(stopFirst) }
        return r
    }

    // MARK: the projects themselves (PJ_OP, ed_proj.c)

    static let pjRc = [1: "a character the FM-1's NAME page has not", 3: stopFirst, 4: "no flash", 7: "not written (MEM FULL or flash)",
                       9: "a restore is running"]
    /// the NAME page's characters (op_name.c NM_SET), 12 at most
    static let pjNameChars = Set(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/#+")

    static func projName(_ s: String) -> String {
        String(s.uppercased().filter { pjNameChars.contains($0) }.prefix(12))
    }

    static func projList(_ t: Transport) async throws -> ProjList {
        try ProjList.parse(await t.ask(.pjOp, [0], timeout: 1.0, retries: 1))
    }

    /// op 1 save the work as project p, 2 open p, 3 clear p, 4 name p (stopped only; 1..3 write flash)
    static func projOp(_ t: Transport, op: Int, _ p: Int, name: String = "") async throws {
        let args = [UInt8(op), UInt8(p & 0x7F)] + (op == 4 ? Wire.strEnc(projName(name)) : [])
        let r = try await t.ask(.pjOp, args, timeout: 10, retries: 0)
        guard r.count >= 3 else { throw WireError("short reply") }
        if r[2] != 0 { throw WireError(pjRc[Int(r[2])] ?? "rc \(r[2])") }
    }

    // MARK: backup (BK_*)

    /// the device's whole object list, in pages
    static func bkListAll(_ t: Transport) async throws -> BkList {
        func page(_ first: Int) async throws -> BkList {
            try BkList.parse(await t.ask(.bkList, [4, UInt8(first)], timeout: 1.5, retries: 1))
        }
        var l = try await page(0)
        while l.version >= 3 && l.objs.count < l.n {
            let p = try await page(l.objs.count)
            guard p.first == l.objs.count, p.count > 0, p.n == l.n else { throw WireError("BK_LIST: a page out of order") }
            l.objs += p.objs
            if p.caps != nil { l.caps = p.caps }
        }
        l.count = l.objs.count
        return l
    }

    /// one object of the list, CRC-checked chunks
    static func readObject(_ t: Transport, _ o: BkObject, progress: ((Int) -> Void)? = nil) async throws -> [UInt8] {
        var data = [UInt8](repeating: 0, count: o.len)
        var off = 0, tries = 0
        while off < o.len {
            try Task.checkCancellation()
            let r = try BkChunk.parse(await t.ask(.bkRead, [UInt8(o.i & 0x7F)] + Wire.wr7(off, 3), timeout: 1.5, retries: 2))
            let c = Array(r.data.prefix(o.len - off))
            if r.i != o.i || r.off != off || c.isEmpty || Int(Wire.crc32(r.data)) != r.crc {
                tries += 1
                if tries > 2 { throw WireError("\(o.tag): bad chunk at \(off)") }
                continue
            }
            tries = 0
            data.replaceSubrange(off..<(off + c.count), with: c)
            off += c.count
            progress?(c.count)
        }
        guard Int(Wire.crc32(data)) == o.crc else { throw WireError("\(o.tag): CRC") }
        return data
    }

    /// Every object the device lists with data (the sample slots only when asked, the FM6 slot always), into backup objects.
    static func readAll(_ t: Transport, withSamples: Bool, progress: ((Double, String) -> Void)? = nil) async throws -> (list: BkList, objects: [BackupObject]) {
        let l = try await bkListAll(t)
        let pick = l.objs.filter { $0.hasData && ($0.kind != "usr" || $0.fm6 || withSamples) }
        let total = max(1, pick.reduce(0) { $0 + $1.len })
        var done = 0
        var objects: [BackupObject] = []
        for o in pick {
            let data = try await readObject(t, o) { n in done += n; progress?(Double(done) / Double(total), o.tag) }
            objects.append(BackupObject(tag: o.tag, kind: o.kind, fm6: o.fm6, data: data))
        }
        return (l, objects)
    }

    /// one storage object: BEGIN, DATA (a chunk again on rc 2), COMMIT (dry: check only, nothing written)
    static func writeObject(_ t: Transport, index i: Int, tag: String, data: [UInt8], dry: Bool = false, step: ((Int) -> Void)? = nil) async throws {
        let crc = Int(Wire.crc32(data))
        var r = try SlotRc.parse(await t.ask(.bkBegin, [UInt8(i & 0x7F)] + Wire.wr7(data.count, 3) + Wire.wr7(crc, 5), timeout: 4, retries: 0))
        if r.rc != 0 { throw WireError("\(tag): BEGIN: \(rcText(r.rc))") }
        var off = 0, tries = 0
        while off < data.count {
            try Task.checkCancellation()
            let c = Array(data[off..<min(off + chunk, data.count)])
            let a = try await t.ask(.bkData, [UInt8(i & 0x7F)] + Wire.wr7(off, 3) + Wire.wr7(Int(Wire.crc32(c)), 5) + Wire.pack7(c), timeout: 1.5, retries: 1)
            guard a.count >= 5 else { throw WireError("short reply") }
            let rc = Int(a[4])
            if rc == 2 && tries < 2 { tries += 1; continue }
            if rc != 0 { throw WireError("\(tag): DATA \(off): \(rcText(rc))") }
            tries = 0
            off += c.count
            step?(c.count)
        }
        let a = try await t.ask(.bkCommit, dry ? [UInt8(i & 0x7F), 1] : [UInt8(i & 0x7F)], timeout: 4, retries: 0)
        guard a.count >= 2 else { throw WireError("short reply") }
        r = SlotRc(slot: Int(a[0]), rc: Int(a[1]))
        if r.rc != 0 { throw CommitRefused(tag: tag, rc: r.rc) }
    }

    struct CommitRefused: Error, LocalizedError {
        var tag: String, rc: Int
        var errorDescription: String? { "\(tag): COMMIT: \(Ops.rcText(rc))" }
    }

    /// A check-only pass over the storage objects (kind st): the tags the device would not load, [] if all would.
    static func checkAll(_ t: Transport, _ list: BkList, _ items: [RestoreItem], step: ((Int) -> Void)? = nil) async throws -> [String] {
        guard list.version >= 4 else { return [] }
        var bad: [String] = []
        for x in items {
            guard let d = x.device else { continue }
            do { try await writeObject(t, index: d.i, tag: x.object.tag, data: x.object.data, dry: true, step: step) }
            catch let e as CommitRefused where e.rc == 8 { bad.append(x.object.tag) }
        }
        return bad
    }

    static func end(_ t: Transport, done: Bool) async throws -> Int {
        let a = try await t.ask(.bkEnd, [done ? 1 : 0], timeout: 1.5, retries: 0)
        return a.isEmpty ? 0 : Int(a[0])
    }

    /// a sample slot's 480 B header and ADPCM data into USR k+1: BEGIN, WRITE x n, END
    static func sendSample(_ t: Transport, slot k: Int, header: [UInt8], data: [UInt8], step: ((Int) -> Void)? = nil) async throws {
        var r = try SlotRc.parse(await t.ask(.smpBegin, [UInt8(k & 0x7F)], timeout: 1, retries: 0))
        if r.rc != 0 { throw WireError(r.rc == 3 ? stopFirst : "begin rc \(r.rc)") }
        var off = 0
        while off < data.count {
            try Task.checkCancellation()
            let a = Sample.dataOffset + off
            let c = Array(data[off..<min(off + 256, data.count)])
            let w = try await t.ask(.smpWrite, [UInt8(k & 0x7F), UInt8(a & 0x7F), UInt8((a >> 7) & 0x7F), UInt8((a >> 14) & 0x7F)] + Wire.pack7(c), timeout: 1, retries: 1)
            guard w.count >= 5 else { throw WireError("short reply") }
            if w[4] != 0 { throw WireError(w[4] == 5 ? stopFirst : "write at 0x\(String(a, radix: 16)) rc \(w[4])") }
            off += c.count
            step?(c.count)
        }
        r = try SlotRc.parse(await t.ask(.smpEnd, [UInt8(k & 0x7F)] + Wire.pack7(header), timeout: 2, retries: 0))
        if r.rc != 0 { throw WireError("end rc \(r.rc)") }
    }

    struct RestoreResult: Equatable {
        var written: [String]
        var skipped: [String]
        var rc: Int
    }

    /// The ticked objects back (web: bkRestore): the whole set checked first, nothing written if the device would refuse one;
    /// then the storage objects through BEGIN / DATA / COMMIT, the sample slots through the sample upload, BK_END (the FM-1
    /// restarts and loads it all).
    static func restore(_ t: Transport, file: [BackupObject], selected: Set<String>, progress: ((Double) -> Void)? = nil) async throws -> RestoreResult {
        let list = try await bkListAll(t)
        let all = Restore.plan(file, list)
        let plan = all.filter { selected.contains($0.object.tag) && $0.why == nil }
        let skipped = all.filter { selected.contains($0.object.tag) && $0.why != nil }.map { $0.object.tag }
        let st = plan.filter { $0.object.kind == "st" }
        let total = max(1, plan.reduce(0) { $0 + $1.object.data.count } + (list.version >= 4 ? st.reduce(0) { $0 + $1.object.data.count } : 0))
        var done = 0
        let step: (Int) -> Void = { n in done += n; progress?(Double(done) / Double(total)) }
        let bad = try await checkAll(t, list, st, step: step)
        if !bad.isEmpty {                                  // nothing is written: the device would not load these
            _ = try await end(t, done: false)
            throw WireError("\(bad.joined(separator: " ")): \(bkRc[8]). Nothing was written.")
        }
        for x in plan where x.object.kind != "usr" && x.object.kind != "snp" {
            try await writeObject(t, index: x.device!.i, tag: x.object.tag, data: x.object.data, step: step)
        }
        for x in plan where x.object.kind == "usr" && !x.object.fm6 {
            guard x.object.data.count >= Sample.dataOffset, let n = Int(String(x.object.tag.last!)) else { throw WireError("\(x.object.tag): not a sample slot") }
            let d = x.object.data
            try await sendSample(t, slot: n - 1, header: Array(d[0..<Sample.headerLength]), data: Array(d[Sample.dataOffset...]))
            step(d.count)
        }
        let rc = try await end(t, done: true)
        return RestoreResult(written: plan.map { $0.object.tag }, skipped: skipped, rc: rc)
    }

    // MARK: sections (one project of the song)

    static func sectionTag(_ k: Int) -> String { "S" + String(format: "%02d", k + 1) + " " }

    static func sectionObject(_ list: BkList, _ k: Int) -> BkObject? { list.objs.first { $0.tag == sectionTag(k) } }

    // MARK: patterns (PAT_*)

    static let patMax = 1400
    static let patChunk = 256

    static func patList(_ t: Transport) async throws -> PatList { try PatList.parse(await t.ask(.patList, [0])) }

    /// pattern (k, s)'s record in chunks (an empty slot: [])
    static func patRead(_ t: Transport, track k: Int, slot s: Int) async throws -> [UInt8] {
        var out: [UInt8] = []
        var off = 0, total = 0
        repeat {
            let r = try PatChunk.parse(await t.ask(.patRead, [UInt8(k & 3), UInt8(s & 15), UInt8(off & 0x7F), UInt8((off >> 7) & 0x7F)]))
            total = r.total
            if total > patMax || (total > off && r.data.isEmpty) { throw WireError("pattern read") }
            out += r.data
            off += r.data.count
        } while off < total
        return out
    }

    /// a record into slot (k, s): in order, the last chunk stores it; a busy device is asked again
    static func patWrite(_ t: Transport, track k: Int, slot s: Int, bytes: [UInt8]) async throws {
        var off = 0, tries = 0
        repeat {
            let c = Array(bytes[off..<min(off + patChunk, bytes.count)])
            let a = try await t.ask(.patWrite, [UInt8(k & 3), UInt8(s & 15), UInt8(off & 0x7F), UInt8((off >> 7) & 0x7F),
                                                UInt8(bytes.count & 0x7F), UInt8((bytes.count >> 7) & 0x7F)] + Wire.pack7(c), timeout: 1.5, retries: 1)
            guard a.count >= 5 else { throw WireError("short reply") }
            let rc = Int(a[4])
            if rc == 3 && tries < 5 { tries += 1; try await Task.sleep(for: .milliseconds(120)); off = 0; continue }
            if rc != 0 {
                throw WireError("pattern write: " + (["", "order", "not a pattern of this track", "busy", "not stored (MEM FULL?)"].indices.contains(rc)
                                                      ? ["", "order", "not a pattern of this track", "busy", "not stored (MEM FULL?)"][rc] : "rc \(rc)"))
            }
            off += c.count
        } while off < bytes.count
    }

    /// op 1 copy (k, a) -> (k2, b), 2 clear (k, a)
    static func patOp(_ t: Transport, op: Int, track k: Int, slot a: Int, track2 k2: Int = 0, slot2 b: Int = 0) async throws {
        let r = try await t.ask(.patOp, [UInt8(op & 3), UInt8(k & 3), UInt8(a & 15), UInt8(k2 & 3), UInt8(b & 15)], timeout: 1.5, retries: 0)
        guard r.count >= 2 else { throw WireError("short reply") }
        if r[1] != 0 { throw WireError("flash write failed (rc \(r[1]))") }
    }

    // MARK: user presets (UP_*)

    static let flashTimeout = 2.5

    static func bankList(_ t: Transport) async throws -> (total: Int, slots: [UpSlot]) {
        let first = try UpList.parse(await t.ask(.upList, [0, 16]))
        var slots = first.slots
        while slots.count < first.total {
            let r = try UpList.parse(await t.ask(.upList, [UInt8(slots.count), UInt8(min(16, first.total - slots.count))]))
            if r.count == 0 { break }
            slots += r.slots
        }
        return (first.total, slots)
    }

    static func bankGet(_ t: Transport, slot: Int, pcount: Int) async throws -> UpPatch {
        try UpPatch.parse(await t.ask(.upGet, [UInt8(slot & 0x7F)]), pcount: pcount)
    }

    private static func rc(_ a: [UInt8]) throws -> Int { try SlotRc.parse(a).rc }

    static func bankPut(_ t: Transport, slot: Int, engine: Int, name: String, p: [Int], pattern: [[Int]]) async throws -> Int {
        let pat = (0..<16).flatMap { i -> [UInt8] in
            let x = i < pattern.count && pattern[i].count >= 2 ? pattern[i] : [0, 0]
            return [UInt8(x[0] & 0x7F), UInt8(x[1] & 7)]
        }
        let args = [UInt8(slot & 0x7F), UInt8(engine & 0x7F)] + Wire.strEnc(Names.upName(name)) + p.flatMap { Wire.v14enc($0) } + pat
        return try rc(await t.ask(.upPut, args, timeout: flashTimeout, retries: 0))
    }

    static func bankStore(_ t: Transport, slot: Int, name: String) async throws -> Int {
        try rc(await t.ask(.upStore, [UInt8(slot & 0x7F)] + Wire.strEnc(name.isEmpty ? "" : Names.upName(name)), timeout: flashTimeout, retries: 0))
    }

    static func bankLoad(_ t: Transport, slot: Int) async throws -> Int {
        try rc(await t.ask(.upLoad, [UInt8(slot & 0x7F)], timeout: 1, retries: 1))
    }

    static func bankErase(_ t: Transport, slot: Int) async throws -> Int {
        try rc(await t.ask(.upErase, [UInt8(slot & 0x7F)], timeout: flashTimeout, retries: 0))
    }

    /// the parameters' labels (DESC of each id), for the library's keys; nil when the firmware has no DESC (a backup-only build)
    static func paramLabels(_ t: Transport, pcount: Int) async throws -> (labels: [String], defaults: [Int])? {
        var labels: [String] = [], defaults: [Int] = []
        for i in 0..<pcount {
            do {
                var r = Reader(try await t.ask(.desc, [0, UInt8(i & 0x7F), UInt8(i >> 7)], timeout: i == 0 ? 0.4 : 0.3, retries: i == 0 ? 0 : 1))
                _ = try r.b(); _ = try r.id(); _ = try r.b()     // scope, id (2 x 7 bit), fmt
                _ = try r.v(); _ = try r.v()                     // min, max
                defaults.append(try r.v())
                labels.append(try r.s())
            } catch let e as WireError where e.isTimeout && i == 0 {
                return nil
            }
        }
        return (labels, defaults)
    }
}

enum Names {
    /// a user preset's name on the device: printable ASCII, 12 characters
    static func upName(_ s: String) -> String {
        let t = String(s.unicodeScalars.filter { $0.value >= 0x20 && $0.value <= 0x7E }.map(Character.init))
            .trimmingCharacters(in: .whitespaces)
        let n = String(t.prefix(12))
        return n.isEmpty ? "PATCH" : n
    }

    static func slotName(_ i: Int) -> String { "U" + String(format: "%02d", i + 1) }
    static func sectionLetter(_ k: Int) -> String { String(UnicodeScalar(UInt8(65 + k))) }
}
