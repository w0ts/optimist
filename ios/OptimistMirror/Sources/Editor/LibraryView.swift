// SPDX-License-Identifier: GPL-3.0-only
// Library: the FM-1's 32 user presets (UP_LIST / GET / PUT / STORE / LOAD / ERASE) and the app's library of patches, which are
// the web editor's library files (felucca-library, felucca-patch).
import SwiftUI
import UniformTypeIdentifiers

struct LibraryView: View {
    @ObservedObject var s: EditorSession
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
                Section { Picker("", selection: $page) { Text("On FM-1").tag(0); Text("My Presets").tag(1) }.pickerStyle(.segmented) }
                if page == 0 { device } else { library }
                Section { NoteView(s: s) }
            }
            .navigationTitle("Library")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    if page == 1 {
                        Menu {
                            Button("Import…") { importing = true }
                            Button("Export Library…") { s.exportLibrary() }.disabled(s.library.isEmpty)
                        } label: { Image(systemName: "ellipsis.circle") }
                    } else if s.ready(.library), let b = s.bank {
                        Menu {
                            Button("Refresh", systemImage: "arrow.clockwise") { s.loadBank() }
                            Button("Export All…", systemImage: "square.and.arrow.up") { s.exportBank() }.disabled(b.slots.allSatisfy { !$0.used })
                        } label: { Image(systemName: "ellipsis.circle") }
                        .disabled(!s.idle)
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
                    Menu {                       // (a tap: the slot's actions, where it was tapped)
                        slotActions(x.slot, b.slots[x.slot])
                    } label: {
                        HStack {
                            Text(Names.slotName(x.slot)).font(.body.monospaced()).frame(width: 44, alignment: .leading)
                            Text(x.used ? x.name : "Empty").foregroundStyle(x.used ? Color.primary : Color.secondary)
                            Spacer()
                            Text(x.used ? engine(x.engine) : "").font(.footnote).foregroundStyle(.secondary)
                            Image(systemName: "ellipsis.circle").foregroundStyle(.secondary)
                        }
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .disabled(!s.idle)
                }
            }
        }
    }

    @ViewBuilder private func slotActions(_ k: Int, _ slot: UpSlot) -> some View {
        Button("Load", systemImage: "arrow.down.circle") { s.bankLoad(k) }.disabled(!slot.used)
        Button(slot.used ? "Replace with Current Sound…" : "Save Current Sound…", systemImage: "square.and.arrow.down") {
            askText("Name", slot.used ? slot.name : "") { name in
                if slot.used { self.ask = Confirmation(title: "Replace \(Names.slotName(k)) (\(slot.name))?") { s.bankStore(k, name: name) } }
                else { s.bankStore(k, name: name) }
            }
        }
        Button("Add to My Presets", systemImage: "plus.square.on.square") { s.bankToLibrary(k); page = 1 }.disabled(!slot.used)
        Button("Erase", systemImage: "trash", role: .destructive) {
            ask = Confirmation(title: "Erase \(Names.slotName(k)) (\(slot.name))?") { s.bankErase(k) }
        }.disabled(!slot.used)
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
                Menu {                           // (a tap: the preset's actions)
                    patchActions(p)
                } label: {
                    HStack {
                        VStack(alignment: .leading) {
                            Text(p.name)
                            Text(([p.engineName] + p.tags).filter { !$0.isEmpty }.joined(separator: " · ")).font(.footnote).foregroundStyle(.secondary)
                        }
                        Spacer()
                        if p.pattern != nil { Image(systemName: "music.note") }
                        Image(systemName: "ellipsis.circle").foregroundStyle(.secondary)
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .swipeActions {
                    Button("Delete", role: .destructive) { ask = Confirmation(title: "Delete \(p.name) from My Presets?") { s.deletePatch(p.id) } }
                }
            }
        }
    }

    @ViewBuilder private func patchActions(_ p: Patch) -> some View {
        let id = p.id
        if s.ready(.library), let b = s.bank {
            Menu("Send to FM-1", systemImage: "square.and.arrow.up.on.square") {
                ForEach(b.slots, id: \.slot) { x in
                    Button("\(Names.slotName(x.slot))  \(x.used ? x.name : "Empty")") {
                        let go = { s.putPatch(id, slot: x.slot) }
                        if x.used { ask = Confirmation(title: "Replace \(Names.slotName(x.slot)) (\(x.name))?", action: go) } else { go() }
                    }
                }
            }
            .disabled(!s.idle)
        }
        Button("Rename…", systemImage: "pencil") {
            askText("Name", p.name) { v in if !v.trimmingCharacters(in: .whitespaces).isEmpty { s.updatePatch(id) { $0.name = String(v.trimmingCharacters(in: .whitespaces).prefix(32)) } } }
        }
        Button("Tags…", systemImage: "tag") {
            askText("Tags, separated by commas", p.tags.joined(separator: ", ")) { v in
                s.updatePatch(id) { $0.tags = Array(NSOrderedSet(array: v.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty })) as? [String] ?? [] }
            }
        }
        Button("Duplicate", systemImage: "plus.square.on.square") { s.duplicatePatch(id) }
        Button("Export…", systemImage: "square.and.arrow.up") { s.exportPatch(id) }
        Button("Delete", systemImage: "trash", role: .destructive) {
            ask = Confirmation(title: "Delete \(p.name) from My Presets?") { s.deletePatch(id) }
        }
    }
}
