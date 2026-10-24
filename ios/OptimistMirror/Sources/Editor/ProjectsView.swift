// SPDX-License-Identifier: GPL-3.0-only
// Projects: the FM-1's 16 projects (PJ_OP 110: open, save the work there, rename, clear), the open project's sections A..
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
    @State private var prompt: Prompt?
    @State private var text = ""

    struct Prompt {
        let title: String
        let done: (String) -> Void
    }

    enum Target { case section(Int), pattern(Int, Int) }

    private static let state = ["empty", "used", "SLOOP 2.4 project", "another firmware's"]

    var body: some View {
        NavigationStack {
            List {
                Section { LinkStatus(s, .projects) }.listRowBackground(Color.clear)
                if s.ready(.proj), let l = s.projects {
                    Section("Projects") {
                        ForEach(0..<ProjList.count, id: \.self) { p in projectRow(l, p) }
                    }
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
                case .pattern(let k, let p):
                    ask = Confirmation(title: "Replace track \(k + 1) · pattern \(p + 1) with the file's?") { s.importPattern(k, p, url) }
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

    private func sectionsTitle(_ l: ProjList) -> String { "Sections of \(name(l, l.open))" }

    private func projectRow(_ l: ProjList, _ p: Int) -> some View {
        let used = p < l.used.count && l.used[p]
        let open = l.open == p
        let n = name(l, p)
        return HStack {
            Text("\(p + 1)").font(.headline).monospacedDigit().frame(width: 28)
            Text(n).foregroundStyle(used || open ? .primary : .secondary)
            if open { Text("open").font(.caption.bold()).foregroundStyle(Color.accentColor) }
            Spacer()
            if !used { Text("empty").foregroundStyle(.secondary) }
            Menu {
                Button("Open") {
                    ask = Confirmation(title: "Open \(n)? The work not saved is replaced.") { s.projOp(2, p) }
                }.disabled(open)
                Button("Save the Work Here") {
                    ask = Confirmation(title: used && !open ? "\(n) holds a project: replace it with the work?" : "Save the work as \(n)?") { s.projOp(1, p) }
                }
                Button("Rename…") {
                    text = p < l.names.count ? l.names[p] : ""
                    prompt = Prompt(title: "Name of project \(p + 1)") { s.projOp(4, p, name: $0) }
                }
                Button("Clear", role: .destructive) {
                    ask = Confirmation(title: "Clear \(n)? Everything stored in it is erased.") { s.projOp(3, p) }
                }.disabled(!used || open)       // (the loaded project: never cleared from here; the user, 2026-10-24)
            } label: { Image(systemName: "ellipsis.circle") }
                .disabled(!s.idle)
        }
    }

    private func sectionRow(_ k: Int) -> some View {
        let u = s.sectionUsed[k]
        let letter = Names.sectionLetter(k)
        let hasObject = s.bkList.flatMap { Ops.sectionObject($0, k) } != nil
        return HStack {
            Text(letter).font(.headline).frame(width: 28)
            Text(u.map { $0 < Self.state.count ? Self.state[$0] : "used" } ?? "unknown").foregroundStyle(.secondary)
            Spacer()
            Menu {
                Button("Load") { ask = Confirmation(title: "Load section \(letter)? The current sound and pattern will be replaced.") { s.projectOp(0, k) } }
                    .disabled(u != 1)
                Button("Save") { ask = Confirmation(title: "Overwrite section \(letter)?") { s.projectOp(1, k) } }
                Button("Export…") { s.exportSection(k) }.disabled(u != 1 || !hasObject)
                Button("Import…") { target = .section(k); importing = true }.disabled(!hasObject)
            } label: { Image(systemName: "ellipsis.circle") }
                .disabled(!s.idle)
        }
    }

    @ViewBuilder private func patterns(_ l: PatList, _ len: [[Int]]) -> some View {
        Picker("Track", selection: $track) { ForEach(0..<4, id: \.self) { Text("\($0 + 1)").tag($0) } }.pickerStyle(.segmented)
        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 4), count: 8), spacing: 4) {
            ForEach(0..<16, id: \.self) { slot in
                let n = len[track][slot]
                let on = s.patSel?.track == track && s.patSel?.slot == slot
                Button { s.patSel = (track, slot) } label: {
                    Text(n > 0 ? "\(n)" : "·").frame(maxWidth: .infinity, minHeight: 34)
                        .background(on ? Color.accentColor.opacity(0.35) : Color.secondary.opacity(n > 0 ? 0.25 : 0.1), in: RoundedRectangle(cornerRadius: 6))
                }.buttonStyle(.plain)
            }
        }
        if let sel = s.patSel {
            let used = len[sel.track][sel.slot] > 0
            Text("Track \(sel.track + 1) · pattern \(sel.slot + 1)").font(.subheadline.bold())
            HStack {
                Button("Export…") { s.exportPattern(sel.track, sel.slot) }.disabled(!used)
                Button("Import…") { target = .pattern(sel.track, sel.slot); importing = true }
                Menu("Copy to") {
                    ForEach(0..<4, id: \.self) { k in
                        Menu("Track \(k + 1)") {
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
        }
    }

    private func copy(from a: (track: Int, slot: Int), to b: (Int, Int), over: Bool) {
        let go = { s.copyPattern(from: (a.track, a.slot), to: b) }
        if over { ask = Confirmation(title: "Pattern \(b.1 + 1) holds a pattern: replace it?", action: go) } else { go() }
    }
}
