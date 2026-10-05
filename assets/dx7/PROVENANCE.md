# DX7 voice data

## `rom1a.syx`: the DX7's first factory cartridge (ROM1A)

- **What:** the 32 voices of Yamaha's DX7 ROM cartridge 1A (BRASS 1 ... E.PIANO 1 ... TAKE OFF), as a
  standard 32-voice bulk dump (VMEM, 4104 bytes).
- **From:** Google's *music-synthesizer-for-android* (msfa), `app/src/main/res/raw/rom1a.syx`, commit
  `f67d41d313b7dc85f6fb99e79e515cc9d208cfff` (<https://github.com/google/music-synthesizer-for-android>).
  SHA-256 `35ce4a1c769f971601ce1321eb8e8cae2202797b2e9c11dac06b7fd3e3ae8247`. The file's checksum byte does
  not match its data; the firmware does not use it (it embeds the 4096 voice bytes).
- **Rights:** the voices were made by Yamaha. The data is widely redistributed (msfa ships it in an
  Apache-2.0 repository), but no licence from Yamaha for it is known. It is **not** covered by
  SLOOP's GPL or msfa's Apache licence. **Fine for private builds; check its status before any public
  release.**
- **In the firmware:** `tools/gen_dx7_rom.py` turns it into `build/gen/dx7_rom1a.h`; with
  `FELUCCA_DX7_ROM=0` (`./build.sh` environment) the firmware is built without it, and the DX7 engine has
  only the self-made voices of `firmware/src/dx7_voices.c`. Another bank can be swapped in by giving
  `gen_dx7_rom.py` another `.syx` (build.py: `tools/gen_dx7_rom.py OUT [bank.syx]`).

## `firmware/src/dx7_voices.c`

The engine's own voices (INIT VOICE, E.PIANO, BASS PLUCK, ... 8OP BASS) were written for SLOOP from
scratch (classic FM recipes, no Yamaha data). GPL-3.0-only like the rest of the firmware.
