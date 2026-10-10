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
    "REVERB": lambda v: {"REV_ROOM": int(v == 0), "REV_PLATE": int(v == 1), "REV_FDN8": int(v == 2)},   # the one-tank choice
}

# Dropped items: a .config naming one still loads; the setting is ignored with a warning (configure.py parse)
RETIRED = {
    "VIS": "the SLOOP 2.4 visualiser was dropped from the builder for now (2026-10-10); HOME on TRACKS flips the mixer dial page",
}

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


GROUPS = ["Reserve", "Synth engines", "Drums", "Sample sets", "FX", "Sequencer", "MIDI & USB", "UI", "System", "Experimental"]

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
     desc="Six-operator FM that plays Yamaha DX7 voices the way the Dexed plug-in does, with its own operator "
          "editor and DX7 SysEx. The biggest engine: about 32 KB of flash, 10 KB of RAM and 12 KB of pool; "
          "its options below trim it down.")
_add("FM6_MARK1", "FELUCCA_FM6_MARK1", "FM6 mode MARK I (DX7's own tables)", E, 10, parent="ENG_FM6",
     desc="The FM6 ENGINE mode that uses the DX7's own log-sine and exponent tables and its feedback behaviour, "
          "for the most DX7-like sound. Costs about 3.2 KB of flash and 4.1 KB of RAM; off: voices asking for it "
          "play another mode you kept.")
_add("FM6_MKI_FLASH", "FELUCCA_FM6_MKI_FLASH", "FM6 MARK I tables in flash", E, 89, default=0, parent="ENG_FM6",
     desc="with MARK I: its log-sine and exponent tables as generated const data in flash instead of RAM tables built "
          "at boot (4 KB of RAM less, 1.9 KB of flash more). CPU cost on the FM-1 unknown: the emulator models neither "
          "the XIP cache nor flash wait states",
     notice="measured in the emulator only, which has no XIP cache and no flash wait states; "
            "on the FM-1 every MARK I operator sample reads flash twice: the CPU cost there is not measured.")
_add("FM6_MODERN", "FELUCCA_FM6_MODERN", "FM6 mode MODERN (clean 24-bit)", E, 11, parent="ENG_FM6",
     desc="The FM6 ENGINE mode with Dexed's modern 24-bit maths (MSFA). Costs only about 0.6 KB of flash (the sine "
          "table stays: the LFO uses it); keep at least one of the three modes.")
_add("FM6_OPL", "FELUCCA_FM6_OPL", "FM6 mode OPL (grittier, chip resolution)", E, 12, parent="ENG_FM6",
     desc="The FM6 ENGINE mode with the resolution of Yamaha OPL chips, for a different tone from the DX7 modes. "
          "Costs about 1.9 KB of flash and 1.5 KB of RAM (tables).")
_add("FM6_KEYS", "FELUCCA_FM6_KEYS", "FM6 operator editor on the black keys", E, 13, parent="ENG_FM6",
     desc="Edit a DX7 voice on the device: hold ENV and press a black key to pick an operator, the pitch envelope or "
          "the global page, then turn the knobs. About 7.8 KB of flash; off: voices can only be changed by DX7 "
          "SysEx or the web editor.")
_add("FM6_SYSEX", "FELUCCA_FM6_SYSEX", "FM6 DX7 SysEx in / SEND (web editor FM6 tab)", E, 14, parent="ENG_FM6",
     desc="Accepts DX7 voice, bank and parameter changes over USB MIDI, so Dexed or any DX7 librarian can edit a "
          "part live, and can SEND the voice back; the web editor's FM6 tab needs it. About 1.2 KB of flash; its "
          "4.1 KB RAM buffer is shared with STORE.")
_add("FM6_VOICES", "FELUCCA_FM6_VOICES", "FM6 16 factory voices R01..R16", E, 15, parent="ENG_FM6",
     desc="The 16 built-in DX7 voices R01..R16 (about 2 KB of flash). Off: the R slots play INIT VOICE, a plain "
          "starting voice; your own bank (U voices) is not affected.")
_add("FM6_ALL", "FELUCCA_FM6_ALL", "FM6 operator editor in VIEW ALL (pages as rows)", E, 80, parent="ENG_FM6",
     desc="with VIEW ALL: an operator's pages (or PIT, GLO) as rows of 4 x 4 PAGEs; off: one page at a time "
          "(saves about 1.2 KB of flash)")
_add("FM6_ALGO", "FELUCCA_FM6_ALGO", "FM6 algorithm full screen (hold ENV)", E, 81, parent="ENG_FM6",
     desc="Holding ENV a while on an FM6 track shows the algorithm full screen, with operators numbered and "
          "carriers, modulators and the feedback loop told apart. Only 48 B of flash; off: holding ENV does nothing extra.")
_add("FM6_STORE", "FELUCCA_FM6_STORE", "FM6 user bank STORE (U01..U32 in a USR slot)", E, 16, parent="ENG_FM6",
     desc="STORE your edited voices into a user bank of 32 (U01..U32), kept in a USR sample slot. About 0.6 KB of "
          "flash and 0.5 KB of RAM; off: no user bank (STORE says NO USER BANK).")
MELODEE_012 = Provenance("Melodee (keremimo/melodee)", "Kerem Kilic", "GPL-3.0-only", "v0.12 18bfd53", MELODEE_URL)
_add("NATIVE_BANKS", "FELUCCA_NATIVE_BANKS", "FM6 / CZ native tone collections in PRESETS", E, 235, default=0,
     provenance=MELODEE_012,
     desc="An FM6 or CZ track's PRESETS list ends with its engine's own tones: FM6 U01..U32 (the FM6 user bank) and, "
          "with CZ, a collection of 26 CZ-1 tones (the web editor imports and exports Casio .syx). Loading one sets "
          "only the engine's values: the track keeps its FX, mix and pattern. With CZ the collection takes 8 KiB at "
          "USR3's end (USR3 holds that much less). About 0.6 KB of flash, 2 KB with CZ (measured); no RAM.",
     symbols=("nb_count", "nb_load"))
_add("ENG_SLICE", "FELUCCA_ENG_SLICE", "SLICE (break slicer + its BREAK sample)", E, 17, default=0,
     desc="Break slicer: cuts a loop (the built-in BREAK, 22 KB of samples, or a USR slot) into 4 / 8 / 16 / 32 "
          "slices or one per hit and plays them from the keys or the sequencer. About 27 KB of flash and 6 KB of "
          "RAM; off by default.", provenance=FELUCCA)

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
                    "Experimental in Optimist: float DSP, emulator-tested only.")
_add("DRUM_X0X909", "FELUCCA_DRUM_X909", "X0X 909 kit (circuit-modelled TR-909)", D, 86, default=0, experimental=True,
     provenance=x0x("80b7d40"), notice=X0X_DRUMS_NOTICE,
     desc="X0X's TR-909 drum kit (kit UID 37; the drum models are circuit-modelled, hi-hats, ride and crash are "
          "samples), on the 16 lanes; SHAKER, CONGA, COWBELL play the synthesised 909's. Big: about 151 KB of flash "
          "(see its cymbal option) and uses floating point; emulator-tested only. A build without it plays the "
          "synthesised 909 for it and keeps the kit")
_add("X909_CYM", "FELUCCA_X909_CYM", "X0X 909: ride and crash sample quality", D, 87, parent="DRUM_X0X909",
     choices=((1, "8-bit (93 KB, 42 dB)"), (2, "6-bit (70 KB, 30 dB)"), (0, "off")),
     desc="8-bit block floating point as before; 6-bit: 22 KB less, 30 dB against the 16-bit source instead of 42 "
          "(screens x0xdrums-perf-2026-10-06); off: RIDE and CRASH play the synthesised 909's (the hi-hat samples stay)")
_add("DRUM_X0X808", "FELUCCA_DRUM_X808", "X0X 808 kit (circuit-modelled TR-808)", D, 88, default=0, experimental=True,
     provenance=x0x("80b7d40"), notice=X0X_DRUMS_NOTICE,
     desc="X0X's TR-808 drum kit (kit UID 38; 16 circuit-modelled sounds) on the 16 lanes; MIDI also plays MT, LC, "
          "HC and the claves (note 75). About 30 KB of flash and uses floating point; emulator-tested only. A build "
          "without it plays the synthesised 808 for it and keeps the kit")

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
_add("FX_CHORUS", "FELUCCA_FX_CHORUS", "chorus send bus", F, 36,
     desc="The chorus effect, fed by each track's CHO send. About 0.4 KB of flash and 4 KB of pool; off: the CHO "
          "sends and the chorus settings disappear.")
_add("FX_DELAY", "FELUCCA_FX_DELAY", "delay send bus", F, 37,
     desc="The tempo-synced delay (echo), fed by each track's DLY send. Costs 128 KB of the pool at the full "
          "length: shorten it with the length option, or leave it out.")
_add("DLY_LEN", "FELUCCA_DLY_LEN", "delay length (how long an echo can be)", F, 38, default=65536, parent="FX_DELAY",
     choices=((65536, "1.49 s (128 KB pool)"), (32768, "0.74 s (64 KB)"), (16384, "0.37 s (32 KB)")),
     desc="The longest delay time, which sets the pool memory the delay takes. A time longer than the line is "
          "halved (see 'long delay times halve') or cut; shorter lines suit fast tempos only.")
_add("FX_REVERB", "FELUCCA_FX_REVERB", "reverb send bus", F, 39,
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
          "meter in the web mixer. Projects that leave it off sound as before. About 2 KB of flash and 2.5 KB of "
          "RAM.",
     symbols=("mc", "mlim"))
_add("TRK_FILT", "FELUCCA_TRK_FILT", "track FILTER (LP <- off -> HP on each track)", F, 205, default=0, provenance=SLOOP_24,
     desc="A DJ-style filter on each track: FX > FILTER (one knob, FILT: left low-pass, right high-pass, centre off) and "
          "FX held + KNOB 4 for the selected track. On the drum track it filters the summed drums and all their sends. "
          "Saved with the project, kept when the sound changes, recordable with motion. Costs nothing at the centre; "
          "engaged, one filter per track (five on the drum bus).",
     notice="From SLOOP 2.4's track filter by isod89 (isod89/sloop-fm1 v2.4, GPL-3.0). Tested in the host tests only, "
            "not on a device.")
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
_add("USB_MODE", "", "USB port", M, 47, default=2,
     choices=((1, "CDC serial console"), (2, "USB audio (EXPERIMENTAL)"), (0, "MIDI only")),
     desc="What the USB port offers besides MIDI. USB audio (default, experimental): 4 stems in + stereo out (UAC1); it "
          "replaces the console and needs +12 KB pool. CDC console: a read-only serial console for diagnostics. MIDI only: "
          "neither (smallest).",
     provenance=MELODEE_USB)
_add("UART", "FELUCCA_UART", "TRS MIDI IN", M, 48,
     desc="MIDI input on the TRS jack (notes and clock from a keyboard or sequencer). About 0.6 KB of flash; off: "
          "the jack hears nothing, and SYNC TRS has no clock.")
_add("MIDI_CLOCK", "FELUCCA_MIDI_CLOCK", "MIDI clock in (SYNC AUTO TRS > USB > INT)", M, 49,
     desc="Follow an external MIDI clock and start / stop (GLO > SYSTEM SYNC: AUTO takes the TRS jack, else USB, "
          "else the internal tempo). About 4.2 KB of flash; off: the FM-1 always runs on its own tempo.")
_add("UA_RESAMPLE", "FELUCCA_UA_RESAMPLE", "USB audio: resample to the host clock", M, 71, default=0,
     experimental=True, desc="Experimental, only with USB audio: the audio sent to the computer is resampled to follow "
     "the computer's clock instead of using packet-size feedback. CPU-heavy (a resampler in the audio path); "
     "about 0.4 KB of flash.", provenance=x0x("80b7d40"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): USB audio resampler. Experimental in Optimist; "
            "CPU-heavy (a resampler in the audio path), only with USB audio.")
_add("MIDI_EXPR", "FELUCCA_MIDI_EXPR", "MIDI expression (bend, mod, sustain, RPN)", M, 50, provenance=MELODEE_MIDI,
     desc="Plays more than notes from MIDI: pitch bend, mod wheel, breath, foot, aftertouch, sustain pedal and "
          "pitch-bend range (RPN 0). About 0.5 KB of flash; off: only notes and the panic messages work.")
# (bits 195, 196, 197 -- MIDI_CH, MIDI_OUT, MIDI_INCLK -- retired 2026-10: a MIDI channel for each track, MIDI OUT = SEQ and
#  MIDI IN = CLOCK are always built; the user's setting is the HOME menu, SYSTEM 1/3 and 2/3; never reuse the bits)

# ---- UI
U = "UI"
_add("UI", "FELUCCA_UI", "user interface: SLOOP's pages or the Optimist rows", U, 251, default=0,
     choices=((0, "SLOOP's UI (pages, held layers)"), (1, "Optimist UI (EXPERIMENTAL, phase 1)")),
     experimental=True,
     desc="Which user interface the firmware is built with (docs/UI-OPTIMIST-DESIGN.md). SLOOP's (the default): pages "
          "opened by the buttons, the layers held. The Optimist UI, phase 1 of its design: every screen a list of rows, "
          "the cursor row's four values on the knobs; HOME is the mixer (SELECT the row, the knobs the four tracks), "
          "SOUND the track's pages as rows, PROJECT and SYSTEM; SAVE = YES, HOME = NO, one confirm for what destroys. "
          "Not built yet in it: STEP, SONG, the held layers (only PLAY and REC), the Felucca look. Its own code "
          "replaces SLOOP's UI code, so it is smaller (the measured sizes: docs/UI-OPTIMIST-DESIGN.md section 11).")
_add("SPLASH", "FELUCCA_SPLASH", "boot logo", U, 51,
     desc="the Optimist logo (drawn, no bitmap), the name and the version for 0.9 s at power-on (0.3 KB of flash); "
     "off: a dark screen until the UI")
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
_add("PARAM_HELP", "FELUCCA_PARAM_HELP", "help line: what the knob changes, in words", U, 138, default=0,
     desc="while a knob turns, the top bar (the live screens' header) names its value in plain words, e.g. 'Filter "
          "cutoff', 'Reverb send', until ~1 s after the last detent; only the lines of the features built (tools/param_help.json, also the "
          "web editor's tooltips). About 6.4 KB of flash, no RAM",
     symbols=("ph_find", "PH_BLOB"))
_add("CHORD_NAMES", "FELUCCA_CHORD_NAMES", "chord names on the STEP page", U, 160,
     desc="SEQ > STEP shows a step of several notes by its chord in any inversion: C, Am, Bdim, Caug, Dsus2, Dsus4, "
          "E5, G7, Fmaj7, F#m7, Bm7b5. Other note sets stay a note and the count ('C4 +2'). About 0.3 KB of flash; "
          "off: the step's first note and the count.",
     provenance=PR45)
_add("KNOB_ACCEL", "FELUCCA_KNOB_ACCEL", "knob acceleration by turn speed", U, 67,
     desc="1 / 2 / 3 / 5 / 8 steps a detent when turned fast; never on lists (engines, kits, presets)",
     provenance=x0x("61654ba"),
     notice="Ported from X0X by Charles Vestal (GPL-3.0): knob acceleration. Tested in the emulator only.")
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
_add("OTA", "FELUCCA_OTA", "web editor and firmware updates (M-UPGRADE)", Y, 54,
     desc="The USB SysEx side of the device: the web editor (editing, presets, samples, backup / restore, snapshots), "
          "firmware updates from the M-UPGRADE tool and the rescue updater. About 18 KB of flash and 6 KB of RAM; "
          "leave it on unless you update through the UBOOT rescue path.",
     off_warning="without it there is no web editor (so no backup / restore from it) and no M-UPGRADE updates: "
                 "updating needs the UBOOT rescue path")
_add("IDLE", "FELUCCA_IDLE", "sleep the CPU between main-loop polls (power, heat)", Y, 55,
     desc="Sleep the CPU (wait for an interrupt) between main-loop polls instead of spinning. Saves power and heat on "
          "the device and makes the emulator much faster (it skips the idle time); no effect on sound (audio and "
          "the panel run from interrupts, 100 us latency at most). No flash cost. Keep on; off only for debugging.")
_add("BACKUP", "FELUCCA_BACKUP", "backup / restore from the web editor", Y, 84,
     desc="everything in flash to one .optimist-backup file and back (editor cmds 43..48); a restore onto another "
          "build reports what it skips and what plays a stand-in", off_warning="no backup: export projects and kits "
          "one by one before an update")
_add("SIZE", "", "smaller UI and storage code (size-optimised)", Y, 70, default=1, choices=((1, "minsize (UI, stores, editor)"),
     (0, "-Os everywhere")), desc="Builds the UI, storage and editor code for minimum size, about 6.8 KB of flash "
     "less than normal -Os, at no cost to sound: the audio path is never size-optimised (tools/size_fns.py guards "
     "it). Choose '-Os everywhere' only to compare.",
     provenance=Provenance("Felucca 1.0.1 (hugelton/Felucca)", "Leo Kuroshita (Hügelton)", "GPL-3.0-only",
                           "20c275e", FELUCCA_URL))
_add("CPU_GUARD", "FELUCCA_CPU_GUARD", "predictive CPU guard (ease back before shedding)", Y, 61, default=0,
     provenance=FLOWSTATE_GUARD,
     desc="Avoids audio dropouts under heavy load by easing quality back step by step before cutting notes. Under "
          "overload (the measured load or the one predicted from the voices sounding, over 85 % for 8 halves, "
          "or a late half): first ACID without oversampling and ANALOG 2's swarm at 2 copies, then UNISON at 2 "
          "voices, then voices shed, never the bass or the lead (MONO / LEGATO / UNISON parts) nor the drums; back "
          "after 2 s under 80 % counting what each step saved. The CPU meter shows %G1..%G3. Weights: "
          "tools/builder/cpu_costs.py from tests/cpu_baseline.txt (docs/CPU-GUARD.md)")
_add("ASM", "FELUCCA_ASM", "hand-written assembly speed-ups (FM6, ANALOG 2)", Y, 56, target_only=True,
     desc="The hottest loops of FM6 and ANALOG 2 written in the processor's assembly: the same sound, about 8 to 19 % "
          "less audio CPU (emulator), for about 0.6 KB of flash and 0.7 KB of fast RAM code. Keep on; off only to "
          "compare with the plain C.")
_add("SIMD", "FELUCCA_SIMD", "packed 16-bit sine and swarm maths (EXPERIMENTAL)", Y, 57, default=0, parent="ASM", experimental=True,
     target_only=True,
     desc="Experimental: uses the processor's packed 16-bit instructions for the sine lookup (DIGITAL, PHASE, drum "
          "synth) and ANALOG 2's swarm. It needs 4 KB more RAM, and what those instructions do is inferred, not "
          "documented: emulator-tested, never run on a real FM-1. Leave off.")

# ---- experimental
X = "Experimental"
_add("DUAL", "FELUCCA_DUAL", "second CPU core renders parts 2-3 (EXPERIMENTAL)", X, 58, default=0, experimental=True,
     choices=((0, "off"), (2, "on")), target_only=True,
     desc="Experimental: the FM-1's second CPU core renders synth parts 2 and 3 while the first renders the rest, "
          "cutting the first core's load by 40 to 44 % in the emulator, with the same sound. It costs about 1.8 KB of "
          "flash, 1.9 KB of RAM and 6 KB of pool, and has never run on a real FM-1.")
_add("BLE", "FELUCCA_BLE", "Bluetooth LE MIDI, our own stack (EXPERIMENTAL)", X, 252, default=0,
     experimental=True, target_only=True,
     desc="Experimental: BLE MIDI as the stock firmware offers it (FM-1_BLE, the BLE-MIDI service), from a stack written "
          "for Optimist (docs/BLE-STACK.md): BLE in plays the synth, the FM-1's own notes go out with real timestamps. "
          "Tested on an FM-1 with macOS Audio MIDI Setup (not yet with iOS or Windows). "
          "The first build needs your own stock FM-1.fwsc (V15): give its path in FM1_STOCK_FWSC (the emulator's "
          "diagnose is needed that once too); the build captures the radio's start-up tables from it in about 30 s and "
          "keeps them in config/ble/ (git-ignored), so later builds need neither. "
          "Bluetooth starts OFF: switch it on in HOME > MENU > BLUETOOTH. MIDI IN must be NOTES for notes to play "
          "(HOME menu SYSTEM). About 30 KB of flash and 6 KB of RAM (the menu shows the measured figures; BLE_DIAG below adds more); where "
          "the build then overflows, ticking it removes the smallest single item that frees enough (sample sets "
          "first, then other items), or the one you pick instead.", symbols=("ble_in_q",),
     tested="tested on an FM-1 with macOS Audio MIDI Setup, not yet with iOS or Windows")
_add("BLE_DIAG", "FELUCCA_BLE_DIAG", "BLE diagnostics: the console's blell, counters and rings", X, 253, default=0,
     parent="BLE", target_only=True,
     desc="Experimental, for finding BLE faults: the console's blell command (needs a CDC / console build, USB_MODE 1) "
          "and the counters and rings behind it (link-layer events, the radio's receive and transmit state, the "
          "protocol packets, BLE-MIDI in). About 6 KB of flash and 2.3 KB of RAM, and 6 KB of flash more with the "
          "console (USB_MODE 1) for blell itself. Leave it off for normal use: BLE works the same without it; "
          "bletrim and blevm stay, and so does the boot breadcrumb.",
     symbols=("con_blell",))

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
