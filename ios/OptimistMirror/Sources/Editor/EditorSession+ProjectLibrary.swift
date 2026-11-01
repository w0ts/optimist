// SPDX-License-Identifier: GPL-3.0-only
// The project library's actions (the library itself: ProjectLibrary). Wording as the Projects screen: source → destination.
import Foundation

extension EditorSession {
    func refreshLibrary() { setLibraryItems(projectLibrary.list()) }

    /// "Slot N → Library", or with `saveFirst` "Active project → Library" (the work is saved into slot N first, then copied)
    func libSave(_ p: Int, saveFirst: Bool) {
        run(saveFirst ? "Saving the Active project into slot \(p + 1)…" : "Reading slot \(p + 1)…") { [self] in
            if saveFirst { try await Ops.projOp(link, op: 1, p) }
            guard let c = try await readSlot(p) else { say("Slot \(p + 1) is empty.", error: true); return }
            try projectLibrary.add(c)
            refreshLibrary()
            say(saveFirst ? "Active project → Library: saved." : "Slot \(p + 1) → Library: saved.")
        }
    }

    /// "Library → Slot N" (the slot's project is replaced; an open slot is opened again), and with `openIt` the project is opened
    /// ("Library → Active project" writes into the active slot)
    func libLoad(_ e: ProjectLibrary.Entry, _ p: Int, openIt: Bool) {
        run("Library → slot \(p + 1)…") { [self] in
            try await writeSlot(p, try projectLibrary.read(e))
            if openIt { try await Ops.projOp(link, op: 2, p) }
            try await readProjects()
            say(openIt ? "Library → Active project: \(e.name) opened (slot \(p + 1))." : "Library → slot \(p + 1): loaded.")
        }
    }

    /// "Swap with slot N": slot N goes into the library first; only then is the chosen project written into it and opened
    func libSwap(_ e: ProjectLibrary.Entry, _ p: Int) {
        run("Swapping with slot \(p + 1)…") { [self] in
            let c = try projectLibrary.read(e)
            try await ProjectSwap.run(saveSlot: { [self] in
                if let held = try await readSlot(p) { try projectLibrary.add(held) }
            }, load: { [self] in
                try await writeSlot(p, c)
                try await Ops.projOp(link, op: 2, p)
            })
            try await readProjects()
            refreshLibrary()
            say("Swap with slot \(p + 1): \(e.name) opened; the old slot \(p + 1) is in the Library.")
        }
    }

    func libRename(_ e: ProjectLibrary.Entry, _ name: String) {
        do { try projectLibrary.rename(e, to: name); refreshLibrary() } catch { say("Rename failed: \(error.localizedDescription)", error: true) }
    }

    func libDelete(_ e: ProjectLibrary.Entry) {
        do { try projectLibrary.delete(e); refreshLibrary() } catch { say("Delete failed: \(error.localizedDescription)", error: true) }
    }

    func libAdd(file url: URL) {
        do { let e = try projectLibrary.add(file: url); refreshLibrary(); say("File → Library: \(e.name) added.") } catch { say("Not a project file: \(error.localizedDescription)", error: true) }
    }
}
