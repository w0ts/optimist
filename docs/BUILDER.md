# Firmware builder

Pick the engines, FX, drum kits, sample sets and features that go into your FM-1 firmware, with a live flash,
RAM, pool and RAMTEXT budget, then build it. Design and inventory: `docs/BUILDER-DESIGN.md`;
the memory budget of the full integration: `docs/MEMORY-BUDGET.md`.

All numbers come from the emulator and host builds: there is no real FM-1 here yet.

## Run it

```sh
python tools/optimist.py setup                    # once: the venv, the SDK files, the toolchain (BUILDING.md)
python tools/optimist.py builder                  # the interactive menu (Textual)
python tools/optimist.py builder --profile drum-machine   # the menu, starting from a profile
python tools/optimist.py builder --config my.config       # the menu on a saved configuration
```

`tools/optimist.py` runs the same on macOS, Linux and Windows. On macOS and Linux `tools/menuconfig` (and
`make builder`) still work and call it.

Keys: `space` / `enter` toggle (a sized item: its next value), `/` search, `p` profiles (shipped + yours), `s` save as your own profile (config/my-profiles, git-ignored; then `--profile NAME` works too), `w` write a .config file, `l` load,
`b` build, `e` build and run the new firmware in the emulator (96 MHz, in the background), `x` / `c` expand / collapse all, `q` quit.

The bars show the estimate from the measured deltas (`tools/builder/costs.json`), red with "OVER by n" when a
region overflows; the message panel then names the biggest items of that region. `b` runs the real build
(about 10 s): the exact sizes replace the estimate, and when it fits the package is `build/felucca.fwsc`, with
its web editor sidecar `build/felucca-ui.zip` (index.html, the font and its licence, SOURCE.txt: the commit and
the configuration's hash; the emulator's fm1-ui serves it beside the firmware).

A configuration error marks the lines of the items it concerns as soon as it holds, in red after the item
(`✗ the drum track needs a drum source: …`; an option's error marks its parent too, in case it is folded),
and the message panel lists it; fixing it clears the marks at once. `b` on an invalid configuration still
says CANNOT BUILD in the panel and keeps it there, like a build's result, until the next `b`.
(`configure.validate()` returns each message as an `Issue`: a string with `.keys`, the items it names.)

Without the menu (scripts, tests, CI): `python tools/optimist.py build | package | config ...` (BUILDING.md,
"Quick start"), for example

```sh
python tools/optimist.py package --config my.config --out dist --summary dist/result.json   # exit 0 = built and fits
```

or, as before on macOS and Linux:

```sh
tools/menuconfig --list                               # the registry with the default values
tools/menuconfig --profile fm-va-studio --budget      # estimate
tools/menuconfig --profile fm-va-studio --set FX_PUNCH=0 --write my.config
tools/menuconfig --config my.config --build           # real build: exact sizes, the package if it fits
tools/menuconfig --profile everything-that-fits --fit # drop items (least loss first) until it fits
sh build.sh --config my.config                        # the same build, directly
tools/menuconfig --profile drum-machine --package ~/GitHub/fm1-firmware   # optimist-drum-machine-<date>.fwsc + -ui.zip
```

The old environment switches still work and override the `.config` (`FELUCCA_ICONS=0 sh build.sh`).

## What is switchable

`tools/builder/registry.py` is the list; `tools/menuconfig --list` prints it. One level only: a top-level
item and its own options (FM6's modes, the sampled kits, the delay length...); an option is ignored while its
parent is off, and no option depends on another item.

| Group | Items |
|---|---|
| Synth engines | ANALOG 2, DIGITAL, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, FM6, SLICE, PHYS, ACID (at least one) |
| FM6 options | MARK I / MODERN / OPL modes (at least one; ENGINE lists only those built), MARK I tables in flash (off: CPU cost on the FM-1 not measured), black-key editor, its VIEW ALL rows, the algorithm long press, DX7 SysEx, factory voices, user bank STORE |
| Drums | drum synth (all synthesised kits: one switch), sampled drums (one switch per kit), sound editor, user samples on lanes, user kits, per-lane sends (at least one drum source); the X0X 909 kit (its ride and crash samples: 8-bit, 6-bit or none) and the X0X 808 kit (their voices also on any lane; four style kits each, UIDs 39..46) (EXPERIMENTAL, off by default; see below) |
| Sample sets | PIANO, BASS, VIBES, HORNS, STRINGS, FLUTE, SCRATCH (PERC goes with the sampled kits) |
| FX | DIST, chorus, delay (length; halving when longer than the line), reverb (spring), SLICER (capture), PUNCH (ring), DJ filter, DUST, DUCK, BASS+, mixer glides (X0X 0.10.1, EXPERIMENTAL) |
| MIDI & USB | USB port: CDC console / USB audio (EXPERIMENTAL; its resampler) / MIDI only; TRS MIDI IN; MIDI clock; MIDI expression; USB MIDI flow control, TRS input past line noise (SLOOP 2.3) |
| Sequencer | song sections (16 / 8 / 4), undo history, per-step chance, QNT SEQ, motion recording, performance macros (GLO > MACRO; its ENERGY bands), the REC screen's dials and count-in (SLOOP 2.3) |
| UI | boot logo, parameter icons, VIEW ALL overview (4 x 4 PAGEs; its ARP graph), the MISSING message, knob acceleration, screen SPI clock, changed-rectangle screen updates, keys lit by the notes played, brightness, LIGHTS / KEYS / NOTES, keys read with their column, the knobs' one rest state (SLOOP 2.3) |
| System | OTA updates, backup / restore, idle, main-loop code built for size, asm kernels (SIMD: EXPERIMENTAL), stricter flash read-back, the overload fade, no stuck note after a VOICE change, a restore checked object by object (SLOOP 2.3), predictive CPU guard (off; docs/CPU-GUARD.md) |
| Experimental | dual core |

The X0X kits' UIDs (37, 38) and names are in every build, built or not: a project or kit naming one keeps it,
plays a stand-in, and the MISSING warning names it. That base saves 128 B of flash on user-default (581,424 against
581,552): the kits' names and the drum lanes' source names are one table (drums.c DRUM_SRC_NAMES), and the stand-in
choice is one out-of-line drum_kit_of. With DRUM_X0X909 and DRUM_X0X808 at 0, felucca.bin and data.bin are
byte-identical to that base (compared on user-default and drum-machine) but for one word, FELUCCA_CFG_HASH: the
configuration's hash, which now covers the two new items.

Errors: FM6 without an ENGINE mode; no drum source. Warnings the menu gives: FM6 without its editor and
without SysEx is preset-only; MARK I tables in flash without MARK I do nothing; sample sets without SAMPLE or GRAIN
play nowhere; SAMPLE / GRAIN without their presets' sets get a preset of their own (below); OTA off removes the
update path; experimental items are emulator-tested only.

Every engine a build has keeps at least one entry on the PRESETS list (and in the web editor's preset list), and
every drum source at least one kit. An engine with no factory preset this build can play (none in its table, or
none whose sample set is built) shows one entry, INIT: loading it sets the engine's and the sound's defaults, as a
fresh track on that engine. INIT is made from the defaults: no preset data, no flash in a build that does not need
it. GRAIN without PIANO, VIBES and FLUTE but with another melodic set gets a real preset instead, GRAIN PAD on the
first of them (its sound beats a silent INIT); SAMPLE with no set at all, or GRAIN with no melodic set, plays only
the USR slots: INIT.

### Where an item came from

Items ported from another project carry their provenance (project, author, licence, commit) in the registry;
the menu shows it in the details panel. Items from X0X (`charlesvestal/fm1-x0x`, GPL-3.0-only, by Charles
Vestal) show a NOTICE when selected. What X0X uses only by its author's permission (its break player,
`dsp/breaks*`: no licence) is never offered (`registry.FORBIDDEN`; a `.config` naming it is refused).

Performance macros (MACROS, off by default; after Flowstate, GPL-3.0): COLOR, MOTION, SPACE, ENERGY on GLO > MACRO,
applied in the audio ISR over the authored values (firmware/src/macro.c has the table), kept in four drum-track values
the drum track never reads (no format change), recorded by motion recording. Its option ENERGY thins / thickens the
drum track's steps in five bands. Off: the build is byte-identical to the one without the code; built in and at home:
the goldens and a 4-track mix render bit-identical (tests/macro_test.c). user-default with MACROS: +1,432 B flash
(+1,996 B with ENERGY), +400 B RAM.

Constraints the configuration checks (errors): at least one synth engine, at least one FM6 mode, a drum source.
Motion recording works with every SECTIONS: with 4 its data sits beside the four project slots (as before),
with 8 / 16 each section's record carries it.

### Song sections (SECTIONS)

16 (default) or 8: sections A..P / A..H in banks of 4, stored compressed in one 32 KiB log (the old project
slots' area, firmware/src/sec_log.c), the song layer's title shows the MEM gauge ("mem 34% +12": the share used
and how many more sections of the last size fit). SAVE + OCT- / OCT+ changes the bank; the keys play / store
within it. MEM FULL refuses a new section; room for one record of the longest size is always kept, so the playing
section can always be saved; clearing one always works; an empty section costs nothing. The first start moves the
four old project slots in as A..D (cut anywhere, the next start goes on). 4: the four slots as before.
The reserve is exact: a record never spans two sectors, so a byte count alone can say "room" when sectors each
holding one ~3 KB section have none. sec_log.c models its compaction (where each record is, the sectors' order)
and takes a store that is not the playing section's only when, after it, a record of the longest size would still
go in; the gauge's "+n" counts on the same model. A compaction cut while copying can leave a half-written copy
taking the head's room; the next start then empties whichever sector's live records fit there (not only the
oldest), so the log always gets its spare sector back.
Motion recording (MOTION) with 8 / 16: a section with recorded knob moves carries them in its record, a chunk
after the flags byte (count, the PLAY bits, 3 bytes an event: +2 B +3 B an event, in the gauge like the rest);
none recorded, no chunk. Saved, stored while playing (the arena), written, cut, staged for the song: with its
section, in one record and one CRC. A raw record with motion leaves out the project's magic, size and sum
(rebuilt as it loads), so the longest record, raw with 64 events and a drum record, is 4,059 B (4,076 B with
its head, a 4 KiB sector holds 4,080). Every build reads a chunk (one without MOTION plays the section without
it, and keeps it until the section is saved again) and counts it in its longest record, so no build's log ever
finds another's record too long. The first start of an 8 / 16 build moves each old slot's motion (beside it in
its sector) into its section. With 4 the storage is as it was: the 208 B beside each project and the autosave.
The song chain: 64 parts with 8 or 16 sections (16 with 4). The settings record keeps its layout, with the first
16 parts, so an older build plays those. The whole chain is a record of the log (id 16), saved before the
settings record, which names it by a tag. A save cut between the two keeps the old chain whole. The backup
object is `SNG1`. The log's ids are the same in every build (0..15 sections, 16..23 songs), so a build with
fewer sections keeps the other sections' records: going from 16 to 8 sections and back loses nothing.
Record sizes (tests/sec_codec_test.c): the power-on project 70 B, a typical 16-step section 478 B (8 a 4 KiB
sector), random ones 1.1 KB on average, the dense worst case 3,877 B (raw). The stage's decode, emulator: 42,606
instructions for a 524 B section (~0.18 ms at 240 MHz at one instruction a cycle).
Data stored in the app slot's unused end does not survive an update: the package pads the app to the slot with
0xFF and the loader writes the whole range (tools/fm1pkg_make.py, firmware/loader), so only the data areas
(the log, the USR slots) can hold sections.

### Backup and restore across builds

The editor's backup holds every stored object (cmds 43..48). BK_LIST tells what the device's build holds (engine,
kit and sample-set masks by UID, slot capacities, the section count and record layout); before a restore the
editor shows a report and asks to confirm: objects not in the build or larger than its slot are skipped whole
(never truncated), and each part whose engine, sample set or kit the build leaves out is named ("section C track 2
uses PHYS: plays ANALOG, settings kept": the orphan path keeps its settings). A section's motion travels in its
record (S01..S16, byte for byte); a build without MOTION (BUILD bit 76) is named in the report ("section B has a
motion recording (10 events): this build has none ..."). With 4 sections the motion sits outside the project's
payload, so a PRJ1..PRJ4 / AUTO backup does not hold it (as before this change).

## How a switch works

- `firmware/src/registry.h`: the engines as an X-macro list with their permanent UIDs (FUN7 numbers) and
  fallbacks, and the defaults of every switch. SLOOP is one translation unit: an item left out is a constant
  `0` (`ENG_IS()`, `if (FELUCCA_FX_X)`), so its code, tables, RAM and pool go. Pages and cells of a missing FX
  disappear (`params.c` `page_shown`, `cell_built`).
- `tools/builder/configure.py` turns a `.config` into `build/gen/felucca_config.h` (included first by
  `tools/build.py`) and the generators' environment (sample sets, PERC, SLICE's BREAK).
- Stable IDs: projects, user presets and the drum lanes store engine, kit and sample-set UIDs. A part whose
  engine this build leaves out plays the fallback (FM6 -> DIGITAL -> ANALOG, GRAIN -> SAMPLE, the rest ->
  ANALOG) with its defaults, shows `FM6*` on HOME, and keeps the original engine, preset, EDIT values and FM6
  voice: they are written back on save as long as you leave that part's sound alone. A full build then plays
  it as before (`tests/builder_rt_test.c`). A user preset of a missing engine stays in the bank, not loadable.
  A kit not built plays the other source's first kit; a sample set left out keeps its number (empty).
- Telling the user (`MISSING_WARN`, UI, on; firmware/src/miss.c): after a load (a project, a song section, also
  while playing, a user preset or kit) the main loop scans the working project for what this build lacks and says
  it in the top bar, "MISSING: PHYS T2, KIT 909 +2", once per item until power-off. The audio side only bumps a
  counter (proj_apply). SAVE > TOOLS > MISS counts the items and lists them one by one. Names come from the
  tables already built (ENG_UID_NAME, DRUM_KIT_NAMES, SMP_ALL_NAMES; an FX name only when its switch is off).
  Cost (measured): 1,112 B flash on user-default (1,160 B on the default build), 64 B RAM (from the undo
  ring). `tests/missing_test.c` loads a full build's project, section and user kit on a reduced build and keeps
  the screens (build/host/miss).
- The firmware reports what it is: editor protocol v6, INFO adds each slot's engine UID, `BUILD` (49) the
  configuration's name, hash and one bit per registry item.

## Budget

`tools/builder/measure_costs.py` builds every item at every non-default value (measurement builds link past the
slot) and writes `costs.json`: the default build's sizes and each item's delta. The deltas add up within about
0.5 %; the menu's build gives the exact figure. Re-run it after a merge. An item whose cost depends on another's
value is measured with it too (`PAIRS` in measure_costs.py): `costs.json` "pairs" holds what the two cost together
beyond their own deltas, which the estimate adds when the configuration has both (today MOTION=1 with
SECTIONS=4: the motion beside the four slots instead of in the section records). Measured 2026-10-06: MOTION with
16 sections adds 2,992 B app, 496 B RAM, 1,376 B pool, 112 B RAM code; with 4 sections 3,376 B app, 480 B RAM,
1,776 B pool, no RAM code.

### The profiles (config/profiles/, real links, 2026-10-06, with the SLOOP 2.3 fixes on, the large font from the small one, the cheaper X0X kits, X0X voices on lanes and style kits; optimist ed3a7c6 + feat/x0x-lanes)

| Profile | Left out to fit | App (of 581,564) | RAM (of 98,304) | Pool (of 335,872) | RAM code (of 32,512) |
|---|---|---|---|---|---|
| user-default | LOFI, VOICE, delay 0.74 s, SCRATCH set, FM6's operators in VIEW ALL | 556,832 | 73,828 | 306,860 | 29,308 |
| fm-va-studio | GRAIN, VOICE, LOFI, PHASE, WHEEL, SCRATCH set | 549,584 | 89,060 | 321,680 | 26,696 |
| drum-machine | FM6, DIGITAL, PHASE, VOICE, TRIO, WHEEL, STRINGS set | 548,944 | 85,080 | 331,028 | 23,416 |
| everything-that-fits | SCRATCH and STRINGS sets, PUNCH ring 0.37 s, changed-rectangle LCD strips | 543,100 | 74,052 | 327,340 | 32,436 |
| x0x-drums | drum-machine's, plus: the five sampled kits, PIANO, HORNS and FLUTE sets, delay 0.74 s (for the X0X 909 and 808 kits) | 526,104 | 69,200 | 293,484 | 23,516 |

The estimate (`--budget`) was above the real app size by 208 to 708 B for the first four profiles and by 2.5 KB (0.5 %) for x0x-drums. A sample set
left out can still be uploaded to a USR slot.

## Verification

`tools/builder/verify.py [--random N] [--emu]`: every profile and N random configurations (fitted by the
estimate) build, link and fit; no symbol of an item left out stays in the ELF; `tests/regress.c` built with the
configuration renders every present preset bit-identically to the full build's goldens; with `--emu` the
emulator boots each image and the audio stays silent (rms 0); every engine built has a loadable entry on the PRESETS list
(a factory preset, else INIT) and every drum source a kit (`tests/preset_cover_test.c`, also run by `tests/builder_test.py` on the profiles,
named edge cases and 12 random choices of the sound sources: each is refused by validate() or holds). Host tests: `tests/run_tests.sh` (includes the
full -> reduced -> full project round trip).
