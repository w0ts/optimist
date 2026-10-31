// SPDX-License-Identifier: GPL-3.0-only
// The Firmware tab's state: one's own cloud build (the builder's last, kept across launches, picked first), the site's
// releases, a .fwsc file; the package chosen (downloaded and checked), the FM-1 found, and an install with its progress. The mirror holds its requests while an install runs.
import Foundation

@MainActor
final class FlashModel: ObservableObject {
    @Published private(set) var site: [FlashEntry] = []
    @Published private(set) var listError: String?
    @Published private(set) var loadingLists = false

    /// the build made last in the builder ({id, name}, as builder.js keeps "optimist-my-build"): kept across launches
    @Published private(set) var myBuild: MyBuild? = MyBuild.load()
    @Published private(set) var buildStatus: String?     // the builder's build in progress, or why it stopped
    @Published private(set) var buildFailed = false
    /// the link beside the progress: the request (its issue) until the run is known, then the run's log, kept after a failure
    @Published private(set) var buildLink: (title: String, url: URL)?
    @Published private(set) var chosen: String?          // the entry's id
    @Published private(set) var package: FlashPackage?
    @Published private(set) var packageError: String?
    @Published private(set) var loadingPackage = false

    @Published private(set) var device: String?          // the FM-1's identity, as the handshake gave it
    @Published private(set) var finding = false
    @Published private(set) var busy = false
    @Published private(set) var progress: Double?
    @Published private(set) var status = ""
    @Published private(set) var result: String?
    @Published private(set) var error: String?
    /// m-bootloader asked for, for one install (download mode; docs/INSTALL-PATHS.md); off: USB-MIDI, the bootloader kept
    @Published var installMboot = false
    /// the bootloader the FM-1 says it has (INFO tag 0x68): "mboot" | "stock" | "unknown", nil: not said (an older firmware)
    @Published private(set) var bootWord: String?
    /// the last install needs download mode only for the build's m-bootloader: the update that keeps the bootloader is offered
    @Published private(set) var keepOffered = false
    /// the last install needs download mode (M-Flasher on a computer); true: the soft key may help there
    @Published private(set) var softKeyOffered = false
    /// the last install needs download mode: the tab says it needs a computer (M-Flasher) and links the release
    @Published private(set) var needsComputer = false

    private let link: MirrorLink
    private let updater: FlashUpdater
    private var listsLoaded = false
    private static let step1Reads = 74.0                 // the reads of step 1 on a real FM-1 (installer.js STEP1_READS)

    init(link: MirrorLink, ports: FlashPorts = CoreMIDIFlashPorts()) {
        self.link = link
        self.updater = FlashUpdater(ports: ports)
    }

    func loadListsOnce() async {
        guard !listsLoaded else { return }
        await loadLists()
    }

    func loadLists() async {
        loadingLists = true
        listError = nil
        defer { loadingLists = false }
        do { site = try await FlashCatalog.loadSite() } catch { listError = message(error) }
        listsLoaded = listError == nil
    }

    /// the build's BOOTLOADER as this install takes it, the line shown
    var bootLine: String? {
        guard let p = package else { return nil }
        return InstallRoute.bootloaderChoice(p.bootloader, install: installMboot).1
            + (["mboot": ". The FM-1 has m-bootloader already (it says so).", "stock": ". The FM-1 has M-VAVE's bootloader (it says so)."][bootWord ?? ""] ?? "")
    }

    /// the way for the package onto the FM-1 that answered (tools/fm1_route.py, web/install_route.js)
    func route(for pkg: FlashPackage, running: String, port: String, boot: String? = nil) -> InstallRoute.Route {
        let info = InstallRoute.packageKind(product: pkg.product, sha256: pkg.sha256, flashLen: InstallRoute.flashLen(pkg.image))
        let choice = InstallRoute.bootloaderChoice(pkg.bootloader, install: installMboot).0
        return InstallRoute.choose(InstallRoute.runningKind(running, port: port), info, bootloader: choice, runningText: running,
                                   mbootPresent: InstallRoute.mbootPresent(boot))
    }

    /// the update over USB-MIDI that keeps the bootloader, after download mode was said for the build's m-bootloader
    func installKeepingBootloader() {
        installMboot = false
        install()
    }

    /// the soft key (F0 22 24 35 7D F7): the FM-1 restarts in the chip's download mode, for M-Flasher on a computer
    func sendSoftKey() async {
        guard !busy, let f = await updater.find() else { status = "No FM-1 found."; return }
        let sent = f.channel.send(InstallRoute.softKey)
        f.channel.close()
        status = sent ? "Sent: the FM-1 restarts in download mode (its screen stays as it was). Switch it off and on to leave it." : "Could not send it."
    }

    func choose(_ e: FlashEntry) {
        guard !busy else { return }
        installMboot = false
        chosen = e.id
        package = nil
        packageError = nil
        loadingPackage = true
        Task {
            defer { if chosen == e.id { loadingPackage = false } }
            do {
                let p = try await FlashCatalog.download(e)
                if chosen == e.id { package = p }
            } catch {
                if chosen == e.id { packageError = message(error) }
            }
        }
    }

    /// The builder page's events: a build in progress is shown; once it is done it is one's own build, picked
    func builder(_ e: BuilderEvent) {
        switch e {
        case .sent(let issue):
            buildFailed = false
            buildStatus = "Sent: waiting for the cloud to start the build…"
            buildLink = issue.map { ("View the request", $0) }
        case .building(let id, let run):
            buildStatus = "Building \(id)… (about 10 minutes)"
            if let run { buildLink = ("View the run", run) }
        case .done(let id, let name):
            buildStatus = nil
            buildLink = nil
            setMyBuild(MyBuild(id: id, name: name))
        case .stopped(let reason):
            buildFailed = true
            buildStatus = reason
        case .open(let w, let id):
            if w == "install" {
                if myBuild?.id != id { setMyBuild(MyBuild(id: id, name: myBuild?.name ?? "custom")) } else { chooseMyBuild() }
            }
        }
    }

    private func setMyBuild(_ b: MyBuild) {
        myBuild = b
        b.save()
        chooseMyBuild()
    }

    /// One's own build: looked up on the cloud-builds branch by its id, downloaded and checked (a build just done may take
    /// a minute to be on the branch: tried again for a few minutes)
    func chooseMyBuild() {
        guard let b = myBuild, !busy else { return }
        chosen = "mine"
        package = nil
        packageError = nil
        loadingPackage = true
        Task {
            defer { if chosen == "mine" { loadingPackage = false } }
            for attempt in 0..<6 {
                do {
                    let e = try await FlashCatalog.loadCloud(key: b.id)
                    let p = try await FlashCatalog.download(e)
                    if chosen == "mine" { package = p }
                    return
                } catch {
                    if attempt == 5 || chosen != "mine" { if chosen == "mine" { packageError = message(error) }; return }
                    try? await Task.sleep(nanoseconds: 30_000_000_000)
                }
            }
        }
    }

    func clearChoice() {
        guard !busy else { return }
        chosen = nil
        package = nil
        packageError = nil
    }

    func open(_ url: URL) {
        guard !busy else { return }
        chosen = "file"
        package = nil
        packageError = nil
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        do {
            package = try FlashPackage.check([UInt8](try Data(contentsOf: url)), title: url.lastPathComponent, file: url.lastPathComponent)
        } catch {
            packageError = message(error)
        }
    }

    func find() async {
        guard !busy, !finding else { return }
        finding = true
        defer { finding = false }
        if let f = await updater.find() {
            f.channel.close()
            device = f.id.text
        } else {
            device = nil
        }
    }

    func install() {
        guard let pkg = package, !busy else { return }
        busy = true
        result = nil
        error = nil
        softKeyOffered = false
        needsComputer = false
        keepOffered = false

        progress = 0
        status = "Looking for the FM-1…"
        link.hold()
        Task {
            defer { busy = false; link.release() }
            var runningText = ""
            if let f = await updater.find() {             // (the route first: download mode is said, never attempted)
                runningText = InstallRoute.runningKind(f.id.text, port: f.channel.name)
                bootWord = await InstallRoute.askBoot(f.channel, identity: f.id.text)   // (a running Optimist says which bootloader it has)
                let r = route(for: pkg, running: f.id.text, port: f.channel.name, boot: bootWord)
                f.channel.close()
                if r.method == "download" {
                    status = ""
                    progress = nil
                    self.error = "Not installed. " + InstallRoute.nextStep(r, file: pkg.file)
                    softKeyOffered = r.softKey
                    needsComputer = true
                    keepOffered = r.wantsMboot
                    return
                }
            }
            do {
                let back = try await updater.install(pkg) { [weak self] s in Task { @MainActor in self?.show(s, pkg) } }
                progress = 1
                status = ""
                device = back
                result = "Installed \(pkg.title) (\(back))." + ""
            } catch {
                let info = InstallRoute.packageKind(product: pkg.product, sha256: pkg.sha256, flashLen: InstallRoute.flashLen(pkg.image))
                if let fe = error as? FlashError, InstallRoute.headRefused(runningText, info, code: fe.code.rawValue) {
                    self.error = InstallRoute.refusedStep(file: pkg.file)
                    needsComputer = true
                } else {
                    self.error = message(error)
                }
                status = ""
                append("error: \(self.error ?? "")")
            }
        }
    }

    private func show(_ s: FlashStep, _ pkg: FlashPackage) {
        switch s {
        case .start(let id):
            append("start \(id)")
            status = "Checking the package with \(id)…"
        case .verify(let n):
            progress = min(1, Double(n) / Self.step1Reads) * 0.1
        case .loader:
            append("loader")
            status = "Waiting for the update loader…"
            progress = 0.1
        case .write(let n, let addr):
            if n == 1 { append("write") }
            status = "Writing… Keep the cable in."
            progress = max(progress ?? 0, 0.1 + 0.85 * Double(addr) / Double(max(1, pkg.image.count)))
        case .reboot:
            append("reboot")
            status = "Restarting the FM-1…"
            progress = 0.97
        case .done(let id):
            append("done \(id)")
        }
    }

    private func append(_ line: String) { print("flash: \(line)") }   // (the console: devicectl --console, Console.app)

    private func message(_ e: Error) -> String {
        switch e {
        case let e as FlashError: return e.description
        case let e as FlashPackage.Problem: return e.description
        case let e as FlashCatalog.NetError: return e.description
        default: return e.localizedDescription
        }
    }
}

/// The build made last in the builder, kept across launches (UserDefaults "myBuild")
struct MyBuild: Codable, Equatable {
    let id: String
    let name: String

    private static let key = "myBuild"
    static func load() -> MyBuild? {
        guard let d = UserDefaults.standard.data(forKey: key), let b = try? JSONDecoder().decode(MyBuild.self, from: d),
              b.id.range(of: #"^[0-9a-f]{16}$"#, options: .regularExpression) != nil else { return nil }
        return b
    }
    func save() { UserDefaults.standard.set(try? JSONEncoder().encode(self), forKey: Self.key) }
}
