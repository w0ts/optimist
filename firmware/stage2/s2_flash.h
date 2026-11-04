/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD flasher's logic, no hardware (docs/SD-CARD-DESIGN.md "Flashing from the card"): a .fwsc read through
 * struct s2_src, checked, then its flash.bin written over [0x4000, its end) through m-bootloader's struct core_nor.
 * tests/stage2_test.c runs this on the host. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "core.h"

#define S2_SECTOR 0x1000u
#define S2_APP_LO 0x4000u          /* the boot area below is never written */
#define S2_STORE_TOP 0xE0000u      /* no layout's app passes it (tools/flash_layout.py STORE_TOP) */
#define S2_ID_MAX 20u
#define S2_LABEL_MAX 20u          /* the screen's columns */
#define S2_LFN_MAX 32u

enum s2_err {
    S2_OK,
    S2_E_READ,      /* the card failed */
    S2_E_NOT_PKG,   /* not a package */
    S2_E_NOT_FM1,   /* a package for another model */
    S2_E_NO_FLASH,  /* no flash.bin in it (an updater-only package, m-bootloader's installer) */
    S2_E_SIZE,      /* its flash.bin is no app slot */
    S2_E_CRC,       /* damaged */
    S2_E_KEY,       /* built for another chip key */
    S2_E_LAYOUT,    /* would switch the flash layout: USB's job, it clears what the other layout left */
    S2_E_FLASH      /* an erase, a write or a read back failed */
};

struct s2_src {
    void *ctx;
    bool (*read)(void *ctx, uint32_t offset, void *dst, uint32_t size);  /* raw bytes of the .fwsc */
};

struct s2_pkg {
    uint32_t offset, size;         /* flash.bin in the logical image (the package without its marker bytes) */
    uint16_t crc;
    char id[S2_ID_MAX + 1];        /* "FM-1_720" */
};

/* A file's long name, from its directory entries as they pass. */
struct s2_lfn {
    char text[S2_LFN_MAX + 1];
    uint8_t sum;
    bool live, valid;
};

typedef void (*s2_progress)(void *ctx, uint32_t done, uint32_t total);

const char *s2_err_text(enum s2_err e);
/* A name from the card's directory that is a package: 8.3, so a long NAME.fwsc reads NAME~1.FWS. */
bool s2_is_firmware(const char *name);
/* The root folder holding the packages, by its 8.3 name: FIRMWARES reads FIRMWA~1, FIRMWARE as itself. */
bool s2_is_folder(const char *name);
/* Each 32-byte entry in order: long-name pieces, then the short entry they belong to, which sets l->valid. */
void s2_lfn_feed(struct s2_lfn *l, const uint8_t entry[32]);
/* What the list shows: the long name without .fwsc, cut to the screen, else the short name. */
void s2_label(char out[S2_LABEL_MAX + 1], const struct s2_lfn *l, const char *short_name);
/* Finds flash.bin. file_size: the .fwsc's. */
enum s2_err s2_pkg_open(const struct s2_src *src, uint32_t file_size, struct s2_pkg *pkg);
/* The whole flash.bin against its CRC; buf holds S2_SECTOR bytes. */
enum s2_err s2_pkg_verify(const struct s2_src *src, const struct s2_pkg *pkg, uint8_t *buf, s2_progress cb, void *ctx);
/* The chip key the package was built for. */
enum s2_err s2_pkg_key(const struct s2_src *src, const struct s2_pkg *pkg, uint8_t *buf, uint16_t *key);
/* Where the installed firmware's slot ends, from its app_area_head's raw end (tools/fm1_layouts.py installed_end);
 * 0 for 0, no firmware mounted. */
uint32_t s2_slot_end(uint32_t head_end);
/* Whether this package may be written here: the unit's key, the same slot as the installed firmware (old_end 0: none
 * mounted, as after a write that broke off). */
enum s2_err s2_pkg_fits(const struct s2_pkg *pkg, uint16_t pkg_key, uint16_t unit_key, uint32_t old_end);
/* Writes the sectors that differ, each read back; the first app sector last, so a write that breaks off leaves no
 * firmware that mounts. fresh and held hold S2_SECTOR bytes each. */
enum s2_err s2_install(const struct s2_src *src, const struct s2_pkg *pkg, const struct core_nor *nor, uint8_t *fresh,
                       uint8_t *held, s2_progress cb, void *ctx);
