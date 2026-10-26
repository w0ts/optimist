# Wi-Fi start-up capture

JieLi's own Wi-Fi radio start-up, run in the FM-1 emulator with its register accesses traced. It's the same idea as
`tools/ble_rf_capture.py` with stock V15, used for docs/WIFI-HW-FACTS.md §2b. Local only: the inputs are the
user's own copy of JieLi's AC79 SDK (`AC79NN_SDK_V1.2.1_2023-12-13`, Apache-2.0), and everything made from them goes
to `worktrees/wifi-capture/` (git-ignored). Nothing of the SDK is committed.

| File | What |
| --- | --- |
| `cap_crt.S`, `cap.ld` | entry and layout (XIP code, `.data` copied, `.bss` zeroed) |
| `cap_main.c` | the sequence: `wf_rf_common_init`, `wifi_rf_on`, `wfhw_iqr_init`, then a second boot that takes the quick start; the trims (the SDK demo board's); stand-ins for the OS, timers, logging and power calls |
| `gen_conf.py` | the RX thresholds, switches and `wl_rfd_ram_lut` from the user's `apps/common/net/wifi_conf.c` (its active branch) |
| `build.sh` | extracts `wl_rf_common.a` (host `llvm-ar`), compiles and links in the toolchain image |
| `split.py` | splits a trace into the two passes at `wifi_rf_off`, writes per function |
| `diff_v15.py` | aligns pass 2 with stock V15's boot-2 writes |

```sh
tools/wifi_capture/build.sh <sdk>/cpu/wl82/liba/wl_rf_common.a <sdk>/apps/common/net/wifi_conf.c
cd worktrees/wifi-capture
FM1_STUB_MMIO=30000-30eff FM1_MMIO_TRACE=trace_t0.txt FM1_MMIO_RANGES=10000-1001f,11900-119ff,14000-1406f,30000-31fff \
  FM1_MMIO_READS=1 ~/GitHub/fm1-emulator/rust-emulator/target/release/examples/diagnose wifi_capture_t0.elf 400000000
python3 ../../tools/wifi_capture/split.py trace_t0.txt wifi_capture_t0.syms --write-pass2 pass2.txt
```

`FM1_STUB_MMIO=30000-30eff`: the emulator models no MAC registers below `0x31000` (`0x328`, `0x324`).
The calibrations' measurements in pass 1 come from the emulator, not from a radio. Only pass 2 (the quick start) is
a program worth replaying.
