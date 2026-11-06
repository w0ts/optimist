# The FM-1 recovery dongle

An RP2040 board on three wires of the FM-1's USB port that puts the chip into its mask-ROM update mode
(`UBOOT1.00`) and then is its USB host, so `tools/fm1_rescue.py --dongle` writes a known-good `.fwsc` back
without the cable swap: the ROM waits only a few seconds for a host after the key, and moving the plug from a
keying board to the Mac inside that window is a matter of luck.

The firmware is [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) by kurogedelic (MIT, with
[Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB), MIT), built here at a pinned commit with one
patch, `transporter-optimist.patch`:

- sectors up to `0xDA000` may be written instead of `0x93000`, because Optimist's SD-card flash layouts reach
  `0xD0000` and a switch between layouts clears the other layout's sectors up to `0xDA000`
  (`tools/flash_layout.py` `CLEAR_TOP`); the kits, presets and settings above stay out of reach;
- `status` reports the chip's INQUIRY vendor and product, so the rescue tool's identity check is the real one.

Status 2026-11-06: builds for both boards; **not yet run against an FM-1 from this tree.** The transporter's
author ran it on an FM-1 with the XIAO RP2040; the Pico build is listed untested there too.

## Boards and wiring

Three wires, **never VBUS**: the FM-1 runs on its battery, switched on and charged.

| Board | FM-1 D+ (green) | FM-1 D- (white) | GND (black) |
|---|---|---|---|
| Seeed XIAO RP2040 | D6 = GP0 | D7 = GP1 | GND |
| Raspberry Pi Pico / Pico W | pin 1 = GP0 | pin 2 = GP1 | pin 3 |

The FM-1 side is a USB-C cable cut open (the colours above are the USB 2.0 convention; check with a meter), or a
USB-C breakout board. The RP2040's own USB port goes to the Mac.

Not a Pico 2 / RP2350: it needs extra pull-downs and a firmware port (the transporter's README).

## Build and flash

```sh
tools/dongle/build.sh        # needs cmake, ninja, arm-none-eabi-gcc: brew install cmake ninja; brew install --cask gcc-arm-embedded
```

It fetches pico-sdk 2.2.0 and the transporter into `build/` and leaves `build/dongle/fm1-dongle-seeed_xiao_rp2040.uf2`
and `build/dongle/fm1-dongle-pico.uf2`. To flash one: hold the board's BOOTSEL button while plugging it into the
Mac, then copy the `.uf2` onto the `RPI-RP2` drive that appears. The board reboots as the dongle and shows up as
two serial ports (`/dev/cu.usbmodem*`): a log console and the data port the rescue tool finds by itself.

## Use

```sh
tools/builder/venv/bin/python tools/fm1_rescue.py FM-1.fwsc --dongle            # check and back up, write nothing
tools/builder/venv/bin/python tools/fm1_rescue.py FM-1.fwsc --dongle --write    # then write
```

No `sudo`: the Mac never owns the FM-1's USB. `FM-1.fwsc` defaults to the stock V15 kept in `web/stock/`; any
other known-good package takes `--confirm-package <sha256 prefix>` as without the dongle (`tools/fm1_rescue.py --help`).

What happens: the tool finds the dongle, and unless the FM-1 is already in update mode it tells the dongle to
start keying and asks you to **switch the FM-1 off, then on**. The ROM takes the key at power-on, the dongle
feeds it the 1 ms pulses it needs and becomes its host; the tool then runs exactly as over the Mac's own USB:
chip identity, a full 1 MiB backup next to the package, only the differing 4 KiB sectors written, a final full
read, restart. If stock V15 is running, the dongle enters update mode with V15's USB-MIDI soft key instead and
no power cycle is needed.

The XIAO's RGB LED: white once the dongle is the host, green when the chip answered as UBOOT, red when it did not.
The Pico has one LED on GP25, which the firmware drives as the XIAO's blue channel, inverted: dark while the dongle
is the host, lit once the chip answered as UBOOT, and lit on failure too. Its log console (the other serial port,
115200) says which.

## Files

| Path | What |
|---|---|
| `build.sh` | fetches, patches and builds the firmware for the two boards |
| `transporter-optimist.patch` | the two changes above, against the pinned transporter commit |
| `../fm1_dongle.py` | the Mac side: the transporter's serial line protocol behind `fm1_rescue.Uboot`'s calls |
| `../fm1_rescue.py --dongle` | the rescue tool through it |
| `../../tests/rescue_dongle_test.py` | the tool against a simulated dongle |

Other ways in, for the record: the ESP8266/ESP32 keying sketches that need the cable swap, and the vendor's own
"JL USB Updater" (a keying MCU plus a USB switch that hands the bus to the PC).
