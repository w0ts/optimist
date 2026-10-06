/* SPDX-License-Identifier: GPL-3.0-only */
/* Power-on resume (project.c autosave_tick / autosave_resume) on a simulated NOR: the working project captured,
 * put into OBJ_AUTOSAVE with its drum record (drum_store.c) and motion (motion_flash.c), the working state
 * wiped as at a fresh boot, then got back and applied. Every track must come back as it was: its engine,
 * preset, every parameter (ENV2's SUS2 REL2 DST2 too, stored in the drum track's ANALOG 2 slots), the drum
 * kit, the steps, the globals. Built by tests/run_tests.sh with FELUCCA_MOTION 0 and 1 (FELUCCA_SECTIONS 4 and
 * 16): with motion, proj_capture stores the patch under the motion, which must land in the same stored places.
 * With motion: a recorded motion that has moved a value is saved as the patch's value (the base). */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/project.c"

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
#include "../firmware/src/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/drum_store.c"
#if FELUCCA_MOTION
#include "../firmware/src/motion_flash.c"
#endif

static void song_backup(void) {}
static void song_restore(void) {}
static uint32_t arrangement_ready(void) { return 15u; }
static void arrangement_apply(uint32_t s) { (void)s; }

static int bad;
static void check(const char *what, int ok)
{
    printf("%-84s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* a project unlike the power-on one: other engines and presets on every part, every parameter off its
 * default (within its range), ENV2's values, another drum kit, steps on every track */
static void make(void)
{
    static const uint8_t ENG[NPART] = {1, 4, 9};    /* DIGITAL, SAMPLE, FM6 (UIDs) */
    uint32_t i, k;
    host_tracks_init();
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        if (k < NPART)
            host_preset_req(t, eng_slot(ENG[k]), 2u + k);
        for (i = 0; i < P_E0; i++) {
            const param_desc_t *d = &TP[i];
            int32_t span = d->max - d->min;
            if (span > 0)
                t->p[i] = (int16_t)(d->min + (int32_t)((i * 7u + k * 3u + 1u) % (uint32_t)(span + 1)));
        }
        for (i = 0; i < 8u && k < NPART; i++) {
            const param_desc_t *d = &ENGINES[t->eng_req]->edit[i];
            int32_t span = d->max - d->min;
            if (span > 0)
                t->p[P_E0 + i] = (int16_t)(d->min + (int32_t)((i * 5u + k + 2u) % (uint32_t)(span + 1)));
        }
        for (i = 0; i < 8u; i++)
            if (k == TRK_DRUM)
                dstep_set(&t->dstep[i], (i * 3u) % 16u, LV_NORM, 0);
            else {
                t->step[i].note[0] = (uint8_t)(40u + i + k);
                t->step[i].n = 1;
                t->step[i].time = ST_NOTE;
            }
    }
#if FELUCCA_ANALOG2
    for (k = TRK_DRUM, i = P_A2WAVE; i < P_E0; i++)
        trk[k].p[i] = TP[i].def;                     /* (the drum track: no ANALOG 2 values of its own) */
#endif
    TDRUM->p[P_E0] = 5;                              /* a kit other than the power-on one */
    song.g[G_SWING] = 55;
    memset(&dl, 0, sizeof dl);
}

/* the power-on state (felucca_init before autosave_resume): defaults, other engines */
static void fresh(void)
{
    uint32_t k;
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset_req(&trk[k], 0, 0);
    TDRUM->p[P_E0] = 0;
    memset(&dl, 0, sizeof dl);
#if FELUCCA_MOTION
    memset(&motion, 0, sizeof motion);
#endif
}

static track_t want[NTRK];
static int16_t want_g[G_COUNT];
static project_t buf;
static dlrec_t buf_dl;

/* autosave_tick's write, then a power cycle and autosave_resume's read */
static int cycle(void)
{
    proj_capture(&buf, &buf_dl);
    if (proj_put(OBJ_AUTOSAVE, &buf, &buf_dl))
        return 0;
#if FELUCCA_MOTION
    motion_flash_write(OBJ_AUTOSAVE, &buf);
    memset(motion_aux, 0, sizeof motion_aux);
    memset(motion_aux_p, 0, sizeof motion_aux_p);
#endif
    memset(&buf, 0, sizeof buf);
    memset(&buf_dl, 0, sizeof buf_dl);
    fresh();
    if (!proj_get(OBJ_AUTOSAVE, &buf, &buf_dl))
        return 0;
#if FELUCCA_MOTION
    motion_flash_read(OBJ_AUTOSAVE, &buf);
#endif
    proj_apply(&buf, &buf_dl, 1);
    return 1;
}

/* track k as it was: engine, preset, every parameter, the steps */
static int track_is(uint32_t k, char *why)
{
    uint32_t i;
    if (k < NPART && trk[k].eng_req != want[k].eng_req)
        return sprintf(why, "track %u: engine slot %u, was %u", k + 1u, trk[k].eng_req, want[k].eng_req), 0;
    if (k < NPART && trk[k].preset != want[k].preset)
        return sprintf(why, "track %u: preset %u, was %u", k + 1u, trk[k].preset, want[k].preset), 0;
    for (i = 0; i < P_COUNT; i++)
        if (trk[k].p[i] != want[k].p[i])
            return sprintf(why, "track %u: parameter %u is %d, was %d", k + 1u, i, trk[k].p[i], want[k].p[i]), 0;
    if (memcmp(trk[k].step, want[k].step, sizeof trk[k].step))
        return sprintf(why, "track %u: steps differ", k + 1u), 0;
    return 1;
}
static int all_back(const char *what)
{
    char why[128] = "";
    uint32_t k;
    int ok = 1;
    for (k = 0; k < NTRK && ok; k++)
        ok = track_is(k, why);
    if (ok && memcmp(song.g, want_g, sizeof want_g))
        ok = 0, sprintf(why, "the globals differ");
    if (!ok)
        printf("  %s\n", why);
    check(what, ok);
    return ok;
}

int main(void)
{
    memset(nor, 0xFF, sizeof nor);

    make();
    memcpy(want, trk, sizeof want);
    memcpy(want_g, song.g, sizeof want_g);
    want_g[G_MIDI] = 0;                              /* (a status, not saved) */
    song.g[G_MIDI] = 0;
    check("autosave written and read back", cycle());
    all_back("resume: every track's engine, preset, parameters (ENV2 too), kit, steps; the globals");
    check("resume: the drum kit", TDRUM->p[P_E0] == 5);

    make();                                          /* the same, saved a second time (copy B) */
    TDRUM->p[P_E0] = 7;
    trk[1].p[P_A2ESUS] = trk[1].p[P_A2ESUS] ? 0 : 1;
    memcpy(want, trk, sizeof want);
    song.g[G_MIDI] = 0;
    check("a second autosave (the other copy)", cycle());
    all_back("resume of the second: every track as it was");

#if FELUCCA_MOTION
    {   /* a motion recorded on the CHORUS send of track 2, playing its step: the patch's value is saved */
        int16_t base;
        make();
        song.g[G_MIDI] = 0;
        memcpy(want, trk, sizeof want);
        base = trk[1].p[P_CHOR];
        memset(&motion, 0, sizeof motion);
        motion_set_event(&trk[1], 0, P_CHOR, base == 0 ? 50 : 0);
        motion_begin();
        motion_step(&trk[1], 0);
        check("motion: the step moved the value", trk[1].p[P_CHOR] != base);
        check("motion: autosave written and read back", cycle());
        all_back("motion: every track as its patch was (the moved value: the base)");
        check("motion: the recorded motion came back with it", motion.count == 1u);
    }
#endif
    printf("resume test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
