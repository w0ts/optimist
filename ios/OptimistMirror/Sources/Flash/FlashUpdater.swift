// SPDX-License-Identifier: GPL-3.0-only
// An install, after web/fm1ota.js Updater (install, write, resume): the running firmware stages the update loader (step 1, where
// nothing is written yet), the loader writes the image (step 2), the new firmware comes back and must report the package's
// identity. A device found in our loader already (an install that broke off) is finished from step 2.
import Foundation

struct FlashError: Error, Equatable, CustomStringConvertible {
    enum Code: String { case notFound, model, stopped, noLoader, lost, noReturn, mismatch, badRequest, foreign }
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
}

final class FlashUpdater {
    private let ports: FlashPorts
    private let pause: (TimeInterval) async -> Void
    /// the waits of the protocol (the web installer's); tests shrink them
    struct Timing {
        var afterUpgrade: TimeInterval = 2
        var beforeLoader: TimeInterval = 3
        var loaderWait: TimeInterval = 30
        var beforeReturn: TimeInterval = 3
        var returnWait: TimeInterval = 40
        var poll: TimeInterval = 1
        var step1Idle: TimeInterval = 8
        var step2Idle: TimeInterval = 180
        var perRead: TimeInterval = 0.01
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
            let end = Date().addingTimeInterval(1)
            while Date() < end {
                guard let p = await c.read(timeout: end.timeIntervalSinceNow) else { break }
                if let id = FlashProtocol.identity(p) { return id }
            }
        }
        return nil
    }

    /// The first port that answers the handshake with an identity `filter` takes.
    func find(_ filter: ((FlashProtocol.Identity) -> Bool)? = nil) async -> Found? {
        var found: Found?
        for c in ports.channels() {
            if found == nil, let id = await handshake(c, tries: 2), filter?(id) ?? true {
                found = Found(channel: c, id: id)
            } else {
                c.close()
            }
        }
        return found
    }

    private func waitFor(_ filter: @escaping (FlashProtocol.Identity) -> Bool, _ seconds: TimeInterval) async -> Found? {
        let end = Date().addingTimeInterval(seconds)
        while Date() < end {
            if let f = await find(filter) { return f }
            if Task.isCancelled { return nil }
            await pause(timing.poll)
        }
        return nil
    }

    // MARK: - the reads

    struct Served { var count = 0; var finished = false; var lost = false }

    /// Answers read requests until the device asks for `finish`; stops early when the port disappears.
    func serve(_ c: FlashChannel, _ image: [UInt8], finish: UInt32, idle: TimeInterval, progress: (Int, UInt32) -> Void) async throws -> Served {
        var s = Served()
        var last = Date()
        while true {
            guard let pkt = await c.read(timeout: 1) else {
                if c.lost { s.lost = true; return s }
                if Date().timeIntervalSince(last) > idle { return s }
                continue
            }
            guard let r = FlashProtocol.request(pkt) else { continue }
            last = Date()
            if r.address == FlashProtocol.step1Done || r.address == FlashProtocol.step2Done {
                _ = c.send(FlashProtocol.success(r.address))
                if r.address == finish { s.finished = true; return s }
                continue
            }
            guard r.length <= FlashProtocol.maxData, Int(r.address) + r.length <= image.count else {
                throw FlashError(code: .badRequest, description: "The FM-1 asked for bytes outside the package (\(String(r.address, radix: 16))+\(r.length)).")
            }
            await pause(timing.perRead)
            let start = Int(r.address)
            guard c.send(FlashProtocol.response(r.address, image[start..<(start + r.length)], flags: r.flags)) else { s.lost = true; return s }
            s.count += 1
            progress(s.count, r.address)
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
        step(.start(dev.id.text))
        _ = dev.channel.send(FlashProtocol.upgrade)
        await pause(timing.afterUpgrade)
        let s1 = try await serve(dev.channel, pkg.image, finish: FlashProtocol.step1Done, idle: timing.step1Idle) { n, _ in step(.verify(n)) }
        dev.channel.close()
        guard s1.finished else {
            throw FlashError(code: s1.lost ? .lost : .stopped,
                             description: "The FM-1 \(s1.lost ? "was disconnected" : "stopped") after \(s1.count) requests: nothing was written.")
        }
        step(.loader)
        await pause(timing.beforeLoader)
        guard let ota = await waitFor({ $0.isLoader }, timing.loaderWait) else {
            throw FlashError(code: .noLoader, description: "The update loader did not appear. Replug the USB cable and press Install again: the FM-1 stays in update mode until the install is finished.")
        }
        return try await resume(ota, pkg, step: step)
    }

    /// Step 2 on a loader already found, then the check of the firmware that comes back.
    private func resume(_ ota: Found, _ pkg: FlashPackage, step: @escaping (FlashStep) -> Void) async throws -> String {
        guard ota.id.isOurLoader else {
            ota.channel.close()
            throw FlashError(code: .foreign, description: "The FM-1 is in the update mode of another firmware (\(ota.id.text)): finish that update with its own updater.")
        }
        _ = ota.channel.send(FlashProtocol.upgrade)
        await pause(timing.afterUpgrade)
        let s2 = try await serve(ota.channel, pkg.image, finish: FlashProtocol.step2Done, idle: timing.step2Idle) { n, a in step(.write(n, a)) }
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
