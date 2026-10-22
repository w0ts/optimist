/* SPDX-License-Identifier: GPL-3.0-only */
/* Projects own their scenes, patterns and song (docs/PROJECTS-DESIGN.md; firmware/src/storage/sections/sections.c, the
 * log's format 2 in sec_log.c) on a simulated NOR:
 *   - a project's state (PROJECT SAVE / LOAD), scenes and patterns are its own: another project opened sees none of
 *     them, and every operation there (scenes stored and cleared, patterns stored, cleared, written, its state saved,
 *     the whole project cleared) leaves the first project's records byte for byte;
 *   - a scene stored while playing waits in the arena and reaches the log before another project opens;
 *   - the song chain and the scenes' BARS are the project's;
 *   - a restart opens the project the settings say, with its chain;
 *   - DELETE / NEW: every record of that project gone, the others whole;
 *   - MEM FULL counts every project; clearing one makes room for another.
 * Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_PATTERNS 1
#ifndef FELUCCA_MOTION
#define FELUCCA_MOTION 1
#endif
#define SEC_LOG_SECTORS 8u                             /* (a small log: compactions early) */
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
static void sections_write(void);
static void persist_flush_now(void) { sections_write(); }   /* (project.c's: the arena, the song, the autosave) */
#include "../firmware/src/storage/sections/sections.c"
#if FELUCCA_AUTO
#include "auto_view.h"              /* (auto_store_same) */
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---- the work as "seed" s: the tracks' steps and values, a track left empty when (s & 16) */
static void make(uint32_t s)
{
    uint32_t i, k;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = (int16_t)(8 + 4 * (s % 8u) + i);
        trk[i].p[P_SDIV] = (int16_t)(s % 3u);
        trk[i].p[P_SSWING] = (int16_t)(10 * i);
        trk[i].p[P_SGATE] = (int16_t)(40 + s);
        if (i == 2u && (s & 16u))
            continue;
        for (k = 0; k < 8u; k++)
            if (i == TRK_DRUM)
                dstep_set(&trk[i].dstep[k], (k + s) % DRUM_LANES, LV_NORM, 0);
            else {
                trk[i].step[k].note[0] = (uint8_t)(40u + 3u * s + k + i);
                trk[i].step[k].n = 1;
                trk[i].step[k].time = ST_NOTE;
            }
    }
    trk[1].p[P_REV] = (int16_t)(10 * (s % 8u));
    memset(&dl, 0, sizeof dl);
#if FELUCCA_AUTO
    for (k = 0; k < NTRK; k++)
        auto_w.l[k].n = 0;
    auto_w.on = 0;
#endif
#if FELUCCA_MOTION
    for (k = 0; k < 6u; k++)
        motion_set_event(&trk[k / 3u], k, P_CHOR, (int32_t)(s * 5u + k));
    auto_w.on = 3;
#endif
}
static project_t want[32], got;
static dlrec_t wantd[32], gotd;
static void take(uint32_t s) { proj_capture(&want[s], &wantd[s]); }
static int same_as(const project_t *p, uint32_t s)    /* p reads as made(s) */
{
    project_t w = want[s];
    sec_canon(&w);
    return !memcmp(p, &w, sizeof w);
}
static int scene_is(uint32_t slot, uint32_t s)        /* scene slot of the open project reads as made(s) */
{
    return sec_read(slot, &got, &gotd) && same_as(&got, s);
}
static int state_is(uint32_t s)                       /* the open project's state record reads as made(s), and loads so */
{
    int n = slg_get(SEC_ID_STATE, sec_rbuf);
    if (n <= 0 || !sec_decode(sec_rbuf, (uint32_t)n, &got, &gotd) || !same_as(&got, s))
        return 0;
    host_tracks_init();
    if (state_load() != 1)
        return 0;
    proj_capture(&got, &gotd);
    sec_canon(&got);
    return same_as(&got, s);
}
static uint32_t npat(void)                            /* pattern records of the open project (the log and the arena) */
{
    uint32_t k, s, n = 0;
    for (k = 0; k < NTRK; k++)
        for (s = 0; s < PAT_N; s++)
            n += slg_has(PAT_ID(k, s)) || sec_pend_has(SEC_PEND_PAT + PAT_N * k + s);
    return n;
}
static uint8_t snap[SLG_IDS][SEC_REC_MAX];            /* a project's records, every id (len 0: none) */
static uint32_t snapl[SLG_IDS];
static void snapshot(void)
{
    uint32_t i;
    for (i = 0; i < SLG_IDS; i++) {
        int n = slg_get(i, snap[i]);
        snapl[i] = n > 0 ? (uint32_t)n : 0u;
    }
}
static int as_snapshot(void)
{
    uint32_t i;
    for (i = 0; i < SLG_IDS; i++) {
        int n = slg_get(i, sec_rbuf);
        if (SLG_GLOBAL(i))
            continue;
        if ((n > 0 ? (uint32_t)n : 0u) != snapl[i] || (n > 0 && memcmp(sec_rbuf, snap[i], (uint32_t)n)))
            return 0;
    }
    return 1;
}
static void fresh(void)
{
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    open_proj = 0;
    proj_scope_read();
    live_sec = -1;
}
static void restart(void)                              /* persist_boot: the log, the open project's scope */
{
    sec_pend_clear();
    sec_boot();
    (void)slg_open_proj(open_proj);
    proj_scope_read();
}
static int open_p(uint32_t p) { return project_scope_set(p) == 0 && open_proj == p && slg.proj == p; }

int main(void)
{
    uint32_t i, k, ok;
    int rc;
#if FELUCCA_AUTO
    (void)auto_for(&proj_tmp.cur, 1); (void)auto_for(&sec_stage_p, 1); (void)auto_for(&got, 1); auto_init();
#endif
    sec_pend_clear();
    fresh();
    /* ---- a project's state and scenes are its own */
    make(1), take(1);
    ok = state_save() == 0;
    project_save(0);                                   /* scene A */
    ok &= !strcmp(last_msg, "SAVED") && scene_is(0, 1) && state_is(1) && npat() == 4u;
    check("project 1: the work saved as its state, stored as scene A (4 patterns)", ok);
    ok = open_p(1) && !project_used(0) && !slg_has(SEC_ID_STATE) && npat() == 0u && state_load() == 0;
    check("project 2 opened: no state, no scene, no pattern; the work untouched", ok && same_as((proj_capture(&got, &gotd), sec_canon(&got), &got), 1));
    make(2), take(2);
    ok = state_save() == 0;
    project_save(0);
    make(3), take(3);
    project_save(2);                                   /* scene C */
    ok &= scene_is(0, 2) && scene_is(2, 3) && state_is(2) && npat() == 8u;
    check("project 2: its own state, scenes A and C, 8 patterns", ok);
    ok = open_p(0) && scene_is(0, 1) && !project_used(2) && state_is(1) && npat() == 4u;
    check("project 1 again: its state and scene A as they were, project 2's scene C not there", ok);
    ok = open_p(1) && scene_is(0, 2) && scene_is(2, 3) && state_is(2) && npat() == 8u;
    check("project 2 again: as it was", ok);
    /* ---- nothing done in project 2 touches project 1 */
    (void)open_p(0);
    snapshot();
    (void)open_p(1);
    for (k = 0, ok = 1; k < 24u; k++) {
        make(4u + k % 8u);
        project_save(k % 4u);                          /* scenes stored over and over (copy-on-write, NO FREE PATTERN too) */
        if (k % 3u == 0u) {
            take(4u + k % 8u);
            ok &= state_save() == 0;
        }
        if (k % 5u == 0u)
            (void)pat_write(k % NTRK, k % PAT_N, 0);   /* a pattern cleared */
        if (k % 7u == 0u)
            (void)pat_store_slot(k % NTRK, (k + 3u) % PAT_N);   /* the working copy into a slot */
        if (k % 11u == 0u)
            (void)sec_scene_clear(k % SEC_IDS);
        if (k % 9u == 0u) {
            arrangement.count = 2, arrangement.entry[0].scene = 0, arrangement.entry[1].scene = 2;
            ok &= sec_song_put_all(&arrangement) == 0;
        }
    }
    ok &= project_clear(1) == 0 && !project_used(0) && npat() == 0u && !slg_has(SEC_ID_STATE) && !slg_has(SEC_ID_SONG);
    check("24 stores, clears, pattern writes, state saves, a chain and the whole project cleared in project 2", ok);
    check("... project 1's records byte for byte as before", open_p(0) && as_snapshot() && scene_is(0, 1) && state_is(1));
    /* ---- the arena across a switch */
    make(9), take(9);
    song.playing = 1;
    section_store(5);                                  /* (SAVE + key while playing: the arena) */
    song.playing = 0;
    ok = sec_pend_has(5) && !slg_has(5);
    ok &= open_p(1) && !project_used(5) && open_p(0) && !sec_pend_has(5) && scene_is(5, 9);
    check("a scene stored while playing waits in the arena: written to project 1's log before project 2 opens", ok);
    /* ---- the song chain and the BARS are the project's */
    arrangement.count = 3;
    arrangement.entry[0].scene = 0, arrangement.entry[1].scene = 5, arrangement.entry[2].scene = 0;
    arrangement.entry[0].bars = 4;
    scene_bars[5] = 8;
    check("the song chain and the scenes' BARS written in project 1", sec_song_put_all(&arrangement) == 0 && sec_sbars_put() == 0);
    ok = open_p(1);                                    /* (no chain of its own: no song; every scene's BARS AUTO) */
    check("... project 2 opened: no song (no record), every scene's BARS AUTO", ok && arrangement.count == 0u && scene_bars[5] == 0u);
    arrangement.count = 1, arrangement.entry[0].scene = 0;
    ok = sec_song_put_all(&arrangement) == 0 && open_p(0);
    check("... a chain written in project 2; project 1 opened: its chain and BARS", ok && arrangement.count == 3u && arrangement.entry[1].scene == 5u &&
          arrangement.entry[0].bars == 4u && scene_bars[5] == 8u);
    check("... project 2 again: its own chain", open_p(1) && arrangement.count == 1u);
    /* ---- a restart opens the project the settings say */
    open_proj = 0;
    restart();
    ok = slg.proj == 0u && scene_is(0, 1) && scene_is(5, 9) && arrangement.count == 3u && state_is(1);
    open_proj = 1;
    restart();
    ok &= slg.proj == 1u && !project_used(5) && arrangement.count == 1u;
    check("a restart: the open project (the settings') with its scenes, state and chain", ok);
    /* ---- DELETE from another project */
    make(10), take(10);
    ok = open_p(1);
    rc = state_save();
    project_save(1);
    check("project 2: a state and scene B stored", ok && rc == 0 && !strcmp(last_msg, "SAVED") && scene_is(1, 10));
    rc = project_clear(0);
    check("DELETE of project 1 from project 2: done, project 2 still open and whole", rc == 0 && open_proj == 1u && scene_is(1, 10) && state_is(10));
    ok = open_p(0);
    check("... project 1 opened: empty (no scene, pattern, state or chain)", ok && !project_used(0) && !project_used(5) && npat() == 0u &&
          !slg_has(SEC_ID_STATE) && !slg_has(SEC_ID_SONG) && arrangement.count == 0u);
    /* ---- MEM FULL counts every project */
    fresh();
    for (i = 0, rc = 0; i < SEC_IDS && rc == 0; i++) {   /* project 1 fills the log with dense scenes (every step busy, none shared) */
        uint32_t j;
        host_tracks_init();
        for (k = 0; k < NTRK; k++) {
            trk[k].p[P_SLEN] = NSTEP;
            for (j = 0; j < NSTEP; j++)
                memset(&trk[k].step[j], (int)(1u + (i * 7u + j + k) % 200u), sizeof trk[k].step[j]);
        }
        memset(pat_cur, PAT_NONE, NTRK);
        project_save(i);
        rc = strcmp(last_msg, "SAVED");
    }
    printf("project 1: %u busy scenes stored, then \"%s\"; MEM %u %%\n", i - 1u, last_msg, slg_used_pct());
    check("project 1 fills the log: MEM FULL (or no free pattern) after a few scenes", i > 4u && (!strcmp(last_msg, "MEM FULL") || !strncmp(last_msg, "T", 1)));
    k = slg_used_pct();
    ok = open_p(1);
    check("... project 2 opened: the gauge the same", ok && slg_used_pct() == k);
    {   /* a dense scene of project 2's (as project 1's: the reserve would not be left): refused */
        uint32_t j;
        host_tracks_init();
        for (k = 0; k < NTRK; k++) {
            trk[k].p[P_SLEN] = NSTEP;
            for (j = 0; j < NSTEP; j++)
                memset(&trk[k].step[j], (int)(1u + (300u + j + k) % 200u), sizeof trk[k].step[j]);
        }
        memset(pat_cur, PAT_NONE, NTRK);
    }
    take(20);
    project_save(0);
    check("... a dense scene stored in project 2: refused (the log is full of project 1's)", strcmp(last_msg, "SAVED") != 0);
    rc = project_clear(0);
    project_save(0);
    check("... project 1 cleared from project 2: the store goes in", rc == 0 && !strcmp(last_msg, "SAVED") && scene_is(0, 20));
    printf("project scope test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
