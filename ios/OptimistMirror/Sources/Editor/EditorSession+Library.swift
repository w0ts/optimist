// SPDX-License-Identifier: GPL-3.0-only
// The Library screen: the FM-1's 32 user presets (UP_*) and the app's own library of patches (felucca-library files).
import Foundation

extension EditorSession {
    // MARK: the library file

    func loadLibrary() {
        do {
            let s = try store.load()
            library = s.patches
            layout = s.layout
        } catch {
            say("Cannot read the library: \(error.localizedDescription)", error: true)
        }
    }

    func saveLibrary() {
        do { try store.save(library, layout: layout) } catch { say("Cannot save the library: \(error.localizedDescription)", error: true) }
    }

    func addToLibrary(_ patches: [Patch]) {
        library += patches.map { p in
            var q = LibraryFile.clean(name: p.name, engine: p.engine, engineName: p.engineName, p: p.p.map { $0 as Any? ?? NSNull() }, pattern: p.pattern,
                                      tags: p.tags, created: p.created, modified: p.modified, slot: p.slot, dx7: p.dx7)
            q.id = UUID()
            return q
        }
        libSel = library.last?.id
        saveLibrary()
    }

    func updatePatch(_ id: UUID, _ change: (inout Patch) -> Void) {
        guard let i = library.firstIndex(where: { $0.id == id }) else { return }
        change(&library[i])
        library[i].modified = BackupFile.isoNow()
        saveLibrary()
    }

    func deletePatch(_ id: UUID) {
        library.removeAll { $0.id == id }
        if libSel == id { libSel = nil }
        saveLibrary()
    }

    func duplicatePatch(_ id: UUID) {
        guard var p = library.first(where: { $0.id == id }) else { return }
        let now = BackupFile.isoNow()
        p.name = String((p.name + " 2").prefix(32))
        p.created = now
        p.modified = now
        addToLibrary([p])
    }

    /// The device's parameter layout becomes the library's; patches stored with another layout are mapped by key (web: libAdopt).
    func adoptLayout(keys: [String]) {
        guard let info else { return }
        let engines = info.engines
        if layout.keys != keys || layout.engines != engines {
            for i in library.indices {
                let q = library[i]
                let name = q.engineName.isEmpty ? (q.engine >= 0 && q.engine < layout.engines.count ? layout.engines[q.engine] : "") : q.engineName
                library[i].p = LibraryFile.remapParams(q.p, from: layout.keys, to: keys)
                let e = name.isEmpty ? q.engine : (engines.firstIndex(of: name) ?? -1)
                if e >= 0 && e < engines.count { library[i].engine = e; library[i].engineName = engines[e] }
            }
        }
        layout = LibraryContext(keys: keys, engines: engines, firmware: info.version, pe0: info.pe0)
        saveLibrary()
    }

    /// the layout files are written with: the device's when it is there
    var context: LibraryContext {
        guard ready, let info, let keys = layout.keys else { return layout }
        return LibraryContext(keys: keys, engines: info.engines, firmware: info.version, pe0: info.pe0)
    }

    // MARK: the device's bank

    func loadBank() {
        run("Reading the user presets…") { [self] in
            if let labels = try await Ops.paramLabels(link, pcount: info?.pcount ?? 0), let info {
                defaults = labels.defaults
                adoptLayout(keys: LibraryFile.paramKeys(labels: labels.labels, pe0: info.pe0, pcount: info.pcount))
            }
            bank = try await Ops.bankList(link)
            note = nil
        }
    }

    func refreshSlot(_ slot: Int) async throws {
        let r = try UpList.parse(await link.ask(.upList, [UInt8(slot), 1]))
        if bank != nil, let s = r.slots.first, slot < bank!.slots.count { bank!.slots[slot] = s }
    }

    private func check(_ rc: Int) throws {
        if rc == 3 { throw WireError("The FM-1 is playing: stop it (PLAY), then try again.") }
        if rc != 0 { throw WireError("Write failed (rc \(rc))") }
    }

    func bankLoad(_ slot: Int) {
        run("Loading…") { [self] in
            let rc = try await Ops.bankLoad(link, slot: slot)
            if rc != 0 { say("Empty slot: \(Names.slotName(slot))", error: true); return }
            say("Loaded \(Names.slotName(slot)) \(bank?.slots[slot].name ?? "")")
        }
    }

    func bankStore(_ slot: Int, name: String) {
        run("Storing…") { [self] in
            try check(await Ops.bankStore(link, slot: slot, name: name.trimmingCharacters(in: .whitespaces)))
            try await refreshSlot(slot)
            say("Stored on the device: \(Names.slotName(slot)) \(bank?.slots[slot].name ?? "")")
        }
    }

    func bankErase(_ slot: Int) {
        run("Erasing…") { [self] in
            try check(await Ops.bankErase(link, slot: slot))
            try await refreshSlot(slot)
            say("Erased \(Names.slotName(slot))")
        }
    }

    func bankToLibrary(_ slot: Int) {
        run("Reading \(Names.slotName(slot))…") { [self] in
            guard let info else { return }
            let u = try await Ops.bankGet(link, slot: slot, pcount: info.pcount)
            if !u.used { say("Empty slot: \(Names.slotName(slot))", error: true); return }
            addToLibrary([LibraryFile.clean(name: u.name, engine: u.engine, engineName: u.engine < info.engines.count ? info.engines[u.engine] : "",
                                            p: u.p, pattern: u.pattern, tags: ["device"], created: nil, modified: nil, slot: nil, dx7: nil)])
            say("Copied to the library: \(u.name)")
        }
    }

    func engineOK(_ p: Patch) -> Bool {
        guard let info else { return false }
        if p.dx7 != nil { return info.engines.contains("FM6") }
        return p.engine >= 0 && p.engine < info.nengines && (p.engineName.isEmpty || info.engines[p.engine] == p.engineName)
    }

    /// A full parameter set for UP_PUT: values the patch lacks get the DESC default where it is known (below the engine's own
    /// eight), else 0.
    func fullParams(_ p: Patch) -> [Int] {
        guard let info else { return [] }
        return (0..<info.pcount).map { i in
            if i < p.p.count, let v = p.p[i] { return v }
            return i < info.pe0 && i < defaults.count ? defaults[i] : 0
        }
    }

    func putPatch(_ id: UUID, slot: Int) {
        guard let p = library.first(where: { $0.id == id }) else { return }
        if p.dx7 != nil { say("A DX7 voice cannot go to a user preset slot", error: true); return }
        guard engineOK(p) else { say("This engine is not in this firmware: \(p.engineName.isEmpty ? String(p.engine) : p.engineName)", error: true); return }
        run("Writing…") { [self] in
            try check(await Ops.bankPut(link, slot: slot, engine: p.engine, name: Names.upName(p.name), p: fullParams(p), pattern: p.pattern ?? []))
            try await refreshSlot(slot)
            say("Written to the device: \(Names.slotName(slot)) \(Names.upName(p.name))")
        }
    }

    func exportBank() {
        run("Reading the user presets…") { [self] in
            guard let info, let b = bank else { return }
            var out: [Patch] = []
            for x in b.slots where x.used {
                say("Reading \(Names.slotName(x.slot))…")
                let u = try await Ops.bankGet(link, slot: x.slot, pcount: info.pcount)
                if u.used {
                    var p = LibraryFile.clean(name: u.name, engine: u.engine, engineName: u.engine < info.engines.count ? info.engines[u.engine] : "",
                                              p: u.p, pattern: u.pattern, tags: [], created: nil, modified: nil, slot: x.slot, dx7: nil)
                    p.slot = x.slot
                    out.append(p)
                }
            }
            export = ExportItem(url: try FileIO.temp("felucca-bank-\(FileIO.today()).json", try LibraryFile.encode(kind: "bank", patches: out, ctx: context)))
            say("Exported \(out.count) presets.")
        }
    }

    // MARK: files

    func exportPatch(_ id: UUID) {
        guard let p = library.first(where: { $0.id == id }) else { return }
        shareFile("felucca-patch-\(FileIO.slug(p.name)).json", kind: "patch", [p])
    }

    func exportLibrary() { shareFile("felucca-library-\(FileIO.today()).json", kind: "library", library) }

    private func shareFile(_ name: String, kind: String, _ patches: [Patch]) {
        do {
            export = ExportItem(url: try FileIO.temp(name, try LibraryFile.encode(kind: kind, patches: patches, ctx: context)))
        } catch { say("Cannot write the file: \(error.localizedDescription)", error: true) }
    }

    func importLibrary(_ urls: [URL]) {
        var n = 0, skipped = 0
        for url in urls {
            do {
                if url.pathExtension.lowercased() == "syx" { throw WireError("DX7 .syx files are imported in the web editor") }
                let r = try LibraryFile.decode(try FileIO.read(url), ctx: context)
                if layout.keys == nil, let k = r.keys {                // no layout yet: the file's becomes the library's
                    layout = LibraryContext(keys: k, engines: r.engines ?? [], firmware: "", pe0: r.pe0)
                }
                var patches = r.patches
                if r.kind == "bank" { for i in patches.indices where !patches[i].tags.contains("bank") { patches[i].tags.append("bank") } }
                addToLibrary(patches)
                n += patches.count
                skipped += r.skipped
            } catch {
                say("Cannot read the file: \(url.lastPathComponent): \(error.localizedDescription)", error: true)
                return
            }
        }
        say("Imported: \(n) patches" + (skipped > 0 ? " (\(skipped) unreadable)" : ""))
    }
}
