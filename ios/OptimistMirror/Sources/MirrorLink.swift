// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 over CoreMIDI: its USB port ("FM1-XXXX"; "Optimist" / "Felucca" on older firmware) when it is plugged in, else its Bluetooth MIDI port (connected with
// iOS's pairing sheet, BluetoothPicker.swift; the FM-1 answers only the mirror's request there). Asks for the mirror (cmd 106)
// every second, takes the panel writes it pushes into a MirrorDecoder. The same ports carry the editor's requests (Editor/):
// exchange(...) sends one and waits for its reply; the mirror's request waits for a free moment between them (one request at a
// time, as the web page does), and does not go out at all while an editor operation holds the link (hold()).
import CoreMIDI
import Foundation
import os

final class MirrorLink: ObservableObject, Transport, CompanionTransport {
    enum State: Equatable {
        case searching            // no FM-1 port
        case asking               // a port, no reply yet
        case noMirror             // the FM-1 answers nothing to 106: a build without ED_MIRROR
        case mirroring
    }

    @Published private(set) var state: State = .searching
    @Published private(set) var portName = ""
    /// An fm1-base app answered the companion capability query (0x60) and not Optimist's INFO: what it says it is (Companion/)
    @Published private(set) var companion: CompanionInfo?
    @Published private(set) var bluetooth = false   // the port in use is the Bluetooth one
    /// Bluetooth: the FM-1's INFO, once a whole one came (the link asks it, the editor screens read it here: one asker, so the
    /// editor's INFO never queues behind the screen or holds the mirror's requests back)
    @Published private(set) var btDevice: DeviceInfo?
    /// Bluetooth: INFO was asked BtAsk.tries times and no whole answer came (a firmware that answers the mirror only there)
    @Published private(set) var btGaveUp = false
    /// The port in use has sent the app something since it was taken (any SysEx: the "No mirror" screen says which case it is)
    @Published private(set) var heard = false
    @Published private(set) var ports: [String] = []   // every MIDI port with both directions, by name (the picker)
    /// .. of them, the ones online: the Mac keeps a Bluetooth port it paired listed while it is away (offline), so the
    /// Bluetooth sheet's connection shows as one going online, not as a new port
    @Published private(set) var online: [String] = []
    /// The port picked by hand ("": the FM-1's, found by its name); kept across launches
    /// No port taken until a port is chosen, Connect is pressed or the FM-1 is picked in the Bluetooth sheet: at launch and after
    /// Disconnect alike, the welcome screen (the user, 2026-10-24: "NO AUTOCONNECT")
    @Published private(set) var paused = true
    /// the port Connect took or the Bluetooth sheet connected ("": the FM-1's, USB first); until the next Disconnect
    private var want = ""
    func disconnect() { atLaunch = false; paused = true; want = ""; reconnect() }
    /// port: the one to take (the Bluetooth sheet's or the one picked: kept while it works)
    func resume(_ port: String = "") { atLaunch = false; paused = false; want = port; reconnect() }
    /// The app just started: an FM-1 port already connected in the system (the USB cable in, its Bluetooth MIDI port connected
    /// in the system's Bluetooth MIDI settings) is taken as if Connect were pressed (the user, 2026-10-31: having to disconnect
    /// and connect it again in the sheet was "super dumb"; connecting it in the system is the user's action). Only in the first
    /// launchTake seconds and until the user connects or disconnects: a port that comes online later is never taken by itself
    /// ("NO AUTOCONNECT", 2026-10-24), nor Bluetooth when the cable is pulled.
    private var atLaunch = true
    /// The welcome screen's "Use an already connected FM-1 at launch" (default on; off: the user picks, as before 2026-10-31)
    @Published var takeConnectedAtLaunch = UserDefaults.standard.object(forKey: "launchConnected") as? Bool ?? true {
        didSet { UserDefaults.standard.set(takeConnectedAtLaunch, forKey: "launchConnected") }
    }
    /// The port in use last time, by name (the launch takes it again when it is connected)
    private var lastPort = UserDefaults.standard.string(forKey: "lastPort") ?? "" {
        didSet { UserDefaults.standard.set(lastPort, forKey: "lastPort") }
    }
    private static let launchTake: TimeInterval = 3
    private func reconnect() {
        if source != 0 { MIDIPortDisconnectSource(inPort, source) }
        source = 0; destination = 0
        connect()
    }
    @Published var chosen = UserDefaults.standard.string(forKey: "port") ?? "" {
        didSet {
            atLaunch = false
            paused = false
            want = ""
            UserDefaults.standard.set(chosen, forKey: "port")
            if source != 0 { MIDIPortDisconnectSource(inPort, source) }
            source = 0; destination = 0
            connect()
        }
    }

    private let lock = OSAllocatedUnfairLock(initialState: Shared())
    private struct Shared {
        var decoder = MirrorDecoder()
        var assembler = SysExAssembler()
        var generation = 0        // panel writes applied so far
        var replies = 0
        var bytes = 0             // SysEx bytes received since the last tick (the link's rate, logged)
        var known = false         // the last reply's version is the decoder's (another firmware's pushes: not drawn)
        var features: [String: Int]?   // INFO's tag 66 over USB (nil: not asked yet); nothing but INFO is sent before it
        var bt = false            // the port in use is the Bluetooth one (`bluetooth`, for CoreMIDI's thread)
        var btInfo = 0            // Bluetooth, mirroring: ticks spent on INFO, to learn whether PANEL and NAV go there too (opti_mirbt)
        var btKnown = false       // .. and a whole answer came (an older firmware answers none: the screen only)
        var held: Date?           // a catch-up began (the whole panel filled): the picture shown stays until it is in
        var rows = [Bool](repeating: true, count: 240)   // since that fill, the rows drawn again
        var rowsLeft = 0
        var lastFrame = Date()
        var pending: Pending?     // the editor request waiting for its reply
        var cpending: CPending?   // the companion request waiting for its answer
        var token = 0
        var lostSeen = 0          // the decoder's checked frames lost, as last acted on
        var lostAsk = Date.distantPast   // the whole panel last asked for because of them
        var asksFix = 0, asksWhole = 0   // since the last tick: lost frames asked by seq / whole panels asked (the log)
        var logged = (sum: 0, gap: 0, more: 0, again: 0)   // the decoder's kinds at the last tick
        var fmStats: [Int]?, fmLogged: [Int]?   // the FM-1's op 5 STATS: the last, and the one logged
        var fmPace: [Int]?, paceLogged: [Int]?  // the FM-1's op 6 PACE (opti_miraimd_1): the last, and the one logged
        var tm = Timing()         // the app's own timing, logged each second ("app: ...")
        var ackAt = Date.distantPast   // the last ACK alone sent (106 bits 5 and 6, opti_miraimd_1)
    }
    /// What the app itself does with the frames, each second (2026-11-01: a whole screen 3-4 s on the Mac, is it the app?): panel
    /// writes applied, CoreMIDI callbacks, the longest pause between two writes, the longest a write waited to be on screen (the
    /// picture held during a catch-up included), the time held, each whole panel from its black fill to its last row
    struct Timing {
        var frames = 0, calls = 0
        var last: Date?, maxGap: TimeInterval = 0
        var unshown: Date?, maxLag: TimeInterval = 0
        var heldFor: TimeInterval = 0, heldSince: Date?
        var wholeAt: Date?, wholes: [Int] = []
    }
    private struct Pending {
        var cmd: UInt8
        var args: [UInt8]
        var token: Int
        var continuation: CheckedContinuation<[UInt8], Error>
    }
    private struct CPending {
        var cmd: UInt8
        var body: [UInt8]
        var token: Int
        var continuation: CheckedContinuation<[UInt8], Error>
    }
    private let gate = AsyncGate()
    private var holds = 0         // editor operations in progress: no mirror requests meanwhile
    /// The rate the link carries, once a second in the system log (Xcode's console, Console.app: subsystem
    /// com.w0ts.optimist.mirror), to measure the Bluetooth link on a real phone.
    private static let log = Logger(subsystem: "com.w0ts.optimist.mirror", category: "link")

    private var client = MIDIClientRef()
    private var inPort = MIDIPortRef()
    private var outPort = MIDIPortRef()
    private var source = MIDIEndpointRef()
    private var destination = MIDIEndpointRef()
    private var timer: Timer?
    private var asked = 0         // requests since the last reply
    private var wantWhole = true
    /// Bluetooth, a port known as the FM-1's: what it offers when INFO went unanswered for btInfoFirst ticks (an older firmware
    /// answers the mirror only there): the screen anyway
    private var btFallback: [String: Int]?
    private static let btInfoFirst = 2

    private static let keepAlive: TimeInterval = 1.0
#if DEBUG
    private var demo: NavDemo?    // MIRROR_DEMO: 109 NAV answered here (NavDemo.swift)
#endif
    private static let unanswered = 3   // requests without a reply: no mirror in this build

    init() {
#if DEBUG
        // the store screenshots on a simulator (no MIDI there): MIRROR_DEMO = a 240 x 240 RGB565 file (little-endian), shown as
        // if an FM-1 mirrored it; a DEBUG build only, never the store's
        if let path = ProcessInfo.processInfo.environment["MIRROR_DEMO"], let d = FileManager.default.contents(atPath: path),
           d.count == MirrorDecoder.side * MirrorDecoder.side * 2 {
            let px = stride(from: 0, to: d.count, by: 2).map { UInt16(d[$0]) | UInt16(d[$0 + 1]) << 8 }
            lock.withLock { $0.decoder.show(px); $0.generation = 1; $0.features = [Feature.nav.name: Feature.nav.version, Feature.clip.name: Feature.clip.version] }
            demo = NavDemo()
            portName = "FM1-0000"
            state = .mirroring
            return
        }
#endif
        MIDIClientCreateWithBlock("Optimist Mirror" as CFString, &client) { [weak self] note in
            if note.pointee.messageID == .msgSetupChanged || note.pointee.messageID == .msgPropertyChanged {   // (online: a property)
                DispatchQueue.main.async { self?.connect() }
            }
        }
        MIDIInputPortCreateWithProtocol(client, "in" as CFString, ._1_0, &inPort) { [weak self] list, _ in
            self?.receive(list)
        }
        MIDIOutputPortCreate(client, "out" as CFString, &outPort)
        connect()
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.launchTake) { [weak self] in self?.atLaunch = false }
        timer = Timer.scheduledTimer(withTimeInterval: Self.keepAlive, repeats: true) { [weak self] _ in self?.tick() }
    }

    deinit {
        timer?.invalidate()
        send([MirrorDecoder.cmd, 0])
        MIDIClientDispose(client)
    }

    /// The decoded panel when it changed since `seen`.
    func snapshot(after seen: Int) -> (pixels: [UInt16], generation: Int)? {
        lock.withLock { s in
            if let h = s.held {
                let now = Date()
                if s.tm.heldSince == nil { s.tm.heldSince = now }
                let most = bluetooth ? Self.catchMax : Self.catchMaxUSB
                if s.rowsLeft > Self.rowsSpare && now.timeIntervalSince(s.lastFrame) < Self.catchQuiet && now.timeIntervalSince(h) < most { return nil }
                s.held = nil
            }
            if let hs = s.tm.heldSince { s.tm.heldFor += Date().timeIntervalSince(hs); s.tm.heldSince = nil }
            guard s.generation != seen else { return nil }
            if let u = s.tm.unshown { s.tm.maxLag = max(s.tm.maxLag, Date().timeIntervalSince(u)); s.tm.unshown = nil }
            return (s.decoder.pixels, s.generation)
        }
    }
    /// A catch-up or a page drawn whole (the FM-1 fills the whole panel black, then draws it all again: ed_mirror.c mir_poll,
    /// op_draw.c ui.force) is shown when it is in, not as it comes: once every row is drawn again, else once the pushes pause
    /// this long (a part left black), or after catchMax at most. (Without the rows: a playing mixer's meters never paused and a
    /// page change over USB froze 1.5 s.)
    private static let catchQuiet: TimeInterval = 0.12
    private static let rowsSpare = 12                   // (rows a page may leave black: the mixer's 6 between its groups)
    private static let catchMax: TimeInterval = 1.0      // (Bluetooth: a whole screen takes ~0.3 s at 6 KB/s)
    private static let catchMaxUSB: TimeInterval = 0.25  // (USB: a page is in within a few frames; the mixer leaves rows black, so
                                                         //  "every row drawn" never came and its meters never paused: 1.5 s, then 2 s seen)
    private static func wholeFill(_ f: [UInt8]) -> Bool {
        (f.count == 18 && f[5] == 0 || f.count == 21 && f[5] == 8) && Array(f[6..<14]) == [0, 0, 0, 0, 112, 1, 112, 1]
    }

    /// Ask for the whole panel again (the app came back to the front).
    func refresh() {
        wantWhole = true
        tick()
    }

    // MARK: - ports

    static func name(_ e: MIDIEndpointRef) -> String {
        var s: Unmanaged<CFString>?
        MIDIObjectGetStringProperty(e, kMIDIPropertyDisplayName, &s)
        return (s?.takeRetainedValue() as String?) ?? ""
    }

    /// The FM-1 by a port's name: "FM1-XXXX" (four hex digits, its name since 2026-10-29; a computer may put more round it),
    /// or "Optimist" / "Felucca" (a device on older firmware); never other gear.
    static func isFM1Name(_ name: String) -> Bool {
        name.range(of: "fm1-[0-9a-f]{4}|felucca|optimist", options: [.regularExpression, .caseInsensitive]) != nil
    }

    private static func isFM1(_ e: MIDIEndpointRef) -> Bool { isFM1Name(name(e)) }

    /// What a port taken is known to offer before anything is asked (nil: INFO first, as over USB). Over Bluetooth our firmware
    /// answers 106 whatever its age, INFO only since opti_mirbt (2026-10-24): a Bluetooth port known as the FM-1's by its name,
    /// picked by hand or connected in the app's Bluetooth sheet (`trusted`) falls back to the mirror when INFO is not answered whole
    /// within btInfoFirst ticks (INFO goes first: asked while the screen streams it comes damaged). Any other Bluetooth port is
    /// asked INFO first: the FM-1's port may carry a name the app does not know (a system may keep the name a device had when
    /// it first connected). It started with no features at all, and that was "No mirror in this firmware" at once, nothing asked.
    static func startFeatures(bluetooth: Bool, name: String, trusted: Bool) -> [String: Int]? {
        guard bluetooth, trusted || isFM1Name(name) else { return nil }
        return [Feature.mirror.name: Feature.mirror.version]
    }

    /// An INFO reply's arguments as the link takes them: over Bluetooth a whole one only (DeviceInfo.wholeOverBluetooth), nil else
    static func takeInfo(_ args: [UInt8], bluetooth: Bool) -> DeviceInfo? {
        guard let i = try? DeviceInfo.parse(args) else { return nil }
        return !bluetooth || i.wholeOverBluetooth ? i : nil
    }

    /// A Bluetooth MIDI port: its device's driver is Apple's Bluetooth MIDI driver, or its name says so (as on a Mac).
    static func isBluetooth(_ e: MIDIEndpointRef) -> Bool {
        var entity = MIDIEntityRef(), device = MIDIDeviceRef()
        var owner: Unmanaged<CFString>?
        if MIDIEndpointGetEntity(e, &entity) == noErr, MIDIEntityGetDevice(entity, &device) == noErr {
            MIDIObjectGetStringProperty(device, kMIDIPropertyDriverOwner, &owner)
        }
        let driver = (owner?.takeRetainedValue() as String?) ?? ""
        return driver.lowercased().contains("bluetooth") || name(e).lowercased().contains("bluetooth")
    }

    private static func isOnline(_ e: MIDIEndpointRef) -> Bool {
        var v: Int32 = 0
        MIDIObjectGetIntegerProperty(e, kMIDIPropertyOffline, &v)
        return v == 0
    }

    /// A port as `pick` sees it
    struct PortSeen: Equatable {
        let name: String
        let bluetooth: Bool
        let online: Bool
    }
    /// The index of the port to take: the port picked by hand (USB or Bluetooth) whenever it is listed, never swapped for another
    /// (the user, 2026-10-31: "i did not ask for usb midi first. i asked you to fix BT"), else the FM-1's: USB first, Bluetooth else.
    static func pick(_ ports: [PortSeen], chosen: String) -> Int? {
        if !chosen.isEmpty, let i = ports.firstIndex(where: { $0.name == chosen }) { return i }
        return ports.firstIndex { !$0.bluetooth && isFM1Name($0.name) } ?? ports.firstIndex { isFM1Name($0.name) }
    }
    /// At launch: among the ports online (a Bluetooth port the system keeps listed offline is not connected), the one used last
    /// time (by name) when it is connected, else any FM-1 port; nil: nothing connected, the welcome screen
    static func launchPort(_ ports: [PortSeen], last: String) -> Int? {
        let on = ports.indices.filter { ports[$0].online }
        return pick(on.map { ports[$0] }, chosen: last).map { on[$0] }
    }
    private static func seen(_ count: Int, _ get: (Int) -> MIDIEndpointRef) -> [PortSeen] {
        (0..<count).map(get).map { PortSeen(name: name($0), bluetooth: isBluetooth($0), online: isOnline($0)) }
    }
    private static func find(_ count: Int, _ get: (Int) -> MIDIEndpointRef, chosen: String) -> MIDIEndpointRef? {
        pick(seen(count, get), chosen: chosen).map { get($0) }
    }

    private func connect() {
        let outs = Set((0..<MIDIGetNumberOfDestinations()).map { Self.name(MIDIGetDestination($0)) })
        ports = (0..<MIDIGetNumberOfSources()).map { Self.name(MIDIGetSource($0)) }.filter { outs.contains($0) }
        online = (0..<MIDIGetNumberOfSources()).map(MIDIGetSource).filter { Self.isOnline($0) }.map(Self.name).filter { outs.contains($0) }
        if !paused, !portName.isEmpty, !online.contains(portName) {
            paused = true; want = ""    // (the port in use gone, the USB cable out: the welcome screen, no other port taken; the
        }                               //  user, 2026-10-24: "NO AUTOCONNECT", it went on over Bluetooth by itself)
        let seenSrc = Self.seen(MIDIGetNumberOfSources(), MIDIGetSource)
        if paused, atLaunch, takeConnectedAtLaunch, let i = Self.launchPort(seenSrc, last: lastPort.isEmpty ? chosen : lastPort),
           outs.contains(seenSrc[i].name) {
            atLaunch = false; paused = false; want = seenSrc[i].name   // (the last port used, else an FM-1 port, already connected at launch: taken)
        }
        let src = paused ? nil : Self.find(MIDIGetNumberOfSources(), MIDIGetSource, chosen: want.isEmpty ? chosen : want)
        let dst = paused ? nil : Self.find(MIDIGetNumberOfDestinations(), MIDIGetDestination, chosen: want.isEmpty ? chosen : want)
        if !paused, let n = src.map(Self.name), n != lastPort { lastPort = n }
        guard src != source || dst != destination else { return }
        if source != 0 { MIDIPortDisconnectSource(inPort, source) }
        source = src ?? 0
        destination = dst ?? 0
        let bt = src.map(Self.isBluetooth) ?? false
        btFallback = Self.startFeatures(bluetooth: bt, name: src.map(Self.name) ?? "", trusted: !chosen.isEmpty || !want.isEmpty)
        lock.withLock {
            $0.assembler = SysExAssembler()
            $0.features = nil   // (INFO first, Bluetooth too: asked before the screen streams it comes whole, btFallback after)
            $0.bt = bt
            $0.known = false     // (the version is the new port's reply's: no frame drawn before it)
            $0.btInfo = 0
            $0.btKnown = false
        }
        companion = nil
        heard = false
        btDevice = nil
        btGaveUp = false
        guard let src, dst != nil else {
            portName = ""
            state = .searching
            return
        }
        MIDIPortConnectSource(inPort, src, nil)
        portName = Self.name(src)
        bluetooth = Self.isBluetooth(src)
        state = .asking
        asked = 0
        wantWhole = true
        tick()
    }

    // MARK: - requests

    private func tick() {
        guard destination != 0, holds == 0 else { return }
        // USB: one request at a time, the mirror's in a free moment between the editor's. Bluetooth: the firmware takes 106 apart
        // from the requests it queues (ed_mirror.c mbr_take), so the mirror's goes out every second whatever waits: an editor
        // request held 1.5 s and more for its reply there, and the screen stalled behind it (MIR_ALIVE_MS 3 s)
        if !bluetooth {
            guard gate.tryAcquire() else { return }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.06) { [gate] in gate.release() }   // (the reply, before the next request)
        }
        let bytes = lock.withLock { s in defer { s.bytes = 0 }; return s.bytes }
        if state == .mirroring {
            Self.log.info("\(self.bluetooth ? "bluetooth" : "usb", privacy: .public) \(bytes) bytes/s")
            print("\(bluetooth ? "bluetooth" : "usb") \(bytes) bytes/s")
            let l = lock.withLock { s -> String? in   // (checked frames: losses by kind and what was asked, each second they happened)
                let d = s.decoder
                let now = (sum: d.lostSum, gap: d.lostGap, more: d.lostMore, again: d.again)
                defer { s.logged = now; s.asksFix = 0; s.asksWhole = 0 }
                guard now != s.logged || s.asksFix + s.asksWhole > 0 else { return nil }
                return "lost: \(now.sum - s.logged.sum) sum, \(now.gap - s.logged.gap) gap, \(now.more - s.logged.more) more, " +
                    "\(now.again - s.logged.again) again; asked \(s.asksFix) by seq, \(s.asksWhole) whole"
            }
            if let l {
                Self.log.info("\(l, privacy: .public)")
                print(l)
            }
            let fm = lock.withLock { s -> String? in   // (the FM-1's own counts, what changed since the last line)
                guard let now = s.fmStats else { return nil }
                defer { s.fmLogged = now }
                guard let was = s.fmLogged, was != now else { return nil }
                let d = zip(now, was).map { ($0 - $1) & 0x1FFFFF }
                return "fm1: whole \(d[0]) lag, \(d[1]) ask, \(d[2]) unknown seq; \(d[3]) pages; \(d[4]) repaints for a lag, " +
                    "\(d[5]) repairs (\(d[7]) B near them); \(d[6]) B sent"
            }
            if let fm {
                Self.log.info("\(fm, privacy: .public)")
                print(fm)
            }
            let app = lock.withLock { s -> String? in   // (the app's own timing, each second frames came)
                defer { s.tm = Timing(last: s.tm.last, unshown: s.tm.unshown, heldSince: s.tm.heldSince, wholeAt: s.tm.wholeAt) }
                guard s.tm.frames > 0 || !s.tm.wholes.isEmpty else { return nil }
                return "app: \(s.tm.frames) writes in \(s.tm.calls) callbacks; longest pause \(Int(s.tm.maxGap * 1000)) ms, " +
                    "write to screen at most \(Int(s.tm.maxLag * 1000)) ms, held \(Int(s.tm.heldFor * 1000)) ms; whole panels \(s.tm.wholes) ms"
            }
            if let app {
                Self.log.info("\(app, privacy: .public)")
                print(app)
            }
            let pace = lock.withLock { s -> String? in   // (the FM-1's budget: logged when it changed)
                guard let now = s.fmPace, now != s.paceLogged else { return nil }
                let was = s.paceLogged.flatMap { $0.count == now.count ? $0 : nil } ?? now
                s.paceLogged = now
                let d = zip(now, was).map { ($0 - $1) & 0x1FFFFF }
                let late = now.count >= 6 ? "; last ACK \(now[4]) ms after its frame, \(d[5]) cuts for a delay" : ""
                return "fm1 pace: budget \(now[0]) B/s; \(d[1]) cuts, \(d[2]) raises, \(d[3]) frames refused" + late
            }
            if let pace {
                Self.log.info("\(pace, privacy: .public)")
                print(pace)
            }
        }
        if asked >= Self.unanswered, state != .mirroring { state = .noMirror }
        if let fb = btFallback, asked >= Self.btInfoFirst, lock.withLock({ $0.features == nil }) {
            lock.withLock { $0.features = fb }   // (Bluetooth: no whole INFO in 2 s, an FM-1's port: the screen; INFO asked beside it)
            asked = 0
        }
        let f = lock.withLock { $0.features }
        if f == nil {                   // INFO first: another firmware may give 106 another meaning (ed_opti.h)
            if companion != nil { return }   // (an fm1-base app: it has been identified, nothing else is asked)
            if asked >= Self.unanswered, asked % 2 == 1 {   // INFO went unanswered: is it an fm1-base app? (every other ask)
                sendFrame(CompanionWire.frame(CompanionWire.hello))
                asked += 1
                return
            }
            send([1])
            asked += 1
            return
        }
        guard f?[Feature.mirror.name] == Feature.mirror.version || state == .mirroring else {   // (its frames drawn: it mirrors)
            state = .noMirror
            return
        }
        if state == .mirroring && bluetooth {   // (over Bluetooth: INFO once mirroring, beside the mirror's request, never instead)
            let ask = lock.withLock { s -> BtAsk in
                let a = BtAsk.next(known: s.btKnown, ticks: s.btInfo)
                if a != .none { s.btInfo += 1 }
                return a
            }
            if ask == .ask { send([1]) }
            if ask == .gaveUp, !btGaveUp { btGaveUp = true }
        }
        // (bit 3: checked frames, an older firmware ignores it; bit 5 with opti_miraimd_1 over Bluetooth: the losses so far and the
        //  next seq, by which the FM-1 paces its stream; never to a firmware that does not list it: it would drop the request)
        let ack = bluetooth && f?[Feature.miraimd.name] == Feature.miraimd.version ? lock.withLock { $0.decoder.ack } : nil
        send([MirrorDecoder.cmd, (wantWhole ? 3 : 1) | MirrorDecoder.checkedBit | (ack == nil ? 0 : MirrorDecoder.ackBit)] + (ack ?? []))
        wantWhole = false
        asked += 1
    }

    private func send(_ body: [UInt8]) {
        sendInner([0x7D, 0x46, 0x4C] + body)
    }

    /// A whole F0 .. F7 frame (the companion services', in the update's framing)
    private func sendFrame(_ f: [UInt8]) {
        guard f.count >= 2 else { return }
        sendInner(Array(f[1..<(f.count - 1)]))
    }

    private func sendInner(_ inner: [UInt8]) {
        guard destination != 0 else { return }
        let words = SysExAssembler.packets(inner)
        let size = 64 + words.count / 2 * 32          // (a long request: a few hundred bytes, in 6-byte packets)
        let raw = UnsafeMutableRawPointer.allocate(byteCount: size, alignment: 8)
        defer { raw.deallocate() }
        let list = raw.bindMemory(to: MIDIEventList.self, capacity: 1)
        var packet = MIDIEventListInit(list, ._1_0)
        words.withUnsafeBufferPointer { w in
            for k in stride(from: 0, to: w.count, by: 2) {
                packet = MIDIEventListAdd(list, size, packet, 0, 2, w.baseAddress! + k)
            }
        }
        MIDISendEventList(outPort, destination, list)
    }

    // MARK: - the editor's requests

    /// An editor operation is running: no mirror requests until `release()` (a restore, a backup).
    func hold() { holds += 1 }
    func release() {
        holds = max(0, holds - 1)
        if holds == 0 { wantWhole = true }
    }

    /// The keyboard can press the panel: the firmware has PANEL, over USB, over Bluetooth when it lists opti_mirbt_1
    var canPress: Bool {
        state == .mirroring && lock.withLock { linkCarries($0) && $0.features?[Feature.panel.name] == Feature.panel.version }
    }
    /// The touch navigation (109 NAV) is there: the firmware lists opti_nav_1 (over Bluetooth: and opti_mirbt_1, as PANEL)
    var canNavigate: Bool {
        state == .mirroring && lock.withLock { linkCarries($0) && $0.features?[Feature.nav.name] == Feature.nav.version }
    }
    /// The clip editor (111 CLIP) is there: the firmware lists opti_clip_1 (over Bluetooth: and opti_mirbt_1, as PANEL)
    var canClip: Bool {
        state == .mirroring && lock.withLock { linkCarries($0) && $0.features?[Feature.clip.name] == Feature.clip.version }
    }
    /// The link in use takes PANEL and NAV: USB always, Bluetooth when the firmware said so (INFO there: opti_mirbt)
    private func linkCarries(_ s: Shared) -> Bool {
        !bluetooth || s.features?[Feature.mirbt.name] == Feature.mirbt.version
    }
    /// Why the touch navigation is not there while the screen mirrors (nil: it is, or nothing mirrors)
    var navMissing: String? {
        guard state == .mirroring, !canNavigate else { return nil }
        if bluetooth, !lock.withLock({ linkCarries($0) }) {
            return lock.withLock({ $0.btKnown || BtAsk.next(known: false, ticks: $0.btInfo) != .gaveUp })
                ? "Asking the FM-1 whether its buttons work over Bluetooth…"
                : "Over Bluetooth this firmware sends its screen only: the touch buttons need the USB cable, or a build made after 2026-10-24."
        }
        guard let f = lock.withLock({ $0.features }) else { return "Reading what this firmware offers…" }
        return f[Feature.nav.name] == nil
            ? "This firmware has no touch buttons (opti_nav_1): install a build made after 2026-10-24 with ED_MIRROR."
            : "This firmware's touch buttons are another version than this app reads."
    }
    /// Panel writes applied so far (a change of the screen: the navigation asks again)
    var generation: Int { lock.withLock { $0.generation } }
    private var presses: [[UInt8]] = []
    private var pressing = false
    /// Knob `role` (panel.c EN_*: 3..6 KNOB 1..4) turned `steps` detents (+ clockwise): merged into a turn of the same knob
    /// still waiting in the queue, so a fast drag sends one request per round trip, not one per detent
    @MainActor
    func turn(_ role: UInt8, _ steps: Int) {
        guard steps != 0 else { return }
        if let last = presses.last, last.count == 3, last[0] == 2, last[1] == role {
            let s = Int(last[2]) - 64 + steps
            if abs(s) <= 63 {
                presses[presses.count - 1] = [2, role, UInt8(64 + s)]
                return
            }
        }
        press([2, role, UInt8(64 + max(-63, min(63, steps)))])
    }
    /// The firmware takes PANEL's seq and let go (opti_panelx_1)
    private var hasPanelX: Bool { lock.withLock { $0.features?[Feature.panelx.name] == Feature.panelx.version } }
    private var seq: UInt8 = 0
    /// A PANEL request (cmd 108), in order, one at a time with the others. With opti_panelx_1 each carries a seq, so one whose
    /// answer was lost is sent again (twice more at most) and done once: a key-up is never lost
    @MainActor
    func press(_ args: [UInt8]) {
        presses.append(args)
        guard !pressing else { return }
        pressing = true
        Task { @MainActor in
            while !presses.isEmpty {
                let a = presses.removeFirst(), x = hasPanelX
                if x { seq = (seq + 1) & 127 }
                _ = try? await exchange(.panel, x ? a + [seq] : a, timeout: 0.3, retries: x ? 2 : 0)
            }
            pressing = false
        }
    }
    /// Every key the app holds up (the app left the front, its key-ups will not come): one let go (108 kind 4), else each key up
    @MainActor
    func letGo(_ held: [[UInt8]]) {
        if hasPanelX { press([4, 0, 0]) } else { held.forEach { press($0 + [0]) } }
    }

    /// True when the port in use can carry the editor's requests: the USB one (over Bluetooth the FM-1 answers the mirror only).
    var editorPossible: Bool { state != .searching && !bluetooth }
    /// A port is taken and is Bluetooth: the editor may ask INFO there, whether the firmware lists opti_btproj_1 decides the rest
    /// (EditorRoute)
    var bluetoothInUse: Bool { state != .searching && bluetooth }

    @MainActor
    func exchange(_ cmd: Cmd, _ args: [UInt8], timeout: TimeInterval, retries: Int) async throws -> [UInt8] {
#if DEBUG
        if let demo, cmd == .nav { return demo.reply(args) }
        if let demo, cmd == .clip { return demo.clip(args) }
#endif
        let body = try Wire.body(cmd.rawValue, args)
        await gate.acquire()
        defer { gate.release() }
        let (wait, tries) = bluetooth ? BtPolicy.apply(cmd, timeout: timeout, retries: retries) : (timeout, retries)
        var left = tries
        while true {
            do {
                return try await attempt(cmd.rawValue, args, body, wait)
            } catch let e as WireError where e.isTimeout && left > 0 {
                left -= 1
            }
        }
    }

    @MainActor
    private func attempt(_ cmd: UInt8, _ args: [UInt8], _ body: [UInt8], _ timeout: TimeInterval) async throws -> [UInt8] {
        guard destination != 0 else { throw WireError("no FM-1 port") }
        return try await withCheckedThrowingContinuation { (c: CheckedContinuation<[UInt8], Error>) in
            let token = lock.withLock { s -> Int in
                s.token += 1
                s.pending = Pending(cmd: cmd, args: args, token: s.token, continuation: c)
                return s.token
            }
            send(body)
            let lock = self.lock
            DispatchQueue.global().asyncAfter(deadline: .now() + timeout) { Self.expire(lock, token, cmd) }
        }
    }

    private static func expire(_ lock: OSAllocatedUnfairLock<Shared>, _ token: Int, _ cmd: UInt8) {
        let c = lock.withLock { s -> CheckedContinuation<[UInt8], Error>? in
            guard let p = s.pending, p.token == token else { return nil }
            s.pending = nil
            return p.continuation
        }
        c?.resume(throwing: WireError.timeout(cmd))
    }

    // MARK: - an fm1-base app's companion requests

    /// One companion request (CompanionTransport), in its turn with the mirror's and the editor's.
    @MainActor
    func companion(_ cmd: UInt8, _ body: [UInt8], timeout: TimeInterval) async throws -> [UInt8] {
        await gate.acquire()
        defer { gate.release() }
        guard destination != 0 else { throw CompanionError.silent(cmd) }
        return try await withCheckedThrowingContinuation { (c: CheckedContinuation<[UInt8], Error>) in
            let token = lock.withLock { s -> Int in
                s.token += 1
                s.cpending = CPending(cmd: cmd, body: body, token: s.token, continuation: c)
                return s.token
            }
            sendFrame(CompanionWire.frame(cmd, body))
            let lock = self.lock
            DispatchQueue.global().asyncAfter(deadline: .now() + timeout) {
                let c = lock.withLock { s -> CheckedContinuation<[UInt8], Error>? in
                    guard let p = s.cpending, p.token == token else { return nil }
                    s.cpending = nil
                    return p.continuation
                }
                c?.resume(throwing: CompanionError.silent(cmd))
            }
        }
    }

    // MARK: - what the FM-1 sends (CoreMIDI's thread)

    private func receive(_ list: UnsafePointer<MIDIEventList>) {
        var frames: [[UInt8]] = []
        let offset = MemoryLayout<MIDIEventPacket>.offset(of: \MIDIEventPacket.words)!
        for packet in list.unsafeSequence() {
            let count = Int(packet.pointee.wordCount)
            let words = (UnsafeRawPointer(packet) + offset).assumingMemoryBound(to: UInt32.self)
            var k = 0
            while k < count {
                let type = words[k] >> 28
                let size = [1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4][Int(type)]
                if type == 3, k + 1 < count {
                    let w0 = words[k], w1 = words[k + 1]
                    if let f = lock.withLock({ $0.assembler.take(w0, w1) }) { frames.append(f) }
                }
                k += size
            }
        }
        guard !frames.isEmpty else { return }
        let got = frames
        lock.withLock { $0.tm.calls += 1 }
        let (replied, identified, answers, companionInfo, drew, btDev) = lock.withLock { s -> (Bool, Bool, [(CheckedContinuation<[UInt8], Error>, [UInt8])], CompanionInfo?, Bool, DeviceInfo?) in
            var identified = false, drew = false
            var btDev: DeviceInfo?
            var companionInfo: CompanionInfo?
            var on = false
            var answers: [(CheckedContinuation<[UInt8], Error>, [UInt8])] = []
            for f in got {
                s.bytes += f.count
                if f.count > 1, f[1] == 0x00, let c = CompanionWire.decode(f) {      // an fm1-base app's answer (never Optimist's: F0 7D ..)
                    if c.cmd == CompanionWire.hello, let i = try? CompanionInfo.parse(c.body), s.features == nil { companionInfo = i }
                    if let p = s.cpending, p.cmd == c.cmd, CompanionWire.matches(p.cmd, p.body, c.body) {
                        s.cpending = nil
                        answers.append((p.continuation, c.body))
                    }
                } else if !MirrorDecoder.isMirror(f), let u = Wire.unframe(f) {         // an editor reply
                    if u.cmd == 1, let i = Self.takeInfo(u.args, bluetooth: s.bt) {   // (Bluetooth: a whole one only; a damaged one is asked again)
                        if s.features == nil { s.features = i.features; identified = true }
                        else if s.bt, !s.btKnown { s.features = i.features }   // (Bluetooth: the whole list now, opti_mirbt among it)
                        if s.bt { s.btKnown = true; btDev = i }
                    }
                    if let p = s.pending, p.cmd == u.cmd, Wire.replyMatches(p.cmd, p.args, u.args) {
                        s.pending = nil
                        answers.append((p.continuation, u.args))
                    }
                } else if let st = MirrorDecoder.stats(f) {
                    s.fmStats = st
                } else if let p = MirrorDecoder.pace(f) {
                    s.fmPace = p
                } else if let l = MirrorDecoder.link(f) {
                    Self.log.info("bluetooth link: \(l.octets) octets a packet, MTU \(l.mtu), interval \(Double(l.interval) * 1.25) ms, central rx/tx \(l.central)")
                    print("bluetooth link: \(l.octets) octets a packet, MTU \(l.mtu), interval \(Double(l.interval) * 1.25) ms, central rx/tx \(l.central)")   // (devicectl --console)
                } else if let r = MirrorDecoder.reply(f) {
                    s.known = r.version == MirrorDecoder.version
                    on = on || (r.on && s.known)
                    s.replies += 1
                } else if s.known, s.decoder.apply(f) {
                    let now = Date()
                    s.tm.frames += 1
                    if let l = s.tm.last { s.tm.maxGap = max(s.tm.maxGap, now.timeIntervalSince(l)) }
                    s.tm.last = now
                    if s.tm.unshown == nil { s.tm.unshown = now }
                    if Self.wholeFill(f) {
                        s.tm.wholeAt = now
                        s.held = Date()
                        s.rows = [Bool](repeating: false, count: 240)
                        s.rowsLeft = 240
                    } else if s.rowsLeft > 0, f.count >= 14, f[5] <= 1 || f[5] == 8 || f[5] == 9 {   // (FILL, BLIT: their rows; MORE goes on in them)
                        let y = Int(f[8]) | Int(f[9]) << 7, h = Int(f[12]) | Int(f[13]) << 7
                        for r in max(0, y)..<min(240, y + h) where !s.rows[r] { s.rows[r] = true; s.rowsLeft -= 1 }
                        if s.rowsLeft <= Self.rowsSpare, let w = s.tm.wholeAt {
                            s.tm.wholes.append(Int(now.timeIntervalSince(w) * 1000)); s.tm.wholeAt = nil
                        }
                    }
                    s.lastFrame = Date()
                    s.generation += 1
                    drew = true
                }
            }
            return (on, identified, answers, companionInfo, drew, btDev)
        }
        for (c, a) in answers { c.resume(returning: a) }
        // checked frames lost: with opti_mirfix_1 their seqs asked for (only their rectangles come again, nothing held), else the
        // whole panel (at most twice a second)
        let lostAsks = lock.withLock { s -> [[UInt8]] in
            guard s.decoder.lost != s.lostSeen else { return [] }
            if s.features?[Feature.mirfix.name] == Feature.mirfix.version {
                let runs = s.decoder.takeLost()            // (a sum failed: its seq shows at the next good frame)
                guard !runs.isEmpty else { return [] }
                s.lostSeen = s.decoder.lost
                s.asksFix += runs.count
                return runs.map { [MirrorDecoder.cmd, 1 | MirrorDecoder.checkedBit | MirrorDecoder.fixBit, UInt8($0.lo), UInt8($0.hi)] }
            }
            guard Date().timeIntervalSince(s.lostAsk) > 0.5 else { return [] }
            s.lostSeen = s.decoder.lost
            s.lostAsk = Date()
            _ = s.decoder.takeLost()
            s.asksWhole += 1
            return [[MirrorDecoder.cmd, 3 | MirrorDecoder.checkedBit]]
        }
        // Bluetooth with opti_miraimd_1: an ACK alone (no reply) about 5 times a second while frames come, with the next seq: the
        // FM-1 times each frame to it and halves its budget when they arrive late (a queue in the Mac or on the air)
        let ackNow = lock.withLock { s -> [UInt8]? in
            guard drew, s.bt, s.features?[Feature.miraimd.name] == Feature.miraimd.version, Date().timeIntervalSince(s.ackAt) >= 0.2 else { return nil }
            s.ackAt = Date()
            return [MirrorDecoder.cmd, 1 | MirrorDecoder.checkedBit | MirrorDecoder.ackBit | MirrorDecoder.quietBit] + s.decoder.ack
        }
        if let ackNow {
            DispatchQueue.main.async { if self.state == .mirroring, self.destination != 0 { self.send(ackNow) } }
        }
        if !lostAsks.isEmpty {          // (the requests alone: tick's other asks and its rate log keep their pace)
            DispatchQueue.main.async {
                if self.state == .mirroring, self.destination != 0 { lostAsks.forEach { self.send($0) } }
            }
        }
        if let btDev { DispatchQueue.main.async { if self.btDevice != btDev { self.btDevice = btDev } } }
        if replied || drew {            // (a panel write drawn is the mirror answering too: a lost 106 reply never shows No mirror)
            DispatchQueue.main.async {
                self.asked = 0
                if self.state != .mirroring { self.state = .mirroring }   // (once: a frame batch comes many times a second)
            }
        } else if identified {          // (INFO in: the mirror's own requests start counting now)
            DispatchQueue.main.async { self.asked = 0; self.tick() }
        }
        if let companionInfo { DispatchQueue.main.async { if self.companion != companionInfo { self.companion = companionInfo } } }
        DispatchQueue.main.async { if !self.heard { self.heard = true } }
    }
}

/// One holder at a time: the editor's requests and the mirror's wait their turn.
final class AsyncGate: @unchecked Sendable {
    private let lock = NSLock()
    private var held = false
    private var waiters: [CheckedContinuation<Void, Never>] = []

    func tryAcquire() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        if held { return false }
        held = true
        return true
    }

    func acquire() async {
        await withCheckedContinuation { (c: CheckedContinuation<Void, Never>) in
            lock.lock()
            if held {
                waiters.append(c)
                lock.unlock()
            } else {
                held = true
                lock.unlock()
                c.resume()
            }
        }
    }

    func release() {
        lock.lock()
        if waiters.isEmpty {
            held = false
            lock.unlock()
        } else {
            let c = waiters.removeFirst()
            lock.unlock()
            c.resume()
        }
    }
}
