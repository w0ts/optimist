# SPDX-License-Identifier: GPL-3.0-only
"""The firmware builder's registry: every item a build may leave out, its switch, its options, where it came from.

One source of truth for tools/builder/configure.py (the .config -> build/gen/felucca_config.h), the budget
(costs.json, measured by tools/builder/measure_costs.py) and the menu (tools/menuconfig). The firmware side of
each switch is in firmware/src/core/registry.h (engines, kits, FX, FM6 options, features) or in the existing flags.

Rules (docs/BUILDER.md):
  - one level: a top-level item and its own options; an option never depends on another option or on another
    top-level item; options are ignored while their parent is off. A group's head (Item.head: DRUMS) is the one
    exception: the group's items are its options, each with its own options (two levels), all left out with it;
  - "bit" numbers are stable (the BUILD SysEx reports them): append only, never reuse;
  - items backported from another project carry provenance (project, author, licence, commit, url); an item
    from X0X (charlesvestal/fm1-x0x, GPL-3.0-only) shows its notice in the menu when selected. Anything X0X
    uses only by its author's permission (its break player, dsp/breaks*: no licence) is never offered.
"""
from dataclasses import dataclass, field
from typing import Optional

X0X_URL = "https://github.com/charlesvestal/fm1-x0x"
MELODEE_URL = "https://github.com/keremimo/melodee"
FELUCCA_URL = "https://github.com/hugelton/Felucca"

# Items that became others: a .config naming one is read as these (configure.py parse)
MIGRATE = {
    "OTA": lambda v: {"EDITOR": int(v != 0)},   # (2026-10-17: the editor alone; updates and the USB rescue always built)
    "REVERB": lambda v: {"REV_ROOM": int(v == 0), "REV_PLATE": int(v == 1), "REV_FDN8": int(v == 2)},   # the one-tank choice
    "USB_MODE": lambda v: {"USB_CONSOLE": int(v == 1), "USB_AUDIO": int(v == 2)},   # (2026-10-20: two checkboxes; bit 47 unused)
}

# Items merged into another: a .config (or --set) naming one is read as the survivor, turned ON if either was on
# (configure.py parse); the old key is warned about once
MERGED = {
    "MIDI_CLKOUT": ("MIDI_CLOCK", "MIDI clock in and out are one item, MIDI_CLOCK (merged 2026-10-15; on if either was on)"),
    "ED_FXP": ("ED_MIX", "the web editor's mixer is one item, ED_MIX (merged 2026-10-19; on if any of ED_FXP, ED_DLAN, ED_SOLO was on)"),
    "ED_DLAN": ("ED_MIX", "the web editor's mixer is one item, ED_MIX (merged 2026-10-19; on if any of ED_FXP, ED_DLAN, ED_SOLO was on)"),
    "ED_SOLO": ("ED_MIX", "the web editor's mixer is one item, ED_MIX (merged 2026-10-19; on if any of ED_FXP, ED_DLAN, ED_SOLO was on)"),
    "ED_STEPLANE": ("ED_SEQ", "the web editor's sequencer is one item, ED_SEQ (merged 2026-10-19; on if any of ED_STEPLANE, ED_TOOLS, ED_SONG was on)"),
    "ED_TOOLS": ("ED_SEQ", "the web editor's sequencer is one item, ED_SEQ (merged 2026-10-19; on if any of ED_STEPLANE, ED_TOOLS, ED_SONG was on)"),
    "ED_SONG": ("ED_SEQ", "the web editor's sequencer is one item, ED_SEQ (merged 2026-10-19; on if any of ED_STEPLANE, ED_TOOLS, ED_SONG was on)"),
    "ED_SCRS": ("ED_SCREENS", "the web editor's device screens are one item, ED_SCREENS (merged 2026-10-19; on if any of ED_SCRS, ED_SCRN, ED_SCOPE was on)"),
    "ED_SCRN": ("ED_SCREENS", "the web editor's device screens are one item, ED_SCREENS (merged 2026-10-19; on if any of ED_SCRS, ED_SCRN, ED_SCOPE was on)"),
    "ED_SCOPE": ("ED_SCREENS", "the web editor's device screens are one item, ED_SCREENS (merged 2026-10-19; on if any of ED_SCRS, ED_SCRN, ED_SCOPE was on)"),
}

# Dropped items: a .config naming one still loads; the setting is ignored with a warning (configure.py parse)
RETIRED = {
    "MIX_ROUND": "a voice into its part and a part into the mix are always one product rounded to the nearest, no floor: clean fades at MASTER max (issue 132; retired 2026-10-31, bit 444, the day it came; the user: \"no need for an option. Bug fix is permanent\"); any old MIX_ROUND value is ignored",
    "BASSPLUS": "BASS+ (Felucca 1.0, Leo Kuroshita) is always built: SYSTEM's speaker low cut always offers it (retired 2026-10-29, bit 73; the user: \"remove the entry from the builder config now and keep it working automatically\")",
    "FAR_OK": "the fake SD mode layout (LAYOUT 1) always has its whole slot; the far probe's answer is no longer asked for (retired 2026-10-29, bit 413; the user: \"drop the far probe, we don't need it\")",
    "KEYLIT": "the keys always light the notes the selected track plays (retired 2026-10-29, bit 65; the user: \"this flag just does not make sense: remove\"; the settings record keeps a NOTES-off bit that no menu line sets yet)",
    "LIGHTS": "the button and key glow (SYSTEM > KEYS: BUTTONS, KEYS) is always built (retired 2026-10-29, bit 106; the user: \"this flag just does not make sense: remove\")",
    "BRIGHT": "the screen's backlight is always full: the BRIGHT setting is gone (retired 2026-10-29, bit 74; the user: \"it is at the max, and we leave it like that\")",
    "SL24_IMPORT": "Optimist no longer imports SLOOP projects (LOAD twice; removed 2026-10-29, bit 163, never reuse it)",
    "SL24_EXPORT": "Optimist no longer exports a project for SLOOP (web editor; removed 2026-10-29, bit 164, never reuse it)",
    "SL24_EDIMPORT": "Optimist no longer imports a SLOOP song or backup in the web editor (removed 2026-10-29, bit 181, never reuse it)",
    "RESERVE_UNDO_KB": "the builder's reserve entries were useless and not working (retired 2026-10-21, bit 59: the user: \"remove the reserve entry from console and web builder\"); the budget shows the undo ring as information",
    "RESERVE_FLASH_KB": "the builder's reserve entries were useless and not working (retired 2026-10-21, bit 60)",
    "PAGE_LINES": "the screens of rows with no picture are always GLOBAL's four lines, not 2x2 cards over a row list (retired 2026-10-21, bit 342; the user: \"remove the entry of the builder and turn it on by default\")",
    "DRUM_ACCENT": "the drum track's ACCENT lane is always built, on (retired 2026-10-20, bit 340; the user: \"drum accent does not need to be a builder choice. on by default\"); without the automation store the firmware still leaves it out, the lane being its events",
    "BT_INQ_PROBE": "the Bluetooth Classic inquiry probe was dropped 2026-10-20 (bit 280 stays unused); its stack is BT_AUDIO's, and with USB_MODE 1 so are the console's btinq / btpage",
    "UI": "the Optimist UI is the only user interface: SLOOP's UI was removed (retired 2026-10-19, bit 251; any old UI value is ignored)",
    "ICONS": "the parameter icons were SLOOP's UI's (retired 2026-10-19 with it, bit 52)",
    "OVERVIEW": "VIEW ALL was SLOOP's UI's (retired 2026-10-19 with it, bit 53)",
    "OV_ARP": "the ARP graph and ARP in VIEW ALL were SLOOP's UI's (retired 2026-10-19 with it, bit 82)",
    "DRUM_STEP": "the drum steps on the keys of SLOOP's DRUMS grid went with SLOOP's UI (retired 2026-10-19, bit 154; the Optimist UI's drum keys are always its steps)",
    "CHORD_NAMES": "the chord names on SLOOP's STEP page went with SLOOP's UI (retired 2026-10-19, bit 160)",
    "BPM_LOCK": "BPM LOCK (SELECT the tempo only with GLO) was SLOOP's UI's (retired 2026-10-19 with it, bit 132)",
    "MOTION_MARK": "the marks on the cards motion moves were SLOOP's UI's (retired 2026-10-19 with it, bit 134)",
    "SEL_PAGES": "SELECT turning a family's pages was SLOOP's UI's (retired 2026-10-19 with it, bit 215)",
    "BIGVALS": "the big values on pages without a graph were SLOOP's UI's (retired 2026-10-19 with it, bit 217)",
    "ED_UNDO": "the web editor's undo and redo are part of live editing, ED_LIVE (retired 2026-10-19, bit 158)",
    "ED_FAVS": "the web editor's favorites are part of live editing, ED_LIVE (retired 2026-10-19, bit 156; with FAVORITES)",
    "ED_NOTE": "playing notes from the web editor is part of live editing, ED_LIVE (retired 2026-10-19, bit 159)",
    "ED_REC": "the web editor's REC key is gone, with its transport and meters (retired 2026-10-19, bit 168)",
    "INS_EQ3": "the EQ3 insert is always built (retired 2026-10-18, bit 143; any old INS_EQ3 value is ignored)",
    "SLOT_AUTO": "locks and motion on an insert's own values (EQ3's bands) are always built (retired 2026-10-18, bit 351)",
    "INS_WRAP": "the WRAP insert (a wavewrapper) was dropped the day it came, 2026-10-18; its bit 344 and type id 20 stay unused",
    "KEYS_DMA": "the key scan by SPI2's DMA failed on the FM-1, 2026-10-18 (SPI2 never reports done: no key is read, "
                "the UI looks frozen); dropped 2026-10-19, its bit 63 stays unused; the keys are bit-banged",
    "SPLASH": "the dinghy boot logo is always built (retired 2026-10-15: no ring-only logo, no off; any old SPLASH value is ignored)",
    "IDLE": "the CPU sleeping between main-loop polls is always built (retired 2026-10)",
    "SIZE": "the UI, stores and editor are always built for size (minsize; retired 2026-10, no -Os-everywhere choice)",
    "SIMD": "SIMD: wrong on the real FM-1, 2026-10-12 (the packed 16-bit sine and swarm maths crackle on the device, "
            "clean with it off); no longer offered while it is investigated, every build is built without it",
    "MONO_RELEASE": "no stuck note after a VOICE change is always built (retired 2026-10-14: SLOOP 2.3 d691ba7, unchanged in 2.6 and Felucca 1.6)",
    "ST_STRICT": "the stricter checks of what is read back from flash are always built (retired 2026-10-14: SLOOP 2.3 d691ba7, unchanged in 2.6 and Felucca 1.6)",
    "USB_FLOW": "USB MIDI in flow control (NAK, malformed ignored) is always built (retired 2026-10-14: SLOOP 2.3 d691ba7, unchanged in 2.6 and Felucca 1.6)",
    "SHED_FADE": "the overload fade (one voice at a time, never the bass or the lead) is always built (retired 2026-10-14: SLOOP 2.3 d691ba7, unchanged in 2.6)",
    "KEYS_FAST": "the keys are always debounced as their column is read (retired 2026-10-14: SLOOP 2.3 d691ba7, unchanged in 2.6 and Felucca 1.6)",
    "TRS_NOISE": "TRS MIDI in past line noise (an FD at the reader) is always built (retired 2026-10-14: SLOOP 2.3 d691ba7, unchanged in 2.6)",
    "BK_CHECK": "a restore always writes an object only if a load would take it (retired 2026-10-14: SLOOP 2.3 d691ba7, unchanged in 2.6)",
    "LAYER_QUIET": "the knobs are always quiet as a layer button is let go (retired 2026-10-14: Felucca 1.0.2 db70550 #39, unchanged in 1.6 and SLOOP 2.6)",
    "GLIDE": "the mixer gains (part level, pan and sends, MASTER, the drum track's level and pan) always glide over "
             "~10 ms (retired 2026-10-19: X0X 49b1fc8; the drum lanes' sends no longer glide)",
    "DLY_HALVE": "a delay time longer than the line always halves, on the beat (retired 2026-10-14: X0X 892a3b5, unchanged in X0X 1.0.6)",
    "UA_RESAMPLE": "the USB audio choice of USB_MODE always resamples the capture to the host's clock (retired 2026-10-15; "
                   "USB_MODE=2 now means USB audio + MIDI, resampled)",
    "LFO2": "LFO 2, a second LFO on each synth track, is always built (retired 2026-10-16: no off; any old LFO2 value is ignored)",
    "FM6_MARK1": "FM6's MARK I mode is gone (retired 2026-10-18, bit 10: FM6 plays Dexed's MODERN only; an old ENGINE setting plays it)",
    "FM6_MODERN": "FM6's MODERN is its only synthesis (retired 2026-10-18, bit 11; no ENGINE choice, nothing to leave out)",
    "FM6_OPL": "FM6's OPL mode is gone (retired 2026-10-18, bit 12: FM6 plays Dexed's MODERN only; an old ENGINE setting plays it)",
    "FM6_MKI_FLASH": "FM6's MARK I tables went with the mode (retired 2026-10-18, bit 89)",
    "FM6_VOICES": "the 16 factory voices R01..R16 are replaced by the voice packs FM6_PK_* (retired 2026-10-18, bit 15; INIT VOICE is always built)",
    "FM6_STORE": "the FM6 user bank U01..U32 and STORE are gone (retired 2026-10-18, bit 16: SAVE AS in PRESETS keeps a voice, with UP_FM6)",
    "FM6_ALL": "FM6's operator editor in VIEW ALL is gone (retired 2026-10-18, bit 80)",
    "FM6_ALGO": "FM6's algorithm full screen is gone (retired 2026-10-18, bit 81)",
    "VIS": "the SLOOP 2.4 visualiser was dropped from the builder for now (2026-10-10); HOME on TRACKS flips the mixer dial page",
    "TAB_IN_POOL": "the sound tables, FM6's buffers and the card's stream tables are always in the pool, the RAM being one packed "
                   "region (retired 2026-10-29, bit 300; the user: \"always on\"; any old TAB_IN_POOL value is ignored)",
    "VOICE_OWN": "at the voice budget a part always restarts its own released voice before fading another part's (retired 2026-10-29, bit 369; the user: \"default on: remove it\"; any old VOICE_OWN value is ignored)",
    "DLY_DOT": "the delay TIME's 1/8D and 1/16D (SLOOP 2.4, appended values, stored numbers unchanged) are always built (retired 2026-10-29, backport bit 176; any old DLY_DOT value is ignored)",
    "CHO_EXACT": "the chorus send's read points are always ramped exactly over a block (fx.c CHO_RAMP; retired 2026-10-29, bit 425; the user: \"by default, remove the option\"; any old CHO_EXACT value is ignored)",
    "JUNO_FINE": "JUNO CHORUS's line is always 32 bits, 4 KB of the effects memory an instance (retired 2026-10-29, bit 429; the user: \"always on\"; any old JUNO_FINE value is ignored)",
    "DUAL": "the second CPU core renders parts 2-3 in every build for the FM-1 (retired 2026-10-29, bit 58; the user: \"remove, always on\"; any old DUAL value is ignored, a DUAL=0 line builds the same as DUAL=2; the sim and the host tests keep one core)",
    "STOCK_SUPPLY": "the core rails are raised to the FM-1's stock values at start-up in every build for the FM-1 (retired 2026-10-29, bit 62; any old STOCK_SUPPLY value is ignored)",
    "REV_HALF": "the half-rate reverb (the ROOM at 22.05 kHz, a half-size ring for PLATE / FDN8 / VTINY) was dropped (2026-10-29, bit 123; the user: \"drop those\"): an old .config with REV_HALF=1 builds the full-rate reverb",
    "REV_HB_LONG": "the steep 63-tap filter of the half-rate reverbs was dropped (2026-10-29, bit 430; the user: \"drop those\"): the reverbs keep the 15-tap half-band",
    "LCD_DIRTY": "the dirty-rectangle screen update (cv_blit_dirty, after X0X 201b95c) had no caller since the old graph strip went with f1ab07ed (2026-10-19), so the item changed nothing on the screen; the code is removed (retired 2026-10-31, bit 68, never reuse it; any old LCD_DIRTY value is ignored)",
    "BT_AFH": "Bluetooth audio always hops adaptively, the channels the headset fails on skipped (bt/bt_afh.c; retired 2026-10-29, bit 395; the user: \"always on\"; any old BT_AFH value is ignored)",
    "BLE_DIAG": "the BLE diagnostics (the console's blell, the link layer's counters and rings) were removed (retired 2026-10-29, bit 253; the user: \"we can remove the diagnostics\"): BLE works the same without them; any old BLE_DIAG value is ignored",
    "BT_DIAG": "the Bluetooth audio diagnostics (the console's btinq / btscan / btpage / btclk / btrx / bttrace, the status histograms, receive logs and per-second counters) were removed (retired 2026-10-29, bit 363; the user: \"we can remove the diagnostics\"): the audio works the same without them; any old BT_DIAG value is ignored",
    "INS_CHORUS": "the chorus insert CHRS is the chorus itself: CHO goes in any track's or the master's insert slot wherever FX_CHORUS is built, with the send's RATE and DEPTH and a dry/wet MIX (retired 2026-10-29, bit 270; the user: \"can't they be just one effect like any other? why complexify just for these two?\"); an old project's CHRS loads as CHO; any old INS_CHORUS value is ignored",
    "INS_REVERB": "the reverb insert ROOM is the reverb itself: REV goes in any track's or the master's insert slot wherever FX_REVERB is built, the ROOM tank with the send's SIZE and DAMP and a dry/wet MIX (retired 2026-10-29, bit 440, the day it came); an old project's ROOM loads as REV; any old INS_REVERB value is ignored",
    "INS_TDELAY": "the delay insert TDLY is the delay itself: DLY goes in any track's or the master's insert slot wherever FX_DELAY is built, with the send's TIME, FDBK and filter and a dry/wet MIX (retired 2026-10-29, bit 441, the day it came); an old project's TDLY loads as DLY; any old INS_TDELAY value is ignored",
    "DLY_ARENA": "the effects memory (the delay send's line and the limited inserts' lines) is always sized automatically: the delay's line and one line of each limited insert type built, in the arena (ARENA on) and as the fixed memory (off) alike (retired 2026-10-29, bit 378; the user: \"remove the item, always auto\"): the number of an old DLY_ARENA setting is ignored",
    "OVERLAY": "the engines' fast code is no longer loaded per part (the RAMTEXT overlays, bit 412, retired 2026-10-30: the packed SRAM runs all its code at one speed and gives what is free to the arena, the slots cost each engine's code three times in flash, and the release build never linked them; the user: \"retire it\")",
    "KNOB_ONEREST": "the knobs are always read with Felucca 1.6's decoder (one rest state a detent, fast turns counted; "
                    "retired 2026-10-14, released in Felucca 1.1.5 #126 and unchanged since)",
}

# Retired items every image is still built as (their firmware default): the configuration hash counts them at that
# value (configure.py cfg_hash), so retiring one changes no image's hash. SIMD was default 0 in every profile; LFO2 is
# counted ON (the image every build is now; a build that had it off is a different image).
# items that were a count before the effects memory (2026-10-23) and are built or not since: a .config's count reads as
# built (core.py parse: any non-zero is 1)
WAS_COUNT = frozenset({"INS_DELAY", "INS_JCHORUS", "INS_FLANGER", "INS_XDLY",
                       "ARENA"})   # (ARENA: its KB until 2026-10-29, the free RAM since: on)

ALWAYS_ON_TARGET = {"FELUCCA_DUAL": 2, "FELUCCA_STOCK_SUPPLY": 1}   # (target builds only, no item: system/dual.c, system/supply.c; 2026-10-29)
HASH_AS = {"BASSPLUS": 1, "KEYLIT": 1, "LIGHTS": 1, "BRIGHT": 0, "SIMD": 0, "KNOB_ONEREST": 1, "MONO_RELEASE": 1, "ST_STRICT": 1, "USB_FLOW": 1, "SHED_FADE": 1, "KEYS_FAST": 1, "TRS_NOISE": 1, "BK_CHECK": 1, "LAYER_QUIET": 1, "DLY_HALVE": 1, "LFO2": 1, "GLIDE": 1, "UI": 1, "ICONS": 0, "OVERVIEW": 0, "OV_ARP": 1, "DRUM_STEP": 1, "CHORD_NAMES": 1, "BPM_LOCK": 0, "MOTION_MARK": 1, "SEL_PAGES": 0, "BIGVALS": 0, "DRUM_ACCENT": 1, "PAGE_LINES": 1, "TAB_IN_POOL": 1, "VOICE_OWN": 1, "DLY_DOT": 1, "CHO_EXACT": 1, "JUNO_FINE": 1, "DUAL": 2, "STOCK_SUPPLY": 1, "REV_HALF": 0, "REV_HB_LONG": 0, "BT_AFH": 1, "LCD_DIRTY": 1, "BLE_DIAG": 0, "BT_DIAG": 0, "OVERLAY": 0}   # (each was its default in every profile)

# Never offered: used by X0X only by permission of its author, no licence
FORBIDDEN = {
    "X0X_BREAKS": "X0X's break player (dsp/breaks*, BB Gen): used by X0X by permission only, no licence",
}


@dataclass(frozen=True)
class Provenance:
    project: str
    author: str
    licence: str
    commit: str = ""
    url: str = ""

    def line(self):
        c = f" @ {self.commit}" if self.commit else ""
        return f"{self.project}{c} by {self.author} ({self.licence})"


def x0x(commit, url=X0X_URL):
    return Provenance("X0X (charlesvestal/fm1-x0x)", "Charles Vestal", "GPL-3.0-only", commit, url)


MELODEE_FM6 = Provenance("Melodee (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "e459da5", MELODEE_URL)   # (the port, 958569b7; ebfe60e is FM6's first commit)
MELODEE_MIDI = Provenance("Melodee (keremimo/melodee)", "ChanceTheMaker, Kerem Kilic", "GPL-3.0-only", "670193c",
                          MELODEE_URL)
MELODEE_USB = Provenance("Melodee (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "", MELODEE_URL)
FELUCCA = Provenance("Felucca (hugelton/Felucca)", "Leo Kuroshita (Hügelton)", "GPL-3.0-only", "", FELUCCA_URL)
PR45 = Provenance("SLOOP 8-track PR (isod89/sloop-fm1 #45)", "Erick Buendia Barrientos (Erbubar23)", "GPL-3.0-only",
                  "8d9623f", "https://github.com/isod89/sloop-fm1/pull/45")
SLOOP_24 = Provenance("SLOOP 2.4 (isod89/sloop-fm1)", "isod89", "GPL-3.0-only", "v2.4 8d3823f", "https://github.com/isod89/sloop-fm1")
SLOOP_28 = Provenance("SLOOP 2.8 (isod89/sloop-fm1)", "isod89", "GPL-3.0-only", "b77bf55", "https://github.com/isod89/sloop-fm1")
FLOWSTATE_GUARD = Provenance("Flowstate (zakariachowdhury/flowstate-fm1)", "Zakaria Chowdhury", "GPL-3.0-only", "e62e186",
                             "https://github.com/zakariachowdhury/flowstate-fm1")   # (guard.c's last change)


@dataclass
class Item:
    key: str                         # registry name (the .config key)
    flag: str                        # C macro (FELUCCA_*); "" for generator-only items
    label: str
    group: str
    bit: int                         # stable bit in the BUILD report
    default: int = 1                 # bool: 0/1; choice: the default value
    parent: Optional[str] = None     # one level only
    choices: tuple = ()              # ((value, label), ...) for a sized/choice item
    desc: str = ""
    experimental: bool = False
    provenance: Optional[Provenance] = None
    notice: str = ""                 # shown when selected (X0X items: required)
    notice_on_change: bool = False   # the notice also when changed back to its default (MEMORY: any change moves the layout)
    off_warning: str = ""            # shown when switched off
    env: str = ""                    # generator environment (sample sets: FELUCCA_SAMPLES_SKIP)
    target_only: bool = False        # only for the target build (the host tests keep their own default)
    symbols: tuple = ()              # ELF symbols that must be gone (or <= 4 B) when the item is off
    tested: str = "emulator-tested only"   # what an EXPERIMENTAL item has been tested on (the warning says it)
    fixed: bool = False              # always on: shown (greyed), never changed (USB MIDI: every build has it)
    head: bool = False               # the head of its group (DRUMS): the group's items are its options, two levels
    no_image: bool = False           # a build setting, not firmware: nothing in the image (not in the hash or the BUILD
                                     # bits), its costs.json deltas are 0 (written, never built)
    hidden: bool = False             # a real item the menu and the web builder do not list (a front sets it; configs and
                                     # profiles keep loading and dumping it, the checks still name it)
    shown_under: Optional[str] = None   # listed under this item instead of its parent (display only: core.built goes by parent)
    dim_when: Optional[str] = None   # greyed (not live) while this item is on (SAMPLE SLOTS, PROJECTS under SD card mode)
    children: list = field(default_factory=list)
    menu_children: list = field(default_factory=list)   # what the menu lists under it: children + shown_under, less hidden

    @property
    def is_choice(self):
        return bool(self.choices)


# The FX in one group (the user, 2026-10-29: "one FX group, no any number / limited instances split"; two since
# 2026-10-13). Each effect's label starts with where it goes: [send+insert] the chorus, the delay and the reverb (a send
# on the master's returns, and the same type as a track's or the master's insert: CHO, DLY, REV), [master] what runs on the whole
# mix only (PUNCH, DUCK, the returns' feedback sends), [insert] every other effect (a track's or the master's insert
# slots). A sub-item is an option of the item above it; the mix's resolution options are no effect: no tag.
FX = "FX"

MEM = "Memory management"
GROUPS = [MEM, "Reserve", "Synth engines", "Drums", "Sample sets", FX, "Sequencer", "Connectivity", "UI", "System"]

LEGACY_GROUPS = ("USB & BT & MIDI",   # (a group renamed: the old .config files still carry its "# header")
                 "FX: any number (each track its own)", "FX: limited instances (once, or a set count)")   # (FX, 2026-10-29)

_ITEMS = []


def _add(*a, **k):
    it = Item(*a, **k)
    _ITEMS.append(it)
    return it


# ---- engines (UID = FUN7 number, firmware/src/core/registry.h ENGINE_LIST)
# A tracks whose engine a build leaves out plays a stand-in engine (ANALOG 2 for most) and the MISSING warning
# names it; the project keeps the original engine and its values (docs/BUILDER.md).
E = "Synth engines"
_add("ENG_ANALOG", "FELUCCA_ENG_ANALOG", "ANALOG 2 (analog-style synth, swarm, 2 filters)", E, 0,
     desc="The main subtractive synth: two oscillators with hard sync, a swarm of up to 6 detuned copies (supersaw), "
          "two filters. About 6 KB of flash and 4.7 KB of fast RAM code; it is also the stand-in that most other "
          "engines fall back to, so keep it unless you need the space.")
# ANALOG 2's filter models (engines/analog/a2_models.h, 2026-10-22): FLT 2 > MODEL, a builder item each (one stored value with
# FTYP: a sound asking for a model not built plays the SVF in the same mode)
_A2M_DESC = (" FLT 2 gets a MODEL card next to FTYP (LP12 LP24 BP HP NOTCH), on an ANALOG track only. A sound that asks for "
             "a model this build leaves out plays ANALOG 2's own filter in the same mode. Floating point (the FPU), about "
             "3x the CPU of ANALOG 2's own filter on the host per voice, not measured on the FM-1 yet.")
_add("A2_LADR", "FELUCCA_A2_LADR", "ANALOG 2 filter: LADR (transistor ladder, Moog-style)", E, 382, parent="ENG_ANALOG", default=0,
     tested="on the FM-1: in use, working (the user, 2026-10-25); host-tested (responses, self-oscillation, stability)",
     desc="A 4-pole transistor ladder: a tanh at its input (drive grits it), the bass thinning as RES rises, ringing "
          "in tune at the top of RES. FTYP as the Oberheim Xpander's mixes of its four stages." + _A2M_DESC)
_add("A2_OTA", "FELUCCA_A2_OTA", "ANALOG 2 filter: OTA (IR3109 4-pole, Juno-style)", E, 383, parent="ENG_ANALOG", default=0,
     tested="on the FM-1: in use, working (the user, 2026-10-25); host-tested (responses, self-oscillation, stability)",
     desc="A 4-pole OTA ladder: the input stays clean and the passband is made up as RES rises, the resonance "
          "saturates on its own path: creamy, its self-oscillation round. FTYP as the Xpander's mixes." + _A2M_DESC)
_add("A2_DIOD", "FELUCCA_A2_DIOD", "ANALOG 2 filter: DIOD (diode ladder, 303-style)", E, 384, parent="ENG_ANALOG", default=0,
     tested="on the FM-1: in use, working (the user, 2026-10-25); host-tested (responses, self-oscillation, stability)",
     desc="A diode ladder: four coupled stages, each loading the one before, solved exactly every 16 samples, a "
          "tanh at its input: squelchy, a thin vocal resonance that rings at the cutoff." + _A2M_DESC)
_add("A2_MS20", "FELUCCA_A2_MS20", "ANALOG 2 filter: MS20 (Sallen-Key, MS-20-style)", E, 385, parent="ENG_ANALOG", default=0,
     tested="on the FM-1: in use, working (the user, 2026-10-25); host-tested (responses, self-oscillation, stability)",
     desc="A Sallen-Key 2-pole with its band fed back through a tanh: it screams at high RES and rings loud. LP, BP, "
          "HP and NOTCH its own, LP24 a further pole (18 dB)." + _A2M_DESC)
_add("ENG_DIGITAL", "FELUCCA_ENG_DIGITAL", "DIGITAL (4-operator FM)", E, 1,
     desc="Four-operator FM synth: 8 classic algorithms, feedback on operator 4 and one modulation INDEX with its "
          "own envelope. About 2 KB of flash; the stand-in for FM6 when FM6 is left out.")
_add("DIG_TONE", "FELUCCA_DIG_TONE", "DIGITAL: TONE (Digitone-style routings T1..T8)", E, 432, default=0, parent="ENG_DIGITAL",
     desc="DIGITAL's ALG gets eight more routings, T1..T8, in the manner of Elektron's Digitone: a carrier C, its "
          "modulator A and a pair B1 B2, two outputs X and Y that MIX crossfades, feedback on one operator. On them the "
          "EDIT values are C, A and B (the ratios), A LEV, B LEV (B's depth follows the track's ENV), FDBK and MIX; the "
          "classic routings 1..8 sound as before. 6 presets of our own. Integer code; about 2.3 KB of flash.",
     notice="Our routings and code after the Digitone's published design; not affiliated with Elektron. Tested in the "
            "host tests only, not on a device.")
_add("ENG_PHASE", "FELUCCA_ENG_PHASE", "PHASE (phase distortion)", E, 2,
     desc="Phase-distortion synth: bends the phase of a cosine wave to get saw, square, pulse and resonant shapes "
          "with a second line to mix or ring-modulate. About 1.4 KB of flash; the stand-in for CZ.")
FE_FOLD = Provenance("Felucca-Engines (hugelton/Felucca-Engines)", "Leo Kuroshita (Hügelton)", "MIT", "04ffe90",
                     "https://github.com/hugelton/Felucca-Engines")
_add("PHASE_FOLD", "FELUCCA_PHASE_FOLD", "PHASE: FOLD (a sine wavefolder on E7)", E, 371, parent="ENG_PHASE", provenance=FE_FOLD,
     desc="PHASE's last EDIT knob (E7, empty until now) becomes FOLD: a sine wavefolder on the voice, after the two lines "
          "and before SUB. 0 is off (the sound as before); up from 0 a soft saturation, then up to four folds. Above A4 it "
          "folds less, so the top notes alias less. Turning it on or off mid-note crossfades, no click. About 0.8 KB.",
     notice="From Felucca-Engines' PHASE FOLD by Leo Kuroshita (hugelton/Felucca-Engines 04ffe90, MIT). Tested in the "
            "host tests only, not on a device.")
_add("ENG_LOFI", "FELUCCA_ENG_LOFI", "LOFI (chip sounds, wavetable)", E, 3,
     desc="Chip voice: pulse, triangle, saw, noise and a 4-bit wavetable, with a few-bit amplitude, a held (low) "
          "sample rate and a pitch sweep. About 2 KB of flash.")
_add("ENG_SAMPLE", "FELUCCA_ENG_SAMPLE", "SAMPLE (sample sets, USR slots)", E, 4,
     desc="Plays the built-in sample sets and your own samples (USR1..USR3 slots) across the keyboard, with loops. "
          "About 1.4 KB of flash plus the sets you keep (see Sample sets); it is also the stand-in for GRAIN and "
          "SLICE.")
_add("ENG_FORMANT", "FELUCCA_ENG_FORMANT", "VOICE (formant, sung vowels)", E, 5,
     desc="Formant synth that sings vowels from the keyboard (4 voices; BUZZ and BREATH shape the voice). About "
          "2.7 KB of flash; a special sound, leave it out if you need the space.")
_add("ENG_TRIO", "FELUCCA_ENG_TRIO", "TRIO (3 oscillators)", E, 6,
     desc="Three oscillators with ring modulation and hard sync into a multimode filter, in the style of early "
          "8-bit home-computer sound chips. About 3.4 KB of flash.")
_add("ENG_DRAWBAR", "FELUCCA_ENG_DRAWBAR", "WHEEL (drawbar organ)", E, 7,
     desc="Tonewheel-style organ: nine sine drawbars per note, 16 registrations to choose from. About 2.3 KB of "
          "flash and 2.5 KB of RAM.")
_add("ENG_GRAIN", "FELUCCA_ENG_GRAIN", "GRAIN (granular on the sample sets)", E, 8,
     desc="Granular synth: plays clouds of tiny grains cut from the sample sets or your USR slots. About 4.9 KB of "
          "flash and 21 KB of pool; with no sample set built it plays your USR samples only.")
_add("ENG_FM6", "FELUCCA_ENG_FM6", "FM6 (DX7, bit-exact with Dexed)", E, 9, provenance=MELODEE_FM6,
     desc="Six-operator FM that plays Yamaha DX7 voices the way the Dexed plug-in does (MSFA, bit for bit), with its own "
          "operator editor and DX7 SysEx in; INIT VOICE and the voice packs below are its sounds. The biggest engine: "
          "about 28 KB of flash and 6 KB of RAM; its options below trim it down.")
_add("FM6_KEYS", "FELUCCA_FM6_KEYS", "FM6 operator editor on the black keys", E, 13, parent="ENG_FM6",
     desc="Edit a DX7 voice on the device: hold ENV and press a black key to pick an operator, the pitch envelope or "
          "the global page, then turn the knobs. About 6.3 KB of flash; off: a voice can only be replaced by DX7 "
          "SysEx.")
_add("FM6_SYSEX", "FELUCCA_FM6_SYSEX", "FM6 DX7 SysEx in (a voice, a 32-voice bank)", E, 14, parent="ENG_FM6",
     desc="Receives DX7 SysEx over USB MIDI, from Dexed or any DX7 librarian: a single voice replaces the selected FM6 "
          "track's voice (then SAVE AS keeps it as a user preset), a 32-voice bank becomes a pick list at the end of that "
          "track's PRESETS (RAM only, until power-off) to audition and SAVE AS. Nothing is sent back. About 1.3 KB of "
          "flash and 4.3 KB of pool RAM for the frame buffers.")
_add("FM6_PK_KEYS", "FELUCCA_FM6_PK_KEYS", "FM6 voice pack: keys and e-pianos (16)", E, 93, default=0, parent="ENG_FM6",
     desc="16 voices of my own for the FM6 track, listed in its PRESETS after the user presets (K01..K16): electric "
          "pianos, clavs, organs, a harpsichord. About 2.0-2.3 KB of flash.")
_add("FM6_PK_BASS", "FELUCCA_FM6_PK_BASS", "FM6 voice pack: basses (16)", E, 94, default=0, parent="ENG_FM6",
     desc="16 bass voices for the FM6 track, listed in its PRESETS after the user presets (B01..B16): slap, fretless, "
          "synth, pluck and sub basses. About 2.0-2.3 KB of flash.")
_add("FM6_PK_BELLS", "FELUCCA_FM6_PK_BELLS", "FM6 voice pack: bells and mallets (16)", E, 95, default=0, parent="ENG_FM6",
     desc="16 voices for the FM6 track, listed in its PRESETS after the user presets (M01..M16): bells, marimba, vibes, "
          "kalimba, steel drum, glass and chimes. About 2.0-2.3 KB of flash.")
_add("FM6_PK_PADS", "FELUCCA_FM6_PK_PADS", "FM6 voice pack: pads, leads, brass and FX (16)", E, 96, default=0, parent="ENG_FM6",
     desc="16 voices for the FM6 track, listed in its PRESETS after the user presets (P01..P16): pads, leads, brass, "
          "strings and effect sounds. About 2.0-2.3 KB of flash.")
_add("FM6_PK_80S", "FELUCCA_FM6_PK_80S", "FM6 voice pack: 80s mega classics (16)", E, 97, default=0, parent="ENG_FM6",
     desc="16 recreations of the famous 80s DX sounds (the e-piano, the bass, the bells, the brass, the marimba, the "
          "harmonica ...) for the FM6 track, listed in its PRESETS after the user presets (801..816). All programmed "
          "from scratch, none copied from a ROM or a cartridge. About 2.0-2.3 KB of flash.")
MELODEE_012 = Provenance("Melodee (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "v0.12 18bfd53", MELODEE_URL)
_add("NATIVE_BANKS", "FELUCCA_NATIVE_BANKS", "CZ native tone collection in PRESETS", E, 235, default=0,
     provenance=MELODEE_012,
     desc="With CZ built, a CZ track's PRESETS list ends with a collection of 26 CZ-1 tones (the web editor imports "
          "and exports Casio .syx). Loading one sets only the engine's values: the track keeps its FX, mix and pattern. "
          "The collection takes 8 KiB at USR3's end (USR3 holds that much less). About 2 KB of flash with CZ "
          "(measured); no RAM. (The FM6 user bank U01..U32 that this item once listed went 2026-10-18; FM6's own "
          "list is its voice packs and a received DX7 bank, with FM6_SYSEX or a pack.)",
     symbols=("nb_count", "nb_load"))
_add("ENG_SLICE", "FELUCCA_ENG_SLICE", "SLICE (break slicer + its BREAK sample)", E, 17, default=0,
     desc="Break slicer: cuts a loop (the built-in BREAK, 22 KB of samples, or a USR slot) into 4 / 8 / 16 / 32 "
          "slices or one per hit and plays them from the keys or the sequencer. About 27 KB of flash and 6 KB of "
          "RAM; off by default.", provenance=FELUCCA)
CARAVEL = Provenance("Caravel 0.3.3 (bonszaimusic/caravel, a Felucca fork)", "bonszaimusic", "GPL-3.0-only", "c6c61c7",
                     "https://github.com/bonszaimusic/caravel")
_add("ENG_DRONE", "FELUCCA_ENG_DRONE", "DRONE (drones, ambient, binaural beats)", E, 358, default=0,
     desc="A coupled network for drones and ambient (engine 16), after Caravel's DRONE (bonszaimusic), itself "
          "inspired by the LYRA-8: four sines a note, each phase-modulating another (COUPLE) as PAIRS, a CHAIN or a "
          "RING (TOPO), SPREAD from unison to 1 : 5/4 : 3/2 : 2, a soft cubic CHAR, a slow DRIFT and a one-pole "
          "TONE; the track's DTUNE detunes the four. BEAT plays binaural beats: each ear its own pitch, 0.05 to 40 "
          "Hz apart (headphones; the network runs twice). HOLD latches the keys. 9 presets (theta, alpha, delta and "
          "gamma beats among them); one voice a part, about half an 8-note ANALOG part's CPU (host-measured).",
     provenance=CARAVEL)
MI_PLAITS = Provenance("Mutable Instruments Plaits (pichenettes/eurorack)", "Emilie Gillet", "MIT", "f4cfa76",
                       "https://github.com/pichenettes/eurorack")
_add("ENG_PLAITS", "FELUCCA_ENG_PLAITS", "PLAITS (Plaits' waveshaping, additive, granular formant, particles)", E, 379,
     default=0, provenance=MI_PLAITS,
     desc="Four of Mutable Instruments Plaits' synthesis models as one engine (engine 17), with Plaits' own "
          "controls: MODEL, HARMONICS, TIMBRE, MORPH. WAVSH: a slope through five waveshapers and a wavefolder; "
          "ADDTV: 24 harmonics under a moving bump spectrum (AUX: an 8-drawbar organ); GRFMT: two synced formant "
          "grainlets (AUX: the Z oscillator); PRTCL: random pings of six resonant filters (rain, droplets, dust). AUX "
          "fades to each model's second output, TONE and LPG are a low-pass gate, VEL lets the velocity open TIMBRE. "
          "12 presets; 4 voices a part, about twice an ANALOG voice's CPU (host-measured; PRTCL the heaviest). Integer "
          "code, no float. About 11 KB of flash (its tables 3.6 KB), no RAM of its own: its state lives in the part memory the other engines share.",
     notice="Plaits' models by Emilie Gillet (pichenettes/eurorack f4cfa76, MIT), ported to integers. Tested in the "
            "host tests only, not on a device.")
CLIPPER = Provenance("Clipper (isod89/clipper-fm1, Clipper 1.1)", "3dSam", "GPL-3.0-only", "e690672",
                     "https://github.com/isod89/clipper-fm1")
_CLIPPER_NOTE = "From Clipper by 3dSam (isod89/clipper-fm1, GPL-3.0-only). Tested in the host tests only, not on a device."
_add("ENG_STRINGS", "FELUCCA_ENG_STRINGS", "STRINGS (a 1970s string machine)", E, 387, default=0, provenance=CLIPPER,
     desc="Clipper's string machine (engine 18), after the Solina, ARP and Elka ensembles: 16', 8' and 4' registers "
          "divided down from one phase, so they stay locked; a three-line ensemble whose taps two LFOs move 120 degrees "
          "apart (ENS, RATE); SHP turns the saws toward squares; a 12 dB low-pass (CUT, RES). 16 presets. Integer code, "
          "about 2.5 KB of flash.",
     notice=_CLIPPER_NOTE)
_add("ENG_VPS", "FELUCCA_ENG_VPS", "VPS (vector phaseshaping)", E, 388, default=0, provenance=CLIPPER,
     desc="Clipper's vector phaseshaping (engine 19, after Kleimola et al., DAFx 2011): a sine read through a bent "
          "phase. D moves the bend along the period, V sets its height: below 1 a phase-distorted, CZ-like wave, above "
          "1 a formant at V times the note (VENV sweeps it with each note). OSC2 a second one, mirrored and detuned "
          "(DTN), SUB a sine an octave down, then a 12 dB low-pass. 10 presets. Integer code, no table: about 2 KB of "
          "flash.",
     notice=_CLIPPER_NOTE)
_add("ENG_TERRAIN", "FELUCCA_ENG_TERRAIN", "TERRAIN (wave terrain)", E, 389, default=0, provenance=CLIPPER,
     desc="Clipper's wave terrain synthesis (engine 20): an orbit circles over one of 8 surfaces (TERR) once a period "
          "of the note, the surface's height under it is the sound. RAD, X and Y place the orbit, ORB squeezes it to a "
          "line or turns it into a figure eight, DRIFT moves its centre slowly, each note its own way. Then a 12 dB "
          "low-pass. 10 presets. Integer code; about 18 KB of flash, the surfaces 16 KB of it.",
     notice=_CLIPPER_NOTE)
_add("ENG_WSEQ", "FELUCCA_ENG_WSEQ", "WSEQ (wave sequencing)", E, 390, default=0, provenance=CLIPPER,
     desc="Clipper's wave sequencing (engine 21), after the Korg Wavestation: each note plays one of 16 sequences "
          "(SEQ) of up to 16 steps, each its own wave, pitch and level, at the song's tempo (RATE 1/32 to a bar). XFADE "
          "fades each step into the next, LOOP plays forward, back, ping-pong or once, START picks the first step, "
          "PITCH scales the steps' intervals. Then a 12 dB low-pass. 16 presets. Integer code, the waves computed: "
          "about 5 KB of flash.",
     notice=_CLIPPER_NOTE)
_add("ENG_VECTOR", "FELUCCA_ENG_VECTOR", "VECTOR (Prophet VS-style vector synthesis)", E, 391, default=0,
     provenance=CLIPPER,
     desc="Clipper's vector synthesis (engine 22), after the Prophet VS and the Wavestation's joystick: four of 16 "
          "single-cycle waves at the corners of a square (A B C D), a point in it (X, Y) mixing them; MOVE sends the "
          "point round a circle, a figure eight, the square's edge, a sweep from A or a random walk, slow or fast. "
          "Then a 12 dB low-pass. 16 presets. Integer code; about 24 KB of flash, the waves 21 KB of it.",
     notice=_CLIPPER_NOTE)
_add("ENG_WAVE", "FELUCCA_ENG_WAVE", "WAVE (Clipper's wavetables)", E, 394, default=0, provenance=CLIPPER,
     desc="Clipper's wavetable engine (engine 25): 8 of its tables (TABL) of 16 single-cycle frames, POS sweeping "
          "through them crossfaded (PPG / Serum style; the track's SHP moves POS), a second oscillator DTN cents up, a "
          "SUB sine, then a 12 dB low-pass with drive (CUT RES DRV KTR). Without aliasing: the tables are spectra, the "
          "main loop builds one table's band-limited copies in the pool, and every WAVE track plays that one table "
          "(the first WAVE track's TABL). 13 presets. Integer code; about 70 KB of flash (the spectra 64 KB) and 72 KB "
          "of the pool: a build with it needs a shorter delay line.",
     notice=_CLIPPER_NOTE)
MINITONE_SID = Provenance("Mini-Tone 1.0.1 (eldontyrellcorp/Mini-Tone), after reSID 0.16 by Dag Lem", "Stephane Schmitz",
                          "GPL-3.0-only (the reSID data GPL-2.0-or-later)", "6a82562",
                          "https://github.com/eldontyrellcorp/Mini-Tone")
_add("ENG_SID", "FELUCCA_ENG_SID", "SID (the Commodore 64's MOS 6581 / 8580)", E, 392, default=0, provenance=MINITONE_SID,
     desc="Mini-Tone's SID engine (engine 23): one voice of reSID 0.16 in integer code, three a part as the chip has. "
          "TRI SAW PULSE NOISE and the combined waveforms sampled from real chips (WAVE), PW, SYNC and RING against a "
          "second oscillator RATIO semitones away (MOD), the chip's multimode filter (MODE: LP BP HP and their mixes, "
          "CUT on the FC register's curve, RES in its 16 steps) and the 6581 or 8580 (MODEL). 26 presets. Integer code; "
          "about 44 KB of flash, reSID's tables 40 KB of it.",
     notice="SID by Stephane Schmitz (Mini-Tone, GPL-3.0-only) after reSID 0.16 by Dag Lem (GPL-2.0-or-later). Tested in "
            "the host tests only, not on a device.")
MELODEE_P5 = Provenance("Melodee (keremimo/melodee): its PROPHET engine's oscillators, Poly-Mod and Curtis filter",
                        "Kerem Kilic", "GPL-3.0-only", "398ada1", "https://github.com/keremimo/melodee")
_add("ENG_PROPHET", "FELUCCA_ENG_PROPHET", "PROPHET (a Prophet-5 style voice)", E, 393, default=0, provenance=MELODEE_P5,
     desc="A Prophet-5 Rev 4 style voice (engine 24) from Melodee's PROPHET: two oscillators (A: saw, pulse or both, "
          "hard-synced to B or not; B: saw, triangle, pulse and their mixes, B FRQ semitones away), the MIX between "
          "them, Poly-Mod from B (PMOD: on A's pitch, or on the cutoff) and the Curtis 4-pole (CUT, RES), PW for both "
          "pulses. The track's ENV and LFO drive it; 12 presets of our own, no Sequential programs. Five voices. "
          "Floating point (the FPU), its own unit; about 11 KB of flash.",
     notice="The voice is Melodee's (Kerem Kilic, GPL-3.0-only), with our controls. Tested in the host tests only, not "
            "on a device. Not affiliated with Sequential.")
CS1_CS80 = Provenance("CS-1 (twochannels/CS-1): its CS-80 voice's design", "twochannels", "GPL-3.0-only", "5dce8fa",
                      "https://github.com/twochannels/CS-1")
_add("ENG_CS80", "FELUCCA_ENG_CS80", "CS80 (a CS-80 style voice)", E, 431, default=0, provenance=CS1_CS80,
     desc="A Yamaha CS-80 style voice (engine 26) after CS-1's: two channels a voice (I and II), each a saw, pulse, "
          "both, saw with a sine or noise through a high-pass and a low-pass in series (HPF, LPF, RES), the MIX lever "
          "between them, channel II at its own footage (II FT) and detuned by the track's DTUNE, and the ring modulator "
          "(RING). The track's ENV, ENV > FLT, LFO and SHP (the pulse width) drive it; velocity opens the filters. 12 "
          "presets of our own. Eight voices, integer code; about 3.5 KB of flash.",
     notice="After CS-1's CS-80 voice (twochannels, GPL-3.0-only), with our controls; none of CS-1's presets or its "
            "RANDOM model. Tested in the host tests only, not on a device. Not affiliated with Yamaha.")
_add("ENG_STRETCH", "FELUCCA_ENG_STRETCH", "STRETCH (PaulStretch-style stretching)", E, 433, default=0,
     desc="Extreme time stretching of the SAMPLE material (engine 27), in the manner of Paul Nasca's PaulStretch: the "
          "source read through a 4096-sample window, a fixed-point FFT, every bin turned by a random phase (SMEAR), the "
          "spectrum moved to each voice's pitch, one inverse FFT and an overlap-add, a frame every 46 ms. STRCH sets how "
          "many times slower (up to frozen), POS where the scan starts, TONE a spectral low-pass, SUB and SHIM octaves, "
          "BLUR tones into noise bands: a smooth, endless sound from any sample. One part plays it at a time (45 KB of "
          "the pool, once). 8 presets of our own. Integer code; about 6 KB of flash.",
     notice="Our code, after PaulStretch's algorithm (Paul Nasca) and AEther's design (an fm1-base app) as a reference; "
            "none of their code. Tested in the host tests only, not on a device.")
# (bit 91, LFO2, retired 2026-10-16: LFO 2, a second LFO on each synth track, is always built; never reuse the bit)
# the worked examples of CONTRIBUTING.md (2026-10-26): one each of an insert, an engine, note generators; off in every
# profile, every line they add is under their flag (a build without them is the build from before)
_EX_TESTED = "host-tested only (tests/examples_test.c), not on a device"
_EX_NOTE = "An example to copy (CONTRIBUTING.md), not a feature: " + _EX_TESTED + "."
_add("EX_SQUARE", "FELUCCA_EX_SQUARE", "SQUARE: the example engine (a pulse, its sub, a filter)", E, 422, default=0,
     experimental=True, tested=_EX_TESTED, notice=_EX_NOTE, symbols=("sq_render",),
     desc="The worked example of CONTRIBUTING.md for a new synth engine (engines/square/eng_square.c, engine UID 28): a "
          "band-limited pulse (PW, the track's SHP moves it), a square an octave down (SUB), a second pulse detuned up "
          "(DTN), white noise (NOIS), a 12 dB low-pass (CUT, RES, TRK: key tracking) and a soft drive (DRV); three "
          "presets, SQ LEAD, SQ BASS and SQ PLUCK. Integer code. Off in every profile: a build without it is the build "
          "from before.")

# ---- drums: first the drum synths, the kits made by synthesis (any of them is a drum source on its own; the drum
# track needs one of these or a sampled kit), then the sampled drums and the drum options (one group: the user,
# 2026-10-20, "drum synths should be under drums")
D = "Drums"
# Three drum synths, independent of each other (the user, 2026-10-22): the 32 kits, KAWA, PLAITS. Tick any of them; the
# code they share (drum_synth.c's voice, the sound table: registry.h DRUM_DS) is built with any one and counted once
# (measure_costs.py: the pair of all three off; the last one on pays it)
# DRUMS (2026-10-26, the user: "a builder item AND a group, like fm6 and its sub elements", "it removes every code related to
# drums"): the head of the group, every drum item its option. Off: no drum track, no drum code, track 4 a fourth synth part
# (core.h NPART 4); the drum items elsewhere (DRUMS_ONLY) are left out with it
_add("DRUMS", "FELUCCA_DRUMS", "DRUMS: the drum track (off: track 4 is a 4th synth track)", D, 424, head=True,
     desc="The drum track and everything of it: the kits, the drum synths, the lanes, their sounds, sends and FX. Off, "
          "none of that code is built and track 4 is a synth track like tracks 1 to 3 (its own engine, preset, notes "
          "and effects, on MIDI channel 4 by default; the 8 voices are shared by the four parts); the click is a short "
          "blip of its own. Its projects are format FUNC (FUNB's layout with a tail: part 4's FM6 voice, the parts' "
          "values FUNB keeps in the drum track's free slots); a project or song section of a build with drums loads "
          "with its parts as they were and track 4 a new part (WARM PAD), its drum steps and motion left out. A build "
          "with drums does not read FUNC. On the default profile: about 117 KB of flash (99 KB of it the PERC samples) "
          "and 9.8 KB of RAM less; the pool holds about 5 KB more (part 4's state).")
_add("DRUM_SYNTH", "FELUCCA_DRUM_SYNTH", "drum synth: the 32 synthesised kits", D, 18, parent="DRUMS",
     desc="The drum synth's 32 kits (808-style, 909-style and others), generated by the firmware, so they take no "
          "sample memory. One of three drum synths, each on its own: these 32 kits, KAWA, PLAITS. The synth's voice "
          "they share (about 9.6 KB of flash) is built with any one of them and counted once (the last one switched "
          "off frees it): with KAWA or PLAITS on, the 32 kits cost about 1.5 KB of flash, alone about 11 KB. Not the "
          "X0X 909 / 808 kits: those are separate items, with their own samples. The 32 kits also play the sounds an X0X kit lacks (without them, a sampled kit, else "
          "KAWA or PLAITS; with none of those, silent). Without them the kit list starts at the first kit built.")
X0X_DRUMS_NOTICE = ("Ported from X0X by Charles Vestal (GPL-3.0), itself from 9W9 / 8W8 by athousanddetails and ER-99 by "
                    "Matthew Cieplak (GPL-3.0); the 808's rim shot after sc808 (Yoshinosuke Horiuchi / Sam Aaron, MIT). "
                    "Float DSP.")
KAWA_TRK = Provenance("Kawa Tracker (Guizmoh/Kawa-Tracker-Fm1)", "Guizmoh", "GPL-3.0-only", "kawa-0.5",
                      "https://github.com/Guizmoh/Kawa-Tracker-Fm1")
HORTATOR = Provenance("Hortator (DeadActive/hortator), after Mutable Instruments Plaits (Emilie Gillet, MIT)",
                      "Leo Kuroshita (Hügelton), DEADACTIVE; Emilie Gillet", "GPL-3.0-only (Plaits' algorithms MIT)",
                      "7e34159", "https://github.com/DeadActive/hortator")
_add("DS_KAWA", "FELUCCA_DS_KAWA", "drum synth: KAWA kit (kit 47)", D, 372, parent="DRUMS",
     provenance=KAWA_TRK,
     desc="A drum synth of its own, the KAWA kit, after Kawa Tracker's kit engine: a guiro, a clap with its hands, a "
          "flammed snare, a brush and a shaker that swell in, tight-cluster hats, a ride's bell, ring-modulated toms and "
          "cowbell, a kick with an FM beater. Any lane of another kit can borrow its sounds. It adds four cymbal tunings, "
          "the second partial ringing or modulating the tone, re-strikes and a rise to the drum synth, used by this kit "
          "only (the other kits sound as before). Needs neither the 32 kits nor PLAITS: about 2.2 KB beside them, "
          "12 KB alone (the synth's voice with it). A build without it plays the 808 kit for KAWA (without the 32 "
          "kits: the first kit built).",
     notice="From Kawa Tracker's kit engine by Guizmoh (Guizmoh/Kawa-Tracker-Fm1 kawa_drums.c, GPL-3.0). Tested in the "
            "host tests only, not on a device.")
_add("DS_PLAITS", "FELUCCA_DS_PLAITS", "drum synth: PLAITS kit (kit 48), Plaits' kick and snare", D, 373, parent="DRUMS",
     provenance=HORTATOR,
     desc="A drum synth of its own, the PLAITS kit: two kicks and two snares from Mutable Instruments Plaits' synthetic "
          "bass drum and snare drum models (a distorted sine with FM and a click; two FM sines with filtered noise), the "
          "other lanes the 909's sounds. Any lane of another kit can borrow them. The drum EDIT moves them: TUNE, DECAY, "
          "CUT + (their TONE / FM), CLICK (the kick's FM), SNAP (the snare's snap). Integer code with 64-bit products, "
          "heavier on the CPU than the other drum sounds. Needs neither the 32 kits nor KAWA: about 4.6 KB beside "
          "them, 14 KB alone (the synth's voice with it). A build without it plays the 909 kit for PLAITS "
          "(without the 32 kits: the first kit built).",
     notice="Plaits' drum models by Emilie Gillet (MIT), in the integer port of Hortator by Leo Kuroshita and DEADACTIVE "
            "(DeadActive/hortator 7e34159, GPL-3.0). Tested in the host tests only, not on a device.")
_add("DRUM_X0X909", "FELUCCA_DRUM_X909", "X0X 909 kit (circuit-modelled TR-909)", D, 86, parent="DRUMS", default=0,
     provenance=x0x("80b7d40"), notice=X0X_DRUMS_NOTICE,
     desc="X0X's TR-909 drum kit (kit UID 37; the drum models are circuit-modelled, hi-hats, ride and crash are "
          "samples), on the 16 lanes; SHAKER, CONGA, COWBELL play the synthesised 909's. Big: about 151 KB of flash "
          "(see its cymbal option) and uses floating point. A build without it plays the "
          "synthesised 909 for it and keeps the kit")
_add("X909_CYM", "FELUCCA_X909_CYM", "X0X 909: ride and crash sample quality", D, 87, parent="DRUM_X0X909",
     choices=((1, "8-bit (93 KB, 42 dB)"), (2, "6-bit (70 KB, 30 dB)"), (0, "off")),
     desc="8-bit block floating point as before; 6-bit: 22 KB less, 30 dB against the 16-bit source instead of 42 "
          "(screens x0xdrums-perf-2026-10-06); off: RIDE and CRASH play the synthesised 909's (the hi-hat samples stay)")
_add("X909_HAT", "FELUCCA_X909_HAT", "X0X 909: hi-hat sample quality", D, 365, parent="DRUM_X0X909",
     choices=((1, "8-bit (21 KB, 42 dB)"), (2, "6-bit (16 KB, 30 dB)")),
     desc="The closed and open hat samples as 8-bit block floating point as before, or 6-bit (four mantissas in three "
          "bytes, as the ride and crash's 6-bit): about 5 KB less, 30 dB against the 16-bit source instead of 42 "
          "(measured by tools/gen_x0x_drums.py; the 909's own cymbals are 6-bit). No other 909 voice reads a sample")
_add("X0X_TANH", "FELUCCA_X0X_TANH", "X0X 909: saturator curve (tanh) resolution", D, 366, parent="DRUM_X0X909",
     choices=((1, "1024 points (4.1 KB)"), (2, "256 points (1 KB)")),
     desc="The 909's saturators read tanh from a table with linear interpolation: 1024 points as before, or 256 points: "
          "3 KB less, within 1e-4 of tanh (80 dB below the signal), no audible change (tests/x0x_drums_test.c measures it)")
_add("DRUM_X0X808", "FELUCCA_DRUM_X808", "X0X 808 kit (circuit-modelled TR-808)", D, 88, parent="DRUMS", default=0,
     provenance=x0x("80b7d40"), notice=X0X_DRUMS_NOTICE,
     desc="X0X's TR-808 drum kit (kit UID 38; 16 circuit-modelled sounds) on the 16 lanes; MIDI also plays MT, LC, "
          "HC and the claves (note 75). About 30 KB of flash and uses floating point. A build "
          "without it plays the synthesised 808 for it and keeps the kit")

_add("DRUM_SAMPLED", "FELUCCA_DRUM_SAMPLED", "sampled drums (the PERC set, 99 KB)", D, 19, parent="DRUMS", env="PERC",
     desc="The recorded drum samples (the PERC set, about 99 KB of flash) behind the five sampled kits below. "
          "Switching this off drops all of them; the drum track then needs a drum synth.")
_KIT_DESC = {
    "ACOUSTIC": "The sampled kit as recorded (the basic kit of the PERC set).",
    "DEEP": "The sampled kit tuned down and softened by a low-pass filter: a darker, heavier kit.",
    "TIGHT": "The sampled kit with every hit cut short by a fast decay: a dry, punchy kit.",
    "BRIGHT": "The sampled kit tuned two semitones up: a lighter kit.",
    "DUST": "The sampled kit tuned slightly down, filtered and reduced to a coarse bit depth: a gritty lo-fi kit.",
}
for i, (k, n) in enumerate((("ACOUSTIC", "ACOUSTIC"), ("DEEP", "DEEP"), ("TIGHT", "TIGHT"), ("BRIGHT", "BRIGHT"),
                            ("DUST", "DUST"))):
    _add(f"KIT_{k}", f"FELUCCA_KIT_{k}", f"sampled kit {n}", D, 20 + i, parent="DRUM_SAMPLED",
         desc=_KIT_DESC[k] + " It shares the PERC samples with the other sampled kits, so leaving it out saves no "
              "memory (only leaving out all of them does); a project using it plays another kit.")
_add("DRUM_EDIT", "FELUCCA_DRUM_EDIT", "drum sound editor (EDIT on the drum track)", D, 25, parent="DRUMS",
     desc="Per-lane sound tweaks over the kit (TUNE, DECAY, SNAP, CLICK, BEND, CUT, DRIVE, LEVEL on synthesised "
          "kits; TUNE, DECAY, CUT, LEVEL on sampled ones). About 1.2 KB of flash; off: kits play as they are and "
          "saved tweaks are kept but not applied.")
_add("DS_NTUNE", "FELUCCA_DS_NTUNE", "drum EDIT: TUNE on the noise sounds (hats, shaker, clap)", D, 368, parent="DRUM_EDIT",
     provenance=SLOOP_28,
     desc="On a synthesised kit, a sound with no tone (closed hat, shaker, clap) had nothing for TUNE to move. With this, "
          "TUNE moves its filter and its noise high-pass, about a semitone's worth a step, as a resampled sample would "
          "sound. A few dozen bytes.",
     notice="From SLOOP 2.8's drum TUNE by isod89 (isod89/sloop-fm1 b77bf55, GPL-3.0). Tested in the host tests only, "
            "not on a device.")
_add("DRUM_USR", "FELUCCA_DRUM_USR", "user samples on drum lanes", D, 26, parent="DRUMS",
     desc="A drum lane can play one of your own samples (a hit of a USR slot, with start and length) instead of the "
          "kit's sound. About 1.2 KB of flash; off: such lanes play the kit sound.")
_add("DRUM_KITS", "FELUCCA_DRUM_KITS", "user drum kits (bank of 16 in data flash)", D, 27, parent="DRUMS",
     desc="Lane sources from other kits and the X0X machines, and a bank of 16 user kits (each lane's source and "
          "sound tweaks) saved in flash. About 2.8 KB of flash; off: lanes play the kit as it is.")
# (bit 83, DRUM_SENDS, retired 2026-10: each drum lane's REV / DLY / CHO are the drums' only sends, in every build;
#  never reuse the bit)


# ---- sample sets (generator: FELUCCA_SAMPLES_SKIP; set numbers stay)
S = "Sample sets"
_SET_DESC = {
    "PIANO": "A grand piano, plus the dusty and lo-fi piano variants.",
    "BASS": "An upright bass, plus a deep bass variant.",
    "VIBES": "A vibraphone.",
    "HORNS": "Horn stabs.",
    "STRGS": "String stabs.",
    "FLUTE": "A lo-fi flute.",
    "SCRCH": "Turntable scratch hits.",
}
for i, (k, n) in enumerate((("PIANO", "PIANO (Steinway, 44 KB)"), ("BASS", "BASS (39 KB)"), ("VIBES", "VIBES (33 KB)"),
                            ("HORNS", "HORNS (26 KB)"), ("STRGS", "STRINGS (20 KB)"), ("FLUTE", "FLUTE (31 KB)"),
                            ("SCRCH", "SCRATCH (23 KB)"))):
    _add(f"SET_{k}", "", n, S, 28 + i, env=k,
         desc=_SET_DESC[k] + " Played by the SAMPLE and GRAIN engines (and their presets); leaving it out frees its "
              "flash, and it can still be uploaded to a USR slot. In SD card mode no set is in the flash (0 B, ticked or not): a ticked "
              "set goes on the card in the SD card zip (OPTIMIST/SAMPLES/<SET>/) and SAMPLE streams it from there; GRAIN "
              "cannot (its grains jump through the set): GRAIN on a set is silent there, said MISSING.")

# ---- FX
F = "FX"
_add("FX_DIST", "FELUCCA_FX_DIST", "[insert] DIST (per-track drive)", F, 35,
     desc="A distortion on each synth track: low cut, drive from 1x to 8x into an asymmetric soft clip, and a tone "
          "filter that closes as the drive rises. About 0.4 KB of flash; off: the DIST controls disappear.")
_add("FX_CHORUS", "FELUCCA_FX_CHORUS", "[send+insert] chorus", F, 36,
     desc="The chorus effect, fed by each track's CHO send. About 0.4 KB of flash and 4 KB of pool; off: the CHO "
          "sends and the chorus settings disappear." + " In a track's insert slot or the master's (INSERTS > S1 / S2) it is an insert like any other, its amount the dry/wet MIX (0 dry, 127 all wet: the send's return at unity): a track's CHO value is then the MIX and the track sends nothing to the bus. It runs the send's own code with the send's settings (RATE and DEPTH, FX > REV/CHO). Each copy needs a 4 KB line: with ARENA a block of the free RAM each, as many as it holds; without, a copy plays where the effects memory has room: room another insert leaves free, or the chorus's or the delay's line while that send is in no master slot; a slot left without room shows FULL and stays dry.")
_add("FX_DELAY", "FELUCCA_FX_DELAY", "[send+insert] delay", F, 37,
     desc="The tempo-synced delay (echo), fed by each track's DLY send. Costs 128 KB of the pool at the full "
          "length: shorten it with the length option, or leave it out." + " In a track's insert slot or the master's (INSERTS > S1 / S2) it is an insert like any other, its amount the dry/wet MIX (0 dry, 127 all wet: the send's return at unity): a track's DLY value is then the MIX and the track sends nothing to the bus. It runs the send's own code with the send's settings (TIME, FDBK and the filter, FX > DLY and DLY F). Each copy needs a line as long as the delay's (the length below): with ARENA a block of the free RAM each, as many as it holds; without, a copy plays on the delay's own line while the delay is in no master slot, or on room another insert leaves free; a slot left without room shows FULL and stays dry.")
_add("DLY_LEN", "FELUCCA_DLY_LEN", "delay length (how long an echo can be)", F, 38, default=65536, parent="FX_DELAY",
     choices=((65536, "1.49 s (128 KB pool)"), (32768, "0.74 s (64 KB)"), (16384, "0.37 s (32 KB)")),
     desc="The longest delay time, which sets the pool memory the delay takes. A time longer than the line is "
          "halved until it fits, so it stays on the beat; shorter lines suit fast tempos only.")
_add("FX_REVERB", "FELUCCA_FX_REVERB", "[send+insert] reverb", F, 39,
     desc="The reverb, fed by each track's REV send and the drums' reverb, with its algorithms below (tick at least "
          "one). Each one ticked is on the device's FX > REVERB > TYPE and in the web editor's Reverb popup, switched "
          "while it plays; with one ticked there is no TYPE. They share one line buffer, sized to the largest. The "
          "biggest user of RAM among the FX (about 17 KB with ROOM); off: the REV sends and reverb settings disappear."
          + " In a track's insert slot or the master's (INSERTS > S1 / S2) it is an insert like any other, its amount the dry/wet MIX (0 dry, 127 all wet: the send's return at unity): a track's REV value is then the MIX and the track sends nothing to the bus. It runs the send's own code with the send's settings (SIZE and DAMP, FX > REV/CHO; the insert is always the ROOM, whatever TYPE the send plays: about 280 host instructions a sample a copy). Each copy needs 19 KB: with ARENA a block of the free RAM each, as many as it holds; without, a copy plays on the delay's line while the delay is in no master slot, or on room another insert leaves free; a slot left without room shows FULL and stays dry.")
# the reverb's algorithms (firmware/src/fx/reverb/rev_type.c): one checkbox each (bit 150, the one-tank choice REVERB, is retired)
_add("REV_ROOM", "FELUCCA_REV_ROOM", "reverb: ROOM", F, 151, parent="FX_REVERB", symbols=("rev_ap",),
     desc="Four delay lines at 44.1 kHz, the reverb as it always was: sparse for its first ~300 ms (separate echoes, "
          "a grainy start), then the tail. Its 17 KB of lines set the shared buffer's size when it is ticked. Off, "
          "with another algorithm ticked: about 1.8 KB of flash, 2.4 KB of RAM and 2 KB of pool saved.")
_add("REV_PLATE", "FELUCCA_REV_PLATE", "reverb: PLATE (Dattorro)", F, 152, default=0, parent="FX_REVERB",
     desc="Dattorro's figure-of-eight plate at 22.05 kHz: dense from ~50 ms, a smooth decay, nothing above ~11 kHz, "
          "about 10 % fewer instructions than ROOM. A 16 KB ring (8 KB at half rate) in the shared buffer. Beside "
          "ROOM: about 2.9 KB of flash and 3.4 KB of RAM; with two or more algorithms each one's code runs from main "
          "RAM. Experimental: measured on the host and in the emulator only, not yet heard on an FM-1.",
     notice="PLATE: measured on the host and in the emulator only; not yet heard on an FM-1.", symbols=("rvp_params",))
_add("REV_FDN8", "FELUCCA_REV_FDN8", "reverb: FDN8 (long, lush)", F, 153, default=0, parent="FX_REVERB",
     desc="Long and lush: eight slowly modulated lines at 22.05 kHz, dense from ~50 ms, the widest and least ringing "
          "tail, nothing above ~11 kHz. Up to SIZE 90 the ROOM's decay; above it the decay doubles every 10 steps to "
          "~14 s at 126 and a near-freeze at 127, the treble kept as DAMP says; about 20 % more instructions than "
          "ROOM. A 16 KB ring (8 KB at half rate) in the shared buffer. Beside ROOM: about 4.0 KB of flash and 4.3 KB "
          "of RAM (with PLATE too, 1.1 KB of flash and 1.8 KB of RAM are shared). Experimental: measured on the host "
          "and in the emulator only, not yet heard on an FM-1.",
     notice="FDN8: measured on the host and in the emulator only; not yet heard on an FM-1.", symbols=("rvf_params",))
_add("REV_POOL", "FELUCCA_REV_POOL", "reverb: FDN8 size", F, 122, default=0, parent="FX_REVERB",
     choices=((1, "big ring (32 KB): the long, lush tail"), (0, "small ring (16 KB)")),
     desc="FDN8's ring: big (32 KB) gives its eight lines twice the length, the long and lush FDN8; small (16 KB) the "
          "shorter one. The key's old name (the reverb's buffers in the pool) is kept so profiles keep their sound: since "
          "the RAM is one region (2026-10-25) where the buffer sits saves nothing, only the ring's size counts.")
_add("REV_AIRWIN", "FELUCCA_REV_AIRWIN", "reverb: VTINY (Airwindows VerbTiny)", F, 230, default=0, parent="FX_REVERB",
     desc="Airwindows' VerbTiny (Chris Johnson, MIT): sixteen lines in four 4 x 4 Householder stages, left and right "
          "in one loop, at 22.05 kHz. A plain early-digital texture: peakier and grainier than FDN8 (it is not "
          "modulated), dense from ~80 ms, ringing no more than the ROOM; nothing above ~11 kHz. SIZE as the "
          "ROOM's decay up to 90, then FDN8's long top (~11 s at 126, a near-freeze at 127); DAMP the ROOM's treble "
          "loss. Its lines fill the shared 16 KB ring (8 KB at half rate: a smaller room). About 40 % more "
          "instructions than FDN8. Beside ROOM: about 4.5 KB of flash and 6.5 KB of main RAM (the tanks' code runs "
          "from RAM), no pool past the shared ring (user-default has no flash left for it). "
          "Experimental: measured on the host only, not yet heard on an FM-1.",
     notice="VTINY: measured on the host only; not yet heard on an FM-1.", symbols=("rva_params",))
# (bit 123, REV_HALF, retired 2026-10-29: the half-rate reverb (the ROOM and the ring of PLATE / FDN8 / VTINY at half the RAM) was
#  dropped; RETIRED above; never reuse the bit)
# the effects' finer arithmetic (2026-10-26, a Reddit report: the send chorus, JUNO CHORUS, WIDE and the reverbs grainy on
# in-ear headphones, smooth on Felucca): each measured on the host against the old arithmetic, which stays the default
_FINE_TESTED = "host-measured only (a sine through the firmware's own code, 2026-10-26); not yet heard on an FM-1"
# (bit 425, CHO_EXACT, retired 2026-10-29: the chorus send's read points are always ramped exactly over the block
#  (fx.c CHO_RAMP); RETIRED above; never reuse the bit)
_add("MIX_FINE", "FELUCCA_MIX_FINE", "mix: finer part level steps (lower noise floor)", F, 426, default=0,
     experimental=True, tested=_FINE_TESTED,
     desc="Each synth part (and the side of WIDE, JUNO CHORUS and DRONE's BEAT) goes into the mix with 3 bits dropped "
          "before its pan gain instead of 4: the steps half as coarse, the noise floor 5.3 dB lower on every part "
          "(about -65 dB under the signal; a quiet part gains the most). Same headroom (the product stays under "
          "2^31). No flash or RAM to speak of; every sound changes a little, so it is off by default.")
_add("MIX_EXACT", "FELUCCA_MIX_EXACT", "mix: full resolution into the mix and the master (quiet tails)", F, 439,
     default=0, experimental=True, tested="host-measured only (issue 107, 2026-10-28); not yet heard on an FM-1",
     desc="Every synth part reaches the mix as 64-bit products, no bit dropped: its mute x duck gain (at rest 32766, "
          "so the 4 bits down were taken on every block) and its pan gain (4 bits down; MIX_FINE: 3). The master "
          "volume too (2 bits down before). Before, each part reached the mix in steps of 16: a long release or a "
          "reverb tail went through its last few bits, a bitcrusher on headphones. Supersedes MIX_FINE. A few "
          "multiplies a sample more, no RAM; every sound changes a little, so it is off by default.")
# (bit 444, MIX_ROUND, retired 2026-10-31: a voice into its part and a part into the mix are always one rounded product
#  each (dsp.c VOICE_OUT, fx.c mix_lg, MIX_PAN); RETIRED above; never reuse the bit)
_add("WIDE_FINE", "FELUCCA_WIDE_FINE", "WIDE: all-pass at full resolution", F, 427, default=0, parent="INS_WIDTH",
     experimental=True, tested=_FINE_TESTED,
     desc="WIDE's all-pass runs at the part's own scale with 64-bit products instead of 4 bits down (the drum bus's "
          "and the master's: 3): its side, only in the left ear, is no longer rounded to 16 units. Measured: 3.5-4.5 dB "
          "less noise in that ear (about 7 with MIX_FINE). More headroom than before (+-3 M, was +-0.5 M). A few "
          "multiplies a sample more.")
_add("TFLT_FINE", "FELUCCA_TFLT_FINE", "track FILTER: full resolution (quiet high-pass)", F, 428, default=0,
     experimental=True, tested=_FINE_TESTED,
     desc="The track FILTER (and the DJ filter) runs at the samples' own scale with 64-bit products instead of 3 bits "
          "down (the drum bus: 2), with the same headroom. As a high-pass at a low cutoff the old rounding came out of "
          "x - y as loud as a quiet part (FILT +10: -18 dB); measured: 18-20 dB lower. A few dozen bytes of flash; "
          "the filter's multiplies become 64-bit ones.")
# (bit 429, JUNO_FINE, retired 2026-10-29: JUNO CHORUS's line is always 32 bits, 4 KB of the effects memory an
#  instance (fxi_mem.h FXA_Z2); RETIRED above; never reuse the bit)
# (bit 430, REV_HB_LONG, retired 2026-10-29: the steep 63-tap filter of the half-rate reverbs was dropped, the 15-tap half-band
#  stays; RETIRED above; never reuse the bit)
REV_ALGOS =("REV_ROOM", "REV_PLATE", "REV_FDN8", "SPRING", "REV_AIRWIN")   # at least one with FX_REVERB (configure.py validate)
_add("FX_SLICER", "FELUCCA_FX_SLICER", "[insert] SLICER (stutter / gate on each track)", F, 40,
     desc="A tempo-synced 16-step gate or stutter on each track (not the sample slicer engine): chops the sound "
          "to a pattern. About 1.2 KB of flash and 32 KB of pool at the full capture length.")
_add("SL_LEN", "FELUCCA_SL_LEN", "SLICER capture length (stutter memory)", F, 41, default=4096, parent="FX_SLICER",
     choices=((4096, "186 ms (32 KB pool)"), (2048, "93 ms (16 KB)")),
     desc="How much sound each track's stutter records to repeat: the shorter capture saves 16 KB of pool and "
          "limits how long a repeated chunk can be (the gate mode is unaffected).")
_add("FX_PUNCH", "FELUCCA_FX_PUNCH", "[master] PUNCH (16 punch-in FX)", F, 42,
     desc="Hold FX and press a white key to put the whole mix through one of 16 effects while it is held (loops, "
          "reverse, tape stop, half speed, wobble, echo, filters, crush, gate), beat-synced. About 2.3 KB of "
          "flash and 64 KB of pool at the full ring.")
_add("PUNCH_N", "FELUCCA_PUNCH_N", "PUNCH memory length (how much mix it can loop)", F, 43, default=32768, parent="FX_PUNCH",
     choices=((32768, "0.74 s (64 KB pool)"), (16384, "0.37 s (32 KB)")),
     desc="How much of the mix the loop, reverse, tape-stop and echo effects can capture; the shorter ring "
          "saves 32 KB of pool and cuts the longest loop in half.")
_add("FX_DJF", "FELUCCA_FX_DJF", "[insert] DJ filter (MASTER FILT)", F, 44,
     desc="One knob on the master (MASTER > FILT): turn left for a low-pass closing, right for a high-pass opening, "
          "centre is off. About 0.7 KB of flash.")
_add("FX_DUST", "FELUCCA_FX_DUST", "[insert] DUST (vinyl / lo-fi)", F, 45,
     desc="One knob on the master (MASTER > DUST) that runs the mix through an old sampler and a record: drive, "
          "lower sample rate, fewer bits, a darker tone, hiss and crackle while playing. About 0.7 KB of flash.")
_add("FX_DUCK", "FELUCCA_FX_DUCK", "[master] DUCK (kick ducks the parts)", F, 46,
     desc="Pumping sidechain effect (MASTER > DUCK): every kick from the drum track dips the synth parts, which "
          "swell back over an eighth note. About 0.1 KB of flash; has no effect without a drum kick.")
_add("MASTER_COMP", "FELUCCA_MASTER_COMP", "[insert] COMP + LIMIT (DYN: compressor, brickwall limiter)", F, 155,
     desc="A compressor and a brickwall limiter on the master (GLO > COMP: THRS, RATIO, ATK, REL; GLO > LIMIT: "
          "GAIN, CEIL and a GR readout) to glue the mix and keep its peaks under a ceiling; a Comp button and a GR "
          "meter in the web mixer. A new project limits at CEIL -3.0 dB (a look-ahead limiter in place of the old "
          "one); saved projects keep their own CEIL (OFF: as before). About 2 KB of flash and 2.5 KB of RAM.",
     symbols=("mc", "mlim"))
_add("TRK_FILT", "FELUCCA_TRK_FILT", "[insert] track FILTER (LP <- off -> HP on each track)", F, 205, default=0, provenance=SLOOP_24,
     desc="A DJ-style filter on each track: FX > FILTER (one knob, FILT: left low-pass, right high-pass, centre off) and "
          "FX held + KNOB 4 for the selected track. On the drum track it filters the summed drums and all their sends. "
          "Saved with the project, kept when the sound changes, recordable with motion. Costs nothing at the centre; "
          "engaged, one filter per track (five on the drum bus).",
     notice="From SLOOP 2.4's track filter by isod89 (isod89/sloop-fm1 v2.4, GPL-3.0). Tested in the host tests only, "
            "not on a device.")
# the further insert types of the tracks' insert slots (fx/insfx/insfx.c, 2026-10-13): each a type in INSERTS' S1 / S2
# lists, one amount (0 off), on every track (the drum bus: its L and R)
FELUCCA_156 = Provenance("Felucca 1.5.6 (hugelton/Felucca)", "Leo Kuroshita (Hügelton)", "GPL-3.0-only", "9f6a885",
                         FELUCCA_URL)
MELODEE_131 = Provenance("Melodee 1.3.1 (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "next 757b65b", MELODEE_URL)
_INS_NOTE = " Tested in the host tests and the emulator only, not on a device."
_add("TFLT_RESO", "FELUCCA_TFLT_RESO", "track FILTER: resonance setting (GLOBAL > FILTER > FILT RESO)", F, 367, parent="TRK_FILT",
     provenance=SLOOP_28,
     desc="One setting for every track's FILTER (and the drum track's and the drum lanes'): GLOBAL > FILTER > FILT RESO, 0 (clean, "
          "as before) to 10 (a resonant peak at the cutoff, never self-oscillating). Saved with the project. A few hundred "
          "bytes. The master's DJ filter stays clean.",
     notice="From SLOOP 2.8's FILTER RESO by isod89 (isod89/sloop-fm1 b77bf55, GPL-3.0). Tested in the host tests only, "
            "not on a device.")
_add("INS_FOLD", "FELUCCA_INS_FOLD", "[insert] FOLD (wavefolder)", F, 256, default=0, provenance=FELUCCA_156,
     desc="A wavefolder type for the tracks' insert slots (INSERTS > S1 / S2: FOLD): the sound driven past full scale "
          "folds back, more folds and harmonics as FOLD rises; the drive follows the track's level as DIST's does, the "
          "bass stays dry. 0 is off.", symbols=("ifx_fold",),
     notice="From Felucca 1.5.6's track INSERT FOLD by Leo Kuroshita (GPL-3.0)." + _INS_NOTE)
_add("INS_FUZZ", "FELUCCA_INS_FUZZ", "[insert] FUZZ", F, 257, default=0, provenance=FELUCCA_156,
     desc="A fuzz type for the tracks' insert slots (INSERTS > S1 / S2: FUZZ): a hard-clipped top and a soft, halved "
          "bottom (lopsided, buzzy), the drive with FUZZ. 0 is off.", symbols=("ifx_fuzz",),
     notice="From Felucca 1.5.6's track INSERT FUZZ by Leo Kuroshita (GPL-3.0)." + _INS_NOTE)
_add("INS_PHASER", "FELUCCA_INS_PHASER", "[insert] PHSR (phaser)", F, 258, default=0, provenance=FELUCCA_156,
     desc="A phaser type for the tracks' insert slots (INSERTS > S1 / S2: PHSR): four all-pass stages swept slowly "
          "(~0.5 Hz); PHSR up to the middle mixes it in, past the middle adds feedback. 0 is off.",
     symbols=("ifx_phsr",), notice="From Felucca 1.5.6's track INSERT PHASR by Leo Kuroshita (GPL-3.0)." + _INS_NOTE)

_add("INS_FORMANT", "FELUCCA_INS_FORMANT", "[insert] FRMT (vowel filter)", F, 260, default=0, provenance=MELODEE_131,
     desc="A vowel filter type for the tracks' insert slots (INSERTS > S1 / S2: FRMT): three formant peaks over the "
          "sound; FRMT moves the vowel from A (low) through E, I, O to U (high). 0 is off.", symbols=("ifx_frmt",),
     notice="From Melodee 1.3.1's FORMANT insert by Kerem Kilic (GPL-3.0); its vowel table, fixed point here."
            + _INS_NOTE)
_add("INS_WIDTH", "FELUCCA_INS_WIDTH", "[insert] WIDE (stereo width)", F, 261, default=0, provenance=MELODEE_131,
     desc="A stereo width type for the tracks' insert slots (INSERTS > S1 / S2: WIDE): one side through slowly swept "
          "all-pass filters, the other dry, so a centred mono track spreads out; WIDE sets how much. 0 is off.",
     symbols=("ifx_wide",), notice="From Melodee 1.3.1's WIDTH insert by Kerem Kilic (GPL-3.0)." + _INS_NOTE)
# the shapers (2026-10-18, fx/insfx/insfx.c ifx_drive ifx_mod): FOLD's state (24 B an instance, 15 instances), any slot
_SH_NOTE = " On any insert slot (tracks, drum sounds but SUB, drum bus, master, returns). Tested in the host tests only, not on a device."
_add("INS_SFOLD", "FELUCCA_INS_SFOLD", "[insert] SFLD (sine wavefolder)", F, 343, default=0,
     desc="A sine wavefolder type (SFLD): like FOLD but the folds are round (a sine of the drive), so it sings and "
          "aliases less; soft saturation at first, then more and more folds as SFLD rises. 0 is off.",
     symbols=("ifx_sfld",), notice="The shape of Felucca 1.5.5's PHASE FOLD by Leo Kuroshita (GPL-3.0)." + _SH_NOTE)
_add("INS_RECT", "FELUCCA_INS_RECT", "[insert] RECT (rectifier, octave up)", F, 345, default=0,
     desc="A rectifier type (RECT): up to the middle the wave's lower half is folded up (half-wave, then full-wave: the "
          "octave up of an Octavia), past the middle it is driven into a soft clip. 0 is off.",
     symbols=("ifx_rect",), notice=_SH_NOTE.strip())
_add("INS_HARM", "FELUCCA_INS_HARM", "[insert] HARM (harmonic exciter)", F, 346, default=0,
     desc="A harmonic exciter type (HARM): adds the octave and the twelfth over each note, warm rather than buzzy, "
          "more as HARM rises. 0 is off.", symbols=("ifx_harm",), notice=_SH_NOTE.strip())
_add("INS_RING", "FELUCCA_INS_RING", "[insert] RING (ring modulator)", F, 347, default=0,
     desc="A ring modulator type (RING): the sound times a sine from 26 Hz (a rough tremolo) to 2.6 kHz (bells, "
          "metal), RING picks its pitch. 0 is off.", symbols=("ifx_ring",), notice=_SH_NOTE.strip())
_add("INS_SUBOCT", "FELUCCA_INS_SUBOCT", "[insert] SUB (octave down)", F, 348, default=0,
     desc="An octave-down type (SUB): a square an octave under the note, following its level, added as SUB rises. "
          "For single-note lines (bass, leads): chords give a rough sub. Not on drum sounds. 0 is off.",
     symbols=("ifx_subo",), notice=_SH_NOTE.replace("drum sounds but SUB, ", "").strip())
_add("INS_SLEW", "FELUCCA_INS_SLEW", "[insert] SLEW (slew-rate crunch)", F, 349, default=0,
     desc="A slew limiter type (SLEW): the wave can only move so fast, so edges round into ramps, a slow op-amp's "
          "dull crunch; light at 1, heavy (muffled, gritty) at 127. 0 is off.", symbols=("ifx_slew",),
     notice=_SH_NOTE.strip())
# TAPE SPIT (2026-10-19, fx/insfx/tapespit.c): the shapers' range, their own state (76 B an instance, 15 instances)
MELODEE_144 = Provenance("Melodee 1.4.4 (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "v1.4.4 4f5fb6d", MELODEE_URL)
_TS_NOTE = (" On the tracks' slots, the drum bus, master, returns and USB input; not on drum sounds (their list is full)."
            " Tested in the host tests only, not on a device.")
_add("INS_TAPE", "FELUCCA_INS_TAPE", "[insert] TAPE (tape tone)", F, 356, default=0, provenance=MELODEE_144,
     desc="A tape tone type (TAPE): the highs rolled off (from 6.5 kHz at 1 to 1.7 kHz at 127) and the mids moved "
          "around at random every couple of seconds, at times inverted against the lows and highs, as a worn tape "
          "plays. TAPE sets how far it wanders and how dark it gets. 0 is off.", symbols=("ifx_tape",),
     notice="From Melodee 1.3.0's TAPE insert by Kerem Kilic (GPL-3.0); fixed point here, its RATE and MIX fixed."
            + _TS_NOTE)
_add("INS_SPIT", "FELUCCA_INS_SPIT", "[insert] SPIT (random resonant band, driven)", F, 357, default=0,
     provenance=MELODEE_144,
     desc="A spitting band type (SPIT): one resonant band jumping at random between 180 Hz and 5.9 kHz a few times a "
          "second, driven into a soft clip at the sound's own level and mixed over it. SPIT sharpens the band and "
          "the drive. 0 is off.", symbols=("ifx_spit",),
     notice="From Melodee 1.3.0's SPIT insert by Kerem Kilic (GPL-3.0); fixed point here, the clip follows the "
            "input's level (Melodee's has a fixed ceiling)." + _TS_NOTE)
_add("EX_GAIN", "FELUCCA_EX_GAIN", "[insert] EXGN: the example effect (a gain in dB)", F, 421, default=0,
     experimental=True, tested=_EX_TESTED, notice=_EX_NOTE, symbols=("ifx_exgn",),
     desc="The worked example of CONTRIBUTING.md for a new insert effect (fx/insfx/exgain.c, FX type 29): EXGN on the "
          "tracks' insert slots (INSERTS > S1 / S2) and the drum bus's, one amount, its GAIN: OFF (bypassed), then "
          "-55.5 dB to +7.5 dB (112 is 0 dB), LEVEL's curve, gliding without a click. Not on the master, the returns, "
          "the USB input or a drum sound. Off in every profile: a build without it is the build from before.")
# the insert copies of the "once" effects (fx/insfx/fxi_kern.c, fxi.c, 2026-10-13): built or not; how many play at once
# is what the effects memory holds (fxi_mem.h, 2026-10-23: they were a count each until then, WAS_COUNT); a
# slot picking one with no room left is bypassed (the INSERTS list says FULL)
_INS_LIM = (" Its line is in the effects memory, sized by the builder: the delay's line and one line of each limited insert type built, "
            "so each type built plays once at the same time (more while the delay or the chorus is in no master slot: a send in none lends its line); a slot that picks it when "
            "there is no room left stays dry until some is free (the INSERTS list shows how many more fit). A saved "
            "project keeps the type, not the room: on load the slots get it in order T1 T2 T3 DR, the master, the USB "
            "input, the returns.")
# (bit 270, INS_CHORUS, CHRS: retired 2026-10-29, the CHO insert is built with FX_CHORUS; RETIRED above, never reuse the bit)
_add("INS_DELAY", "FELUCCA_INS_DELAY", "[insert] ECHO (short delay copies for the tracks)", F, 271, default=0,
     desc="A slapback type for the tracks' insert slots (INSERTS > S1 / S2: ECHO), beside the delay send: one repeat "
          "75 ms after the sound; ECHO up to the middle sets its level, past the middle adds repeats (feedback up to "
          "60 %). Up to 93 ms of line an instance (the long, tempo-synced delay stays the send's): 8 KB of the effects "
          "memory." + _INS_LIM, symbols=("ifx_echo",), notice="Tested in the host tests only, not on a device.")
FM106 = Provenance("fm-106 (PatttF/fm-106)", "PatttF", "GPL-3.0-only", "be74757", "https://github.com/PatttF/fm-106")
_add("INS_JCHORUS", "FELUCCA_INS_JCHORUS", "[insert] JUNO CHORUS (the Juno-106 chorus)", F, 142, default=0, provenance=FM106,
     desc="The Juno-106's chorus as an insert type (INSERTS > S1 / S2, and the master's insert slots: JUNO): the "
          "MN3009 BBD model of fm-106, MODE I, II or I+II, JUNO its mix (127: the Juno's chorus on). A mono track "
          "comes out stereo. About 130 host instructions a sample an instance; 2 KB of the effects memory." + _INS_LIM,
     symbols=("ifx_jcho",), notice="From fm-106 by PatttF (GPL-3.0), after Ultramaster KR-106 (GPL-3.0). Tested in the "
                                   "host tests only, not on a device.")
# the returns' feedback sends (2026-10-16): B -> A, C -> A, C -> B, a block late (fx.c rs_fbv). Bit 167 (reserved 2026-10-16: the next free one after the web editor v14's 158, 159, 165, 166)
_add("RET_FB", "FELUCCA_RET_FB", "[master] return feedback sends (B to A, C to A, C to B)", F, 167, default=1,
     desc="The master's returns send to the returns on their left too (MASTER > A SENDS, B SENDS, C SENDS: each "
          "return to the other two), for feedback between the effects: a delay into a reverb and back. Each such "
          "send takes its return's output of the block before (a loop of 32 samples, 0.73 ms), DC-blocked, limited "
          "to -12 dBFS and soft-clipped, at a quarter of the gain of a send to the right, so no loop runs away. "
          "Without it a return sends only to its right (A to B and C, B to C). About 1.2 KB of RAM; nothing runs "
          "while every such send is 0.",
     symbols=("rs_fbv",), notice="Tested in the host tests only, not on a device.")
# FLNG (2026-10-15, fx-pool): its lines in the effects memory as the copies above (was one line a track, 5.3 KB in every
# build of it)
_add("INS_FLANGER", "FELUCCA_INS_FLANGER", "[insert] FLNG (flanger)", F, 259, default=0, provenance=FELUCCA_156,
     desc="A flanger type for any insert slot (a track's, the master's, the USB input's) and the returns (FLNG): a short "
          "delay swept 0.25 .. 5 ms (~0.25 Hz); FLNG up to the middle mixes it in, past the middle adds feedback. 1 KB "
          "of the effects memory an instance." + _INS_LIM, symbols=("ifx_flng",),
     notice="From Felucca 1.5.6's track INSERT FLANG by Leo Kuroshita (GPL-3.0)." + _INS_NOTE)
# XDLY (fx/insfx/xdly.c, 2026-10-22): the LYRA-8 style cross-modulated dual delay, its lines in the effects memory as the
# copies above
_add("INS_XDLY", "FELUCCA_INS_XDLY", "[insert] XDLY (LYRA-8 style cross-modulated dual delay)", F, 376, default=0,
     desc="Two delay lines whose outputs move each other's time at the audio rate, as the SOMA LYRA-8's modulated delay "
          "does (its idea, no code): an insert type for the tracks' slots and the master's (XDLY). Its page has TIME 1 "
          "and TIME 2 (each line, 1 ms up to the line's length; turning one bends the repeats), MOD (how far each "
          "line's output moves the other's time: a slow chorus to metallic FM grit) and FDBK (the repeats; past ~95 % "
          "they grow into a soft clip and sustain, the LYRA's self-oscillation). XDLY, the amount, is the MIX, 0 off. "
          "A mono track gets the two lines' mean, the drum bus and the master line 1 on L and line 2 on R. A track's "
          "repeats go on after its notes end. About 190 host instructions a sample an instance. Each instance is "
          "two lines of the length below in the effects memory (64 KB, or 32 KB); as many play at once as it holds, a "
          "slot picking it when there is no room left stays dry.",
     symbols=("ifx_xdly",), notice="Tested in the host tests only, not on a device.")
_add("XDLY_LEN", "FELUCCA_XDLY_LEN", "XDLY line length (its longest TIME)", F, 377, default=16384, parent="INS_XDLY",
     choices=((16384, "0.37 s (64 KB pool an instance)"), (8192, "0.19 s (32 KB)")),
     desc="How long each of XDLY's two lines is: TIME's top end. The shorter line halves every TIME and takes 32 KB "
          "less of the effects memory an instance.")
# (bits 440 441, INS_REVERB INS_TDELAY, ROOM TDLY: came and went 2026-10-29, the REV and DLY inserts are built with
#  FX_REVERB and FX_DELAY; RETIRED above, never reuse the bits)
# (bit 378, DLY_ARENA, retired 2026-10-29: the effects memory (fx/insfx/fxi_mem.h, fxi.c) is always sized automatically,
#  configure.py fx_memory / dly_arena; RETIRED above; never reuse the bit)
# GATER (fx/insfx/gater.c, 2026-10-14): a further insert type, any number (a few bytes a track)
_add("INS_GATER", "FELUCCA_INS_GATER", "[insert] GATER (tempo-synced trance gate)", F, 330, default=0,
     desc="A rhythmic volume gate type for the tracks' insert slots (INSERTS > S1 / S2: GATER), locked to the tempo "
          "and the sequencer's steps (it restarts with PLAY, follows the swing): one of 16 built-in 16-step patterns "
          "(trance gates, 1/8 and 1/16 chops, offbeats, dotted, clave), a RATE of 1/8, 1/16, 1/32 or their triplets, "
          "DEPTH (the amount: how far a closed step closes, 0 off) and SHAPE (hard to smooth). Every track can run it "
          "at once.", symbols=("ifx_gate",), notice="Tested in the host tests only, not on a device.")
_add("CHORDPLUS", "FELUCCA_CHORDPLUS", "CHORD+ (black keys change the chord), STRUM, VLEAD", "Sequencer", 206, default=0,
     provenance=SLOOP_24,
     desc="With a chord mode on (SCL > CHORD), the black keys change the chord played: F# major <-> minor, G# adds "
          "the 7th, A# sus4, C# the 9th, D# inverts; hold several to combine, or press one while a chord is held. "
          "SCL 2 gains STRUM (1-60 ms a note, right low to high, left high to low; keys and chord steps) and VLEAD "
          "(each chord voiced nearest the last). Recorded as it sounds; MIDI out sends the chord unstrummed.",
     notice="From SLOOP 2.4's CHORD+ by isod89 (isod89/sloop-fm1 v2.4, GPL-3.0). Tested in the host tests only, "
            "not on a device.")

# ---- sequencer
Q = "Sequencer"
_add("SECTIONS", "FELUCCA_SECTIONS", "song sections", Q, 85, default=16,
     choices=((16, "16: A..P, compressed log"), (8, "8: A..H, compressed log"), (4, "4: A..D, the old project slots")),
     desc="8 / 16: banks of 4 (SAVE + OCT), stored compressed in one 32 KiB log with a MEM gauge; the old slots move "
          "in at the first start. 4: the slots as before (needed by motion recording)")
_add("SNAPSHOTS", "FELUCCA_SNAPSHOTS", "snapshots (whole-state slots)", Q, 137, default=4,
     choices=((4, "4 slots: 32 KiB, USR3 keeps 32 KiB"), (2, "2 slots: 24 KiB, USR3 keeps 40 KiB"),
              (8, "8 slots: 48 KiB, USR3 keeps 16 KiB"), (0, "off: USR3 64 KiB")),
     desc="PROJECT > SNAPSHOT: the working project, every section and the song in one slot, loaded back whole (the state "
          "before is kept in BEFORE LOAD); export / import in the web editor. The flash comes from the end of USR3 "
          "(slots + 4 sectors of 4 KiB): the user sample slot USR3 holds that much less (docs/SNAPSHOTS.md)",
     symbols=("sn_scan", "sn_load", "ed_snap"))
_add("PATTERNS", "FELUCCA_PATTERNS", "per-project patterns and scenes", Q, 250, default=0,
     desc="Each of the 16 projects owns its state, 16 patterns a track, its scenes A..P and its song (docs/PROJECTS-DESIGN.md): "
          "nothing done in one project touches another. The log moves to 0xA0000 and grows to 128 KB (80 KB with "
          "SAMPLE or DRUM_USR built: user sample slot 1, which the samples lose). The old log at 0x97000 is not read. "
          "LFO held: the PATTERN layer (launch at the end, the next bar or now; store, copy, clear); the web editor shows "
          "the slots in the mixer. Off: a project is a plain section with its steps in it, in the old log.",
     symbols=("pat_scene_put", "project_open"))
_add("UNDO_HISTORY", "FELUCCA_UNDO_HISTORY", "undo / redo history (many levels)", Q, 69,
     desc="Undo and redo of pattern edits (EDIT + OCT- / OCT+) over many levels; the history lives in the pool and "
          "RAM this build leaves free (at least 1 KiB). About 2.2 KB of flash; off: a single undo level.")
FLOWSTATE = Provenance("Flowstate (zakariachowdhury/flowstate-fm1)", "Zakaria Chowdhury", "GPL-3.0-only", "3962560",
                       "https://github.com/zakariachowdhury/flowstate-fm1")
_add("MACROS", "FELUCCA_MACROS", "performance macros (GLO > MACRO)", Q, 120, default=0, provenance=FLOWSTATE,
     desc="COLOR, MOTION, SPACE, ENERGY: four knobs, each moving several sounds' parameters at once (filters and FM "
          "index, LFO depths, sends and width, drive and drum level), kept per project and section (the drum track's "
          "unused ENV / LFO DEST values: no format change); recorded by motion recording. At home: no change. "
          "About 1.4 KB of flash and 0.4 KB of RAM.",
     symbols=("mac_pre", "mac_post", "MAC_ROWS"))
_add("ENERGY", "FELUCCA_ENERGY", "ENERGY bands thin / thicken the drums", Q, 121, default=0, parent="MACROS",
     provenance=FLOWSTATE,
     desc="ENERGY also thins or thickens the drum pattern in five bands, walked on the beat: core lanes on the "
          "eighths, no ghosts, as written, harder hits, hat ratchets and a snare fill every second pass. About "
          "0.6 KB of flash.", symbols=("EN_EDGE",))
_add("LANE_FX", "FELUCCA_LANE_FX", "drum lane FX: hold a black key (ROLL, CHANCE, FILL, NUDGE, VRAND, FLAM, RAMP, EVERY)", Q, 420, default=0,
     experimental=True, tested="host-tested only, not on a device",
     desc="On the drum track, a black key held past the hold time opens its lane's FX page until the key is let go: "
          "ROLL (every hit of the lane repeats x2 to x8 in its step), CHANCE (each hit plays with this probability), "
          "FILL (the lane plays always, only in a fill or never in one: GLO + 9 / 10) and NUDGE (the whole lane early "
          "or late, up to half a step); SELECT, the second page: VRAND (each hit's velocity at random, up to 64 either "
          "way), FLAM (a grace note 5 to 40 ms before the hit), RAMP (a roll's hits fade in or out) and EVERY (the lane "
          "plays on loop 1:2 .. 4:4 only; with COND). KNOB 1-4 turn them, SHIFT + a knob resets. Saved with the pattern, "
          "as events of its automation (an older build refuses a drum pattern that has them). Not on STEP (a lane key "
          "held + a step sets its level there). Needs the automation store.")

_add("REC_PLAIN", "FELUCCA_REC_PLAIN", "REC plain: every note played is recorded, the voice mode never cuts it", Q, 364, default=0,
     desc="Experimental (2026-10-21, off for now). Recording keeps every note you play: a chord stays a chord, an overdub only "
          "touches a note you play again, and a long note you play into keeps going. The voice mode (MONO, LEGATO, UNISON) "
          "decides only what the voice plays, as it does for the keys, instead of cutting a recorded step to one note and "
          "joining the notes held over each other. A part on an engine that is mono by nature (ACID) records as before; an "
          "arp part always records plain. About 0.1 KB of flash less or more.")
_add("NOTEGEN", "FELUCCA_NOTEGEN", "note generators (STEP's GEN and GEN OUT)", Q, 374, default=0, provenance=FELUCCA,
     desc="A note generator on every synth track and every drum lane, played live over the pattern and never written into "
          "it but by WRITE, which asks: EUCLID (HITS spread over STEPS, turned by ROT), RANDOM (DENSITY, SEED, ACCENT) and "
          "PULSE (EVERY, OFFSET, RATCHET), each REPLACING, ADDING to or MASKING the pattern. STEP's last pages GEN and GEN "
          "OUT; saved with the project, one undo level a WRITE. On a drum lane picked with ALGORITHM, that lane only. Apart "
          "from the STEP tools (SEQTOOLS) and from GRIDS (GEN_GRIDS) since 2026-10-22: each builds without the others. "
          "EUCLID after Felucca 1.5.6's. "
          "About 6.8 KB of flash and 2 KB of RAM: 6.1 KB of it the generators' framework, shared with GRIDS, EUCLID RANDOM "
          "PULSE themselves 0.7 KB (the builder's measure, 2026-10-22).",
     symbols=("gen_sync",))
_add("EX_NOTEGEN", "FELUCCA_EX_NOTEGEN", "note generators: the examples WALK and UPDN (they choose the notes)", Q, 423,
     default=0, parent="NOTEGEN", experimental=True, tested=_EX_TESTED, notice=_EX_NOTE, symbols=("wk_gstep", "ud_gstep"),
     desc="The worked example of CONTRIBUTING.md for a new note generator (seq/gen_ex.c, generator types 4 and 5): two "
          "more TYPEs on STEP's GEN page that choose the notes, not only the hits. WALK: a random walk in the track's "
          "scale over RANGE degrees, at most LEAP degrees a step, DRAW another walk. UPDN: up the scale and back down "
          "over RANGE degrees, HOP degrees a note, a note every RATE steps. On a drum lane they give hits only. Off in "
          "every profile: a build without it is the build from before.")

# ---- Connectivity (was "USB & BT & MIDI"; Wi-Fi + Link since 2026-10-28) (the user, 2026-10-20). USB MIDI (notes, clock, the editor and updates) is in every build. The USB
# port's two extras, each a checkbox, not both: the controller has three data endpoints (hal/fm1_usb.h EP1..3), USB MIDI
# takes EP1, USB audio EP2 both ways and EP3 IN, the console EP2 and EP3 (io/usb/usb.c). (bit 47, USB_MODE, the one
# choice before: MIGRATE reads it; never reuse the bit)
M = "Connectivity"
_add("USB_MIDI", "", "USB MIDI (always: notes, clock, the editor, updates)", M, 362, default=1, no_image=True,
     fixed=True,
     desc="USB MIDI is in every build: notes and clock both ways, the web editor, and firmware updates (the installer "
          "and the builder flash through it). Shown for the record, it cannot be switched off.")
_add("USB_CONSOLE", "", "USB serial console (logs, diagnostics)", M, 360, default=0,
     desc="A read-only serial console on the USB port, next to USB MIDI: the logs and the diagnostic commands "
          "(with Bluetooth audio, its btinq / btpage traces). Not with USB audio: the USB port has three data "
          "endpoints, USB MIDI takes one and USB audio the other two.")
_add("USB_AUDIO", "", "USB audio (4 stems in, stereo out)", M, 361, default=0,
     desc="4 stems in + stereo out (UAC1) next to USB MIDI; the stems are resampled to the computer's clock (cubic, "
          "after X0X 80b7d40), so a host passing the input live to an output does not drop out. Needs +12 KB pool "
          "(a resampler in the audio path is CPU-heavy). It streams on the FM-1 since 2026-10-12 (before, a USB audio "
          "build hung and went to the USB rescue); its audio quality is not checked yet. Not with the serial console: "
          "the USB port has three data endpoints, USB MIDI takes one and USB audio the other two.",
     provenance=MELODEE_USB,
     notice="USB audio capture resampler ported from X0X by Charles Vestal (GPL-3.0), 80b7d40.")
_add("UART", "FELUCCA_UART", "TRS MIDI IN", M, 48,
     desc="MIDI input on the TRS jack (notes and clock from a keyboard or sequencer). About 0.6 KB of flash; off: "
          "the jack hears nothing, and SYNC TRS has no clock.")
_add("MIDI_CLOCK", "FELUCCA_MIDI_CLOCK", "MIDI clock in + out", M, 49,
     desc="MIDI clock both ways. In: follow an external MIDI clock and start / stop (GLO > SYSTEM SYNC: AUTO takes the "
          "TRS jack, else USB, else the internal tempo). Out: send MIDI clock on USB so a computer, drum machine or "
          "sequencer can follow the FM-1: 24 ticks a beat (also while stopped, so the other side has the tempo first), "
          "Start, Stop, and Song Position + Continue when the FM-1 itself continues an external song. Each tick leaves "
          "with the sound of its beat (latency-compensated, no jitter of its own), follows tempo changes and the nudge, "
          "and never swings. Switched on in the HOME menu (CLOCK OUT, off by default). While the FM-1 follows a USB "
          "clock nothing is sent back to USB; following the TRS jack, the clock is passed on to USB. The TRS jack is "
          "input only (no MIDI out on the FM-1's board). Off: the FM-1 always runs on its own tempo and sends no clock.",
     symbols=("clko_block",))
# (bit 198, MIDI_CLKOUT, merged into MIDI_CLOCK 2026-10-15: one item, MIDI clock in + out; MERGED below maps old configs;
#  never reuse the bit)
# (bit 71, UA_RESAMPLE, retired 2026-10-15: the USB audio choice of USB_MODE always resamples the capture; never reuse
#  the bit)
_add("MIDI_EXPR", "FELUCCA_MIDI_EXPR", "MIDI expression (bend, mod, sustain, RPN)", M, 50, provenance=MELODEE_MIDI,
     desc="Plays more than notes from MIDI: pitch bend, mod wheel, breath, foot, aftertouch, sustain pedal and "
          "pitch-bend range (RPN 0). About 0.5 KB of flash; off: only notes and the panic messages work.")
_add("MIDI_NOTEOUT", "FELUCCA_MIDI_OUT", "MIDI note out of the sequencer (GLOBAL MIDI OUT = BOTH or SEQ)", M, 199,
     provenance=SLOOP_24,
     desc="MIDI OUT = BOTH (SLOOP 2.4's SEQ: the keys and the sequencer) or SEQ (the sequencer alone, no keys): what the "
          "sequencer, the arp and the rolls play also goes to MIDI out, each track on its channel, every note ended (STOP, a "
          "channel changed, back to KEYS). Switched on GLOBAL's MIDI page (MIDI OUT KEYS / BOTH / SEQ). About 1.2 to 2.5 KB of "
          "flash, by build; off: the line goes and only the panel's keys go out.")
# (bits 195, 196, 197 -- MIDI_CH, MIDI_OUT, MIDI_INCLK -- retired 2026-10: a MIDI channel for each track and MIDI IN = CLOCK
#  are always built, the user's setting is the HOME menu, SYSTEM 1/3 and 2/3; MIDI OUT = SEQ is a builder item again since
#  2026-10-11 as MIDI_NOTEOUT on bit 199 (the user: one item for note and clock out if both are small, two if either is
#  big; clock out measured 2.5 KB). Never reuse 195..197)

# ---- UI
U = "UI"
_add("MISSING_WARN", "FELUCCA_MISSING_WARN", "say what a project uses and this build lacks", U, 90,
     desc="'MISSING: PHYS T2, KIT 909' in the top bar when a project, song section, user preset or kit uses an "
          "engine, kit, sample set or FX this build leaves out (once per item until power-off; never stalls the "
          "audio); PROJECT > TOOLS > MISSING lists them again. Off: they play their stand-ins silently (saves 1.5 KB of flash)")
_add("FAVORITES", "FELUCCA_FAVORITES", "favorites: mark sounds, browse only them", U, 141, provenance=FELUCCA,
     desc="With the presets: SAVE held + PRESETS marks the sound playing, the FAVORITES filter of the preset list (SHIFT2 + "
          "PRESETS) browses only the favorites, a star after a favorite's name in the toast and in PROJECT > USER (SHIFT2 + ITEM marks a slot). Up to 64 factory sounds (kept by engine and name: they stay marked over builds that leave engines "
          "out) and any of the 32 user presets (an erased preset loses its mark); the FM6 / CZ banks and the drum track have "
          "none. Kept with the settings (136 B at the end of the settings record: an older record reads whole with none; "
          "a build without this item keeps the rest and forgets them on its next save). About 1 KB of flash, 136 B of pool. "
          "Off: no marks.",
     notice="After Felucca 1.6's favorites by Leo Kuroshita (GPL-3.0). Tested in the host UI and storage tests only, not on "
            "a device.")
_add("PARAM_HELP", "FELUCCA_PARAM_HELP", "help line: what the knob changes, in words", U, 138, default=0,
     desc="the screen's last line names what a knob changes in plain words, e.g. 'Filter cutoff', 'Reverb send', while "
          "a value turns and ~2 s after, and always on GLOBAL, SYSTEM and PROJECT (the cursor line, the hot action, "
          "' -> SAVE' after an action that asks first: the button that runs it); only the lines of the features built (tools/param_help.json, also the web "
          "editor's tooltips). About 7.7 KB of flash (the texts packed), no RAM code",
     symbols=("ph_find", "PH_BLOB"))
_add("SYS_DEBUG", "FELUCCA_SYS_DEBUG", "SYSTEM's debug pages (internals, USB stats)", U, 341, default=0,
     desc="SYSTEM keeps its pages for finding faults: INTERNALS (the last reset, the flash's "
          "interrupts-off time, the second core's internals), USB STATS (the USB audio glitch counters) and on INFO the "
          "clock, the second core, the drops and the CPU guard apart. Off: INFO shows what a player wants, CPU LOAD, "
          "UNDERRUNS (the drop-outs counted in) and BATTERY, in the large face, and the code of the rest is left out "
          "(about 2.9 KB less flash, measured on a USB-audio build). The red / amber mark on the header's rule after a "
          "drop-out stays either way.")
_add("KNOB_ACCEL", "FELUCCA_KNOB_ACCEL", "knob acceleration by turn speed", U, 67,
     desc="1 / 2 / 3 / 5 / 8 steps a detent when turned fast; never on lists (engines, kits, presets)",
     provenance=x0x("61654ba"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): knob acceleration. Tested in the emulator only.")
# (bit 63, KEYS_DMA, dropped 2026-10-19: failed on the FM-1, RETIRED above; never reuse the bit)
_add("LCD_BAUD", "LCD_BAUD", "screen data speed (SPI clock)", U, 79, default=1,
     choices=((1, "30 MHz"), (4, "12 MHz (as before X0X)"), (0, "60 MHz")),
     desc="How fast data is sent to the display: 60 MHz / (n + 1). Faster redraws finish sooner (less tearing); the "
          "ST7789V takes ~62 MHz, so 60 MHz is the limit. No flash cost; the faster settings are untested here on "
          "hardware.", provenance=x0x("d179e03"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): the faster LCD clock ran on X0X's FM-1; untested on "
            "hardware here.")
# (bit 68, LCD_DIRTY, retired 2026-10-31: its function cv_blit_dirty had no caller since f1ab07ed, so it changed nothing on the screen; RETIRED above;
#  never reuse the bit)

# ---- system
Y = "System"
_add("EDITOR", "FELUCCA_EDITOR", "web editor (USB SysEx)", Y, 54,
     desc="The web editor's communication layer: the frames and INFO, BUILD, PING. What it can do comes with its "
          "options: BACKUP, LIBRARIAN, live editing (ED_LIVE) and its features. Firmware updates (the installer, "
          "M-UPGRADE) and the USB rescue are not part of it: every build has them.",
     off_warning="without it there is no web editor (so no backup / restore from it); installing and the USB rescue "
                 "still work")
# (bit 55, IDLE, retired 2026-10: the CPU sleeps between main-loop polls in every build; never reuse the bit)
_add("BACKUP", "FELUCCA_BACKUP", "backup / restore from the web editor", Y, 84, parent="EDITOR",
     desc="everything in flash to one .optimist-backup file and back (editor cmds 43..48; without live editing the "
          "sample slots' 11..15 and the snapshots' 54..57 too, which a restore writes through); a restore onto another "
          "build reports what it skips and what plays a stand-in", off_warning="no backup: export projects and kits "
          "one by one before an update")
_add("LIBRARIAN", "FELUCCA_LIBRARIAN", "web editor: librarian (projects, presets, patterns, kits)", Y, 352, parent="EDITOR",
     desc="The web editor manages the user data one piece at a time: the user presets (the Library: list, rename, move, "
          "erase, files) and the user drum kits (load, store, erase, files), with live editing or without; without it "
          "also the sections A..P (load, save, a file each) and the per-track patterns (copy, clear, files), next to "
          "Backup. Editor cmds 16..21, 38..41; without live editing 9 and 79..84 too (editor/ed_lite.c).",
     off_warning="no user presets or user kits from the web editor (the device's own SAVE pages keep them)")
_add("ED_LIVE", "FELUCCA_ED_LIVE", "web editor: live editing (else backup / restore and the librarian)", Y, 350, parent="EDITOR",
     desc="The web editor edits the FM-1 live: the sounds' values, presets, user presets, tracks, steps, the mixer, "
          "kits and the ED_* pages, kept in sync both ways. Off: the editor's side is backup and restore only (the "
          "frames, INFO, BUILD, the user sample slots a restore writes, the snapshots, cmds 43..48: editor/ed_lite.c); "
          "the web page then shows its Backup (and, with LIBRARIAN, the librarian) alone. The ED_* items do nothing "
          "without it.",
     off_warning="no live editing from the web editor: backup and restore, and with LIBRARIAN the projects, presets, "
                 "patterns and kits")
# (2026-10-19, the user: "group them in logical features": the thirteen ED_* commands became three features; bits 144..149,
#  156..159, 165, 166, 168 (ED_FXP .. ED_REC) retired, never reuse them)
_add("ED_MIX", "FELUCCA_ED_MIX", "web editor: the mixer (effect slots, drum lanes, solo and mute)", Y, 353, parent="EDITOR",
     desc="The web editor's mixer beyond levels and pans: every FX slot by owner with its parameters (track inserts, the "
          "drum bus and lane inserts, master inserts, returns, the USB input; cmds 86, 87, 94), the drum lanes' pan, "
          "mutes, solos and LFOs (cmd 95), track and lane SOLO and MUTE (cmd 104). Off: the editor's Inserts / Returns "
          "pop-ups and FX pages, the lane LFOs and the strips' SOLO are not offered; the levels, pans and sends stay.")
_add("ED_SEQ", "FELUCCA_ED_SEQ", "web editor: the sequencer (lane steps, pattern tools, song chain)", Y, 354, parent="EDITOR",
     desc="The web editor's sequencer tools beyond the steps themselves: a step held in a drum lane's context with the "
          "device's own list of lockable values (cmd 105; with the automation store), the pattern tools REVERSE, "
          "RANDOM, COOK, LENGTH and the note generators run by the device (cmd 98; SEQTOOLS, NOTEGEN), the song chain "
          "(cmd 97; the arranger). Off: the editor lists raw parameter ids, has no tools buttons and no song chain.")
_add("ED_SCREENS", "FELUCCA_ED_SCREENS", "web editor: the device's screens (cards, GLOBAL / SYSTEM, scope)", Y, 355,
     parent="EDITOR",
     desc="The web editor's Device tab: the Optimist UI's SOUND, MIX, MASTER, PROJECT, STEPS, SONG and TEMPO pages as "
          "rows of cards (cmd 99), GLOBAL and SYSTEM as the device's lines with their help (cmd 96) and the "
          "oscilloscope (cmd 101). Off: no Device tab; "
          "the editor keeps its own Sound / Mixer / Sequence tabs.")
_add("ED_MIRROR", "FELUCCA_ED_MIRROR", "web editor: the screen mirrored (the Mirror tab, the iPhone app; USB or Bluetooth)", Y, 381,
     parent="EDITOR", default=0, tested="on the FM-1: the web editor's Mirror tab and the Optimist Mirror app, USB and "
     "Bluetooth (the user, 2026-10-25); host-tested (the stream decoded back to every screen drawn, every catch-up the panel)",
     desc="The FM-1's screen in the web editor's Mirror tab or on an iPhone (the Optimist Mirror app, ios/OptimistMirror), over "
          "a USB cable or Bluetooth MIDI (with BLE): while one asks (cmd 106), every write to the panel also goes out, "
          "run-length coded, as it is drawn, so it shows the very pixels: the mixer, the step grid, the layers, the dialogs. "
          "No frame buffer on the device; nothing is sent while none asks; the mixer's meters go 10 times a second over USB, "
          "never over Bluetooth. A link that falls behind (USB: the host stops reading; Bluetooth: its 8 KB send ring is full) "
          "drops what is drawn and catches up once it is clear again, at most 4 times a second: the panel's bands drawn again "
          "in place, no flash on the device. Over Bluetooth whole screens come at the rate the radio carries. With "
          "live editing (ED_LIVE) or without.")
# (bit 70, SIZE, retired 2026-10: the UI, storage and editor code is always built minsize, tools/size_fns.py keeps the
#  audio path out of it; never reuse the bit)
# (bit 300, TAB_IN_POOL, retired 2026-10-29: the sound tables and FM6's buffers are always in the pool, the RAM being one
#  packed region; RETIRED above; never reuse the bit)
_add("TAB_CODED_ALL", "FELUCCA_TAB_CODED_ALL", "tables unpacked into RAM (flash for pool)", F, 386, default=0,
     choices=((0, "off"), (1, "the small tables and the small font; PLAITS' waveshapers, JUNO's tables, the compressor's and XDLY's curves when built (5.8 KB of pool, up to 11.4 with every one)"),
              (2, "every table, FM6's five too (3 KB of pool more)")),
     experimental=True, tested="host-tested only (tests/tab_coded_test.c: every table decoded byte for byte), not on a device",
     desc="Trades flash for pool: smooth lookup tables the sound and the UI read are kept in flash as the differences between neighbours and unpacked at start-up into the pool, always the pool (main RAM pays nothing); the undo history ring, which takes what the pool has left, is that much shorter. The same values: the sound and the screens are unchanged; the tables read a little faster from RAM than from flash. The eleven small tables (pitch, envelope and LFO rates, cutoff, levels, decay): 4.3 KB of pool for about 2.2 KB of flash; every table adds FM6's five (fine, detune, LFO and portamento): 7.3 KB of pool for about 3.1 KB of flash (measured on the synth profile and the user's). With PLAITS built, either choice also unpacks its five waveshapers: 2.5 KB more of pool for about 1.9 KB more of flash. Either choice also moves the small font's bitmaps to the pool as they are (1.5 KB of pool for 1.0 KB of flash: they travel in the start-up stream like the rest) and, when built, JUNO's eleven small tables, the master compressor's 2^x curve and XDLY's TIME curve, delta-coded like the small tables (together 2.0 KB of pool for 1.3 KB of flash on the user's configuration, 2026-10-24).")
_add("LIVE_SAVE", "FELUCCA_LIVE_SAVE", "save while playing (flash writes in short windows)", Y, 380, default=0,
     tested="on the FM-1: saving while playing works (the user, 2026-10-25); host-tested",
     desc="Saving no longer stops or waits for the transport: SAVE on a user preset, a kit, a project, a name or the song "
          "writes as the music plays (no STOP BEFORE SAVE, the naming page no longer stops and restarts the transport), "
          "and what waited for the stop (live sections and patterns, the song, the settings) goes to flash while "
          "playing, one record at a time, 250 ms apart, once the panel has rested 1.5 s. The audio keeps playing "
          "because every sector erase is already cut into 1 ms windows with the erase suspended between them "
          "(hal/fm1_flash_slice.h); the main loop (the screen, the panel) still pauses for one record's write, tens of "
          "ms. If the flash part ever holds an erase in one window (no suspend: the audio silenced in it), saves wait "
          "for the stop again until the next start. Snapshots, backup restores, sample uploads, the web editor's writes "
          "and the working project's autosave still wait for the stop. About 240 B of flash, 1 byte of RAM.",
     off_warning="saves refused (STOP BEFORE SAVE) or held until the transport stops, as before")
# ---- Memory management (the user, 2026-10-28; three choices 2026-10-31): the first group. The real items are LAYOUT
# (the flash step, FELUCCA_LAYOUT) and SDCARD, both hidden; the menu and the web builder show three fronts instead (the
# user, 2026-10-31: "one switch for SDCard mode (on/off) and when off, two options: number of sample slot and numbe of
# projects"): SD_MODE, SMP_SLOTS and PROJECTS (docs/SD-MODE-DESIGN.md 9.7). A front has no firmware switch and no image
# of its own (no_image): core.sync_memory reads it back from LAYOUT / SDCARD / PATTERNS, core.set_front writes them when
# one is picked (room.after_toggle), dump() leaves it out, an old .config's LAYOUT / SDCARD / MEMORY lines keep loading.
# MEMORY (bit 436, the one-row front of 2026-10-28..31) stays a hidden front for those lines. Bits 447..: the next free (444 is MIX_ROUND, retired).
# The steps: tools/flash_layout.py STEPS is the one table; this is the builder's copy of what it needs (the web builder
# runs without tools/ on its path), checked by tests/flash_layout_test.py.
# layout -> (user sample slots kept, projects, app.bin's room in the classic build); normal: 2 slots (3 with PATTERNS off)
LAYOUT_STEPS = {0: (2, 16, 581564), 2: (1, 16, 655292), 6: (2, 4, 659388), 7: (2, 2, 675772), 3: (0, 16, 720828), 4: (1, 4, 741308), 5: (1, 2, 757692), 8: (0, 4, 806844), 1: (0, 2, 831420)}
NORMAL_APP = LAYOUT_STEPS[0][2]
# MEMORY's old values (derived, never stored): normal 0, more flash 1, SD card mode 2, no user samples 4; 5 any other step
MEMORY_OF = {0: 0, 3: 4, 1: 1}
MEMORY_CHOICES = ((0, "normal"),
                  (4, "no user samples: 16 projects, more flash"),
                  (1, "more flash: two projects in flash"),
                  (2, "SD card mode"),
                  (5, "another step: see SAMPLE SLOTS and PROJECTS"))
SLOT_WORDS = ((0, "0: none (SAMPLE's built-in sets still play)"), (1, "1: USR3 (64 KiB, the snapshots)"),
              (2, "2: USR2 + USR3 (144 KiB, as normal)"), (3, "3: USR1..USR3, no projects (PATTERNS off)"))
_add("MEMORY", "", "memory: the one-row choice of before (an old .config's MEMORY line)", MEM, 436, default=0,
     choices=MEMORY_CHOICES, no_image=True, hidden=True, target_only=True,
     desc="The single MEMORY row of 2026-10-28..31, kept so that an old .config's MEMORY= line and --set MEMORY= still "
          "work: 0 normal, 4 no user samples, 1 more flash, 2 SD card mode. The menu shows SD CARD MODE, SAMPLE SLOTS "
          "and PROJECTS instead.")
_add("SD_MODE", "", "SD card mode: user data on an SD card (a hardware mod)", MEM, 445, default=0, no_image=True,
     experimental=True, target_only=True, tested="host-tested only (docs/SD-CARD-DESIGN.md); no FM-1 has the card yet",
     desc="On: the biggest flash for code (831,420 B, the release 823,228 B) with the user data on a microSD card wired "
          "to the LCD bus (docs/SD-CARD-DESIGN.md): one project in flash, the others, the samples and the recordings "
          "on the card; SD_RING and SD_REC under it. SAMPLE SLOTS and PROJECTS are greyed while it is on. Off: the "
          "flash keeps the user data, as SAMPLE SLOTS and PROJECTS choose.")
_add("SMP_SLOTS", "", "sample slots: user samples kept in flash", MEM, 446, default=3, no_image=True,
     choices=tuple((v, w) for v, w in SLOT_WORDS if v == 3 or any(s == v for s, _, _ in LAYOUT_STEPS.values())),
     dim_when="SD_MODE", target_only=True,
     desc="How many user-sample slots the flash keeps; each slot given up is code room (docs/SD-MODE-DESIGN.md 9.7). "
          "2: USR2 and USR3 where normal keeps them (and the snapshots, the CZ collection at USR3's top); 1: USR3 "
          "alone; 0: none (no user drum samples, snapshots or CZ collection; SAMPLE plays its built-in sets). 3: the "
          "projects off (PATTERNS off): USR1 too, the old song sections. With PROJECTS, the pair is the flash layout.")
_add("PROJECTS", "", "projects kept in flash", MEM, 447, default=16, no_image=True,
     choices=tuple((p, str(p)) for p in sorted({p for _, p, _ in LAYOUT_STEPS.values()})),
     dim_when="SD_MODE", target_only=True,
     desc="How many projects the flash keeps (the user, 2026-10-31: 2, 4 or 16); fewer projects is a smaller project "
          "log, more code room. The log's room (MEM FULL past it; a busy project is about 6-8 KB, an estimate): 16: "
          "128 KiB with no sample slot (about 13-17 busy projects), 80 KiB in normal (7-10); 4: 44 KiB (3-5); 2: 28 KiB. "
          "The web editor's backup keeps the projects a smaller choice has no room for.")
# RAM_PACKED (bit 437, retired from the menu 2026-10-29, never reused): hidden, not removed, so a .config or profile that
# names it keeps loading and dumping as before.
_add("RAM_PACKED", "", "RAM: one packed region, always", MEM, 437, default=1, no_image=True, fixed=True, hidden=True,
     desc="The RAM is one packed region in every build (the code RAM, the data and the pool share it: docs/MEMORY-MAP.md), "
          "so there is no switch for it. Shown for the record, it cannot be switched off.")

# bit 409 (2026-10-25): the first free one after the reverted font items' 399..408 (never reused)
_add("SDCARD", "FELUCCA_SDCARD", "SD card on the LCD bus (a hardware mod)", MEM, 409, default=0, hidden=True,
     experimental=True, target_only=True, tested="host-tested only (a card model on real FAT16 / FAT32 images); no FM-1 "
     "has the mod yet",
     desc="Reads and writes a microSD card wired to the LCD's SPI bus: the card's clock and data in on the LCD's, its data "
          "out on the LCD's D/C line through 1 kOhm, its chip select the LCD's through one inverter (74LVC1G04), so the "
          "card is selected whenever the screen is not (docs/SD-CARD-DESIGN.md has the wiring and the parts). FAT16 or "
          "FAT32 (cards up to 32 GB as sold; larger ones reformatted FAT32). In the normal layout 8.3 names, files made "
          "on a computer that the FM-1 reads and writes inside (Petit FatFs: no new files, no growing); with LAYOUT "
          "1 full FatFs: long names, files created and grown, the card's own OPTIMIST folder (about 1 KB of "
          "flash more while only the console uses it, about 20 KB once the card's pages use all of it). For now the "
          "serial console's sd commands only (USB_CONSOLE): mount, list, dump, speed test, write test. Without the mod "
          "it finds no card and changes nothing. 5,032 B of flash, 20 B of RAM and 556 B of pool with the console "
          "(measured 2026-10-25, Petit FatFs).",
     off_warning="no SD card")
# bit 410 (2026-10-25): the next free one after SDCARD's 409
_add("LAYOUT", "FELUCCA_LAYOUT", "flash layout: the MEMORY step (set by SD CARD MODE, SAMPLE SLOTS and PROJECTS)", MEM, 410,
     default=0, hidden=True,
     choices=tuple((k, f"{s} sample slot{'' if s == 1 else 's'}, {p} projects: {room:,} B for code") for k, (s, p, room)
                   in sorted(LAYOUT_STEPS.items(), key=lambda kv: kv[1][2])),
     experimental=True, target_only=True, tested="host-tested only; on an FM-1 whose flash runs the release image's 820 KB (docs/SD-MODE-DESIGN.md)",
     desc="The flash step (docs/SD-MODE-DESIGN.md 9.7, tools/flash_layout.py STEPS): each pair of SAMPLE SLOTS and "
          "PROJECTS is one; the user sample slots given up and the smaller project log become code space (code runs in "
          "place from flash: no RAM). 0 normal (2 slots, 16 projects, 581,564 B of code); 1 (0 slots, 2 projects, "
          "831,420 B; the SD card mode's with SDCARD); 3 (0 slots, 16 projects, 720,828 B). Installing another step "
          "ERASES what the two keep in different places (the projects in flash always; the update loader clears the "
          "other step's sectors first): back up with the web editor before. The first install of a step needs a normal "
          "build of this version first (an older updater refuses its size).")
# bit 411 (2026-10-25): the next free one after LAYOUT's 410; the arena (core/arena.c, fx/fx_arena.c, docs/SD-MODE-DESIGN.md
# 6b): KB, 0 the fixed memories as before. The core (every build it is set in): the parts' engine states placed by need,
# the edit's one confirm for what to drop, a load's refusals on it one by one; the extras only in the SD card mode
# (registry.h FELUCCA_SD_MODE: LAYOUT 1 until its own flag): the effects' lines, the other buffers, the load's list page
# (FELUCCA_FX_ARENA, FELUCCA_MEM_LIST). The pool estimate: core.py arena_mem (sd_mode)
_add("ARENA", "FELUCCA_ARENA_KB", "memory per project (the free RAM: one arena for the engines and the effects' lines)",
     MEM, 411, default=0,
     experimental=True, tested="host-tested (tests/fx_arena_test.c, arena_bufs_test.c, arena_test.c, arena_frag_test.c, placeholder_test.c); "
                          "on the user's FM-1 since 2026-10-29 (the fm1-next test packages)",
     desc="Instead of reserving every engine's memory whether a project uses it or not, the RAM the firmware leaves free "
          "is one arena that holds what the project uses (the user, 2026-10-29: arena first, undo gets the rest): each "
          "synth part's engine state (FM6, PHYS, GRAIN, ACID, PLAITS, JUNO, DRAWBAR, CZ, SLICE), placed when an engine "
          "is picked, its room free for the others once the part plays an engine that keeps nothing there; the "
          "effects' lines (the delay, chorus and reverb sends' lines, the limited inserts' own room: one line of each type built) and the other buffers only some projects use (each track's SLICER "
          "recording while it is on STUT, WAVE's table while a part plays WAVE, the X0X 909's and 808's states while the "
          "kit or a lane plays them, the PUNCH ring while the project's PUNCH switch is on: GLOBAL > Rec, FM6's sine "
          "table while a part plays FM6). The undo history keeps 16 KB and borrows what the arena does not use, giving "
          "it back, its oldest levels first, as soon as the arena needs it. A change that does not fit is refused and "
          "the device proposes what to drop (DROP T2 FM6, then the effects' lines: DROP REV 32 KB); a project load that "
          "does not fit opens a list of what to drop. Before anything is refused the memories are moved together when "
          "the free room would hold it, and a project load packs them all; nothing that sounds is moved (a part only "
          "while it plays nothing, a send line while its tail is silent), what sounds is packed once it is quiet "
          "(SYSTEM > INFO's RAM bar shows the arena in use and the undo history's share). A dropped or refused effect "
          "is bypassed and dimmed (FULL), kept in the project, and comes back by itself once there is room. Off: every "
          "memory reserved as before, nothing ever refused. An older configuration's size (ARENA=96) reads as on.")
# bit 414 (2026-10-26): the next free one after the retired FAR_OK's 413; the fm-1 session's handover (projects off the device)
_add("BT_PROJ", "FELUCCA_BT_PROJ", "projects to and from the phone over Bluetooth", Y, 414, default=0,
     parent="EDITOR", experimental=True, tested="host-tested only (tests/mirror_test.c); no client sends it yet",
     desc="The web editor's and the phone's project moves (a project read from the FM-1, or one written into it: the "
          "backup's project streams and the project command) over Bluetooth MIDI too, not only over the USB cable, so "
          "with one project on the FM-1 the phone keeps the others without a cable. Over Bluetooth a project moves at "
          "the link's few KB a second (a dense project in a few seconds). About 640 B of the pool (a request's room for "
          "the stream's chunks).",
     off_warning="projects move over the USB cable only")
# bit 413 (FAR_OK, retired 2026-10-29, never reused): the big slot waited for the far probe's answer on an FM-1; the user:
# "drop the far probe, we don't need it", the fake SD mode layout always has the whole slot (RETIRED below)
# bit 415 (2026-10-26): the next free one after BT_PROJ's 414; samples streamed from the SD card (storage/sd/sd_stream.c,
# sd_stream_plan.c, docs/SD-CARD-DESIGN.md 7): the ring's KB a voice, 0 off (registry.h FELUCCA_SD_RING_KB -> SD_STREAM:
# full FatFs, SAMPLE and the arena too, else it builds nothing)
_add("SD_RING", "FELUCCA_SD_RING_KB", "samples streamed from the SD card (SAMPLE's CARD set): each voice's lookahead", MEM,
     415, default=0, parent="SDCARD", shown_under="SD_MODE",
     choices=((0, "off"), (4, "4 KB a voice (46 ms of 44.1 kHz 16-bit mono)"), (8, "8 KB a voice (93 ms)"),
              (12, "12 KB a voice (139 ms)"), (16, "16 KB a voice (186 ms)")),
     experimental=True, target_only=True, tested="host-tested only (tests/sd_stream_test.c, sd_stream_plan_test.c: "
     "a card model with stalls, an interrupt-driven audio clock); no FM-1 has the card yet",
     desc="SAMPLE's SET gets a last value, CARD: the part plays its sample from the SD card (a WAV of 8 or 16 bits, mono "
          "or stereo, up to 176 kHz), streamed, not loaded. Each sample's first moments are kept in memory (the "
          "pre-buffer, one setting: 25, 50, 100 or 200 ms, 50 by default; 50 ms of 44.1 kHz 16-bit mono is 4.4 KB a "
          "sample) so a note starts at once while the card's first block comes; each playing voice reads ahead "
          "into a ring of this size, filled in short bursts of 4 KB between the screen's strips (the screen and the "
          "card share a bus). A card that stalls longer than the ring holds makes the voice fade out at once (no "
          "click; the console's sd stream counts it). 8 voices take 8 x this of the arena while the project plays "
          "card samples, with the heads; a project whose samples do not fit is refused and the device proposes what "
          "to drop (a part's engine, or a shorter pre-buffer). A card sample plays at most an octave up (each octave "
          "doubles what it reads). Needs LAYOUT (full FatFs) and SDCARD, SAMPLE and ARENA.",
     off_warning="no samples from the card")
# bit 416 (2026-10-26): the next free one after SD_RING's 415; the master recorded to the SD card (storage/sd/sd_rec.c,
# docs/SD-CARD-DESIGN.md 7): the ring's KB, 0 off (registry.h FELUCCA_SD_REC_KB -> SD_REC with full FatFs)
_add("SD_REC", "FELUCCA_SD_REC_KB", "record the master output to the SD card: the buffer for the card's stalls", MEM,
     416, default=0, parent="SDCARD", shown_under="SD_MODE",
     choices=((0, "off"), (16, "16 KB (93 ms of 44.1 kHz 16-bit stereo)"), (32, "32 KB (186 ms)"), (64, "64 KB (372 ms)")),
     experimental=True, target_only=True, tested="host-tested only (tests/sd_wr_test.c, sd_rec_test.c: a card model with "
     "stalls, an interrupt-driven audio clock); no FM-1 has the card yet; for now started from the console (sd rec)",
     desc="Records what the FM-1 plays (the master, 16-bit stereo at 44.1 kHz) into a WAV on the SD card, "
          "RECORD/REC-0001.WAV and on. The audio goes into a buffer of this size and the card takes it a block at a "
          "time between the screen's strips (the screen and the card share a bus); a card that stalls longer than the "
          "buffer holds loses frames (counted), never writes noise. The file is one contiguous run sized for the "
          "length asked (a minute when none): a take that reaches its end stops there. While it records, the card "
          "does nothing else: samples from the card wait, the card's lists and saves say the card is busy. "
          "Needs LAYOUT (full FatFs) and SDCARD. The buffer is RAM of the pool for as long as the build has it.",
     off_warning="no recording to the card")
# (bit 412, OVERLAY, retired 2026-10-30: RETIRED; never reuse it)
_add("CPU_GUARD", "FELUCCA_CPU_GUARD", "predictive CPU guard (ease back before shedding)", Y, 61, default=0,
     provenance=FLOWSTATE_GUARD,
     desc="Avoids audio dropouts under heavy load by easing quality back step by step before cutting notes. Under "
          "overload (the measured load or the one predicted from the voices sounding, over 85 % for 8 halves, "
          "or a late half): first ACID without oversampling and ANALOG 2's swarm at 2 copies, then UNISON at 2 "
          "voices, then voices shed, never the bass or the lead (MONO / LEGATO / UNISON parts) nor the drums; back "
          "after 2 s under 80 % counting what each step saved; SYSTEM > INFO's GUARD line (SYS_DEBUG) shows the step. Weights: "
          "tools/builder/cpu_costs.py from tests/cpu_baseline.txt (docs/CPU-GUARD.md)")
# (bit 369, VOICE_OWN, retired 2026-10-29: a part at the voice budget always restarts its own released voice before
#  fading another part's (core/voice.c, JIANT 0.9.9 cpu3 da61bc62, GPL-3.0); RETIRED above; never reuse the bit)
_add("CACHE_RAM", "FELUCCA_CACHE_RAM", "more memory from the data cache (for the undo history)", Y, 92, default=0,
     choices=((0, "off"), (2, "8 KB (6 cache ways kept)"), (4, "16 KB (4 kept)"), (7, "28 KB (1 kept, as V15)")),
     target_only=True,
     tested="8 KB: in daily use on the user's FM-1 for days, no fault (2026-10-25); on one FM-1 2026-10-18: the boot test passes, cache RAM as fast as main RAM, flash data reads over 16 KB 57x slower (1.7 us vs 32 ns: flash data goes through the data cache)",
     desc="Uses 2, 4 or 7 of the 8 data-cache ways (4 KB each) as RAM; 7 is what M-VAVE's own FM-1 firmware and the AC79 SDK's flash-only "
          "builds do; the instruction cache (the flash's code and constants) keeps all its ways. The memory is tested "
          "at start-up; when the test passes it becomes the undo history's second part (28 KB instead of what main "
          "RAM has left), else everything is put back as it was and nothing uses it. It costs about 0.4 KB of flash and "
          "0.1 KB of fast RAM code. "
          "The flash's data reads go through the data cache: with fewer ways kept, flash data reads over 16 KB slow down (57x measured with 4 KB left), so 8 KB is the safe choice; the console's tabbench (flash 16K) and boot lines show it.")
_add("ASM", "FELUCCA_ASM", "hand-written assembly speed-ups (FM6, ANALOG 2)", Y, 56, target_only=True,
     desc="The hottest loops of FM6 and ANALOG 2 written in the processor's assembly: the same sound, about 8 to 19 % "
          "less audio CPU (emulator), for about 0.6 KB of flash and 0.7 KB of fast RAM code. Keep on; off only to "
          "compare with the plain C.")
# bit 442 (2026-10-29): the compiler (tools/toolchain.py: COMPILERS, LLVM_URL; the release build through tools/fm1base.py)
PI32V2_LLVM = Provenance("pi32v2-llvm (jvitkauskas/pi32v2-llvm, pi32v2-isa)", "Julius Vitkauskas",
                         "Apache-2.0 WITH LLVM-exception, MIT-0", "4f631b31b",
                         "https://github.com/jvitkauskas/pi32v2-llvm")
_add("COMPILER", "FELUCCA_COMPILER", "compiler: pi32v2-llvm or JieLi's clang", Y, 442, default=1, target_only=True,
     choices=((1, "pi32v2-llvm (smaller)"), (0, "JieLi clang 4")), provenance=PI32V2_LLVM,
     tested="on the FM-1 (2026-10-29): Optimist's own and the release build with pi32v2-llvm 4f631b31b, the user's "
            "project played and saved, no drop, no crash; the same sound as JieLi's in an A/B",
     desc="The C compiler the firmware is built with. pi32v2-llvm (Julius Vitkauskas's LLVM with a pi32v2 target) makes "
          "the same firmware 14 to 30 KB smaller than JieLi's own clang 4 (measured on the default profile and a full "
          "release configuration), the audio's CPU the same. JieLi's is the compiler the stock firmware and every "
          "earlier Optimist were built with. costs-all measures with the default (pi32v2-llvm); this item's cost at JieLi is "
          "how much bigger JieLi's makes the same firmware.")
# bit 443 (2026-10-30, the next free after COMPILER's 442): a flash-step setting, not firmware (no_image: nothing in
# the image, its hash or its costs). tools/build.py writes it beside the package (PACKAGE.install.json,
# tools/install_meta.py); the installers read it from there and show it (docs/INSTALL-PATHS.md)
M_BOOTLOADER = Provenance("m-bootloader (jvitkauskas/m-bootloader)", "Julius Vitkauskas", "MIT", "v1.2 22e2559",
                          "https://github.com/jvitkauskas/m-bootloader")
_add("BOOTLOADER", "", "bootloader: keep it, or install m-bootloader", Y, 443, default=0, no_image=True,
     choices=((0, "keep (as it is)"), (1, "m-bootloader")), provenance=M_BOOTLOADER,
     tested="host-tested only (the installers' choice and display); m-bootloader itself on FM-1s (its author's tests)",
     desc="What installing this build does to the FM-1's bootloader. Keep: it is never touched (the stock one, or "
          "m-bootloader if it is there already). m-bootloader: an open bootloader by Julius Vitkauskas (MIT): hold REC "
          "while switching on for the chip's download mode, PLAY for a recovery menu, even when a firmware does not "
          "start; it stays through firmware updates. With it chosen, the installers (make flash, the builder's f, "
          "tools/fm1_install.py) install it once if the FM-1 does not have it, through download mode with M-Flasher, "
          "after a full flash backup and your yes, then the firmware. The installer shows the choice before it writes, "
          "and its --keep-bootloader (a checkbox on the web) keeps the bootloader for one install. Nothing in the "
          "firmware changes.")
# (bit 57, SIMD, withdrawn 2026-10-12: the packed 16-bit forms compute something else on the real FM-1 than the emulator
#  assumes, a build with them crackles; the code stays (hal/fm1_simd.h, FELUCCA_SIMD default 0, the boot probe
#  FELUCCA_SIMD_PROBE through tools/build.py); RETIRED below; never reuse the bit)
# (bit 51, SPLASH, retired 2026-10-15: the dinghy boot logo is always built; never reuse the bit)

# ---- the second core
X = Y
JANGADA = Provenance("Jangada (zednaked/jangada)", "zednaked", "GPL-3.0-only", "07cce38",
                     "https://github.com/zednaked/jangada")
# (bit 58, DUAL, and bit 62, STOCK_SUPPLY, retired 2026-10-29: the second core renders parts 2-3 and the rails are raised to
#  the FM-1's stock values at start-up in every target build, ALWAYS_ON_TARGET below; RETIRED above; never reuse the bits.
#  Attribution kept: the second core's guard against misread RAM is ported from X0X by Charles Vestal (GPL-3.0, 47f8e4c), the
#  supply start-up from Melodee by Kerem Kilic (GPL-3.0, 7a51700), LICENSING.md and system/dual.c, system/supply.c)
_add("BLE", "FELUCCA_BLE", "Bluetooth LE MIDI, our own stack", M, 252, default=0,
     target_only=True,
     desc="BLE MIDI as the stock firmware offers it (named FM1-XXXX, the BLE-MIDI service), from a stack written "
          "for Optimist (docs/BLE-STACK.md): BLE in plays the synth, the FM-1's own notes go out with real timestamps. "
          "Tested on an FM-1 with macOS Audio MIDI Setup (not yet with iOS or Windows). "
          "The radio's start-up tables, captured from the stock V15 firmware, are included: no V15 file is needed "
          "(FM1_STOCK_FWSC makes a fresh capture instead, with the same result). "
          "Bluetooth starts OFF: switch it on in SYSTEM > Bluetooth. About 32 KB of flash and 6.5 KB of RAM (the menu "
          "shows the measured figures); where "
          "the build then overflows, ticking it removes the smallest single item that frees enough (sample sets "
          "first, then other items), or the one you pick instead.", symbols=("ble_in_q",),
     tested="tested on an FM-1 with macOS Audio MIDI Setup, not yet with iOS or Windows")
# (bit 253, BLE_DIAG, retired 2026-10-29: the BLE diagnostics removed; RETIRED above; never reuse the bit)
_add("BLE_BOND", "FELUCCA_BLE_BOND", "BLE pairing when a Mac or phone connects to the FM-1 (Just Works, encrypted, remembered)", M, 254, default=0,
     parent="BLE", target_only=True,
     desc="LE legacy pairing with Just Works (no passkey, as the FM-1 has no screen keypad for it) and "
          "bonding, so a Mac or phone that pairs once gets an encrypted link and finds the FM-1 bonded after a power-off. "
          "The FM-1 hands out its identity and takes the other side's (its IRK and identity address, kept until "
          "power-off for now), which reconnecting to devices with private addresses needs later. Pairing happens only when the other side asks for it. Paired "
          "with a Mac on an FM-1 (2026-10-18). About 3.6 KB of flash and 0.4 KB of RAM.",
     symbols=("bsmp",), tested="on an FM-1 with a Mac (2026-10-18)")
_add("BLE_CENTRAL", "FELUCCA_BLE_CENTRAL", "BLE devices: the FM-1 connects to BLE-MIDI devices", M, 255,
     default=0, parent="BLE", target_only=True,
     choices=((0, "off"), (1, "full: also pairs with devices that ask (passkey, private addresses)"),
              (3, "minimal: never pairs (controllers, another FM-1)")),
     desc="SYSTEM > BT MIDI lists the BLE-MIDI devices nearby (keyboards, controllers, "
          "a Mac or an iPhone app that advertises BLE MIDI, another FM-1), scanning while the page is on screen with no "
          "link (the FM-1 stops advertising meanwhile). Picking one connects to it as "
          "a central, MIDI both ways; it becomes the last device, which the FM-1 reconnects to by itself while "
          "Bluetooth is on (with up to 3 others remembered). Two levels, for the flash they take: FULL (1) pairs when the "
          "other side asks, with Just Works or, for an iPhone app that needs it, a 6-digit code shown on the FM-1, and "
          "finds the last device again when its address changes (Macs and iPhones use private addresses): everything "
          "works, 21.9 KB of flash and 2.3 KB of RAM. "
          "MINIMAL (3) never pairs: controllers and another FM-1 work; a device that asks to pair (a Mac or an iPhone "
          "app advertising BLE MIDI, as Apple's do, some controllers) fails with FAILED: NEEDS PAIRING: 13.4 KB and 1.6 KB (8.5 KB of flash less than FULL). Level 1 brings the bonding of BLE_BOND "
          "with it; at level 3 BLE_BOND still lets a Mac pair with the FM-1 when the Mac connects to it.",
     symbols=("ble_found",), tested="on an FM-1: an iPhone app reconnected as the last device (2026-10-18)")
_add("BT_AUDIO", "FELUCCA_BT_AUDIO", "Bluetooth Classic Audio (A2DP Source, SBC)", M, 281,
     default=0, target_only=True,
     tested="on the FM-1: AirPods, a Sony WH-1000XM4 and a Bose SoundLink Mini, with BLE MIDI and the Mirror app at once (the user, 2026-10-25); host-tested (SSP, SAFER+, SBC encoder, AVDTP, L2CAP)",
     desc="Bluetooth Classic audio out (A2DP source, SBC 44.1 kHz) to headphones and speakers: its own page, S"
          "YSTEM > BT AUDIO (MODE OFF, MAN, AUTO), beside BT MIDI: with BLE built, MIDI and audio can be on at "
          "the same time on the one radio. Our own stack: inquiry, paging, Secur"
          "e Simple Pairing (Just Works), E0 encryption, L2CAP, AVDTP and a fixed-point SBC encoder. Streams on "
          "the FM-1 to AirPods, a Sony WH-1000XM4 and a Bose SoundLink Mini (2026-10-23).",
     symbols=("ba_sbc", "bt_sbc_encode"))
# (bit 395, BT_AFH, retired 2026-10-29: Bluetooth audio always skips the channels the headset fails on, bt/bt_afh.c; RETIRED above;
#  never reuse the bit)
# (bit 363, BT_DIAG, retired 2026-10-29: the Bluetooth audio diagnostics removed; RETIRED above; never reuse the bit)
# Wi-Fi (2026-10-28; docs/WIFI-UI-DESIGN.md, docs/WIFI-LINK-HANDOVER.md). Needs BLE (the Wi-Fi driver is the BLE
# driver's radio, BLE_HW_WL82); on the classic firmware the USB console build too (the manager and the web editor's
# Wi-Fi command run through the CDC console; the release has them on fm1-base's own stack): core.validate says so. FELUCCA_LINK follows FELUCCA_WIFI (core.flags). The console's Wi-Fi test
# commands and the software CCMP (FELUCCA_WIFI_CON, FELUCCA_WIFI_SWCCMP) are not items: OPTIMIST_WIFI=1 (tools/build.py).
_add("WIFI_LINK", "FELUCCA_WIFI", "Wi-Fi + Link: join a network, Ableton Link as a clock", M, 434,
     default=0, target_only=True, experimental=True,
     tested="on an FM-1 as far as receive and decrypt (docs/WIFI-LINK-HANDOVER.md); DHCP and a Link session not through yet",
     desc="The FM-1 joins a Wi-Fi network (WPA2-PSK with CCMP) on the radio BLE already uses: SYSTEM > WI-FI scans, lists "
          "the access points heard, keeps the saved networks and joins one (the password typed on the keys), and the web "
          "editor has a Wi-Fi tab. Ableton Link is a clock source: SYNC gets LINK (AUTO ranks it first), the session's "
          "tempo and beat are the MIDI clock. Needs BLE (the same radio, the on-chip driver). On Optimist's own "
          "firmware it needs the serial console (USB_CONSOLE) too: the Wi-Fi manager and the editor's Wi-Fi command run "
          "on it; the release runs them on fm1-base's own Wi-Fi stack (wifi_stack = own), with no console. Off by default. Measured 2026-10-28 on the bt-debug configuration, with and without it: "
          "26.6 KB of flash, 1.9 KB of RAM and 3.7 KB of the pool. Open and WPA3-only networks, TKIP and networks "
          "that need protected management frames are shown grey and cannot be joined. The console's Wi-Fi test "
          "commands and the software CCMP are not part of it (the OPTIMIST_WIFI=1 dev build, +9.9 KB of flash).",
     symbols=("lk_poll",))
_add("WIFI_AP", "FELUCCA_WIFI_AP", "Wi-Fi AP: the FM-1 as an access point (MODE AP)", M, 435,
     default=0, parent="WIFI_LINK", target_only=True, experimental=True,
     tested="host-tested (the authenticator against our own station, the daemon over a stand-in radio, the page, the web command); the radio side is not tested on an FM-1 yet",
     desc="MODE AP on the SYSTEM > WI-FI page: the FM-1 is the Wi-Fi access point, so a phone, a laptop or Ableton Live joins "
          "it directly, with no router (a gig). MODE gets two more values, AP AUTO (starts at power-on) and AP MAN (started and stopped with SAVE): the page shows the network's name (FM1- and "
          "four digits of the radio's address, or one set from the web editor), the channel (1 to 13) and the state; "
          "SAVE types the password (8 to 63 characters, the same entry as a network's), SAVE held clears the key. No access "
          "point runs without a password; only the key made from it is kept. WPA2 personal (AES), up to 4 stations, "
          "addresses 192.168.4.2 to .5 from the FM-1's own DHCP server, Link runs on that network. The web editor's Wi-Fi "
          "tab gets the same. Needs Wi-Fi + Link. Not tested on the device yet (host-tested only): the radio side may need "
          "changes. Off by default. Measured 2026-10-28 on the bt-debug configuration, with and without it (Wi-Fi + Link "
          "on in both): 8.1 KB of flash, no RAM and 1.8 KB of the pool.")
# USB MIDI host (2026-10-28): fm1-base's (fm1.toml usb_host = true, felucca-bsp's chip/usb_host.c, ported from Melodee
# with its authors' permission); the release only: Optimist's own firmware has no USB host (core.validate says so).
# Bit 438 (436 / 437: MEMORY / RAM_PACKED, retired).
_add("USB_HOST", "FELUCCA_USB_HOST", "USB MIDI host: a USB-MIDI keyboard, controller or synth plugged into the FM-1", M, 438,
     default=0, experimental=True,
     tested="host-tested only; USB host IN ran on Melodee, OUT not validated on a device",
     off_warning="no USB-MIDI keyboard or synth on the FM-1's own USB port (a computer's USB MIDI works as always)",
     desc="The FM-1's USB port as a MIDI host: a class-compliant USB-MIDI keyboard, controller or synth plugged into "
          "the FM-1, directly or behind one powered hub. The FM-1 gives the port no power: a powered hub or a powered "
          "USB adapter is needed. SYSTEM > USB: MODE DEVICE or HOST, and DEVICE, the device's name as it gives it (else "
          "CONNECTED, NONE, NO MIDI). Its notes play the tracks on their channels as any MIDI input, its clock can be "
          "followed (CLK IN USB or AUTO), and the FM-1's notes and CLOCK OUT go to it. HOST is the port's only use "
          "meanwhile: no computer (no USB MIDI to a Mac, no USB audio, no web editor or installer over USB) until MODE "
          "is DEVICE again or the FM-1 is switched off. HOST is never remembered: the FM-1 always starts as a device, "
          "so it is never locked out of its computer. The release only (Optimist as an fm1-base app: fm1-base's USB "
          "host, from Melodee); Optimist's own firmware has none. Off by default. Host-tested only (the host "
          "simulator's USB device, felucca-bsp's model of the USB controller): USB host input ran on Melodee, output "
          "is not validated on a device yet.")

# Where each engine's hot code runs (the user, 2026-10-29): the engine's item is a choice, off / on (its render path
# from the XIP flash, through the flash cache) / on, code in RAM (RAM code: out of the cache's way, its bytes taken from
# the free RAM, about two thirds of them in flash: the boot stream codes RAM code). The firmware's switch per engine
# (core.h FAST_RAM_<name>, through FAST): FAST_NAMES. Defaults: where each ran until then (FAST_RAM:
# ANALOG 2 with SUPER, PHASE, DIGITAL, TRIO, FORMANT, DRAWBAR, LOFI, SAMPLE; the rest from the flash). The values are
# 0 / 2 / 3: a .config's 1 (an engine "on" before) reads as the engine's place by default (core.parse), so an old
# profile builds what it built. FAST_BYTES: the render path's code, bytes (the release's ELF, fm1-next 0efe7cd6, jam
# configuration, each engine moved alone; tools/builder/core.py's estimate for a place costs.json has not measured)
ENG_OFF, ENG_ON, ENG_RAM = 0, 2, 3
ENG_LEGACY_ON = 1
FAST_NAMES = {"ENG_ANALOG": ("analog", "super"), "ENG_PHASE": ("phase",), "ENG_DIGITAL": ("digital",),
              "ENG_TRIO": ("trio",), "ENG_FORMANT": ("formant",), "ENG_DRAWBAR": ("drawbar",), "ENG_LOFI": ("lofi",),
              "ENG_SAMPLE": ("sample",), "ENG_FM6": ("fm6",), "ENG_ACID": ("acid",), "ENG_PLAITS": ("plaits",),
              "ENG_PHYS": ("phys",), "ENG_PROPHET": ("prophet",), "ENG_JUNO": ("juno",), "ENG_CZ": ("cz",),
              "ENG_STRINGS": ("strings",)}
FAST_RAM = frozenset({"ENG_ANALOG", "ENG_PHASE", "ENG_DIGITAL", "ENG_TRIO", "ENG_FORMANT", "ENG_DRAWBAR", "ENG_LOFI",
                      "ENG_SAMPLE"})
FAST_BYTES = {"ENG_ANALOG": 5132, "ENG_PHASE": 2260, "ENG_DIGITAL": 1314, "ENG_TRIO": 2812, "ENG_FORMANT": 2412,
              "ENG_DRAWBAR": 1512, "ENG_LOFI": 956, "ENG_SAMPLE": 644, "ENG_FM6": 8000, "ENG_ACID": 12816,
              "ENG_PLAITS": 6408, "ENG_PHYS": 8904, "ENG_PROPHET": 3344, "ENG_JUNO": 2596, "ENG_CZ": 3832,
              "ENG_STRINGS": 1552}
# the flash an engine's render path in RAM saves over the same in XIP, measured where FAST_BYTES was (release builds of
# the jam configuration, fm1-next 0efe7cd6 + this branch, the engine moved alone); else FAST_FLASH_RAM's model
FAST_FLASH_SAVED = {"ENG_ANALOG": 1776, "ENG_FM6": 600, "ENG_ACID": 4616, "ENG_PLAITS": 1572, "ENG_PHYS": 2444,
                    "ENG_PROPHET": 1160, "ENG_JUNO": 664, "ENG_CZ": 876, "ENG_STRINGS": 488}
FAST_FLASH_RAM = 0.67    # flash a RAM-code byte takes (the boot stream's coding; measured 0.665 and 0.705, probe B, D)


def fast_flash_saved(key):
    """the flash this engine's render path saves in RAM (measured, else the model)"""
    return FAST_FLASH_SAVED.get(key, round(FAST_BYTES.get(key, 0) * (1 - FAST_FLASH_RAM)))


def eng_place(key):
    """an engine item's place when on by default (ENG_ON / ENG_RAM)"""
    return ENG_RAM if key in FAST_RAM else ENG_ON


def _eng_fast(it):
    """an engine item of FAST_NAMES as its three-way choice (its default: off, or on at its place)"""
    if it.key in FAST_NAMES and not it.choices:
        it.choices = ((ENG_OFF, "off"), (ENG_ON, "on"), (ENG_RAM, "on, code in RAM"))
        it.default = eng_place(it.key) if it.default else ENG_OFF


for _it in _ITEMS:
    _eng_fast(_it)
ITEMS = {it.key: it for it in _ITEMS}


def _level_ok(it):
    """one level of options; two under a group's head (Item.head: DRUMS > DRUM_X0X909 > X909_CYM)"""
    p = ITEMS.get(it.parent)
    return p is not None and (not p.parent or (not p.head and ITEMS[p.parent].head))


for _it in _ITEMS:
    if _it.parent:
        assert _level_ok(_it), f"{_it.key}: one level only (two under a group's head)"
        ITEMS[_it.parent].children.append(_it.key)
for _it in _ITEMS:                                   # the menu's tree: shown_under over parent, hidden items left out
    _up = _it.shown_under or _it.parent
    if _up and not _it.hidden:
        assert _it.shown_under is None or _it.shown_under in ITEMS, f"{_it.key}: shown_under names no item"
        ITEMS[_up].menu_children.append(_it.key)
assert len({it.bit for it in _ITEMS}) == len(_ITEMS), "registry: duplicate bit"
assert all(k not in ITEMS for k in FORBIDDEN), "registry: a forbidden item is registered"
for _it in _ITEMS:
    if _it.provenance and "fm1-x0x" in _it.provenance.project + _it.provenance.url:
        assert _it.notice, f"{_it.key}: an X0X item needs its notice"


# the items of other groups that only drums use: left out with DRUMS (core.built), their switches 0 (core.flags)
DRUMS_ONLY = ("FX_DUCK", "ENERGY", "LANE_FX", "GRIDS_EUCLID", "GRIDS_LFO", "GRIDS_VEL",
              "SL24_SAFE")   # (SLOOP's data kept safe: a drum track)


def ancestors(key):
    """the item's parent, its parent's parent (a group's head)"""
    out, p = [], ITEMS[key].parent
    while p:
        out.append(p)
        p = ITEMS[p].parent
    return out


def top_level(group=None):
    return [it for it in _ITEMS if not it.parent and not it.hidden and (group is None or it.group == group)]


def add_item(item):
    """register an item at import time (backported features add themselves here: tools/builder/backports.py)"""
    assert item.key not in ITEMS and item.key not in FORBIDDEN
    assert item.bit not in {it.bit for it in _ITEMS}, f"{item.key}: bit {item.bit} taken"
    if item.provenance and "fm1-x0x" in item.provenance.project + item.provenance.url:
        assert item.notice, f"{item.key}: an X0X item needs its notice"
    _eng_fast(item)
    _ITEMS.append(item)
    ITEMS[item.key] = item
    if item.parent:
        assert _level_ok(item), f"{item.key}: one level only (two under a group's head)"
        ITEMS[item.parent].children.append(item.key)
    _up = item.shown_under or item.parent
    if _up and not item.hidden:
        ITEMS[_up].menu_children.append(item.key)   # (the menu's tree, as the loop above makes it for the registry's own items)


# Values a build kind cannot build (the registry's fact, read by core.validate, the menu, and measure_costs.py, which records them as
# "not in this build" in that kind's costs file and probes them: a mark whose value links again is reported). Key: (item, value);
# value: {"classic" (Optimist's own firmware) / "release" (the fm1-base app): why}. Not a failure of the measuring: a value
# that SHOULD build and does not is (measure_costs.py reports it loudly)
_S0_RELEASE = ("not for the release yet: fm1-base's own update loader and app_end do not take the MEMORY steps' table "
               "(its slot switch would leave the old step's log sectors in the new one's; docs/SD-MODE-DESIGN.md 9.4, "
               "the proposed fm1-base branch pr/app-end-table)")
# (PHYS in RAM was a NOT_BUILT entry until its render stopped calling libgcc's __udivdi3: phys_dsp.c px_udiv, 2026-10-30)
NOT_BUILT = {
    ("MEMORY", 4): {"release": _S0_RELEASE},
    ("LAYOUT", 2): {"release": _S0_RELEASE},
    ("LAYOUT", 3): {"release": _S0_RELEASE},
    ("LAYOUT", 4): {"release": _S0_RELEASE},
    ("LAYOUT", 5): {"release": _S0_RELEASE},
    ("LAYOUT", 6): {"release": _S0_RELEASE},
    ("LAYOUT", 7): {"release": _S0_RELEASE},
    ("LAYOUT", 8): {"release": _S0_RELEASE},
}


def not_built(key, value, kind):
    """-> why this build kind ("classic" / "release") cannot build item key at value, else an empty string"""
    return NOT_BUILT.get((key, value), {}).get(kind, "")


def to_json():
    out = []
    for it in _ITEMS:
        d = {k: getattr(it, k) for k in ("key", "flag", "label", "group", "bit", "default", "parent", "desc",
                                          "experimental", "notice", "off_warning", "env", "target_only", "symbols", "no_image",
                                          "fixed", "head")}
        d["choices"] = [list(c) for c in it.choices]
        d["not_built"] = {str(v): why for (k, v), why in NOT_BUILT.items() if k == it.key}
        d["provenance"] = it.provenance.__dict__ if it.provenance else None
        out.append(d)
    return {"groups": GROUPS, "items": out, "forbidden": FORBIDDEN, "drums_only": list(DRUMS_ONLY)}


# the symbol check (tools/builder/verify.py): what an item left out must not leave behind
_SYMS = {
    "ENG_ANALOG": ("ENG_ANALOG", "analog_render"), "ENG_DIGITAL": ("ENG_DIGITAL", "digital_render"),
    "ENG_PHASE": ("ENG_PHASE", "pd_wave"), "ENG_LOFI": ("ENG_LOFI", "lofi_render", "WRAM"),
    "ENG_SAMPLE": ("ENG_SAMPLE",), "ENG_FORMANT": ("ENG_FORMANT", "formant_render"),
    "ENG_TRIO": ("ENG_TRIO", "trio_render"), "ENG_DRAWBAR": ("ENG_DRAWBAR", "drawbar_render"),
    "ENG_GRAIN": ("ENG_GRAIN", "grain_block"), "ENG_FM6": ("ENG_FM6", "fm6_pt", "FM6_INIT", "fm6k_knob"),
    "FM6_KEYS": ("fm6k_knob",), "FM6_PK_KEYS": ("FM6_PK_KEYS",), "FM6_PK_BASS": ("FM6_PK_BASS",),
    "FM6_PK_BELLS": ("FM6_PK_BELLS",), "FM6_PK_PADS": ("FM6_PK_PADS",), "FM6_PK_80S": ("FM6_PK_80S",),
    "ENG_SLICE": ("ENG_SLICE",), "ENG_DRONE": ("ENG_DRONE", "drone_render"), "ENG_PLAITS": ("ENG_PLAITS", "plaits_render"),
    "ENG_STRINGS": ("ENG_STRINGS", "strings_render"), "ENG_VPS": ("ENG_VPS", "vps_render"),
    "ENG_TERRAIN": ("ENG_TERRAIN", "terrain_render"), "ENG_WSEQ": ("ENG_WSEQ", "wseq_render"),
    "ENG_VECTOR": ("ENG_VECTOR", "vector_render"), "ENG_SID": ("ENG_SID", "sid_render"),
    "ENG_PROPHET": ("ENG_PROPHET", "prophet_render"), "ENG_WAVE": ("ENG_WAVE", "wave_render", "wt_mip"),
    "DRUM_SYNTH": ("DS_KITS",), "DRUM_EDIT": ("de_synth",), "DS_KAWA": ("DS_XSNDS_0",), "DS_PLAITS": ("kpunc_tick", "scrak_tick"), "FX_CHORUS": ("cho_buf",),
    "FX_DELAY": ("dly_buf",), "FX_REVERB": ("rev_line", "rev_ap"), "FX_SLICER": ("sl_buf",),
    "FX_PUNCH": ("punch_ring",), "EDITOR": ("ed_handle",),
    "DRUM_X0X909": ("drum909_trigger", "x0x_smp_hh_m"),
    "X909_CYM": ("x0x_smp_ride_m", "x0x_smp_crash_m"), "DRUM_X0X808": ("drum808_trigger",),
    "X909_HAT": ("x0x_smp_hh_m",), "X0X_TANH": ("x0x_tanh_tab",),
    "CPU_GUARD": ("cg", "cg_post", "CG_VCOST"),
}
for _k, _v in _SYMS.items():
    ITEMS[_k].symbols = _v
