// SPDX-License-Identifier: GPL-3.0-only
// The Library section of the Projects screen: the projects kept in the app (ProjectLibrary), with the actions that move them between
// the library and the FM-1's slots. Wording as the rest of the screen: source → destination.
import SwiftUI
import UniformTypeIdentifiers

struct LibrarySection: View {
    @ObservedObject var s: EditorSession
    @State private var ask: Confirmation?
    @State private var renaming: ProjectLibrary.Entry?
    @State private var text = ""
    @State private var adding = false

    private var connected: Bool { s.ready(.proj) && s.projects != nil }

    private func slotName(_ p: Int) -> String {
        guard let l = s.projects, p < l.names.count, !l.names[p].isEmpty else { return "Project \(p + 1)" }
        return l.names[p]
    }

    private func held(_ p: Int) -> Bool { s.projects.map { p < $0.used.count && $0.used[p] } ?? false }

    var body: some View {
        Section {
            if s.libraryItems.isEmpty { Text("No projects in the Library yet. Slot N → Library keeps a copy of a slot here.").foregroundStyle(.secondary) }
            ForEach(s.libraryItems) { e in row(e) }
            Button("File → Library…") { adding = true }
            if connected, let l = s.projects {
                Menu("Slot → Library…") {
                    ForEach(0..<s.projectCount, id: \.self) { p in
                        Button("Slot \(p + 1)" + (held(p) ? " (\(slotName(p)))" : " (empty)")) { s.libSave(p, saveFirst: false) }.disabled(!held(p))
                    }
                }.disabled(!s.idle)
                Button("Active project → Library") {
                    ask = Confirmation(title: "Active project → Library: save the Active project into slot \(l.open + 1), then copy it to the Library?") { s.libSave(l.open, saveFirst: true) }
                }.disabled(!s.idle)
            }
        } header: { Text("Library (in this app)") } footer: { Text("One .fm1proj per project, in the app's Documents/Projects folder (the Files app shows it).") }
        .confirm($ask)
        .fileImporter(isPresented: $adding, allowedContentTypes: [.data]) { r in
            if case .success(let url) = r { s.libAdd(file: url) }
        }
        .alert("Name of the project", isPresented: Binding(get: { renaming != nil }, set: { if !$0 { renaming = nil } })) {
            TextField("", text: $text).textInputAutocapitalization(.characters).autocorrectionDisabled()
            Button("OK") { if let e = renaming { s.libRename(e, text) }; renaming = nil }
            Button("Cancel", role: .cancel) { renaming = nil }
        } message: { Text("Up to 12 of A-Z 0-9 space - . _ / # +") }
        .onAppear { s.refreshLibrary() }
    }

    private func row(_ e: ProjectLibrary.Entry) -> some View {
        HStack {
            VStack(alignment: .leading) {
                Text(e.name.isEmpty ? "(no name)" : e.name)
                Text("\(e.date.formatted(date: .abbreviated, time: .shortened)) · \(ByteCountFormatter.string(fromByteCount: Int64(e.size), countStyle: .file))")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            Menu {
                if connected, let l = s.projects {
                    Menu("Swap with slot…") {
                        ForEach(0..<s.projectCount, id: \.self) { p in
                            Button("Swap with slot \(p + 1)" + (held(p) ? " (\(slotName(p)))" : " (empty)")) {
                                ask = Confirmation(title: "Swap with slot \(p + 1): slot \(p + 1) → Library, then \(e.name) → slot \(p + 1) and opened.\(l.open == p ? " Slot \(p + 1) is the Active project: its unsaved changes are lost." : "") Stopped only.") { s.libSwap(e, p) }
                            }
                        }
                    }
                    Menu("Library → Slot…") {
                        ForEach(0..<s.projectCount, id: \.self) { p in
                            Button("Library → Slot \(p + 1)" + (held(p) ? " (\(slotName(p)))" : " (empty)")) {
                                let go = { s.libLoad(e, p, openIt: false) }
                                if held(p) { ask = Confirmation(title: "Library → slot \(p + 1): slot \(p + 1) holds \(slotName(p)). Replace it with \(e.name)? Stopped only.", action: go) } else { go() }
                            }
                        }
                    }
                    Button("Library → Active project") {
                        ask = Confirmation(title: "Library → Active project: write \(e.name) into slot \(l.open + 1) and open it. The Active project's unsaved changes are lost. Stopped only.") { s.libLoad(e, l.open, openIt: true) }
                    }
                }
                Button("Rename…") { text = e.name; renaming = e }
                Button("Library → File…") { s.export = ExportItem(url: s.projectLibrary.url(e)) }
                Button("Delete…", role: .destructive) {
                    ask = Confirmation(title: "Delete \(e.name) from the Library? The file is erased.") { s.libDelete(e) }
                }
            } label: { Image(systemName: "ellipsis.circle") }.disabled(!s.idle)
        }
    }
}
