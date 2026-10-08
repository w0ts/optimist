/* SPDX-License-Identifier: GPL-3.0-only */
/* The PATTERN layer (firmware/src/ui/sloop/ui_pat.c, LFO held; docs/PATTERNS-DESIGN.md 6.2) on the real storage
 * and sequencer (FELUCCA_PATTERNS=1, a simulated NOR), keys -> actions:
 *   white n: launch (stopped: at once; playing: at the end, OCT- the next bar, OCT+ now); black 1..4 the track;
 *   black 5 stop; black 6 + n STORE ("AGAIN" over a used slot); black 7 + a, b COPY (to another synth track);
 *   black 8 + n CLEAR (twice); black 9 DUPLICATE; KNOB k cues track k's next stored pattern;
 *   the tiles (playing green, queued amber, stored grey, empty dark, "*" changed) and the dials; every message
 *   fits the title (232 px of the 8-px font). Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_PATTERNS 1
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

static struct { int force; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static char last_msg[64];
static uint32_t msg_long;                               /* messages wider than the title */
static void ui_message(const char *m)
{
    str_cpy(last_msg, m, sizeof last_msg);
    msg_long += strlen(m) * 8u > 232u;
}
static void ui_say(const char *a, const char *b)
{
    char m[64];
    str_cpy(m, a, sizeof m);
    str_cpy(m + strlen(m), b, sizeof m - strlen(m));
    ui_message(m);
}
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/storage/sections/sections.c"

/* ---- what ui_pat.c takes from the UI (ui_layers.c, panel.c, gfx.c, ui_colors.c): doubles */
enum { B_OCTDN, B_OCTUP };
static struct { uint8_t btn[2]; } panel = {{0, 1}};
enum { C_BLACK, C_OK, C_WARN, TE_G1, TE_G3, TE_G4, C_TRK };
static uint16_t trk_col(uint32_t i) { (void)i; return C_TRK; }
typedef struct { char lab[8]; uint16_t bg, fg, top; uint8_t marks; } tile_t;
static void track_select(uint32_t i) { song.sel = (uint8_t)(i % NTRK); }
#include "../firmware/src/ui/sloop/ui_pat.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
/* the keys from F: white n, black n (1..) */
static uint32_t white(uint32_t n) { static const uint8_t W[7] = {0, 2, 4, 6, 7, 9, 11}; return (n / 7u) * 12u + W[n % 7u]; }
static uint32_t black(uint32_t n) { static const uint8_t B[5] = {1, 3, 5, 8, 10}; return ((n - 1u) / 5u) * 12u + B[(n - 1u) % 5u]; }
static void wkey(uint32_t n) { pat_layer_key(white(n), (int32_t)n); }
static void bkey(uint32_t n) { pat_layer_key(black(n), -1); }
static void bup(uint32_t n) { pat_layer_up(black(n)); }
static void t1(uint32_t n, uint32_t base)
{
    uint32_t i;
    steps_clear(&trk[0]);
    trk[0].p[P_SLEN] = (int16_t)n;
    for (i = 0; i < n; i++)
        trk[0].step[i].note[0] = (uint8_t)(base + i), trk[0].step[i].n = 1, trk[0].step[i].time = ST_NOTE;
}
static void run_block(void)
{
    int32_t out[CTL * 2];
    mix_block(out, CTL);
}

int main(void)
{
    static tile_t tl[16];
    static char sub[24], v[4][10];
    const char *lab[4];
    int32_t ratio[4];
    uint32_t i, ok;
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    song.sel = 0;
    /* ---- STORE */
    t1(16, 40);
    bkey(6), wkey(0), bup(6);
    check("black 6 + white 1: the working copy STORED into T1 1, its source", slg_has(PAT_ID(0, 0)) && pat_cur[0] == 0 &&
          !strcmp(last_msg, "STORED T1 1"));
    t1(12, 80);
    bkey(6), wkey(0);
    ok = !strcmp(last_msg, "AGAIN: T1 1") && slg.alen[PAT_ID(0, 0)] > 0;
    wkey(0), bup(6);
    check("... over a used slot: AGAIN: T1 1, the key again stores", ok && !strcmp(last_msg, "STORED T1 1"));
    t1(16, 40);
    bkey(6), wkey(1), bup(6);                           /* (T1 2: the 16 steps) */
    /* ---- launch, stopped */
    wkey(0);
    check("white 1 stopped: T1 1 loaded at once", trk[0].p[P_SLEN] == 12 && pat_cur[0] == 0 && !strcmp(last_msg, "LOADED T1 1"));
    /* ---- launch, playing: END, BAR (OCT- held), NOW (OCT+ held) */
    clk_beat = clk_pos = 0;
    seq_start();
    run_block();
    wkey(1);
    ok = pat_req[0] == 1 && pat_when[0] == PW_END && !strcmp(last_msg, "T1 2 AT END");
    fm1_in.buttons = 1u << 0;
    wkey(1);
    ok &= pat_when[0] == PW_BAR && !strcmp(last_msg, "T1 2 NEXT BAR");
    fm1_in.buttons = 1u << 1;
    wkey(1);
    ok &= pat_when[0] == PW_NOW && !strcmp(last_msg, "T1 2 NOW");
    fm1_in.buttons = 0;
    check("white 2 playing: queued at the end; OCT- held: next bar; OCT+ held: now", ok);
    pat_layer_draw(tl, sub, lab, v, ratio);
    check("the tiles: 1 playing green, 2 queued amber, 3 empty dark; the dial: 1>2",
          tl[0].bg == C_OK && tl[1].bg == C_WARN && tl[2].bg == TE_G1 && !strcmp(v[0], "1>2") && !strcmp(tl[0].lab, "1"));
    for (i = 0; i < 2000u && pat_req[0] != PAT_NONE; i++)
        run_block(), sec_service();
    pat_chg_ms = fm1_ms - 1000u;
    pat_layer_draw(tl, sub, lab, v, ratio);
    check("... it plays: tile 2 green, stored 1 grey", pat_cur[0] == 1 && tl[1].bg == C_OK && tl[0].bg == TE_G3 && !strcmp(v[0], "2"));
    trk[0].step[0].note[0] = 99;
    fm1_ms += 1000u;
    pat_layer_draw(tl, sub, lab, v, ratio);
    check("... an edit: its tile marked 2*", !strcmp(tl[1].lab, "2*"));
    song.playing = 0;
    /* ---- tracks, stop, knobs */
    bkey(2);
    check("black 2: T2 selected", song.sel == 1);
    bkey(1);
    bkey(5);
    check("black 5 stopped: T1 empty at once", pat_cur[0] == PAT_NONE && trk[0].step[0].n == 0 && !strcmp(last_msg, "STOPPED"));
    pat_knob(0, 1);
    check("KNOB 1 turned right: T1's next stored pattern (1)", pat_cur[0] == 0 && trk[0].p[P_SLEN] == 12);
    pat_knob(0, 1);
    ok = pat_cur[0] == 1;
    pat_knob(0, 1);
    check("... again: 2, then round to 1", ok && pat_cur[0] == 0);
    /* ---- COPY to T2, CLEAR, DUPLICATE */
    bkey(7), wkey(1), bkey(2), wkey(4), bup(7);
    check("black 7 + T1's 2, then T2, white 5: COPIED to T2 5", slg_has(PAT_ID(1, 4)) && !strcmp(last_msg, "COPIED T2 5"));
    bkey(1);
    bkey(7), wkey(1), bkey(4), wkey(4), bup(7);
    check("... the drum track refuses a synth pattern", !slg_has(PAT_ID(3, 4)) && !strcmp(last_msg, "NO COPY"));
    bkey(1);
    bkey(8), wkey(1);
    ok = slg_has(PAT_ID(0, 1)) && !strcmp(last_msg, "AGAIN: T1 2");
    wkey(1), bup(8);
    check("black 8 + white 2, twice: CLEARED T1 2", ok && !slg_has(PAT_ID(0, 1)) && !strcmp(last_msg, "CLEARED T1 2"));
    bkey(9);
    check("black 9: DUPLICATE into the first free slot (2), which T1 plays from now", slg_has(PAT_ID(0, 1)) && pat_cur[0] == 1 &&
          !strcmp(last_msg, "DUPLICATE T1 2"));
    check("every message fits the title", msg_long == 0u);
    printf("patterns ui test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
