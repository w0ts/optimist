# SPDX-License-Identifier: GPL-3.0-only
"""The firmware builder's registry: every item a build may leave out, its switch, its options, where it came from.

One source of truth for tools/builder/configure.py (the .config -> build/gen/felucca_config.h), the budget
(costs.json, measured by tools/builder/measure_costs.py) and the menu (tools/menuconfig). The firmware side of
each switch is in firmware/src/core/registry.h (engines, kits, FX, FM6 options, features) or in the existing flags.

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

# Items that became others: a .config naming one is read as these (configure.py parse)
MIGRATE = {
    "OTA": lambda v: {"EDITOR": int(v != 0)},   # (2026-10-17: the editor alone; updates and the USB rescue always built)
    "REVERB": lambda v: {"REV_ROOM": int(v == 0), "REV_PLATE": int(v == 1), "REV_FDN8": int(v == 2)},   # the one-tank choice
}

# Items merged into another: a .config (or --set) naming one is read as the survivor, turned ON if either was on
# (configure.py parse); the old key is warned about once
MERGED = {
    "MIDI_CLKOUT": ("MIDI_CLOCK", "MIDI clock in and out are one item, MIDI_CLOCK (merged 2026-10-15; on if either was on)"),
}

# Dropped items: a .config naming one still loads; the setting is ignored with a warning (configure.py parse)
RETIRED = {
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
    "KNOB_ONEREST": "the knobs are always read with Felucca 1.6's decoder (one rest state a detent, fast turns counted; "
                    "retired 2026-10-14, released in Felucca 1.1.5 #126 and unchanged since)",
}

# Retired items every image is still built as (their firmware default): the configuration hash counts them at that
# value (configure.py cfg_hash), so retiring one changes no image's hash. SIMD was default 0 in every profile; LFO2 is
# counted ON (the image every build is now; a build that had it off is a different image).
HASH_AS = {"SIMD": 0, "KNOB_ONEREST": 1, "MONO_RELEASE": 1, "ST_STRICT": 1, "USB_FLOW": 1, "SHED_FADE": 1, "KEYS_FAST": 1, "TRS_NOISE": 1, "BK_CHECK": 1, "LAYER_QUIET": 1, "DLY_HALVE": 1, "LFO2": 1}   # (each was its default in every profile)

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


MELODEE_FM6 = Provenance("Melodee (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "e459da5", MELODEE_URL)   # (the port, 958569b7; ebfe60e is FM6's first commit)
MELODEE_MIDI = Provenance("Melodee (keremimo/melodee)", "ChanceTheMaker, Kerem Kilic", "GPL-3.0-only", "670193c",
                          MELODEE_URL)
MELODEE_USB = Provenance("Melodee (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "", MELODEE_URL)
MELODEE_SUPPLY = Provenance("Melodee (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "7a51700",
                            MELODEE_URL)   # (its port of the stock V15 supply start-up, hal/fm1_power.h, 1.3.6)
FELUCCA = Provenance("Felucca (hugelton/Felucca)", "Leo Kuroshita (Hügelton)", "GPL-3.0-only", "", FELUCCA_URL)
PR45 = Provenance("SLOOP 8-track PR (isod89/sloop-fm1 #45)", "Erick Buendia Barrientos (Erbubar23)", "GPL-3.0-only",
                  "8d9623f", "https://github.com/isod89/sloop-fm1/pull/45")
SLOOP_24 = Provenance("SLOOP 2.4 (isod89/sloop-fm1)", "isod89", "GPL-3.0-only", "v2.4 8d3823f", "https://github.com/isod89/sloop-fm1")
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
    tested: str = "emulator-tested only"   # what an EXPERIMENTAL item has been tested on (the warning says it)
    no_image: bool = False           # a build setting, not firmware: nothing in the image (not in the hash or the BUILD
                                     # bits), its costs.json deltas are 0 (written, never built)
    children: list = field(default_factory=list)

    @property
    def is_choice(self):
        return bool(self.choices)


# The FX in two groups (2026-10-13): those every track (and slot) can run at once, their state small and sized for every
# track (CPU the only limit), and those that run once (the master, the chorus, delay and reverb) or as many times as the build gives
# them instances (the insert copies of the chorus and a short delay: a pool line each; fx/insfx/fxi.c hands them out).
# The items are declared with group "FX" and sorted into the two by key (fx_group), the backports' too.
FX_ANY = "FX: any number (each track its own)"
FX_ONCE = "FX: limited instances (once, or a set count)"
FX_ONCE_KEYS = {"FX_CHORUS", "FX_DELAY", "FX_REVERB", "FX_PUNCH", "FX_DUCK", "BASSPLUS",
                "INS_CHORUS", "INS_DELAY", "INS_JCHORUS", "INS_FLANGER"}


def fx_group(key, parent=None):
    """the group of an FX item ("FX" when declared): by its own key, a child by its parent's"""
    return FX_ONCE if (parent or key) in FX_ONCE_KEYS else FX_ANY


GROUPS = ["Reserve", "Synth engines", "Drums", "Sample sets", FX_ANY, FX_ONCE, "Sequencer", "MIDI & USB", "UI", "System"]

_ITEMS = []


def _add(*a, **k):
    it = Item(*a, **k)
    _ITEMS.append(it)
    return it


# ---- reserve: headroom the user keeps instead of filling the device to the last byte. Settings of the build, not
# firmware: no flag, nothing in the image, no cost (costs.json: 0); tools/builder/configure.py reserve_* applies them
# to the budget and tools/build.py refuses a build that leaves less (docs/BUILDER.md, docs/MEMORY-MAP.md section 2.1)
RES = "Reserve"
_add("RESERVE_UNDO_KB", "", "keep at least this much undo history", RES, 59, default=0, no_image=True,
     choices=((0, "no minimum"), (4, "4 KB"), (8, "8 KB"), (16, "16 KB"), (32, "32 KB")),
     desc="Keeps headroom for the undo history instead of filling RAM and pool to the last byte: the history's ring "
          "is what they leave free ((98,304 - RAM) + (344,064 - 8,192 - pool), docs/MEMORY-MAP.md). The estimate "
          "shows the ring and warns below this figure; a build whose real ring is smaller is refused. Nothing in "
          "the firmware changes (it needs the undo / redo history item).")
_add("RESERVE_FLASH_KB", "", "keep at least this much app flash free", RES, 60, default=0, no_image=True,
     choices=((0, "no minimum"), (8, "8 KB"), (16, "16 KB"), (32, "32 KB"), (64, "64 KB")),
     desc="Keeps room in the app slot for later (a feature, an update) instead of filling it to the last byte: the "
          "budget subtracts it from the slot and a build that leaves less free is refused. Nothing in the firmware "
          "changes.")

# ---- engines (UID = FUN7 number, firmware/src/core/registry.h ENGINE_LIST)
# A tracks whose engine a build leaves out plays a stand-in engine (ANALOG 2 for most) and the MISSING warning
# names it; the project keeps the original engine and its values (docs/BUILDER.md).
E = "Synth engines"
_add("ENG_ANALOG", "FELUCCA_ENG_ANALOG", "ANALOG 2 (analog-style synth, swarm, 2 filters)", E, 0,
     desc="The main subtractive synth: two oscillators with hard sync, a swarm of up to 6 detuned copies (supersaw), "
          "two filters. About 6 KB of flash and 4.7 KB of fast RAM code; it is also the stand-in that most other "
          "engines fall back to, so keep it unless you need the space.")
_add("ENG_DIGITAL", "FELUCCA_ENG_DIGITAL", "DIGITAL (4-operator FM)", E, 1,
     desc="Four-operator FM synth: 8 classic algorithms, feedback on operator 4 and one modulation INDEX with its "
          "own envelope. About 2 KB of flash; the stand-in for FM6 when FM6 is left out.")
_add("ENG_PHASE", "FELUCCA_ENG_PHASE", "PHASE (phase distortion)", E, 2,
     desc="Phase-distortion synth: bends the phase of a cosine wave to get saw, square, pulse and resonant shapes "
          "with a second line to mix or ring-modulate. About 1.4 KB of flash; the stand-in for CZ.")
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
# (bit 91, LFO2, retired 2026-10-16: LFO 2, a second LFO on each synth track, is always built; never reuse the bit)

# ---- drums
D = "Drums"
_add("DRUM_SYNTH", "FELUCCA_DRUM_SYNTH", "drum synth (all 32 synthesised kits)", D, 18,
     desc="The 32 synthesised drum kits (808, 909 and others), generated by the firmware, so they take no "
          "sample memory. About 13.7 KB of flash; the drum track needs this or at least one sampled kit.")
_add("DRUM_SAMPLED", "FELUCCA_DRUM_SAMPLED", "sampled drums (the PERC set, 99 KB)", D, 19, env="PERC",
     desc="The recorded drum samples (the PERC set, about 99 KB of flash) behind the five sampled kits below. "
          "Switching this off drops all of them; the drum track then needs the drum synth.")
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
_add("DRUM_EDIT", "FELUCCA_DRUM_EDIT", "drum sound editor (EDIT on the drum track)", D, 25,
     desc="Per-lane sound tweaks over the kit (TUNE, DECAY, SNAP, CLICK, BEND, CUT, DRIVE, LEVEL on synthesised "
          "kits; TUNE, DECAY, CUT, LEVEL on sampled ones). About 1.2 KB of flash; off: kits play as they are and "
          "saved tweaks are kept but not applied.")
_add("DRUM_USR", "FELUCCA_DRUM_USR", "user samples on drum lanes", D, 26,
     desc="A drum lane can play one of your own samples (a hit of a USR slot, with start and length) instead of the "
          "kit's sound. About 1.2 KB of flash; off: such lanes play the kit sound.")
_add("DRUM_KITS", "FELUCCA_DRUM_KITS", "user drum kits (bank of 16 in data flash)", D, 27,
     desc="Lane sources from other kits and the X0X machines, and a bank of 16 user kits (each lane's source and "
          "sound tweaks) saved in flash. About 2.8 KB of flash; off: lanes play the kit as it is.")
# (bit 83, DRUM_SENDS, retired 2026-10: each drum lane's REV / DLY / CHO are the drums' only sends, in every build;
#  never reuse the bit)
X0X_DRUMS_NOTICE = ("Ported from X0X by Charles Vestal (GPL-3.0), itself from 9W9 / 8W8 by athousanddetails and ER-99 by "
                    "Matthew Cieplak (GPL-3.0); the 808's rim shot after sc808 (Yoshinosuke Horiuchi / Sam Aaron, MIT). "
                    "Float DSP.")
_add("DRUM_X0X909", "FELUCCA_DRUM_X909", "X0X 909 kit (circuit-modelled TR-909)", D, 86, default=0,
     provenance=x0x("80b7d40"), notice=X0X_DRUMS_NOTICE,
     desc="X0X's TR-909 drum kit (kit UID 37; the drum models are circuit-modelled, hi-hats, ride and crash are "
          "samples), on the 16 lanes; SHAKER, CONGA, COWBELL play the synthesised 909's. Big: about 151 KB of flash "
          "(see its cymbal option) and uses floating point. A build without it plays the "
          "synthesised 909 for it and keeps the kit")
_add("X909_CYM", "FELUCCA_X909_CYM", "X0X 909: ride and crash sample quality", D, 87, parent="DRUM_X0X909",
     choices=((1, "8-bit (93 KB, 42 dB)"), (2, "6-bit (70 KB, 30 dB)"), (0, "off")),
     desc="8-bit block floating point as before; 6-bit: 22 KB less, 30 dB against the 16-bit source instead of 42 "
          "(screens x0xdrums-perf-2026-10-06); off: RIDE and CRASH play the synthesised 909's (the hi-hat samples stay)")
_add("DRUM_X0X808", "FELUCCA_DRUM_X808", "X0X 808 kit (circuit-modelled TR-808)", D, 88, default=0,
     provenance=x0x("80b7d40"), notice=X0X_DRUMS_NOTICE,
     desc="X0X's TR-808 drum kit (kit UID 38; 16 circuit-modelled sounds) on the 16 lanes; MIDI also plays MT, LC, "
          "HC and the claves (note 75). About 30 KB of flash and uses floating point. A build "
          "without it plays the synthesised 808 for it and keeps the kit")

_add("DRUM_ACCENT", "FELUCCA_DRUM_ACCENT", "drum ACCENT lane (909 style)", D, 340,
     desc="A 17th step lane on the drum track, the ACCENT lane: no sound of its own, its steps raise every hit of the "
          "step by the drum track's ACCENT amount (0 to 127 velocity, default 27: a normal hit plays at full velocity), "
          "into each kit's own velocity response (the X0X 909 / 808: their accent input); MIDI out sends the raised "
          "velocity. It works as a lane: steps, levels, ratchets, chance and conditions, copy, erase, the STEP tools, "
          "patterns, sections, snapshots. Off: the lane is kept and edited but not heard. Needs the automation store "
          "(motion, locks, chance or conditions).")

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
              "flash, and it can still be uploaded to a USR slot.")

# ---- FX
F = "FX"
_add("FX_DIST", "FELUCCA_FX_DIST", "DIST (per-track drive)", F, 35,
     desc="A distortion on each synth track: low cut, drive from 1x to 8x into an asymmetric soft clip, and a tone "
          "filter that closes as the drive rises. About 0.4 KB of flash; off: the DIST controls disappear.")
_add("FX_CHORUS", "FELUCCA_FX_CHORUS", "chorus", F, 36,
     desc="The chorus effect, fed by each track's CHO send. About 0.4 KB of flash and 4 KB of pool; off: the CHO "
          "sends and the chorus settings disappear.")
_add("FX_DELAY", "FELUCCA_FX_DELAY", "delay", F, 37,
     desc="The tempo-synced delay (echo), fed by each track's DLY send. Costs 128 KB of the pool at the full "
          "length: shorten it with the length option, or leave it out.")
_add("DLY_LEN", "FELUCCA_DLY_LEN", "delay length (how long an echo can be)", F, 38, default=65536, parent="FX_DELAY",
     choices=((65536, "1.49 s (128 KB pool)"), (32768, "0.74 s (64 KB)"), (16384, "0.37 s (32 KB)")),
     desc="The longest delay time, which sets the pool memory the delay takes. A time longer than the line is "
          "halved until it fits, so it stays on the beat; shorter lines suit fast tempos only.")
_add("FX_REVERB", "FELUCCA_FX_REVERB", "reverb", F, 39,
     desc="The reverb, fed by each track's REV send and the drums' reverb, with its algorithms below (tick at least "
          "one). Each one ticked is on the device's FX > REVERB > TYPE and in the web editor's Reverb popup, switched "
          "while it plays; with one ticked there is no TYPE. They share one line buffer, sized to the largest. The "
          "biggest user of RAM among the FX (about 17 KB with ROOM); off: the REV sends and reverb settings disappear.")
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
_add("REV_POOL", "FELUCCA_REV_POOL", "reverb buffers in the pool (saves ~17 KB RAM)", F, 122, default=0,
     parent="FX_REVERB",
     desc="Keeps the reverb's shared line buffer (17 KB with ROOM, 8.7 KB at half rate; PLATE and FDN8 without ROOM "
          "16 KB, 8 KB at half rate; SPRING alone 8 KB) in the pool instead of main RAM: main RAM is the scarcer, and "
          "the sound and the code stay the same. Needs that much pool free (the undo history shrinks by it in the "
          "pool and grows by it in RAM).")
_add("REV_HALF", "FELUCCA_REV_HALF", "half-rate reverb (half the RAM, no top octave)", F, 123, default=0,
     parent="FX_REVERB",
     desc="Runs the ROOM reverb at half the sample rate (22.05 kHz) behind a half-band filter: its memory takes half "
          "the RAM (-8.7 KB) and it costs less CPU, with the same decay and room size. PLATE and FDN8 already run at "
          "22.05 kHz; here they get half the ring (8 KB), a smaller tank with more audible modes. The reverb loses "
          "its top octave (above ~11 kHz); the dry sound and the other buses are untouched.", symbols=("rev_half",))
REV_ALGOS = ("REV_ROOM", "REV_PLATE", "REV_FDN8", "SPRING", "REV_AIRWIN")   # at least one with FX_REVERB (configure.py validate)
_add("FX_SLICER", "FELUCCA_FX_SLICER", "SLICER (stutter / gate insert)", F, 40,
     desc="A tempo-synced 16-step gate or stutter on each track (not the sample slicer engine): chops the sound "
          "to a pattern. About 1.2 KB of flash and 32 KB of pool at the full capture length.")
_add("SL_LEN", "FELUCCA_SL_LEN", "SLICER capture length (stutter memory)", F, 41, default=4096, parent="FX_SLICER",
     choices=((4096, "186 ms (32 KB pool)"), (2048, "93 ms (16 KB)")),
     desc="How much sound each track's stutter records to repeat: the shorter capture saves 16 KB of pool and "
          "limits how long a repeated chunk can be (the gate mode is unaffected).")
_add("FX_PUNCH", "FELUCCA_FX_PUNCH", "PUNCH (16 punch-in FX)", F, 42,
     desc="Hold FX and press a white key to put the whole mix through one of 16 effects while it is held (loops, "
          "reverse, tape stop, half speed, wobble, echo, filters, crush, gate), beat-synced. About 2.3 KB of "
          "flash and 64 KB of pool at the full ring.")
_add("PUNCH_N", "FELUCCA_PUNCH_N", "PUNCH memory length (how much mix it can loop)", F, 43, default=32768, parent="FX_PUNCH",
     choices=((32768, "0.74 s (64 KB pool)"), (16384, "0.37 s (32 KB)")),
     desc="How much of the mix the loop, reverse, tape-stop and echo effects can capture; the shorter ring "
          "saves 32 KB of pool and cuts the longest loop in half.")
_add("FX_DJF", "FELUCCA_FX_DJF", "DJ filter (MASTER FILT)", F, 44,
     desc="One knob on the master (MASTER > FILT): turn left for a low-pass closing, right for a high-pass opening, "
          "centre is off. About 0.7 KB of flash.")
_add("FX_DUST", "FELUCCA_FX_DUST", "DUST (vinyl / lo-fi master)", F, 45,
     desc="One knob on the master (MASTER > DUST) that runs the mix through an old sampler and a record: drive, "
          "lower sample rate, fewer bits, a darker tone, hiss and crackle while playing. About 0.7 KB of flash.")
_add("FX_DUCK", "FELUCCA_FX_DUCK", "DUCK (kick ducks the parts)", F, 46,
     desc="Pumping sidechain effect (MASTER > DUCK): every kick from the drum track dips the synth parts, which "
          "swell back over an eighth note. About 0.1 KB of flash; has no effect without a drum kick.")
_add("MASTER_COMP", "FELUCCA_MASTER_COMP", "COMP + LIMIT (master compressor, brickwall limiter)", F, 155,
     desc="A compressor and a brickwall limiter on the master (GLO > COMP: THRS, RATIO, ATK, REL; GLO > LIMIT: "
          "GAIN, CEIL and a GR readout) to glue the mix and keep its peaks under a ceiling; a Comp button and a GR "
          "meter in the web mixer. A new project limits at CEIL -3.0 dB (a look-ahead limiter in place of the old "
          "one); saved projects keep their own CEIL (OFF: as before). About 2 KB of flash and 2.5 KB of RAM.",
     symbols=("mc", "mlim"))
_add("TRK_FILT", "FELUCCA_TRK_FILT", "track FILTER (LP <- off -> HP on each track)", F, 205, default=0, provenance=SLOOP_24,
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
_add("INS_FOLD", "FELUCCA_INS_FOLD", "FOLD insert (wavefolder)", F, 256, default=0, provenance=FELUCCA_156,
     desc="A wavefolder type for the tracks' insert slots (INSERTS > S1 / S2: FOLD): the sound driven past full scale "
          "folds back, more folds and harmonics as FOLD rises; the drive follows the track's level as DIST's does, the "
          "bass stays dry. 0 is off.", symbols=("ifx_fold",),
     notice="From Felucca 1.5.6's track INSERT FOLD by Leo Kuroshita (GPL-3.0)." + _INS_NOTE)
_add("INS_FUZZ", "FELUCCA_INS_FUZZ", "FUZZ insert", F, 257, default=0, provenance=FELUCCA_156,
     desc="A fuzz type for the tracks' insert slots (INSERTS > S1 / S2: FUZZ): a hard-clipped top and a soft, halved "
          "bottom (lopsided, buzzy), the drive with FUZZ. 0 is off.", symbols=("ifx_fuzz",),
     notice="From Felucca 1.5.6's track INSERT FUZZ by Leo Kuroshita (GPL-3.0)." + _INS_NOTE)
_add("INS_PHASER", "FELUCCA_INS_PHASER", "PHSR insert (phaser)", F, 258, default=0, provenance=FELUCCA_156,
     desc="A phaser type for the tracks' insert slots (INSERTS > S1 / S2: PHSR): four all-pass stages swept slowly "
          "(~0.5 Hz); PHSR up to the middle mixes it in, past the middle adds feedback. 0 is off.",
     symbols=("ifx_phsr",), notice="From Felucca 1.5.6's track INSERT PHASR by Leo Kuroshita (GPL-3.0)." + _INS_NOTE)

_add("INS_FORMANT", "FELUCCA_INS_FORMANT", "FRMT insert (vowel filter)", F, 260, default=0, provenance=MELODEE_131,
     desc="A vowel filter type for the tracks' insert slots (INSERTS > S1 / S2: FRMT): three formant peaks over the "
          "sound; FRMT moves the vowel from A (low) through E, I, O to U (high). 0 is off.", symbols=("ifx_frmt",),
     notice="From Melodee 1.3.1's FORMANT insert by Kerem Kilic (GPL-3.0); its vowel table, fixed point here."
            + _INS_NOTE)
_add("INS_WIDTH", "FELUCCA_INS_WIDTH", "WIDE insert (stereo width)", F, 261, default=0, provenance=MELODEE_131,
     desc="A stereo width type for the tracks' insert slots (INSERTS > S1 / S2: WIDE): one side through slowly swept "
          "all-pass filters, the other dry, so a centred mono track spreads out; WIDE sets how much. 0 is off.",
     symbols=("ifx_wide",), notice="From Melodee 1.3.1's WIDTH insert by Kerem Kilic (GPL-3.0)." + _INS_NOTE)
# the shapers (2026-10-18, fx/insfx/insfx.c ifx_drive ifx_mod): FOLD's state (24 B an instance, 15 instances), any slot
_SH_NOTE = " On any insert slot (tracks, drum sounds but SUB, drum bus, master, returns). Tested in the host tests only, not on a device."
_add("INS_SFOLD", "FELUCCA_INS_SFOLD", "SFLD insert (sine wavefolder)", F, 343, default=0,
     desc="A sine wavefolder type (SFLD): like FOLD but the folds are round (a sine of the drive), so it sings and "
          "aliases less; soft saturation at first, then more and more folds as SFLD rises. 0 is off.",
     symbols=("ifx_sfld",), notice="The shape of Felucca 1.5.5's PHASE FOLD by Leo Kuroshita (GPL-3.0)." + _SH_NOTE)
_add("INS_WRAP", "FELUCCA_INS_WRAP", "WRAP insert (wavewrapper)", F, 344, default=0,
     desc="A wavewrapper type (WRAP): past full scale the wave jumps to the other side instead of folding back, a "
          "harsh digital buzz; clean at low settings, more jumps as WRAP rises. 0 is off.",
     symbols=("ifx_wrap",), notice=_SH_NOTE.strip())
_add("INS_RECT", "FELUCCA_INS_RECT", "RECT insert (rectifier, octave up)", F, 345, default=0,
     desc="A rectifier type (RECT): up to the middle the wave's lower half is folded up (half-wave, then full-wave: the "
          "octave up of an Octavia), past the middle it is driven into a soft clip. 0 is off.",
     symbols=("ifx_rect",), notice=_SH_NOTE.strip())
_add("INS_HARM", "FELUCCA_INS_HARM", "HARM insert (harmonic exciter)", F, 346, default=0,
     desc="A harmonic exciter type (HARM): adds the octave and the twelfth over each note, warm rather than buzzy, "
          "more as HARM rises. 0 is off.", symbols=("ifx_harm",), notice=_SH_NOTE.strip())
_add("INS_RING", "FELUCCA_INS_RING", "RING insert (ring modulator)", F, 347, default=0,
     desc="A ring modulator type (RING): the sound times a sine from 26 Hz (a rough tremolo) to 2.6 kHz (bells, "
          "metal), RING picks its pitch. 0 is off.", symbols=("ifx_ring",), notice=_SH_NOTE.strip())
_add("INS_SUBOCT", "FELUCCA_INS_SUBOCT", "SUB insert (octave down)", F, 348, default=0,
     desc="An octave-down type (SUB): a square an octave under the note, following its level, added as SUB rises. "
          "For single-note lines (bass, leads): chords give a rough sub. Not on drum sounds. 0 is off.",
     symbols=("ifx_subo",), notice=_SH_NOTE.replace("drum sounds but SUB, ", "").strip())
_add("INS_SLEW", "FELUCCA_INS_SLEW", "SLEW insert (slew-rate crunch)", F, 349, default=0,
     desc="A slew limiter type (SLEW): the wave can only move so fast, so edges round into ramps, a slow op-amp's "
          "dull crunch; light at 1, heavy (muffled, gritty) at 127. 0 is off.", symbols=("ifx_slew",),
     notice=_SH_NOTE.strip())
# the insert copies of the "once" effects (fx/insfx/fxi_kern.c, fxi.c, 2026-10-13): a count of instances each, how many
# tracks may hold the type at once; a track picking it with none left is bypassed (the INSERTS list says FULL)
_INS_LIM = (" Each instance is a line in the pool; a track that picks it when every instance is in use stays dry "
            "until one is free (the INSERTS list shows how many are left). A saved project keeps the type, not the "
            "instance: on load the tracks get them in order T1 T2 T3 DR.")
_add("INS_CHORUS", "FELUCCA_INS_CHORUS", "CHRS insert (chorus copies for the tracks)", F, 270, default=0,
     choices=((0, "off"), (1, "1 track (2 KB pool)"), (2, "2 tracks (4 KB)"), (3, "3 tracks (6 KB)"),
              (4, "every track (8 KB)")),
     desc="A chorus type for the tracks' insert slots (INSERTS > S1 / S2: CHRS), beside the chorus send: two taps "
          "around 7 ms swept apart at ~0.8 Hz; CHRS up to the middle mixes it in, past the middle widens the sweep. "
          "On the drum track it turns the drums' mid into a stereo chorus. 0 is off." + _INS_LIM,
     symbols=("ifx_chrs",), notice="Tested in the host tests only, not on a device.")
_add("INS_DELAY", "FELUCCA_INS_DELAY", "ECHO insert (short delay copies for the tracks)", F, 271, default=0,
     choices=((0, "off"), (1, "1 track (8 KB pool)"), (2, "2 tracks (16 KB)"), (3, "3 tracks (24 KB)"),
              (4, "every track (32 KB)")),
     desc="A slapback type for the tracks' insert slots (INSERTS > S1 / S2: ECHO), beside the delay send: one repeat "
          "75 ms after the sound; ECHO up to the middle sets its level, past the middle adds repeats (feedback up to "
          "60 %). Up to 93 ms of line an instance (the long, tempo-synced delay stays the send's). 0 is off."
          + _INS_LIM, symbols=("ifx_echo",), notice="Tested in the host tests only, not on a device.")
FM106 = Provenance("fm-106 (PatttF/fm-106)", "PatttF", "GPL-3.0-only", "be74757", "https://github.com/PatttF/fm-106")
_add("INS_JCHORUS", "FELUCCA_INS_JCHORUS", "JUNO CHORUS insert (the Juno-106 chorus)", F, 142, default=0, provenance=FM106,
     choices=((0, "off"), (1, "1 slot (2 KB pool)"), (2, "2 slots (4 KB)"), (3, "3 slots (6 KB)"),
              (4, "4 slots (8 KB)")),
     desc="The Juno-106's chorus as an insert type (INSERTS > S1 / S2, and the master's insert slots: JUNO): the "
          "MN3009 BBD model of fm-106, MODE I, II or I+II, JUNO its mix (127: the Juno's chorus on). A mono track "
          "comes out stereo. About 130 host instructions a sample an instance. 0 is off." + _INS_LIM,
     symbols=("ifx_jcho",), notice="From fm-106 by PatttF (GPL-3.0), after Ultramaster KR-106 (GPL-3.0). Tested in the "
                                   "host tests only, not on a device.")
# EQ3 (2026-10-15, fx-pool: fx/insfx/eq3.c): a further insert type, any number (its state a few words an owner)
_add("INS_EQ3", "FELUCCA_INS_EQ3", "EQ3 insert (three-band DJ EQ)", F, 143, default=0,
     desc="A three-band equaliser type, like Ableton's EQ Three, for any insert slot (a track's, a drum sound's, the "
          "master's) and the returns (EQ3): LO MID HI from KILL (the left end) to +6 dB, the crossovers FREQ LO (250 Hz) "
          "and FREQ HI (2.5 kHz) at 12 dB/oct (Linkwitz-Riley: the bands sum flat). All three at 0 dB is a bypass that "
          "costs nothing; moving in and out of it crossfades in 3 ms. About 75 host instructions a sample on a track.",
     symbols=("eq3_run",), notice="Tested in the host tests only, not on a device.")
# the returns' feedback sends (2026-10-16): B -> A, C -> A, C -> B, a block late (fx.c rs_fbv). Bit 167 (reserved 2026-10-16: the next free one after the web editor v14's 158, 159, 165, 166)
_add("RET_FB", "FELUCCA_RET_FB", "return feedback sends (B to A, C to A, C to B)", F, 167, default=1,
     desc="The master's returns send to the returns on their left too (MASTER > A SENDS, B SENDS, C SENDS: each "
          "return to the other two), for feedback between the effects: a delay into a reverb and back. Each such "
          "send takes its return's output of the block before (a loop of 32 samples, 0.73 ms), DC-blocked, limited "
          "to -12 dBFS and soft-clipped, at a quarter of the gain of a send to the right, so no loop runs away. "
          "Without it a return sends only to its right (A to B and C, B to C). About 1.2 KB of RAM; nothing runs "
          "while every such send is 0.",
     symbols=("rs_fbv",), notice="Tested in the host tests only, not on a device.")
# FLNG (2026-10-15, fx-pool): a count of instances as the copies above (was one line a track, 5.3 KB in every build of it)
_add("INS_FLANGER", "FELUCCA_INS_FLANGER", "FLNG insert (flanger)", F, 259, default=0, provenance=FELUCCA_156,
     choices=((0, "off"), (1, "1 slot (1 KB pool)"), (2, "2 slots (2 KB)"), (3, "3 slots (3 KB)"), (4, "4 slots (4 KB)")),
     desc="A flanger type for any insert slot (a track's, the master's, the USB input's) and the returns (FLNG): a short "
          "delay swept 0.25 .. 5 ms (~0.25 Hz); FLNG up to the middle mixes it in, past the middle adds feedback. 0 is "
          "off." + _INS_LIM, symbols=("ifx_flng",),
     notice="From Felucca 1.5.6's track INSERT FLANG by Leo Kuroshita (GPL-3.0)." + _INS_NOTE)
# GATER (fx/insfx/gater.c, 2026-10-14): a further insert type, any number (a few bytes a track)
_add("INS_GATER", "FELUCCA_INS_GATER", "GATER insert (tempo-synced trance gate)", F, 330, default=0,
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
     desc="SAVE > SNAPSHOT: the working project, every section and the song in one slot, loaded back whole (the state "
          "before is kept in BEFORE LOAD); export / import in the web editor. The flash comes from the end of USR3 "
          "(slots + 4 sectors of 4 KiB): the user sample slot USR3 holds that much less (docs/SNAPSHOTS.md)",
     symbols=("sn_scan", "sn_load", "ed_snap"))
_add("PATTERNS", "FELUCCA_PATTERNS", "per-track patterns and scenes", Q, 250, default=0,
     desc="16 patterns a track, stored in the section log; a section becomes a scene that names a pattern a track, "
          "so scenes share patterns and an unchanged track costs nothing. The old sections become scenes at the first "
          "start. LFO held: the PATTERN layer (launch at the end, the next bar or now; store, copy, clear); the web editor "
          "shows the slots in the mixer. About 6.1 KB of flash and 0.1 KB of RAM. Off: sections as before (a scene stored "
          "with patterns plays as the section it would be).",
     symbols=("pat_scene_put", "pat_migrate"))
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

# ---- MIDI & USB
M = "MIDI & USB"
_add("USB_MODE", "", "USB port", M, 47, default=0,
     choices=((0, "MIDI only"), (1, "CDC serial console"), (2, "USB audio + MIDI, resampled (EXPERIMENTAL)")),
     desc="What the USB port offers. USB MIDI (notes, clock, the editor and updates) is in every choice. MIDI only "
          "(default): nothing more (smallest). CDC console: a read-only serial console for diagnostics. USB audio + "
          "MIDI (EXPERIMENTAL): 4 stems in + stereo out (UAC1) next to USB MIDI; the stems are resampled to the "
          "computer's clock (cubic, after X0X 80b7d40), so a host passing the input live to an output does not drop "
          "out. It replaces the console and needs +12 KB pool (a resampler in the audio path is CPU-heavy). It streams on the FM-1 since 2026-10-12 (before, a USB audio build hung and "
          "went to the USB rescue); its audio quality is not checked yet.",
     provenance=MELODEE_USB,
     notice="USB audio capture resampler ported from X0X by Charles Vestal (GPL-3.0), 80b7d40: part of the USB audio "
            "choice of this item.")
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
_add("UI", "FELUCCA_UI", "user interface: SLOOP's pages or the Optimist rows", U, 251, default=0,
     choices=((0, "SLOOP's UI (pages, held layers)"), (1, "Optimist UI (phase 1)")),
     desc="Which user interface the firmware is built with (docs/UI-OPTIMIST-DESIGN.md). SLOOP's (the default): pages "
          "opened by the buttons, the layers held. The Optimist UI, phase 1 of its design: every screen a list of rows, "
          "the cursor row's four values on the knobs; HOME is the mixer (SELECT the row, the knobs the four tracks), "
          "SOUND the track's pages as rows, PROJECT and SYSTEM; SAVE = YES, HOME = NO, one confirm for what destroys. "
          "Not built yet in it: STEP, SONG, the held layers (only PLAY and REC), the Felucca look. Its own code "
          "replaces SLOOP's UI code, so it is smaller (the measured sizes: docs/UI-OPTIMIST-DESIGN.md section 11).")
_add("ICONS", "FELUCCA_ICONS", "parameter icons", U, 52,
     desc="A small 12 x 12 icon beside each parameter label. About 5.4 KB of flash; off: no icons, and the "
          "labels get their full width back.")
_add("OVERVIEW", "FELUCCA_OVERVIEW", "VIEW ALL overview (4 x 4 PAGEs)", U, 53,
     desc="GLO > SYSTEM VIEW ALL: shows a whole page family at once, 4 rows x 4 knobs a PAGE, PAGE n/m. About 2.4 KB "
          "of flash; off: one page at a time.")
_add("OV_ARP", "FELUCCA_OV_ARP", "ARP graph and ARP in VIEW ALL", U, 82, parent="OVERVIEW",
     desc="Shows the arpeggiator's graph and puts the ARP settings into VIEW ALL. About 0.6 KB of flash; off: the "
          "ARP graph and its VIEW ALL rows are not built.")
_add("MISSING_WARN", "FELUCCA_MISSING_WARN", "say what a project uses and this build lacks", U, 90,
     desc="'MISSING: PHYS T2, KIT 909' in the top bar when a project, song section, user preset or kit uses an "
          "engine, kit, sample set or FX this build leaves out (once per item until power-off; never stalls the "
          "audio); SAVE > TOOLS > MISS lists them again. Off: they play their stand-ins silently (saves 1.5 KB of flash)")
_add("DRUM_STEP", "FELUCCA_DRUM_STEP", "drum steps on the keys (SLOOP 2.4 'Drums with the keys')", U, 154,
     provenance=SLOOP_24,
     desc="On the DRUMS grid page (SEQ tapped on the drum track) the 16 white keys are the 16 steps of the sound KNOB 1 "
          "picks: press to set a step (you hear the sound), again to clear it; the first four black keys pick the page "
          "of steps. You hear what you pick: the sound when KNOB 1 changes it (on the grid and in the SEQ layer), the "
          "step's sounds when KNOB 2 moves to it. SELECT switches grid and kit (it is the tempo there without this "
          "item). Extra: while playing, the page follows the playhead (black key 5 turns it on / off). SEQ + a white key picks the "
          "sound (the SEQ layer's keys are its sounds). With the automation store built (PLOCK, MICRO, FILLS, CHANCE, "
          "MOTION), a step key held past the HOLD time is a held step as in the SEQ layer of the synth tracks: KNOB 4 "
          "nudge, PRESETS lock value, ALGORITHM lock parameter, KNOB 2 chance, OCT+ fill, OCT- clear (all of it). No data change. "
          "About 1.4 KB of flash, 16 B of RAM; the audition runs in the audio interrupt (no fast RAM code). Off: the "
          "keys play the pads on the grid page, as before.",
     notice="After SLOOP 2.4 'Drums with the keys' by isod89 (GPL-3.0), idea first from PR #45 by Erick Buendia Barrientos "
            "(Erbubar23). Tested in the host UI tests only, not on a device.")
_add("FAVORITES", "FELUCCA_FAVORITES", "favorites: mark sounds, browse only them", U, 141, provenance=FELUCCA,
     desc="SAVE > PRESETS (SLOOP's UI): KNOB 3 FAV marks the selected track's sound as a favorite (ON / OFF), KNOB 4 LIST "
          "shows ALL or only the favorites (the PRESETS knob and KNOB 1 then walk just those); a marked sound shows a star "
          "in the list. The Optimist UI: with the presets: SHIFT2 (OCT+ held) + PRESETS marks the sound playing, SHIFT2 + SELECT "
          "browses only the favorites, a star after a favorite's name in the toast and in PROJECT > USER (SHIFT2 + ITEM marks a slot). Up to 64 factory sounds (kept by engine and name: they stay marked over builds that leave engines "
          "out) and any of the 32 user presets (an erased preset loses its mark); the FM6 / CZ banks and the drum track have "
          "none. Kept with the settings (136 B at the end of the settings record: an older record reads whole with none; "
          "a build without this item keeps the rest and forgets them on its next save). About 1 KB of flash, 136 B of pool. "
          "Off: PRESETS KNOB 3 and 4 stay empty.",
     notice="After Felucca 1.6's favorites by Leo Kuroshita (GPL-3.0). Tested in the host UI and storage tests only, not on "
            "a device.")
_add("PARAM_HELP", "FELUCCA_PARAM_HELP", "help line: what the knob changes, in words", U, 138, default=0,
     desc="while a knob turns, the top bar (the live screens' header) names its value in plain words, e.g. 'Filter "
          "cutoff', 'Reverb send', until ~1 s after the last detent; only the lines of the features built (tools/param_help.json, also the "
          "web editor's tooltips). The Optimist UI (UI=1): the screen's last line, while a value turns and ~2 s after, and "
          "always on GLOBAL, SYSTEM and PROJECT (the cursor line, the hot action, '(asks)' where it asks first). About "
          "5.5 KB of flash (UI=1: 7.7 KB, its own lines too; the texts packed), no RAM code",
     symbols=("ph_find", "PH_BLOB"))
_add("PAGE_LINES", "FELUCCA_PAGE_LINES", "pages as lines (GLOBAL's look), not 2x2 cards + list", U, 342, default=1,
     desc="The Optimist UI (UI=1): a screen of rows with no picture (EDIT, VOICE, PROJECT, SONG, the master FX, SOUND's "
          "pages without a graph) shows its cursor row as four lines, as GLOBAL and SYSTEM do: the value's name, the "
          "value large on the right, its bar across the screen; KNOB k is line k, SELECT the next row (the header names "
          "it and its n/N). Screens with a picture (ENV, LFO, ARP, the FX needles, TEMPO, SCOPE) keep the 2x2 cards over "
          "it, the mixer its 1x4 cards. Off: the 2x2 cards with the row list under them, as before. No data change.")
_add("SYS_DEBUG", "FELUCCA_SYS_DEBUG", "SYSTEM's debug pages (internals, USB stats)", U, 341, default=0,
     desc="The Optimist UI (UI=1): SYSTEM keeps its pages for finding faults: INTERNALS (the last reset, the flash's "
          "interrupts-off time, the second core's internals), USB STATS (the USB audio glitch counters) and on INFO the "
          "clock, the second core, the drops and the CPU guard apart. Off: INFO shows what a player wants, CPU LOAD, "
          "UNDERRUNS (the drop-outs counted in) and BATTERY, in the large face, and the code of the rest is left out "
          "(about 2.9 KB less flash, measured on a UI=1 USB-audio build). The red / amber mark on the header's rule after a "
          "drop-out stays either way.")
_add("CHORD_NAMES", "FELUCCA_CHORD_NAMES", "chord names on the STEP page", U, 160,
     desc="SEQ > STEP shows a step of several notes by its chord in any inversion: C, Am, Bdim, Caug, Dsus2, Dsus4, "
          "E5, G7, Fmaj7, F#m7, Bm7b5. Other note sets stay a note and the count ('C4 +2'). About 0.3 KB of flash; "
          "off: the step's first note and the count.",
     provenance=PR45)
_add("KNOB_ACCEL", "FELUCCA_KNOB_ACCEL", "knob acceleration by turn speed", U, 67,
     desc="1 / 2 / 3 / 5 / 8 steps a detent when turned fast; never on lists (engines, kits, presets)",
     provenance=x0x("61654ba"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): knob acceleration. Tested in the emulator only.")
_add("KEYS_DMA", "FELUCCA_KEYS_DMA", "key scan by DMA (SPI2) instead of bit-banging", U, 63, default=0,
     desc="The keys' and LEDs' shift register gets its column word from SPI2's DMA instead of 16 bit-banged clock "
          "edges in the 10 kHz scan interrupt: a shorter interrupt (less time in which the audio can be held off). "
          "After M-VAVE's official FM1 Open V13 firmware. Off until checked on an FM-1.",
     tested="emulator SPI2 model only; FAILS on the FM-1 (2026-10-18): SPI2 never reports done, no key is read (the UI looks frozen)")
_add("LCD_BAUD", "LCD_BAUD", "screen data speed (SPI clock)", U, 79, default=1,
     choices=((1, "30 MHz"), (4, "12 MHz (as before X0X)"), (0, "60 MHz")),
     desc="How fast data is sent to the display: 60 MHz / (n + 1). Faster redraws finish sooner (less tearing); the "
          "ST7789V takes ~62 MHz, so 60 MHz is the limit. No flash cost; the faster settings are untested here on "
          "hardware.", provenance=x0x("d179e03"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): the faster LCD clock ran on X0X's FM-1; untested on "
            "hardware here.")
_add("LCD_DIRTY", "FELUCCA_LCD_DIRTY", "screen: send only the changed rectangle", U, 68,
     desc="Redraws only the part of the graph strip that changed instead of the whole strip: less data per frame and "
          "less tearing (with the 30 MHz screen clock). Costs about 1.5 KB of flash and 0.6 KB of RAM.",
     provenance=x0x("201b95c"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): dirty-rectangle screen updates. Not yet tried on "
            "hardware here.")

# ---- system
Y = "System"
_add("EDITOR", "FELUCCA_EDITOR", "web editor (USB SysEx)", Y, 54,
     desc="The web editor's side of the device: editing, presets, samples, backup / restore, snapshots. Firmware "
          "updates (the installer, M-UPGRADE) and the USB rescue are not part of it: "
          "every build has them.",
     off_warning="without it there is no web editor (so no backup / restore from it); installing and the USB rescue "
                 "still work")
# (bit 55, IDLE, retired 2026-10: the CPU sleeps between main-loop polls in every build; never reuse the bit)
_add("BACKUP", "FELUCCA_BACKUP", "backup / restore from the web editor", Y, 84, parent="EDITOR",
     desc="everything in flash to one .optimist-backup file and back (editor cmds 43..48); a restore onto another "
          "build reports what it skips and what plays a stand-in", off_warning="no backup: export projects and kits "
          "one by one before an update")
_add("ED_FXP", "FELUCCA_ED_FXP", "web editor: every effect slot and its parameters (cmd 94)", Y, 144, parent="EDITOR",
     desc="the web editor's FX slots by owner (track inserts, the drum bus and lane inserts, master inserts, returns, the USB input): type list, each type's own parameters (cmd 94 FXP, INFO tag 0x57). Off: the editor's Inserts / Returns pop-ups and the FX slot pages are not offered (the older FX command, 86, keeps its three send effects). About 5.3 KB of flash (measured with the Optimist UI). The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_DLAN", "FELUCCA_ED_DLAN", "web editor: drum lanes' mix, LFOs and mutes (cmd 95)", Y, 145, parent="EDITOR",
     desc="the web editor's drum lane pan / mute / solo / sequencer mute, the lane's own LFO and the tracks' sequencer mutes (cmd 95 DLAN, INFO tag 0x58). Off: the lane pop-up keeps its older knobs; no lane LFO, mute or solo from the editor. About 1.6 KB of flash (measured with the Optimist UI). The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_SCRN", "FELUCCA_ED_SCRN", "web editor: GLOBAL and SYSTEM settings (cmd 96)", Y, 146, parent="EDITOR",
     desc="the web editor's Device settings: GLOBAL and SYSTEM as the Optimist UI's lines, with their help (cmd 96 SCRN, INFO tag 0x59). Needs the Optimist UI (UI=1: with SLOOP's UI nothing is built). Off: the editor's Device-lines settings are hidden. About 1.3 KB of flash (measured with the Optimist UI). The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_SONG", "FELUCCA_ED_SONG", "web editor: the song chain (cmd 97)", Y, 147, parent="EDITOR",
     desc="the web editor's song chain: PART 1..64 (scene, bars), INSERT / DELETE, LOOP / SONG, SAVE (cmd 97 SONGC, INFO tag 0x5A). Off: the editor's song chain editor is hidden. About 0.7 KB of flash (measured with the Optimist UI). The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_TOOLS", "FELUCCA_ED_TOOLS", "web editor: pattern tools and EUCLID (cmd 98)", Y, 148, parent="EDITOR",
     desc="the web editor's pattern tools REVERSE, RANDOM, COOK, LENGTH x2 / 1/2 and EUCLID, run by the device's own function (cmd 98 TOOLS, INFO tag 0x5B). Needs the Optimist UI and SEQTOOLS. Off: the editor's tools buttons are hidden. About 0.8 KB of flash (measured with the Optimist UI). The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_SCRS", "FELUCCA_ED_SCRS", "web editor: the Optimist UI's screens as rows of cards (cmd 99)", Y, 149, parent="EDITOR",
     desc="the web editor's Device screens tab: SOUND, MIX, MASTER, PROJECT, STEPS, SONG and TEMPO pages as the device shows them, any track, lane or FM6 operator (cmd 99 SCRS, INFO tag 0x5C). Needs the Optimist UI (UI=1). Off: no Device screens tab; the editor keeps its own Sound / Mixer / Sequence tabs. About 2.8 KB of flash (measured with the Optimist UI). The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_FAVS", "FELUCCA_ED_FAVS", "web editor: favorites (cmd 100)", Y, 156, parent="EDITOR",
     desc="the web editor's stars in Load preset and its favorites-only list (cmd 100 FAVS, INFO tag 0x5D). Needs the favorites (FAVORITES). Off: no stars in the editor. About 1.0 KB of flash (measured with the Optimist UI). The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_SCOPE", "FELUCCA_ED_SCOPE", "web editor: the oscilloscope (cmd 101)", Y, 157, parent="EDITOR",
     desc="the web editor's Device screens > SCOPE: the device's own trace of the master or a track (cmd 101 SCOPE, INFO tag 0x5E). Needs the Optimist UI (UI=1). Off: no scope in the editor. About 0.6 KB of flash (measured with the Optimist UI). The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_UNDO", "FELUCCA_ED_UNDO", "web editor: undo and redo (cmd 102)", Y, 158, parent="EDITOR",
     desc="the web editor's UNDO and REDO of the patterns through the device's own undo history (cmd 102 UNDO, INFO tag 0x5F); with it built the editor's own step, lock, nudge, fill and chance writes are undo levels, as a panel step key's are (writes to one track within 800 ms are one level). Off: no undo in the editor. The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_NOTE", "FELUCCA_ED_NOTE", "web editor: play notes from the editor (cmd 103)", Y, 159, parent="EDITOR",
     desc="the web editor's keyboard: a note on / off for a track through the device's own note input (the arp, the chord layout, the voice budget, REC and MIDI OUT KEYS apply), a ring to the audio interrupt of its own, at most 8 notes held, and an automatic note off after 3 s without a request from the host, on a USB reset, or after a given length (cmd 103 NOTE, INFO tag 0x60). Off: the editor cannot play notes. The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_SOLO", "FELUCCA_ED_SOLO", "web editor: track and lane solo and mute (cmd 104)", Y, 165, parent="EDITOR",
     desc="the web editor's track and drum-lane SOLO and MUTE through the device's own masks (cmd 104 SOLO, INFO tag 0x61): the same state GLO + key 1..8 and the lane rows set. Off: the strip's SOLO stays a label. The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_STEPLANE", "FELUCCA_ED_STEPLANE", "web editor: a step in a drum lane, its lockable values (cmd 105)", Y, 166, parent="EDITOR",
     desc="the web editor's held step in a track and drum-lane context: level, ratchet, chance, condition, nudge, fill, and the device's own list of lockable values (the Optimist UI's lock pages, a drum lane's TUNE .. LEVEL) with the locks and hold events on the step (cmd 105 STEPLANE, INFO tag 0x62). Needs the automation store. Off: the editor lists raw parameter ids and has no lane context. The INFO tag is sent only when it is built, and the editor hides what it does not find.")
_add("ED_REC", "FELUCCA_ED_REC", "web editor: the REC key and its state (cmd 107)", Y, 168, parent="EDITOR",
     desc="the web editor's REC button in the transport bar: the device's own REC key (arm, start recording, stop, close a free take) and its state: recording, waiting for the start, count-in, free take (cmd 107 REC, INFO tag 0x63). Off: the editor has no REC button. The INFO tag is sent only when it is built, and the editor hides what it does not find.")
# (bit 70, SIZE, retired 2026-10: the UI, storage and editor code is always built minsize, tools/size_fns.py keeps the
#  audio path out of it; never reuse the bit)
_add("TAB_IN_POOL", "FELUCCA_TAB_IN_POOL", "sound tables and FM6 buffers in the pool, not main RAM", Y, 300, default=0,
     experimental=True, tested="emulator-tested only (no memory timing there)",
     desc="Moves what the sound loops read from main RAM to the pool: the sine, soft-clip and GRAIN window tables (about 2.5 KB, copied there from flash at start-up) and FM6's sine table, operator buses and voices (about 5.9 KB), so builder items get that much more main RAM. Unverified on hardware: both are the same SRAM as far as is known and the emulator cannot tell a speed difference, so on the FM-1 it may be slower; compare the console's TABBENCH figures and the CPU meter with it off and on. The sound is the same.")
_add("CPU_GUARD", "FELUCCA_CPU_GUARD", "predictive CPU guard (ease back before shedding)", Y, 61, default=0,
     provenance=FLOWSTATE_GUARD,
     desc="Avoids audio dropouts under heavy load by easing quality back step by step before cutting notes. Under "
          "overload (the measured load or the one predicted from the voices sounding, over 85 % for 8 halves, "
          "or a late half): first ACID without oversampling and ANALOG 2's swarm at 2 copies, then UNISON at 2 "
          "voices, then voices shed, never the bass or the lead (MONO / LEGATO / UNISON parts) nor the drums; back "
          "after 2 s under 80 % counting what each step saved. The CPU meter shows %G1..%G3. Weights: "
          "tools/builder/cpu_costs.py from tests/cpu_baseline.txt (docs/CPU-GUARD.md)")
_add("CACHE_RAM", "FELUCCA_CACHE_RAM", "more memory from the data cache (for the undo history)", Y, 92, default=0,
     choices=((0, "off"), (2, "8 KB (6 cache ways kept)"), (4, "16 KB (4 kept)"), (7, "28 KB (1 kept, as V15)")),
     experimental=True, target_only=True,
     tested="on one FM-1 2026-10-18: the boot test passes, cache RAM as fast as main RAM, but flash data reads over 16 KB 57x slower (1.7 us vs 32 ns: flash data goes through the data cache, 4 KB left)",
     desc="Uses 2, 4 or 7 of the 8 data-cache ways (4 KB each) as RAM; 7 is what M-VAVE's own FM-1 firmware and the AC79 SDK's flash-only "
          "builds do; the instruction cache (the flash's code and constants) keeps all its ways. The memory is tested "
          "at start-up; when the test passes it becomes the undo history's second part (28 KB instead of what main "
          "RAM has left), else everything is put back as it was and nothing uses it. It costs about 0.4 KB of flash and "
          "0.1 KB of fast RAM code. "
          "Off until checked on an FM-1: whether the flash's data reads use the data cache (then the sound's tables "
          "in flash could cost CPU) is not known; the console's tabbench (flash 16K) and boot lines show it.")
_add("ASM", "FELUCCA_ASM", "hand-written assembly speed-ups (FM6, ANALOG 2)", Y, 56, target_only=True,
     desc="The hottest loops of FM6 and ANALOG 2 written in the processor's assembly: the same sound, about 8 to 19 % "
          "less audio CPU (emulator), for about 0.6 KB of flash and 0.7 KB of fast RAM code. Keep on; off only to "
          "compare with the plain C.")
# (bit 57, SIMD, withdrawn 2026-10-12: the packed 16-bit forms compute something else on the real FM-1 than the emulator
#  assumes, a build with them crackles; the code stays (hal/fm1_simd.h, FELUCCA_SIMD default 0, the boot probe
#  FELUCCA_SIMD_PROBE through tools/build.py); RETIRED below; never reuse the bit)
# (bit 51, SPLASH, retired 2026-10-15: the dinghy boot logo is always built; never reuse the bit)

# ---- the second core
X = Y
JANGADA = Provenance("Jangada (zednaked/jangada)", "zednaked", "GPL-3.0-only", "07cce38",
                     "https://github.com/zednaked/jangada")
_add("DUAL", "FELUCCA_DUAL", "second CPU core renders parts 2-3", X, 58, default=2,
     choices=((0, "off"), (2, "on")), target_only=True, provenance=x0x("47f8e4c"),
     notice="Its guard against misread RAM is ported from X0X by Charles Vestal (GPL-3.0): the second core runs only "
            "the next job, with a matching check word, and is held at the first one it refuses.",
     desc="The FM-1's second CPU core renders synth parts 2 and 3 while the first renders the rest, "
          "cutting the first core's load by 40 to 44 % in the emulator, with the same sound. It costs about 1.8 KB of "
          "flash, 1.9 KB of RAM and 6 KB of pool. On some FM-1s the second core misreads RAM at the boot loader's "
          "supply: it then runs nothing it misread, is held for the session and the FM-1 goes on with one core "
          "(the message CPU1 OFF MISREAD); a boot after one that crashed in its first 30 s stays on one core.",
     tested="emulator-tested; one user's FM-1 runs it")
_add("STOCK_SUPPLY", "FELUCCA_STOCK_SUPPLY", "raise the core rails to the FM-1's stock values at start-up (for the second core)",
     X, 62, default=1, parent="DUAL", target_only=True, provenance=MELODEE_SUPPLY,
     notice="At your own risk: the FM-1's own firmware raises its rails this way, every custom firmware "
            "leaves them at the boot loader's lower levels. 1.38 V core and 1.60 V on the 1.4 V rail are above the "
            "AC79 datasheet's table (the chip's absolute limits are not public); chips vary from unit to unit; the "
            "effect on heat and battery life has not been measured.",
     desc="At start-up, before the second core, the audio and Bluetooth, the FM-1's rails are set as its own M-VAVE "
          "firmware sets them: the 1.4 V rail moved from its LDO to the DC-DC, then raised to 1.60 V (VDC14 7), "
          "then the core rail to 1.38 V (SYSVDD 15), a step at a time, never lowered (the boot loader leaves 1.40 V "
          "and 1.26 V). Read back after: if a value is wrong, the second core stays off (2ND CORE OFF SUPPLY) and "
          "the FM-1 runs on one core. SYSTEM > INTERNALS shows the rails (SUPPLY). Second-core builds only; it "
          "replaces the earlier DUAL_BOOST (1.50 V / 1.38 V on the LDO). On the second core's test unit both cores "
          "crashed within seconds at the boot loader's rails and ran 3 x 3 minutes with 1.50 V / 1.38 V; the stock "
          "sequence itself is still being measured.",
     tested="the sequence emulator- and host-tested; 1.50 V / 1.38 V measured on one FM-1 (2026-10-16)")
_add("BLE", "FELUCCA_BLE", "Bluetooth LE MIDI, our own stack", M, 252, default=0,
     target_only=True,
     desc="BLE MIDI as the stock firmware offers it (named Optimist XXXX, the BLE-MIDI service), from a stack written "
          "for Optimist (docs/BLE-STACK.md): BLE in plays the synth, the FM-1's own notes go out with real timestamps. "
          "Tested on an FM-1 with macOS Audio MIDI Setup (not yet with iOS or Windows). "
          "The radio's start-up tables, captured from the stock V15 firmware, are included: no V15 file is needed "
          "(FM1_STOCK_FWSC makes a fresh capture instead, with the same result). "
          "Bluetooth starts OFF: switch it on in HOME > MENU > BLUETOOTH. MIDI IN must be NOTES for notes to play "
          "(HOME menu SYSTEM). About 30 KB of flash and 6 KB of RAM (the menu shows the measured figures; BLE_DIAG below adds more); where "
          "the build then overflows, ticking it removes the smallest single item that frees enough (sample sets "
          "first, then other items), or the one you pick instead.", symbols=("ble_in_q",),
     tested="tested on an FM-1 with macOS Audio MIDI Setup, not yet with iOS or Windows")
_add("BLE_DIAG", "FELUCCA_BLE_DIAG", "BLE diagnostics: the console's blell, counters and rings", M, 253, default=0,
     parent="BLE", target_only=True,
     desc="For finding BLE faults: the console's blell command (needs a CDC / console build, USB_MODE 1) "
          "and the counters and rings behind it (link-layer events, the radio's receive and transmit state, the "
          "protocol packets, BLE-MIDI in). About 6 KB of flash and 2.3 KB of RAM, and 6 KB of flash more with the "
          "console (USB_MODE 1) for blell itself. Leave it off for normal use: BLE works the same without it; "
          "bletrim and blevm stay, and so does the boot breadcrumb.",
     symbols=("con_blell",))
_add("BLE_BOND", "FELUCCA_BLE_BOND", "BLE bonding: pairing (Just Works) and an encrypted link", M, 254, default=0,
     parent="BLE", target_only=True,
     desc="LE legacy pairing with Just Works (no passkey, as the FM-1 has no screen keypad for it) and "
          "bonding, so a Mac or phone that pairs once gets an encrypted link and finds the FM-1 bonded after a power-off. "
          "The FM-1 hands out its identity and takes the other side's (its IRK and identity address, kept until "
          "power-off for now), which reconnecting to devices with private addresses needs later. Pairing happens only when the other side asks for it. Not yet "
          "tested on an FM-1. About 6.7 KB of flash and 0.7 KB of RAM.",
     symbols=("bsmp",), tested="host and emulator only")
_add("BLE_CENTRAL", "FELUCCA_BLE_CENTRAL", "BLE devices: connect to BLE-MIDI devices, reconnect the last one", M, 255,
     default=0, parent="BLE", target_only=True,
     desc="HOME > MENU > BLUETOOTH > DEVICES lists the BLE-MIDI devices nearby (keyboards, controllers, "
          "a Mac or an iPhone app that advertises BLE MIDI, another FM-1) with their names and relative signal bars, "
          "scanning only while the list is open (the FM-1 stops advertising meanwhile). Picking one connects to it as "
          "a central (pairing with Just Works when it asks, as Apple's devices do), MIDI both ways; it becomes the "
          "last device, which the FM-1 reconnects to by itself while Bluetooth is on. Brings the bonding of BLE_BOND "
          "with it. Without this item DEVICES shows NONE and the last device only. Not yet tested on an FM-1.",
     symbols=("ble_found",), tested="host and emulator only")
_add("BT_INQ_PROBE", "FELUCCA_BT_INQ_PROBE", "Bluetooth Classic inquiry probe (console btclk / btinq)", M, 280,
     default=0, experimental=True, target_only=True,
     tested="host-tested (sync word, FHS, EIR) and emulator-tested (clock and interrupts only); not run on an FM-1",
     notice="experimental: a Bluetooth Classic inquiry probe; no BLE in this build",
     desc="Stage 1 of our own Bluetooth Classic stack (docs/BT-CLASSIC-HW-FACTS.md section 14), for testing on an "
          "FM-1 from the CDC console (needs USB_MODE 1, and BLE off: one engine on the radio at a time). At start-up "
          "it starts the radio with the stored calibration (stock V15's VM; none: nothing is started) and checks the "
          "Bluetooth clock and its interrupts for 3 s, transmitting nothing (console btclk). btinq [SECONDS] then runs "
          "an inquiry (10.24 s by default): the FM-1 sends inquiry IDs and prints every device that answers (its "
          "address, class, clock, signal and EIR name) and a status line a second; btscan [SECONDS] is the other side (an "
          "inquiry scan: a phone or computer searching finds OPTIMIST BT); both take options for the hardware's unknowns "
          "(entries, RX gain, window, frequency, carrier offset) and bttrace / btrx print their interrupts and receptions. "
          "btpage ADDR pages a device as the master and sets the link up with a minimal LMP (features, version, host "
          "connection, setup complete; a Raspberry Pi accepted it as a connection): no profile, no audio yet. "
          "About 26 KB of flash (most of it the radio's start-up program, which BLE carries too), 1.8 KB of RAM and "
          "13 KB of pool.",
     symbols=("bti_st",))

for _it in _ITEMS:
    if _it.group == "FX":
        _it.group = fx_group(_it.key, _it.parent)
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
    if item.group in ("FX", FX_ANY, FX_ONCE):
        item.group = fx_group(item.key, item.parent)
    _ITEMS.append(item)
    ITEMS[item.key] = item
    if item.parent:
        assert item.parent in ITEMS and not ITEMS[item.parent].parent
        ITEMS[item.parent].children.append(item.key)


def to_json():
    out = []
    for it in _ITEMS:
        d = {k: getattr(it, k) for k in ("key", "flag", "label", "group", "bit", "default", "parent", "desc",
                                          "experimental", "notice", "off_warning", "env", "target_only", "symbols", "no_image")}
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
    "ENG_GRAIN": ("ENG_GRAIN", "grain_block"), "ENG_FM6": ("ENG_FM6", "fm6_pt", "FM6_INIT", "fm6k_knob"),
    "FM6_KEYS": ("fm6k_knob",), "FM6_PK_KEYS": ("FM6_PK_KEYS",), "FM6_PK_BASS": ("FM6_PK_BASS",),
    "FM6_PK_BELLS": ("FM6_PK_BELLS",), "FM6_PK_PADS": ("FM6_PK_PADS",), "FM6_PK_80S": ("FM6_PK_80S",),
    "ENG_SLICE": ("ENG_SLICE",),
    "DRUM_SYNTH": ("DS_KITS",), "DRUM_EDIT": ("de_synth",), "FX_CHORUS": ("cho_buf",),
    "FX_DELAY": ("dly_buf",), "FX_REVERB": ("rev_line", "rev_ap"), "FX_SLICER": ("sl_buf",),
    "FX_PUNCH": ("punch_ring",), "ICONS": ("ICON_DATA",), "EDITOR": ("ed_handle",),
    "DRUM_X0X909": ("drum909_trigger", "x0x_smp_hh_m"),
    "X909_CYM": ("x0x_smp_ride_m", "x0x_smp_crash_m"), "DRUM_X0X808": ("drum808_trigger",),
    "CPU_GUARD": ("cg", "cg_post", "CG_VCOST"),
}
for _k, _v in _SYMS.items():
    ITEMS[_k].symbols = _v
