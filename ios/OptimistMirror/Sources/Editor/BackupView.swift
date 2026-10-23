// SPDX-License-Identifier: GPL-3.0-only
// Backup: everything the FM-1 keeps in one .optimist-backup file (BK_LIST / READ), and a restore of the objects ticked (BK_BEGIN /
// DATA / COMMIT, the sample slots through SMP_*, BK_END), the web editor's Backup tab. The files are the web editor's.
import SwiftUI
import UniformTypeIdentifiers

struct BackupView: View {
    @ObservedObject var s: EditorSession
    @Environment(\.dismiss) private var dismiss
    @State private var ask: Confirmation?
    @State private var importing = false

    var body: some View {
        NavigationStack {
            List {
                Section { LinkStatus(s) }.listRowBackground(Color.clear)
                if s.ready, let l = s.bkList {
                    let usr = l.objs.filter { $0.kind == "usr" && $0.hasData && !$0.fm6 }.reduce(0) { $0 + $1.len }
                    let all = l.objs.filter { $0.hasData && ($0.kind != "usr" || $0.fm6) }.reduce(0) { $0 + $1.len }
                    Section {
                        Toggle("With the samples (\(usr / 1024) KiB)", isOn: $s.withSamples)
                        Button("Back up (\(all / 1024)\(s.withSamples ? " + \(usr / 1024)" : "") KiB)…") { s.backup() }
                    }
                    .disabled(!s.idle)
                    Section {
                        Button("Restore from a file…") { importing = true }
                    }
                    .disabled(!s.idle)
                    if let f = s.restoreFile { restoreSection(f) }
                } else if s.ready {
                    Section { Button("Read the object list") { s.loadBackupList() } }.disabled(!s.idle)
                }
                Section { NoteView(s: s) }
            }
            .navigationTitle("Backup")
            .toolbar { ToolbarItem(placement: .topBarLeading) { Button("Close") { dismiss() } } }
            .confirm($ask)
            .fileImporter(isPresented: $importing, allowedContentTypes: [.data]) { r in
                if case .success(let url) = r { s.openBackup(url) }
            }
        }
        .task(id: s.ready) { if s.ready && s.bkList == nil { s.loadBackupList() } }
        .onChange(of: s.bkList) { _, _ in
            if let f = s.restoreFile, s.restoreSel.isEmpty, let l = s.bkList {
                s.restoreSel = Set(Restore.plan(f.objects, l).filter { $0.why == nil }.map { $0.object.tag })
            }
        }
    }

    @ViewBuilder private func restoreSection(_ f: BackupContents) -> some View {
        Section("\(s.restoreName): \(f.version) \(f.magic), \(f.created)") {
            ForEach(s.restorePlan, id: \.object.tag) { x in
                let tag = x.object.tag
                Toggle(isOn: Binding(get: { s.restoreSel.contains(tag) }, set: { on in
                    if on { s.restoreSel.insert(tag) } else { s.restoreSel.remove(tag) }
                })) {
                    VStack(alignment: .leading) {
                        Text("\(tag.trimmingCharacters(in: .whitespaces))  \(Restore.label(tag))\(x.object.fm6 ? " (FM6)" : "")")
                        Text("\(x.object.data.count / 1024) KiB" + (x.why.map { " · \($0.rawValue)" } ?? ""))
                            .font(.footnote).foregroundStyle(x.why == nil ? Color.secondary : Color.red)
                    }
                }
                .disabled(x.why != nil || !s.idle)
            }
            Button("Restore \(s.restoreSel.count) objects…") {
                ask = Confirmation(title: "Write \(s.restoreSel.count) objects back into the FM-1 (what is there is replaced)? Stopped only. The FM-1 restarts when done.") { s.restore() }
            }
            .disabled(s.restoreSel.isEmpty || !s.idle)
        }
    }
}
