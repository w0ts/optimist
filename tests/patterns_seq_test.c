/* SPDX-License-Identifier: GPL-3.0-only */
/* Launching patterns while playing (firmware/src/storage/sections/pat.c pat_launch / pat_service / pat_switch,
 * docs/PATTERNS-DESIGN.md phase 2), FELUCCA_PATTERNS=1 on a simulated NOR with the section log, the real sequencer:
 *   end      the launched pattern starts at its step 1 when the playing one wraps (odd LEN: its own phrase)
 *   bar      OCT-: on the next bar, from its step 1 (not at the pattern's end)
 *   now      OCT+: on the next step, where it is (legato)
 *   swing    with SWING and odd LENs: one step a grid step, never skipped or doubled across a switch
 *   motion   the new pattern's motion plays on its track, the other tracks keep theirs
 *   rec      a take on the track ends at the switch; undo brings the pattern before it back
 *   stopped  a launch while stopped loads at once
 *   scene    a live jump staged after a launch takes the launched pattern with it ("scene B with T1's 1")
 *   song     a launch in song mode plays until the next part, which brings its own pattern
 * Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_PATTERNS 1
#ifndef FELUCCA_MOTION
#define FELUCCA_MOTION 1
#endif
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
#if FELUCCA_MOTION
#include "../firmware/src/storage/motion_flash.c"
#endif

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
#if FELUCCA_AUTO
#include "auto_view.h"              /* (the automation store as motion's old store) */
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* T1's patterns: n steps of note base + i (a note on every step) */
static void t1(uint32_t n, uint32_t base)
{
    uint32_t i;
    steps_clear(&trk[0]);
    trk[0].p[P_SLEN] = (int16_t)n;
    for (i = 0; i < n; i++) {
        trk[0].step[i].note[0] = (uint8_t)(base + i);
        trk[0].step[i].n = 1;
        trk[0].step[i].time = ST_NOTE;
    }
}
/* scenes: A = T1 16 steps (slot 0), B = T1 12 steps (slot 1), C = T1 3 steps (slot 2); T2 8 steps in every one */
static void setup(void)
{
    uint32_t s, i;
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    trk[1].p[P_SLEN] = 8;
    for (i = 0; i < 8u; i++)
        trk[1].step[i].note[0] = (uint8_t)(20u + i), trk[1].step[i].n = 1, trk[1].step[i].time = ST_NOTE;
    memset(pat_cur, PAT_NONE, NTRK);
    for (s = 0; s < 3u; s++) {
        t1(s == 0 ? 16u : s == 1 ? 12u : 3u, 40u * (s + 1u));
#if FELUCCA_MOTION
        motion_reset();
        motion_set_event(&trk[0], 1, P_CHOR, (int32_t)(10u + s));   /* (T1's step 2: CHORUS by pattern) */
        motion_set_event(&trk[1], 2, P_CHOR, 77);
#endif
        live_sec = -1;
        project_save(s);
    }
}

/* playing: what T1 played, a step at a time (the note of the step, its grid step) */
static uint8_t played[512];
static uint32_t nplayed;
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = trk[0].seq_abs;
    mix_block(out, CTL);
    if (trk[0].seq_abs != a && nplayed < sizeof played)
        played[nplayed++] = trk[0].step[trk[0].seq_idx].note[0];
}
static void play_from(uint32_t scene)
{
    song.playing = 0;
    transport_req = 0;
    project_load(scene);
    nplayed = 0;
    clk_beat = clk_pos = 0;
    seq_start();
    run_block();
}
static void run_steps(uint32_t n)                       /* until T1 has played n more steps */
{
    uint32_t want = nplayed + n, guard = 0;
    while (nplayed < want && guard++ < 200000u) {
        run_block();
        sec_service();                                 /* (the main loop between blocks) */
    }
}
static int seq_is(uint32_t from, const uint8_t *w, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (from + i >= nplayed || played[from + i] != w[i])
            return 0;
    return 1;
}

#if FELUCCA_AUTO
#define IFM_BIND() ((void)auto_for(&proj_tmp.cur, 1), (void)auto_for(&sec_stage_p, 1))   /* (persist_boot) */
#else
#define IFM_BIND() ((void)0)
#endif
int main(void)
{
    uint8_t w[64];
    uint32_t i, ok;
    IFM_BIND();
    setup();
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;

    /* ---- at the end of the pattern playing */
    play_from(0);
    run_steps(4);                                      /* (A's steps 1..5 played) */
    pat_launch(0, 1, PW_END);
    run_steps(20);
    for (i = 0; i < 16u; i++)
        w[i] = (uint8_t)(40u + i);
    for (i = 0; i < 12u; i++)
        w[16u + i] = (uint8_t)(80u + i);
    check("END: A plays to its end, then B from its step 1 (12 steps), looped on its own length",
          seq_is(0, w, 25) && pat_cur[0] == 1 && trk[0].p[P_SLEN] == 12);
    /* ---- the next bar (an odd LEN: the bar is not the pattern's end) */
    play_from(2);                                      /* (C: 3 steps) */
    run_steps(1);
    pat_launch(0, 1, PW_BAR);
    run_steps(20);
    for (i = 0; i < 16u; i++)
        w[i] = (uint8_t)(120u + i % 3u);
    for (i = 0; i < 4u; i++)
        w[16u + i] = (uint8_t)(80u + i);
    check("BAR (OCT-): C (3 steps) plays to the bar (16 sixteenths), then B from its step 1", seq_is(0, w, 20));
    /* ---- now, legato */
    play_from(0);
    run_steps(5);                                      /* (steps 1..6 of A played: grid step 5) */
    pat_launch(0, 1, PW_NOW);
    run_steps(2);
    check("NOW (OCT+): the next step is B's step at the same place (grid step 6 of 12: B's step 7)",
          played[6] == 80u + 6u && played[7] == 80u + 7u);
    /* ---- swing, odd lengths: one step a grid step across switches */
    song.g[G_SWING] = 60;
    play_from(2);
    pat_launch(0, 1, PW_END);
    run_steps(40);
    {
        uint32_t ab = trk[0].seq_abs;
        check("SWING 60, C (3) then B (12): every grid step played once (no skip, no double)", ab + 1u == nplayed);
    }
    song.g[G_SWING] = 0;
#if FELUCCA_MOTION
    /* ---- motion */
    play_from(0);
    pat_launch(0, 1, PW_NOW);
    run_steps(2);
    {
        uint32_t n0 = 0, n1 = 0, v0 = 0, v1 = 0;
        const motion_store_t *mv = mview();
        for (i = 0; i < mv->count; i++)
            if ((mv->ev[i].place >> 6) == 0)
                n0++, v0 = (uint32_t)mv->ev[i].value;
            else
                n1++, v1 = (uint32_t)mv->ev[i].value;
        check("motion: T1's is B's now (one event, CHORUS 11), T2's kept (77)", n0 == 1u && v0 == 11u && n1 == 1u && v1 == 77u);
    }
#endif
    /* ---- a take, undo */
    play_from(0);
    song.rec = 1;
    pat_launch(0, 1, PW_NOW);
    run_steps(2);
    ok = !(song.rec & 1u) && trk[0].step[0].note[0] == 80u;
    song.playing = 0;
    undo_apply(0);
    check("a take on T1 ends at the switch; undo (EDIT + OCT-) brings A's steps back",
          ok && trk[0].step[0].note[0] == 40u && trk[0].step[15].note[0] == 55u);
    /* ---- stopped */
    song.playing = 0;
    project_load(0);
    pat_launch(0, 2, PW_END);
    check("stopped: the launch loads C at once", trk[0].p[P_SLEN] == 3 && trk[0].step[0].note[0] == 120u && pat_cur[0] == 2);
    /* ---- a launch, then a live jump: the scene takes the launched pattern */
    play_from(2);
    pat_launch(0, 1, PW_END);
    sec_service();
    song.playing = 1;
    ok = section_cue(0);
    sec_service();
    for (i = 0; i < 4000u && live_req >= 0; i++)
        run_block(), sec_service();
    check("a live jump to A staged after T1's launch of B: A plays with B on T1", ok && live_req < 0 && trk[0].p[P_SLEN] == 12 &&
          trk[0].step[0].note[0] == 80u && pat_cur[0] == 1);
    /* ---- song mode: a launch lasts until the next part */
    song.playing = 0;
    arr_defaults(&arrangement);
    arrangement.count = 2, arrangement.loop = 1;
    arrangement.entry[0] = (arr_entry_t){0, 4};
    arrangement.entry[1] = (arr_entry_t){2, 4};
    arrangement_enabled = 1;
    project_load(0);
    sec_service();
    clk_beat = clk_pos = 0;
    nplayed = 0;
    seq_start();
    run_steps(2);
    pat_launch(0, 1, PW_NOW);
    run_steps(4);
    ok = trk[0].p[P_SLEN] == 12;
    for (i = 0; i < 40000u && arrangement_clock.index == 0u; i++)
        run_block(), sec_service();
    run_steps(1);
    check("song mode: B launched in part 1 plays there; part 2 (C) brings its own T1",
          ok && arrangement_clock.index == 1u && trk[0].p[P_SLEN] == 3 && sec_misses == 0u);
    arrangement_enabled = 0;
    printf("patterns seq test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
