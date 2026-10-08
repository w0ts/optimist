/* SPDX-License-Identifier: GPL-3.0-only */
/* Per-track patterns and scenes (firmware/src/storage/sections/pat.c, docs/PATTERNS-DESIGN.md phase 1) on a simulated NOR with the
 * section log. Built with FELUCCA_PATTERNS=1 (the default here):
 *   - a stored scene reads back as the project stored (steps, LEN DIV SWING GATE, motion, step extras), byte for byte
 *     as a section would; an empty track costs no pattern;
 *   - storing an unchanged scene again, or under another letter, shares its patterns (nothing written); a changed
 *     track is written into its own slot when no other scene plays it, else into a new one (copy-on-write); every
 *     slot taken: NO FREE PATTERN and nothing stored;
 *   - the old sections become scenes at the first start, cut at every flash program and started again: every
 *     section reads as before, no pattern twice;
 *   - stored while playing: the patterns and the scene wait in the arena (a warm reset keeps them), written
 *     patterns first; the stage (a live jump) assembles the scene; the tracks' sources follow loads and the stage;
 *   - MEM FULL; the playing scene can always be stored with four new patterns (the reserve);
 *   - the patterns' state record; a snapshot and a backup (PTN1..) carry the patterns.
 * Built with FELUCCA_PATTERNS=0 it reads what the PATTERNS build wrote (argv: an image file): every scene plays
 * flattened, a section stored over a scene is a plain section, the patterns are kept; the PATTERNS build then
 * converts that section again and every other scene is as it was. Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#ifndef FELUCCA_PATTERNS
#define FELUCCA_PATTERNS 1
#endif
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
static int fail_after = -1;
static uint32_t progs;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    progs++;
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

static int bad;
static void check(const char *what, int ok)
{
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---- projects: the tracks as "section s" (seed), a track left empty when (s >> k) & 8 */
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
            continue;                                  /* (T3 empty) */
        for (k = 0; k < 8u; k++)
            if (i == TRK_DRUM)
                dstep_set(&trk[i].dstep[k], (k + s) % 16u, LV_NORM, 0);
            else {
                trk[i].step[k].note[0] = (uint8_t)(40u + 3u * s + k + i);
                trk[i].step[k].n = 1;
                trk[i].step[k].time = ST_NOTE;
            }
    }
    trk[1].p[P_REV] = (int16_t)(10 * (s % 8u));
    memset(&dl, 0, sizeof dl);
#if FELUCCA_MOTION
    memset(&motion, 0, sizeof motion);
    for (k = 0; k < 6u; k++)                           /* (events on T1 and T2, track order) */
        motion_set_event(&trk[k / 3u], k, P_CHOR, (int32_t)(s * 5u + k));
    motion.on = 3;
#endif
#if FELUCCA_SL24_XSTEP
    for (k = 0; k < NTRK; k++)
        stepx_clear(STEPX(k));
    STEPX(0)->micro[1] = (int8_t)(s % 9u) - 4;         /* (T1: a nudge, DR: a lock and a fill) */
    stepx_lock_set(STEPX(TRK_DRUM), 2, P_PAN, (int16_t)s);
    stepx_fill_set(STEPX(TRK_DRUM), 5, FC_FILL);
#endif
}
static project_t want[16], got;
static dlrec_t wantd[16], gotd;
#if FELUCCA_MOTION
static motion_store_t wantm[16];
#endif
#if FELUCCA_SL24_XSTEP
static stepx_t wantx[16][NTRK];
#endif

#if FELUCCA_MOTION
#define IFM(x) x
#else
#define IFM(x)
#endif
#if FELUCCA_SL24_XSTEP
#define IFX(x) x
#else
#define IFX(x)
#endif
#if FELUCCA_PATTERNS
#define IFP(x) x
#else
#define IFP(x)
#endif
static void take(uint32_t s) { proj_capture(&want[s], &wantd[s]); IFM(wantm[s] = motion;) IFX(memcpy(wantx[s], sx_work, sizeof sx_work);) }
/* section s reads as made: the project (as a section keeps it) and its motion */
static int same(uint32_t s)
{
    project_t w = want[s];
    sec_canon(&w);
    if (!sec_read(s, &got, &gotd) || memcmp(&got, &w, sizeof got))
        return 0;
#if FELUCCA_MOTION
    {
        const motion_store_t *m = motion_for(&got, 0);
        if (!m || m->psum != got.sum || m->count != wantm[s].count || m->on != wantm[s].on ||
            memcmp(m->ev, wantm[s].ev, 3u * m->count))
            return 0;
    }
#endif
#if FELUCCA_SL24_XSTEP
    {
        const sx_store_t *x = sx_for(&got, 0);
        if (!x || x->psum != got.sum || memcmp(x->x, wantx[s], sizeof wantx[s]))
            return 0;
    }
#endif
    return 1;
}
static uint32_t npat(void)                            /* pattern records in the log and the arena */
{
    uint32_t k, s, n = 0;
    for (k = 0; k < NTRK; k++)
        for (s = 0; s < PAT_N; s++)
            n += slg_has(PAT_ID(k, s)) || sec_pend_has(SEC_PEND_PAT + PAT_N * k + s);
    return n;
}
static int is_scene(uint32_t s)
{
    return slg_has(s) && (nor[slg_at(s) + SEC_HEAD] & SEC_SCN);
}
static void fresh(void)
{
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    IFP(memset(pat_cur, PAT_NONE, NTRK);)
    live_sec = -1;
}
/* old sections A..H written as a firmware before patterns wrote them (plain records), with their motion */
static void legacy(uint32_t n)
{
    uint32_t s, len;
    fresh();
    for (s = 0; s < n; s++) {
        make(s | (s == 3u ? 16u : 0u));
        take(s);
        proj_capture(&proj_tmp.cur, &sec_tmp_dl);
        len = sec_encode(&proj_tmp.cur, &sec_tmp_dl, sec_rbuf);
        slg_put(s, sec_rbuf, len, 1);
        IFX(sx_log_put(SX_ID0 + s, proj_hash(sec_rbuf, len), &proj_tmp.cur, 1);)
    }
}

#if FELUCCA_PATTERNS
static void unit(void)
{
    uint32_t s, k, n0, ok, cut, gauge;
    /* ---- store and read back */
    fresh();
    for (s = 0; s < 4u; s++) {
        make(s | (s == 2u ? 16u : 0u));
        take(s);
        project_save(s);
    }
    ok = 1;
    for (s = 0; s < 4u; s++)
        ok &= is_scene(s) && same(s);
    check("four scenes stored (PROJECT SAVE): each reads back as the project stored, motion too", ok);
    check("... their patterns in slot s of each track; C's empty T3 costs none (15 patterns)",
          npat() == 15u && slg_has(PAT_ID(0, 0)) && slg_has(PAT_ID(3, 3)) && !slg_has(PAT_ID(2, 2)));
    {   /* the MEM gauge's size: the scene with the patterns it wrote (D: scene + its four patterns) */
        uint32_t d = slg.alen[3] + slg.alen[PAT_ID(0, 3)] + slg.alen[PAT_ID(1, 3)] + slg.alen[PAT_ID(2, 3)] + slg.alen[PAT_ID(3, 3)];
        check("the gauge counts a stored scene with its new patterns (the last store: D)", sec_last_n == d && d > slg.alen[3]);
        gauge = d;
    }
    /* ---- sharing */
    project_load(1);
    ok = !memcmp(pat_cur, (uint8_t[]){1, 1, 1, 1}, 4);
    n0 = slg.seq;
    project_save(4);
    want[4] = want[1], wantd[4] = wantd[1];
    IFM(wantm[4] = wantm[1];)
    IFX(memcpy(wantx[4], wantx[1], sizeof wantx[1]);)
    check("B loaded (the tracks' sources: B's), stored as E unchanged: E plays B's patterns, none written",
          ok && same(4) && npat() == 15u && slg.seq == n0 + 1u);
    check("... a store that shares every pattern does not lower the gauge's size", sec_last_n == gauge);
    /* ---- copy-on-write */
    project_load(1);
    trk[1].step[3].note[0] = 99;
    project_save(4);
    take(4);
    ok = same(4) && same(1) && npat() == 16u && slg_has(PAT_ID(1, 4));
    {
        uint8_t r[4];
        ok &= pat_scene_refs(4, r) && r[0] == 1 && r[1] == 4 && r[2] == 1 && r[3] == 1;
    }
    check("... T2 changed and stored as E: T2's pattern into slot E (B still plays slot B), the rest shared", ok);
    check("... the gauge: that scene and the one pattern it wrote", sec_last_n == slg.alen[4] + slg.alen[PAT_ID(1, 4)]);
    project_load(0);
    trk[0].step[0].note[0] = 98;
    project_save(0);
    take(0);
    check("A changed and stored over A (nobody else plays it): its pattern rewritten in its own slot", same(0) && npat() == 16u);
    /* ---- NO FREE PATTERN: sixteen scenes, each its own T1; E then made B's copy (its old T1 slot unused but kept: a
     * stored pattern is never overwritten behind the user's back); E's T1 changed has nowhere to go */
    fresh();
    for (s = 0; s < 16u; s++) {
        make(s);
        memset(pat_cur, PAT_NONE, 4);
        project_save(s);
    }
    project_load(1);
    project_save(4);
    project_load(4);
    trk[0].step[1].note[0] = 77;
    n0 = slg.seq;
    live_sec = -1;
    project_save(4);
    check("every T1 slot played by another scene or kept: a changed T1 says T1: NO FREE PATTERN, nothing written",
          npat() == 64u && !strcmp(last_msg, "T1: NO FREE PATTERN") && slg.seq == n0);
    {
        uint8_t r[4];
        project_load(4);
        trk[1].step[2].note[0] = 66;                   /* (T2 changed: B plays slot B, slot E kept: none either) */
        project_save(4);
        check("... and T2 likewise", !strcmp(last_msg, "T2: NO FREE PATTERN") && pat_scene_refs(4, r) && r[1] == 1);
    }
    /* ---- the old sections become scenes, cut anywhere */
    legacy(8);
    for (s = 0, ok = 1; s < 8u; s++)
        ok &= !is_scene(s);
    sec_boot();
    for (s = 0; s < 8u; s++)
        ok &= is_scene(s) && same(s);
    check("eight old sections: the first start makes them scenes, each reads as before (motion too)", ok);
    check("... their patterns in slot s (D's empty T3: none)", npat() == 31u && slg_has(PAT_ID(1, 7)));
    for (cut = 0, ok = 1; cut < 120u; cut++) {
        legacy(8);
        fail_after = (int)cut;
        sec_boot();
        fail_after = -1;
        sec_boot();
        for (s = 0; s < 8u; s++)
            ok &= is_scene(s) && same(s);
        ok &= npat() == 31u;
    }
    check("... the conversion cut at each of 120 flash programs, started again: every section as before, no pattern twice", ok);
    /* ---- stored while playing */
    fresh();
    make(2);
    take(9);
    song.playing = 1;
    section_store(9);
    ok = sec_pend_has(9) && npat() == 4u && !slg_has(9) && !slg_has(PAT_ID(0, 9)) && (sec_dirty >> 9 & 1u);
    sec_dirty = 0;
    sec_boot();
    ok &= sec_pend_has(9) && npat() == 4u && same(9);
    check("SAVE + key while playing: the scene and its 4 patterns wait in RAM, a warm reset keeps them, it reads", ok);
    song.playing = 0;
    sections_write();
    ok = is_scene(9) && slg_has(PAT_ID(3, 9)) && !sec_pend_has(9) && npat() == 4u && same(9) &&
         slg.aseq[PAT_ID(0, 9)] < slg.aseq[9];
    check("... written when stopped: the patterns first, the scene last", ok);
    /* ---- the stage, the sources */
    make(3);
    project_save(10);
    take(10);
    song.playing = 1;
    ok = section_cue(10) && sec_stage_id == 10 && !memcmp(pat_bref[1], (uint8_t[]){10, 10, 10, 10}, 4);
    make(0);
    arrangement_apply(10);
    proj_capture(&got, &gotd);
    {
        project_t w = want[10];
        sec_canon(&w), sec_canon(&got);
        ok &= !memcmp(got.t, w.t, sizeof got.t) && !memcmp(pat_cur, (uint8_t[]){10, 10, 10, 10}, 4);
    }
    check("a live jump to K: the stage holds the scene assembled, the ISR applies it, the sources are K's", ok);
    song.playing = 0, live_req = -1;
    project_load(9);
    pat_state_save();
    memset(pat_cur, PAT_NONE, 4);
    slg_boot();
    pat_state_load();
    check("the sources with the autosave (id 17): back after a restart", !memcmp(pat_cur, (uint8_t[]){9, 9, 9, 9}, 4));
    /* ---- MEM FULL, the reserve */
    fresh();
    for (s = 0; s < SEC_IDS; s++) {
        host_tracks_init();
        for (k = 0; k < NTRK; k++) {
            uint32_t j;
            trk[k].p[P_SLEN] = NSTEP;
            for (j = 0; j < NSTEP; j++)
                memset(&trk[k].step[j], (int)(1u + (s * 7u + j + k) % 200u), sizeof trk[k].step[j]);
        }
        memset(pat_cur, PAT_NONE, 4);
        live_sec = -1;
        project_save(s);
        if (!strcmp(last_msg, "MEM FULL"))
            break;
    }
    ok = !strcmp(last_msg, "MEM FULL") && s < SEC_IDS && s > 2u;
    {
        uint32_t pct, more, j;
        sec_mem(&pct, &more);
        live_sec = 0;
        project_load(0);
        for (k = 0; k < NTRK; k++)
            for (j = 0; j < NSTEP; j++)
                trk[k].step[j].note[2] ^= 0x55;        /* (every track changed: four new patterns) */
        project_save(0);
        check("MEM FULL: dense scenes until refused (0 more); the playing one, all four tracks changed, still saved",
              ok && more == 0u && !strcmp(last_msg, "SAVED"));
        printf("patterns: %u dense scenes, MEM %u %%; the reserve: a scene of %u B + 4 patterns of %u B\n", s, pct,
               SCN_REC_MAX, PAT_REC_MAX);
    }
}
#endif

/* ---- the round trip between builds: argv[1] an image (the NOR, then want[], wantd[]) */
static int img_io(const char *f, int wr)
{
    FILE *fp = fopen(f, wr ? "wb" : "rb");
    int ok;
    if (!fp)
        return 0;
    ok = wr ? fwrite(nor, sizeof nor, 1, fp) && fwrite(want, sizeof want, 1, fp) && fwrite(wantd, sizeof wantd, 1, fp) IFM(&& fwrite(wantm, sizeof wantm, 1, fp)) IFX(&& fwrite(wantx, sizeof wantx, 1, fp))
            : fread(nor, sizeof nor, 1, fp) && fread(want, sizeof want, 1, fp) && fread(wantd, sizeof wantd, 1, fp) IFM(&& fread(wantm, sizeof wantm, 1, fp)) IFX(&& fread(wantx, sizeof wantx, 1, fp));
    fclose(fp);
    return ok;
}
static void cross(const char *f, int step)
{
    uint32_t s, ok = 1;
    if (step == 1) {                                  /* PATTERNS: eight scenes, some sharing, written out */
        fresh();
        for (s = 0; s < 8u; s++) {
            if (s == 5u)
                project_load(1);                       /* (F: B's patterns shared) */
            else
                make(s | (s == 3u ? 16u : 0u));
            take(s);
            project_save(s);
        }
        check("cross 1 (PATTERNS): eight scenes written", is_scene(7) && npat() == 24u + 3u && img_io(f, 1));
        return;
    }
    if (!img_io(f, 0)) {
        check("cross: the image", 0);
        return;
    }
    sec_pend_clear();
    sec_boot();
    if (step == 2) {                                  /* not PATTERNS: flattened; C stored again as a section */
        for (s = 0; s < 8u; s++)
            ok &= is_scene(s) && same(s);
        check("cross 2 (no PATTERNS): every scene plays flattened, as the project stored", ok);
        make(21);
        take(2);
        project_save(2);
        check("... C stored again: a plain section; the patterns kept in the log", !is_scene(2) && same(2) && npat() == 27u);
        img_io(f, 1);
        return;
    }
    for (s = 0; s < 8u; s++)                          /* PATTERNS again: C converted, the rest as they were */
        ok &= is_scene(s) && same(s);
    check("cross 3 (PATTERNS again): C converted back into a scene, every scene as it was", ok);
}

int main(int argc, char **argv)
{
    IFM((void)motion_for(&proj_tmp.cur, 1); (void)motion_for(&sec_stage_p, 1); (void)motion_for(&got, 1);)   /* (bound first: persist_boot) */
    IFX((void)sx_for(&proj_tmp.cur, 1); (void)sx_for(&sec_stage_p, 1); (void)sx_for(&got, 1); sx_init();)
    sec_pend_clear();
    if (argc > 2) {
        cross(argv[1], argv[2][0] - '0');
    } else {
#if FELUCCA_PATTERNS
        unit();
#endif
    }
    printf("patterns test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
