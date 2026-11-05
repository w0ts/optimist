// SPDX-License-Identifier: GPL-3.0-only
// An install, after web/fm1ota.js Updater (install, write, resume): the running firmware stages the update loader (step 1, where
// nothing is written yet), the loader writes the image (step 2), the new firmware comes back and must report the package's
// identity. A device found in our loader already (an install that broke off) is finished from step 2.
import Foundation

struct FlashError: Error, Equatable, CustomStringConvertible {
    enum Code: String { case notFound, model, stopped, noLoader, lost, noReturn, mismatch, badRequest, foreign, cancelled, unexpected }
    let code: Code
    let description: String
}

enum FlashStep: Equatable {
    case start(String)            // the firmware found (its identity)
    case verify(Int)              // step 1: reads answered
    case loader                   // waiting for the update loader
    case write(Int, UInt32)       // step 2: reads answered, the last address
    case reboot
    case done(String)             // the identity the FM-1 reports now
    case found(String)            // found again after a dropped connection (its identity): resuming
    case checking(Int)            // step 2, the fast loader checks what is on the FM-1: the bar in percent (FlashCheckBar)
    case kept                     // the loader went on with its fast session over a replug (it never heard the new hello)
}

/// The bar while a fast loader checks the sectors already on the FM-1, in percent of the image, never going back
/// (tools/fm1_install.py CheckBar, web/fm1ota_optimist.js OptCheckBar): its 0x32 frames, else an estimate by time at
/// `sectorCheck` per sector, slower than the loader on purpose (a bitwise CRC16 and CRC-32 over a sector read from flash:
/// ~7 ms on the FM-1, as a resumed install took ~1 s for ~147 sectors, 2026-11-01, not measured per sector), so it stays
/// behind it; report(pct) until the first read request (the write's own progress after).
struct FlashCheckBar {
    let sectors: Int
    let start: Date
    var framesWait: TimeInterval = 0.5
    var sectorCheck: TimeInterval = 0.012
    private(set) var pct = 0
    private(set) var frames = false
    private(set) var asked = false

    init(sectors: Int, start: Date = Date(), framesWait: TimeInterval = 0.5, sectorCheck: TimeInterval = 0.012) {
        self.sectors = max(1, sectors)
        self.start = start
        self.framesWait = framesWait
        self.sectorCheck = sectorCheck
    }

    /// -> the new percentage when it rose
    private mutating func set(_ v: Double) -> Int? {
        let p = Int(min(99, v))
        guard p > pct, !asked else { return nil }
        pct = p
        return p
    }
    mutating func tick(_ now: Date = Date()) -> Int? {
        let t = now.timeIntervalSince(start)
        guard !frames, t > framesWait else { return nil }
        return set(min(95, t * 100 / (Double(sectors) * sectorCheck)))
    }
    mutating func frame(_ checked: Int, _ total: Int) -> Int? {
        frames = true
        return set(Double(checked) * 100 / Double(total))
    }
    mutating func request() { asked = true }
}

/// The cable dropped mid-install (the user, 2026-11-01: "There should be a user warning 'connection dropped, verify cable and
/// enter to resume'", then "the flasher should automatically resume", and "DO NOT POWER OFF until the flashing is
/// finished"): the sentences, and what the FM-1 found again means (tools/fm1_install.py resume_plan, web/install_route.js
/// resumePlan).
enum FlashResume {
    static let dropped = "Connection dropped. Check the cable: the install continues by itself when the FM-1 is back. Do not power off the FM-1."
    static let noPowerOff = "Do not power off the FM-1."
    enum Phase: String { case check, write }      // step 1 (the running firmware stages the loader), step 2 (the loader writes)
    /// absent: nothing answered (ask again); write: our loader (step 2 again: the fast loader skips every sector already right);
    /// restart: the same running firmware after step 1 (it gave the update up: step 1 again); done: the package running after
    /// step 2 (the loader had finished); foreign: another firmware's loader; unexpected: anything else (stop, nothing sent)
    enum Plan: String { case absent, write, restart, done, foreign, unexpected }

    static func plan(_ phase: Phase, _ id: FlashProtocol.Identity?, product: String, running: String) -> Plan {
        guard let id else { return .absent }
        let model = { (t: String) in t.split(separator: "_").first.map { String($0).replacingOccurrences(of: "ota-", with: "", options: [.caseInsensitive, .anchored]) } ?? "" }
        if model(id.text) != model(product) { return .unexpected }
        if id.isLoader { return id.isOurLoader ? .write : .foreign }
        if phase == .check, id.text == running { return .restart }
        if phase == .write, id.text == product { return .done }
        return .unexpected
    }
}

/// Which FM-1 a MIDI port belongs to (web/install_route.js fm1Key, tools/fm1_install.py fm1_devices): its "FM1-XXXX" (all its ports
/// share it), else the port's name. Bluetooth ports (the same FM-1 seen twice) and generic names ("USB-MIDI", "composite") are none.
enum FlashDevices {
    static func key(_ name: String) -> String? {
        if name.range(of: "bluetooth", options: .caseInsensitive) != nil { return nil }
        if let r = name.range(of: "FM1-[0-9A-F]{4}", options: [.regularExpression, .caseInsensitive]) { return name[r].uppercased() }
        return name.range(of: "fm-1|optimist|felucca", options: [.regularExpression, .caseInsensitive]) != nil ? name : nil
    }

    /// the FM-1s among the port names, one key each, in the order met
    static func keys(_ names: [String]) -> [String] {
        var out: [String] = []
        for k in names.compactMap(key) where !out.contains(k) { out.append(k) }
        return out
    }
}

/// A flag set on the main actor (Cancel) and read by the install, wherever it runs.
final class FlashCancel: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    var isSet: Bool { lock.lock(); defer { lock.unlock() }; return value }
    func set(_ v: Bool) { lock.lock(); value = v; lock.unlock() }
}

final class FlashUpdater {
    private let ports: FlashPorts
    /// the keys (FlashDevices) of the FM-1s that are not the chosen one: no port of theirs is ever asked
    var excluded: Set<String> = []
    /// the chosen FM-1's key when it is an FM1-XXXX: a port with another token is not it (a loader's port may carry another name)
    var chosen: String?
    /// where a dropped connection happened (installResumable)
    private(set) var phase: FlashResume.Phase?
    private let pause: (TimeInterval) async -> Void
    /// the waits of the protocol (the web installer's); tests shrink them
    struct Timing {
        var afterStop: TimeInterval = 0.1        // after the MIDI STOP, before the upgrade command
        var afterUpgrade: TimeInterval = 2
        var beforeLoader: TimeInterval = 3
        var loaderWait: TimeInterval = 30
        var beforeReturn: TimeInterval = 3
        var returnWait: TimeInterval = 40
        var poll: TimeInterval = 1
        var step1Idle: TimeInterval = 8
        var step2Idle: TimeInterval = 180
        var perRead: TimeInterval = 0.01
        var fastWord: TimeInterval = 0.5        // the wait for the loader's answer to the fast install's word
        var afterUpgradeFast: TimeInterval = 0.05
        var resumePoll: TimeInterval = 0.5     // after a drop: the FM-1 looked for this often, by itself, until it is back or Cancel
        var loaderPoll: TimeInterval = 0.2     // after step 1: the loader looked for this often (no blind wait), beforeLoader + loaderWait in all
        var checkFrames: TimeInterval = 0.5    // step 2: the fast loader's 0x32 frames looked for this long, else the estimate
        var sectorCheck: TimeInterval = 0.012  // the estimate's time per sector (FlashCheckBar)
        var checkTick: TimeInterval = 0.1      // how often the bar looks while the loader checks
    }
    var timing = Timing()

    init(ports: FlashPorts, pause: @escaping (TimeInterval) async -> Void = { try? await Task.sleep(nanoseconds: UInt64($0 * 1e9)) }) {
        self.ports = ports
        self.pause = pause
    }

    struct Found {
        let channel: FlashChannel
        let id: FlashProtocol.Identity
    }

    // MARK: - finding the FM-1

    private func handshake(_ c: FlashChannel, tries: Int) async -> FlashProtocol.Identity? {
        c.drain()
        for _ in 0..<tries {
            guard c.send(FlashProtocol.handshake) else { return nil }
            guard let id = await identity(c, seconds: 1) else { continue }
            guard FlashProtocol.mayBeCut(id), c.send(FlashProtocol.handshakeFull) else { return id }
            return FlashProtocol.whole(id, await identity(c, seconds: 0.6))
        }
        return nil
    }

    private func identity(_ c: FlashChannel, seconds: TimeInterval) async -> FlashProtocol.Identity? {
        let end = Date().addingTimeInterval(seconds)
        while Date() < end {
            guard let p = await c.read(timeout: end.timeIntervalSinceNow) else { break }
            if let id = FlashProtocol.identity(p) { return id }
        }
        return nil
    }

    /// The FM-1s on the cable by their port names: nothing is sent to any port.
    func devices() -> [String] {
        let channels = ports.channels()
        let keys = FlashDevices.keys(channels.map(\.name))
        channels.forEach { $0.close() }
        return keys
    }

    /// The first port that answers the handshake with an identity `filter` takes (never one of an FM-1 not chosen).
    func find(_ filter: ((FlashProtocol.Identity) -> Bool)? = nil) async -> Found? {
        var found: Found?
        for c in ports.channels() {
            if notChosen(c.name) { c.close(); continue }
            if found == nil, let id = await handshake(c, tries: 2), filter?(id) ?? true {
                found = Found(channel: c, id: id)
            } else {
                c.close()
            }
        }
        return found
    }

    /// a port of an FM-1 that was not chosen: one of the others' keys, or another FM1-XXXX than the chosen one's
    func notChosen(_ name: String) -> Bool {
        if let k = FlashDevices.key(name), excluded.contains(k) { return true }
        guard let c = chosen, c.range(of: "^FM1-[0-9A-F]{4}$", options: [.regularExpression, .caseInsensitive]) != nil,
              let r = name.range(of: "FM1-[0-9A-F]{4}", options: [.regularExpression, .caseInsensitive]) else { return false }
        return name[r].uppercased() != c.uppercased()
    }

    private func waitFor(_ filter: @escaping (FlashProtocol.Identity) -> Bool, _ seconds: TimeInterval, every: TimeInterval? = nil) async -> Found? {
        let end = Date().addingTimeInterval(seconds)
        while Date() < end {
            if let f = await find(filter) { return f }
            if Task.isCancelled { return nil }
            await pause(every ?? timing.poll)
        }
        return nil
    }

    // MARK: - the reads

    struct Served { var count = 0; var finished = false; var lost = false }

    /// The fast install's word (FlashProtocol.fastHello) -> true when the loader answers it; an older loader ignores it.
    func fastHello(_ c: FlashChannel) async -> Bool {
        c.drain()
        guard c.send(FlashProtocol.fastHello) else { return false }
        let end = Date().addingTimeInterval(timing.fastWord)
        while Date() < end {
            guard let p = await c.read(timeout: end.timeIntervalSinceNow) else { return false }
            if FlashProtocol.fastReply(p) == FlashProtocol.fastChunk { return true }
        }
        return false
    }

    /// Answers read requests until the device asks for `finish`; stops early when the port disappears. `fast`: 4 KiB
    /// reads and the table, no pause before an answer (the fast install); else today's protocol.
    /// bar: the fast loader's check (its 0x32 frames, else the estimate) -> onCheck(percent) until the first read, which
    /// also reports at once (the sectors before it are right). onKept: today's protocol until a 4 KiB or table read comes
    /// (a loader still in its fast session over a replug: it never heard the new hello), then the fast way, onKept() once.
    func serve(_ c: FlashChannel, _ image: [UInt8], finish: UInt32, idle: TimeInterval, fast: Bool = false,
               bar: FlashCheckBar? = nil, onCheck: (Int) -> Void = { _ in }, onKept: (() -> Void)? = nil,
               progress: (Int, UInt32) -> Void) async throws -> Served {
        var s = Served()
        var last = Date()
        var fast = fast, bar = bar, onKept = onKept
        while true {
            let pkt = await c.read(timeout: bar.map { $0.asked ? 1 : timing.checkTick } ?? 1)
            if let p = bar?.tick() { onCheck(p) }
            guard let pkt else {
                if c.lost { s.lost = true; return s }
                if Date().timeIntervalSince(last) > idle { return s }
                continue
            }
            guard let r = FlashProtocol.request(pkt) else {
                if let k = FlashProtocol.checkProgress(pkt), let p = bar?.frame(k.checked, k.total) { onCheck(p) }
                continue
            }
            if let kept = onKept, r.length > FlashProtocol.maxData || (r.address >= FlashProtocol.table && r.address < FlashProtocol.step1Done) {
                fast = true
                onKept = nil
                kept()
            }
            if r.address < FlashProtocol.table, bar?.asked == false {
                bar?.request()
                progress(s.count, r.address)
            }
            last = Date()
            if r.address == FlashProtocol.step1Done || r.address == FlashProtocol.step2Done {
                _ = c.send(FlashProtocol.success(r.address))
                if r.address == finish { s.finished = true; return s }
                continue
            }
            guard let data = FlashProtocol.answer(image, r, maxData: fast ? FlashProtocol.fastChunk : FlashProtocol.maxData) else {
                throw FlashError(code: .badRequest, description: "The FM-1 asked for bytes outside the package (\(String(r.address, radix: 16))+\(r.length)).")
            }
            if !fast { await pause(timing.perRead) }
            guard c.send(FlashProtocol.response(r.address, data, flags: r.flags)) else { s.lost = true; return s }
            s.count += 1
            if r.address < FlashProtocol.table { progress(s.count, r.address) }
        }
    }

    // MARK: - install

    /// Running firmware -> loader -> new firmware. Returns the identity the FM-1 reports at the end.
    func install(_ pkg: FlashPackage, step: @escaping (FlashStep) -> Void) async throws -> String {
        if let ota = await find({ $0.isLoader }) { return try await resume(ota, pkg, step: step) }
        guard let dev = await find({ !$0.isLoader }) else {
            throw FlashError(code: .notFound, description: "FM-1 not found: check the USB cable, and that no other app is using it.")
        }
        let model = pkg.product.split(separator: "_").first.map(String.init) ?? ""
        guard dev.id.model == model else {
            dev.channel.close()
            throw FlashError(code: .model, description: "The device is \(dev.id.text), the package is for \(pkg.product).")
        }
        phase = .check
        step(.start(dev.id.text))
        _ = dev.channel.send(FlashProtocol.midiStop)         // the transport stops first (the firmware does it too)
        await pause(timing.afterStop)
        _ = dev.channel.send(FlashProtocol.upgrade)
        await pause(timing.afterUpgrade)
        let s1 = try await serve(dev.channel, pkg.image, finish: FlashProtocol.step1Done, idle: timing.step1Idle) { n, _ in step(.verify(n)) }
        dev.channel.close()
        guard s1.finished else {
            throw FlashError(code: s1.lost ? .lost : .stopped,
                             description: "The FM-1 \(s1.lost ? "was disconnected" : "stopped") after \(s1.count) requests: nothing was written.")
        }
        step(.loader)
        // (no blind wait: the loader looked for every loaderPoll within the time as before; the running firmware that still
        // answers before it restarts is not the loader)
        guard let ota = await waitFor({ $0.isLoader }, timing.beforeLoader + timing.loaderWait, every: timing.loaderPoll) else {
            throw FlashError(code: .noLoader, description: "The update loader did not appear. Replug the USB cable and press Install again: the FM-1 stays in update mode until the install is finished.")
        }
        return try await resume(ota, pkg, step: step)
    }

    /// install(), surviving a dropped cable: when the connection drops (.lost), onDrop(error) says so, then the FM-1 is looked
    /// for every resumePoll by itself, with no time limit, until cancelled() (Cancel: .cancelled). Found again, FlashResume.plan says what
    /// it is: our loader -> step 2 again (the sectors left), the same running firmware after step 1 -> step 1 again, the package
    /// running after step 2 -> done; anything else stops with nothing more sent. running: the firmware before step 1 ("" when the
    /// FM-1 was in update mode already).
    func installResumable(_ pkg: FlashPackage, running: String, step: @escaping (FlashStep) -> Void,
                          onDrop: @escaping (FlashError) -> Void, cancelled: @escaping () -> Bool) async throws -> String {
        while true {
            let err: FlashError
            do { return try await install(pkg, step: step) } catch let e as FlashError where e.code == .lost { err = e }
            let ph = phase ?? .write
            onDrop(err)
            var found: Found?
            var plan = FlashResume.Plan.absent
            while true {
                if cancelled() || Task.isCancelled {
                    throw FlashError(code: .cancelled, description: ph == .write
                        ? "Cancelled. Nothing more was sent. The FM-1 stays in update mode: keep it on and press Install again to finish."
                        : "Cancelled. Nothing was written.")
                }
                found = await find { _ in true }
                found?.channel.close()
                plan = FlashResume.plan(ph, found?.id, product: pkg.product, running: running)
                if plan != .absent { break }
                await pause(timing.resumePoll)
            }
            guard let id = found?.id else { continue }
            switch plan {
            case .done:
                step(.done(id.text))
                return id.text
            case .foreign:
                throw FlashError(code: .foreign, description: "The FM-1 came back in the update mode of another firmware (\(id.text)): finish that update with its own updater.")
            case .unexpected:
                throw FlashError(code: .unexpected, description: "The FM-1 came back as \(id.text), not the one being updated: nothing more was sent. Check which FM-1 this is, then press Install again.")
            default:
                step(.found(id.text))     // (install() again: it takes the loader first, else the running firmware from step 1)
            }
        }
    }

    /// Step 2 on a loader already found, then the check of the firmware that comes back.
    private func resume(_ ota: Found, _ pkg: FlashPackage, step: @escaping (FlashStep) -> Void) async throws -> String {
        guard ota.id.isOurLoader else {
            ota.channel.close()
            throw FlashError(code: .foreign, description: "The FM-1 is in the update mode of another firmware (\(ota.id.text)): finish that update with its own updater.")
        }
        phase = .write
        let fast = await fastHello(ota.channel)
        _ = ota.channel.send(FlashProtocol.upgrade)
        await pause(fast ? timing.afterUpgradeFast : timing.afterUpgrade)
        let bar = fast ? FlashCheckBar(sectors: pkg.image.count / FlashProtocol.sector, framesWait: timing.checkFrames, sectorCheck: timing.sectorCheck) : nil
        if fast { step(.checking(0)) }
        let s2 = try await serve(ota.channel, pkg.image, finish: FlashProtocol.step2Done, idle: timing.step2Idle, fast: fast, bar: bar,
                                 onCheck: { step(.checking($0)) }, onKept: fast ? nil : { step(.kept) }) { n, a in step(.write(n, a)) }
        ota.channel.close()
        guard s2.finished else {
            throw FlashError(code: s2.lost ? .lost : .stopped,
                             description: "The loader \(s2.lost ? "was disconnected" : "stopped") after \(s2.count) requests. Replug the USB cable and press Install again to resume.")
        }
        step(.reboot)
        await pause(timing.beforeReturn)
        guard let back = await waitFor({ !$0.isLoader }, timing.returnWait) else {
            throw FlashError(code: .noReturn, description: "The FM-1 did not come back: power-cycle it.")
        }
        back.channel.close()
        guard back.id.text == pkg.product else {
            throw FlashError(code: .mismatch, description: "Installed, but the FM-1 reports \(back.id.text).")
        }
        step(.done(back.id.text))
        return back.id.text
    }
}
