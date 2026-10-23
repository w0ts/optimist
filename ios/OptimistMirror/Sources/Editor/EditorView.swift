// SPDX-License-Identifier: GPL-3.0-only
// The editor screens of the app: Projects, Library and Backup (the web editor's tabs of the same names), a sheet over the mirror.
// They talk to the FM-1 over the USB port only: over Bluetooth the firmware answers the screen mirror's request alone.
import SwiftUI
import UIKit
import UniformTypeIdentifiers

struct EditorView: View {
    @ObservedObject var link: MirrorLink
    @StateObject private var session: EditorSession

    init(link: MirrorLink) {
        self.link = link
        _session = StateObject(wrappedValue: EditorSession(link: link))
    }

    var body: some View {
        TabView {
            ProjectsView(s: session)
                .tabItem { Label("Projects", systemImage: "folder") }
            LibraryView(s: session)
                .tabItem { Label("Library", systemImage: "books.vertical") }
            BackupView(s: session)
                .tabItem { Label("Backup", systemImage: "externaldrive") }
        }
        .task(id: "\(link.portName)|\(link.bluetooth)") { await session.connect() }
        .sheet(item: $session.export) { ActivityView(url: $0.url) }
    }
}

/// What the screens say when there is no USB link to the FM-1 (and the FM-1's own answer, once there is one).
struct LinkStatus: View {
    @ObservedObject var s: EditorSession
    @ObservedObject var link: MirrorLink

    init(_ s: EditorSession) {
        self.s = s
        self.link = s.link
    }

    var body: some View {
        if link.bluetooth {
            Label("This needs the USB cable. Over Bluetooth the FM-1 answers only the screen mirror.", systemImage: "cable.connector")
        } else if link.state == .searching {
            Label("Connect the FM-1 with a USB cable.", systemImage: "cable.connector")
        } else if s.info == nil, s.note?.error != true {
            HStack { ProgressView(); Text("Asking \(link.portName)…").padding(.leading, 8) }
        }
    }
}

/// The last status line or error, and the progress of a transfer.
struct NoteView: View {
    @ObservedObject var s: EditorSession

    var body: some View {
        if let p = s.progress { ProgressView(value: p) }
        if let n = s.note {
            Text(n.text)
                .font(.footnote)
                .foregroundStyle(n.error ? Color.red : Color.secondary)
        }
    }
}

/// iOS's share sheet for a file (Save to Files, AirDrop, Mail ...), the file keeps its name.
struct ActivityView: UIViewControllerRepresentable {
    let url: URL
    func makeUIViewController(context: Context) -> UIActivityViewController { UIActivityViewController(activityItems: [url], applicationActivities: nil) }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}

struct Confirmation: Identifiable {
    let id = UUID()
    let title: String
    let action: () -> Void
}

extension View {
    func confirm(_ c: Binding<Confirmation?>) -> some View {
        confirmationDialog(c.wrappedValue?.title ?? "", isPresented: Binding(get: { c.wrappedValue != nil }, set: { if !$0 { c.wrappedValue = nil } }),
                           titleVisibility: .visible, presenting: c.wrappedValue) { x in
            Button("Continue") { x.action() }
            Button("Cancel", role: .cancel) {}
        }
    }
}
