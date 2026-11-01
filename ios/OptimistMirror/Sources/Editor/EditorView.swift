// SPDX-License-Identifier: GPL-3.0-only
// The editor screens of the app: Projects, Library and Backup (the web editor's tabs of the same names), tabs beside the screen.
// They talk to the FM-1 over the USB port; over Bluetooth only the project slots and the project library work, and only when INFO
// lists opti_btproj_1 (EditorRoute): any other firmware answers the screen mirror's request alone there.
import SwiftUI
import UIKit
import UniformTypeIdentifiers

/// What the screens say when there is no USB link to the FM-1 (and the FM-1's own answer, once there is one).
struct LinkStatus: View {
    @ObservedObject var s: EditorSession
    @ObservedObject var link: MirrorLink
    let feature: Feature

    init(_ s: EditorSession, _ feature: Feature) {
        self.s = s
        self.link = s.link
        self.feature = feature
    }

    var body: some View {
        if s.route == .needsUSB {
            Label("This needs the USB cable. Over Bluetooth this firmware answers only the screen mirror (the project slots and library need a build with BT_PROJ).", systemImage: "cable.connector")
        } else if s.route == .bluetooth, !s.route.carries(feature) {
            Label("This needs the USB cable. Over Bluetooth only the project slots and the project library work.", systemImage: "cable.connector")
        } else if s.route == .none {
            Label("Connect the FM-1 with a USB cable, or over Bluetooth.", systemImage: "cable.connector")
        } else if s.info == nil, s.note?.error != true {
            HStack { ProgressView(); Text("Asking \(link.portName)…").padding(.leading, 8) }
        } else if let i = s.info, !i.has(feature) {
            Label("This firmware does not offer it.", systemImage: "xmark.octagon")
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

/// A file to save or send: the Mac's Save dialog, iOS's share sheet (its Save to Files) elsewhere
struct ExportView: View {
    let url: URL
    var body: some View {
#if targetEnvironment(macCatalyst)
        SaveView(url: url)
#else
        ActivityView(url: url)
#endif
    }
}

#if targetEnvironment(macCatalyst)
/// The Mac's Save dialog for a file (on the Mac the share sheet has no Save), the file keeps its name
struct SaveView: UIViewControllerRepresentable {
    let url: URL
    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        UIDocumentPickerViewController(forExporting: [url], asCopy: true)
    }
    func updateUIViewController(_ controller: UIDocumentPickerViewController, context: Context) {}
}
#endif

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
