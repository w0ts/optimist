// SPDX-License-Identifier: GPL-3.0-only
// The computer keyboard as the FM-1's panel, the FM-1 Emulator's keys (fm1-emulator rust-emulator/src/ui.rs): letters and Space
// hold the buttons, the arrows OCT- and OCT+, A W S E D R F G T H Y J K the note keys (Shift: an octave up), pairs of keys turn
// the knobs. Sent as editor cmd 108 PANEL (firmware io/editor/ed_mirror.c): buttons by label, knobs by role.
import SwiftUI

enum PanelKeys {
    /// panel.c's labels: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+
    static let buttons: [String: UInt8] = ["x": 0, "b": 1, "v": 2, "l": 3, "i": 4, "o": 5, "u": 6, "z": 7, "p": 8, "q": 9,
                                           " ": 10, "c": 11, "←": 12, "→": 13]
    /// note keys 0 .. 12 (F3 up), Shift: 13 .. 25
    static let notes = Array("awsedrfgthyjk").map(String.init)
    /// panel.c's roles: SELECT ALGORITHM PRESETS KNOB 1 .. 4; counter-clockwise, clockwise
    static let knobs: [String: (role: UInt8, step: Int)] = [
        "[": (0, -1), "]": (0, 1), "-": (1, -1), "=": (1, 1), "9": (2, -1), "0": (2, 1),
        "1": (3, -1), "2": (3, 1), "3": (4, -1), "4": (4, 1), "5": (5, -1), "6": (5, 1), "7": (6, -1), "8": (6, 1)]

    static func name(_ k: KeyPress) -> String {
        switch k.key {
        case .leftArrow: "←"
        case .rightArrow: "→"
        default: k.characters.lowercased()
        }
    }

    /// The PANEL arguments for a key held (kind 0 a button, 1 a note key), nil: not a panel key
    static func held(_ name: String, shift: Bool) -> [UInt8]? {
        if let b = buttons[name] { return [0, b] }
        if let n = notes.firstIndex(of: name) { return [1, UInt8(n + (shift ? 13 : 0))] }
        return nil
    }
}
