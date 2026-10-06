/* SPDX-License-Identifier: GPL-3.0-only */
/* Every sound source of this build has something to pick (a modular build must never leave one empty):
 *   - every synth engine built has at least one factory preset the PRESETS list reaches (ui.c BANK as this build
 *     resolves it: the engine built, the preset in its table, its sample set built: preset_playable);
 *   - every drum source built (the sampled kits, the synthesised kits, each X0X kit) has at least one kit on the
 *     kit list (drum_kit_built), and the power-on kit is one of them.
 * Built by tests/run_tests.sh on several configurations (the defaults, every engine and kit, sample headers with
 * sets left out) and by tests/builder_test.py on random builder configurations (tools/builder/verify.py
 * preset_cover). Prints one line per source:  cover: <ENGINE> <presets in its table> <reachable>  */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint16_t screen[240*240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ uint32_t i,j; assert(x+w<=240 && y+h<=240); for(j=0;j<h;j++) for(i=0;i<w;i++) screen[(y+j)*240+x+i]=p[j*w+i]; }
#include "../firmware/src/gfx.c"
static void lcd_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c)
{ uint32_t i,j; assert(x+w<=240&&y+h<=240); for(j=0;j<h;j++)for(i=0;i<w;i++)screen[(y+j)*240+x+i]=swap16(c); }
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
static uint8_t fm1_led[16], fm1_led_dim[16];
#if FELUCCA_LIGHTS
static uint8_t fm1_led_bg[16];
static uint16_t fm1_led_bg_ns;
#endif
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
static void fm1_wdt_feed(void) {}
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#define FELUCCA_ICONS 1
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/ui_drums.c"
static int project_used(uint32_t i) { return i < 2; }
static void project_save(uint32_t i) { (void)i; }
static void project_load(uint32_t i) { (void)i; }
static void arrangement_save(void) {}
static uint32_t arrangement_ready(void) { return 3; }
static void arrangement_apply(uint32_t s) { (void)s; }
static void song_backup(void) {}
static void song_restore(void) {}
static void section_store(uint32_t s) { live_sec = (int8_t)s; }
static void section_load(uint32_t s) { live_sec = (int8_t)s; }
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return 1; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { (void)k; b[0] = 0; }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; }
static void settings_save(void) {}
#include "../firmware/src/ui_song.c"
#include "../firmware/src/ui_studio.c"
#include "../firmware/src/ui_fm6.c"
#include "../firmware/src/icons.c"
#define PROJ_HOST 1
#include "../firmware/src/project.c"
#include "../firmware/src/miss.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_overview.c"
#include "../firmware/src/ui_layers.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/fm6_store.c"
static uint8_t kit_nor[0x2000];
static int st_read(uint32_t off, void *dst, uint32_t n)
{ if (off < 0xDA000u || off + n > 0xDC000u) return -1; memcpy(dst, kit_nor + off - 0xDA000u, n); return 0; }
static int st_erase(uint32_t off) { if (off < 0xDA000u || off >= 0xDC000u) return -1; memset(kit_nor + off - 0xDA000u, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{ uint32_t i; if (off < 0xDA000u || off + n > 0xDC000u) return -1; for (i = 0; i < n; i++) kit_nor[off - 0xDA000u + i] &= ((const uint8_t *)src)[i]; return 0; }
#include "../firmware/src/storage.c"
static uint32_t kit_tmp[4096 / 4];
#define UK_HOST 1
#define UK_TMP ((ukit_bank_t *)(void *)kit_tmp)
#include "../firmware/src/drum_kits.c"
#include "../firmware/src/splash.c"

static int bad;

/* the PRESETS list entries (ui.c BANK, resolved for this build) that load a preset of engine slot e */
static uint32_t reachable(uint32_t e)
{
    uint32_t n, k, hits = 0;
    for (n = 0; n < NBANK; n++) {
        if (preset_at(n, &k) != e)
            continue;
        hits += k < ENGINES[e]->npresets && preset_playable(ENGINES[e], k);
    }
    return hits;
}

/* the kits of kit UIDs lo..hi-1 on this build's kit list */
static uint32_t kits_built(uint32_t lo, uint32_t hi)
{
    uint32_t k, n = 0;
    for (k = lo; k < hi && k < DRUM_KITS; k++)
        n += (uint32_t)drum_kit_built(k);
    return n;
}

static void source(const char *name, int built, uint32_t have, uint32_t reach)
{
    int ok = !built || reach >= 1u;
    printf("cover: %-10s %-5s %3u %3u %s\n", name, built ? "built" : "-", have, reach, ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(void)
{
    uint32_t e, k;
    bank_resolve();
    for (e = 0; e < NENGINES; e++)
        source(ENG_UID_NAME[eng_uid(e)], 1, ENGINES[e]->npresets, reachable(e));
    /* the drum sources (registry.h): kit UIDs 0..4 sampled, 5.. synthesised, 37 / 38 the X0X kits */
    source("KITS SMPL", DRUM_SMASK != 0, 5u, kits_built(0, DRUM_SAMPLED));
    source("KITS SYNTH", FELUCCA_DRUM_SYNTH, DS_NKITS, kits_built(DRUM_SAMPLED, DRUM_SYNTH_END));
    source("X0X 909", FELUCCA_DRUM_X909, 1u, kits_built(DRUM_UID_X909, DRUM_UID_X909 + 1u));
    source("X0X 808", FELUCCA_DRUM_X808, 1u, kits_built(DRUM_UID_X808, DRUM_UID_X808 + 1u));
    for (k = DRUM_UID_X808 + 1u; k < DRUM_KITS; k++)          /* (a kit UID added later: its own source) */
        source(DRUM_KIT_NAMES[k], drum_kit_built(k), 1u, (uint32_t)drum_kit_built(k));
    source("KIT LIST", 1, DRUM_KITS, kits_built(0, DRUM_KITS));
    source("POWER-ON", 1, 1u, (uint32_t)drum_kit_built(DRUM_DEFAULT_KIT));
    printf(bad ? "PRESET COVER FAILED (%d)\n" : "preset cover: every source of this build has a preset / kit\n", bad);
    return bad != 0;
}
