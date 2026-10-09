# BLE-MIDI on Jangada: feasibility study

Status: analysis only. Nothing was built, downloaded or run on hardware. Date: 2026-10-05. **Updated 2026-10-08 with stock-firmware facts and measured sizes: see section 9, which supersedes §1.5 and §8.**
Scope: adding a BLE-MIDI peripheral to Jangada on the FM-1 (JieLi AC791N / WL82), using the
prebuilt Bluetooth libraries of the JieLi AC79 SDK (Apache-2.0).

Each claim is tagged:
- **[V]** verified in this study (file, symbol or measurement given);
- **[I]** inferred or estimated, not measured. Treat it as a hypothesis.

Sources read: `firmware/` in this tree; the AC79 SDK checkout at `~/GitHub/ac79-sdk`; the
fm1-emulator notes (`~/GitHub/fm1-emulator-boot/rust-emulator/*.md`, describing observed behaviour
of the stock and Baud Girl images); the public BLE-MIDI specification (from memory, see section 5).
No code from the stock M-VAVE or Baud Girl images was read or used. The `.fwsc` packages are
encrypted, so a `strings` scan found nothing useful in them (only `ENABLE_SDRAM`) [V].

---

## 0. Summary

| Question | Answer |
| --- | --- |
| Is there a BLE-only configuration? | Yes. `apps/demo/demo_ble` sets `TCFG_USER_BLE_ENABLE 1`, `TCFG_USER_BT_CLASSIC_ENABLE 0` [V]. |
| Closest starting point | `demo_ble` with `TRANS_DATA_EN` → `apps/common/ble/le_trans_data.c`. It is a GATT server with a 128-bit UUID service, a NOTIFY characteristic and a WRITE_WITHOUT_RESPONSE characteristic, which is the shape of the BLE-MIDI service [V]. |
| What the libraries need | A FreeRTOS-backed `os_*` API (tasks with blocking queues, semaphores, mutexes, delays), software timers, `request_irq`, a heap, `lbuf`/`cbuf`, `syscfg_read/write`, clocks and power stubs. About 110 external symbols in total; the full list is in section 4 [V]. |
| Can Jangada host it? | Yes, but with conditions. The recommended path is a small OS shim on core 0 with cooperative stack-switching tasks (option b), plus some leaf modules reused from the SDK's `system.a`. |
| Biggest technical risk | The interrupt priority and latency that the closed controller (`btctrler.a`) needs, measured against Jangada's audio ISR. That ISR renders a whole 5.8 ms half-buffer at priority 3, using up to 85 % of the period. |
| Biggest non-technical issue | GPL-3.0. A Jangada image linked with the binary-only BT libraries cannot be distributed with its complete Corresponding Source. Private use is fine. Public distribution needs a GPL section 7 linking exception from the copyright holders (section 7 below). |
| Recommendation | **Go with conditions**: a hardware gate first (Phase 0), then the shim. See section 8. |

---

## 1. What the SDK BLE stack needs

### 1.1 Libraries and their form

| Library | Members | Format | Role |
| --- | --- | --- | --- |
| `cpu/wl82/liba/btctrler.a` | 107 | 106 LLVM bitcode + 1 ELF (version) [V] | Link layer, baseband, RF (`RF_ble.c`, `RF_ble5.c`, `analog.c`, `ll_*.c`), the in-memory HCI controller side (`hci_controller.c`), plus Classic/TWS code |
| `cpu/wl82/liba/btstack.a` | 72 | 71 bitcode + 1 ELF [V] | Host: HCI, L2CAP, ATT (`att_db.c`, `att_server.c`, `att_send.c`), SM (`sm.c`), GATT client, Classic profiles |
| `cpu/wl82/liba/wl_rf_common.a` | 14 | bitcode [V] | Shared Wi-Fi/BT RF front-end init and calibration (`wf_rf_cal.c`, `wf_rf_init.c`) |
| `crypto_toolbox_Osize.a`, `lib_ccm_aes.a` | 13 / 4 | bitcode [V] | SM crypto (ECDH / AES-CCM) |

- Debug strings name the compiler as `clang version 4.0.1` and the build path as
  `/jks/workspace/wifi_sdk_all_export_gitlab/...` [V]. That is the same clang version as the
  JieLi toolchain Jangada already uses (`tools/get_toolchain.sh`) [V].
- Because the libraries are bitcode, the link must go through LTO. The SDK links with
  `lto-wrapper` and `--plugin-opt=...` (`apps/demo/demo_ble/board/wl82/Makefile`, lines 39 and 264–287) [V].
  Jangada links with plain `pi32v2/bin/ld` (`tools/build.py`, `build_app`) [V], so the link step
  must change.
- BLE-only builds are configured through **`const int` symbols that the application supplies**
  (`apps/common/config/log_config/lib_btctrler_config.c`: `config_btctler_modules = BT_MODULE_LE`,
  `config_btctler_le_roles`, `config_btctler_le_hw_nums`, …) [V]. Stripping the Classic code
  therefore depends on LTO constant propagation and dead-code removal [I]. Without LTO, the
  Classic/TWS objects (for example `lmp.c.o`, 602 KB of bitcode) would be pulled in.
- The member sizes are bitcode with debug info, **not** flash sizes. There is no map file or
  BT-enabled ELF in the checkout. `cpu/wl82/tools/loader_tools/sdk.elf` contains no BT symbols [V].

### 1.2 Headers are missing from this checkout

`~/GitHub/ac79-sdk` is a sparse, blob-less clone. Its sparse checkout contains only `apps`,
`cpu/wl82`, `include_lib/driver` and `include_lib/system` [V] (`git sparse-checkout list`).
`include_lib/btctrler/**` and `include_lib/btstack/**` are listed in `git ls-tree HEAD`, but their
blobs are not local [V]. They include `le/att.h`, `le/le_user.h`, `le/ble_api.h`, `le/sm.h`,
`btctrler_task.h`, `btstack_task.h`, `port/wl82/btcontroller_config.h` and the linker fragments
`btctler_lib_{text,data,bss}.ld` and `btstack_lib_*.ld`. Phase 0 must fetch them; this study was
not allowed to download.

### 1.3 Tasks (from `apps/demo/demo_ble/app_main.c`, `task_info_table`) [V]

| Task | Prio | Stack (words) | Queue (words) | Note |
| --- | --- | --- | --- | --- |
| `btctrler` | 19 | 512 (2 KiB) | 384 (1.5 KiB) | `#C0btctrler` (pinned to core 0) when `CPU_CORE_NUM > 1` |
| `btstack` | 18 | 768 single-core / 1024 dual-core (3–4 KiB) | 384 | `#C0btstack` (pinned to core 0) |
| `sys_timer`, `systimer`, `sys_event`, `app_core` | 9 / 14 / 29 / 15 | 512 / 256 / 512 / 1024 | – | SDK infrastructure. A shim does not need them [I] |

`include_lib/system/task.h` gives the stack unit as words [V]. `CPU_CORE_NUM` is 2 for WL82
(`include_lib/driver/cpu/wl82/asm/cpu.h:43`) [V]. So in its dual-core configuration the SDK itself
pins the BT tasks to **core 0** [V]. Why it does so is unknown [I].

Both task bodies block in `os_taskq_pend` and are woken by `os_taskq_post_type`, which the
controller ISRs and the host also call (`btctrler_task.c.o`, `btstack_task.c.o`, `bredr_link.c.o`) [V].
The host side is a BTstack-style run loop (`btstack_run_loop_embedded_execute_once`,
`btstack_sys_timer.c.o`) driven by `sys_timer_add` [V]. HCI between host and controller goes through
in-memory buffers: `hci_transport_h4_controller_instance` is referenced by `btstack_task.c.o`, and
`cbuf_read`/`cbuf_write` by `btstack_main.c.o` and `att_send.c.o` [V].

### 1.4 Interrupts

- BT sources (`include_lib/driver/cpu/wl82/asm/hwi.h`) [V]: `IRQ_BLE_RX_IDX` 29, `IRQ_BREDR_IDX` 40,
  `IRQ_BT_CLKN_IDX` 41, `IRQ_BT_DBG_IDX` 42, `IRQ_WL_LOFC_IDX` 43, `IRQ_BLE_EVENT_IDX` 45, `IRQ_WF_IDX` 52.
- The libraries register their handlers through `request_irq(index, prio 0..7, handler, cpu_id)`.
  The callers are `RF_ble.c.o`, `bredr_RF.c.o`, `bredr_frame.c.o`, `bredr_slot_timer.c.o`,
  `direct_test_dut.c.o`, `h4_*.c.o` and `wl_hw_init.c.o` [V].
- **`request_irq` is an external symbol** (from `system.a:hwi.c.o`) [V]. A shim therefore owns it.
  It can see every priority the libraries ask for and remap it. The SDK also overrides priorities and
  cores per IRQ through the application's `irq_info_table` (`hwi.c.o` references it) [V].
- The priority values the libraries request are compile-time constants inside bitcode. They could not
  be read without LLVM tools (none installed). The Phase 1 logging wrapper will show them.
- ISR stubs: the controller ISRs are presumably compiled as SDK interrupt functions that go straight
  into the vector table [I]. Jangada's own handlers instead go through asm wrappers
  (`hal/fm1_isr.S`, `fm1_irq_attach`) [V]. The shim's `request_irq` must write `FM1_VEC[n]` and
  `FM1_ICFG` in the way the SDK's ISR convention expects. Check this against the disassembly of one
  linked handler in Phase 1.

### 1.5 RAM and flash footprint

| Item | Estimate | Basis |
| --- | --- | --- |
| Task stacks + queues | ~8–9 KiB | table above [V] |
| Controller LE buffers | small | `config_btctler_le_rx_nums = 5`, `le_acl_total_nums = 5`, `le_acl_packet_length = 27` (or 251 with DLE) [V]; `le_hw_ram_use_static = 0` without SDRAM [V], so they come from the heap |
| Lib `.data/.bss` + heap (ATT db, SM, lbuf pools) | ~20–45 KiB | [I]. Measure in Phase 0 from `BTCTLER_LE_RAM_TOTAL`, `BTCTLER_COMMON_RAM_TOTAL` and `BTSTACK_LE_HOST_MESH_RAM_TOTAL`, which `cpu/wl82/sdk_ld_sfc.c:313-328` defines [V] |
| **Total RAM** | **~30–55 KiB** | [I] |
| Flash, BLE-only controller + host + RF init/cal + SM crypto | ~120–200 KiB | [I], estimated from experience with JieLi BLE-only images. Measure `BTCTLER_*_CODE_TOTAL` in Phase 0 |

### 1.6 CPU load, calibration, clocks

- **RF calibration at boot.** The emulator notes for the stock and Baud Girl images
  (`BAUD-GIRL.md`, "Earlier blockers" item 1) report a btctrler RF-calibration busy loop of
  ~142 M emulated instructions. At the default 24 MHz model it outlasted the 4 s watchdog; at
  `FM1_CPU_MHZ=192` it did not [V, as an emulator observation]. Real hardware time is unknown.
  At 160–320 MIPS that is roughly 0.5–1 s [I]. The calibration belongs to `wl_rf_common.a`:
  `btctrler:analog.c.o` uses `keep_BT_CMPUK1..3` and `keep_BT_OFSI/OFSQ`, which `wf_rf_cal.c.o`
  defines [V].
- **Calibration cache.** `wl_rf_common` calls application functions for trim data:
  `wifi_get_rf_trim_data`/`wifi_set_rf_trim_data` (`apps/common/net/wifi_conf.c:876-889`, stored in
  VM item `VM_WIFI_RF_INIT_INFO`), `wifi_get_xosc` and `wifi_get_pa_trim_data`
  (`apps/common/config/user_cfg.c:529-566`) [V]. A shim can store these in Jangada's own flash store,
  so a full calibration probably runs only on first boot [I].
- **busy-polling btstack.** In the emulator (no radio model), the stock `btstack` task loops on an
  empty HCI `cbuf_read` and never blocks, starving lower tasks (`BAUD-GIRL.md`, "Blocker") [V as an
  emulator observation]. On hardware the controller is presumed to answer, so this does not happen [I].
  A shim **provides `cbuf_read`** itself, so it can yield when the buffer is empty. That guards
  against a non-blocking poll freezing a cooperative scheduler.
- **Clock.** The SDK default is `SYS_CLK=320MHz`, `HSB_DIV=1` (`cpu/wl82/tools/isd_config_rule.c:136-150`) [V].
  Jangada does not program the PLL. It runs on the clock the SPL hands over (`firmware/crt0.S`; no
  clock code in `firmware/hal`) [V]. The handover registers were captured once
  (`ATOMIC.md`, "SPL clock handoff") but not decoded into a frequency [V]. The BT code asks for
  clocks through `clk_get` (`h4_uart_hw.c.o`, `wf_rf_init.c.o`, `wl_rfd_init.c.o`) and delays through
  `delay`/`hw_udelay`/`delay_us` (cpu.a) [V]. The shim can implement these from Jangada's TIMER4
  (24 MHz, `hal/fm1_time.h`), which does not depend on the CPU clock. Whether the BT RF PLL depends on
  the system PLL setting is unknown [I].
- **Licence keys and chip ID.** `btctrler:lmp.c.o` references `get_chip_id`, `doe` and `norflash_ioctl`,
  and `analog.c.o` defines `get_jl_chip_id`/`get_jl_chip_id2` [V]. Function names in the debug info
  link them to the JieLi test-box protocol (`lmp_escape3_jl_chip_req`, `bt_get_chip_key_crc`,
  `EX_INFO_TYPE_CHIP_KEY_CRC`) inside Classic LMP [V]. No licence or authorisation check was found in
  the LE path [I]. Bitcode can hide a check behind an innocuous name, so this is not proof.

---

## 2. BLE-only configuration and starting point

- `apps/demo/demo_ble/include/app_config.h`: `TCFG_USER_BLE_ENABLE 1`, `TCFG_USER_BT_CLASSIC_ENABLE 0`,
  `TRANS_DATA_EN 1`, `TCFG_BLE_SECURITY_EN 1` [V].
- The demo Makefile builds with `CONFIG_NO_SDRAM_ENABLE` (the FM-1 also has no SDRAM in use) and links
  `fs, event, system, cfg_tool, cpu, common_lib, wl_rf_common, btctrler, btstack, crypto_toolbox_Osize, lib_ccm_aes, lib_sig_mesh, update` [V].
- `apps/common/ble/le_trans_data.c` (1354 lines) is the GATT server. It has an advertising/scan-response
  builder (`make_set_adv_data`), `att_read_callback`/`att_write_callback`, CCC handling,
  `app_send_user_data` → `ble_op_att_send_data`, `ble_op_conn_param_request`, `ble_op_set_data_length`,
  and Just-Works SM (`sm_just_works_confirm`) [V].
- Its ATT database (`apps/common/ble/include/le_trans_data.h`) already declares a 128-bit-UUID service
  with a NOTIFY characteristic and a WRITE_WITHOUT_RESPONSE characteristic (`0000F530-...`) [V].
  BLE-MIDI needs the same shape: one characteristic with READ, WRITE_WITHOUT_RESPONSE and NOTIFY.
- Other examples: `le_hogp.c` (HID over GATT), `multi_demo/` and `mesh/` are not relevant.

---

## 3. Coexistence with Jangada's realtime audio

### 3.1 Jangada's interrupt picture [V]

| Source | Prio | Work | File |
| --- | --- | --- | --- |
| ALNK0 (I2S half) | **3** | Renders the **whole** half-buffer: `HALF_FRAMES` 256 at 44.1 kHz = 5.8 ms, in `CTL` = 32-sample blocks (0.73 ms) | `src/audio.c` (`fm1_alnk0_irq`, `audio_init`), `src/core.h:16`, `src/dsp.c:124` |
| TIMER5 10 kHz (input, `usb_poll` and `uart_midi_poll` at 2 kHz) | 1 | Kept below ALNK0 on purpose: "no nesting into audio" | `src/main.c:40` |
| CPU exception | 7 | – | `hal/fm1_irq.h` |

Overload policy: when a half-buffer takes more than 85 % of 5.8 ms (about 4.9 ms), `shed_req` sheds
a voice before the next half. `song.cpu_q8` is the smoothed load [V].

### 3.2 Consequences

- **Below priority 3**, a BT ISR can wait up to ~4.9 ms behind a heavy audio half. For a BLE
  peripheral, the hardware sequencer runs the connection-event timing; the software ISR
  (`IRQ_BLE_EVENT`/`IRQ_BLE_RX`) handles RX/TX buffers and prepares later events [I, from names in
  `RF_ble.c.o`: `__set_anchor_cnt`, `__set_widen`, `ble_hw_rx`, …]. With a connection interval of
  11.25–15 ms, a 5 ms delay may be survivable. A 7.5 ms interval is marginal. **Unmeasured.**
- **Above priority 3** (for example 5–6), BT ISR time is taken from the audio budget. That is safe if
  the ISRs are short (tens of µs per event [I]), and the existing shedding absorbs bursts. This is the
  recommended starting point. The shim's `request_irq` sets the priority, and the audio meter
  (`cpu_q8`, `felucca_dbg.max_us`/`late`) measures the cost on hardware.
- Task-level BT work (host stack, SM pairing with software P-256 in `crypto_toolbox_Osize.a`) runs
  below all ISRs. It never touches audio. It can delay the UI if it runs in the main-loop context
  (see option b2).
- **Flash writes.** Jangada's flash driver turns XIP off with interrupts disabled (`hal/fm1_flash.h`) [V].
  BT ISRs are simply held off for that time. A sector erase can take tens to hundreds of ms
  (typical NOR) [I]. Missed connection events are acceptable within the supervision timeout. Long saves
  may drop the link [I]. Mitigations: pause BLE around saves, or use a longer supervision timeout.

### 3.3 Memory headroom

- **Flash.** The app slot is `XIP LENGTH = 0x8DFBC` (581,564 B) (`firmware/app.ld`;
  `tools/fm1pkg_make.py: APP_SLOT`) [V]. The flash is only 1 MiB [V]: the VM/Felucca store starts at
  0x93000 (`fm1pkg_make.py:133`), the Felucca store is 0x97000–0xE0000, and BTIF, USR and the
  globals follow (`hal/fm1_flash.h`) [V]. A local build of Felucca 0.9 was 417,668 B
  (`fm1-emulator-boot/rust-emulator/FELUCCA.md`) [V], which leaves ~164 KB. Jangada with the SLOOP
  drum kits (acoustic samples) is larger, and its size is **unmeasured** (no build in this tree). A BLE
  stack of 120–200 KB [I] could fill or overflow the slot. **Flash may be the deciding constraint.**
  The slot cannot grow without moving the user store, which breaks compatibility with saved data.
- **RAM.** `.data/.bss` has 96 KiB (`build.py` checks it). POOL is 0x54000 (344 KiB), and `build.py`
  demands ≥ 8 KiB spare [V]. A static tally of the POOL buffers (`fx.c` delay 128 KiB, chorus 4 KiB,
  reverb ~12 KiB; `gfx.c` canvas ~58 KiB; `slicer.c` 32 KiB; `eng_grain.c` ~27 KiB) gives ~262 KiB,
  so ~80 KiB is free [I, static estimate]. That would fit a 30–55 KiB BLE heap and stacks.
  `.data/.bss` usage is unmeasured.
- **Stacks.** Jangada has one user stack (0x01C74100–0x01C7A000, ~24 KiB) and one system stack (8 KiB)
  (`app.ld`) [V]. The BT task stacks (2 KiB and 4 KiB) would be separate POOL allocations.

---

## 4. OS shim options

### 4.1 What the BT libraries import

Method: `nm` on the archives (Apple's `nm` reads the bitcode symbol tables). Symbols left undefined
by `btctrler.a + btstack.a + wl_rf_common.a + crypto_toolbox_Osize.a + lib_ccm_aes.a` were resolved
against the SDK's other libraries [V]. This is an **upper bound**: LTO in a BLE-only build will drop
many of these (Classic, TWS, Wi-Fi SDIO).

- **From `system.a` (68):** `os_current_task`, `os_current_task_rom`, `os_mutex_{create,del,pend,post}`,
  `os_sem_{create,del,pend,post,set}`, `os_taskq_pend`, `os_taskq_post_type`, `os_time_dly`,
  `task_create`, `task_kill`, `thread_fork`, `thread_kill`, `vPortYield`, `sys_timer_{add,del,modify,re_run,get_ms,get_user_data,set_user_data,add_to_task}`,
  `sys_timeout_{add,del,add_to_task}`, `usr_timeout_{add,del}`, `usr_timer_del`, `request_irq`,
  `unrequest_irq`, `bit_set_ie`, `bit_clr_ie`, `irq_read`, `malloc`, `zalloc`, `free`, `ram_malloc`,
  `ram_free`, `lbuf_*` (12), `cbuf_*` (5), `msleep`, `printf`/`puts`/`putchar`/`put_buf`/`printf_buf`/`log_print`,
  `ASCII_IntToStr`, `ASCII_StrToInt`.
- **From `cpu.a` (40):** `clk_get`, `clk_set`, `clk_get_osc_cap`, `delay`, `delay_us`, `hw_udelay`,
  `low_power_*` (13), `get_chip_id`, `doe`, `CRC16`, `chip_crc16`, `crc16_sw`, `crc_get_16bit/32bit`,
  `aes_hw_lock/unlock`, `norflash_ioctl`, `P33_SYSTEM_RESET`, `OSGetTime`, `gpio_set_uart1`/`gpio_close_uart1`,
  plus Wi-Fi/SDIO-only `wlc_*` and `sdio_host_send_command`.
- **Others:** `bt_event_notify` (event.a), `syscfg_read`, `syscfg_write`, `bt_vm_interface` (cfg_tool.a),
  `time_lapse`, `lmp_ch_update_resume_hdl`, `sdfile_get_disk_capacity`.
- **Application-supplied:** ~300 symbols, almost all `config_*`/`CONFIG_*` constants and `log_tag_const_*`
  bytes. Also `local_irq_disable`/`enable`, `__local_irq_*`, `sys_local_irq_*`, `cpu_in_irq`,
  `cpu_irq_disabled`, `jiffies`, `rf_mdm_con_ble_sync_word`, `bb_le_clk_get_time_us`, `ll_config_ctrler_clk`,
  the Wi-Fi RF trim callbacks (1.6), and linker-script symbols (`acl_rx_pool`, `tws_*_begin/end`, …).
  The `btctler_lib_*.ld` fragments define these (section 1.2).

The full raw list is reproducible with
`python3 undef.py btctrler.a,btstack.a,wl_rf_common.a,crypto_toolbox_Osize.a,lib_ccm_aes.a system.a,cpu.a,common_lib.a,event.a,update.a,cfg_tool.a,fs.a`.
The script was a one-off and is not in this repo.

**Leaf modules that can be reused as they are** [V]. Their imports are only IRQ masking, logging and
asserts:
- `system.a:lbuf.c.o` and `circular_buf.c.o`. That covers all `lbuf_*`/`cbuf_*`.
- `malloc.c.o` (imports `HEAP_BEGIN`/`HEAP_END`, `OSGetTime`).
- `mem_heap_ram.c.o` (`RAM_HEAP_BEGIN/END`).

`timer.c.o` (`sys_timer_*`) and `task.c.o` need the real OS (task_create, sem, taskq), so the shim
re-implements them. `hwi.c.o` only imports `irq_info_table`, but it programs the interrupt controller
its own way, so the shim replaces it with one that uses Jangada's `fm1_irq` tables.

### 4.2 Option (a): adopt the SDK OS (FreeRTOS in `system.a`) for the whole firmware

`system.a` contains FreeRTOS (`tasks.c`, `queue.c`, `port.c`, the `os_api.c` wrapper) and the SDK's
startup expectations: `cpu.a:startup.S`, `boot.c`, `clock.c`, power, VM/`syscfg` on its own flash
layout [V]. Jangada would have to give up its startup (`crt0.S`), vector table and fault handler
(`fm1_vec.S`/`fm1_irq.h`), and possibly its flash map. The SDK's VM sits where Felucca's store
lives (0x93000+). The audio would then run as SDK tasks and ISRs.
**Effort: very high (re-platforming). Risk: high.** This is what the stock firmware does, and it
inherits the stock problems (busy-poll, calibration time). **Not recommended.**

### 4.3 Option (b): a minimal shim on core 0 (recommended)

Implement only the imported API on top of Jangada.
- **Tasks:** 2–3 static task contexts (`btctrler`, `btstack`, and `btencry` only if LTO keeps it [I])
  with their own stacks in POOL. `os_taskq_pend`/`os_sem_pend`/`os_mutex_pend`/`os_time_dly`
  switch to the next ready context. `os_taskq_post_type` (also from ISRs) enqueues a message and marks
  the task ready. A cooperative switch happens only at call boundaries, so it saves callee-saved
  registers, `rets` and `sp`. It never runs inside a hardware `rep` or conditional block, which the
  emulator found a preemptive switch must handle (`BAUD-GIRL.md`, fix 17) [V].
- **Where tasks run (b1 or b2):**
  - **b1:** from the main loop (`bt_service()` once per loop iteration). This is simple, but BT work
    waits behind LCD redraws [I].
  - **b2:** from a low-priority software interrupt, `FM1_IRQ_SOFT0` (120, `hal/fm1_irq.h`) at
    priority 2: above TIMER5 (1), below audio (3). The ISR posts SOFT0, and the SOFT0 handler resumes
    ready task contexts until all block. This preempts the UI but never audio, without a full RTOS.
    It is a design proposal [I].
- **Timers:** `sys_timer_*`/`sys_timeout_*`/`usr_timeout_*` as a sorted list ticked from TIMER5
  (`fm1_ms`) and run in task context.
- **IRQ:** `request_irq` writes `FM1_VEC`/`FM1_ICFG` with a remapped priority and logs what was asked.
  `local_irq_*` maps to `cli`/`sti` with nesting. `cpu_in_irq` reads the core state.
- **Heap:** `malloc.c.o` from `system.a` over a POOL region (32–48 KiB).
- **Storage:** `syscfg_read/write` for the few items the stack uses: BT MAC, bonding keys
  (`sm.c.o`, `remote_device_list.c.o` call it), RF trim (`analog.c.o`, `wf_rf_*`). Store them in a
  small record in Jangada's store (`src/storage.c`), **never** through the SDK VM driver, whose region
  overlaps Felucca's store [V, flash map above]. MAC: use the factory value if `key_mac` (0xFF000)
  or BTIF (0xE9000) holds one; otherwise use a random static address [I].
- **Stubs:** `low_power_*` (never sleep), `clk_*` (fixed values), `get_chip_id` (read JL_INTEST
  CHIP_ID 0x10200; `ATOMIC.md` saw `0x6f01`) [V], `P33_SYSTEM_RESET` maps to Jangada's reboot,
  `printf`/`log_print` go to the CDC console or nowhere.

Size: ~60 functions, roughly 800–1500 lines including the context-switch asm [I].
**Effort: medium (2–3 weeks). Risk: medium.** The unknowns are the library behaviour that the
headers and docs do not describe.

### 4.4 Option (c): BT on the second core

- The stock image starts core 1: it writes the entry to 0x01C7FFF8, sets `C1_CON` bit 3 and waits for
  an acknowledgement byte (`SMP.md`) [V, from emulator analysis of the unchanged image]. Interrupt
  configuration banks and tick timers are per core (`SMP.md`) [V as emulator modelling].
- A hardware probe that tried to start CPU 1 **reset the board** (`SMP.md`, last paragraph) [V]. The
  bring-up sequence is not proven on Jangada.
- Both cores execute from the shared XIP/cache. Jangada's flash writes switch XIP off, so core 1 would
  have to be paused (`Cx_CON` bit 2, as the stock flash routine does per `SMP.md`) or run entirely
  from RAM. RAM is not realistic for 120–200 KB of code. Bus and cache contention would also slow the
  core 0 audio rendering [I].
- The SDK pins `btctrler`/`btstack` to core 0 in its dual-core configuration (1.3) [V], which suggests
  core 0 is the supported place for BT [I].

Benefit: complete CPU isolation from audio. **Effort: high. Risk: high** (undocumented SMP bring-up,
cache/XIP coordination). This is worth revisiting only if option (b) measurably hurts audio.

---

## 5. MIDI path

### 5.1 BLE-MIDI protocol (public spec, from memory; check against the MIDI Association document)

- Service `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`. One characteristic,
  `7772E5DB-3868-4112-A1A9-F2669D106BF3`, with READ (returns an empty payload), WRITE_WITHOUT_RESPONSE
  and NOTIFY. Put the 128-bit service UUID in the advertising data.
- Packet: a header byte `10tttttt` (top 6 bits of a 13-bit millisecond timestamp), then for each MIDI
  message a timestamp byte `1ttttttt` (low 7 bits) and the message. Running status is allowed within a
  packet. Realtime bytes may appear with their own timestamp byte. SysEx can span packets; a
  continuation packet starts with the header only.
- The payload size per packet is MTU − 3 (20 B by default). Negotiate a larger MTU.
  `ble_op_att_set_send_mtu` and `ble_op_set_data_length` exist in `le_trans_data.c` [V].
- Connection interval: hosts start slow (tens of ms). The peripheral should ask for 7.5–15 ms with
  `ble_op_conn_param_request` (`le_trans_data.c:237`) [V]. Apple's accessory guidelines allow
  11.25 ms for MIDI (from memory) [I].

### 5.2 Into Jangada

The current input path [V]:
- `usb.c:ep1_rx` and `midi_uart.c:um_byte` push 4-byte USB-MIDI event packets
  (`CIN | status<<8 | d1<<16 | d2<<24`) into `midi_in_q[MQ=64]` with `RING_PUBLISH()`.
- Both producers run in the TIMER5 ISR, so they never race each other.
- `seq.c:events_block()` drains the ring once per 32-sample block **inside the audio ISR** and routes
  note-on/off by channel (`midi_route`, `midi_track`). Other messages are ignored.

Proposed:
- **In:** the BT `att_write_callback` (btstack task context) parses BLE-MIDI into the same 4-byte
  packets. It pushes them into a **separate SPSC ring `ble_in_q`**, because a third producer outside
  TIMER5 context would race `mi_w`. `events_block` drains `ble_in_q` next to `midi_in_q`, or TIMER5
  copies it across. Timestamps can be ignored at first. For de-jitter later: map the 13-bit timestamp
  to `fm1_ms`, delay by a fixed ~1 interval, and release the event in the right 32-sample block.
- **Out:** `midi_out_event()` (audio ISR) writes `midi_out_q` only when `usb.config` is set [V].
  Duplicate into a `ble_out_q` when a BLE central has subscribed (CCC). The btstack task batches
  pending messages into one notification per connection event, with a header and timestamp bytes
  from `fm1_ms & 0x1FFF`.
- **SysEx** (the M-UPGRADE editor and update path in `ota.c`/`usb.c`) over BLE is possible later.
  Leave it out of v1.
- **Latency [I]:** BLE adds on average half a connection interval plus scheduling: ~4–8 ms at 7.5–15 ms,
  with jitter up to one interval when timestamps are ignored. Jangada's own path adds
  0.73 ms block granularity plus the 5.8–11.6 ms I2S double buffer, the same as USB today.

---

## 6. Phased plan

| Phase | Work | Effort [I] | Exit criterion |
| --- | --- | --- | --- |
| **0: hardware gate** | Fetch the full SDK (BT headers, `.ld` fragments). Install the toolchain. Build stock `demo_ble` (`TRANS_DATA_EN`). Package it for the FM-1 and flash it, keeping a recovery path (USB update mode, `fm1_enter_uboot`). Connect from a phone (nRF Connect). Record `BTCTLER_*_TOTAL`/`BTSTACK_*_TOTAL` from the map. | 2–4 days | Advertising seen and connection stable on FM-1 hardware (antenna, RF trim, MAC OK). Real flash and RAM numbers. |
| **1: BLE-MIDI on the SDK** | Turn `demo_ble` into a BLE-MIDI peripheral (GATT db, adv, CCC, parser and encoder). Test with macOS Audio MIDI Setup and iOS. Wrap `request_irq` to log priorities and cores. Use `IRQ_TIME_COUNT_EN`/`dump_cpu_irq_usage` (`hwi.h`) for ISR time. | 3–5 days | Notes in and out work. Measured ISR durations and requested priorities. |
| **2: shim** | Option (b) in Jangada: a `firmware/bt/` directory with tasks, timers, irq, syscfg and stubs. Change the link to LTO (`lto-wrapper`). Build-time switch `FELUCCA_BLE`, default 0. | 2–3 weeks | Jangada boots with BLE, advertises, connects. Audio meter (`cpu_q8`, `late`, `max_us`) unchanged within noise. |
| **3: MIDI integration** | `ble_in_q`/`ble_out_q`, routing, UI (SYSTEM page: BLE on/off, name, connected state). | ~1 week | Plays from a DAW over BLE. Keys go out over BLE. |
| **4: hardening** | Bonding storage, reconnect, flash-save interplay (pause or extend supervision timeout), calibration cache, watchdog during calibration, long soak with heavy patches. | 1–2 weeks | 8 h soak with no dropout or disconnect. |

### Tests

- **Host-testable** (in the style of `tests/midi_uart_test.c`):
  - BLE-MIDI parser and encoder: timestamps and wrap, running status, interleaved realtime, SysEx
    continuation, malformed packets;
  - ring hand-off;
  - shim timer list and message queue semantics (the context switch replaced by direct calls or
    ucontext on the host).
- **Emulator** (`fm1-emulator`): it has no radio model (`radio.rs` keeps register values only;
  `BAUD-GIRL.md`) [V]. Use it to show that a `FELUCCA_BLE=1` image with BLE **disabled at runtime**
  still boots and renders the same audio. A BLE-enabled boot would loop in RF calibration or HCI
  wait, as the stock image does.
- **Hardware only:** RF, connection, latency (loopback: BLE note-in → audio onset, scope), the audio
  dropout counter `felucca_dbg.late`, CPU meter deltas, and flash-save behaviour.

### Risks

| Risk | Likelihood [I] | Mitigation |
| --- | --- | --- |
| Controller needs IRQ priority or latency that conflicts with the 5 ms audio ISR | medium | Put BT IRQs above audio; measure in Phase 1; fallback option (c) or a smaller `HALF_FRAMES` |
| Flash slot too small for Jangada + kits + BLE | medium–high | Measure in Phase 0; trim kits or samples; repartition (breaks saved data) |
| RF trim or MAC data missing on the FM-1 (Jangada's package layout may have erased the SDK VM items) | medium | Phase 0 shows it; fresh calibration plus a random static address |
| Undocumented library behaviour (busy polls, assumptions about FreeRTOS semantics) | medium | Shim-provided `cbuf_read` yields; logging shim; Phase 1 on the real SDK first |
| GPL distribution problem | certain if distributed | Section 7 |
| Upstream headers or `.ld` fragments differ from the libs in this checkout | low | Use one SDK tag for both |

---

## 7. Licensing

Jangada's code is GPL-3.0-only (`LICENSING.md`) [V]. Apache-2.0 is GPLv3-compatible, so the
**licence terms** do not conflict. The problem is **source availability**: `btctrler.a`, `btstack.a`
and `wl_rf_common.a` are binary-only bitcode. GPLv3 section 1 requires Corresponding Source for every
part of a conveyed object code. These libraries are not a "System Library" in GPLv3's sense [I, this
is a legal reading, not legal advice].

- **Private use** (build and flash your own device) is not restricted by the GPL.
- **Distributing** a BLE-enabled image or package requires an additional permission under GPL
  section 7. It must allow linking with the JieLi BT libraries and come from **all** copyright
  holders of the code involved: Leo Kuroshita / Hügelton Instruments for Felucca, plus the Jangada
  contributors. This is the same mechanism `LICENSING.md` already uses for the Felucca Assets.
- Debug symbol names in `btstack.a` resemble BlueKitchen BTstack (`btstack_run_loop_embedded_*`,
  `hci_transport_h4_*`, `att_db.c`, `sm.c`) [V]. How JieLi licensed that code is unknown. Check it
  before any public release [I].
- Keep `FELUCCA_BLE` off by default so the published GPL build stays clean.

A GPL-clean fallback: an external BLE-MIDI module on a UART (the TRS MIDI UART path already exists in
`midi_uart.c`). That needs a hardware modification.

---

## 8. Recommendation

**Go with conditions.**

1. **Phase 0 hardware gate first.** Stock `demo_ble` must advertise and hold a connection on an FM-1.
   If the radio does not come up on this board, stop.
2. **Measure before writing the shim:** real BLE flash and RAM from the SDK map, ISR durations and
   requested IRQ priorities (Phase 1). If the BLE code does not fit the 581,564 B slot alongside
   Jangada and its kits, decide on content cuts before investing further.
3. **Architecture:** option (b), a minimal shim on core 0 with BT ISRs above the audio ISR and BT tasks
   as cooperative contexts resumed from SOFT0 (b2). Reuse the leaf modules of `system.a` (`lbuf`,
   `cbuf`, `malloc`). Keep Jangada's own flash store for MAC, bonding and RF trim.
4. **Licensing:** keep BLE behind `FELUCCA_BLE=0` by default. Do not distribute BLE images without a
   section 7 exception from all copyright holders.

**The single biggest unknown:** what interrupt priority and latency the closed BLE controller
actually needs. Nothing in the bitcode or headers documents it, and only hardware can measure it.
It decides whether the controller can share core 0 with an audio ISR that holds the CPU for up to
~4.9 ms per 5.8 ms half-buffer. That in turn decides between the medium-effort shim (b) and the
high-risk second-core route (c).

---

## 9. Update 2026-10-08: stock firmware reference and three measured routes

This section supersedes the size estimates in §1.5 and the open questions in §0 and §8 where they
conflict. Tags: **[M]** measured (link map, emulator trace or RAM dump), **[I]** inferred, **[S]**
published source. Nothing in this section ran on hardware. The working files of these studies (scripts,
link maps, IR excerpts, prototype code) were throwaways and are not part of this repository; the facts they produced are recorded here and in `BLE-HW-FACTS.md`.

### 9.1 The FM-1 does BLE MIDI from the factory

- M-VAVE's FM-1 spec page lists "MIDI: USB MIDI · Bluetooth BLE MIDI · 3.5mm MIDI IN (all three
  usable simultaneously)" [S]. The radio and antenna are populated, so the Phase 0 hardware gate (§6, §8.1)
  is answered by the vendor. The hardware scan in §9.6 only records the details.
- The stock `.fwsc` packages are **not** opaque (correcting the Sources paragraph above): FM-1-RE
  decrypts them with jl-misctools, chip key 38927 = `0x980F` [M]. The M-VAVE SMK-37 Pro uses the same key and
  the same code base (`amalahama/smk37-firmware-custom-mod`). Stock V13, V14 and V15
  (`fm1-firmware/FM-1.fwsc`, SHA-256 `db1642b2…`, = FM-1-RE `STOCK_V15_SHA256`) all contain the
  BLE-MIDI profile [M].
- Facts only: no stock code is copied into GPL firmware.

### 9.2 What stock V15 does (reference behaviour)

The app loads at `0x02000120` (address = file offset + 0x120) [M]. All values below are from V15.

| Item | Stock behaviour | Tag |
| --- | --- | --- |
| Enable | BLE starts at every boot, with no stored setting on the path. Holding **HOME ≈1 s** toggles BT off/on (button table `0x0204EF98`, entry 8 `0x0201FD72`). The flag lives in RAM, so a power cycle restores BLE | M (emulator); power cycle I |
| Name | **`FM-1_BLE`**, built in code ("FM-1" + "_BLE"). The SDK default names in `cfg_tool.bin` are not used | M |
| Advertising data (28 B) | `02 01 06` · `11 07` + BLE-MIDI service UUID · `06 FF 73 69 6E 63 6F` (manufacturer data "sinco", no real company ID) | M |
| Scan response | `09 09 "FM-1_BLE"` | M |
| Advertising | Interval 0xA0 (100 ms), connectable undirected, channels 37/38/39 | M values |
| GATT | GAP 0x01–0x03; vendor 0xAE40 (AE41 write-no-rsp, AE42 notify + CCCD 0x65); BLE-MIDI service 0x70, characteristic `7772E5DB-…` 0x72 (read, write-no-rsp, notify), CCCD 0x73. No 0x1801 / Service Changed | M |
| Security | No attribute needs security. A Just Works request is confirmed automatically, and the device does not start pairing itself | M code / I |
| MTU | Offers 517; payload per notification = min(MTU−3, 512) − 5 | M |
| Connection parameters | Requested when notifications are enabled: {6, 9, 0, 100} (7.5–11.25 ms, 1 s timeout), retried once with {12, 12, 0, 100} | M |
| Timestamps | Not generated: echoes the last received header/timestamp, or sends `0x80 0x80` | M |
| Routing | BLE in → synth only. BLE out = the FM-1's own key/knob stream, the same as USB out. No bridging or thru | M (boot config) |
| Vendor channel | AE41/AE42 carry a command protocol (0x11–0x30); command 0x14 injects MIDI. Probably the M-VAVE app/OTA channel | M / I |
| Interrupts | Audio IRQ 11 at prio 3. BT IRQ 40/41 and BLE IRQ 45 (event) / 29 (RX) at prio 2. **All on CPU 0** | M (emulator) |
| Stored ids | 102 BT MAC, 104 BLE MAC, 601/615 packed (probably TX power), 602/603/652 unknown | M ids / I meaning |

Lessons for our implementation: copy the compatible parts (name style, MIDI service, the
connection-parameter request after the CCCD write, no required pairing). Improve on stock by adding
real BLE-MIDI timestamps and a Service Changed characteristic, since iOS caches GATT tables [I].

### 9.3 Size by route (all link-measured with the JieLi clang 4.0.1, `-Oz -flto`)

**Route A: the SDK stack (btctrler + btstack), trimmed** [M from maps; none of the variants ran]

- Felucca's `demo_ble` reproduces exactly: 280,576 B flash, 39,212 B static RAM. Bluetooth's own
  share, measured as the difference from the same build without BT, is 136.6 KB. Much of the rest is
  FAT (≈45 KB) and log strings (≈35 KB).
- Smallest **supported** config (LE peripheral only, no GATT client, no SM, logging off, BIG_FLASH 0,
  one link): **83.9 KB flash, 9.4 KB static RAM** for BT.
- With ≈2,000 Classic/ISO/ext-adv/scan/master functions replaced by empty stubs (an unsupported
  config that has never run): **67.4 KB, 3.95 KB**. Just Works SM adds ≈13 KB.
- `wl_rf_common` cannot be removed: dropping `wifi_conf.c` breaks the link, because RF init is shared with
  Wi-Fi.

**Route B: JieLi controller only + our own host over HCI** [M]

- The controller has a clean in-memory HCI boundary:
  - **in:** `hci_send_cmd_payload` and `le_hci_send_acl_packet`;
  - **out:** `int hci_packet_handler(type, pkt, size)`, which may run in ISR context.
- `btctrler.a` links without `btstack.a`.
- **Precondition:** `config_btctler_hci_standard` must be 1, otherwise `hci_send_cmd` asserts. This
  is unproven in normal mode.
- **Controller + RF + CCM:** 70.8 KB flash, 2.6 KB static RAM, ≈15 KB heap/stack at runtime [I].
  It needs 87 external symbols: OS (one "btctrler" task with a queue, mutex, sem, sys timers), IRQ,
  malloc, clocks/power, `syscfg_write`, the Wi-Fi trim hooks and a 2 KB `wl_rfd_ram_lut`.
- **Host options:**
  - **Hand-written minimal ATT host:** 2.7 KB flash, 0.7 KB RAM, about 4–6 KB once hardened [I]. Its
    host-side tests pass under ASan/UBSan; it is untested with real centrals.
  - **Apache NimBLE, peripheral only:** about 35 KB with its OS port, +7 KB with legacy SM. It
    compiles unchanged with the JieLi clang.
- **Totals:** ≈75–80 KB with the minimal host, ≈106–110 KB with NimBLE.

**Route C: no JieLi libraries, our own link layer** [M for register facts; sizes I]

- The bitcode keeps its debug info (`llvm-dis --disable-auto-upgrade-debug-info`). It gives every
  MMIO address and value, the per-link control block layout (`struct ble_param`, 324 B), the IRQ
  setup and the init sequences. Some control-bit meanings stay unknown.
- The baseband is a **link-controller engine** (anchors, hop algorithm #1, AA/CRC, ack and
  retransmit with alternating TX/RX buffers, the event counter and instants, advertising with an
  automatic scan response). It is not a raw radio, which keeps the LL small. Encryption is software
  AES-CCM; a hardware AES block exists.
- **Hardest part:** RF bring-up goes through the Wi-Fi RF init and closed-loop calibration (VCO bank scan,
  IQ/DC, temperature retrim every 3 s). The hope is to reload the stored trim; only hardware can tell.
- **Estimate:** 15–35 KiB flash, 5–9 KiB RAM; 3–6 person-months.
- **Licence:** GPL-clean, because only our own code ships.
- **Clean-room rule:** one person documents the facts, another implements from the Core spec plus
  those facts.

| Route | BT flash | BT static RAM | Runtime heap/stack | Distributable under GPL | Main risk |
| --- | --- | --- | --- | --- | --- |
| A: SDK stack, supported trim | 83.9 KB [M] | 9.4 KB [M] | ≈30 KB [I] | No (closed libs) | Shim for ≈110 symbols |
| A: SDK stack, stubbed | 67.4 KB [M] | 3.95 KB [M] | ≈30 KB [I] | No | Unsupported, never ran |
| **B: controller + minimal host** | **≈75–80 KB** [M+I] | ≈4 KB | ≈15 KB [I] | No (closed controller) | Raw-HCI mode unproven |
| B: controller + NimBLE | ≈106–110 KB | ≈9 KB | ≈18 KB | No | Size |
| **C: own link layer** | **≈15–35 KiB** [I] | ≈5–9 KiB [I] | included | **Yes** | RF bring-up and calibration |

Optimist's default build has ≈0 B flash free, so routes A and B mean a "BLE edition" that drops one
big feature: about the size of the PERC-to-DRUM swap (−67,812 B, `ENGINE-PLUGINS.md` §4.1). Route C
might fit with small cuts.

### 9.4 Licence note

The ac79-sdk declares Apache-2.0 for "this SDK" (`README-en.md:496`, root `LICENSE`) [M]. It does
not say whether that covers the binary libraries; §7 did not consider this. **Ask JieLi** whether the
`.a` libraries are under Apache-2.0 and whether source is available. If so, the routes A/B
distribution problem may go away (a GPL section 7 exception would still be cleanest for binary-only
parts [I, not legal advice]).

### 9.5 Timing against audio (the §8 "single biggest unknown"), partly answered

- Stock runs the BLE event and RX ISRs at prio 2 on CPU 0, below its audio ISR (prio 3) [M], and it
  works for M-VAVE.
- Our audio ISR holds CPU 0 for up to ≈4.9 ms per 5.8 ms half-buffer. At prio 2, CONNECT_IND
  handling could miss the first anchor, which is about 1.25–2.5 ms away [I].
- Routes B and C let our shim choose the priority (we own `request_irq`), so the two short BLE ISRs
  can run above audio. Their duration is unmeasured.
- **The emulator can measure the ISR budget** once `wireless.rs` gets a behavioural engine model.

### 9.6 Hardware test script (stock V15, nRF Connect), about 15 minutes

1. Flash `fm1-firmware/FM-1.fwsc` (stock V15). Scan for **`FM-1_BLE`** or the service
   `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`. Record:
   - the address and its type;
   - the RSSI at 1 m;
   - the raw advertising and scan-response bytes (compare with §9.2).
2. Do a classic scan too: does a BR/EDR "FM-1" appear?
3. If nothing appears: power-cycle and rescan, then hold HOME 2 s and rescan. Record any screen or LED change.
4. Connect: record the services and handles, and whether pairing is requested.
5. Enable notifications on `7772E5DB…`: record the parameter update requested, the final interval, and the MTU after requesting 517.
6. FM-1 → phone: play keys and knobs. Record the notification hex (expect `80 80` headers) and check that USB out matches.
7. Phone → FM-1: write `80 80 90 3C 64`, then `80 80 80 3C 00`. Expect a note. Confirm USB notes are not bridged to BLE.
8. Disconnect: does advertising resume? Note the time from boot until the device appears.
9. Play dense chords while streaming BLE: listen for clicks (CPU 0 sharing).

Optional: an nRF52840 dongle as a sniffer, needed later for route C.

### 9.7 Recommendation (replaces §8)

1. **Run the §9.6 hardware test** as soon as the device is available.
2. **Main path: route B with the minimal host**, privately at first. It is the fastest way to a
   working link and about 75–80 KB, built as a builder "BLE edition".
   - First gate: raw HCI (`config_btctler_hci_standard = 1`) must reset, advertise and connect on a
     plain SDK build.
   - If that fails, fall back to route A's supported trim (83.9 KB).
3. **Route C as a time-boxed spike (≤3 weeks, needs the device and a sniffer):**
   - **Steps:** trace the stock RF init, then send a non-connectable advertisement from our own code
     using the stored trim.
   - **Continue** if advertisements are clean on 37/38/39.
   - **Stop** if live Wi-Fi calibration is needed and cannot be reproduced.
   - It is the only route that is GPL-distributable and small enough for the default build. Route B
     doubles as its reference oracle.
4. **Emulator prep now (no device needed):**
   - a behavioural BLE engine model in `wireless.rs` (anchors, IRQ 45/29, buffer toggles);
   - a scripted central;
   - an ISR-budget measurement against audio;
   - stock RF-init traces;
   - spec-vector unit tests (CRC24, hop #1, AES-CCM, SM c1/s1).
   - Later: a virtual BLE link that bridges the emulated GATT to macOS through CoreBluetooth.
5. **Ask JieLi** about the library licence (§9.4).
