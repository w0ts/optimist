// SPDX-License-Identifier: GPL-3.0-only
// Projects: the FM-1's projects (as many as the installed MEMORY layout keeps: 2, 4, 16; PJ_OP 110: open, save the work there, rename, clear, a project as a .fm1proj file), the open project's sections A..
// (PROJECT load / save, a section as a .optimist-backup file) and the per-track patterns
// (PAT_*: a pattern as an optimist-pattern .json), as the web editor's Projects tab and librarian.
import SwiftUI
import UniformTypeIdentifiers

struct ProjectsView: View {
    @ObservedObject var s: EditorSession
    @State private var ask: Confirmation?
    @State private var importing = false
    @State private var target: Target?
    @State private var track = 0
    /// the tracks as the FM-1 and the rest of the app name them (the user, 2026-10-25: "use T1/T2/T3/DR like everywhere else")
    static func trackName(_ k: Int) -> String { k == 3 ? "DR" : "T\(k + 1)" }
    @State private var prompt: Prompt?
    @State private var text = ""

    struct Prompt {
        let title: String
        let done: (String) -> Void
    }

    enum Target { case section(Int), pattern(Int, Int), project(Int) }

    private static let state = ["empty", "used", "SLOOP 2.4 project", "another firmware's"]

    var body: some View {
        NavigationStack {
            List {
                if s.route.usbOnlyLine(.projects) == nil || !s.ready(.proj) {   // (Bluetooth with the slots: the grey line below instead)
                    Section { LinkStatus(s, .projects) }.listRowBackground(Color.clear)
                }
                if s.ready(.proj), let l = s.projects {
                    Section { activeHeader(l) }
                    Section {
                        ForEach(0..<s.projectCount, id: \.self) { p in projectRow(l, p) }
                    } header: {
                        Text("Project slots")
                    } footer: {
                        if s.bkList?.caps?.projects == 1 { Text("SD card mode: the FM-1 keeps 1 project in its flash.") }
                    }
                }
                LibrarySection(s: s)
                if s.ready(.proj), let line = s.route.usbOnlyLine(.projects) {
                    Section { Text(line).font(.footnote).foregroundStyle(.secondary) }.listRowBackground(Color.clear)
                }
                if s.ready(.projects) {
                    Section(s.projects.map { sectionsTitle($0) } ?? "Sections") {
                        ForEach(0..<s.sectionUsed.count, id: \.self) { k in sectionRow(k) }
                    }
                    if let l = s.patList, let len = l.len {
                        Section("Patterns") { patterns(l, len) }
                    }
                }
                Section { NoteView(s: s) }
            }
            .navigationTitle("Projects")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button { s.loadProjects() } label: { Image(systemName: "arrow.clockwise") }.disabled(!s.idle)
                }
            }
            .confirm($ask)
            .fileImporter(isPresented: $importing, allowedContentTypes: [.data, .json]) { r in
                guard case .success(let url) = r, let t = target else { return }
                switch t {
                case .section(let k):
                    ask = Confirmation(title: "Replace section \(Names.sectionLetter(k)) with the file's? Stopped only. The FM-1 restarts when done.") { s.importSection(k, url) }
                case .project(let p):
                    let used = s.projects.map { p < $0.used.count && $0.used[p] } ?? false
                    let go = { s.importProject(p, url) }
                    let held = s.projects.map { name($0, p) } ?? "a project"
                    let again = s.projects?.open == p ? " It is the Active project: it is opened again." : ""
                    if used { ask = Confirmation(title: "File → slot \(p + 1): slot \(p + 1) holds \(held). Replace it with the file's project?\(again) Stopped only.", action: go) } else { go() }
                case .pattern(let k, let p):
                    ask = Confirmation(title: "Replace \(Self.trackName(k)) · pattern \(p + 1) with the file's?") { s.importPattern(k, p, url) }
                }
            }
        }
        .alert(prompt?.title ?? "", isPresented: Binding(get: { prompt != nil }, set: { if !$0 { prompt = nil } })) {
            TextField("", text: $text).textInputAutocapitalization(.characters).autocorrectionDisabled()
            Button("OK") { prompt?.done(text); prompt = nil }
            Button("Cancel", role: .cancel) { prompt = nil }
        } message: { Text("Up to 12 of A-Z 0-9 space - . _ / # +") }
        .task(id: s.ready(.projects) || s.ready(.proj)) {
            if (s.ready(.projects) || s.ready(.proj)) && s.sectionUsed.isEmpty && s.projects == nil { s.loadProjects() }
        }
    }

    private func name(_ l: ProjList, _ p: Int) -> String {
        p < l.names.count && !l.names[p].isEmpty ? l.names[p] : "Project \(p + 1)"
    }

    private func sectionsTitle(_ l: ProjList) -> String { "Sections of the Active project (\(name(l, l.open)))" }

    /// a menu item with the one-line reason it is off (a tooltip on the Mac, a second line in the menu)
    @ViewBuilder private func item(_ title: String, off why: String?, role: ButtonRole? = nil, _ go: @escaping () -> Void) -> some View {
        Button(role: role, action: go) {
            Text(title)
            if let why { Text(why) }
        }.disabled(why != nil).help(why ?? "")
    }

    /// "Active project: NAME (slot N)" or "not saved yet", and the one button that saves it (into its own slot, or a picked one)
    @ViewBuilder private func activeHeader(_ l: ProjList) -> some View {
        let o = l.open
        let saved = o < l.used.count && l.used[o]
        VStack(alignment: .leading, spacing: 4) {
            Text(saved ? "Active project: \(name(l, o)) (slot \(o + 1))" : "Active project: not saved yet").font(.headline)
            Text("What plays on the FM-1 now. Saving writes it into a slot; opening a slot makes that slot the Active project.").font(.caption).foregroundStyle(.secondary)
        }
        if saved {
            Button("Save active project → slot \(o + 1)") { saveActive(l, o) }.disabled(!s.idle)
        } else {
            Menu("Save active project → slot…") {
                ForEach(0..<s.projectCount, id: \.self) { p in
                    Button("Slot \(p + 1)" + (p < l.used.count && l.used[p] ? " (\(name(l, p)))" : " (empty)")) { saveActive(l, p) }
                }
            }.disabled(!s.idle)
        }
    }

    private func saveActive(_ l: ProjList, _ p: Int) {
        let held = p < l.used.count && l.used[p] && l.open != p
        ask = Confirmation(title: held ? "Active project → slot \(p + 1): slot \(p + 1) holds \(name(l, p)). Replace it with the Active project?" : "Save the Active project into slot \(p + 1)?") { s.projOp(1, p) }
    }

    private func projectRow(_ l: ProjList, _ p: Int) -> some View {
        let used = p < l.used.count && l.used[p]
        let active = l.open == p
        let n = name(l, p)
        let k = p + 1
        return HStack {
            Text("\(k)").font(.headline).monospacedDigit().frame(width: 28)
            Text(n).foregroundStyle(used || active ? .primary : .secondary)
            if active { Text("Active").font(.caption.bold()).foregroundStyle(Color.accentColor) }
            Spacer()
            if !used { Text("empty").foregroundStyle(.secondary) }
            Menu {
                item("Slot \(k) → Active project", off: active ? "Slot \(k) is already the Active project." : used ? nil : "Slot \(k) is empty.") {
                    ask = Confirmation(title: "Slot \(k) → Active project: open \(n) on the FM-1. The Active project's unsaved changes are replaced.") { s.projOp(2, p) }
                }
                item("Active project → slot \(k)", off: nil) { saveActive(l, p) }
                item("Slot \(k) → File…", off: used ? nil : "Slot \(k) is empty: nothing to export.") { s.exportProject(p, saveFirst: false) }
                item("Active project → File…", off: active ? nil : "Only the Active project's own slot (\(l.open + 1)) exports it.") {
                    ask = Confirmation(title: "Save the Active project into slot \(k), then export slot \(k) to a file?") { s.exportProject(p, saveFirst: true) }
                }
                item("File → Slot \(k)…", off: nil) { target = .project(p); importing = true }
                item("Rename slot \(k)…", off: nil) {
                    text = p < l.names.count ? l.names[p] : ""
                    prompt = Prompt(title: "Name of slot \(k)") { s.projOp(4, p, name: $0) }
                }
                item("Clear slot \(k)…", off: used ? (active ? "Slot \(k) is the Active project: save it elsewhere first." : nil) : "Slot \(k) is already empty.", role: .destructive) {
                    ask = Confirmation(title: "Clear slot \(k) (\(n))? Everything stored in it is erased.") { s.projOp(3, p) }
                }
            } label: { Image(systemName: "ellipsis.circle") }
                .disabled(!s.idle)
        }
    }

    private func sectionRow(_ k: Int) -> some View {
        let u = k < s.sectionUsed.count ? s.sectionUsed[k] : nil   // (a row drawn once more after a reconnect emptied the list: crashed)
        let letter = Names.sectionLetter(k)
        let hasObject = s.bkList.flatMap { Ops.sectionObject($0, k) } != nil
        return HStack {
            Text(letter).font(.headline).frame(width: 28)
            Text(u.map { $0 < Self.state.count ? Self.state[$0] : "used" } ?? "unknown").foregroundStyle(.secondary)
            Spacer()
            Menu {
                item("Section \(letter) → Active project", off: u == 1 ? nil : "Section \(letter) holds nothing to load.") {
                    ask = Confirmation(title: "Section \(letter) → Active project? The current sound and pattern will be replaced.") { s.projectOp(0, k) }
                }
                item("Active project → Section \(letter)", off: nil) { ask = Confirmation(title: "Active project → Section \(letter): overwrite section \(letter)?") { s.projectOp(1, k) } }
                Button("Export…") { s.exportSection(k) }.disabled(u != 1 || !hasObject)
                Button("Import…") { target = .section(k); importing = true }.disabled(!hasObject)
            } label: { Image(systemName: "ellipsis.circle") }
                .disabled(!s.idle)
        }
    }

    @ViewBuilder private func patterns(_ l: PatList, _ len: [[Int]]) -> some View {
        Picker("Track", selection: $track) { ForEach(0..<4, id: \.self) { Text(Self.trackName($0)).tag($0) } }.pickerStyle(.segmented)
        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 4), count: 8), spacing: 4) {
            ForEach(0..<16, id: \.self) { slot in
                let n = len[track][slot]
                let on = s.patSel?.track == track && s.patSel?.slot == slot
                Button { s.patSel = (track, slot) } label: {
                    Text(l.slotText(track, slot)).italic(l.unsaved(track, slot)).frame(maxWidth: .infinity, minHeight: 34)
                        .background(on ? Color.accentColor.opacity(0.35) : Color.secondary.opacity(n > 0 ? 0.25 : 0.1), in: RoundedRectangle(cornerRadius: 6))
                }.buttonStyle(.plain)
            }
        }
        if let sel = s.patSel {
            let used = len[sel.track][sel.slot] > 0
            Text("\(Self.trackName(sel.track)) · pattern \(sel.slot + 1)").font(.subheadline.bold())
            HStack {
                Button("Export…") { s.exportPattern(sel.track, sel.slot) }.disabled(!used)
                Button("Import…") { target = .pattern(sel.track, sel.slot); importing = true }
                Menu("Copy to") {
                    ForEach(0..<4, id: \.self) { k in
                        Menu(Self.trackName(k)) {
                            ForEach(0..<16, id: \.self) { p in
                                Button("Pattern \(p + 1)") { copy(from: sel, to: (k, p), over: len[k][p] > 0) }
                            }
                        }
                    }
                }.disabled(!used)
                Button("Clear", role: .destructive) {
                    ask = Confirmation(title: "Clear pattern \(sel.slot + 1)?") { s.clearPattern(sel.track, sel.slot) }
                }.disabled(!used)
            }
            .buttonStyle(.bordered)
            .disabled(!s.idle)
            if l.live != nil {                                // (issue 142: edits not saved, "*": SAVE or RELOAD, as the web editor)
                HStack {
                    Button("Save") { s.savePattern(sel.track, sel.slot) }
                    Button("Reload") {
                        ask = Confirmation(title: "RELOAD \(Self.trackName(sel.track)) \(sel.slot + 1)? Its edits not saved go, the saved pattern plays again.") {
                            s.reloadPattern(sel.track, sel.slot)
                        }
                    }
                }
                .buttonStyle(.bordered)
                .disabled(!s.idle || !l.unsaved(sel.track, sel.slot))
            }
        }
    }

    private func copy(from a: (track: Int, slot: Int), to b: (Int, Int), over: Bool) {
        let go = { s.copyPattern(from: (a.track, a.slot), to: b) }
        if over { ask = Confirmation(title: "Pattern \(b.1 + 1) holds a pattern: replace it?", action: go) } else { go() }
    }
}
