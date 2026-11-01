// SPDX-License-Identifier: GPL-3.0-only
// The screen mirror over Bluetooth measured from a Mac (CoreBluetooth central, read-only: it asks 106 MIRROR as the
// app does and nothing else): the link the FM-1 agreed (op 3 LINK: data length, ATT MTU, interval), the SysEx bytes
// and frames a second, the notifications' sizes and gaps. Every 2 s it asks for the whole panel again (bit 1), so the
// link runs full and the rate is the link's. Nothing is paired, nothing is written to the FM-1's flash.
//   swiftc -O tools/mirror_ble_probe.swift -o worktrees/mirror_ble_probe && worktrees/mirror_ble_probe [seconds]
// The FM-1 must be advertising (no iPad or other central connected to it). macOS asks once for Bluetooth access for
// the terminal. The Mac's own central gets 30 ms, DLE 251, MTU 247 (bluetoothd, 2026-10-28); an iPad's MIDI sheet
// gets other values, which the app logs (subsystem com.w0ts.optimist.mirror).
import CoreBluetooth
import Foundation

let midiService = CBUUID(string: "03B80E5A-EDE8-4B33-A751-6CE34EC4C700")
let midiChar = CBUUID(string: "7772E5DB-3868-4112-A1A9-F2669D106BF3")
let seconds = CommandLine.arguments.count > 1 ? Double(CommandLine.arguments[1]) ?? 20 : 20

func now() -> Double { Date().timeIntervalSince1970 }

final class Probe: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var cm: CBCentralManager!
    var p: CBPeripheral?
    var ch: CBCharacteristic?
    var t0 = 0.0
    var sx: [UInt8] = []
    var inSx = false
    var notes = 0, noteBytes = 0, maxNote = 0
    var noteTimes: [Double] = []
    var frames = 0, frameBytes = 0
    var ops = [Int: Int]()
    var perSec = [Int: (bytes: Int, frames: Int)]()

    override init() {
        super.init()
        cm = CBCentralManager(delegate: self, queue: nil)
    }
    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        switch c.state {
        case .poweredOn: c.scanForPeripherals(withServices: nil, options: nil)
        case .unauthorized, .unsupported: print("no Bluetooth access"); exit(2)
        default: break
        }
    }
    func centralManager(_ c: CBCentralManager, didDiscover per: CBPeripheral, advertisementData: [String: Any], rssi: NSNumber) {
        let name = per.name ?? (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? ""
        guard name.hasPrefix("FM1-") || name.hasPrefix("Optimist") else { return }   // (FM1-XXXX since 2026-10-29; Optimist: older firmware)
        print("found \(name), rssi \(rssi)")
        c.stopScan()
        p = per
        per.delegate = self
        c.connect(per, options: nil)
    }
    func centralManager(_ c: CBCentralManager, didConnect per: CBPeripheral) { per.discoverServices([midiService]) }
    func centralManager(_ c: CBCentralManager, didFailToConnect per: CBPeripheral, error: Error?) {
        print("connect failed: \(String(describing: error))"); exit(3)
    }
    func peripheral(_ per: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = per.services?.first(where: { $0.uuid == midiService }) else { print("no MIDI service"); exit(4) }
        per.discoverCharacteristics([midiChar], for: s)
    }
    func peripheral(_ per: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        guard let c = s.characteristics?.first(where: { $0.uuid == midiChar }) else { print("no MIDI I/O"); exit(5) }
        ch = c
        per.setNotifyValue(true, for: c)
    }
    func peripheral(_ per: CBPeripheral, didUpdateNotificationStateFor c: CBCharacteristic, error: Error?) {
        t0 = now()
        ask(1)
        Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { _ in self.ask(1) }   // (the keep-alive)
        Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { _ in self.ask(3) }   // (the whole panel again)
        Timer.scheduledTimer(withTimeInterval: seconds, repeats: false) { _ in self.finish() }
    }
    func ask(_ bits: UInt8) {               // F0 7D 46 4C 6A bits F7 in one BLE-MIDI packet
        guard let p = p, let c = ch else { return }
        p.writeValue(Data([0x80, 0x80, 0xF0, 0x7D, 0x46, 0x4C, 0x6A, bits, 0x80, 0xF7]), for: c, type: .withoutResponse)
    }
    func peripheral(_ per: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
        guard let d = c.value, d.count > 1 else { return }
        let t = now(), b = [UInt8](d)
        notes += 1; noteBytes += b.count; maxNote = max(maxNote, b.count); noteTimes.append(t)
        var i = 1                           // (after the header: a timestamp before each status; SysEx data plain)
        while i < b.count {
            let x = b[i]
            if x & 0x80 != 0 {
                if i + 1 < b.count && b[i + 1] & 0x80 != 0 {
                    if b[i + 1] == 0xF0 { sx = [0xF0]; inSx = true }
                    else if b[i + 1] == 0xF7 { if inSx { sx.append(0xF7); frame(sx, t) }; inSx = false }
                    i += 2
                } else {
                    i += 1
                }
                continue
            }
            if inSx { sx.append(x) }
            i += 1
        }
    }
    func frame(_ f: [UInt8], _ t: Double) {
        frames += 1; frameBytes += f.count
        let s = Int(t - t0), e = perSec[s] ?? (0, 0)
        perSec[s] = (e.bytes + f.count, e.frames + 1)
        guard f.count >= 7, f[1] == 0x7D, f[4] == 0x6A else { return }
        ops[Int(f[5]), default: 0] += 1
        if f[5] == 3 && f.count >= 21 {
            let v = (0..<7).map { Int(f[6 + 2 * $0]) | Int(f[7 + 2 * $0]) << 7 }
            print(String(format: "LINK data length %d, MTU %d, interval %d (%.2f ms); central's LL_LENGTH rx %d/%d us, tx %d/%d us",
                         v[0], v[1], v[2], Double(v[2]) * 1.25, v[3], v[4], v[5], v[6]))
        }
    }
    func finish() {
        let dur = now() - t0
        print(String(format: "%.1f s: %d notifications (%d bytes, largest %d), %d frames, %d SysEx bytes: %.0f B/s, %.1f frames/s",
                     dur, notes, noteBytes, maxNote, frames, frameBytes, Double(frameBytes) / dur, Double(frames) / dur))
        print("frames by op (0 FILL, 1 BLIT, 2 MORE, 3 LINK, 4 STATE): \(ops.sorted { $0.key < $1.key }.map { "\($0.key):\($0.value)" }.joined(separator: " "))")
        for s in perSec.keys.sorted() { print("  second \(s): \(perSec[s]!.bytes) B, \(perSec[s]!.frames) frames") }
        let gaps = zip(noteTimes.dropFirst(), noteTimes).map { ($0 - $1) * 1000 }.sorted()
        if !gaps.isEmpty {
            let near = gaps.filter { $0 < 5 }.count
            print(String(format: "gaps between notifications (ms): p10 %.1f p50 %.1f p90 %.1f max %.1f; %d of %d under 5 ms (the same event)",
                         gaps[gaps.count / 10], gaps[gaps.count / 2], gaps[gaps.count * 9 / 10], gaps.last!, near, gaps.count))
        }
        if let p = p { cm.cancelPeripheralConnection(p) }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { exit(0) }
    }
}

let probe = Probe()
RunLoop.main.run()
