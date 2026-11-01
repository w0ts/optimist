// SPDX-License-Identifier: GPL-3.0-only
// What the app shows for an fm1-base app (one that answered the companion capability query, not Optimist's INFO): what it is,
// fm1-base's own pages (Pages), its files (Files) and its parameters (Parameters).
import SwiftUI
import UniformTypeIdentifiers

struct Fm1BaseRoot: View {
    @ObservedObject var link: MirrorLink
    let info: CompanionInfo
    @StateObject private var web = Fm1BaseWeb()
    @StateObject private var model: Fm1BaseModel
    @State private var package: [UInt8]?
    @State private var packageName: String?
    @State private var picking = false
    @State private var page: Fm1BasePage = .companion

    init(link: MirrorLink, info: CompanionInfo) {
        self.link = link
        self.info = info
        _model = StateObject(wrappedValue: Fm1BaseModel(link: link))
    }

    private var site: Fm1BaseSite { Fm1BaseSite(info: info, package: package) }

    var body: some View {
        TabView {
            about.tabItem { Label("App", systemImage: "info.circle") }
            pages.tabItem { Label("Pages", systemImage: "safari") }
            if info.hasFiles { FilesTab(model: model, info: info).tabItem { Label("Files", systemImage: "folder") } }
            if info.hasParams { ParamsTab(model: model).tabItem { Label("Parameters", systemImage: "slider.horizontal.3") } }
        }
        .background(Color(.systemBackground).ignoresSafeArea())
        .sheet(item: $model.export) { ExportView(url: $0.url) }
        .task(id: "\(link.portName)|\(link.paused)") {
            web.follow(port: link.paused ? "" : link.portName, bluetooth: link.bluetooth)
        }
        .onAppear { web.use(info, package: package) }
        .onChange(of: info) { _, i in web.use(i, package: package); web.reset() }
        .onChange(of: package) { _, p in web.use(info, package: p) }
    }

    private var about: some View {
        List {
            Section {
                row("Name", info.name)
                row("Version", info.appVersion)
                row("Identity", info.identity)
                row("Companion protocol", String(info.version))
            } header: { Text("This FM-1 runs an fm1-base app") }
            Section("Services") {
                ForEach(info.serviceList, id: \.self) { Text($0) }
                if info.hasFiles { row("Files", String(info.files)) }
                if info.hasParams { row("Parameters", String(info.params)) }
            }
            if !info.extras.isEmpty {
                Section("The app's tags") { ForEach(info.extras.sorted(by: <), id: \.key) { row($0.key, $0.value) } }
            }
            Section {
                Text("Optimist's own screens (the mirror, the touch buttons, the clip editor) are for the Optimist firmware. This app serves fm1-base's pages and the app's files and parameters.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
        .padding(.top, 44)             // (Disconnect, top right)
    }

    private func row(_ k: String, _ v: String) -> some View {
        HStack { Text(k); Spacer(); Text(v).foregroundStyle(.secondary).textSelection(.enabled) }
    }

    private var pages: some View {
        VStack(spacing: 0) {
            Picker("Page", selection: $page) {
                ForEach(site.pages) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)
            .padding(.horizontal, 12).padding(.top, 44).padding(.bottom, 8)
            if page == .installer {
                HStack {
                    Button { picking = true } label: { Label(packageName ?? "Choose the .fwsc package…", systemImage: "doc") }
                    Spacer()
                }
                .padding(.horizontal, 12).padding(.bottom, 6)
            }
            Fm1BaseWebView(web: web)
        }
        .onAppear { link.hold(); web.use(info, package: package); web.show(page) }
        .onDisappear { link.release() }
        .onChange(of: page) { _, p in web.show(p) }
        .fileImporter(isPresented: $picking, allowedContentTypes: [.data]) { r in
            guard case .success(let url) = r else { return }
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            if let d = try? Data(contentsOf: url) { package = [UInt8](d); packageName = url.lastPathComponent }
        }
    }
}

struct NoteLine: View {
    @ObservedObject var model: Fm1BaseModel
    var body: some View {
        if let p = model.progress { ProgressView(value: p) }
        if let n = model.note { Text(n.text).font(.footnote).foregroundStyle(n.error ? Color.red : Color.secondary) }
    }
}

struct FilesTab: View {
    @ObservedObject var model: Fm1BaseModel
    let info: CompanionInfo

    var body: some View {
        List {
            Section {
                Button { Task { await model.backUp(info) } } label: { Label("Back up all files", systemImage: "externaldrive") }
                    .disabled(model.busy)
                NoteLine(model: model)
            } footer: { Text("Saves every readable file of the app in one file, each checked against the FM-1's CRC-32.") }
            Section("Files") {
                ForEach(model.files ?? []) { f in
                    Button { Task { await model.save(f) } } label: {
                        VStack(alignment: .leading) {
                            Text(f.name)
                            Text("\(f.kind), \(f.size) of \(f.room) bytes").font(.caption).foregroundStyle(.secondary)
                        }
                    }
                    .disabled(model.busy || !f.readable)
                }
            }
        }
        .padding(.top, 44)
        .task { if model.files == nil { await model.loadFiles() } }
        .refreshable { await model.loadFiles() }
    }
}

struct ParamsTab: View {
    @ObservedObject var model: Fm1BaseModel

    private var groups: [(String, [CompanionParam])] {
        var order: [String] = []
        let all = model.params ?? []
        for p in all where !order.contains(p.group) { order.append(p.group) }
        return order.map { g in (g, all.filter { $0.group == g }) }
    }

    var body: some View {
        List {
            NoteLine(model: model)
            ForEach(groups, id: \.0) { g in
                Section(g.0) { ForEach(g.1) { ParamRow(model: model, p: $0) } }
            }
        }
        .padding(.top, 44)
        .task { if model.params == nil { await model.loadParams() } }
        .refreshable { await model.loadParams(); }
    }
}

struct ParamRow: View {
    @ObservedObject var model: Fm1BaseModel
    let p: CompanionParam

    private var value: Int { model.values[p.id] ?? p.def }

    private func text(_ v: Int) -> String {
        p.isEnum ? (p.labels.indices.contains(v - p.min) ? p.labels[v - p.min] : String(v)) : "\(v)\(p.unit.isEmpty ? "" : " " + p.unit)"
    }

    var body: some View {
        if p.readOnly {
            HStack { Text(p.name); Spacer(); Text(text(value)).foregroundStyle(.secondary) }
        } else if p.isEnum, !p.labels.isEmpty {
            Picker(p.name, selection: Binding(get: { value }, set: { v in Task { await model.set(p, v) } })) {
                ForEach(Array(p.labels.enumerated()), id: \.offset) { Text($0.element).tag(p.min + $0.offset) }
            }
        } else {
            VStack(alignment: .leading) {
                HStack { Text(p.name); Spacer(); Text(text(value)).foregroundStyle(.secondary) }
                Stepper("", value: Binding(get: { value }, set: { v in Task { await model.set(p, v) } }), in: p.min...max(p.min, p.max))
                    .labelsHidden()
            }
        }
    }
}
