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
     desc="the DX7's log-sine resolution (6.1 KB RAM tables)")
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
_add("UNDO_HISTORY", "FELUCCA_UNDO_HISTORY", "undo / redo history (many levels)", Q, 69,
     desc="EDIT + OCT- / OCT+: the history lives in the pool and RAM this build leaves free (at least 1 KiB); "
          "off: one level")

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
_add("SPLASH", "FELUCCA_SPLASH", "boot logo", U, 51, default=0)
_add("ICONS", "FELUCCA_ICONS", "parameter icons", U, 52)
_add("OVERVIEW", "FELUCCA_OVERVIEW", "VIEW ALL overview (4 x 4 PAGEs)", U, 53,
     desc="GLO > SYSTEM VIEW ALL: a page family at once, 4 rows x 4 knobs a PAGE, PAGE n/m")
_add("OV_ARP", "FELUCCA_OV_ARP", "ARP graph and ARP in VIEW ALL", U, 82, parent="OVERVIEW")
_add("MISSING_WARN", "FELUCCA_MISSING_WARN", "say what a project uses and this build lacks", U, 90,
     desc="'MISSING: PHYS T2, KIT 909' in the top bar when a project, song section, user preset or kit uses an "
          "engine, kit, sample set or FX this build leaves out (once per item until power-off; never stalls the "
          "audio); SAVE > TOOLS > MISS lists them again. Off: they play their stand-ins silently")
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
    "FM6_MARK1": ("FM6_MKI_LOG", "FM6_MKI_EXP"), "FM6_OPL": ("FM6_OPL_LOG", "FM6_OPL_EXP"),
    "FM6_KEYS": ("fm6k_knob",), "FM6_VOICES": ("FM6_ROM",), "ENG_SLICE": ("ENG_SLICE",),
    "DRUM_SYNTH": ("DS_KITS",), "DRUM_EDIT": ("de_synth",), "FX_CHORUS": ("cho_buf",),
    "FX_DELAY": ("dly_buf",), "FX_REVERB": ("rev_line", "rev_ap"), "FX_SLICER": ("sl_buf",),
    "FX_PUNCH": ("punch_ring",), "ICONS": ("ICON_DATA",), "OTA": ("ota_session",),
    "SPLASH": ("SLOOP_SPLASH_RLE",),
}
for _k, _v in _SYMS.items():
    ITEMS[_k].symbols = _v
