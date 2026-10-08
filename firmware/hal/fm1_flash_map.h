/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The FM-1 flash map as Optimist may write it: plain macros (no code, no data), so the RAM flash driver
 * (fm1_flash.h), the storage (storage.c) and the host tests all check the same ranges. docs/MEMORY-MAP.md.
 *
 * Allowed (Optimist's own data): the main store 0x97000..0xDFFFF, the globals 0xFC000..0xFEFFF, the drum records
 * 0xE5000..0xE6FFF and, with FELUCCA_UP_FM6, the user presets' FM6 voices 0x95000..0x96FFF (0x93000..0x94FFF: the
 * app slot's room to grow, kept free). The update staging 0xE0000..0xE4FFF is the update's own (ota.c), not the
 * store's.
 *
 * Never (FL_NEVER, refused even when an allow-list says yes):
 *   0xE7000..0xE7FFF  retired: UP_FM6's old copy A (before fix/upfm6-off-vm); also SLOOP 2.4's USR4 head
 *   0xE8000..0xE8FFF  the stock firmware's SDK VM (its settings and the radio calibration, magic 55 AA AA 55)
 *   0xE9000..0xE9FFF  the SDK's BTIF
 *   0xEA000..0xFBFFF  the SDK's USR area (the stock firmware's resources)
 *   0xFF000..0xFFFFF  key_mac
 * The update loader (firmware/loader) refuses FL_SDK_SYS (VM, BTIF, key_mac) and the retired sector too. */
#pragma once
#include <stdint.h>

/* [off, off + n) inside [lo, hi), without wrapping: off + n can overflow, and
 * the 1 MiB part ignores the high address bits, so a wrapped range lands low. */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
/* [off, off + n) touches [lo, hi) (n > 0), without computing off + n */
#define FL_HITS(off, n, lo, hi) ((uint32_t)(off) < (hi) && \
                                 ((uint32_t)(n) > (lo) || (uint32_t)(off) > (lo) - (uint32_t)(n)))

#define FL_APP_LO       0x00004000u                /* the app slot (fm1pkg_make.py FLASH_SIZE, ldr_core.c) */
#define FL_APP_HI       0x00093000u
#define FL_UPF_LO       0x00095000u                /* FELUCCA_UP_FM6: OBJ_UPFM6, A/B (storage.c, upreset.c) */
#define FL_UPF_HI       0x00097000u
#define FL_DATA_LO      0x00097000u                /* Felucca main store */
#define FL_DATA_HI      0x000E0000u
#define FL_OTA_LO       0x000E0000u                /* M-UPGRADE loader staging, ota.c */
#define FL_OTA_HI       0x000E5000u
#define FL_DLANE_LO     0x000E5000u                /* the projects' drum records (src/drum_store.c), A/B */
#define FL_DLANE_HI     0x000E7000u
#define FL_OLD_UPF_LO   0x000E7000u                /* retired: UP_FM6's old copies A (0xE7000) and B (0xE8000) */
#define FL_OLD_UPF_HI   0x000E9000u
#define FL_VM_LO        0x000E8000u                /* the stock firmware's SDK VM: never written */
#define FL_VM_HI        0x000E9000u
#define FL_BTIF_LO      0x000E9000u                /* the SDK's BTIF */
#define FL_BTIF_HI      0x000EA000u
#define FL_USR_LO       0x000EA000u                /* the SDK's USR area */
#define FL_USR_HI       0x000FC000u
#define FL_GLOB_LO      0x000FC000u                /* Felucca superblock / globals */
#define FL_GLOB_HI      0x000FF000u
#define FL_KEYMAC_LO    0x000FF000u                /* key_mac */
#define FL_KEYMAC_HI    0x00100000u

/* the SDK's own sectors: nothing of ours, the update loader included, erases or programs them */
#define FL_SDK_SYS(off, n) (FL_HITS(off, n, FL_OLD_UPF_LO, FL_BTIF_HI) || FL_HITS(off, n, FL_KEYMAC_LO, 0xFFFFFFFFu))
/* ... and for the app: the SDK's USR area too */
#define FL_NEVER(off, n) (FL_SDK_SYS(off, n) || FL_HITS(off, n, FL_USR_LO, FL_USR_HI))

/* Felucca's own store (projects, user samples; settings; the projects' drum records; with FELUCCA_UP_FM6 the
 * user presets' FM6 voices in the two sectors before the main store) */
#if defined(FELUCCA_UP_FM6) && FELUCCA_UP_FM6
#define FL_STORE_OK(off, n) (!FL_NEVER(off, n) && \
                             (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || \
                              FL_IN(off, n, FL_DLANE_LO, FL_DLANE_HI) || FL_IN(off, n, FL_UPF_LO, FL_UPF_HI)))
#else
#define FL_STORE_OK(off, n) (!FL_NEVER(off, n) && \
                             (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || \
                              FL_IN(off, n, FL_DLANE_LO, FL_DLANE_HI)))
#endif
