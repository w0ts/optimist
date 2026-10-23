// SPDX-License-Identifier: GPL-3.0-only
// The preset library's files, as the web editor writes and reads them (web/editor.html libraryFile / readLibraryFile / cleanPatch /
// remapParams / paramKeys): "felucca-library" version 1 (kind library, bank or patch) and the "felucca-patch" file of the
// editor's Save to file. The app keeps its library as one such file, so what it exports is what it holds.
import Foundation

struct Patch: Identifiable {
    var id = UUID()
    var name: String
    var engine: Int
    var engineName: String
    var p: [Int?]
    var pattern: [[Int]]?
    var tags: [String]
    var created: String
    var modified: String
    var slot: Int?
    var dx7: Any?          // a DX7 voice of the web editor's library: kept as it is, only the web editor plays it
}

struct LibraryContext {
    var keys: [String]?
    var engines: [String]
    var firmware = ""
    var pe0: Int?
}

enum LibraryFile {
    static let format = "felucca-library"
    static let version = 1
    static let patSteps = 16

    // MARK: JSON helpers

    static func int(_ x: Any?) -> Int? {
        guard let n = x as? NSNumber, CFGetTypeID(n) != CFBooleanGetTypeID(), n.doubleValue.isFinite else { return nil }
        return n.doubleValue == n.doubleValue.rounded() ? n.intValue : nil
    }

    /// JavaScript's Math.round on a finite number
    private static func round(_ x: Any?) -> Int? {
        guard let n = x as? NSNumber, CFGetTypeID(n) != CFBooleanGetTypeID(), n.doubleValue.isFinite else { return nil }
        return Int((n.doubleValue + 0.5).rounded(.down))
    }

    // MARK: patches

    /// one pattern step as the firmware keeps it: a tie has no note, a rest no flags
    static func patNorm(_ n: Int, _ f: Int) -> [Int] { f & 4 != 0 ? [0, 4] : [n & 0x7F, n != 0 ? f & 3 : 0] }

    static func patternUsed(_ pat: [[Int]]?) -> Bool { pat?.contains { ($0.first ?? 0) != 0 } ?? false }

    /// sequencer steps (STEP_GET objects) -> a 16-step pattern, as the firmware's UP_STORE does
    static func patternFromSteps(_ steps: [[String: Any]]) -> [[Int]] {
        (0..<patSteps).map { i in
            guard i < steps.count else { return [0, 0] }
            let s = steps[i]
            let time = int(s["time"]) ?? 0, n = int(s["n"]) ?? 0, flags = int(s["flags"]) ?? 0
            let first = (s["notes"] as? [Any]).flatMap { int($0.first) } ?? 0
            return patNorm(time == 0 && n != 0 ? first : 0, time == 1 ? 4 : flags)
        }
    }

    /// A patch from its fields; the same clean-up as the web's cleanPatch.
    static func clean(name: Any?, engine: Int, engineName: String, p: [Any], pattern: [Any]?, tags: [Any]?, created: String?, modified: String?,
                      slot: Int?, dx7: Any?, now: String = BackupFile.isoNow()) -> Patch {
        var pat: [[Int]]?
        if let pattern, !pattern.isEmpty {
            pat = (0..<patSteps).map { i in
                if i < pattern.count, let x = pattern[i] as? [Any] { return patNorm(int(x.first) ?? 0, int(x.count > 1 ? x[1] : nil) ?? 0) }
                return [0, 0]
            }
        }
        var seen = Set<String>(), uniq: [String] = []
        for t in tags ?? [] {
            let s = "\(t)".trimmingCharacters(in: .whitespaces)
            if !s.isEmpty && seen.insert(s).inserted { uniq.append(s) }
        }
        let nm = (name == nil || name is NSNull) ? "PATCH" : "\(name!)"
        var out = Patch(name: String(nm.prefix(32)).isEmpty ? "PATCH" : String(nm.prefix(32)), engine: engine, engineName: engineName,
                        p: p.map { round($0) }, pattern: patternUsed(pat) ? pat : nil, tags: uniq,
                        created: created ?? now, modified: modified ?? created ?? now, slot: slot, dx7: nil)
        if let dx7, !(dx7 is NSNull) { out.dx7 = dx7; out.engineName = "FM6" }
        return out
    }

    // MARK: keys and remapping

    /// the library's parameter keys from the device's labels (web: paramKeys); the engine's eight are E0..E7
    static func paramKeys(labels: [String], pe0: Int, pcount: Int) -> [String] {
        var seen: [String: Int] = [:], out: [String] = []
        for i in 0..<pcount {
            if i >= pe0 && i < pe0 + 8 { out.append("E\(i - pe0)"); continue }
            let l0 = i < labels.count && !labels[i].isEmpty ? labels[i] : "P\(i)"
            let l = l0 == "DROP" ? "PIT" : l0
            if l == "DST A" && seen[l] == nil { for x in ["PIT", "FLT", "SHP"] { seen[x, default: 0] += 1 } }
            seen[l, default: 0] += 1
            out.append(seen[l]! > 1 ? "\(l)#\(seen[l]!)" : l)
        }
        return out
    }

    static func lfoSlotsFromDepths(_ d: [Int]) -> [Int] {
        let v = d.enumerated().map { i, x in i == 3 ? max(0, min(127, x)) : max(-64, min(63, x)) }
        let w = v.enumerated().map { i, x in i == 3 ? (x + 1) >> 1 : abs(x) }
        var a = 4, b = 4
        for i in 0..<4 where w[i] != 0 && (a == 4 || w[i] > w[a]) { a = i }
        for i in 0..<4 where i != a && w[i] != 0 && (b == 4 || w[i] > w[b]) { b = i }
        if b < a { swap(&a, &b) }
        func slot(_ i: Int) -> [Int] { i < 4 ? [1 + i, i == 3 ? v[i] : 2 * v[i]] : [0, 0] }
        return slot(a) + slot(b)
    }

    static let a2AmtKeys = ["FLT#3", "PIT#3", "SHP#3", "OSC2", "ENV2"]

    /// values by fromKeys -> values by toKeys (nil where the source has no such key); the older ANALOG 2 / LFO keys converted
    static func remapParams(_ p: [Int?], from fromKeys: [String]?, to toKeys: [String]?) -> [Int?] {
        guard let fromKeys, let toKeys, fromKeys != toKeys else { return p }
        var at: [String: Int] = [:]
        for (i, k) in fromKeys.enumerated() { at[k] = i }
        func val(_ k: String) -> Int? { at[k].flatMap { $0 < p.count ? p[$0] : nil } }
        var out: [Int?] = toKeys.map { val($0) }
        func put(_ k: String, _ v: Int) { if let i = toKeys.firstIndex(of: k), at[k] == nil { out[i] = v } }
        if toKeys.contains("DST A") && at["DST A"] == nil && ["PIT#2", "FLT#2", "SHP#2", "AMP"].contains(where: { at[$0] != nil }) {
            let s = lfoSlotsFromDepths(["PIT#2", "FLT#2", "SHP#2", "AMP"].map { val($0) ?? 0 })
            for (i, k) in ["DST A", "AMT A", "DST B", "AMT B"].enumerated() { put(k, s[i]) }
        }
        if toKeys.contains("OSC2") && !toKeys.contains("DST2") {
            if let v = val("FATK") { put("ATK2", v) }
            if let v = val("FDEC") { put("DEC2", v) }
            if let amt = val("AMT2") ?? val("FENV") {
                let d = max(0, min(4, val("DST2") ?? 0))
                for (i, k) in a2AmtKeys.enumerated() { put(k, i == d ? amt : 0) }
            }
        }
        return out
    }

    // MARK: files

    /// -> the file as JSON bytes
    static func encode(kind: String, patches: [Patch], ctx: LibraryContext, exported: String = BackupFile.isoNow()) throws -> Data {
        let list: [[String: Any]] = patches.map { x in
            let c = clean(name: x.name, engine: x.engine, engineName: x.engineName, p: x.p.map { $0 as Any? ?? NSNull() }, pattern: x.pattern,
                          tags: x.tags, created: x.created, modified: x.modified, slot: x.slot, dx7: x.dx7)
            var o: [String: Any] = ["name": c.name, "engine": c.engine,
                                    "engineName": c.engineName.isEmpty ? (c.engine >= 0 && c.engine < ctx.engines.count ? ctx.engines[c.engine] : "") : c.engineName,
                                    "params": c.p.map { $0 as Any? ?? NSNull() }, "pattern": c.pattern ?? NSNull(),
                                    "tags": c.tags, "created": c.created, "modified": c.modified]
            if let s = x.slot { o["slot"] = s }
            if let d = c.dx7 { o["dx7"] = d }
            return o
        }
        let file: [String: Any] = ["format": format, "version": version, "kind": kind, "firmware": ctx.firmware,
                                   "pCount": ctx.keys?.count ?? 0, "pE0": ctx.pe0 as Any? ?? NSNull(), "paramLabels": ctx.keys ?? [],
                                   "engines": ctx.engines, "exported": exported, "patches": list]
        return try JSONSerialization.data(withJSONObject: file, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])
    }

    struct Read {
        var patches: [Patch]
        var skipped: Int
        var keys: [String]?
        var engines: [String]?
        var pe0: Int?
        var kind: String
    }

    static func decode(_ data: Data, ctx: LibraryContext) throws -> Read {
        guard let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { throw WireError("not JSON") }
        func engIndex(_ name: String?, _ idx: Int, _ fileEngines: [String]?) -> Int {
            let nm = (name?.isEmpty == false ? name : nil) ?? (fileEngines.flatMap { idx >= 0 && idx < $0.count ? $0[idx] : nil })
            if !ctx.engines.isEmpty {
                if let nm { return ctx.engines.firstIndex(of: nm) ?? -1 }
                return idx >= 0 && idx < ctx.engines.count ? idx : -1
            }
            return idx
        }
        let fmt = obj["format"] as? String
        if fmt == "felucca-patch" {                            // the editor's "Save to file": the whole current sound
            guard let p = obj["p"] as? [Any], let eng = int(obj["engine"]) else { throw WireError("felucca-patch without engine / p") }
            let en = obj["engineName"] as? String
            let e = engIndex(en, eng, nil)
            if e < 0 { return Read(patches: [], skipped: 1, keys: nil, engines: nil, pe0: nil, kind: "patch") }
            let name = (obj["presetName"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? en.flatMap { $0.isEmpty ? nil : $0 } ?? "PATCH"
            let cut = Array(p.prefix(ctx.keys?.count ?? p.count))
            let pat = (obj["steps"] as? [[String: Any]]).map(patternFromSteps)
            let patch = clean(name: name, engine: e, engineName: en ?? "", p: cut, pattern: pat, tags: ["file"], created: nil, modified: nil, slot: nil, dx7: nil)
            return Read(patches: [patch], skipped: 0, keys: nil, engines: nil, pe0: nil, kind: "patch")
        }
        guard fmt == format else { throw WireError("unknown format") }
        guard (int(obj["version"]) ?? 0) >= 1 else { throw WireError("version") }
        guard let list = obj["patches"] as? [Any] else { throw WireError("no patches") }
        let fileKeys = (obj["paramLabels"] as? [Any]).flatMap { $0.isEmpty ? nil : $0.map { "\($0)" } }
        let fileEngines = (obj["engines"] as? [Any])?.map { "\($0)" }
        var out: [Patch] = []
        var skipped = 0
        for item in list {
            guard let x = item as? [String: Any], let params = x["params"] as? [Any] else { skipped += 1; continue }
            let hasDx7 = x["dx7"] != nil && !(x["dx7"] is NSNull)
            let idx = int(x["engine"]) ?? 0
            let e = engIndex(hasDx7 ? "FM6" : x["engineName"] as? String, idx, fileEngines)
            if e < 0 && !hasDx7 { skipped += 1; continue }
            let raw = params.map { round($0) }
            let mapped = remapParams(raw, from: fileKeys, to: ctx.keys)
            let name = e >= 0 && e < ctx.engines.count ? ctx.engines[e] : ((x["engineName"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? (fileEngines.flatMap { idx >= 0 && idx < $0.count ? $0[idx] : nil }) ?? "")
            out.append(clean(name: x["name"], engine: e, engineName: name, p: mapped.map { $0 as Any? ?? NSNull() }, pattern: x["pattern"] as? [Any],
                             tags: x["tags"] as? [Any], created: x["created"] as? String, modified: x["modified"] as? String,
                             slot: int(x["slot"]), dx7: x["dx7"]))
        }
        return Read(patches: out, skipped: skipped, keys: fileKeys, engines: fileEngines, pe0: int(obj["pE0"]), kind: obj["kind"] as? String ?? "library")
    }
}
