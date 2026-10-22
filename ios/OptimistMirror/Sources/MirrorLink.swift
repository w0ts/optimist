// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 over CoreMIDI (its USB port: "Felucca" / "Optimist", never its Bluetooth one, which does not answer the editor
// protocol): asks for the mirror (cmd 106) every second, takes the panel writes it pushes into a MirrorDecoder.
import CoreMIDI
import Foundation
import os

final class MirrorLink: ObservableObject {
    enum State: Equatable {
        case searching            // no FM-1 port
        case asking               // a port, no reply yet
        case noMirror             // the FM-1 answers nothing to 106: a build without ED_MIRROR
        case mirroring
    }

    @Published private(set) var state: State = .searching
    @Published private(set) var portName = ""

    private let lock = OSAllocatedUnfairLock(initialState: Shared())
    private struct Shared {
        var decoder = MirrorDecoder()
        var assembler = SysExAssembler()
        var generation = 0        // panel writes applied so far
        var replies = 0
    }

    private var client = MIDIClientRef()
    private var inPort = MIDIPortRef()
    private var outPort = MIDIPortRef()
    private var source = MIDIEndpointRef()
    private var destination = MIDIEndpointRef()
    private var timer: Timer?
    private var asked = 0         // requests since the last reply
    private var wantWhole = true

    private static let keepAlive: TimeInterval = 1.0
    private static let unanswered = 3   // requests without a reply: no mirror in this build

    init() {
        MIDIClientCreateWithBlock("Optimist Mirror" as CFString, &client) { [weak self] note in
            if note.pointee.messageID == .msgSetupChanged {
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
        lock.withLock { s in s.generation == seen ? nil : (s.decoder.pixels, s.generation) }
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
        return (n.contains("felucca") || n.contains("optimist")) && !n.contains("bluetooth")
    }

    private static func find(_ count: Int, _ get: (Int) -> MIDIEndpointRef) -> MIDIEndpointRef? {
        (0..<count).map(get).first(where: isFM1)
    }

    private func connect() {
        let src = Self.find(MIDIGetNumberOfSources(), MIDIGetSource)
        let dst = Self.find(MIDIGetNumberOfDestinations(), MIDIGetDestination)
        guard src != source || dst != destination else { return }
        if source != 0 { MIDIPortDisconnectSource(inPort, source) }
        source = src ?? 0
        destination = dst ?? 0
        lock.withLock { $0.assembler = SysExAssembler() }
        guard let src, dst != nil else {
            portName = ""
            state = .searching
            return
        }
        MIDIPortConnectSource(inPort, src, nil)
        portName = Self.name(src)
        state = .asking
        asked = 0
        wantWhole = true
        tick()
    }

    // MARK: - requests

    private func tick() {
        guard destination != 0 else { return }
        if asked >= Self.unanswered, state != .mirroring { state = .noMirror }
        send([MirrorDecoder.cmd, wantWhole ? 3 : 1])
        wantWhole = false
        asked += 1
    }

    private func send(_ body: [UInt8]) {
        guard destination != 0 else { return }
        let words = SysExAssembler.packets([0x7D, 0x46, 0x4C] + body)
        var list = MIDIEventList()
        var packet = MIDIEventListInit(&list, ._1_0)
        words.withUnsafeBufferPointer { w in
            for k in stride(from: 0, to: w.count, by: 2) {
                packet = MIDIEventListAdd(&list, MemoryLayout<MIDIEventList>.size, packet, 0, 2, w.baseAddress! + k)
            }
        }
        MIDISendEventList(outPort, destination, &list)
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
                    if let f = lock.withLock({ $0.assembler.take(words[k], words[k + 1]) }) { frames.append(f) }
                }
                k += size
            }
        }
        guard !frames.isEmpty else { return }
        var replied = false
        lock.withLock { s in
            for f in frames {
                if let r = MirrorDecoder.reply(f) {
                    replied = replied || r.on
                    s.replies += 1
                } else if s.decoder.apply(f) {
                    s.generation += 1
                }
            }
        }
        if replied {
            DispatchQueue.main.async {
                self.asked = 0
                self.state = .mirroring
            }
        }
    }
}
