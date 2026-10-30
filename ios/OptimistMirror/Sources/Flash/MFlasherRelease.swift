// SPDX-License-Identifier: GPL-3.0-only
// The pinned M-Flasher release (Julius Vitkauskas, MIT) for the Firmware tab's "this switch needs a computer" help. The data is
// NOT written here: tools/mflasher.py is the one source of truth (version, file names, sizes, SHA-256), and the build phase
// tools/sync_mflasher.py writes it into the bundle as mflasher.json (project.yml), the way the web editor is bundled.
// tests/mflasher_get_test.py and Tests/FlashTests.swift check that the bundle agrees with the source.
import Foundation

struct MFlasherRelease: Decodable, Equatable {
    struct File: Decodable, Equatable {
        let name: String
        let url: URL
        let sha256: String
        let size: Int
        let os: String            // macos windows linux
        let arch: String          // any x86_64 aarch64
        let kind: String          // dmg zip tar.gz deb rpm
    }

    let version: String
    let page: URL
    let files: [File]

    /// what the Firmware tab shows: the words, the one link to open, and the checksum of the file it points at
    struct Advice: Equatable {
        let text: String
        let title: String
        let link: URL
        let sha256: String?
    }

    static func decode(_ data: Data) throws -> MFlasherRelease {
        try JSONDecoder().decode(MFlasherRelease.self, from: data)
    }

    /// the bundle's mflasher.json (tools/sync_mflasher.py), nil when the build phase did not run
    static func bundled(_ bundle: Bundle = .main) -> MFlasherRelease? {
        guard let url = bundle.url(forResource: "mflasher", withExtension: "json"), let data = try? Data(contentsOf: url) else { return nil }
        return try? decode(data)
    }

    /// iPhone / iPad cannot run M-Flasher: say a computer is needed and link the release page. Mac Catalyst is a Mac: link the
    /// macOS disk image directly, with its SHA-256. The app downloads nothing itself.
    func advice(catalyst: Bool) -> Advice {
        if catalyst, let dmg = files.first(where: { $0.os == "macos" && $0.kind == "dmg" }) {
            return Advice(text: "This switch needs M-Flasher \(version) (download mode). Download it for macOS (\(dmg.name), SHA-256 \(dmg.sha256)); "
                          + "on Windows or Linux, use the release page.", title: "M-Flasher for macOS", link: dmg.url, sha256: dmg.sha256)
        }
        return Advice(text: "This switch needs a computer: M-Flasher for macOS / Windows / Linux (release \(version)).",
                      title: "M-Flasher releases", link: page, sha256: nil)
    }

    /// the same, from the bundle, or the generic release page when there is no data
    static func advice(catalyst: Bool, bundle: Bundle = .main) -> Advice {
        if let r = bundled(bundle) { return r.advice(catalyst: catalyst) }
        return Advice(text: "This switch needs a computer: M-Flasher for macOS / Windows / Linux.", title: "M-Flasher releases",
                      link: InstallRoute.mflasher, sha256: nil)
    }

    static var isCatalyst: Bool { ProcessInfo.processInfo.isMacCatalystApp }
}
