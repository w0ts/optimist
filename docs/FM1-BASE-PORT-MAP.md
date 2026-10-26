<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Optimist on fm1-base: the port map

Every hardware call Optimist's code makes (`firmware/src`, the `fm1_*` HAL, 164 functions, counted 2026-10-26 on sdcard
10447865), what fm1-base's `plat.h` offers for it (its `own-bt` branch d0133c2, which carries our Bluetooth stack), and what
the port does. The rule (the user, 2026-10-26): **use fm1-base's part whenever it is as good as ours**; port to the fork
(w0ts/fm1-base, private) only what it lacks, as a feature branch that can become a pull request; keep in Optimist's app only
what is ours (GPL, not ours to give: Felucca, SLOOP, Melodee code) or app-specific. Roadmap: FM1-BASE-ROADMAP.md.

## The map

| Optimist (calls in firmware/src) | fm1-base | The port |
|---|---|---|
| Interrupts: `fm1_irq_on` 126, `fm1_irq_off` 123, `fm1_irq_hold` 4, `fm1_icfg` 7 | `plat_lock` / `plat_unlock` | **use theirs**: a rename (the shim step) |
| Time: `fm1_ticks` 90, `fm1_delay_ms` 14, `fm1_time_init` | `plat_ticks24` (the same TIMER4 count), `plat_ms` | **use theirs** (a delay helper in the app) |
| Bluetooth LE: `fm1_ble_*` (20 functions, ~130 calls) and BR/EDR `fm1_br_*` (25, ~90 calls) | own-bt: our stack in `bt/`, its drivers in felucca-bsp, `plat_ble_*`, `plat_bta_*` | **use theirs** (it is ours, with Charles's knobs and the radio calibrated at every start); Optimist's BLE page and MIDI router call `plat_ble_*` / `plat_bta_*` |
| Panel input: `fm1_enc_take`, `fm1_input_edges`, `fm1_input_note_edges`, `fm1_input_scan` / `tick` / `init` / `oct_raw` | `plat_buttons`, `plat_keys`, `plat_enc` (the same debounce lineage) | **use theirs**; the edges (pressed since last) in the app from the held bits; the boot combo (OCT- OCT+ 3 s, UBOOT) to check against theirs |
| LEDs: `fm1_led_key`, `fm1_led*` arrays (full, dim, half) | `plat_lights(buttons, keys, dim_b, dim_k, glow)`, `plat_lights_mid`, `plat_play_red` | **use theirs**: three levels map to full / mid / dim; PLAY's green lamp via `LED_PLAY_GREEN` |
| MASTER: `fm1_adc_read(FM1_ADC_MASTER)` | `plat_master` (0..4096), `APP_GAIN` | **use theirs** |
| Screen: `fm1_lcd_*` (8: init, baud, send, wait, deselect, backlight) and display/lcd.c, gfx.c | device/lcd.c, gfx.c: the same Felucca canvas (20 functions with the same names: cv_begin, cv_rect, cv_text, draw_text_box, lcd_blit, lcd_fill ...) | **use theirs**; our extras (cv_box, cv_frame, ink_on, sst_strip, lcd_touched) as helpers in the app or a small PR; the backlight dimming (`fm1_lcd_bl_*`) to compare |
| The mirror: our `mir_blit` / `mir_fill` hooks in lcd.c | device/mirror.c (`mirror_blit`) | **keep ours** (about 4x smaller on our UI, Bluetooth too): a PR for an app-owned mirror hook, or ours as fm1-base's mirror v4 |
| Audio: `fm1_audio_init` / `pending` / `free_half` / `ack_half` / `ack_aux` / `sync` / `stop`, `fm1_alnk0_irq` (commit-aside, CPU guard) | `app_render` from the ALNK0 interrupt, `APP_OVERLOAD`, `plat_cpu_pct`, `plat_xruns` | **needs a hook (PR)**: the app owning the half (its own commit-aside and guard), or app_render as is if the guard's prediction moves into the app |
| Second core: `fm1_dual_*` (25 functions), `fm1_cpu1_*` | `plat_cpu2_*` (a job model), `APP_CSTART`, `APP_FAULT` (an app's own second core) | **keep ours in the app** (dual.c is Optimist's), on the hooks fm1-base already has for that |
| USB: `fm1_usb_*` (18: endpoints, SIE, SOF) in io/usb (USB audio from Melodee, the editor's SysEx, the console) | felucca-bsp usb.c: USB-MIDI, USB audio in and out, `APP_SYSEX`, `APP_USB_CMD`, `plat_sysex_send`, `plat_host_read` | **use theirs** for MIDI and audio; the editor over `APP_SYSEX` (to check: its sizes, the reply path, the mirror's push ring); the console (CDC) to check |
| MIDI over TRS: `fm1_uart1_*` | `plat_midi_in` / `send` (PORT_TRS) | **use theirs** |
| Flash: storage.c's `st_read` / `st_erase` / `st_prog`, `fm1_xip_ptr`, `fm1_xip_word` | `plat_data`, `plat_data_erase`, `plat_data_write` (our windows exactly), `plat_store_*` | **use theirs** under our formats (projects, sections, kits stay where they are: the users' data unchanged); settings to `plat_store` or kept in the data window |
| Update and recovery: `fm1_enter_update`, `fm1_enter_uboot`, `fm1_reboot`, `fm1_reset_reason`, `fm1_fault`, `fm1_wdt_*` | felucca-bsp's loader and ota, the crash record, safe mode, `plat_yield` | **use theirs** (one loader for every FM-1 firmware); our package identity FM-1_7xx kept; the watchdog fed by `plat_yield` |
| Power: `fm1_supply_*`, `fm1_boost_level`, `fm1_lvr_stock`, `fm1_p33_read` | `plat_battery`, felucca-bsp's supply set-up | **use theirs**; the battery volts guess to compare |
| Memory: `fm1_cache_ram_*`, `fm1_guard_*`, `fm1_flip_*`, `fm1_mem_readable`, `fm1_peek8`, the LZB boot stream | felucca-bsp's start-up, app.ld | **PR** for what they lack (the sdcard branch's packed SRAM, the guard ending at the code's real end, a bigger app slot); the LZB stream and the arena stay ours |
| SD card: `fm1_sd_*` (14, the SPI bus shared with the LCD) | none | **keep in the app** until the SD board exists, then a PR (the LCD and SD sharing SPI1) |
| Remote panel (the mirror's PANEL): `fm1_remote_key` / `enc` / `clear` | device/mirror.c's remote | with the mirror (above) |

## What this says

- About half of Optimist's hardware calls (interrupts, time, Bluetooth, input, LEDs, MASTER, screen, MIDI, flash) map onto
  fm1-base as it is, most by a rename.
- Four things need fm1-base to grow (pull requests): the audio block owned by the app, the mirror as an app hook (or ours as
  theirs), the memory layout of the sdcard branch, the SD bus. The second core stays in the app on the hooks they have.
- Not checked yet: the editor's SysEx through `APP_SYSEX`; the console; the backlight; the boot combos; flash and RAM of the
  whole thing.

## Next

1. The shim inside Optimist: the calls of the first rows renamed to `plat_*` names on our own HAL (no change in behaviour),
   so the same app source builds on both.
2. A trial app in the fork (branch `trial/optimist-app` from own-bt): Optimist's engines, sequencer and effects on
   `app_render`, then the UI on gfx.c, row by row of this map.
