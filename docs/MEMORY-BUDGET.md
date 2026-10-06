# Memory budget of the integrated firmware (flash, RAM, pool)

Measured 2026-10-05 on `feat/integration` at `c8608b1` (all merges in: ANALOG 2, FM6, melodee-ports,
speed-top3, idle-wait, midi-clock, usb-audio, dual-core, exp/simd, analog2-asm; plus the splash switch and the
user-preset migration). Every number is from a real build of this tree (`ld` region report, `build.py`'s summary,
the ELF section and symbol tables) unless marked:

| Tag | Meaning |
|---|---|
| [M] | measured here: two builds of this tree, the difference |
| [D] | exact data size (generated headers, ELF symbol of a table) |
| [S] | sum of ELF symbol sizes by name group: misses inlined code, string literals, padding (±~10 %) |
| [B] | from `docs/BUILDER-DESIGN.md`, measured on `064a40c` (ANALOG 2 + FM6 + melodee-ports), not re-measured |

**The user chooses.** Nothing was trimmed in the default build. Two documented build options were added only so
that a build fits for verification: `FELUCCA_SAMPLES_SKIP` (leave named sample sets out) and `FELUCCA_DLY_LEN`
(the delay line length). The verification config used for every test of the integration is
`FELUCCA_SAMPLES_SKIP=SCRCH FELUCCA_DLY_LEN=32768` (below: "fitting config").

## 1. Where the default build stands

| Region | Size | Default build (everything) | Fitting config | Notes |
|---|---|---|---|---|
| Flash app slot (XIP region) | 581,564 B | **585,376 B: 3,812 B over** (`ld`: XIP overflowed by 3,812) | 562,376 B (19,188 free) | the image = `.text` + the load images of `.ram_text`, `.ram_hot`, `.data` |
| Main RAM `.data+.bss` | 98,304 B | 91,312 B (6,992 free) | 91,312 B | `.ram_hot2` is empty (section 4) |
| Pool (`.pool`, NOLOAD) | 344,064 B | **347,320 B: 3,256 B over**; build.py also wants ≥ 8,192 B spare: **11,448 B short** | 281,784 B (62,280 free) | USB audio's two rings (12,288) on top of FM6's voices (11,712) did it |
| RAMTEXT (`.ram_text` + `.ram_hot`) | 32,512 B | 2,964 + 28,600 = 31,564 B (948 free) | same | the audio path in RAM (speed-top3) |
| NOINIT | 15,696 B | 14,748 B (948 free) | same | 4 project slots (14,544) + boot / crash records |

The 8 KiB pool headroom rule is `tools/build.py` (`keep >= 8 KiB of the pool spare`, since SLOOP 2.0 `a3f9fd0`); its
reason is not documented in the tree.

### Sections of the fitting build (ELF, `c8608b1`)

| Section | Bytes | What |
|---|---|---|
| `.text` (XIP) | 525,168 | code and constants; samples 292,992 of it (SCRCH left out; 315,854 with all sets) |
| `.ram_text` | 2,964 | flash driver, CPU1 idle loop (RAM, copied at boot; also counts in flash) |
| `.ram_hot` | 28,600 | the audio path in RAM (also counts in flash) |
| `.data` | 5,644 | initialised RAM (also in flash): RAM tables (SINE 2,048, TANH 514, GR_HANN 514, FM6_OPL_EXP 512), drums state 1,820 |
| `.bss` | 85,664 | zeroed RAM |
| `.pool` | 281,784 | big zeroed buffers (dly_buf here 65,536 B; 131,072 B by default) |
| `.noinit` | 14,748 | survives resets |

## 2. Per feature, engine, table (fitting build)

### 2.1 By group, from the symbol table [S]

"flash" counts `.text` + `.ram_text` + `.ram_hot` + `.data`; "(RAMTEXT)" is the part of it that also runs from RAMTEXT;
RAM is `.data` + `.bss`.

| Group | flash | (RAMTEXT) | RAM | pool | noinit |
|---|---|---|---|---|---|
| Samples, built-in (7 sets here, ADPCM + zone table) | 294,792 | | | | |
| Fonts S + L | 47,272 | | | | |
| UI (pages, layers, overview, draw) | 47,276 | | 2,871 | | 32 |
| FM6 (engine, user bank, black-key editor, ROM, tables) | 26,848 | 0 | 18,915 | 11,712 | |
| Drums (32 synth kits `DS_KITS` 11,648 [D], synth, lanes, mix) | 19,276 | 3,050 | 2,015 | | |
| Sequencer, input, recording | 13,848 | | 1,460 | | |
| Voice core, shared tables (PITCH_INC 8,192 …) | 12,736 | | 9,238 | | |
| Boot, HAL, libc, console | 10,450 | 76 | 4,305 | | 140 |
| FX buses, master, mix (`mix_block` 9,012 in RAMTEXT) | 9,224 | 9,110 | 18,525 | 71,626 | |
| Editor SysEx (protocol v5) | 8,481 | | 3,015 | | |
| OTA, recovery, flash storage | 7,226 | 2,886 | 6,773 | | 12 |
| ANALOG 2 (incl. swarm and asm kernels) | 6,022 | 4,454 | 1 | | |
| Projects, user presets, autosave | 5,932 | | 13,148 | 7,272 | 14,564 |
| GRAIN | 5,116 | | 530 | 21,060 | |
| Icons | 4,468 | | | | |
| USB (MIDI, SIE, descriptors) | 3,869 | | 833 | | |
| TRIO | 3,492 | 2,740 | | | |
| FORMANT (VOICE) | 2,919 | 2,326 | 4 | | |
| MIDI in, expression, clock | 2,766 | 160 | 1,062 | | |
| DIGITAL | 2,100 | 1,374 | | | |
| WHEEL (drawbar) | 2,046 | 1,434 | 2,496 | | |
| LOFI | 1,866 | 934 | | | |
| Display (canvas, LCD) | 1,670 | | 560 | 59,520 | |
| PHASE | 1,516 | 1,094 | | | |
| SAMPLE engine | 1,478 | 750 | 1,796 | | |
| USB audio | 1,278 | | 1,891 | 12,288 | |
| SLICER | 1,008 | 970 | 128 | 32,768 | |
| Song / arranger | 876 | | 112 | | |
| Punch-in FX (the ring is the pool) | 368 | 204 | 112 | 65,536 | |
| other (tables of parameters, small helpers) | 9,213 | | 1,400 | | |

Largest single items: `SMP_DATA` 292,992 (here) · `dly_buf` 65,536 (pool; 131,072 by default) · `punch_ring` 65,536 (pool) ·
`cv_px` 59,520 (pool, the screen canvas) · `sl_buf` 32,768 (pool) · `ui_draw` 25,902 · `FONT_L_DATA` 24,576 ·
`FONT_S_DATA` 21,504 · `gr_p` 21,060 (pool) · `rev_line` 17,368 (RAM) · `proj_slot` 14,544 (noinit) · `DS_KITS` 11,648 ·
`mix_block` 9,012 (RAMTEXT) · `fm6_v` 8,640 (pool) · `events_block` 8,230 · `PITCH_INC` 8,192 · `ua_cap` 8,192 (pool).

### 2.2 Switches, each alone on the fitting config [M]

Base: 562,376 B flash, RAM 91,312, pool 281,784, RAMTEXT 31,564.

| Build option | flash | RAM | pool | RAMTEXT | Loses / gains |
|---|---|---|---|---|---|
| `FELUCCA_SPLASH=1` (the SLOOP bitmap until 2026-10-06; now the drawn Optimist logo, +384 on user-default, default on) | +5,168 | 0 | 0 | 0 | the boot logo |
| `FELUCCA_ICONS=0` | −5,616 | 0 | 0 | 0 | the parameter icons (labels stay) |
| `FELUCCA_FM6_KEYS=0` (the user keeps it) | −5,392 | −416 | 0 | +64 | FM6's operator editor on the black keys |
| `FELUCCA_USB_AUDIO=0` (the CDC console comes back) | +1,548 | +848 | −12,288 | −40 | USB audio |
| `FELUCCA_USB_AUDIO=0 FELUCCA_CDC=0` | −2,544 | −1,952 | −12,288 | −48 | USB audio and the console |
| `FELUCCA_UART=0` | −48 | −160 | 0 | +64 | TRS MIDI IN (notes, the TRS clock) |
| `FELUCCA_ASM=0` | −572 | 0 | 0 | −620 | the asm kernels (FM6, ANALOG 2): slower, same sound |
| `FELUCCA_OTA=0` | −12,772 | −5,952 | 0 | +92 | updates from the editor (not an option: it is the update path) |
| `FELUCCA_DUAL=2` (EXPERIMENTAL) | +1,844 | +1,856 | +6,144 | +568 | CPU1 renders parts 2–3 |
| `FELUCCA_SIMD=1` (EXPERIMENTAL) | −96 | +4,096 | 0 | −128 | packed sine / swarm |
| `FELUCCA_IDLE=0` | 0 | 0 | 0 | 0 | — |
| `FELUCCA_DLY_LEN=32768` (vs the default 65,536) | −8 | 0 | **−65,536** | 0 | delay line 1.49 s → 0.74 s: a 1/4-note delay needs ≥ 81 BPM |
| sample set SCRCH (`FELUCCA_SAMPLES_SKIP=SCRCH`) | −22,992 | 0 | 0 | 0 | SCRATCH preset |
| sample set STRGS | −19,984 | 0 | 0 | 0 | STRING STB preset |

The deltas add up within ~0.5 % ([B]: five items removed together came within 84 B of the sum).

### 2.3 Engines, FX and features measured on `064a40c` [B]

Removal savings: DIGITAL 2,240 · PHASE 1,616 · LOFI 2,332 · SAMPLE engine 1,776 · FORMANT 2,864 · TRIO 3,632 ·
WHEEL 2,480 (+2,496 RAM) · GRAIN 5,116 (+21,060 pool) · FM6 all 29,832 (+12,544 RAM, +11,712 pool) · ANALOG 2 ~5.2 KB [S]
(no ablation: projects import SUPER through it) · DIST 368 · the three send buses 2,324 (RAM 17,680, pool 137,164 at the
full delay) · SLICER 1,376 (pool 32,768) · DUST 624 · PUNCH 2,200 (pool 65,536) · DJ filter 1,168 · DUCK 140 ·
CDC console 4,696 · overview pages (VIEW ALL) 2,364 · editor SysEx ~8.9 KB [S] · arranger ~1.8 KB [S]
(`FELUCCA_ARRANGER=0` does **not** compile: a dead flag, `ui_layers.c` uses `live_req`, `srec`).

### 2.4 Sample sets [D] (IMA ADPCM, 22.05 kHz; `SMP_ZONES` 1,344 B, `SMP_PRESET_TABLE` 360 B)

| Set | Bytes | Presets that use it |
|---|---|---|
| PIANO (Steinway, 4 zones) | 44,104 | GRAND PNO, DUSTY PNO, LOFI KEYS; GRAIN |
| BASS | 39,138 | UP BASS, DEEP BASS |
| VIBES | 33,078 | VIBES; GRAIN VIBE HAZE |
| FLUTE | 31,422 | LOFI FLUTE (track 3 at power-on); GRAIN FLUTE DUST |
| HORNS | 26,462 | HORN STAB |
| SCRCH | 22,862 | SCRATCH |
| STRGS | 19,844 | STRING STB |
| PERC (GM kit, 27 zones) | 98,944 | the 5 sampled drum kits, GM KIT (cannot be left out by the option) |
| total | 315,854 | 54 % of the image |

USR1..3 (3 × 80 KiB at 0xA0000, outside the app slot) can hold a set as user data: a set left out of the build can still
be uploaded there (except PERC, larger than a slot).

### 2.5 What each merge cost (flash, measured at the merge, in the config of that step)

| Merge / change | flash | Notes |
|---|---|---|
| analog2 + fm6 | 580,580 B total (default config, fitted then) | FUN7 project format |
| melodee-ports (+ FM6 controller hooks) | +4,584 | the default build stopped fitting here (3,600 over) |
| speed-top3 (with the placement below) | +1,656 | |
| idle-wait | 0 | |
| midi-clock | +5,360 | TRS on by default |
| splash switch (default off) | −5,072 | |
| user-preset migration | +192 | |
| usb-audio | −1,716 | the CDC console goes; pool +12,288 |
| dual-core (off) | +16 | |
| exp/simd (off) | 0 | |
| analog2-asm | +620 | |

## 3. The overage and the options, ranked

Flash: **3,812 B** over. Pool: **3,256 B** over (**11,448 B** with the 8 KiB rule). RAM and RAMTEXT fit.

### 3.1 Flash, least loss first

| # | Option | Saves | Cost to the user | Builder switch? |
|---|---|---|---|---|
| 1 | Parameter icons off (`FELUCCA_ICONS=0`) | 5,616 [M] | icons on the pages; labels stay | yes (exists) |
| 2 | Large font drawn from the small one at 2× (`FONT_L` 24,576 [D]) | up to ~24 KB | none if FONT_L is exactly FONT_S doubled — **not verified** | no (code work) |
| 3 | Size-optimise cold code (UI `ui_draw` 25,902, `ui_input` 10,272, `events_block` 8,230: clang `minsize` on non-audio functions) | unmeasured | slower UI code (not audio) | could be a flag |
| 4 | One sample set out or moved to a USR slot: STRGS 19,984 [M] · SCRCH 22,992 [M] · HORNS ~26.6 KB · FLUTE ~31.6 KB (track 3's default sound) | 20–44 KB | its presets (or a manual upload into USR) | yes (`FELUCCA_SAMPLES_SKIP`; needs stable set IDs, BUILDER-DESIGN §2) |
| 5 | Fewer synthesised drum kits | 364 B a kit [D] | those kits | yes (`gen_drumkits.py` list) |
| 6 | Overview pages (VIEW ALL) | ~2,364 [B] | GLO > SYSTEM VIEW ALL | needs a flag |
| 7 | ANALOG 2's still-cutoff filter kernels (`a2_lp/bp/hp/lp2`, the moving `_i` ones always run) | 616 [D] (+ RAMTEXT) | a little CPU while the cutoff is still | needs a flag |
| 8 | Less inlining in the mix (`mix_block` 9,012 inlines the parts, DIST, the buses) | unmeasured | CPU in the audio ISR | no |
| 9 | USB audio and CDC off (`FELUCCA_USB_AUDIO=0 FELUCCA_CDC=0`) | 2,544 [M] (and the pool problem) | USB audio | yes (exists) |
| 10 | FM6 black-key editor off (`FELUCCA_FM6_KEYS=0`) | 5,392 [M] | the user wants to keep it | yes (exists) |
| — | OTA off | 12,772 [M] | the update path: **no** | — |

### 3.2 Pool

| # | Option | Saves | Cost | Builder switch? |
|---|---|---|---|---|
| 1 | Delay line 0.74 s (`FELUCCA_DLY_LEN=32768`) | 65,536 [M] | a 1/4-note delay only from 81 BPM up (1/8 from 41 BPM) | yes (exists) |
| 2 | USB audio off | 12,288 [M] | USB audio | yes (exists) |
| 3 | Punch-in ring half (`PUNCH_N` 32,768 → 16,384 samples) | 32,768 [D] | shorter punch loops (not checked which loops at slow tempi) | needs a flag |
| 4 | SLICER capture half (`SL_LEN` 4,096 → 2,048 a track) | 16,384 [D] | 93 ms of capture instead of 186 | needs a flag |
| 5 | GRAIN off | 21,060 [B] (+5,116 flash) | the GRAIN engine | yes (engine switch) |
| 6 | Relax the 8 KiB headroom rule | 0 (3,256 still over) | its reason is unknown | — |
| 7 | USB audio rings into main RAM (6,992 free) | 12,288 | main RAM down to ~-5 KB: does **not** fit | — |

### 3.3 Combinations measured [M]

| Config | Image | RAM | Pool | Fits | Loses |
|---|---|---|---|---|---|
| default | 585,376 | 91,312 | 347,320 | no (flash −3,812, pool −3,256) | — |
| `FELUCCA_ICONS=0 FELUCCA_DLY_LEN=32768` | 579,752 (1,812 free) | 91,312 | 281,784 | **yes** | icons; delay ≤ 0.74 s |
| `FELUCCA_ICONS=0 FELUCCA_USB_AUDIO=0 FELUCCA_CDC=0` | 577,428 (4,136 free) | 89,360 | 335,032 (9,032 spare) | **yes** | icons; USB audio |
| `FELUCCA_SAMPLES_SKIP=STRGS FELUCCA_DLY_LEN=32768` | 565,384 | 91,312 | 281,784 | yes | STRING STB; delay ≤ 0.74 s |
| `FELUCCA_SAMPLES_SKIP=SCRCH FELUCCA_DLY_LEN=32768` (the verification config) | 562,376 | 91,312 | 281,784 | yes | SCRATCH; delay ≤ 0.74 s |

## 4. What runs from RAM (re-decided at the speed-top3 merge)

speed-top3 put the mix and nine renders in RAMTEXT (`.ram_hot`) and FORMANT + GRAIN in main RAM (`.ram_hot2`, 6,428 B).
After the merge FM6's boot-built tables took ~11.8 KB of main RAM (FM6_SIN 4,100, FM6_MKI_LOG 4,096, FM6_MKI_EXP 2,048,
FM6_OPL_LOG 1,024, FM6_OPL_EXP 512) and main RAM was down to 736 B free. Decided:

| Code | Where | Bytes |
|---|---|---|
| mix (`mix_block` 9,012, `drums_mix` 3,050, buses, DIST, SLICER, master), `midi_pitch_tick` | RAMTEXT | ~14.3 KB [S] |
| ANALOG 2 (render, oscillators, filters, swarm, asm kernels) | RAMTEXT (new) | 4,454 [S] |
| FORMANT | RAMTEXT (was `.ram_hot2`) | 2,326 [S] |
| DIGITAL, TRIO, PHASE, WHEEL, SAMPLE, LOFI | RAMTEXT (as speed-top3) | 8,326 [S] |
| GRAIN | XIP (was `.ram_hot2`) | 5,116 |
| FM6 (`fm6_render` 3,326, `fm6_control` 2,604, `fm6_block` 1,284, …) | XIP | ~7.7 KB [S] |
| `.ram_hot2` | empty | 0 (main RAM 6,992 free instead of 736) |

Not measurable in the emulator (it models no cache and no flash wait states). Options for the user / hardware tests:
FM6 into RAMTEXT needs ~7.7 KB there (948 free): move TRIO, DIGITAL, PHASE, LOFI and WHEEL (7,576 [S]) back to XIP,
or FM6 into `.ram_hot2` (main RAM, 6,992 free: it would leave ~-0.7 KB, so it needs RAM back first: `proj_v3_tmp` 2,584
is only used to convert FUN1..3 projects and could share `proj_tmp`'s 3,636; `fm6_rx` 4,104 could share `st_buf` 3,840 —
both unmeasured). `FELUCCA_ASM_CHECK` builds keep everything in XIP (the double kernels do not fit RAMTEXT).

## 5. Natural switches for the builder

Already switches: `FELUCCA_ICONS`, `FELUCCA_SPLASH`, `FELUCCA_FM6_KEYS`, `FELUCCA_USB_AUDIO` / `FELUCCA_CDC`, `FELUCCA_UART`,
`FELUCCA_ASM`, `FELUCCA_DUAL`, `FELUCCA_SIMD`, `FELUCCA_IDLE`, `FELUCCA_DLY_LEN`, `FELUCCA_SAMPLES_SKIP`, `FELUCCA_SLICE`
(off), `FELUCCA_ANALOG2` (1; 0 = the old ANALOG + SUPER).
Natural next: engines (leave out of `ENGINES[]`, fold `== &ENG_X`: [B] shows the code, RAM and pool go with them),
drum kits (the generator's list), sample sets (needs stable set IDs so USR and projects do not shift), `PUNCH_N`,
`SL_LEN`, the overview pages, FX bus split (one loop today). Which code is HOT should follow the engine choice (RAMTEXT
is shared). Not a switch: OTA (the update path), the arranger (dead flag), fonts.
