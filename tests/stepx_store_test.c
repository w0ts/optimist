/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4's step extras kept with the projects (FELUCCA_SL24_XSTEP: the automation store's step-only events,
 * seq/auto.h; auto_proj.c, stepx_log.c; compared in 2.4's form, auto_to_stepx) on a simulated NOR
 * with the section log: PROJECT SAVE / LOAD keeps every track's nudges, locks and fills; none costs no record; a
 * section saved again without its extras record plays none (never another version's); a section stored while
 * playing keeps them in the arena over a warm reset and writes them with it; the stage applies them; the autosave's;
 * old projects load with none. Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_SL24_XSTEP 1
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
    printf("%-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static stepx_t want[NTRK];
static stepx_t *sx_now(uint32_t i)                     /* track i's step-only events in 2.4's form */
{
    static stepx_t x[NTRK];
    (void)auto_to_stepx(&x[i % NTRK], AUTO_L(i));
    return &x[i % NTRK];
}
static void make(uint32_t seed, int extras)            /* tracks as some section, with extras or none */
{
    uint32_t i, k;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = 16;
        for (k = 0; k < 4u; k++)
            if (i != TRK_DRUM) {
                trk[i].step[k].note[0] = (uint8_t)(40u + seed + k + i);
                trk[i].step[k].n = 1;
                trk[i].step[k].time = ST_NOTE;
            }
        AUTO_L(i)->n = 0;
        if (extras) {
            step_micro_set(&trk[i], (seed + i) % 64u, -3 - (int32_t)i);
            (void)auto_put(AUTO_L(i), ((seed + 2u * i) % 64u) | AUTO_ONLY, P_PAN, (int32_t)(20u + seed + i));
            (void)auto_put(AUTO_L(i), ((seed + 2u * i) % 64u) | AUTO_ONLY, P_LEVEL, -7);
            step_fill_set(&trk[i], (seed + 5u) % 64u, i & 1u ? FC_FILL : FC_NOFILL);
        }
        want[i] = *sx_now(i);
    }
}
static int same(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        if (memcmp(sx_now(i), &want[i], sizeof want[i]))
            return 0;
    return 1;
}
static stepx_t held[NTRK];
static void hold(void) { memcpy(held, want, sizeof held); }
static int back(void) { uint32_t i; for (i = 0; i < NTRK; i++) if (memcmp(sx_now(i), &held[i], sizeof held[i])) return 0; return 1; }
static int none(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        if (AUTO_L(i)->n)
            return 0;
    return 1;
}

int main(void)
{
    int ok;
    memset(nor, 0xFF, sizeof nor);
    auto_init();
    sec_pend_clear();
    sec_boot();
    song.playing = 0, transport_req = 0;
    make(1, 1);
    project_save(3);
    ok = !strcmp(last_msg, "SAVED") && slg_has(SX_ID0 + 3);
    hold();
    make(2, 0);
    project_load(3);
    check("SAVE D with nudges, locks, fills on every track; LOAD it back: the same", ok && back());
    make(4, 0);
    project_save(4);
    ok = !strcmp(last_msg, "SAVED") && !slg_has(SX_ID0 + 4);
    make(5, 1);
    project_load(4);
    check("a section with none: no extras record; loading it clears the working extras", ok && none());
    make(1, 1);
    project_save(3);
    make(6, 0);
    project_save(3);                                    /* D saved again with none: the record cleared */
    ok = !slg_has(SX_ID0 + 3);
    make(5, 1);
    project_load(3);
    check("D saved again without extras: its record cleared, D loads with none", ok && none());
    make(7, 1);
    project_save(5);
    {   /* F's record replaced behind its extras' back (a cut between the two writes, a restore, a snapshot) */
        int n;
        make(8, 0);
        n = (int)sec_capture();
        slg_put(5, sec_rbuf, (uint32_t)n, 0);
        make(9, 1);
        project_load(5);
        check("F's section record replaced without its extras: F plays none, never another version's", none());
    }
    make(10, 1);
    hold();
    song.playing = 1;
    section_store(12);
    ok = sec_pend_has(12) && sec_pend_has(SEC_IDS + 12);
    sec_boot();                                         /* a warm reset: .noinit keeps the arena */
    ok &= sec_pend_has(SEC_IDS + 12);
    make(11, 0);
    song.playing = 0;
    section_load(12);
    ok &= back();
    check("SAVE + key while playing: M's extras wait in the arena with it, over a warm reset", ok);
    sections_write();
    ok = slg_has(12) && slg_has(SX_ID0 + 12) && !sec_pend_has(12) && !sec_pend_has(SEC_IDS + 12);
    make(12, 0);
    project_load(12);
    check("... written when stopped: M and its extras in the log; LOAD M: the same", ok && back());
    {
        stepx_t keep[NTRK];
        uint32_t i;
        make(10, 1);
        memcpy(keep, want, sizeof keep);
        make(15, 0);
        ok = section_cue(12);
        arrangement_apply(12);
        for (i = 0; i < NTRK; i++)
            ok &= !memcmp(sx_now(i), &keep[i], sizeof keep[i]);
    }
    check("a live jump to M: the stage carries M's extras, the ISR applies them on the bar", ok);
    {   /* the autosave's extras: id SX_ID_AUTO, keyed by its project's sum */
        static project_t as;
        static dlrec_t ad;
        make(16, 1);
        proj_capture(&as, &ad);
        ok = sx_log_put(SX_ID_AUTO, as.sum, &as, 0) == 0 && slg_has(SX_ID_AUTO);
        make(17, 0);
        (void)auto_fresh(&as);                          /* (autosave_resume: its motion form read first) */
        sx_log_get(SX_ID_AUTO, as.sum, &as);
        proj_apply(&as, &ad, 1);
        make(16, 1), ok &= 1;
        {
            stepx_t keep[NTRK];
            uint32_t i;
            for (i = 0; i < NTRK; i++)
                keep[i] = want[i];
            make(17, 0);
            (void)auto_fresh(&as);
            sx_log_get(SX_ID_AUTO, as.sum, &as);
            proj_apply(&as, &ad, 1);
            for (i = 0; i < NTRK; i++)
                ok &= !memcmp(sx_now(i), &keep[i], sizeof keep[i]);
            (void)auto_fresh(&as);
            sx_log_get(SX_ID_AUTO, as.sum ^ 1u, &as);       /* another project's key: none */
            as.sum ^= 0;
            proj_apply(&as, &ad, 1);
            ok &= none();
        }
        check("the autosave's extras: written beside it, read back with it; another key: none", ok);
    }
    {   /* an old project (no extras anywhere): loads with none */
        static project_t old;
        static dlrec_t od;
        make(18, 1);
        proj_capture(&old, &od);
        old.dl_hash ^= 0;
        {
            auto_store_t *m = auto_for(&old, 0);
            if (m)
                m->psum = 0;                            /* (as read from an older save: nothing bound) */
        }
        proj_apply(&old, &od, 1);
        check("an old project (no extras stored with it) loads with none", none());
    }
    slg_boot();
    make(19, 0);
    project_load(12);
    make(10, 1);
    {
        stepx_t keep[NTRK];
        uint32_t i;
        for (i = 0; i < NTRK; i++)
            keep[i] = want[i];
        make(19, 0);
        project_load(12);
        ok = 1;
        for (i = 0; i < NTRK; i++)
            ok &= !memcmp(sx_now(i), &keep[i], sizeof keep[i]);
    }
    check("after a restart (the log scanned again): M loads with its extras", ok);
    printf("stepx store test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
