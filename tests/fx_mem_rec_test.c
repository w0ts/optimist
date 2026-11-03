/* SPDX-License-Identifier: GPL-3.0-only */
/* FX MEMORY in a project (2026-11-01; fx/fx_memsz.h, fx_rec.c FXZ_TLV): FULL everywhere keeps nothing; DLY 1/4, REV SMALL,
 * XDLY HALF round trip; a project from before (no TLV) and a new one read FULL; a value past the build's choices (a
 * project from a build with a longer line) is the smallest; PROJECT SAVE / LOAD keeps them.
 * Built as fx_mem_test (run_tests.sh). */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
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

static struct { int force, redraw; } ui;   /* (redraw: sections.c since LCD_DIRTY went, 26c6b7ac) */
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
#if FXZ_DLY_N != 4u || FXZ_REV_N != 2u || FXZ_XDL_N != 2u
#error "fx_mem_rec_test: built as fx_mem_test (run_tests.sh)"
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("fx_mem_rec: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static int is(int d, int r, int x) { return song.g[G_FXZ_DLY] == d && song.g[G_FXZ_REV] == r && song.g[G_FXZ_XDL] == x; }

int main(void)
{
    uint8_t b[FXR_MAX + 8u];
    uint32_t n;
    host_tracks_init();
    (void)fxr_decode(0, 0, 1);
    check("a new project: every line FULL, nothing kept for it", is(0, 0, 0) && fxr_encode(b) == 0u);
    song.g[G_FXZ_DLY] = 2, song.g[G_FXZ_REV] = 1, song.g[G_FXZ_XDL] = 1;
    n = fxr_encode(b);
    song.g[G_FXZ_DLY] = song.g[G_FXZ_REV] = song.g[G_FXZ_XDL] = 0;
    check("DLY 1/4, REV SMALL, XDLY HALF: kept in the FX record, decoded back (a load)", n && fxr_decode(b, n, 1) && is(2, 1, 1));
    check("... a song section (all 0): the sizes playing kept", fxr_decode(b, n, 0) && is(2, 1, 1));
    check("no record (a project from before): every line FULL", !fxr_decode(0, 0, 1) && is(0, 0, 0));
    fxr_head(b);
    b[FXR_HEAD] = FXZ_TLV, b[FXR_HEAD + 1u] = 3u, b[FXR_HEAD + 2u] = 9u, b[FXR_HEAD + 3u] = 7u, b[FXR_HEAD + 4u] = 0;
    check("a size past this build's choices (another build's longer line): its smallest", fxr_decode(b, FXR_HEAD + 5u, 1) &&
          is((int)FXZ_DLY_N - 1, (int)FXZ_REV_N - 1, 0));
    b[FXR_HEAD + 1u] = 1u;
    check("a shorter TLV (DLY only): the rest FULL", fxr_decode(b, FXR_HEAD + 3u, 1) && is((int)FXZ_DLY_N - 1, 0, 0));
    /* PROJECT SAVE / LOAD */
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    song.playing = 0, transport_req = 0;
    host_tracks_init();
    fxs_set(FXS_DEF);
    song.g[G_FXZ_DLY] = 1, song.g[G_FXZ_REV] = 1, song.g[G_FXZ_XDL] = 0;
    project_save(3);
    check("PROJECT SAVE with DLY 1/2, REV SMALL: saved", !strcmp(last_msg, "SAVED"));
    song.g[G_FXZ_DLY] = song.g[G_FXZ_REV] = song.g[G_FXZ_XDL] = 0;
    project_save(4);
    project_load(3);
    check("... LOAD it: DLY 1/2, REV SMALL back", is(1, 1, 0));
    project_load(4);
    check("... LOAD one saved at FULL: FULL", is(0, 0, 0));
    printf("fx_mem_rec_test: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
