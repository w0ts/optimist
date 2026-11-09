# Getting back

- **To the official firmware:** in [Install](https://w0ts.github.io/optimist/webapp/editor/#install), step 4, take
  the copy of M-VAVE's FM-1 V15 the site keeps (or your own `FM-1.fwsc` from [m-vave.com](https://www.m-vave.com/download),
  unchanged) and install it. Or use M-VAVE's own updater, M-UPGRADE. The official firmware does not read Optimist's data: back it up first.
- **The FM-1 does not start Optimist:** hold **OCT−** alone while switching on (*OPTIMIST USB RESCUE*), then install again.
  After two start-ups that crash, the FM-1 goes there by itself.
- **Switching between Optimist's own firmware and the release, or from any firmware:** the installer picks the way
  itself (the running firmware's updater, else the chip's download mode through M-Flasher, a full backup first):
  [docs/INSTALL-PATHS.md](../INSTALL-PATHS.md). No detour through V15.
- **Nothing starts at all:** hold **OCT− + OCT+** while switching on and keep them held for **3 s**: the chip's own update mode
  (UBOOT; the screen stays dark). Then M-UPGRADE, or `tools/fm1_rescue.py` from this repository (macOS; from X0X: it
  backs up the whole flash first and writes only the firmware area), or
  [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter). With the RP2040 dongle of
  [tools/dongle/](../../tools/dongle/README.md) (the transporter on a Seeed XIAO RP2040 or a Raspberry Pi Pico, three wires
  on the USB port) `tools/fm1_rescue.py --dongle` needs neither the key combo nor a cable swap: the dongle keys the chip
  when the FM-1 is switched on and is its USB host.
