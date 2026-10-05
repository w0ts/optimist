# Melodee vs SLOOP-plus: what each has that the other lacks

Read-only study, 2026-10-05. Uncommitted working note.

- **Melodee**: `~/GitHub/melodee`, keremimo/melodee at `e459da5` (Felucca renamed 2026-10-05; 75 commits, the
  post-Felucca-0.9 work is mostly Kerem Kilic's, 2026-10-03..05).
- **SLOOP-plus**: `~/GitHub/sloop-merged`, `feat/sloop-plus` at `f369f71` (SLOOP 2.2 by isod89 + SUPER, DX7,
  overview, FX bypass).
- Shared base: Felucca 0.9-beta (`e5a908d` + `1e838e1`, QNT OFF/SNAP/WHITE). Everything Melodee did after
  that is a candidate; everything SLOOP did after it is a candidate for upstreaming.

## How flash costs were measured

Melodee was built with the JieLi toolchain (Docker) from `git archive` copies in the scratchpad (the Melodee
repo was not touched), at each feature commit and at its parent, with the same flags
(`USB_AUDIO=0 CDC=0 UART=1`). **Cost = app image size difference** reported by `tools/build.py`
(`ok image N B`). These are Melodee-tree numbers; in our tree the cost will differ by some hundred bytes
(other surrounding code, inlining in the single translation unit). RAM = `.data+.bss` difference;
pool = the `.pool` (NOLOAD, big buffers) difference.

Our budget: `build/felucca.bin` at hand is 574,792 B (built 14:05 today, may be slightly stale against HEAD)
against the 581,564 B slot: **~6.7 KB free**. RAM ~15 KB free (`_bss_end` 0x1c1c520 of 96 KiB).
Pool: `_pool_end` 0x1c6eaf0, so 322,288 of 344,064 B used, **~21.7 KB free**.

Melodee's full default build (FM6 + USB audio + UART) is 491,396 B: it has ~90 KB of headroom because its
samples are smaller (258,554 B ADPCM, 5 sets) — not because its code is smaller.

## 1. Features Melodee has that SLOOP-plus lacks

Legend: cost S (< 1 day), M (1–3 days), L (more). Flash = measured as above unless marked *est.*

| # | Feature | User value | Where in Melodee | Port | Flash (measured) | Conflicts with our work | Recommendation |
|---|---|---|---|---|---|---|---|
| 1 | **USB audio** (UAC1: stereo out + 4 isolated track inputs, 16/24 bit, 44.1 kHz; GLO > SYSTEM device on/off) | DAW recording per track | `usb_audio*.c`, `usb.c`; `21daa15` .. `be2d151` | — | — | — | **in progress** (`feat/usb-audio`) |
| 2 | **MIDI clock / transport in** (USB or TRS; GLO > GLOBAL > CLK INT/USB/TRS; 500 ms dropout stop) | sync to DAW / drum machine | `seq.c:562` (Start), `midi_clock_pulse`, `midi_uart.c:43`; `f9dc3ee` | — | — | — | **in progress** (`feat/midi-clock`) |
| 3 | **FM6** (DX7-exact, Dexed parity, DX7 SysEx, user bank U01–U32, editor .syx import) | — | `eng_fm6.c`, `fm6_store.c`; `ebfe60e`, `d54088a`, `595391c`, `7b7db7f`, `4f29159`, `d6b7acb` | — | ~19 KB (`ebfe60e` alone; later commits add more) | replaces our DX7 | **in progress** (`feat/fm6`) |
| 4 | **TRS MIDI IN on by default + ring read by content** | play the FM-1 from any 5-pin/TRS keyboard | `MELODEE_UART 1` (`melodee.c:46`); ring fix `midi_uart.c:83` (`UM_EMPTY` markers), `hal/fm1_uart.h`; `e69c2f4`, `f087328` | S | not measured separately (small: ~+200 B *est.*) | ours: `FELUCCA_UART 0` "untested" (`felucca.c:40`); our `uart_midi_poll` (`midi_uart.c:60`) still trusts the DMA count, which `f087328` replaced after a hardware fault | **in progress?** Belongs with `feat/midi-clock`: make sure that branch takes `f087328` and turns UART on, else port it here |
| 5 | **MIDI expression**: pitch bend (14-bit, smoothed, RPN 0 range per channel), CC1 vibrato (5 Hz, ±50 c), CC64 sustain, CC120 / 121 / 123, channel aftertouch (FM6) | external keyboards actually feel like a synth; panic | `midi_control.c` (whole file, `midi_control` :159, `midi_event` :237), `voice.c:503` `midi_pitch_tick`, one-line hooks in `eng_*.c`; doc `docs/MIDI-EXPRESSION.txt`; `670193c` (ChanceTheMaker) | M | **+1,984 B**, RAM +336 | ours drops every non-note message: `seq.c:1558` (`if (st != 0x90u && st != 0x80u) continue;`). Touches `voice.c` pitch path (speed / RAM-hot work), `eng_analog.c` (feat/analog2), our DX7/FM6 (FM6's wheel/foot/breath/AT routing **needs** this controller layer — coordinate with `feat/fm6`). Must call our `input_on`/`input_off` (free take, arm, latency-compensated rec), not Melodee's | **port** |
| 6 | **Incoming MIDI through the scale layouts** (WHITE / ALL: note 60 = ROOT, each key one degree; TRN applies) | play SLOOP's scales — and with an adaptation our one-key **chords** — from a MIDI keyboard | `seq.c:202` `midi_map`, `:163` `scale_map`, `:137` `scale_degree_map`; `12ccb56` | S–M | **+288 B**, RAM **+3,072** (`midi_notes[16][128]` u16 + `live_refs`) | ours: `input_on(t, d1, d2)` takes the raw note (`seq.c:1565`), so SCL KEYS/CHORD do nothing for MIDI. Reuse our `midi_sel_on[16][128]` (2 KB) widened, or put the map in pool, to avoid +3 KB RAM | **port** (and extend: CHORD mode for MIDI notes is ours to add) |
| 7 | **QNT ALL** layout (every key, white or black, one scale degree) | denser scale keyboard | `params.c:10` (`N_QUANT` adds ALL, MPC), `seq.c` kb_map; `d294fa0` | S | **+44 B** | our SCL layer KNOB 3 clamps 0..2 (`ui_layers.c:399`), `N_QUANT` 3 entries (`params.c:10`); project/preset value range | **port** (cheap) |
| 8 | **QNT MPC** (MPC Sample bank H pads 20–35 → scale degrees, H02 = ROOT, DEG subpage, other notes filtered) | Akai MPC Sample as a scale pad controller | `seq.c` midi_map, `params.c` DEG, `ui_draw.c`; `21a9a34`, `c891140`, `e6146f3`, `1dabd7d`; editor fix `43ed194` | M | **+872 B** (all four), RAM +32 | adds a parameter (project format, editor P_COUNT layout — Melodee needed `43ed194` for that); niche hardware | **skip** (later if someone has an MPC Sample) |
| 9 | **STEP note length**: hold a note + PRESETS = its length (ties written/cleared, across banks, up to a pattern); MIDI notes in STEP with held length | step entry of long notes/pads without live recording | `seq_edit.c:21..` (`step_note_start`, `step_note_length`), `ui_input.c`; `0dbe626`, `c3a8216` | M | **+856 B** (`0dbe626`) **+708 B** (`c3a8216`) | ours: SEQ layer step-held knobs are SOUND/NOTE, LEVEL, RATCHET (`ui_layers.c` header :8); KNOB 4 is free while a step is held → natural home for LENGTH. Our `step_t` is 10 B (lvl, rat) — tie writing must keep them | **port** (adapted to the SEQ layer, ~1 KB) |
| 10 | **Live notes / chord readout** (HOME: notes as they sound after the scale, chord named incl. extended/altered, kept after release) | see what you play, learn chords; useful with SCL chords | `ui_draw.c:1149` `graph_notes`, `:1109` `chord_of`, `CHORDS` table; `seq.c:43` `live_held`; `187596f`, `a35a3c8` | S–M | **+1,696 B** (with `48fbc75`'s +108), RAM +16 | our HOME is TRACKS / overview (`ui_studio.c`, `ui_overview.c`): needs a place; `live_held` hooks in our input path | **later** (nice, but 1.7 KB) |
| 11 | **Eight patterns per track** (SEQ + white keys pick/copy; switch at each track's loop end; per-pattern LEN/DIV/SWING/GATE) | pattern chaining per track | `core.h:23` `NPAT`, `:244` `pat_bank` in `.pool`, `seq.c:494` `pat_switch`, `project.c`, `storage.c`; `1131934` | L | **+948 B**, pool **+34,220 B**, RAM −4,452 | does **not fit our pool** (21.7 KB free; our step is 10 B, so 4×8×64 steps ≈ 20.5 KB before any buffers). Melodee also moved projects into user sample slots 2–3 (we keep 3 USR slots). Overlaps our song sections A–D, undo, autosave, project format | **skip** (our song mode covers arrangement; revisit only with a much smaller NPAT) |
| 12 | **TR-808 circuit-model drum kit** (bridged-T, pulse shaper, pitch sigh, six-square metal; 8 kit controls) | very authentic 808 | `drums.c` (whole), `DR_EDIT`; `b377202` | L | **+7,012 B**, RAM +1,456 | replaces the drum engine; ours has 37 kits × 16 lanes, levels, ratchets, level-matched. Would have to be a 38th "808 CIRCUIT" kit in parallel — flash alone eats our headroom | **skip** (maybe borrow single ideas, e.g. the BD pitch sigh, into `drum_synth.c`) |
| 13 | **Encoder first-detent fix** (first click after a rest was lost) | every knob/encoder answers on the first click | `hal/fm1_input.h:229` (+`tests/encoder_test.c`); `6ec2deb` | S | **+52 B** | ours has the identical pre-fix code (`hal/fm1_input.h:236`); our file also has FM1_PRESS / dim LEDs (no overlap) | **port** |
| 14 | **USB SysEx sent as CIN 0xF single bytes** (macOS splits long dumps that way; a byte was lost) | reliable large SysEx: sample upload, editor, DX7/FM6 banks, and the OTA path (usb.c is also built into the loader) | `usb.c:561` `usb_midi_rx`; `55a0d62` | S | **+32 B** | ours: `usb.c:456-462` takes only CIN 4..7 as SysEx. FM6 port may bring it — check to avoid double work | **port** (first, tiny) |
| 15 | **MIDI status on GLO > SYSTEM** (USB ON/--/OFF or TRS ON/RX with 250 ms activity) | debug cables / channels | `ui_draw.c` SYSTEM column; `670193c` | S | inside #5 (*est.* ~150 B) | our SYSTEM page has a USB cell (`fc5fa13`) | **port with #4/#5** |
| 16 | **Hold EDIT: key map of pages** (black keys = EDIT pages, white keys = first 8 sounds / FM6 operators) | quick page jumps | `ui_input.c` `nav_page`, `NAV_PAGE`; `4f29159` | M | part of FM6 commit (not separated) | conflicts with our EDIT layer (erase) and our ENV black-key DX7 editor | **skip** |
| 17 | **STUDIO look + EDIT/SEQ/HOME workspaces**, LOOP 4-track view, scope | Melodee's own UI direction | `ui_draw.c` (`draw_graph` 9.5 KB), `ui_input.c`, `tests/ui_preview.c`; `b565819` | L | **+6,232 B** | ours has a different, complete UI (layers, overview, TRACKS) | **skip** |
| 18 | **Record live MIDI on the playing step and follow it** | — | `seq.c` `seq_record_follow`; `48fbc75` | S | +108 B | ours already records latency-compensated to the nearest step | **skip** (ours is better) |
| 19 | **Scale and QNT shared across parts** | — | `ui.c` `scale_setting_set`; `9046296` | S | +276 B | ours shares ROOT and SCALE (`ui_layers.c:252, :394-396`), QNT stays per track | **skip** (equivalent; per-track KEYS is deliberate) |
| 20 | **AUDIO_STATS** editor cmd 33, `tools/usb_audio_stats.py` | USB audio diagnosis | `web/EDITOR_PROTOCOL.md` "Commands" | — | — | — | **in progress** (with #1) |
| 21 | Tests: `encoder_test.c`, `midi_expression_test.c`, `midi_scale_test.c`, `midi_clock_test.c`, `seq_edit_test.c`, `usb_audio_*`, `fm6_parity.*`; `target_budget` entries for `midi_event`, `midi_pitch_tick` | regression safety | `tests/` | S each | 0 | — | **port with each feature** |
| 22 | Build flags renamed `MELODEE_*`; `MELODEE_USB_AUDIO` default 1, `MELODEE_UART` default 1, CDC off when USB audio | — | `melodee.c:17-60`, `tools/build.py:174` | — | — | ours `FELUCCA_*` + `FELUCCA_ARRANGER`, `FELUCCA_DX7_ROM`, and the branch flags (`FELUCCA_ASM`, `_DUAL`, `_IDLE`, `_ANALOG2`, `_SIMD`) | keep `FELUCCA_*`; map names when porting (`MELODEE_X` → `FELUCCA_X`) |

Not found in Melodee (checked, so not gaps): program change, MIDI clock **out**, CC learn/CC parameter
map, per-note expression/MPE — neither firmware has them. SysEx editor protocol: Melodee's is our v4
plus cmd 33 (USB audio); nothing else to take. Web editor: same tabs; Melodee's extras are FM6 / .syx
(in progress), the MPC DEG field and pattern display.

## 2. What SLOOP-plus has that Melodee lacks (for upstreaming later)

Checked by grepping Melodee's `firmware/src` (no undo, autosave, punch FX, tap tempo, solo, ratchet,
DUST/DUCK, free take, FDN reverb, recovery; loader without the package-CRC/JEDEC check; installer without
SHA-256).

- Layers (hold a button: keys and knobs change job) with lock, landmark LEDs, dim LEDs (`ui_layers.c`, `hal/fm1_input.h` FM1_PRESS / `fm1_led_dim`).
- Drum track with 16 lanes, 4 levels and ratchets per hit; 37 kits (5 sampled, 32 synthesised), level-matched (`drum_synth.c`, `drums.c`, `dstep_t`).
- Live recording latency-compensated, free take (loop length and tempo from the playing), overdub at once, hold-REC clear.
- Undo / redo, autosave of the working project, NEW project.
- Note repeat / rolls, erase-as-it-plays, Elektron-style step layer, one-key chords in the song's key.
- Mute / solo / tap tempo / per-track FX bypass; click (metronome) OFF/REC/ON; MPC swing 50–75 %; sample-accurate clock.
- 16 punch-in FX (`punch.c`); master DUST, DUCK (sidechain), DJ filter; stereo chorus; FDN reverb.
- Song mode A–D live + song recording, arranger (`arranger*.c`, `ui_song.c`).
- SUPER engine; DX7 with OP7/OP8 (being replaced by FM6); overview pages (VIEW ALL).
- 86 presets browsed by kind, level-matched; Steinway grand samples; 3 user sample slots + CHOP in the editor.
- Safer updates: package SHA-256 in the installer, loader CRC + flash-chip check (`loader/ldr_core.c`), USB rescue, `recovery.c`, `bootguard.h`.
- Tests Melodee lacks: `soak_test`, `punch_test`, `song_*`, `ui_pages_test` (host screen renders), `studio_drums_test`, `drumkit_test`, preset/drum level tools.
- Work in branches: asm hot loops, dual core, idle wait, silent-bus skip, `.ram_hot` (see HANDOFF-fm1.md).

## 3. Top 10 to port (ranked)

| Rank | Item | Why | Flash |
|---|---|---|---|
| 1 | #14 USB SysEx CIN 0xF fix | real data-loss bug on macOS for uploads/editor/OTA; 32 B | +32 B |
| 2 | #13 encoder first-detent fix | every knob loses its first click today; 52 B | +52 B |
| 3 | #4 TRS ring-by-content fix + UART on | hardware-verified fix; prerequisite for TRS clock (confirm `feat/midi-clock` has it) | ~+0.2 KB *est.* |
| 4 | #5 MIDI expression (bend, mod, sustain, panic, RPN) | biggest musical gap for external keyboards; FM6 controllers depend on it | +2.0 KB |
| 5 | #6 MIDI through scale layouts (+ our CHORD for MIDI) | makes SLOOP's key/chords work from a keyboard | +0.3 KB (+RAM, put in pool) |
| 6 | #7 QNT ALL | almost free, new playing layout | +44 B |
| 7 | #15 MIDI/TRS status on GLO > SYSTEM | needed to debug TRS on hardware | ~0.15 KB *est.* |
| 8 | #9 step note length (SEQ layer, KNOB 4 with a step held) | missing editing tool for pads/long notes | ~1.0–1.5 KB |
| 9 | #21 the matching host tests (encoder, expression, MIDI scale) | regression safety for 1–8 | 0 |
| 10 | #10 live notes / chord readout | nice with chords; only if flash remains | +1.7 KB |

Sum of ranks 1–8 ≈ 3.7–4.3 KB: fits the ~6.7 KB free, but **not together with FM6 (~19 KB+) unless the FM6
port frees the DX7 space it replaces** — the flash plan of `feat/fm6` decides how much of this list fits.
Skipped on purpose: 8 patterns (pool), 808 circuit kit (7 KB), STUDIO UI (6 KB), MPC mode, EDIT page map.

## Unverified / caveats

- Flash numbers are measured in Melodee's tree, not ours; expect ± a few hundred bytes after porting.
- #4's own cost and #15 are estimates (not built separately).
- Our free flash uses a `build/felucca.bin` that may predate the last commits on `feat/sloop-plus`.
- Nothing here was tested on hardware; Melodee's own docs say TRS end-to-end timing is unverified.
