// SPDX-License-Identifier: GPL-3.0-only
// Firmware: an install over the USB cable (the web installer's, web/installer/). In order (the user, 2026-10-25): one's own
// build, just made in the builder and picked; the releases with their published flavours (version and date); a .fwsc file.
// Build opens the web builder (BuilderWebView.swift, its app page, started on the flavour ticked); no list of cloud builds.
// The return to the official firmware stays on the web.
import SwiftUI
import UniformTypeIdentifiers

struct FlashView: View {
    @ObservedObject var flash: FlashModel
    @ObservedObject var link: MirrorLink
    /// shown over the welcome screen (the whole window): its Done button; nil in the tab
    var close: (() -> Void)? = nil
    /// a build's "Try it in the simulator": the app's own Simulator with it (the user, 2026-10-25: "should stay in the app")
    var trySim: (String) -> Void = { _ in }
    @State private var ask: Confirmation?
    @State private var building: BuilderPage?            // the builder shown, started on a flavour
    @State private var importing = false
    @State private var release = ""                      // the release picked (its group: "Optimist v0.1 · 2026-10-18")

    var body: some View {
        NavigationStack {
            List {
                installSection
                buildSection
            }
            .navigationTitle("Firmware")
            .toolbar {
                if let close {
                    ToolbarItem(placement: .confirmationAction) { Button("Done", action: close).disabled(flash.busy) }
                }
            }
            .refreshable { await flash.loadLists() }
            .confirm($ask)
            .fileImporter(isPresented: $importing, allowedContentTypes: [.data]) { r in
                if case .success(let url) = r { flash.open(url) }
            }
        }
        .fullScreenCover(item: $building) { page in
            BuilderSheet(url: page.url, event: { e in
                flash.builder(e)
                if case .open(let w, let id) = e {
                    building = nil
                    if w == "sim" { trySim(id) }
                }
            }, close: { building = nil })
        }
        .task {
            await flash.loadListsOnce()
            if flash.chosen == nil, flash.myBuild != nil { flash.chooseMyBuild() }   // (one's own build, when there is one, picked)
        }
        .task { await flash.find() }
    }

    private var releases: [(group: String, entries: [FlashEntry])] { FlashCatalog.grouped(flash.site) }
    private var releaseShown: String { releases.contains { $0.group == release } ? release : releases.first?.group ?? "" }
    private var flavours: [FlashEntry] { releases.first { $0.group == releaseShown }?.entries ?? [] }

    /// Install: what to write (an official release's version and flavour, one's own last build, a .fwsc file), then the
    /// button, its progress and the FM-1 found, together (the user, 2026-10-25)
    @ViewBuilder private var installSection: some View {
        Section {
            if releases.isEmpty {
                if flash.loadingLists { ProgressView() } else { Text("No release found.").foregroundStyle(.secondary) }
            } else {
                Picker("Official Release", selection: Binding(get: { releaseShown }, set: { g in
                    release = g
                    if let e = releases.first(where: { $0.group == g })?.entries.first { flash.choose(e) }
                })) {
                    ForEach(releases, id: \.group) { Text($0.group).tag($0.group) }
                }
                .pickerStyle(.menu)
                Picker("Flavour", selection: Binding(get: { flavours.first { $0.id == flash.chosen }?.id ?? "" }, set: { id in
                    if let e = flavours.first(where: { $0.id == id }) { flash.choose(e) }
                })) {
                    if !flavours.contains(where: { $0.id == flash.chosen }) { Text("Choose…").tag("") }
                    ForEach(flavours) { Text($0.title).tag($0.id) }
                }
                .pickerStyle(.menu)
            }
            if let b = flash.myBuild {
                Button { flash.chooseMyBuild() } label: {
                    HStack {
                        Text("Your Build").foregroundStyle(.primary)
                        Spacer()
                        Text("\(b.name) · \(b.id.prefix(8))").font(.callout.monospaced()).foregroundStyle(.secondary)
                        mark("mine")
                    }
                }
            }
            Button { importing = true } label: {
                HStack { Text("A .fwsc File…").foregroundStyle(.primary); Spacer(); mark("file") }
            }
            if let err = flash.listError ?? flash.packageError { Text(err).font(.footnote).foregroundStyle(.red) }
            Button(flash.package.map { "Install \($0.file.isEmpty ? $0.title : $0.file)" } ?? "Install") {
                ask = Confirmation(title: "Write \(flash.package.map { $0.file.isEmpty ? $0.title : "\($0.file) (\($0.title), \($0.product))" } ?? "") to the FM-1? Projects, presets, kits and samples stay. Keep the USB cable in until it is done.") {
                    flash.install()
                }
            }
            .bold()
            .disabled(flash.package == nil || flash.busy)
            if let p = flash.progress, flash.busy || flash.result != nil { ProgressView(value: p) }
            if !flash.status.isEmpty { Text(flash.status).font(.footnote) }
            if let r = flash.result { Text(r).font(.footnote).foregroundStyle(.green) }
            if let e = flash.error { Text(e).font(.footnote).foregroundStyle(.red) }
        } header: {
            Text("Install")
        } footer: {
            Text(flash.device.map { "\($0) on USB." } ?? (link.bluetooth ? "Installing needs the USB cable, not Bluetooth." : "Connect the FM-1 with a USB cable."))
        }
        .disabled(flash.busy)
    }

    @ViewBuilder private func mark(_ id: String) -> some View {
        if flash.chosen == id {
            if flash.loadingPackage { ProgressView() } else if flash.package != nil { Image(systemName: "checkmark") }
        }
    }

    /// Build Your Own: a flow of its own (the builder's page); when it is built it is "Your Build" in Install, picked
    private var buildSection: some View {
        Section {
            Button(flash.myBuild == nil ? "Make Your Build…" : "Make Another Build…") {
                building = BuilderPage(url: FlashCatalog.builder)   // (the builder's own Start from: a flavour or a saved one)
            }
            .disabled(flash.busy)
            if let s = flash.buildStatus {
                Label(s, systemImage: flash.buildFailed ? "exclamationmark.triangle" : "hammer")
                    .font(.footnote)
                    .foregroundStyle(flash.buildFailed ? Color.red : Color.secondary)
                if let l = flash.buildLink { Link(l.title, destination: l.url).font(.footnote) }   // (Safari: the GitHub page)
            }
        } header: {
            Text("Build Your Own")
        } footer: {
            Text("Pick what goes in and build it in the cloud (about 10 minutes); it then shows above as Your Build.")
        }
    }
}

struct BuilderPage: Identifiable {
    let id = UUID()
    let url: URL
}
