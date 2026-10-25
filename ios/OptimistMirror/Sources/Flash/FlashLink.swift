// SPDX-License-Identifier: GPL-3.0-only
// The update's MIDI ports: every USB MIDI port pair whose name may be the FM-1's (web/fm1ota.js find: its running firmware, or its
// update loader, which comes up as another USB device), each with a SysEx queue. Never a Bluetooth port: an update goes over
// the USB cable only (a dropped link in the middle of a write leaves the FM-1 in its loader). Its own CoreMIDI client, beside
// the mirror's (MirrorLink), which holds its requests during an update.
import CoreMIDI
import Foundation

/// One port pair carrying raw SysEx (whole F0 ... F7 messages).
protocol FlashChannel: AnyObject {
    var name: String { get }
    var lost: Bool { get }        // the port is gone (unplugged, or the device restarted)
    func send(_ bytes: [UInt8]) -> Bool
    func drain()
    func read(timeout: TimeInterval) async -> [UInt8]?
    func close()
}

/// The ports an update may talk to, opened afresh on every call.
protocol FlashPorts {
    func channels() -> [FlashChannel]
}

/// The SysEx messages received and not read yet, with one reader waiting at most.
final class SysExQueue: @unchecked Sendable {
    private let lock = NSLock()
    private var queue: [[UInt8]] = []
    private var waiter: (token: Int, c: CheckedContinuation<[UInt8]?, Never>)?
    private var token = 0

    func push(_ m: [UInt8]) {
        lock.lock()
        if let w = waiter {
            waiter = nil
            lock.unlock()
            w.c.resume(returning: m)
            return
        }
        queue.append(m)
        lock.unlock()
    }

    func drain() { lock.lock(); queue.removeAll(); lock.unlock() }

    func read(timeout: TimeInterval) async -> [UInt8]? {
        await withCheckedContinuation { (c: CheckedContinuation<[UInt8]?, Never>) in
            lock.lock()
            if !queue.isEmpty {
                let m = queue.removeFirst()
                lock.unlock()
                c.resume(returning: m)
                return
            }
            token += 1
            let t = token
            waiter = (t, c)
            lock.unlock()
            DispatchQueue.global().asyncAfter(deadline: .now() + max(0, timeout)) { [weak self] in self?.expire(t) }
        }
    }

    private func expire(_ t: Int) {
        lock.lock()
        guard let w = waiter, w.token == t else { lock.unlock(); return }
        waiter = nil
        lock.unlock()
        w.c.resume(returning: nil)
    }
}

final class CoreMIDIFlashPorts: FlashPorts {
    private var client = MIDIClientRef()
    private var outPort = MIDIPortRef()

    init() {
        MIDIClientCreateWithBlock("Optimist Mirror update" as CFString, &client) { _ in }
        MIDIOutputPortCreate(client, "update out" as CFString, &outPort)
    }

    deinit { MIDIClientDispose(client) }

    /// a name the FM-1 may have (its firmware's, its loader's, the stock one's), never other gear (web/fm1ota.js find)
    static func mayBeFM1(_ name: String) -> Bool {
        name.range(of: "fm-1|felucca|optimist|ota|composite|sinco|usb-midi", options: [.regularExpression, .caseInsensitive]) != nil
            && !name.lowercased().contains("bluetooth")
    }

    func channels() -> [FlashChannel] {
        let dests = (0..<MIDIGetNumberOfDestinations()).map(MIDIGetDestination)
        var out: [FlashChannel] = []
        for src in (0..<MIDIGetNumberOfSources()).map(MIDIGetSource) {
            let name = MirrorLink.name(src)
            guard mayBe(src, name) else { continue }
            guard let dst = dests.first(where: { MirrorLink.name($0) == name }) ?? (dests.count == 1 ? dests[0] : nil) else { continue }
            if let c = CoreMIDIChannel(client: client, outPort: outPort, source: src, destination: dst, name: name) { out.append(c) }
        }
        return out
    }

    private func mayBe(_ e: MIDIEndpointRef, _ name: String) -> Bool {
        var off: Int32 = 0
        MIDIObjectGetIntegerProperty(e, kMIDIPropertyOffline, &off)
        return off == 0 && Self.mayBeFM1(name) && !MirrorLink.isBluetooth(e)
    }
}

final class CoreMIDIChannel: FlashChannel {
    let name: String
    private let outPort: MIDIPortRef
    private var inPort = MIDIPortRef()
    private let sourceID: MIDIUniqueID
    private let destinationID: MIDIUniqueID
    private let queue = SysExQueue()
    private var assembler = SysExAssembler()        // (CoreMIDI's thread only)

    init?(client: MIDIClientRef, outPort: MIDIPortRef, source: MIDIEndpointRef, destination: MIDIEndpointRef, name: String) {
        self.name = name
        self.outPort = outPort
        var s: MIDIUniqueID = 0, d: MIDIUniqueID = 0
        MIDIObjectGetIntegerProperty(source, kMIDIPropertyUniqueID, &s)
        MIDIObjectGetIntegerProperty(destination, kMIDIPropertyUniqueID, &d)
        sourceID = s; destinationID = d
        let q = queue
        let status = MIDIInputPortCreateWithProtocol(client, "update in" as CFString, ._1_0, &inPort) { [weak self] list, _ in
            self?.receive(list, q)
        }
        guard status == noErr, MIDIPortConnectSource(inPort, source, nil) == noErr else { return nil }
    }

    deinit { close() }

    private static func endpoint(_ id: MIDIUniqueID) -> MIDIEndpointRef? {
        var obj = MIDIObjectRef(), type = MIDIObjectType.other
        guard MIDIObjectFindByUniqueID(id, &obj, &type) == noErr else { return nil }
        var off: Int32 = 0
        MIDIObjectGetIntegerProperty(obj, kMIDIPropertyOffline, &off)
        return off == 0 ? obj : nil
    }

    var lost: Bool { Self.endpoint(sourceID) == nil || Self.endpoint(destinationID) == nil }

    func send(_ bytes: [UInt8]) -> Bool {
        guard bytes.count >= 2, let dst = Self.endpoint(destinationID) else { return false }
        let words = SysExAssembler.packets(Array(bytes[1..<(bytes.count - 1)]))
        let size = 64 + words.count / 2 * 32
        let raw = UnsafeMutableRawPointer.allocate(byteCount: size, alignment: 8)
        defer { raw.deallocate() }
        let list = raw.bindMemory(to: MIDIEventList.self, capacity: 1)
        var packet = MIDIEventListInit(list, ._1_0)
        words.withUnsafeBufferPointer { w in
            for k in stride(from: 0, to: w.count, by: 2) {
                packet = MIDIEventListAdd(list, size, packet, 0, 2, w.baseAddress! + k)
            }
        }
        return MIDISendEventList(outPort, dst, list) == noErr
    }

    func drain() { queue.drain() }
    func read(timeout: TimeInterval) async -> [UInt8]? { await queue.read(timeout: timeout) }

    func close() {
        if inPort != 0 { MIDIPortDispose(inPort) }
        inPort = 0
    }

    private func receive(_ list: UnsafePointer<MIDIEventList>, _ q: SysExQueue) {
        let offset = MemoryLayout<MIDIEventPacket>.offset(of: \MIDIEventPacket.words)!
        for packet in list.unsafeSequence() {
            let count = Int(packet.pointee.wordCount)
            let words = (UnsafeRawPointer(packet) + offset).assumingMemoryBound(to: UInt32.self)
            var k = 0
            while k < count {
                let type = words[k] >> 28
                let size = [1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4][Int(type)]
                if type == 3, k + 1 < count, let f = assembler.take(words[k], words[k + 1]) { q.push(f) }
                k += size
            }
        }
    }
}
