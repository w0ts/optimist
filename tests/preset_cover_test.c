/* SPDX-License-Identifier: GPL-3.0-only */
/* Every sound source of this build has something to pick (a modular build must never leave one empty):
 *   - every synth engine built has at least one loadable entry on the PRESETS list: a factory preset (ui.c BANK as
 *     this build resolves it: the engine built, the preset in its table, its sample set built: preset_playable), or
 *     else its INIT (the engine's defaults); each entry loaded through the list gives that engine (INIT: its
 *     default values). COVER_STRICT=1 (the full build): no engine falls back to INIT;
 *   - every drum source built (the sampled kits, the synthesised kits, each X0X kit and style kit) has at least one
 *     kit on the kit list (drum_kit_built), and the power-on kit is one of them (tests/kits_sound_test.c: each
 *     heard).
 * Built by tests/run_tests.sh on several configurations (the defaults, every engine and kit, sample headers with
 * sets left out) and by tests/builder_test.py on random builder configurations (tools/builder/verify.py
 * preset_cover). One line per source:  cover: <NAME> <built> <in its table> <loadable> [INIT]
 *   preset_cover_test [DIR]   DIR: the PRESETS page and TRACKS with each INIT loaded (presets-init-*.ppm) */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint16_t screen[240*240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ uint32_t i,j; assert(x+w<=240 && y+h<=240); for(j=0;j<h;j++) for(i=0;i<w;i++) screen[(y+j)*240+x+i]=p[j*w+i]; }
#include "../firmware/src/display/gfx.c"
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
#include "../firmware/src/ui/panel.c"
#include "../firmware/src/ui/sloop/ui.c"
#include "../firmware/src/ui/sloop/ui_drums.c"
#include "../firmware/src/ui/sloop/ui_colors.c"  /* the colour language (engine, drum kind, status) */
static int project_used(uint32_t i) { return i < 2; }
#if FELUCCA_SL24_SAFE
static uint32_t project_state(uint32_t s) { return (uint32_t)project_used(s); }   /* (sl24_guard.c: no slot of 2.4's here) */
static void sl24_auto_import(void) {}                          /* (sl24_guard.c: no 2.4 autosave here) */
#endif
static void project_save(uint32_t i) { (void)i; }
static void project_load(uint32_t i) { (void)i; }
static void arrangement_save(void) {}
static uint32_t arrangement_ready(void) { return 3; }
static void arrangement_apply(uint32_t s) { (void)s; }
static void song_backup(void) {}
static void song_restore(void) {}
static void section_store(uint32_t s) { live_sec = (int8_t)s; }
static void section_load(uint32_t s) { live_sec = (int8_t)s; }
#if FELUCCA_QCHAIN
static uint32_t section_bars(uint32_t s) { (void)s; return 1u; }   /* (the quick chain's bars: sections.c) */
#endif
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return 1; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { (void)k; b[0] = 0; }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; }
#include "snap_ui_stub.h"
static void settings_save(void) {}
#include "../firmware/src/ui/sloop/ui_song.c"
#include "../firmware/src/ui/sloop/ui_studio.c"
#include "../firmware/src/ui/sloop/ui_fm6.c"
#include "../firmware/src/ui/sloop/icons.c"
#define PROJ_HOST 1
#include "../firmware/src/storage/project.c"
#include "../firmware/src/storage/miss.c"
#include "../firmware/src/ui/sloop/ui_draw.c"
#include "../firmware/src/ui/sloop/ui_overview.c"
#include "../firmware/src/ui/sloop/ui_layers.c"
#include "../firmware/src/ui/sloop/ui_menu.c"
#include "../firmware/src/ui/sloop/ui_input.c"
#include "../firmware/src/engines/fm6/fm6_store.c"
#if FELUCCA_NATIVE_BANKS
#include "../firmware/src/storage/nbank.c"       /* the FM6 / CZ collections in PRESETS */
#endif
static uint8_t kit_nor[0x2000];
static int st_read(uint32_t off, void *dst, uint32_t n)
{ if (off < 0xDA000u || off + n > 0xDC000u) return -1; memcpy(dst, kit_nor + off - 0xDA000u, n); return 0; }
static int st_erase(uint32_t off) { if (off < 0xDA000u || off >= 0xDC000u) return -1; memset(kit_nor + off - 0xDA000u, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{ uint32_t i; if (off < 0xDA000u || off + n > 0xDC000u) return -1; for (i = 0; i < n; i++) kit_nor[off - 0xDA000u + i] &= ((const uint8_t *)src)[i]; return 0; }
#include "../firmware/src/storage/storage.c"
static uint32_t kit_tmp[4096 / 4];
#define UK_HOST 1
#define UK_TMP ((ukit_bank_t *)(void *)kit_tmp)
#include "../firmware/src/drums/drum_kits.c"
#include "../firmware/src/ui/splash.c"

static int bad;

/* the screens (DIR given): the PRESETS page and TRACKS with each INIT loaded, DIR/presets-init-<ENGINE>.ppm */
static void ppm(const char *dir, const char *name)
{
    char path[512];
    unsigned i;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t q = swap16(screen[i]);
        uint8_t rgb[3] = {(uint8_t)((q >> 11) * 255 / 31), (uint8_t)(((q >> 5) & 63) * 255 / 63), (uint8_t)((q & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void frame(void)
{
    uint32_t q;
    static int32_t o[CTL * 2];
    for (q = 0; q < 22u; q++)
        mix_block(o, CTL);
    ui_input();
    ui_leds();
    ui_draw();
    fm1_ms += 16;
}
static void shots(const char *dir, uint32_t e)
{
    char nm[64];
    uint32_t i;
    for (i = 0; i < sizeof PAGES / sizeof PAGES[0]; i++)
        if (PAGES[i].graph == GR_BROWSE)
            break;
    ui.page = (uint8_t)i;
    ui.force = 1;
    for (i = 0; i < 12u; i++)
        frame();
    snprintf(nm, sizeof nm, "presets-init-%s", ENG_UID_NAME[eng_uid(e)]);
    ppm(dir, nm);
    go_home();
    ui.force = 1;
    frame();
    frame();
    snprintf(nm, sizeof nm, "tracks-init-%s", ENG_UID_NAME[eng_uid(e)]);
    ppm(dir, nm);
}
#ifndef COVER_STRICT
#define COVER_STRICT 0          /* 1: every engine has a factory preset on the list (no INIT): the full build */
#endif

/* the PRESETS list entries of engine slot e: its factory presets (ui.c BANK as this build resolves it) and its INIT
 * (an engine with none: its defaults). Each one loaded on track 1 through the list (preset_go) must give that engine
 * and, for INIT, its defaults; *init: the INIT entries */
static const char *shot_dir;
static uint32_t loadable(uint32_t e, uint32_t *init)
{
    uint32_t n, k, i, total, hits = 0;
    track_t *t = &trk[0];
    *init = 0;
    preset_pos(&total);
    for (n = 0; n < total; n++) {
        if (preset_at(n, &k) != e)
            continue;
        preset_go(n);
        if (t->eng_req != e || t->user) {
            printf("cover: %s: list entry %u does not load engine %u\n", preset_name(e, k), n, e);
            bad++;
            continue;
        }
        if (k == PRESET_INIT) {
            *init += 1;
            if (shot_dir)
                shots(shot_dir, e);
            for (i = 0; i < 8u; i++)
                if (t->p[P_E0 + i] != ENGINES[e]->edit[i].def) {
                    printf("cover: INIT of engine %u: EDIT value %u not its default\n", e, i);
                    bad++;
                }
            hits++;
        } else
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

static void source(const char *name, int built, uint32_t have, uint32_t reach, uint32_t init)
{
    int ok = (!built || reach >= 1u) && !(COVER_STRICT && init);
    printf("cover: %-10s %-5s %3u %3u%s %s\n", name, built ? "built" : "-", have, reach, init ? " INIT" : "",
           ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(int argc, char **argv)
{
    uint32_t e, k, init;
    shot_dir = argc > 1 ? argv[1] : 0;
    host_tracks_init();
    song.sel = 0;
    if (shot_dir) {
        panel = PANEL_DEFAULT;
        layers_init();
        settings.palette = 4;
        palette_set(4);
        go_home();
    }
    bank_resolve();
    for (e = 0; e < NENGINES; e++) {
        uint32_t n = loadable(e, &init);
        source(ENG_UID_NAME[eng_uid(e)], 1, ENGINES[e]->npresets, n, init);
    }
    /* the drum sources (registry.h): kit UIDs 0..4 sampled, 5.. synthesised, 37 / 38 the X0X kits */
    source("KITS SMPL", DRUM_SMASK != 0, 5u, kits_built(0, DRUM_SAMPLED), 0);
    source("KITS SYNTH", FELUCCA_DRUM_SYNTH, DS_NKITS, kits_built(DRUM_SAMPLED, DRUM_SYNTH_END), 0);
    source("X0X 909", FELUCCA_DRUM_X909, 1u, kits_built(DRUM_UID_X909, DRUM_UID_X909 + 1u), 0);
    source("X0X 808", FELUCCA_DRUM_X808, 1u, kits_built(DRUM_UID_X808, DRUM_UID_X808 + 1u), 0);
    for (k = DRUM_UID_X808 + 1u; k < DRUM_KITS; k++)          /* (a kit UID added later: its own source) */
        source(DRUM_KIT_NAMES[k], drum_kit_built(k), 1u, (uint32_t)drum_kit_built(k), 0);
    source("KIT LIST", 1, DRUM_KITS, kits_built(0, DRUM_KITS), 0);
    source("POWER-ON", 1, 1u, (uint32_t)drum_kit_built(DRUM_DEFAULT_KIT), 0);
    printf(bad ? "PRESET COVER FAILED (%d)\n" : "preset cover: every source of this build has a preset (or INIT) / kit\n", bad);
    return bad != 0;
}
