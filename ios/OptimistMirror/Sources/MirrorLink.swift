// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 over CoreMIDI: its USB port ("Felucca" / "Optimist") when it is plugged in, else its Bluetooth MIDI port (connected with
// iOS's pairing sheet, BluetoothPicker.swift; the FM-1 answers only the mirror's request there). Asks for the mirror (cmd 106)
// every second, takes the panel writes it pushes into a MirrorDecoder. The same ports carry the editor's requests (Editor/):
// exchange(...) sends one and waits for its reply; the mirror's request waits for a free moment between them (one request at a
// time, as the web page does), and does not go out at all while an editor operation holds the link (hold()).
import CoreMIDI
import Foundation
import os

final class MirrorLink: ObservableObject, Transport {
    enum State: Equatable {
        case searching            // no FM-1 port
        case asking               // a port, no reply yet
        case noMirror             // the FM-1 answers nothing to 106: a build without ED_MIRROR
        case mirroring
    }

    @Published private(set) var state: State = .searching
    @Published private(set) var portName = ""
    @Published private(set) var bluetooth = false   // the port in use is the Bluetooth one
    @Published private(set) var ports: [String] = []   // every MIDI port with both directions, by name (the picker)
    /// .. of them, the ones online: the Mac keeps a Bluetooth port it paired listed while it is away (offline), so the
    /// Bluetooth sheet's connection shows as one going online, not as a new port
    @Published private(set) var online: [String] = []
    /// The port picked by hand ("": the FM-1's, found by its name); kept across launches
    /// No port taken until a port is chosen, Connect is pressed or the FM-1 is picked in the Bluetooth sheet: at launch and after
    /// Disconnect alike, the welcome screen (the user, 2026-10-24: "NO AUTOCONNECT")
    @Published private(set) var paused = true
    func disconnect() { paused = true; reconnect() }
    func resume() { paused = false; reconnect() }
    private func reconnect() {
        if source != 0 { MIDIPortDisconnectSource(inPort, source) }
        source = 0; destination = 0
        connect()
    }
    @Published var chosen = UserDefaults.standard.string(forKey: "port") ?? "" {
        didSet {
            paused = false
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
        var btInfo = 0            // Bluetooth, mirroring: INFOs sent to learn whether PANEL and NAV go there too (opti_mirbt)
        var btKnown = false       // .. and its answer came (an older firmware answers none: the screen only)
        var held: Date?           // a catch-up began (the whole panel filled): the picture shown stays until it is in
        var rows = [Bool](repeating: true, count: 240)   // since that fill, the rows drawn again
        var rowsLeft = 0
        var lastFrame = Date()
        var pending: Pending?     // the editor request waiting for its reply
        var token = 0
    }
    private struct Pending {
        var cmd: UInt8
        var args: [UInt8]
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

    private static let keepAlive: TimeInterval = 1.0
#if DEBUG
    private var demo: NavDemo?    // MIRROR_DEMO: 109 NAV answered here (NavDemo.swift)
#endif
    private static let unanswered = 3   // requests without a reply: no mirror in this build
    private static let btReplyWait: TimeInterval = 1.5   // (Bluetooth: a reply waits behind up to 0.65 s of screen, ed_mirror.c MIR_BLE_LAG)

    init() {
#if DEBUG
        // the store screenshots on a simulator (no MIDI there): MIRROR_DEMO = a 240 x 240 RGB565 file (little-endian), shown as
        // if an FM-1 mirrored it; a DEBUG build only, never the store's
        if let path = ProcessInfo.processInfo.environment["MIRROR_DEMO"], let d = FileManager.default.contents(atPath: path),
           d.count == MirrorDecoder.side * MirrorDecoder.side * 2 {
            let px = stride(from: 0, to: d.count, by: 2).map { UInt16(d[$0]) | UInt16(d[$0 + 1]) << 8 }
            lock.withLock { $0.decoder.show(px); $0.generation = 1; $0.features = [Feature.nav.name: Feature.nav.version, Feature.clip.name: Feature.clip.version] }
            demo = NavDemo()
            portName = "Optimist"
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
                let most = bluetooth ? Self.catchMax : Self.catchMaxUSB
                if s.rowsLeft > Self.rowsSpare && now.timeIntervalSince(s.lastFrame) < Self.catchQuiet && now.timeIntervalSince(h) < most { return nil }
                s.held = nil
            }
            return s.generation == seen ? nil : (s.decoder.pixels, s.generation)
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
        f.count == 18 && f[5] == 0 && Array(f[6..<14]) == [0, 0, 0, 0, 112, 1, 112, 1]
    }

    /// Ask for the whole panel again (the app came back to the front).
    func refresh() {
        wantWhole = true
        tick()
    }

    // MARK: - ports

    private static func name(_ e: MIDIEndpointRef) -> String {
        var s: Unmanaged<CFString>?
        MIDIObjectGetStringProperty(e, kMIDIPropertyDisplayName, &s)
        return (s?.takeRetainedValue() as String?) ?? ""
    }

    private static func isFM1(_ e: MIDIEndpointRef) -> Bool {
        let n = name(e).lowercased()
        return n.contains("felucca") || n.contains("optimist")
    }

    /// A Bluetooth MIDI port: its device's driver is Apple's Bluetooth MIDI driver, or its name says so (as on a Mac).
    private static func isBluetooth(_ e: MIDIEndpointRef) -> Bool {
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

    /// The port picked by hand when it is there, else the FM-1's: USB first, Bluetooth else.
    private static func find(_ count: Int, _ get: (Int) -> MIDIEndpointRef, chosen: String) -> MIDIEndpointRef? {
        let all = (0..<count).map(get)
        if !chosen.isEmpty, let e = all.first(where: { name($0) == chosen }) { return e }
        let fm1 = all.filter(isFM1)
        return fm1.first { !isBluetooth($0) } ?? fm1.first
    }

    private func connect() {
        let outs = Set((0..<MIDIGetNumberOfDestinations()).map { Self.name(MIDIGetDestination($0)) })
        ports = (0..<MIDIGetNumberOfSources()).map { Self.name(MIDIGetSource($0)) }.filter { outs.contains($0) }
        online = (0..<MIDIGetNumberOfSources()).map(MIDIGetSource).filter { Self.isOnline($0) }.map(Self.name).filter { outs.contains($0) }
        let src = paused ? nil : Self.find(MIDIGetNumberOfSources(), MIDIGetSource, chosen: chosen)
        let dst = paused ? nil : Self.find(MIDIGetNumberOfDestinations(), MIDIGetDestination, chosen: chosen)
        guard src != source || dst != destination else { return }
        if source != 0 { MIDIPortDisconnectSource(inPort, source) }
        source = src ?? 0
        destination = dst ?? 0
        let bt = src.map(Self.isBluetooth) ?? false
        let named = src.map { !chosen.isEmpty || Self.name($0).lowercased().contains("optimist") } ?? false   // (picked by hand: trusted)
        lock.withLock {                 // (over Bluetooth only 106 is answered: our firmware is known by its name, "Optimist ...")
            $0.assembler = SysExAssembler()
            $0.features = bt ? (named ? [Feature.mirror.name: Feature.mirror.version] : [:]) : nil
            $0.btInfo = 0
            $0.btKnown = false
        }
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
        guard destination != 0, holds == 0, gate.tryAcquire() else { return }
        defer { DispatchQueue.main.asyncAfter(deadline: .now() + 0.06) { [gate] in gate.release() } }   // (the reply, before the next request)
        let bytes = lock.withLock { s in defer { s.bytes = 0 }; return s.bytes }
        if state == .mirroring {
            Self.log.info("\(self.bluetooth ? "bluetooth" : "usb", privacy: .public) \(bytes) bytes/s")
            print("\(bluetooth ? "bluetooth" : "usb") \(bytes) bytes/s")
        }
        if asked >= Self.unanswered, state != .mirroring { state = .noMirror }
        let f = lock.withLock { $0.features }
        if f == nil {                   // INFO first: another firmware may give 106 another meaning (ed_opti.h)
            send([1])
            asked += 1
            return
        }
        guard f?[Feature.mirror.name] == Feature.mirror.version else {
            state = .noMirror
            return
        }
        let askBT = state == .mirroring && bluetooth && lock.withLock { s -> Bool in
            guard !s.btKnown, s.btInfo < Self.unanswered else { return false }
            s.btInfo += 1
            return true
        }
        if askBT {                      // (over Bluetooth: INFO once mirroring; a firmware with opti_mirbt answers it there)
            send([1])
            return
        }
        send([MirrorDecoder.cmd, wantWhole ? 3 : 1])
        wantWhole = false
        asked += 1
    }

    private func send(_ body: [UInt8]) {
        guard destination != 0 else { return }
        let words = SysExAssembler.packets([0x7D, 0x46, 0x4C] + body)
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
            return lock.withLock({ $0.btKnown || $0.btInfo < Self.unanswered })
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
    /// A PANEL request (cmd 108), in order, one at a time with the others
    @MainActor
    func press(_ args: [UInt8]) {
        presses.append(args)
        guard !pressing else { return }
        pressing = true
        Task { @MainActor in
            while !presses.isEmpty {
                let a = presses.removeFirst()
                _ = try? await exchange(.panel, a, timeout: 0.3, retries: 0)
            }
            pressing = false
        }
    }

    /// True when the port in use can carry the editor's requests: the USB one (over Bluetooth the FM-1 answers the mirror only).
    var editorPossible: Bool { state != .searching && !bluetooth }

    @MainActor
    func exchange(_ cmd: Cmd, _ args: [UInt8], timeout: TimeInterval, retries: Int) async throws -> [UInt8] {
#if DEBUG
        if let demo, cmd == .nav { return demo.reply(args) }
        if let demo, cmd == .clip { return demo.clip(args) }
#endif
        let body = try Wire.body(cmd.rawValue, args)
        await gate.acquire()
        defer { gate.release() }
        var left = retries
        while true {
            do {
                return try await attempt(cmd.rawValue, args, body, bluetooth ? max(timeout, Self.btReplyWait) : timeout)
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
        let (replied, identified, answers) = lock.withLock { s -> (Bool, Bool, [(CheckedContinuation<[UInt8], Error>, [UInt8])]) in
            var identified = false
            var on = false
            var answers: [(CheckedContinuation<[UInt8], Error>, [UInt8])] = []
            for f in got {
                s.bytes += f.count
                if !MirrorDecoder.isMirror(f), let u = Wire.unframe(f) {         // an editor reply
                    if u.cmd == 1, s.features == nil, let i = try? DeviceInfo.parse(u.args) { s.features = i.features; identified = true }
                    else if u.cmd == 1, s.btInfo > 0, !s.btKnown, let i = try? DeviceInfo.parse(u.args) {
                        s.features = i.features   // (Bluetooth: the whole list now, opti_mirbt among it)
                        s.btKnown = true
                    }
                    if let p = s.pending, p.cmd == u.cmd, Wire.replyMatches(p.cmd, p.args, u.args) {
                        s.pending = nil
                        answers.append((p.continuation, u.args))
                    }
                } else if let l = MirrorDecoder.link(f) {
                    Self.log.info("bluetooth link: \(l.octets) octets a packet, MTU \(l.mtu), interval \(Double(l.interval) * 1.25) ms, central rx/tx \(l.central)")
                    print("bluetooth link: \(l.octets) octets a packet, MTU \(l.mtu), interval \(Double(l.interval) * 1.25) ms, central rx/tx \(l.central)")   // (devicectl --console)
                } else if let r = MirrorDecoder.reply(f) {
                    s.known = r.version == MirrorDecoder.version
                    on = on || (r.on && s.known)
                    s.replies += 1
                } else if s.known, s.decoder.apply(f) {
                    if Self.wholeFill(f) {
                        s.held = Date()
                        s.rows = [Bool](repeating: false, count: 240)
                        s.rowsLeft = 240
                    } else if s.rowsLeft > 0, f.count >= 14, f[5] <= 1 {   // (FILL, BLIT: their rows; MORE goes on in them)
                        let y = Int(f[8]) | Int(f[9]) << 7, h = Int(f[12]) | Int(f[13]) << 7
                        for r in max(0, y)..<min(240, y + h) where !s.rows[r] { s.rows[r] = true; s.rowsLeft -= 1 }
                    }
                    s.lastFrame = Date()
                    s.generation += 1
                }
            }
            return (on, identified, answers)
        }
        for (c, a) in answers { c.resume(returning: a) }
        if replied {
            DispatchQueue.main.async {
                self.asked = 0
                self.state = .mirroring
            }
        } else if identified {          // (INFO in: the mirror's own requests start counting now)
            DispatchQueue.main.async { self.asked = 0; self.tick() }
        }
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
