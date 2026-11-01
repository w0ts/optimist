/* SPDX-License-Identifier: GPL-3.0-only */
/* Live patterns (issue w0ts/optimist#142, docs/PATTERNS-DESIGN.md 9; firmware/src/storage/sections/pat.c) on a simulated NOR:
 * every pattern keeps its own edits, in flash, next to its saved version.
 *   - stopped: a launch (or a stop) keeps the outgoing pattern's edits as its live record; the pattern launched again plays
 *     them; edited back to the saved one, the live record goes; a restart keeps them;
 *   - SAVE + SEQ (the slot saved) and a clear drop the live record; RELOAD drops it and loads the saved one;
 *   - projects: the edits stay with their project (tag 0x10 | p), another project sees none; PROJECT SAVE saves everything
 *     (each live pattern becomes the saved one); a save as copies the patterns as they play, the source keeps its live ones;
 *   - a live record made from a saved record replaced since (an older firmware's save) is not played and is swept at the
 *     next open; a sweep keeps the ones that stand;
 *   - MEM FULL: the live record waits in RAM, said "MEM FULL: SAVE OR RELOAD"; a project switch is refused then (never a
 *     silent loss: the arena was cleared before 2026-11-01); the RAM full too: the loss named;
 *   - a power cut at every flash program of a launch's live write, a PROJECT SAVE's promotion and a RELOAD: the pattern plays
 *     its old or its new version, whole, never a mix.
 * Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_PATTERNS 1
#ifndef FELUCCA_MOTION
#define FELUCCA_MOTION 1
#endif
#ifndef SEC_LOG_SECTORS
#define SEC_LOG_SECTORS 8u                             /* (a small log: compactions early, MEM FULL reachable) */
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"
static uint8_t nor[0x100000];
static uint32_t progs;
static int cut_in = -1;                                /* a power cut: the cut_in-th program / erase from now is half done and
                                                        * every one after it fails (-1: none) */
static int cut_now(void) { return cut_in == 0 || (cut_in > 0 && --cut_in == 0); }
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    int c = cut_in == 0 ? 2 : cut_now();
    memset(nor + off, 0xFF, c == 2 ? 0u : c ? 2048u : 4096u);
    return c ? -1 : 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    int c = cut_in == 0 ? 2 : cut_now();
    progs++;
    n = c == 2 ? 0u : c ? n / 2u : n;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return c ? -1 : 0;
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

static int bad;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---- the tracks as seed s: 8 notes each (the drum track: 8 hits) */
static void make(uint32_t s)
{
    uint32_t i, k;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = (int16_t)(8 + i);
        for (k = 0; k < 8u; k++)
            if (i == TRK_DRUM)
                dstep_set(&trk[i].dstep[k], (k + s) % DRUM_LANES, LV_NORM, 0);
            else {
                trk[i].step[k].note[0] = (uint8_t)(40u + 3u * s + k + i);
                trk[i].step[k].n = 1;
                trk[i].step[k].time = ST_NOTE;
            }
    }
    memset(&dl, 0, sizeof dl);
#if FELUCCA_AUTO
    for (k = 0; k < NTRK; k++)
        auto_w.l[k].n = 0;
    auto_w.on = 0;
#endif
}
static void edit(uint32_t k, uint32_t v) { trk[k].step[2].note[0] = (uint8_t)v; }   /* (a synth track's step 3: note v) */
static uint8_t enc[NTRK][SEC_REC_MAX];
static uint32_t encn[NTRK];
static void remember(uint32_t k)                       /* track k's working pattern as a record: enc[k] */
{
    (void)sec_capture();
    encn[k] = pat_encode(&proj_tmp.cur, k, enc[k]);
}
static uint8_t gb[SEC_REC_MAX];
static int plays(uint32_t k, uint32_t s, const uint8_t *r, uint32_t n)   /* (k, s) plays record r */
{
    return pat_get(k, s, gb) == n && !memcmp(gb, r, n);
}
static int saved_is(uint32_t k, uint32_t s, const uint8_t *r, uint32_t n)
{
    return pat_get_saved(k, s, gb) == n && !memcmp(gb, r, n);
}
static void fresh(void)
{
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    open_proj = 0;
    proj_scope_read();
    live_sec = -1;
    song.playing = 0;
    transport_req = 0;
}
static void restart(void)                              /* a power-on: the arena lost (a cold start), the log, the open project */
{
    sec_pend_clear();
    sec_boot();
    (void)slg_open_proj(open_proj);
    proj_scope_read();
}
/* every track stored in slot 3, T1 also in slot 5 (other notes): the base of each test */
static uint8_t s3[SEC_REC_MAX], s5[SEC_REC_MAX], ed[SEC_REC_MAX];
static uint32_t s3n, s5n, edn;
static void base(void)
{
    uint32_t k;
    fresh();
    make(1);
    for (k = 0; k < NTRK; k++)
        (void)pat_store_slot(k, 3);
    remember(0);
    memcpy(s3, enc[0], s3n = encn[0]);
    edit(0, 90);
    (void)pat_store_slot(0, 5);
    remember(0);
    memcpy(s5, enc[0], s5n = encn[0]);
    pat_launch(0, 3, PW_END);
    edit(0, 77);                                       /* T1 on slot 3, edited: note 77 */
    remember(0);
    memcpy(ed, enc[0], edn = encn[0]);
}

static void stopped_tests(void)
{
    base();
    check("base: T1 plays slot 4 with an edit, nothing live yet", pat_cur[0] == 3u && !pat_live_count() && (pat_changed() & 1u));
    pat_launch(0, 5, PW_END);
    check("stopped launch of slot 6: the edit kept as slot 4's live record, its saved one untouched",
          pat_cur[0] == 5u && pat_live_at(0, 3) == 2 && saved_is(0, 3, s3, s3n) && plays(0, 3, ed, edn) && plays(0, 5, s5, s5n));
    check("... slot 4 has a pattern (the grid), one live pattern in the project", pat_has(0, 3) && pat_live_count() == 1u);
    pat_launch(0, 3, PW_END);
    remember(0);
    check("... slot 4 again: it plays the edit", encn[0] == edn && !memcmp(enc[0], ed, edn) && trk[0].step[2].note[0] == 77u);
    restart();
    check("... a power-off: the live record kept (in flash), played", pat_live_at(0, 3) == 2 && plays(0, 3, ed, edn));
    pat_cur[0] = 3;                                    /* (the tracks' sources come back with the autosave: project.c) */
    edit(0, 78);
    pat_launch(0, 5, PW_END);
    remember(0);
    check("edited again and left: the live record replaced", plays(0, 3, ed, edn) == 0 && pat_live_at(0, 3) && pat_live_count() == 1u);
    pat_launch(0, 3, PW_END);
    edit(0, 45);                                       /* (make(1)'s note there: the saved pattern again) */
    pat_launch(0, 5, PW_END);
    check("edited back to the saved pattern and left: the live record dropped", !pat_live_at(0, 3) && !slg_has(PAT_LIX(0, 3)) && plays(0, 3, s3, s3n));
    /* SAVE + SEQ */
    base();
    pat_launch(0, 5, PW_END);
    pat_launch(0, 3, PW_END);
    (void)pat_store_slot(0, 3);
    check("SAVE + SEQ (the slot saved): the saved record is the edit, the live one gone, nothing unsaved",
          !pat_live_at(0, 3) && !slg_has(PAT_LIX(0, 3)) && saved_is(0, 3, ed, edn) && !(pat_changed() & 1u));
    /* SAVE + n into another slot: the source's live record stays */
    base();
    pat_launch(0, 5, PW_END);
    pat_launch(0, 3, PW_END);
    (void)pat_store_slot(0, 7);
    check("SAVE + 8 (another slot): slot 8 saved, slot 4's live record stays", saved_is(0, 7, ed, edn) && pat_live_at(0, 3) && plays(0, 3, ed, edn));
    /* RELOAD */
    base();
    check("RELOAD stopped: the edit (not left yet) dropped, slot 4's saved pattern in the work", pat_reload(0) == 0);
    remember(0);
    check("... the work is the saved pattern, nothing live, clean", encn[0] == s3n && !memcmp(enc[0], s3, s3n) && !pat_live_count() && !(pat_changed() & 1u));
    base();
    pat_launch(0, 5, PW_END);
    pat_launch(0, 3, PW_END);
    (void)pat_reload(0);
    remember(0);
    check("RELOAD of a live record (left and back): the record dropped in flash, the saved pattern back",
          !slg_has(PAT_LIX(0, 3)) && encn[0] == s3n && !memcmp(enc[0], s3, s3n));
    pat_cur[1] = PAT_NONE;
    check("RELOAD of a track from no slot: nothing (1)", pat_reload(1) == 1);
    /* clear */
    base();
    pat_launch(0, 5, PW_END);
    (void)pat_write(0, 3, 0);
    check("HOME + 4 (the slot cleared): its saved and live records gone", !pat_has(0, 3) && !pat_live_at(0, 3) && !slg_has(PAT_LIX(0, 3)));
    /* a stop keeps the edits too */
    base();
    pat_launch(0, PAT_NONE, PW_END);
    check("a track stopped (black 6): its edits kept", pat_cur[0] == PAT_NONE && plays(0, 3, ed, edn));
    /* a scene loaded stopped: the tracks leave */
    base();
    project_save(0);                                   /* scene A: T1 slot 4 (the edit written as saved? no: its slot) */
    edit(0, 66);
    remember(0);
    memcpy(enc[1], enc[0], encn[1] = encn[0]);
    section_load(0);
    check("a scene loaded stopped: the tracks' edits kept as their live patterns, the scene plays them",
          pat_live_at(0, pat_cur[0]) && plays(0, pat_cur[0], enc[1], encn[1]));
}

static void project_tests(void)
{
    int rc;
    base();
    pat_launch(0, 5, PW_END);
    rc = project_open(1);
    check("another project opened (stopped): its scope, no live pattern of the first seen", rc >= 0 && open_proj == 1u && !pat_live_count() &&
          !slg_has(PAT_LIX(0, 3)));
    edit(1, 55);
    pat_cur[1] = 2;
    rc = project_open(0);
    check("... back: the first project's live pattern there, played", rc >= 0 && pat_live_at(0, 3) && plays(0, 3, ed, edn));
    rc = project_open(1);
    check("... the second project kept its own edit (T2 slot 3, left at the switch)", rc >= 0 && pat_live_at(1, 2));
    (void)project_open(0);
    /* PROJECT SAVE: everything */
    for (rc = 0; rc < (int)NTRK; rc++)
        pat_launch((uint32_t)rc, 3, PW_END);
    edit(2, 44);
    remember(2);
    rc = project_save_all(0);
    check("PROJECT SAVE: done, no live pattern left, the saved records are the edits (slot 4 of T1 and T3)",
          rc == 0 && !pat_live_count() && saved_is(0, 3, ed, edn) && saved_is(2, 3, enc[2], encn[2]) && !pat_changed());
    check("... the state saved too", slg_has(SEC_ID_STATE));
    check("... RELOAD has nothing to go back to: the saved pattern is the edit", pat_reload(0) == 0 && (remember(0), encn[0] == edn && !memcmp(enc[0], ed, edn)));
    /* save as: the patterns as they play, the source keeps its live ones */
    base();
    pat_launch(0, 5, PW_END);
    rc = project_save_all(4);
    check("PROJECT SAVE into another slot (a save as): done, project 5 open, its slot 4 of T1 the edit as saved, nothing live there",
          rc == 0 && open_proj == 4u && saved_is(0, 3, ed, edn) && !pat_live_count());
    rc = project_open(0);
    check("... the source project kept its live pattern, its saved one untouched", rc >= 0 && pat_live_at(0, 3) && saved_is(0, 3, s3, s3n));
}

static void stale_tests(void)
{
    uint32_t b;
    base();
    pat_launch(0, 5, PW_END);
    b = slg.aseq[PAT_ID(0, 3)];
    (void)slg_put(PAT_ID(0, 3), s5, s5n, 0);           /* (an older firmware saved slot 4 over it: no live record cleared) */
    check("an older firmware's save of the slot: its live record stale (not played), the newer save plays",
          slg_has(PAT_LIX(0, 3)) && !pat_live_at(0, 3) && plays(0, 3, s5, s5n) && slg.aseq[PAT_ID(0, 3)] != b);
    restart();
    check("... swept at the next open (a tombstone), the saved one stays", !slg_has(PAT_LIX(0, 3)) && saved_is(0, 3, s5, s5n));
}

static void full_tests(void)
{
    uint32_t i, k;
    int rc = 0;
    base();
    {                                                  /* (the log filled with other records: ids 142..225, a later use's) */
        static uint8_t z[1000];
        static const uint16_t SZ[4] = {1000, 400, 150, 60};
        for (k = 0; k < 4u; k++)
            for (i = SLG_ID_MPAT0; i < SLG_IDS; i++)
                if (!slg_has(i))
                    z[0] = (uint8_t)i, (void)slg_put(i, z, SZ[k], 0);
    }
    pat_launch(0, 3, PW_END);
    for (k = 0; k < 16u; k++)
        trk[0].step[k].note[0] = (uint8_t)(30u + k), trk[0].step[k].n = 4, trk[0].step[k].time = ST_NOTE;
    trk[0].p[P_SLEN] = 64;
    for (k = 16; k < 64u; k++)
        trk[0].step[k].note[0] = (uint8_t)(30u + k), trk[0].step[k].n = 4, trk[0].step[k].time = ST_NOTE;
    last_msg[0] = 0;
    pat_launch(0, 5, PW_END);
    check("the log full: a left pattern's live record waits in RAM, \"MEM FULL: SAVE OR RELOAD\"",
          pat_live_at(0, 3) == 1 && !strcmp(last_msg, "MEM FULL: SAVE OR RELOAD"));
    rc = project_open(1);
    check("... another project refused (-2), said, the first still open, the record still waiting",
          rc == -2 && open_proj == 0u && !strcmp(last_msg, "MEM FULL: SAVE OR RELOAD") && pat_live_at(0, 3) == 1);
    for (i = SLG_ID_MPAT0; i < SLG_IDS; i++)
        (void)slg_put(i, gb, 0, 0);
    sections_write();
    check("... room made (patterns cleared): written at the next write, the project switch goes", pat_live_at(0, 3) == 2 && project_open(1) >= 0);
    /* the RAM full too */
    base();
    {
        static uint8_t junk[SEC_ARENA];
        uint32_t j;
        for (j = 0; sec_pend.used + 1100u <= SEC_ARENA && j < 48u; j++)
            (void)sec_pend_put(SEC_PEND_PAT + 16u + j, junk, 1100u);   /* (saved patterns of T2..DR waiting: the arena nearly full) */
        for (; sec_pend.used + 40u <= SEC_ARENA && j < 48u; j++)
            (void)sec_pend_put(SEC_PEND_PAT + 16u + j, junk, 40u);
        song.playing = 1;                              /* (playing: nothing reaches the log meanwhile) */
        for (j = 0; j < 16u; j++)
            trk[0].step[j].n = 4, trk[0].step[j].note[3] = (uint8_t)(70u + j), trk[0].step[j].time = ST_NOTE;
        (void)sec_capture();
        (void)pat_leave(&proj_tmp.cur, 0);
        song.playing = 0;
        check("the arena (RAM) full too: the edits lost, said by name (\"T1 4 EDITS LOST\")", !strcmp(last_msg, "T1 4 EDITS LOST") && !pat_live_at(0, 3));
        sec_pend_clear();
    }
}

/* a power cut at every program of op: after it (and a cold start) slot (0, 3) plays a or b, whole */
static void cut_case(const char *what, void (*setup)(void), void (*op)(void), const uint8_t *a, uint32_t an, const uint8_t *b, uint32_t bn)
{
    uint32_t n, p0, cuts = 0, ok = 1, olds = 0, news = 0;
    setup();
    p0 = progs;
    op();
    n = progs - p0;
    for (uint32_t c = 0; c <= n + 1u; c++) {
        setup();
        cut_in = (int)c;
        op();
        cut_in = -1;
        restart();
        if (plays(0, 3, a, an))
            olds++;
        else if (plays(0, 3, b, bn))
            news++;
        else
            ok = 0;
        cuts++;
    }
    printf("  %s: %u programs, %u cuts: old %u, new %u\n", what, n, cuts, olds, news);
    check(what, ok && n > 0u && (news > 0u || (an == bn && !memcmp(a, b, an))));
}
static void cs_base_left(void) { base(); }
static void op_leave(void) { pat_launch(0, 5, PW_END); }
static void cs_left(void) { base(); pat_launch(0, 5, PW_END); pat_launch(0, 3, PW_END); }
static void op_promote(void) { (void)project_save_all(0); }
static void op_reload(void) { (void)pat_reload(0); }
static uint8_t e2[SEC_REC_MAX];
static uint32_t e2n;
static void cs_edit_again(void) { base(); pat_launch(0, 5, PW_END); pat_launch(0, 3, PW_END); edit(0, 79); remember(0); memcpy(e2, enc[0], e2n = encn[0]); }
static void op_leave2(void) { pat_launch(0, 5, PW_END); }

int main(void)
{
    stopped_tests();
    project_tests();
    stale_tests();
    full_tests();
    cut_case("a power cut at every program of a launch's live write: slot 4 plays its saved or its edited version, whole",
             cs_base_left, op_leave, s3, s3n, ed, edn);
    cut_case("... of a PROJECT SAVE's promotion: the edit either way (live, or saved), whole", cs_left, op_promote, ed, edn, ed, edn);
    cut_case("... of a RELOAD: the edit or the saved pattern, whole", cs_left, op_reload, ed, edn, s3, s3n);
    {
        cs_edit_again();
        cut_case("... of a live record replaced by a newer one: the older or the newer edit, whole", cs_edit_again, op_leave2, ed, edn, e2, e2n);
    }
    printf("patlive test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
