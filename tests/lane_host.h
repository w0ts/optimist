/* SPDX-License-Identifier: GPL-3.0-only */
/* The host harness the drum lane tests share (tests/lane_auto_test.c, lane_lfo_test.c): every switch of the automation
 * store on, a simulated NOR with the section log and the patterns, the editor's reply builder, ed_stepx.c, ed_sl24.c and
 * MISS's scan. Include first. */
#ifndef LANE_HOST_H
#define LANE_HOST_H
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_PATTERNS 1
#define FELUCCA_MOTION 1
#define FELUCCA_PLOCK 1
#define FELUCCA_MICRO 1
#define FELUCCA_FILLS 1
#define FELUCCA_CHANCE 1
#define FELUCCA_SL24_EXPORT 1
#include <sys/mman.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"
#include "../firmware/src/storage/motion_flash.c"

static struct { int force; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/storage/sections/sections.c"
#define AUTO_VIEW_PROJ 1
#include "auto_view.h"

#include "../firmware/src/io/editor/ed_out.h"
static uint8_t ed_out[ED_OUT_N];
static uint32_t ed_n;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out - 1u) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_v(int32_t v) { uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192); ed_b(u); ed_b(u >> 7); }
static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
#include "../firmware/src/io/editor/ed_stepx.c"
static void ed_pack7(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(p[j] >> 7) << j;
        ed_b(m);
        for (j = 0; j < k; j++)
            ed_b(p[j]);
        p += k, n -= k;
    }
}
typedef struct { uint8_t palette, lowcut, zoom; } b26_settings_t;
static b26_settings_t settings;
typedef struct { uint32_t magic; uint8_t btn[14], enc[7]; int8_t dir[7]; } panel_t;
static panel_t panel;
static uint32_t bp23_word(void) { return 0x100u; }
static project_t autosave_buf;
static dlrec_t autosave_dl;
#include "../firmware/src/io/editor/ed_sl24.c"
#define MISS_SCAN_ONLY 1
#include "../firmware/src/storage/miss.c"
#endif
