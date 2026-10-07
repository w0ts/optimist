# SPDX-License-Identifier: GPL-3.0-only
"""The firmware builder's registry: every item a build may leave out, its switch, its options, where it came from.

One source of truth for tools/builder/configure.py (the .config -> build/gen/felucca_config.h), the budget
(costs.json, measured by tools/builder/measure_costs.py) and the menu (tools/menuconfig). The firmware side of
each switch is in firmware/src/registry.h (engines, kits, FX, FM6 options, features) or in the existing flags.

Rules (docs/BUILDER.md):
  - one level: a top-level item and its own options; an option never depends on another option or on another
    top-level item; options are ignored while their parent is off;
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

# Never offered: used by X0X only by permission of its author, no licence (FM1-SCENE-2026-10.md section 2)
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


MELODEE_FM6 = Provenance("Melodee (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "ebfe60e", MELODEE_URL)
MELODEE_MIDI = Provenance("Melodee (keremimo/melodee)", "ChanceTheMaker, Kerem Kilic", "GPL-3.0-only", "670193c",
                          MELODEE_URL)
MELODEE_USB = Provenance("Melodee (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "", MELODEE_URL)
FELUCCA = Provenance("Felucca (hugelton/Felucca)", "Leo Kuroshita (Hügelton)", "GPL-3.0-only", "", FELUCCA_URL)
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
    off_warning: str = ""            # shown when switched off
    env: str = ""                    # generator environment (sample sets: FELUCCA_SAMPLES_SKIP)
    target_only: bool = False        # only for the target build (the host tests keep their own default)
    symbols: tuple = ()              # ELF symbols that must be gone (or <= 4 B) when the item is off
    children: list = field(default_factory=list)

    @property
    def is_choice(self):
        return bool(self.choices)


GROUPS = ["Synth engines", "Drums", "Sample sets", "FX", "Sequencer", "MIDI & USB", "UI", "System", "Experimental"]

_ITEMS = []


def _add(*a, **k):
    it = Item(*a, **k)
    _ITEMS.append(it)
    return it


# ---- engines (UID = FUN7 number, firmware/src/registry.h ENGINE_LIST)
E = "Synth engines"
_add("ENG_ANALOG", "FELUCCA_ENG_ANALOG", "ANALOG 2 (VA, swarm, 2 filters)", E, 0)
_add("ENG_DIGITAL", "FELUCCA_ENG_DIGITAL", "DIGITAL (4-op FM-style)", E, 1)
_add("ENG_PHASE", "FELUCCA_ENG_PHASE", "PHASE (phase distortion)", E, 2)
_add("ENG_LOFI", "FELUCCA_ENG_LOFI", "LOFI (wavetable)", E, 3)
_add("ENG_SAMPLE", "FELUCCA_ENG_SAMPLE", "SAMPLE (sample sets, USR slots)", E, 4)
_add("ENG_FORMANT", "FELUCCA_ENG_FORMANT", "VOICE (formant)", E, 5)
_add("ENG_TRIO", "FELUCCA_ENG_TRIO", "TRIO (3 oscillators)", E, 6)
_add("ENG_DRAWBAR", "FELUCCA_ENG_DRAWBAR", "WHEEL (drawbar organ)", E, 7)
_add("ENG_GRAIN", "FELUCCA_ENG_GRAIN", "GRAIN (granular on the sample sets)", E, 8)
_add("ENG_FM6", "FELUCCA_ENG_FM6", "FM6 (DX7, bit-exact with Dexed)", E, 9, provenance=MELODEE_FM6)
_add("FM6_MARK1", "FELUCCA_FM6_MARK1", "ENGINE mode MARK I", E, 10, parent="ENG_FM6",
     desc="the DX7's log-sine resolution (4.1 KB RAM tables)")
_add("FM6_MKI_FLASH", "FELUCCA_FM6_MKI_FLASH", "MARK I tables in flash", E, 89, default=0, parent="ENG_FM6",
     desc="with MARK I: its log-sine and exponent tables as generated const data in flash instead of RAM tables built "
          "at boot (4 KB of RAM less, 1.9 KB of flash more). CPU cost on the FM-1 unknown: the emulator models neither "
          "the XIP cache nor flash wait states",
     notice="measured in the emulator only, which has no XIP cache and no flash wait states; "
            "on the FM-1 every MARK I operator sample reads flash twice: the CPU cost there is not measured.")
_add("FM6_MODERN", "FELUCCA_FM6_MODERN", "ENGINE mode MODERN", E, 11, parent="ENG_FM6",
     desc="MSFA 24-bit (the sine table stays: the LFO uses it)")
_add("FM6_OPL", "FELUCCA_FM6_OPL", "ENGINE mode OPL", E, 12, parent="ENG_FM6", desc="OPL resolution (1.5 KB tables)")
_add("FM6_KEYS", "FELUCCA_FM6_KEYS", "operator editor on the black keys", E, 13, parent="ENG_FM6")
_add("FM6_SYSEX", "FELUCCA_FM6_SYSEX", "DX7 SysEx in / SEND (web editor FM6 tab)", E, 14, parent="ENG_FM6",
     desc="voice, bank and parameter changes from Dexed or the web editor; with the STORE buffer: 4.1 KB RAM")
_add("FM6_VOICES", "FELUCCA_FM6_VOICES", "16 factory voices R01..R16", E, 15, parent="ENG_FM6",
     desc="off: R voices play INIT VOICE")
_add("FM6_ALL", "FELUCCA_FM6_ALL", "operator editor in VIEW ALL (pages as rows)", E, 80, parent="ENG_FM6",
     desc="with VIEW ALL: an operator's pages (or PIT, GLO) as rows of 4 x 4 PAGEs; off: one page at a time")
_add("FM6_ALGO", "FELUCCA_FM6_ALGO", "ENV held: the algorithm full screen", E, 81, parent="ENG_FM6")
_add("FM6_STORE", "FELUCCA_FM6_STORE", "user bank STORE (U01..U32 in a USR slot)", E, 16, parent="ENG_FM6")
_add("ENG_SLICE", "FELUCCA_ENG_SLICE", "SLICE (break slicer + its BREAK sample)", E, 17, default=0,
     desc="Felucca's slicer with its built-in break (22 KB of samples)", provenance=FELUCCA)

# ---- drums
D = "Drums"
_add("DRUM_SYNTH", "FELUCCA_DRUM_SYNTH", "drum synth (all 32 synthesised kits)", D, 18)
_add("DRUM_SAMPLED", "FELUCCA_DRUM_SAMPLED", "sampled drums (the PERC set, 99 KB)", D, 19, env="PERC")
for i, (k, n) in enumerate((("ACOUSTIC", "ACOUSTIC"), ("DEEP", "DEEP"), ("TIGHT", "TIGHT"), ("BRIGHT", "BRIGHT"),
                            ("DUST", "DUST"))):
    _add(f"KIT_{k}", f"FELUCCA_KIT_{k}", f"kit {n}", D, 20 + i, parent="DRUM_SAMPLED")
_add("DRUM_EDIT", "FELUCCA_DRUM_EDIT", "drum sound editor (EDIT on the drum track)", D, 25)
_add("DRUM_USR", "FELUCCA_DRUM_USR", "user samples on drum lanes", D, 26)
_add("DRUM_KITS", "FELUCCA_DRUM_KITS", "user drum kits (bank of 16 in data flash)", D, 27)
_add("DRUM_SENDS", "FELUCCA_DRUM_SENDS", "per-lane drum sends (REV / DLY / CHO)", D, 83,
     desc="SOUND 3: each drum lane's own reverb, delay and chorus sends (the drum record keeps them in every build)")
X0X_DRUMS_NOTICE = ("Ported from X0X by Charles Vestal (GPL-3.0), itself from 9W9 / 8W8 by athousanddetails and ER-99 by "
                    "Matthew Cieplak (GPL-3.0); the 808's rim shot after sc808 (Yoshinosuke Horiuchi / Sam Aaron, MIT). "
                    "Experimental in Optimist: float DSP, emulator-tested only.")
_add("DRUM_X0X909", "FELUCCA_DRUM_X909", "X0X 909 kit (circuit-modelled TR-909)", D, 86, default=0, experimental=True,
     provenance=x0x("80b7d40"), notice=X0X_DRUMS_NOTICE,
     desc="kit UID 37: X0X's TR-909 (9W9's models of BD SD toms RS CP; hi-hats, ride and crash: ER-99's samples, "
          "8-bit block float) on the 16 lanes; SHAKER, CONGA, COWBELL play the synthesised 909's. A build without "
          "it plays the synthesised 909 for it and keeps the kit")
_add("X909_CYM", "FELUCCA_X909_CYM", "its ride and crash samples", D, 87, parent="DRUM_X0X909",
     choices=((1, "8-bit (93 KB, 42 dB)"), (2, "6-bit (70 KB, 30 dB)"), (0, "off")),
     desc="8-bit block floating point as before; 6-bit: 22 KB less, 30 dB against the 16-bit source instead of 42 "
          "(screens x0xdrums-perf-2026-10-06); off: RIDE and CRASH play the synthesised 909's (the hi-hat samples stay)")
_add("DRUM_X0X808", "FELUCCA_DRUM_X808", "X0X 808 kit (circuit-modelled TR-808)", D, 88, default=0, experimental=True,
     provenance=x0x("80b7d40"), notice=X0X_DRUMS_NOTICE,
     desc="kit UID 38: X0X's TR-808 (8W8's models, 16 sounds) on the 16 lanes; MIDI also plays MT, LC, HC and the "
          "claves (note 75). A build without it plays the synthesised 808 for it and keeps the kit")

# ---- sample sets (generator: FELUCCA_SAMPLES_SKIP; set numbers stay)
S = "Sample sets"
for i, (k, n) in enumerate((("PIANO", "PIANO (Steinway, 44 KB)"), ("BASS", "BASS (39 KB)"), ("VIBES", "VIBES (33 KB)"),
                            ("HORNS", "HORNS (26 KB)"), ("STRGS", "STRINGS (20 KB)"), ("FLUTE", "FLUTE (31 KB)"),
                            ("SCRCH", "SCRATCH (23 KB)"))):
    _add(f"SET_{k}", "", n, S, 28 + i, env=k, desc="SAMPLE / GRAIN; left out, it can still be uploaded to a USR slot")

# ---- FX
F = "FX"
_add("FX_DIST", "FELUCCA_FX_DIST", "DIST (per-track drive)", F, 35)
_add("FX_CHORUS", "FELUCCA_FX_CHORUS", "chorus send bus", F, 36)
_add("FX_DELAY", "FELUCCA_FX_DELAY", "delay send bus", F, 37)
_add("DLY_LEN", "FELUCCA_DLY_LEN", "delay length", F, 38, default=65536, parent="FX_DELAY",
     choices=((65536, "1.49 s (128 KB pool)"), (32768, "0.74 s (64 KB)"), (16384, "0.37 s (32 KB)")))
_add("FX_REVERB", "FELUCCA_FX_REVERB", "reverb send bus", F, 39)
_add("REV_POOL", "FELUCCA_REV_POOL", "reverb lines in the pool", F, 122, default=0, parent="FX_REVERB",
     desc="the four delay lines (17 KB; 8.7 KB at half rate) in the pool instead of main RAM: main RAM is the "
          "scarcer, the sound and the code the same. Needs that much pool free (the undo history shrinks by it in "
          "the pool and grows by it in RAM)")
_add("REV_HALF", "FELUCCA_REV_HALF", "reverb at half rate (22.05 kHz)", F, 123, default=0, parent="FX_REVERB",
     desc="the reverb's tank at 22.05 kHz behind a half-band filter: its lines take half the RAM (-8.7 KB) and it "
          "costs less CPU; the same decay and room size. The reverb loses its top octave (above ~11 kHz); the "
          "dry sound and the other buses are untouched", symbols=("rev_half",))
_add("REVERB", "FELUCCA_REVERB", "reverb tank", F, 150, default=0, parent="FX_REVERB",
     choices=((0, "ROOM (4-line FDN, 44.1 kHz)"), (1, "PLATE (Dattorro, 22.05 kHz)"), (2, "FDN8 (8 modulated lines, 22.05 kHz)")),
     desc="EXPERIMENTAL (exp/reverb). ROOM: today's four lines. PLATE: Dattorro's figure-of-eight plate, dense from the "
          "first 50 ms. FDN8: eight modulated lines, a Householder matrix. Both new tanks run at 22.05 kHz in one 16 KB "
          "ring (REV_HALF: 8 KB), 3.4 KB less RAM than the ROOM; they lose the top octave above ~11 kHz",
     symbols=("rv",))
_add("FX_SLICER", "FELUCCA_FX_SLICER", "SLICER (stutter / gate insert)", F, 40)
_add("SL_LEN", "FELUCCA_SL_LEN", "SLICER capture", F, 41, default=4096, parent="FX_SLICER",
     choices=((4096, "186 ms (32 KB pool)"), (2048, "93 ms (16 KB)")))
_add("FX_PUNCH", "FELUCCA_FX_PUNCH", "PUNCH (16 punch-in FX)", F, 42)
_add("PUNCH_N", "FELUCCA_PUNCH_N", "PUNCH ring", F, 43, default=32768, parent="FX_PUNCH",
     choices=((32768, "0.74 s (64 KB pool)"), (16384, "0.37 s (32 KB)")))
_add("FX_DJF", "FELUCCA_FX_DJF", "DJ filter (MASTER FILT)", F, 44)
_add("FX_DUST", "FELUCCA_FX_DUST", "DUST (vinyl / lo-fi master)", F, 45)
_add("FX_DUCK", "FELUCCA_FX_DUCK", "DUCK (kick ducks the parts)", F, 46)

# ---- sequencer
Q = "Sequencer"
_add("SECTIONS", "FELUCCA_SECTIONS", "song sections", Q, 85, default=16,
     choices=((16, "16: A..P, compressed log"), (8, "8: A..H, compressed log"), (4, "4: A..D, the old project slots")),
     desc="8 / 16: banks of 4 (SAVE + OCT), stored compressed in one 32 KiB log with a MEM gauge; the old slots move "
          "in at the first start. 4: the slots as before (needed by motion recording)")
_add("SNAPSHOTS", "FELUCCA_SNAPSHOTS", "snapshots (whole-state slots)", Q, 137, default=4,
     choices=((4, "4 slots: 32 KiB, USR3 keeps 32 KiB"), (2, "2 slots: 24 KiB, USR3 keeps 40 KiB"),
              (8, "8 slots: 48 KiB, USR3 keeps 16 KiB"), (0, "off: USR3 64 KiB")),
     desc="SAVE > SNAPSHOT: the working project, every section and the song in one slot, loaded back whole (the state "
          "before is kept in BEFORE LOAD); export / import in the web editor. The flash comes from the end of USR3 "
          "(slots + 4 sectors of 4 KiB): the user sample slot USR3 holds that much less (docs/SNAPSHOTS.md)",
     symbols=("sn_scan", "sn_load", "ed_snap"))
_add("UNDO_HISTORY", "FELUCCA_UNDO_HISTORY", "undo / redo history (many levels)", Q, 69,
     desc="EDIT + OCT- / OCT+: the history lives in the pool and RAM this build leaves free (at least 1 KiB); "
          "off: one level")
FLOWSTATE = Provenance("Flowstate (zakariachowdhury/flowstate-fm1)", "Zakaria Chowdhury", "GPL-3.0-only", "3962560",
                       "https://github.com/zakariachowdhury/flowstate-fm1")
_add("MACROS", "FELUCCA_MACROS", "performance macros (GLO > MACRO)", Q, 120, default=0, provenance=FLOWSTATE,
     desc="COLOR, MOTION, SPACE, ENERGY: four knobs, each moving several sounds' parameters at once (filters and FM "
          "index, LFO depths, sends and width, drive and drum level), kept per project and section (the drum track's "
          "unused ENV / LFO DEST values: no format change); recorded by motion recording. At home: no change",
     symbols=("mac_pre", "mac_post", "MAC_ROWS"))
_add("ENERGY", "FELUCCA_ENERGY", "ENERGY bands thin / thicken the drums", Q, 121, default=0, parent="MACROS",
     provenance=FLOWSTATE,
     desc="ENERGY also walks five bands on the beat: core lanes on the eighths, no ghosts, as written, harder hits, "
          "hat ratchets and a snare fill every second pass", symbols=("EN_EDGE",))

# ---- MIDI & USB
M = "MIDI & USB"
_add("USB_MODE", "", "USB port", M, 47, default=2,
     choices=((1, "CDC serial console"), (2, "USB audio (EXPERIMENTAL)"), (0, "MIDI only")),
     desc="USB audio: 4 stems in + stereo out (UAC1); it replaces the console and needs +12 KB pool",
     provenance=MELODEE_USB)
_add("UART", "FELUCCA_UART", "TRS MIDI IN", M, 48)
_add("MIDI_CLOCK", "FELUCCA_MIDI_CLOCK", "MIDI clock in (SYNC AUTO TRS > USB > INT)", M, 49)
_add("UA_RESAMPLE", "FELUCCA_UA_RESAMPLE", "USB audio: resample to the host clock", M, 71, default=0,
     experimental=True, desc="only with USB audio: the capture follows the host's clock instead of packet-size "
     "feedback", provenance=x0x("80b7d40"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): USB audio resampler. Experimental in Optimist; "
            "CPU-heavy (a resampler in the audio path), only with USB audio.")
_add("MIDI_EXPR", "FELUCCA_MIDI_EXPR", "MIDI expression (bend, mod, sustain, RPN)", M, 50, provenance=MELODEE_MIDI)

# ---- UI
U = "UI"
_add("SPLASH", "FELUCCA_SPLASH", "boot logo", U, 51,
     desc="the Optimist logo (drawn, no bitmap), the name and the version for 0.9 s at power-on; off: a dark "
     "screen until the UI")
_add("ICONS", "FELUCCA_ICONS", "parameter icons", U, 52)
_add("OVERVIEW", "FELUCCA_OVERVIEW", "VIEW ALL overview (4 x 4 PAGEs)", U, 53,
     desc="GLO > SYSTEM VIEW ALL: a page family at once, 4 rows x 4 knobs a PAGE, PAGE n/m")
_add("OV_ARP", "FELUCCA_OV_ARP", "ARP graph and ARP in VIEW ALL", U, 82, parent="OVERVIEW")
_add("MISSING_WARN", "FELUCCA_MISSING_WARN", "say what a project uses and this build lacks", U, 90,
     desc="'MISSING: PHYS T2, KIT 909' in the top bar when a project, song section, user preset or kit uses an "
          "engine, kit, sample set or FX this build leaves out (once per item until power-off; never stalls the "
          "audio); SAVE > TOOLS > MISS lists them again. Off: they play their stand-ins silently")
_add("PARAM_HELP", "FELUCCA_PARAM_HELP", "help line: what the knob changes, in words", U, 138, default=0,
     desc="while a knob turns, the top bar (the live screens' header) names its value in plain words, e.g. 'Filter "
          "cutoff', 'Reverb send', until ~1 s after the last detent; only the lines of the features built (tools/param_help.json, also the "
          "web editor's tooltips). No RAM",
     symbols=("ph_find", "PH_BLOB"))
_add("KNOB_ACCEL", "FELUCCA_KNOB_ACCEL", "knob acceleration by turn speed", U, 67,
     desc="1 / 2 / 3 / 5 / 8 steps a detent when turned fast; never on lists (engines, kits, presets)",
     provenance=x0x("61654ba"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): knob acceleration. Tested in the emulator only.")
_add("LCD_BAUD", "LCD_BAUD", "screen SPI clock", U, 79, default=1,
     choices=((1, "30 MHz"), (4, "12 MHz (as before X0X)"), (0, "60 MHz")),
     desc="60 MHz / (n + 1) from the clock the SPL leaves; the ST7789V takes ~62 MHz", provenance=x0x("d179e03"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): the faster LCD clock ran on X0X's FM-1; untested on "
            "hardware here.")
_add("LCD_DIRTY", "FELUCCA_LCD_DIRTY", "screen: send only the changed rectangle", U, 68,
     desc="graph strips go out as the changed rectangle only (with the 30 MHz SPI clock: less tearing)",
     provenance=x0x("201b95c"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): dirty-rectangle screen updates. Not yet tried on "
            "hardware here.")

# ---- system
Y = "System"
_add("OTA", "FELUCCA_OTA", "updates from the web editor (M-UPGRADE)", Y, 54,
     off_warning="without it, updates need the UBOOT rescue path")
_add("IDLE", "FELUCCA_IDLE", "idle between UI frames (power)", Y, 55)
_add("BACKUP", "FELUCCA_BACKUP", "backup / restore from the web editor", Y, 84,
     desc="everything in flash to one .optimist-backup file and back (editor cmds 43..48); a restore onto another "
          "build reports what it skips and what plays a stand-in", off_warning="no backup: export projects and kits "
          "one by one before an update")
_add("SIZE", "", "main-loop code built for size", Y, 70, default=1, choices=((1, "minsize (UI, stores, editor)"),
     (0, "-Os everywhere")), desc="the audio path is never size-optimised (tools/size_fns.py guards it)",
     provenance=Provenance("Felucca 1.0.1 (hugelton/Felucca)", "Leo Kuroshita (Hügelton)", "GPL-3.0-only",
                           "20c275e", FELUCCA_URL))
_add("CPU_GUARD", "FELUCCA_CPU_GUARD", "predictive CPU guard (ease back before shedding)", Y, 61, default=0,
     provenance=FLOWSTATE_GUARD,
     desc="under overload (the measured load or the one predicted from the voices sounding, over 85 % for 8 halves, "
          "or a late half): first ACID without oversampling and ANALOG 2's swarm at 2 copies, then UNISON at 2 "
          "voices, then voices shed, never the bass or the lead (MONO / LEGATO / UNISON parts) nor the drums; back "
          "after 2 s under 80 % counting what each step saved. The CPU meter shows %G1..%G3. Weights: "
          "tools/builder/cpu_costs.py from tests/cpu_baseline.txt (docs/CPU-GUARD.md)")
_add("ASM", "FELUCCA_ASM", "asm kernels (FM6, ANALOG 2: faster)", Y, 56, target_only=True)
_add("SIMD", "FELUCCA_SIMD", "SIMD packed sine (EXPERIMENTAL)", Y, 57, default=0, parent="ASM", experimental=True,
     target_only=True)

# ---- experimental
X = "Experimental"
_add("DUAL", "FELUCCA_DUAL", "dual core: CPU1 renders parts 2-3 (EXPERIMENTAL)", X, 58, default=0, experimental=True,
     choices=((0, "off"), (2, "on")), target_only=True)

ITEMS = {it.key: it for it in _ITEMS}
for _it in _ITEMS:
    if _it.parent:
        assert _it.parent in ITEMS and not ITEMS[_it.parent].parent, f"{_it.key}: one level only"
        ITEMS[_it.parent].children.append(_it.key)
assert len({it.bit for it in _ITEMS}) == len(_ITEMS), "registry: duplicate bit"
assert all(k not in ITEMS for k in FORBIDDEN), "registry: a forbidden item is registered"
for _it in _ITEMS:
    if _it.provenance and "fm1-x0x" in _it.provenance.project + _it.provenance.url:
        assert _it.notice, f"{_it.key}: an X0X item needs its notice"


def top_level(group=None):
    return [it for it in _ITEMS if not it.parent and (group is None or it.group == group)]


def add_item(item):
    """register an item at import time (backported features add themselves here: tools/builder/backports.py)"""
    assert item.key not in ITEMS and item.key not in FORBIDDEN
    assert item.bit not in {it.bit for it in _ITEMS}, f"{item.key}: bit {item.bit} taken"
    if item.provenance and "fm1-x0x" in item.provenance.project + item.provenance.url:
        assert item.notice, f"{item.key}: an X0X item needs its notice"
    _ITEMS.append(item)
    ITEMS[item.key] = item
    if item.parent:
        assert item.parent in ITEMS and not ITEMS[item.parent].parent
        ITEMS[item.parent].children.append(item.key)


def to_json():
    out = []
    for it in _ITEMS:
        d = {k: getattr(it, k) for k in ("key", "flag", "label", "group", "bit", "default", "parent", "desc",
                                          "experimental", "notice", "off_warning", "env", "target_only", "symbols")}
        d["choices"] = [list(c) for c in it.choices]
        d["provenance"] = it.provenance.__dict__ if it.provenance else None
        out.append(d)
    return {"groups": GROUPS, "items": out, "forbidden": FORBIDDEN}


# the symbol check (tools/builder/verify.py): what an item left out must not leave behind
_SYMS = {
    "ENG_ANALOG": ("ENG_ANALOG", "analog_render"), "ENG_DIGITAL": ("ENG_DIGITAL", "digital_render"),
    "ENG_PHASE": ("ENG_PHASE", "pd_wave"), "ENG_LOFI": ("ENG_LOFI", "lofi_render", "WRAM"),
    "ENG_SAMPLE": ("ENG_SAMPLE",), "ENG_FORMANT": ("ENG_FORMANT", "formant_render"),
    "ENG_TRIO": ("ENG_TRIO", "trio_render"), "ENG_DRAWBAR": ("ENG_DRAWBAR", "drawbar_render"),
    "ENG_GRAIN": ("ENG_GRAIN", "gr_p"), "ENG_FM6": ("ENG_FM6", "fm6_v", "FM6_ROM", "fm6k_knob"),
    "FM6_MARK1": ("FM6_MKI_LOG", "FM6_MKI_EXP", "FM6_MKI_LOGQ", "FM6_MKI_EXPF"),
    "FM6_OPL": ("FM6_OPL_LOG", "FM6_OPL_EXP"),
    "FM6_KEYS": ("fm6k_knob",), "FM6_VOICES": ("FM6_ROM",), "ENG_SLICE": ("ENG_SLICE",),
    "DRUM_SYNTH": ("DS_KITS",), "DRUM_EDIT": ("de_synth",), "FX_CHORUS": ("cho_buf",),
    "FX_DELAY": ("dly_buf",), "FX_REVERB": ("rev_line", "rev_ap"), "FX_SLICER": ("sl_buf",),
    "FX_PUNCH": ("punch_ring",), "ICONS": ("ICON_DATA",), "OTA": ("ota_session",),
    "SPLASH": ("lg_rect", "lg_span"), "DRUM_X0X909": ("drum909_trigger", "x0x_smp_hh_m"),
    "X909_CYM": ("x0x_smp_ride_m", "x0x_smp_crash_m"), "DRUM_X0X808": ("drum808_trigger",),
    "CPU_GUARD": ("cg", "cg_post", "CG_VCOST"),
}
for _k, _v in _SYMS.items():
    ITEMS[_k].symbols = _v
