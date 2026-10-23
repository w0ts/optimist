// SPDX-License-Identifier: GPL-3.0-only
// Library: the FM-1's 32 user presets (UP_LIST / GET / PUT / STORE / LOAD / ERASE) and the app's library of patches, which are
// the web editor's library files (felucca-library, felucca-patch).
import SwiftUI
import UniformTypeIdentifiers

struct LibraryView: View {
    @ObservedObject var s: EditorSession
    @Environment(\.dismiss) private var dismiss
    @State private var page = 0
    @State private var query = ""
    @State private var ask: Confirmation?
    @State private var importing = false
    @State private var prompt: Prompt?
    @State private var text = ""

    struct Prompt: Identifiable {
        let id = UUID()
        let title: String
        let initial: String
        let done: (String) -> Void
    }

    var body: some View {
        NavigationStack {
            List {
                Section { Picker("", selection: $page) { Text("Device").tag(0); Text("Library").tag(1) }.pickerStyle(.segmented) }
                if page == 0 { device } else { library }
                Section { NoteView(s: s) }
            }
            .navigationTitle("Library")
            .toolbar {
                ToolbarItem(placement: .topBarLeading) { Button("Close") { dismiss() } }
                ToolbarItem(placement: .topBarTrailing) {
                    if page == 1 {
                        Menu {
                            Button("Import…") { importing = true }
                            Button("Export the library…") { s.exportLibrary() }.disabled(s.library.isEmpty)
                        } label: { Image(systemName: "ellipsis.circle") }
                    }
                }
            }
            .confirm($ask)
            .fileImporter(isPresented: $importing, allowedContentTypes: [.json, .data], allowsMultipleSelection: true) { r in
                if case .success(let urls) = r { s.importLibrary(urls) }
            }
            .alert(prompt?.title ?? "", isPresented: Binding(get: { prompt != nil }, set: { if !$0 { prompt = nil } })) {
                TextField("", text: $text)
                Button("OK") { prompt?.done(text); prompt = nil }
                Button("Cancel", role: .cancel) { prompt = nil }
            }
        }
        .task(id: s.ready(.library)) { if s.ready(.library) && s.bank == nil { s.loadBank() } }
    }

    private func askText(_ title: String, _ initial: String, _ done: @escaping (String) -> Void) {
        text = initial
        prompt = Prompt(title: title, initial: initial, done: done)
    }

    // MARK: the device's bank

    @ViewBuilder private var device: some View {
        Section { LinkStatus(s, .library) }.listRowBackground(Color.clear)
        if s.ready(.library), let b = s.bank {
            Section("\(b.slots.filter(\.used).count) / \(b.total) used") {
                ForEach(b.slots, id: \.slot) { x in
                    HStack {
                        Text(Names.slotName(x.slot)).font(.body.monospaced()).frame(width: 44, alignment: .leading)
                        Text(x.used ? x.name : "empty").foregroundStyle(x.used ? Color.primary : Color.secondary)
                        Spacer()
                        Text(x.used ? engine(x.engine) : "").font(.footnote).foregroundStyle(.secondary)
                    }
                    .contentShape(Rectangle())
                    .listRowBackground(s.bankSel == x.slot ? Color.accentColor.opacity(0.25) : nil)
                    .onTapGesture { s.bankSel = x.slot }
                }
            }
            if let k = s.bankSel, k < b.slots.count {
                let slot = b.slots[k]
                Section(Names.slotName(k)) {
                    Button("Load on the track") { s.bankLoad(k) }.disabled(!slot.used)
                    Button("Store the current sound…") {
                        askText("Name (12 ASCII characters; empty: the device names it)", slot.used ? slot.name : "") { name in
                            if slot.used { self.ask = Confirmation(title: "Overwrite slot \(Names.slotName(k)) (\(slot.name))?") { s.bankStore(k, name: name) } }
                            else { s.bankStore(k, name: name) }
                        }
                    }
                    Button("Copy to the library") { s.bankToLibrary(k); page = 1 }.disabled(!slot.used)
                    Button("Erase", role: .destructive) {
                        ask = Confirmation(title: "Erase slot \(Names.slotName(k)) (\(slot.name))?") { s.bankErase(k) }
                    }.disabled(!slot.used)
                }
                .disabled(!s.idle)
            }
            Section {
                Button("Read again") { s.loadBank() }
                Button("Export the bank…") { s.exportBank() }.disabled(b.slots.allSatisfy { !$0.used })
            }
            .disabled(!s.idle)
        }
    }

    private func engine(_ i: Int) -> String { s.info.map { i >= 0 && i < $0.engines.count ? $0.engines[i] : String(i) } ?? String(i) }

    // MARK: the library

    private var shown: [Patch] {
        let q = query.trimmingCharacters(in: .whitespaces).lowercased()
        return s.library.filter { q.isEmpty || $0.name.lowercased().contains(q) || $0.tags.contains { $0.lowercased().contains(q) } || $0.engineName.lowercased().contains(q) }
            .sorted { $0.modified > $1.modified }
    }

    @ViewBuilder private var library: some View {
        Section {
            TextField("Search name, engine or tag", text: $query).textInputAutocapitalization(.never).autocorrectionDisabled()
        }
        Section("\(shown.count) / \(s.library.count)") {
            ForEach(shown) { p in
                HStack {
                    VStack(alignment: .leading) {
                        Text(p.name)
                        Text(([p.engineName] + p.tags).filter { !$0.isEmpty }.joined(separator: " · ")).font(.footnote).foregroundStyle(.secondary)
                    }
                    Spacer()
                    if p.pattern != nil { Image(systemName: "music.note") }
                }
                .contentShape(Rectangle())
                .listRowBackground(s.libSel == p.id ? Color.accentColor.opacity(0.25) : nil)
                .onTapGesture { s.libSel = p.id }
                .swipeActions {
                    Button("Delete", role: .destructive) { ask = Confirmation(title: "Delete \(p.name) from the library?") { s.deletePatch(p.id) } }
                }
            }
        }
        if let id = s.libSel, let p = s.library.first(where: { $0.id == id }) {
            Section(p.name) {
                Button("Rename…") { askText("Name", p.name) { v in if !v.trimmingCharacters(in: .whitespaces).isEmpty { s.updatePatch(id) { $0.name = String(v.trimmingCharacters(in: .whitespaces).prefix(32)) } } } }
                Button("Tags…") {
                    askText("Tags, separated by commas", p.tags.joined(separator: ", ")) { v in
                        s.updatePatch(id) { $0.tags = Array(NSOrderedSet(array: v.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty })) as? [String] ?? [] }
                    }
                }
                Button("Duplicate") { s.duplicatePatch(id) }
                Button("Export…") { s.exportPatch(id) }
                if let k = s.bankSel, s.ready(.library), let b = s.bank, k < b.slots.count {
                    Button("Write to \(Names.slotName(k))") {
                        let go = { s.putPatch(id, slot: k) }
                        if b.slots[k].used { ask = Confirmation(title: "Overwrite slot \(Names.slotName(k)) (\(b.slots[k].name))?", action: go) } else { go() }
                    }
                    .disabled(!s.idle)
                }
            }
        }
    }
}
