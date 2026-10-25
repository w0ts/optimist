// SPDX-License-Identifier: GPL-3.0-only
// Web MIDI for the web editor inside the app (the user, 2026-10-25: the web's other tabs in the app, "web + MIDI bridge"):
// WKWebView has no navigator.requestMIDIAccess, so the page gets a stand-in (shim, put in before the page's own scripts)
// that talks to this bridge: the app's CoreMIDI ports (USB and Bluetooth) as MIDIInput / MIDIOutput, bytes both ways.
// Only what the pages use (web/editor.html, fm1ota.js, installer.js, sim.js): requestMIDIAccess, inputs / outputs and their
// values(), a port's id name state connection open() close() onmidimessage send(), access.onstatechange, an event's data
// and timeStamp.
import CoreMIDI
import Foundation
import WebKit

final class WebMIDIBridge: NSObject, WKScriptMessageHandler {
    static let handler = "optimistMidi"
    private var client = MIDIClientRef()
    private var inPort = MIDIPortRef()
    private var outPort = MIDIPortRef()
    private var assemblers: [MIDIUniqueID: SysExAssembler] = [:]   // (CoreMIDI's thread only)
    weak var webView: WKWebView?
    /// the one port the page may see: the app's own connection (MirrorLink's; the user, 2026-10-25: "the connection should
    /// be fully shared across tabs"); "": none (the app disconnected: the page loses its port too). Over Bluetooth its name
    /// carries "Bluetooth" (the editor tells the links apart by the name, isBtPort)
    var allowed = "" { didSet { if allowed != oldValue { pushPorts() } } }
    var allowedBluetooth = false

    override init() {
        super.init()
        MIDIClientCreateWithBlock("Optimist Mirror web" as CFString, &client) { [weak self] note in
            if note.pointee.messageID == .msgSetupChanged || note.pointee.messageID == .msgPropertyChanged {
                DispatchQueue.main.async { self?.pushPorts() }
            }
        }
        MIDIInputPortCreateWithProtocol(client, "web in" as CFString, ._1_0, &inPort) { [weak self] list, src in
            self?.receive(list, from: src.map { MIDIUniqueID(Int(bitPattern: $0)) } ?? 0)
        }
        MIDIOutputPortCreate(client, "web out" as CFString, &outPort)
        connectAll()
    }

    deinit { MIDIClientDispose(client) }

    // MARK: - ports

    private struct Port: Encodable { let id: String; let name: String; let type: String }

    private static func uid(_ e: MIDIEndpointRef) -> MIDIUniqueID {
        var u: MIDIUniqueID = 0
        MIDIObjectGetIntegerProperty(e, kMIDIPropertyUniqueID, &u)
        return u
    }

    private static func online(_ e: MIDIEndpointRef) -> Bool {
        var off: Int32 = 0
        MIDIObjectGetIntegerProperty(e, kMIDIPropertyOffline, &off)
        return off == 0
    }

    private var connected = Set<MIDIUniqueID>()
    /// every source connected once, its unique id as the connection's reference (the source of a packet)
    private func connectAll() {
        for e in (0..<MIDIGetNumberOfSources()).map(MIDIGetSource) {
            let u = Self.uid(e)
            guard !connected.contains(u) else { continue }
            if MIDIPortConnectSource(inPort, e, UnsafeMutableRawPointer(bitPattern: Int(u))) == noErr { connected.insert(u) }
        }
    }

    private func ports() -> [Port] {
        guard !allowed.isEmpty else { return [] }
        let shown = { (n: String) in self.allowedBluetooth && !n.lowercased().contains("bluetooth") ? "\(n) Bluetooth" : n }
        let ins = (0..<MIDIGetNumberOfSources()).map(MIDIGetSource).filter { Self.online($0) && MirrorLink.name($0) == self.allowed }
            .map { Port(id: "i\(Self.uid($0))", name: shown(MirrorLink.name($0)), type: "input") }
        let outs = (0..<MIDIGetNumberOfDestinations()).map(MIDIGetDestination).filter { Self.online($0) && MirrorLink.name($0) == self.allowed }
            .map { Port(id: "o\(Self.uid($0))", name: shown(MirrorLink.name($0)), type: "output") }
        return ins + outs
    }

    /// the page's port list, again (a device plugged in or out): its statechange
    func pushPorts() {
        connectAll()
        guard let data = try? JSONEncoder().encode(ports()), let json = String(data: data, encoding: .utf8) else { return }
        webView?.evaluateJavaScript("window.__optimistMidi && window.__optimistMidi.ports(\(json))")
    }

    // MARK: - the page -> a port

    func userContentController(_ c: WKUserContentController, didReceive m: WKScriptMessage) {
        guard let body = m.body as? [String: Any], let op = body["op"] as? String else { return }
        switch op {
        case "ports":
            pushPorts()
        case "send":
            guard let id = body["port"] as? String, id.hasPrefix("o"), let u = MIDIUniqueID(id.dropFirst()),
                  ports().contains(where: { $0.id == id }), let raw = body["data"] as? [Any] else { return }   // (only the app's port)
            let bytes = raw.compactMap { ($0 as? NSNumber).map { UInt8(truncatingIfNeeded: $0.intValue) } }
            send(bytes, to: u)
        default:
            break
        }
    }

    private func send(_ bytes: [UInt8], to u: MIDIUniqueID) {
        var obj = MIDIObjectRef(), type = MIDIObjectType.other
        guard MIDIObjectFindByUniqueID(u, &obj, &type) == noErr else { return }
        let words = Self.ump(bytes)
        guard !words.isEmpty else { return }
        let size = 64 + words.count * 8
        let raw = UnsafeMutableRawPointer.allocate(byteCount: size, alignment: 8)
        defer { raw.deallocate() }
        let list = raw.bindMemory(to: MIDIEventList.self, capacity: 1)
        var packet = MIDIEventListInit(list, ._1_0)
        var k = 0
        words.withUnsafeBufferPointer { w in
            while k < w.count {
                let n = Int(w[k] >> 28) == 3 ? 2 : 1
                packet = MIDIEventListAdd(list, size, packet, 0, n, w.baseAddress! + k)
                k += n
            }
        }
        MIDISendEventList(outPort, obj, list)
    }

    /// MIDI 1.0 bytes (one or more messages) as Universal MIDI Packets: SysEx as data 64 (type 3), the channel voice
    /// messages as type 2, the system ones as type 1
    static func ump(_ b: [UInt8]) -> [UInt32] {
        var out: [UInt32] = [], i = 0
        while i < b.count {
            let s = b[i]
            if s == 0xF0 {
                guard let end = b[i...].firstIndex(of: 0xF7) else { break }
                out += SysExAssembler.packets(Array(b[(i + 1)..<end]))
                i = end + 1
            } else if s >= 0xF8 || s == 0xF6 {
                out.append(0x1000_0000 | UInt32(s) << 16); i += 1
            } else if s == 0xF1 || s == 0xF3 {
                out.append(0x1000_0000 | UInt32(s) << 16 | UInt32(i + 1 < b.count ? b[i + 1] : 0) << 8); i += 2
            } else if s == 0xF2 {
                out.append(0x1000_0000 | UInt32(s) << 16 | UInt32(i + 1 < b.count ? b[i + 1] : 0) << 8 | UInt32(i + 2 < b.count ? b[i + 2] : 0)); i += 3
            } else if s >= 0x80 {
                let two = (s & 0xF0) == 0xC0 || (s & 0xF0) == 0xD0
                let d1 = i + 1 < b.count ? b[i + 1] : 0, d2 = two || i + 2 >= b.count ? 0 : b[i + 2]
                out.append(0x2000_0000 | UInt32(s) << 16 | UInt32(d1) << 8 | UInt32(d2)); i += two ? 2 : 3
            } else {
                i += 1                      // (running status: the pages never send it)
            }
        }
        return out
    }

    // MARK: - a port -> the page

    private func receive(_ list: UnsafePointer<MIDIEventList>, from u: MIDIUniqueID) {
        var msgs: [[UInt8]] = []
        let offset = MemoryLayout<MIDIEventPacket>.offset(of: \MIDIEventPacket.words)!
        for packet in list.unsafeSequence() {
            let count = Int(packet.pointee.wordCount)
            let w = (UnsafeRawPointer(packet) + offset).assumingMemoryBound(to: UInt32.self)
            var k = 0
            while k < count {
                let type = Int(w[k] >> 28), size = [1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4][type]
                switch type {
                case 1:
                    let s = UInt8(truncatingIfNeeded: w[k] >> 16)
                    let n = s == 0xF2 ? 3 : (s == 0xF1 || s == 0xF3) ? 2 : 1
                    msgs.append(Array([s, UInt8(truncatingIfNeeded: w[k] >> 8), UInt8(truncatingIfNeeded: w[k])].prefix(n)))
                case 2:
                    let s = UInt8(truncatingIfNeeded: w[k] >> 16), two = (s & 0xF0) == 0xC0 || (s & 0xF0) == 0xD0
                    msgs.append(Array([s, UInt8(truncatingIfNeeded: w[k] >> 8), UInt8(truncatingIfNeeded: w[k])].prefix(two ? 2 : 3)))
                case 3 where k + 1 < count:
                    var a = assemblers[u] ?? SysExAssembler()
                    if let f = a.take(w[k], w[k + 1]) { msgs.append(f) }
                    assemblers[u] = a
                default:
                    break
                }
                k += size
            }
        }
        guard !msgs.isEmpty, ports().contains(where: { $0.id == "i\(u)" }), let data = try? JSONSerialization.data(withJSONObject: msgs.map { $0.map(Int.init) }),
              let json = String(data: data, encoding: .utf8) else { return }
        DispatchQueue.main.async { [weak self] in
            self?.webView?.evaluateJavaScript("window.__optimistMidi && window.__optimistMidi.recv(\"i\(u)\", \(json))")
        }
    }

    // MARK: - the page's side

    /// navigator.requestMIDIAccess for the page, before its own scripts
    static let shim = """
    (() => {
      if (navigator.requestMIDIAccess || !window.webkit || !window.webkit.messageHandlers.\(handler)) return;
      const post = (m) => window.webkit.messageHandlers.\(handler).postMessage(m);
      const ins = new Map(), outs = new Map();
      const access = { inputs: ins, outputs: outs, sysexEnabled: true, onstatechange: null, _l: [],
        addEventListener(t, f) { if (t === "statechange") this._l.push(f); }, removeEventListener(t, f) { this._l = this._l.filter((x) => x !== f); } };
      const fire = (port) => { const ev = { port }; if (access.onstatechange) access.onstatechange(ev); access._l.forEach((f) => f(ev)); };
      function port(p) {
        const o = { id: p.id, name: p.name, manufacturer: "", type: p.type, version: "", state: "connected", connection: "open", _l: [], _h: null,
          open() { return Promise.resolve(o); }, close() { return Promise.resolve(o); },
          addEventListener(t, f) { if (t === "midimessage") o._l.push(f); }, removeEventListener(t, f) { o._l = o._l.filter((x) => x !== f); },
          send(data) { post({ op: "send", port: o.id, data: Array.from(data) }); } };
        Object.defineProperty(o, "onmidimessage", { get: () => o._h, set: (f) => { o._h = f; } });
        return o;
      }
      let first = null;
      const ready = new Promise((r) => { first = r; });
      window.__optimistMidi = {
        ports(list) {
          const seen = new Set();
          for (const p of list) {
            seen.add(p.id);
            const m = p.type === "input" ? ins : outs;
            if (!m.has(p.id)) { m.set(p.id, port(p)); fire(m.get(p.id)); }
          }
          for (const m of [ins, outs]) for (const [id, o] of m) if (!seen.has(id)) { o.state = "disconnected"; m.delete(id); fire(o); }
          if (first) { first(); first = null; }
        },
        recv(id, msgs) {
          const o = ins.get(id);
          if (!o) return;
          for (const b of msgs) {
            const ev = { data: Uint8Array.from(b), timeStamp: performance.now(), target: o };
            if (o._h) o._h(ev);
            o._l.forEach((f) => f(ev));
          }
        },
      };
      navigator.requestMIDIAccess = () => { post({ op: "ports" }); return ready.then(() => access); };
    })();
    """
}
