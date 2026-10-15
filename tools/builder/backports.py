# SPDX-License-Identifier: GPL-3.0-only
"""Backported features (tools/backports.json, kept by the backport branches) as registry items.

Each entry of backports.json is one FELUCCA_* switch with its source (repo, commit, author, licence), a
notice and "warning" (X0X-derived or experimental). Here each becomes an Item with that provenance; an item
from X0X (charlesvestal/fm1-x0x) always carries a notice the menu shows as a NOTICE when it is selected.
Bits are stable: a switch keeps the bit listed in BITS (append new ones; never reuse)."""
import json
from pathlib import Path

import registry as R

SRC = Path(__file__).resolve().parents[2] / "tools" / "backports.json"
GROUP = {"sequencer": "Sequencer", "ui": "UI", "fx": R.FX_ANY, "drums": "Drums", "midi": "MIDI & USB",
         "system": "System", "engines": "Synth engines", "engine": "Synth engines", "experimental": "Experimental"}
LABEL = {  # short menu labels (the title stays in the details)
    "FELUCCA_CHANCE": "per-step chance (STEP 2 PROB)", "FELUCCA_KEYLIT": "keys light the notes played",
    "FELUCCA_QNT_SEQ": "SCL > QNT SEQ (sequenced notes snap)", "FELUCCA_SPRING": "reverb: SPRING",
    "FELUCCA_BASSPLUS": "BASS+ speaker mode", "FELUCCA_BRIGHT": "screen brightness (MENU > BRIGHT)",
    "FELUCCA_MOTION": "motion recording (knobs per step)",
    "FELUCCA_ENG_NOISE": "NOISE (colours, crackle, shift-register noise)",
    "FELUCCA_ENG_PHYS": "PHYS (physical models)", "FELUCCA_ENG_ACID": "ACID (TB-303 bass + line generator)",
    "FELUCCA_ENG_CZ": "CZ (CZ-1 style tones)",
    "FELUCCA_ENG_JUNO": "JUNO (Juno-106 voice)",
    "FELUCCA_GLIDE": "mixer glides ~10 ms (no zipper)",
    "FELUCCA_LIGHTS": "menu LIGHTS / KEYS / NOTES (play in the dark)",
    "FELUCCA_REC_MODES": "REC screen dials: mode, length, count-in",
    "FELUCCA_BPM_LOCK": "BPM LOCK: SELECT is the tempo only with GLO",
    "FELUCCA_DIV_ORDER": "divisions in length order (1/8 8T 1/16 ...)",
    "FELUCCA_PUNCH_LATCH": "punch LATCH: FX + key latches its effect",
    "FELUCCA_MOTION_MARK": "mark the parameters motion recording moves",
    "FELUCCA_UP_FM6": "FM6 user presets keep their voice",
    "FELUCCA_SL24_SAFE": "SLOOP 2.4's data kept safe (never erased)",
    "FELUCCA_SL24_XSTEP": "step extras storage (nudge, locks, fills; 2.4)",
    "FELUCCA_DIV_LONG": "SEQ DIV 1/2, 1BAR, 2BAR (SLOOP 2.4)", "FELUCCA_DLY_DOT": "delay TIME 1/8D, 1/16D (SLOOP 2.4)",
    "FELUCCA_MICRO": "micro timing: nudge a step (SLOOP 2.4)", "FELUCCA_FILLS": "fills: GLO + 9 / 10, step conditions (SLOOP 2.4)",
    "FELUCCA_PLOCK": "parameter locks per step (SLOOP 2.4)", "FELUCCA_QCHAIN": "quick chain: SAVE + section taps (SLOOP 2.4)",
    "FELUCCA_SL24_IMPORT": "import SLOOP 2.4 projects (LOAD twice)",
    "FELUCCA_SEL_PAGES": "SELECT turns the pages of a family (SLOOP 2.4)",
    "FELUCCA_BIGVALS": "big values on pages without a graph (SLOOP 2.4)",
    "FELUCCA_SL24_EXPORT": "export a project for SLOOP 2.4 (web editor)",
    "FELUCCA_SL24_EDIMPORT": "import a SLOOP 2.4 backup file (web editor)",
    "FELUCCA_COND": "step conditions 1:2 .. 4:4 (Felucca 1.5.6)",
    "FELUCCA_SEQTOOLS": "STEP tools: REVERSE RANDOM COOK EUCLID (Felucca 1.5.6)",
}
DESC = {  # what each switch does for the user (plain words; sizes from tools/builder/costs.json, details in backports.json)
    "FELUCCA_CHANCE": "Gives each synth step a chance to play (SEQ > STEP 2, KNOB 2: 0 to 100 % in 5 % steps); a step "
                      "that fails plays as a rest. About 0.5 KB of flash; nothing changes until a step's chance is turned "
                      "down. Synth tracks only; the web editor does not show it yet. Off: every step plays (chances "
                      "stay saved).",
    "FELUCCA_KEYLIT": "Lights the keys of the notes the selected track plays (its steps, the arpeggiator, held notes), "
                      "so you see the pattern on the keyboard. About 0.2 KB of flash, LEDs only; with LIGHTS, MENU > "
                      "NOTES turns it on and off at run time.",
    "FELUCCA_QNT_SEQ": "SCL > QNT SEQ: the keys and the sequenced notes snap to the scale as they play, so a pattern "
                       "follows a change of ROOT or SCALE; the steps keep their notes as written. About 0.3 KB of "
                       "flash; not on GM KIT or SLICE parts. A build without it plays a project's QNT SEQ as ALL.",
    "FELUCCA_SPRING": "A spring-tank reverb, one of the reverb's algorithms: the chirp and drip of a guitar amp's "
                      "spring (SPRNG on FX > REVERB > TYPE when two or more are ticked). Its output is mono, and it "
                      "is a little lighter on CPU than ROOM (emulator). Beside ROOM: about 1.9 KB of flash and "
                      "2.3 KB of RAM; ticked alone it is the only reverb.",
    "FELUCCA_BASSPLUS": "A third MENU > LOWCUT setting (OFF / LOWCUT / BASS+) for the FM-1's small speaker: it adds "
                        "harmonics of the bass below ~150 Hz, which the speaker can play, and raises the low cut to "
                        "~220 Hz. About 0.5 KB of flash and 0.35 KB of RAM; CPU only while BASS+ is on.",
    "FELUCCA_BRIGHT": "MENU > BRIGHT: the screen backlight in 8 levels, 4/16 at the lowest (software PWM). The level "
                      "lasts until power-off: every boot starts at full and a dim level is never saved. About 36 B "
                      "of flash; X0X reports it working on a real FM-1 but it is not tried here on hardware. Off: "
                      "always full brightness.",
    "FELUCCA_MOTION": "Motion recording: while recording, knob turns are stored per step and replayed on every pass "
                      "(SEQ > MOTION: play on / off per track, clear). The events are the automation store's (up "
                      "to 128 a track with its locks, nudges and fills); undone with the recording pass, editable "
                      "from the web editor's protocol (AUTO_GET / AUTO_SET). About 3.4 KB of flash, 0.5 KB of RAM "
                      "and 1.4 KB of pool (measured before the automation store).",
    "FELUCCA_ENG_NOISE": "Noise engine from Felucca-Engines (engine 15, MIT): white, pink and brown noise, crackle "
                         "(DUST), a shift register clocked above the key (LFSR) and short-sequence metal tones (META), "
                         "through a resonant filter that morphs from low-pass to band-pass, with key tracking, a slow "
                         "random drift and a bit crusher; 4 presets. About 3 KB of flash, no RAM; 1.1..1.3 K host "
                         "instructions a sample with 8 voices (the heaviest ANALOG preset: 1.7 K).",
    "FELUCCA_ENG_PHYS": "Physical-modelling engine (engine 11): modal, string, membrane and sympathetic-string models, "
                        "3 voices per part. Heavy: about 9.8 KB of flash and 38.7 KB of pool, and 3 voices of its "
                        "heaviest preset take about 17 % of the audio budget at 312 MHz (at 96 MHz that was too much "
                        "and voices were shed).",
    "FELUCCA_ENG_ACID": "X0X's TB-303-style bass voice (one monophonic voice per part, engine 12) with the TB-3PO line "
                        "generator. Experimental: floating-point DSP (about 14.4 KB of flash, 1.7 KB of RAM) that ran on "
                        "a real FM-1 in X0X but has not been tried here; no pitch bend or TUNE on the 303; 4 presets.",
    "FELUCCA_ENG_CZ": "Casio CZ-1 style engine from Melodee 0.11 (engine 13): two lines with 8-step envelopes, 8 "
                      "built-in tones (TONE) changed by the EDIT values; no envelope editing, banks or SysEx yet. About "
                      "7 KB of flash and 3.1 KB of RAM; similar CPU to FM6 (emulator).",
    "FELUCCA_ENG_JUNO": "Juno-106 voice from fm-106 / KR-106 (engine 14): the DCO (saw, pulse, sub, noise), the "
                        "4-pole OTA ladder filter and the 4-position HPF; the track's ENV and LFO drive it; 6 voices, "
                        "8 patches of our own (no Roland factory patches). About 2x the CPU of an ANALOG voice.",
    "FELUCCA_REC_MODES": "REC screen dials: MODE (free / tempo), LENGTH (1 / 2 / 4 bars) and START (note / 4-3-2-1 "
                         "count-in). The count-in runs on the internal clock only and in 4/4. About 1.2 KB of flash; "
                         "off: free recording started by a note.",
    "FELUCCA_LIGHTS": "Lights for playing in the dark: MENU LIGHTS (every button glows OFF / LOW / MID / HIGH), KEYS (C "
                      "or white keys glow) and NOTES (the note lights of KEYLIT on / off at run time). About 1.2 KB of "
                      "flash.",
    "FELUCCA_GLIDE": "Part level, pan and sends, the master volume and the drum track's level, pan and sends glide over "
                     "~10 ms instead of jumping, which removes the zipper noise of a moving knob. About 1.3 KB of "
                     "flash, 0.3 KB of RAM and 0.7 KB of fast RAM code; the sound changes only while a gain moves.",
    "FELUCCA_BPM_LOCK": "SELECT changes the tempo only while GLO is held, so the tempo cannot slip live; GLO > GLOBAL's "
                        "BPM knob and tap tempo still work. No menu item: the build switch is the choice. About 64 B "
                        "of flash.",
    "FELUCCA_DIV_ORDER": "Lists note divisions in length order (1/4 1/8 8T 1/16 16T 1/32) on ARP RATE, SEQ DIV, DELAY "
                         "TIME and SLICER RATE; stored values are unchanged. About 0.2 KB of flash.",
    "FELUCCA_PUNCH_LATCH": "PUNCH: FX + a key latches its effect so you can let go; the same key or FX + OCT- turns it "
                           "off, another key switches to its effect, and FX stays lit while one plays. About 80 B of "
                           "flash.",
    "FELUCCA_MOTION_MARK": "With motion recording: a small square in the track colour marks the parameter cards that "
                           "the track's motion moves (page and VIEW ALL). About 0.3 KB of flash.",
    "FELUCCA_UP_FM6": "FM6 user presets keep their whole voice (operator edits included) instead of only the VOICE "
                      "number. About 0.7 KB of flash plus a 3.6 KB store in flash; a preset written from the web "
                      "editor drops its kept voice.",
    "FELUCCA_DIV_LONG": "SEQ > DIV (and the SEQ layer's KNOB 2) go on past 1/4 to 1/2, 1BAR and 2BAR: a step of 2, 4 or 8 "
                        "beats, for slow chords and pads; such steps are never swung. Stored values unchanged (appended), so "
                        "SLOOP 2.4 projects load as they play there. Costs 172 B of flash.",
    "FELUCCA_DLY_DOT": "FX > DLY > TIME gains 1/8D and 1/16D (dotted: 3/4 and 3/8 of a beat), as SLOOP 2.4. With the "
                       "0.74 s delay line a 1/8D below 61 BPM halves to 1/16D. Costs 28 B of flash.",
}
DESC["FELUCCA_SL24_SAFE"] = (
    "Coming from SLOOP 2.4, 2.5 or 2.6: Optimist never erases or writes over what SLOOP left that it cannot read: the"
    " four project slots and the autosave (2.4 / 2.5's FUN5 and 2.6's FUN6 projects, shown as SLOOP on the "
    "PROJECT page and in the editor, not EMPTY), 2.4's FM6 bank and a user sample longer than ours (USR3, USR4); "
    "2.4's settings word and 2.4 .. 2.6's user presets are read right (2.5's engine numbers, 2.6's ANALOG filter "
    "TYPE); a user preset Optimist saves into SLOOP's bank leaves SLOOP's presets there byte for byte (to SLOOP that "
    "slot is empty). SLOOP 2.5 / 2.6's settings (their SYN1..SYN4 drum kits and "
    "2.6's MIDI LEARN after them) are never read as Optimist's and never written over: Optimist keeps its own "
    "settings beside them, so each firmware finds its own after a switch (a cut while Optimist saves its settings: "
    "their 88 bytes again, as at the first start). Off, the first start erases 2.4's projects. About 0.7 KB of flash.")
DESC["FELUCCA_SL24_XSTEP"] = (
    "Keeps SLOOP 2.4's per-step extras with every project: each step's nudge (micro timing), its parameter locks and "
    "fill condition (the automation store's step-only events, up to 128 a track), stored in 2.4's own layout while it "
    "holds them (24 locks a track), so 2.4 projects carry them over. Storage only: the "
    "sequencer features that play them come with their own switches. A section with none costs nothing. About 1.6 KB of flash, "
    "0.9 KB of RAM and 4.2 KB of pool.")
DESC["FELUCCA_SL24_IMPORT"] = (
    "A SLOOP 2.4, 2.5 or 2.6 project left in a slot shows as SLOOP on the PROJECT page; LOAD it twice and it "
    "becomes the working project (values, engines, drum kits, steps; nudges, locks and fills with the step extras "
    "storage), then SAVE puts it in a section; PROJECT > A24 (twice) does the same with 2.4's autosave. 2.4's "
    "original stays in flash. An FM6 part keeps its sound: its patch (2.4's factory one, or the one in 2.4's FM6 "
    "bank) becomes the part's voice, with 2.4's macros in it; a USR1..USR3 kit plays the same user sample slot from "
    "the drum lanes. Lost: the track filter, strum and voice-leading values (without their switches), FM6's DTUN, a "
    "USR4 kit (the default kit); from 2.5: a NOISE part (ANALOG at its defaults), a SYN1..SYN4 kit (the default kit)"
    " (2.5's drums' delay send becomes every drum lane's DLY, the nearest level); from 2.6 also: the INSERT, ENV SYNC, LFO 2's TRIG / POL, GLO > TEXTURE (its "
    "ANALOG filter TYPE goes to ANALOG 2's, LFO 2's SYNC to the LFO's tempo-synced RATE). About 3.3 KB of flash.")
DESC["FELUCCA_MICRO"] = (
    "Micro timing, as SLOOP 2.4: hold a step on the SEQ layer and turn KNOB 4 to play it up to half a step early or late "
    "(1/64 of a step a click; the step shows a dot). With it, KNOB 4 on a held step is the nudge and the note length "
    "moves to SELECT. Needs SL24_XSTEP (the storage). 2.1 KB of flash with the editor commands.")
DESC["FELUCCA_FILLS"] = (
    "Fills, as SLOOP 2.4: hold a step on the SEQ layer and press OCT+ to make it play only in a fill (F) or never in one "
    "(x); OCT- clears its nudge, locks and fill. GLO + key 9 plays a fill while held, GLO + key 10 makes the next bar "
    "one. The FX bypass moves to GLO + black keys 1-4. Needs SL24_XSTEP. 2.5 KB of flash with the editor commands.")
DESC["FELUCCA_PLOCK"] = (
    "Parameter locks, as SLOOP 2.4: hold a step on the SEQ layer and turn PRESETS to give it its own value of a sound "
    "parameter (the last one you turned, ALGORITHM picks another; the title shows it), up to 128 a track with the "
    "motion and the other step events, several a step; "
    "it goes back at the next step without one. Beside motion recording: motion plays first, the lock wins on its step. "
    "The drum kit locks; the macros' places do not. Needs SL24_XSTEP. 4.4 KB of flash with the editor commands, 0.5 KB of RAM, 0.1 KB of RAM code.")
DESC["FELUCCA_SEL_PAGES"] = (
    "Turning SELECT on a parameter page (ENV, LFO, FX, EDIT, ARP, SEQ, SCL, GLO, SAVE) goes to the previous or next "
    "page of that group, as SLOOP 2.4 and as tapping the group's button again, but both ways and stopping at the ends. "
    "On TRACKS, the SONG and DRUM screens, the REC screen and while a layer is held, SELECT stays the tempo. Off: SELECT "
    "is always the tempo. Flash only (about 0.2 KB, unmeasured).")
DESC["FELUCCA_BIGVALS"] = (
    "Pages without a graph (EDIT, VOICE, the DEST pages, GLOBAL, MASTER, SYSTEM...) use the empty middle of the screen: "
    "their four values in large type, placed as the knobs are (KNOB 1 2 above, KNOB 3 4 below), the one you turn in "
    "white; a page with one value shows it across the middle. As SLOOP 2.4. Costs 608 B of flash and 112 B of RAM.")
DESC["FELUCCA_QCHAIN"] = (
    "Quick chain, as SLOOP 2.4: while playing, hold SAVE and tap up to 8 sections (of the bank shown: A..P with 16 "
    "sections): the first plays from the next bar, and when SAVE is let go they play in that order, each for the bars "
    "of its longest pattern, looped; one tap is a plain jump and stops the chain, STOP ends it. RAM only. 1 KB of flash.")
DESC["FELUCCA_SL24_EXPORT"] = (
    "Adds Export for SLOOP 2.4 to the web editor's Projects screen: it saves the working project and the settings as "
    "a SLOOP backup file, which SLOOP's own editor restores (BACKUP > Restore), so a project moves to SLOOP. The file"
    " is for SLOOP 2.5 and later (2.6 reads it): a PHYS part stays PHYS; SLOOP 2.4 reads it too, but plays a PHYS "
    "part as ANALOG with PHYS's knob values. Nothing is written to the FM-1. An FM6 voice that is one of 2.4's own "
    "patches stays that patch; with FM6 voices into 2.4's bank ticked, the others go into 2.4's FM6 bank (the file "
    "carries it: restoring it replaces 2.4's bank, kept as it was with the voices added). Lossy: what 2.4 has not is "
    "left out and the editor lists it (FX OFF, ANALOG 2, SLICE / ACID / CZ parts play their fallback, an FM6 voice "
    "not sent to the bank becomes the closest 2.4 factory patch, X0X and user kits become 808, the drum lanes' edits "
    "and sends but their DLY, which becomes 2.5's drums' delay send (the level most lanes have), the reverb type, COMP / LIMIT, locks on those); sections, the song, motion and samples stay behind. "
    "About 2 KB of flash.")
DESC["FELUCCA_SL24_EDIMPORT"] = (
    "Adds Import a SLOOP 2.4 backup to the web editor's Projects screen: a project of a SLOOP 2.4, 2.5 or 2.6 backup "
    "file (2.6's made a 2.5 one by the editor: its new sound settings and filter TYPE left out, LFO 2's SYNC kept as the LFO's RATE) (its working project "
    "or A..D) becomes the working project, as LOAD twice does with one left in flash, FM6 bank patches included; SAVE"
    " then keeps it in a section. The editor can also put 2.4's FM6 patches (from the file, or the bank 2.4 left in "
    "flash) in the free slots of the FM6 user bank. Nothing else is written. Needs SL24_IMPORT. About 1.2 KB of flash"
    " and 0.4 KB of pool.")
DESC["FELUCCA_COND"] = (
    "Step conditions from Felucca 1.5.6: a step plays only on loop a of every b of its track (1:2 2:2 1:3 2:3 3:3 1:4 "
    "2:4 3:4 4:4), counted from PLAY, a song section or a launched pattern. With FILL and the chance on the same step: "
    "the condition, then FILL, then the chance. The Optimist UI: a held step's EXTRA page, COND; SLOOP's UI plays them. "
    "A step-only event of the automation store (it builds the store); an older build plays such a step always. About "
    "1.4 KB of flash beside the store's other items (11 KB when it builds the store alone).")
DESC["FELUCCA_SEQTOOLS"] = (
    "The sequence tools of Felucca 1.5.6 on STEP's last two pages (the Optimist UI): TOOLS (REVERSE, RANDOM: new notes "
    "in the scale or a new beat, COOK: a few small changes, LENGTH x2 / half), each one asking first, and EUCLID (HITS STEPS "
    "ROT, ON / OFF: ON plays it over the pattern, OFF asks to keep it or give the pattern back). On a drum lane picked with ALGORITHM, that lane only. Notes keep their exact lengths; locks, chance "
    "and conditions move with their steps (the EDIT layer's SHIFT and LENGTH too). About 5.3 KB of flash; nothing with "
    "SLOOP's UI.")
PARENT = {"FELUCCA_SPRING": "FX_REVERB", "FELUCCA_DLY_DOT": "FX_DELAY", "FELUCCA_PUNCH_LATCH": "FX_PUNCH",
          "FELUCCA_MOTION_MARK": "MOTION", "FELUCCA_UP_FM6": "ENG_FM6"}   # options of a registry item
AFTER = {"FELUCCA_SPRING": "REV_FDN8"}   # an option's place among its parent's (SPRING beside the other reverb algorithms)
BITS = {  # switch -> stable BUILD bit (append only)
    "FELUCCA_CHANCE": 64, "FELUCCA_KEYLIT": 65, "FELUCCA_QNT_SEQ": 66, "FELUCCA_KNOB_ACCEL": 67,
    "FELUCCA_LCD_DIRTY": 68, "FELUCCA_UNDO_HISTORY": 69, "FELUCCA_SIZE": 70,   # (71: FELUCCA_UA_RESAMPLE, retired 2026-10-15: USB audio always resamples; never reuse the bit)
    "FELUCCA_SPRING": 72, "FELUCCA_BASSPLUS": 73, "FELUCCA_BRIGHT": 74,   # (75: FELUCCA_DLY_HALVE, retired 2026-10-14: always built; never reuse)
    "FELUCCA_MOTION": 76, "FELUCCA_ENG_PHYS": 77, "FELUCCA_ENG_ACID": 78,
    # (110: FELUCCA_BK_CHECK, retired 2026-10-14: always built; never reuse the bit)
    # (109: FELUCCA_TRS_NOISE, retired 2026-10-14: always built; never reuse the bit)
    # (108: FELUCCA_KNOB_ONEREST, retired 2026-10-14: Felucca 1.6's knob decoder is always built; never reuse the bit)
    "FELUCCA_GLIDE": 107,
    "FELUCCA_LIGHTS": 106,
    "FELUCCA_REC_MODES": 105,
    # (104: FELUCCA_KEYS_FAST, retired 2026-10-14: always built; never reuse the bit)
    # (103: FELUCCA_SHED_FADE, retired 2026-10-14: always built; never reuse the bit)
    # (102: FELUCCA_USB_FLOW, retired 2026-10-14: always built; never reuse the bit)
    # (101: FELUCCA_ST_STRICT, retired 2026-10-14: always built; never reuse the bit)
    # (100: FELUCCA_MONO_RELEASE, retired 2026-10-14: always built; never reuse the bit)
    # (130: FELUCCA_LAYER_QUIET, retired 2026-10-14: always built; never reuse the bit)
    "FELUCCA_BPM_LOCK": 132,
    "FELUCCA_DIV_ORDER": 133,
    "FELUCCA_PUNCH_LATCH": 131,
    "FELUCCA_MOTION_MARK": 134,
    "FELUCCA_ENG_NOISE": 140,                              # (the NOISE engine; 139 is JUNO's: feat/engine-juno)
    "FELUCCA_UP_FM6": 135,
    "FELUCCA_ENG_CZ": 136,
    "FELUCCA_ENG_JUNO": 139,
    "FELUCCA_SL24_SAFE": 161,
    "FELUCCA_SL24_XSTEP": 162,
    "FELUCCA_SL24_IMPORT": 163,                           # (SLOOP 2.4 phase 0: 161..164; 160 is feat/pr45-small's)
    "FELUCCA_SL24_EXPORT": 164,
    # SLOOP 2.4 sequencer (Phase 2 of the 2.4 backport: bits 175..194)
    "FELUCCA_DIV_LONG": 175, "FELUCCA_DLY_DOT": 176, "FELUCCA_MICRO": 177, "FELUCCA_FILLS": 178,
    "FELUCCA_PLOCK": 179, "FELUCCA_QCHAIN": 180,
    "FELUCCA_SL24_EDIMPORT": 181,                       # (SLOOP 2.4 leftovers: the editor import of a 2.4 backup file)
    "FELUCCA_SEL_PAGES": 215,                           # (SLOOP 2.4 phase 5, the UI: 215..229)
    "FELUCCA_BIGVALS": 217,
    "FELUCCA_COND": 240,                                # (Felucca 1.5.6: 240..244)
    "FELUCCA_SEQTOOLS": 241,
}
NEXT_FREE = 80                                          # (switches not in BITS yet: from here, by name)


def is_x0x(src):
    return "fm1-x0x" in json.dumps(src).lower() or "charles vestal" in json.dumps(src).lower()


def load(path=SRC):
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return []
    items = []
    extra = sorted(e["switch"] for e in data.get("items", []) if e["switch"] not in BITS)
    for e in data.get("items", []):
        sw = e["switch"]
        key = sw.replace("FELUCCA_", "")
        if key in R.ITEMS or key in R.FORBIDDEN or sw in [it.flag for it in R.ITEMS.values()]:
            continue
        src = e.get("source", {})
        x0x = is_x0x(src) or is_x0x(e.get("also", {}))
        prov = R.Provenance(project=src.get("repo", "").replace("https://github.com/", ""),
                            author=src.get("author", ""), licence=src.get("licence", ""),
                            commit=src.get("commit", ""), url=src.get("repo", ""))
        notice = e.get("notice", "") if (x0x or e.get("warning")) else ""   # (provenance shows for the rest)
        if x0x and not notice:
            notice = f"Ported from X0X by Charles Vestal (GPL-3.0). Experimental in this firmware."
        if (e.get("warning") or x0x) and notice and not notice.upper().startswith(("WARNING", "NOTICE")):
            notice = ("WARNING: " if e.get("warning") else "") + notice
        bit = BITS.get(sw, NEXT_FREE + extra.index(sw) if sw in extra else NEXT_FREE)
        group = GROUP.get(e.get("group", ""), "System")
        if group not in R.GROUPS:
            R.GROUPS.insert(R.GROUPS.index("UI"), group)
        cost = e.get("cost", {})
        desc = DESC.get(sw) or e.get("title", "")
        if sw not in DESC and cost.get("cpu"):
            desc += f" (CPU: {cost['cpu']})"
        if sw not in DESC and e.get("limits"):
            desc += f". Limits: {e['limits']}"
        parent = PARENT.get(sw)
        if parent:
            group = R.ITEMS[parent].group
        it = R.Item(key, sw, LABEL.get(sw, e.get("title", key)[:48]), group, bit, default=int(e.get("default", 0)),
                    desc=desc, provenance=prov, notice=notice, parent=parent,
                    experimental=bool(e.get("experimental", False) or (x0x and e.get("warning") and not e.get("default"))))
        R.add_item(it)
        if sw in AFTER and parent and AFTER[sw] in R.ITEMS[parent].children:
            ch = R.ITEMS[parent].children
            ch.remove(key)
            ch.insert(ch.index(AFTER[sw]) + 1, key)
        items.append(it)
    return items


ITEMS = load()
