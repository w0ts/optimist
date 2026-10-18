# The simulator

Optimist's own firmware (`firmware/src/felucca.c`, the one compilation unit the FM-1 runs) built as WebAssembly for the
browser simulator (`web/sim/`). Not an emulator of the chip: the same C, compiled for the host, on a host HAL.

## What runs

Everything the firmware does above the registers: the boot (`fm1_main` up to its loop), the main loop (UI, editor SysEx,
autosave, storage), the TIMER5 tick (keys, USB poll, `fm1_ms`) and the audio interrupt (`core/audio.c`, every engine,
the drums, the effects). `firmware/sim/hal/fm1_*.h` replace the chip's headers of the same names (found first through
`-I`; the three that `src/` includes by a relative path, `fm1_usb.h`, `fm1_uart.h`, `fm1_xip.h`, through a clang VFS
overlay that `tools/sim.py` writes): same names, types and constants, no registers.

- **Time** is simulated: a 64-bit count of 24 MHz ticks (`hal/sim_core.h`). Code takes no simulated time; a timer read
  moves the clock one tick (a spin on the timer ends), `fm1_delay_*` moves it by the delay (the interrupts due on the way
  run), the watchdog feed 10 us (the firmware feeds it while it waits for an interrupt's work).
- **The scheduler** (`firmware/sim/sim.c`, `sim_run`) moves the clock from event to event: the 10 kHz TIMER5 tick, the
  audio DMA's switch every 256 frames at 44.1 kHz (the half just played goes to the output, the audio handler renders the
  next), and between them the main loop: a pass every 15 ms (`fm1_main_frame`), the turns of its wait
  (`fm1_main_idle_step`). Handlers run when the clock passes their event with the interrupts on; no nesting.
  `system/main.c` splits `fm1_main` with four macros under `FELUCCA_SIM`; the device's code is token for token as before
  (synth profile flash 580,352 bytes before and after, `text.bin` identical but for the build time).
- **Flash**: a 1 MiB RAM image at the FM-1's offsets (`hal/fm1_flash_map.h`), blank (0xFF) at start. The same range
  checks as the device's driver; an erase sets 0xFF, a program ANDs (NOR). A backup made on an FM-1 restores here and back.
- **Screen**: a model of the ST7789's column / row address and memory write: a 240 x 240 RGB565 buffer (native order),
  a dirty mask of 15 bands of 16 rows, a revision.
- **USB MIDI**: a model of the SIE registers `io/usb/usb.c` uses, the device configured at boot (`ep1_config`, as a
  SET_CONFIGURATION). usb.c's own EP1 paths carry the MIDI in and out: notes, clock, the editor's SysEx, backup and
  restore (`io/editor/ed_backup.c`).
- **Panel**: `fm1_in` as the matrix scan leaves it, fed by the page: presses and releases queued, taken one change per key
  a scan frame (~1.1 ms); knob steps added at once; the MASTER pot from the page; the battery reads full.

## Switched off

Whatever the configuration says (`tools/sim.py` `SIM_FORCE`): the second core (`DUAL`), Bluetooth (`BLE`,
`BT_INQ_PROBE`), the cache RAM, the key scan's DMA, USB audio and the USB serial console (`USB_MODE=0`); and SIMD, the
asm kernels (their C), the stock supply start-up, the bench. No TRS MIDI IN (the UART reads nothing), no CPU clock
measured (`cpu_khz` 0), no watchdog, no crash report from a CPU exception (a wasm trap is thrown to the page).

## API

`firmware/sim/sim.c`, exported by name (`export_name`); every integer is 32 bit. Pointers are offsets in the exported
`memory`, which never grows.

| Export | |
| --- | --- |
| `sim_flash()`, `sim_flash_size()`, `sim_flash_rev()` | the flash image (write a saved one before `sim_boot`), 1 MiB, erases + programs so far |
| `sim_boot()` | the start-up (the splash's delays just move the clock) |
| `sim_run(frames)` | advance by `frames` at 44.1 kHz; returns `sim_status()` |
| `sim_status()` | bit 0 reboot requested, bit 1 UBOOT / update requested, bit 2 fatal; set, the run has ended: start a fresh instance on `sim_flash()`'s bytes |
| `sim_audio()`, `sim_audio_take()` | interleaved L, R int16; the frames ready (up to 16384), count reset |
| `sim_pixels()`, `sim_frame_rev()`, `sim_dirty()` | 240 x 240 RGB565; revision; band mask (bit b = rows 16b..16b+15), cleared on read |
| `sim_key(i, down)` | note key 0..26 (F3 .. G5) |
| `sim_button(i, down)` | a button by label, `ui/panel.c` order: FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ |
| `sim_enc(i, delta)` | a knob by role: SELECT ALGORITHM PRESETS KNOB 1..4; + = clockwise |
| `sim_master(v)` | the MASTER pot, 0..1023 |
| `sim_leds()`, `sim_led_count()` | 41 bytes: the buttons by label, then the 27 keys; 0 off, 1 dim, 2 lit |
| `sim_midi_buf()`, `sim_midi_in(len)` | raw MIDI in (16 KiB: SysEx, short messages, running status) |
| `sim_midi_out_buf()`, `sim_midi_out()` | raw MIDI the device sent; the bytes ready, count reset |
| `sim_info(what)` | 0 keys, 1 buttons, 2 knobs, 3 frames a half buffer (256), 4 sample rate, 5 backlight 0..16, 6 MIDI bytes lost, 7 audio frames lost, 8 `fm1_ms` |
| `sim_button_names()`, `sim_enc_names()` | the labels, `\n`-separated, 0-terminated |

Instantiate with no imports, then call `_initialize` (the reactor's start) once:

```js
const { instance } = await WebAssembly.instantiate(bytes, {});
instance.exports._initialize();
```

## Build and test

```sh
tools/builder/venv/bin/python tools/sim.py build --wasm [--config FILE] [--out build/sim/sim.wasm]
node tests/sim_test.mjs [build/sim/sim.wasm]
```

The venv's Python (the generators need Pillow) and `emcc` (Emscripten). The test boots, runs 3 s, checks the screen,
sound after a key, the editor's INFO, GET / SET / DESC, PROJECT save and load across a restart, BK_LIST, a backup of
the working project restored into a blank flash (and the restart it asks for), the UBOOT SysEx, and prints the wall time
a simulated second takes.
