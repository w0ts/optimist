# Felucca licensing

Felucca is free software. Its **code** is licensed under the GNU General Public License,
version 3 only (`GPL-3.0-only`, full text in `LICENSE`). Its **assets** are not part of
that licence: the icon atlas `assets/icons.png`, the panel image `docs/panel.jpg` and the drum sounds made by
`tools/gen_waves.py` (the Hügelton Sample Pack) are Copyright (C) 2026 Hügelton Instruments,
all rights reserved. Their licence terms will be published later. SLOOP's firmware does not contain the
Hügelton Sample Pack: its sampled drum kit is made of CC0 recordings (`assets/samples-cc0/KIT`), and
`gen_waves.py` only feeds the SLICE engine's demo loop, which SLOOP does not build.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments\
USB audio (from Melodee): Copyright (C) 2026 Kerem Kilic (Ellic Studio)

## What is code (GPL-3.0-only)

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the web pages (installer, editor) and their tests: `web/` (not the Fukiai font, below)
- the host tests: `tests/`

You may use, study, change and share it under the GPL. If you distribute Felucca, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified Felucca
inside.

## Additional permission (GPL-3.0 section 7)

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
| Terminus font 8x16 (ter-u16n) | SIL OFL 1.1 | `assets/fonts/ter-u16n.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| Fukiai icon font (Hügelton Instruments), web editor only | MIT | `web/fukiai.ttf`, `web/FUKIAI-LICENSE.txt` |
| CrispyZebra by Leo Kuroshita (<https://github.com/hugelton/CrispyZebra>): the PHASE engine's waveforms are a C port of its oscillator | GPL-3.0 | `firmware/src/eng_phase.c` |
| klattsch by Tony Gies (<https://github.com/tgies/klattsch>): design reference for the VOICE (formant) engine; no code copied. Formant data from Klatt (1980) / Hillenbrand et al. (1995) | MIT (klattsch) | credit only |
| FM6 engine by Kerem Kilic (Melodee, <https://github.com/keremimo/melodee>, `firmware/src/eng_fm6.c`, `eng_fm6_rom.h`, `fm6_store.c`, its tests and web editor support, at e459da5): ported into SLOOP, where it replaces the DX7 engine; its factory voices are Melodee's own | GPL-3.0-only | `firmware/src/eng_fm6.c`, `firmware/src/eng_fm6_rom.h`, `firmware/src/fm6_store.c`, `tests/fm6_*`, `tests/dexed_ref.cc`, `web/editor.html` |
| MSFA (Music Synthesizer for Android, Copyright 2012 Google Inc., <https://github.com/google/music-synthesizer-for-android>) and Dexed (Copyright 2013-2025 Pascal Gauthier, <https://github.com/asb2m10/dexed>; portamento rates by Jean Pierre Cimalando), as Melodee credits them: the FM6 engine restates their synthesis in fixed-point C so that it renders the samples Dexed renders (MSFA's envelopes, pitch envelope, LFO, operator kernels and Dx7Note, Apache-2.0; Dexed's MARK I and OPL engines and its voice handling, GPL-3.0-or-later); their DX7 measurement tables and the tables Dexed computes at start (sine, 2^x, frequency, log-sine / exponent, OPL ROM, detune, LFO, portamento) are used as data. `tests/dexed_ref.cc` builds Dexed's own sources (from a checkout, not in this tree) to compare against | Apache-2.0, GPL-3.0-or-later | `firmware/src/eng_fm6.c`, `tools/gen_tables.py`, `tests/dexed_ref.cc` |
| Melodee (keremimo/melodee, the Felucca line continued by Kerem Kilic, <https://github.com/keremimo/melodee>): ported fixes and features, each marked in the source — encoder first-click fix (`6ec2deb`, Kerem Kilic); MIDI pitch bend, mod wheel, sustain, panic and the GLO > SYSTEM MIDI status (`670193c`, ChanceTheMaker); MIDI notes through the scale layouts (`12ccb56`, Kerem Kilic); the QNT ALL layout (`d294fa0`, Kerem Kilic); step note length (`0dbe626`, Kerem Kilic) | GPL-3.0-only | `firmware/hal/fm1_input.h`, `tests/encoder_test.c`; `firmware/src/midi_control.c`, `midi_pitch_tick` in `voice.c`, `tests/midi_expression_test.c`; `midi_map` / `scale_map` in `seq.c`, `tests/midi_scale_test.c`; `step_note_resize` in `ui_layers.c` |
| Melodee by Kerem Kilic (Ellic Studio) (<https://github.com/keremimo/melodee>, e459da5), a fork of Felucca: TRS MIDI IN reading the ring by content (f087328), clock and transport queued with the notes, the INT / USB / TRS clock source and the lost-clock stop | GPL-3.0-only | `firmware/src/midi_uart.c` (its header), `firmware/src/usb.c` (`midi_in_enqueue`), `firmware/src/clock_sync.c`, `tests/midi_uart_test.c` (`test_uart_ring`) |
| Melodee by Kerem Kilic (Ellic Studio) (<https://github.com/keremimo/melodee>, a Felucca fork): the USB audio (UAC1 stream core, endpoint service, descriptors, its host tests) and the CIN 0xF SysEx fix, ported | GPL-3.0-only | `firmware/src/usb_audio*.c`, `firmware/src/usb_audio_desc.h`, `firmware/src/usb.c`, `tests/usb_audio*` |
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
