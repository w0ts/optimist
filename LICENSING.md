# Felucca licensing

Felucca is free software. Its **code** is licensed under the GNU General Public License,
version 3 only (`GPL-3.0-only`, full text in `LICENSE`). Optimist builds on SLOOP, which was built on
Felucca 0.9-beta, and it took Felucca's licence notes from there. Felucca 1.0 changed what they say about
Felucca's assets. This section says which terms apply to which asset in this tree (updated 2026-10-06,
from `hugelton/Felucca` at the tags `v0.9-beta` e5a908d, `v1.0` 727f272 and `v1.0.3` b22a24b):

- **The Hügelton Sample Pack** (the drum sounds made by `tools/gen_waves.py`): **GPL-3.0-only.** Felucca
  0.8-beta and 0.9-beta kept it out of the GPL ("Copyright (C) 2026 Hügelton Instruments, all rights
  reserved. Their licence terms will be published later."). Since Felucca 1.0 (727f272, 2026-10-05) its
  `LICENSING.md` says that the GPL "covers the code and its own assets", and lists under "Hügelton
  Instruments' own work":

  > \| The Hügelton Sample Pack: Felucca's drum sounds, made by `tools/gen_waves.py` (not CC0) \| GPL-3.0-only \| `tools/gen_waves.py` \|

  and since 1.0.2 (db70550; the same in 1.0.3, b22a24b, `LICENSING.md` line 37):

  > \| The Hügelton Sample Pack: Felucca's drum sounds, made by `tools/gen_waves.py`, from which the SLICE engine's BREAK is built (not CC0) \| GPL-3.0-only \| `tools/gen_waves.py` \|

  Our `tools/gen_waves.py` is the same generator (it differs from 1.0.3's in three comment lines only).
  In Optimist it makes the SLICE engine's built-in BREAK (`tools/gen_samples.py`; the builder's
  `ENG_SLICE`, on in the drum-machine and x0x-drums profiles). The sampled drum kit (PERC) takes all of its
  16 sounds from the CC0 recordings in `assets/samples-cc0/KIT`. The pack would fill in only for a sound
  that kit lacked, and it lacks none.
- **The panel picture** `docs/panel.jpg`: GPL-3.0-only since Felucca 1.0 (replaced by `docs/controls.jpg`,
  also GPL-3.0-only, in 1.0.3). This tree holds neither file.
- **The parameter icons** `assets/icons.png` (used by the `ICONS` build switch, on in every profile;
  `tools/gen_icons.py` turns it into `ICON_DATA`): **GPL-3.0-only, Optimist's own.** They are drawn by
  `tools/draw_icons.py` from the drawings in `tools/icon_drawings.py` (Copyright (C) 2026 the Optimist
  contributors), which are their source; the PNG is their output. History: on 2026-10-06 they replaced the
  atlas taken from Felucca 0.9-beta.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments\
USB audio (from Melodee): Copyright (C) 2026 Kerem Kilic (Ellic Studio)

## What is code (GPL-3.0-only)

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the web pages (installer, editor) and their tests: `web/` (not the Fukiai font, below)
- the host tests: `tests/`
- the parameter icons: `assets/icons.png`, drawn by `tools/draw_icons.py` (`tools/icon_drawings.py`)

You may use, study, change and share it under the GPL. If you distribute Felucca, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified Felucca
inside.

## Additional permission (GPL-3.0 section 7)

Felucca 0.9-beta's grant, kept as it was given. No Felucca Asset outside the GPL is left in this tree (the
last one, the icon atlas, was replaced on 2026-10-06, above); the grant matters only to a work that adds one.

As an additional permission under GPL-3.0 section 7, you may combine Felucca, or a work
based on it, with the Felucca Assets (above), and convey the combination.
This is allowed even though the Felucca Assets are not licensed under the GPL, provided
that:

- you follow the GPL for every part that is not a Felucca Asset; and
- you follow the terms published for the assets.

The Felucca Assets are data (wavetables, icons, sample data). They are not program
code. A firmware image built from the GPL sources with replacement assets, or with no
assets, is entirely governed by the GPL.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Instrument and drum samples (Versilian Studios VSCO-2 CE, VCSL; Sonic Pi: SCRCH) | CC0 1.0 | `assets/samples-cc0/`, provenance in `ATTRIBUTION.txt` there |
| Hip-hop pack E.PIANO (Wurlitzer EP200 samples by Greg Sullivan, sfz mapping by kinwie) | CC BY 3.0: credit Greg Sullivan when sharing the samples | `assets/hiphop-pack/` (user-slot pack, not in the firmware image), `CREDITS.txt` there; the pack's other sounds are CC0 1.0 |
| Terminus font 8x16 (ter-u16n) | SIL OFL 1.1 | `assets/fonts/ter-u16n.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| Fukiai icon font (Hügelton Instruments), web editor only | MIT | `web/fukiai.ttf`, `web/FUKIAI-LICENSE.txt` |
| CrispyZebra by Leo Kuroshita (<https://github.com/hugelton/CrispyZebra>): the PHASE engine's waveforms are a C port of its oscillator | GPL-3.0 | `firmware/src/eng_phase.c` |
| klattsch by Tony Gies (<https://github.com/tgies/klattsch>): design reference for the VOICE (formant) engine; no code copied. Formant data from Klatt (1980) / Hillenbrand et al. (1995) | MIT (klattsch) | credit only |
| FM6 engine by Kerem Kilic (Melodee, <https://github.com/keremimo/melodee>, `firmware/src/eng_fm6.c`, `eng_fm6_rom.h`, `fm6_store.c`, its tests and web editor support, at e459da5): ported into SLOOP, where it replaces the DX7 engine; its factory voices are Melodee's own | GPL-3.0-only | `firmware/src/eng_fm6.c`, `firmware/src/eng_fm6_rom.h`, `firmware/src/fm6_store.c`, `tests/fm6_*`, `tests/dexed_ref.cc`, `web/editor.html` |
| MSFA (Music Synthesizer for Android, Copyright 2012 Google Inc., <https://github.com/google/music-synthesizer-for-android>) and Dexed (Copyright 2013-2025 Pascal Gauthier, <https://github.com/asb2m10/dexed>; portamento rates by Jean Pierre Cimalando), as Melodee credits them: the FM6 engine restates their synthesis in fixed-point C so that it renders the samples Dexed renders (MSFA's envelopes, pitch envelope, LFO, operator kernels and Dx7Note, Apache-2.0; Dexed's MARK I and OPL engines and its voice handling, GPL-3.0-or-later); their DX7 measurement tables and the tables Dexed computes at start (sine, 2^x, frequency, log-sine / exponent, OPL ROM, detune, LFO, portamento) are used as data. `tests/dexed_ref.cc` builds Dexed's own sources (from a checkout, not in this tree) to compare against | Apache-2.0, GPL-3.0-or-later | `firmware/src/eng_fm6.c`, `tools/gen_tables.py`, `tests/dexed_ref.cc` |
| Melodee (keremimo/melodee, the Felucca line continued by Kerem Kilic, <https://github.com/keremimo/melodee>): ported fixes and features, each marked in the source — encoder first-click fix (`6ec2deb`, Kerem Kilic); MIDI pitch bend, mod wheel, sustain, panic and the GLO > SYSTEM MIDI status (`670193c`, ChanceTheMaker); MIDI notes through the scale layouts (`12ccb56`, Kerem Kilic); the QNT ALL layout (`d294fa0`, Kerem Kilic); step note length (`0dbe626`, Kerem Kilic) | GPL-3.0-only | `firmware/hal/fm1_input.h`, `tests/encoder_test.c`; `firmware/src/midi_control.c`, `midi_pitch_tick` in `voice.c`, `tests/midi_expression_test.c`; `midi_map` / `scale_map` in `seq.c`, `tests/midi_scale_test.c`; `step_note_resize` in `ui_layers.c` |
| Melodee by Kerem Kilic (Ellic Studio) (<https://github.com/keremimo/melodee>, e459da5), a fork of Felucca: TRS MIDI IN reading the ring by content (f087328), clock and transport queued with the notes, the INT / USB / TRS clock source and the lost-clock stop | GPL-3.0-only | `firmware/src/midi_uart.c` (its header), `firmware/src/usb.c` (`midi_in_enqueue`), `firmware/src/clock_sync.c`, `tests/midi_uart_test.c` (`test_uart_ring`) |
| Melodee by Kerem Kilic (Ellic Studio) (<https://github.com/keremimo/melodee>, a Felucca fork): the USB audio (UAC1 stream core, endpoint service, descriptors, its host tests) and the CIN 0xF SysEx fix, ported | GPL-3.0-only | `firmware/src/usb_audio*.c`, `firmware/src/usb_audio_desc.h`, `firmware/src/usb.c`, `tests/usb_audio*` |
| Felucca 1.0.1 by Leo Kuroshita (Hügelton Instruments) (<https://github.com/hugelton/Felucca>, 20c275e): the size-optimised build of the main-loop code (`tools/size_fns.py` and its step in `tools/build.py`), adapted; the return to the official V15 from the installer page (`validateStockPackage` in `web/fm1pkg.js`, the page's panel) | GPL-3.0-only | `tools/size_fns.py`, `tools/build.py`, `web/fm1pkg.js`, `web/index_pkg.html` |
| FM-1-transporter by kurogedelic (<https://github.com/kurogedelic/FM-1-transporter>, docs/PROTOCOL.md): the UBOOT protocol and write policy `fm1_rescue.py` follows (through X0X) | MIT | `tools/fm1_rescue.py` |
| jl-uboot-tool by kagaimiq (<https://github.com/kagaimiq/jl-uboot-tool>): `wl82loader.bin`, downloaded by `fm1_rescue.py` at run time and checked by its SHA-256; not in this tree | MIT (as reported in the FM-1 scene survey; not re-checked here) | (downloaded) |
| X0X by Charles Vestal (fm1-x0x, <https://github.com/charlesvestal/fm1-x0x>): ported fixes, each marked in the source — the encoder decoder without the two-scan filter, a skipped state continuing the turn, and its host test (`b637df3`); knob acceleration by turn speed (`61654ba`); the LCD SPI at 30 MHz (`d179e03`, `1393d39`); the changed-rectangle transfer of the main area (`201b95c`); the UBOOT rescue tool `fm1_rescue.py` / `fm1_rescue.sh` (`a61701e`, `05494ad`, `70440e5`); the USB audio capture resampler and its clock test (`80b7d40`, rewritten in fixed point) | GPL-3.0-only | `firmware/hal/fm1_input.h`, `tests/encoder_fast_test.c`; `firmware/src/knob_accel.h`, `tests/knob_accel_test.c`; `firmware/src/lcd.c`, `firmware/src/lcd_dirty.c`; `tools/fm1_rescue.py`, `tools/fm1_rescue.sh`; `firmware/src/usb_audio_stream.c` (FELUCCA_UA_RESAMPLE), `tests/usb_audio_clock_test.c` |
| Felucca 1.0 by Leo Kuroshita (@kurogedelic), Hügelton Instruments (<https://github.com/hugelton/Felucca>, `727f272`): per-step chance (`step_chance`, the roll in `seq_step`), behind the build switch `FELUCCA_CHANCE` (tools/backports.json lists every backported switch with its source) | GPL-3.0-only | `firmware/src/chance.c` |
| Felucca 1.0.1 by Leo Kuroshita (`20c275e`, #38: the keys of the notes the sequencer and ARP play) and renebohne's SLOOP fork (<https://github.com/renebohne/sloop-fm1>, `e2e5099`: the sounding voices of a synth track light their keys), behind `FELUCCA_KEYLIT` | GPL-3.0-only | `firmware/src/keylit.c` |
| Felucca 1.0.1 by Leo Kuroshita (`20c275e`, #37): QNT SEQ, the sequenced notes snapped to the scale as they play, behind `FELUCCA_QNT_SEQ` | GPL-3.0-only | `firmware/src/qnt_seq.c` |
| Felucca 1.0 by Leo Kuroshita (`727f272`): the SPRING reverb (`rev_spring`: a low cut, a chain of stretched allpasses after Välimäki, Parker and Abel, a damped loop, two pickups), behind `FELUCCA_SPRING` | GPL-3.0-only | `firmware/src/spring.c` |
| Felucca 1.0 by Leo Kuroshita (`727f272`): BASS+, the small speaker mode (`spk_bass`, the low cut an octave up), behind `FELUCCA_BASSPLUS` | GPL-3.0-only | `firmware/src/bassplus.c` |
| Felucca 1.0 by Leo Kuroshita (`727f272`): motion recording (`motion.c`: the store of 64 step events, the patch kept under them, the capture of a knob while recording, the values set at each step), behind `FELUCCA_MOTION` | GPL-3.0-only | `firmware/src/motion.c` |
| Felucca 1.0 by Leo Kuroshita (`727f272`): the PHYS engine (`eng_phys.c`), behind `FELUCCA_ENG_PHYS` | GPL-3.0-only | `firmware/src/eng_phys.c` |
| Felucca 1.0.2 (`db70550`) and 1.0.3 (`b22a24b`) by Leo Kuroshita (@kurogedelic), Hügelton Instruments (<https://github.com/hugelton/Felucca>): BASS+'s 4-pole low-pass and its response check (#42); the editor's live sync, a repeat WATCH keeping unsent changes, no RELOAD echo of the editor's own loads, INFO tag `53 01` (#65); the tolerant DX7 .syx import (1.0.3 `parseSysex`, adapted) and its file variants as tests; behind their switches: the knob quiet window as a layer is let go (#39, `FELUCCA_LAYER_QUIET`), the punch latch (#40, `FELUCCA_PUNCH_LATCH`), BPM LOCK (#58, `FELUCCA_BPM_LOCK`), the divisions in length order (#48, `FELUCCA_DIV_ORDER`), the motion mark on the cards (#63, `FELUCCA_MOTION_MARK`); the idea of FM6 user presets that keep their voice (1.0.3 `up_fm6.c`; our code, `FELUCCA_UP_FM6`) | GPL-3.0-only | `firmware/src/bassplus.c`, `editor.c`, `ui_input.c`, `ui.c`, `ui_layers.c`, `seq.c`, `params.c`, `motion.c`, `ui_draw.c`, `ui_overview.c`, `upreset.c`; `web/editor.html`, `web/test_web.mjs`; `tests/backports_test.c`, `tests/ui_pages_test.c`, `tests/fel102_ui.c` |
| DaisySP by Electrosmith, Corp and Emilie Gillet (<https://github.com/electro-smith/DaisySP>), ported to fixed point by Leo Kuroshita for Felucca 1.0: the PHYS engine's modal and string models and the resonator (`FELUCCA_ENG_PHYS`) | MIT | `firmware/src/phys_dsp.c`, `LICENSES/MIT-DaisySP.txt` |
| Rings by Emilie Gillet (<https://github.com/pichenettes/eurorack>), ported to fixed point by Leo Kuroshita for Felucca 1.0: the PHYS engine's sympathetic strings (`FELUCCA_ENG_PHYS`) | MIT | `firmware/src/phys_symp.c`, `LICENSES/MIT-Rings.txt` |
| Flowstate by Zakaria Chowdhury (flowstate-fm1, <https://github.com/zakariachowdhury/flowstate-fm1>, `6a8ef32`): the large font drawn as the small one at scale 2 (no bitmap of its own, 24.7 KB of flash); the removal of SLOOP's unreachable HOME screen and its audio-interrupt scope tap (`2228c06`) | GPL-3.0-only | `tools/gen_font.py`, `firmware/src/gfx.c` (`cv_text`, `text_w`); `firmware/src/audio.c`, `ui.c`, `ui_draw.c`, `ui_input.c` |
| X0X by Charles Vestal (<https://github.com/charlesvestal/fm1-x0x>): the backlight PWM and its brightness steps (`61654ba`), behind `FELUCCA_BRIGHT`; the delay time halved instead of cut when longer than the line (`892a3b5`), behind `FELUCCA_DLY_HALVE` | GPL-3.0-only | `firmware/hal/fm1_lcd_hw.h` (`fm1_lcd_bl_tick`), `firmware/src/bright.c`, `firmware/src/fx.c` (`delay_samples`) |
| X0X by Charles Vestal (<https://github.com/charlesvestal/fm1-x0x>, `80b7d40`): the ACID engine's TB-303 voice (`dsp/bass303.c`, `bass303.h`, `fastmath.h`, `x0x_param.h`, copied unchanged; the last two are `firmware/src/dsp_float.h` and `firmware/src/x0x_param.h`, one copy shared with the X0X kits; X0X ported it from schwung-303: Open303 by Robin Schmidt, MIT; Devilfish extensions after jc303 (midilab); RAT drive after dm-Rat (Dave Mollen), GPL-3.0) and its TB-3PO line generator (`seq/tb3po.c`, after schwung-tb3po and the Phazerville Hemisphere Suite's TB_3PO applet by djphazer and contributors, GPL-3.0), behind `FELUCCA_ENG_ACID`. X0X's break player is not used | GPL-3.0-only (Open303 parts MIT) | `firmware/src/acid/`, `firmware/src/eng_acid.c`, `firmware/src/dsp_float.h`, `firmware/src/x0x_param.h` |
| Melodee 0.11 by Kerem Kilic (Ellic Studio) (<https://github.com/keremimo/melodee>, `v0.11`, a Felucca fork): the CZ engine — its native tone layout (`cz_patch.h`), the panel-value encoding (`cz_legacy.h`), the CZ envelopes (`eng_phase.c`) and the engine (`eng_cz.c`), adapted, behind `FELUCCA_ENG_CZ`. Casio's 64 CZ-1 factory tones that Melodee ships are not included, nor anything derived from them: the tones are our own | GPL-3.0-only | `firmware/src/eng_cz.c`, `tests/cz_test.c` |
| MAME uPD933 device model by Devin Acker (<https://github.com/mamedev/mame>, `src/devices/sound/upd933.cpp`), modifications by Kerem Kilic (Melodee 0.11): the CZ engine's native phase functions, windows, envelope rate law and logarithmic amplitude in fixed-point C (`FELUCCA_ENG_CZ`) | BSD-3-Clause | `firmware/src/cz_native.c`, `LICENSES/BSD-3-Clause-uPD933.txt` |
| Flowstate by Zakaria Chowdhury (<https://github.com/zakariachowdhury/flowstate-fm1>, `e62e186`): the CPU part of its guard (`firmware/src/guard.c`, docs/guardrails.md 2.5) — a load that is the larger of the measured one and an estimate from what sounds, held over a ceiling for 8 halves, released after 2 s under 1/16 below it — reworked as the predictive CPU guard behind `FELUCCA_CPU_GUARD` (with X0X's lite mode of the 303 as its ACID step) | GPL-3.0-only | `firmware/src/cpuguard.c`, `firmware/src/cpuguard.h`, `tests/cpuguard_test.c` |
| X0X by Charles Vestal (<https://github.com/charlesvestal/fm1-x0x>, `80b7d40`): the X0X 909 and X0X 808 drum kits (kit UIDs 37 / 38), behind `FELUCCA_DRUM_X909` / `FELUCCA_DRUM_X808` (builder items DRUM_X0X909 / DRUM_X0X808, off by default). `dsp/drum909*` (X0X's port of [9W9](https://github.com/athousanddetails/schwung-9W9) by athousanddetails, itself grown out of [ER-99](https://github.com/matthewcieplak/er-99) by Matthew Cieplak, GPL-3.0; changed here: the cymbals read as 8-bit block floating point) and `dsp/drum808*` (X0X's port of [8W8](https://github.com/athousanddetails/schwung-8W8) by athousanddetails, GPL-3.0: circuit models after the TR-808 service notes and Werner / Abel / Smith), `fastmath.h` and `x0x_param.h` (one copy with ACID's: `firmware/src/dsp_float.h`, `firmware/src/x0x_param.h`), and `tools/gen_drum_samples.py` (as `tools/gen_x0x_drums.py`, changed). X0X's break player is not used | GPL-3.0-only | `firmware/src/x0x/`, `firmware/src/drum_x0x.c`, `firmware/src/dsp_float.h`, `firmware/src/x0x_param.h`, `tools/gen_x0x_drums.py`, `tests/x0x_drums_test.c` |
| The 909 hi-hat, ride and crash samples of ER-99 (Matthew Cieplak), shipped by 9W9 and X0X; stored here as 8-bit block floating point by `tools/gen_x0x_drums.py` | GPL-3.0 | `assets/x0x909/` (README.txt there) |
| sc808 (Yoshinosuke Horiuchi's 808 SynthDefs, adapted for Sonic Pi by Sam Aaron; Sonic Pi `etc/synthdefs/`): the X0X 808's rim shot is a transcription of it (through 8W8 and X0X) | MIT | `firmware/src/x0x/drum808.c`, `LICENSES/MIT-sc808.txt` |
| SLOOP 2.3 by isod89 (<https://github.com/isod89/sloop-fm1>, `d691ba7`; many of its fixes after Felucca 1.0 / 1.0.1 by Leo Kuroshita), each behind its switch (`firmware/src/backports23.h`, `tools/backports.json`): CHOP of any length in the web editor; no stuck note after a VOICE change (`FELUCCA_MONO_RELEASE`); stricter checks of what is read back from flash (`FELUCCA_ST_STRICT`); USB MIDI in with flow control, malformed events ignored (`FELUCCA_USB_FLOW`); on overload one voice faded at a time, never the bass or the lead (`FELUCCA_SHED_FADE`); keys debounced as their column is read (`FELUCCA_KEYS_FAST`); the REC screen's dials and count-in (`FELUCCA_REC_MODES`); menu LIGHTS / KEYS / NOTES, the dim layers timed by TIMER4 after Felucca 1.0.1 #35, NOTES by renebohne (`FELUCCA_LIGHTS`); the knobs' one rest state, after Felucca 1.0 #23, and its host test (`FELUCCA_KNOB_ONEREST`); the TRS MIDI reader passing over a received FD, after Felucca [Salt] by ChanceTheMaker (`FELUCCA_TRS_NOISE`); a restore checking each object as a load would (`FELUCCA_BK_CHECK`); the return to V15 checked once the FM-1 is back | GPL-3.0-only | `web/editor.html`, `web/test_web.mjs`, `firmware/src/voice.c`, `firmware/src/storage.c`, `firmware/src/panel.c`, `firmware/src/usb.c`, `firmware/src/audio.c`, `firmware/hal/fm1_input.h`, `firmware/src/seq.c`, `firmware/src/ui_studio.c`, `firmware/src/ui_input.c`, `firmware/src/project.c`, `firmware/src/lights.c`, `firmware/src/ui_menu.c`, `tests/keys_test.c`, `tests/encoder_onerest_test.c`, `tests/midi_uart_test.c`, `tests/bp23_test.c`, `tests/bp23_ui.c`, `firmware/src/midi_uart.c`, `firmware/src/ed_backup.c`, `tests/backup_test.c`, `web/fm1ota.js`, `web/index_pkg.html` |
| SLOOP 2.4 by isod89 (<https://github.com/isod89/sloop-fm1>, `8d3823f`; on Felucca by Leo Kuroshita), the sequencer, each behind its switch (`firmware/src/backports24seq.h`, `tools/backports.json`): SEQ DIV 1/2, 1BAR, 2BAR (`FELUCCA_DIV_LONG`); delay TIME 1/8D, 1/16D (`FELUCCA_DLY_DOT`); micro timing (`FELUCCA_MICRO`); fills (`FELUCCA_FILLS`); parameter locks (`FELUCCA_PLOCK`); quick chain (`FELUCCA_QCHAIN`) | GPL-3.0-only | `firmware/src/core.h`, `firmware/src/seq.c`, `firmware/src/params.c`, `firmware/src/fx.c`, `firmware/src/ui_layers.c`, `firmware/src/ui_input.c`, `firmware/src/seq24.c`, `web/editor.html`, `tests/sl24_seq_test.c`, `tests/sl24_ui.c` |
| X0X 0.10.1-beta by Charles Vestal (<https://github.com/charlesvestal/fm1-x0x>, `49b1fc8`: part volume, pan and sends, the master volume and the drum voices' pans glide over ~10 ms): the mixer glides, in fixed point, behind `FELUCCA_GLIDE` (its NOTICE in the builder) | GPL-3.0-only | `firmware/src/dsp.c` (`glide_next`), `firmware/src/fx.c` (`mix_part`, `mix_finish`), `firmware/src/drums.c` (`drums_mix`), `firmware/src/drum_sends.c` (`dsend_lane`) |
| SLOOP 8-track PR by Erick Buendia Barrientos (Erbubar23) (<https://github.com/isod89/sloop-fm1/pull/45>, a community fork of SLOOP 2.3): osc 2 following osc 1's fine pitch (`0528a8a`: our `vmod_t.fine_all`, ANALOG / ANALOG 2 / PHASE); `tools/fm1_cpu.py`, the load of a real FM-1 from its console (`d23e326`, adapted); `chord_name`, a step's chord by name on the STEP page (`8d9623f`), behind `FELUCCA_CHORD_NAMES` | GPL-3.0-only | `firmware/src/voice.c`, `eng_analog2.c`, `eng_analog.c`, `eng_phase.c`; `tools/fm1_cpu.py`; `firmware/src/ui.c` |
| SLOOP 2.4 by isod89 (<https://github.com/isod89/sloop-fm1>, `v2.4` = `8d3823f`), phase 3, each behind its switch: the sequencer to MIDI OUT with its note sets, ended notes and STOP rule (`FELUCCA_MIDI_OUT`: `seq_out_on` / `seq_out_off` / `seq_out_track_off` / `seq_out_all_off`); MIDI IN = CLOCK (`FELUCCA_MIDI_INCLK`); the per-track MIDI channels (`FELUCCA_MIDI_CH`) are our own, on the same `trk_midi_ch()` seam | GPL-3.0-only | `firmware/src/seq_midi.c`, `firmware/src/seq.c`, `firmware/src/midi_control.c`, `firmware/src/bp_set.c`, `firmware/src/project.c`, `tests/midi_ch_test.c`, `tests/midi_seq_test.c` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Contributions

Contributions are welcome under GPL-3.0-only. By submitting one, you agree that it may be
combined with the Felucca Assets under the section 7 permission above.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.

"M-VAVE" and "FM-1" are trademarks of their respective owners. Felucca is independent
firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by those owners.

## Radio

Felucca never enables the Bluetooth / Wi-Fi radio of the hardware.
