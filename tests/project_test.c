/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project formats (firmware/src/project.c, -DPROJ_HOST part). Format 6 ("FUN6": format 5
 * and the FM6 parts' voices, their operator switches and DX7 functions) is written; format 5 ("FUN5":
 * 10-byte steps with levels and ratchets, the drum track's 16 lanes, P_CHORD, P_FXOFF) is written;
 * format 4 ("FUN4", SLOOP 2.0 .. 2.2, before P_FXOFF), format 3 ("FUN3", SLOOP 1.x), format 2 ("FUN2", 53 parameters per track) and format 1 ("FUN1"), built
 * byte for byte as the firmware stored them, convert: every old value at its parameter, the parameters
 * added since at their defaults, the swings onto the MPC scale (x 0.8), synth steps as they were, the
 * drum track's notes onto its lanes (accent: hard), globals, selection, the engine bytes (kept; the
 * drum track's 0); damaged ones are refused. Run by tests/run_tests.sh (needs build/gen-host). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i)       /* ui.c TRK_DEF: ANALOG, DIGITAL, LOFI */
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/project.c"
static dlrec_t rt_dl;                            /* (the drum record a capture / apply takes: format 10) */
#define UP_HOST 1                                /* user presets: the bank part, with the engines (UPB1 migration) */
#define UP_WITH_ENGINES 1
#include "../firmware/src/upreset.c"
static dlrec_t tdl;                              /* the drum record of the projects captured here */

static int check(const char *what, int ok)
{
    printf("%-66s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* the value parameter k (old id) of track t had in the old project */
static int16_t oldv(uint32_t t, uint32_t k) { return (int16_t)(t * 100u + k * 3u + 1u); }

static const uint8_t OLD_ENG[NTRK] = {7, 0, 6, 8};   /* WHEEL, ANALOG, TRIO; the drum track: 8 (none) */
static void fill_old_steps(step8_t *st, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++) {
        step8_t *s = &st[k];
        s->note[0] = (uint8_t)(36u + (k + t) % 40u);
        s->note[1] = (uint8_t)(38u + k % 5u);
        s->n = (uint8_t)(k % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)(k & 3u);
        s->vel = (uint8_t)(64u + t);
    }
}
static void fill_v2_track(proj_trk_v2_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V2; k++)
        d->p[k] = oldv(t, k);
    d->engine = OLD_ENG[t];
    d->preset = (uint8_t)(t + 5u);
    fill_old_steps(d->step, t);
}
static void fill_v3_track(proj_trk_v3_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V3; k++)
        d->p[k] = oldv(t, k);
    d->p[P_SSWING] = 50;                           /* (swings: within 0..100) */
    d->p[P_ASWING] = 100;
    d->engine = OLD_ENG[t];
    d->preset = (uint8_t)(t + 5u);
    fill_old_steps(d->step, t);
}

/* the steps of a converted track against the old ones: synth as they were, drums onto lanes */
static int steps_ok(const proj_trk_t *n, const step8_t *o, int drum)
{
    uint32_t k, i;
    for (k = 0; k < NSTEP; k++) {
        if (drum) {
            const dstep_t *d = &n->dstep[k];
            uint32_t want = 0;
            if (o[k].time == ST_NOTE)
                for (i = 0; i < o[k].n; i++)
                    want |= 1u << lane_of_note(o[k].note[i]);
            if (dstep_mask(d) != want)
                return 0;
            for (i = 0; i < DRUM_LANES; i++)
                if ((want >> i) & 1u && dstep_lvl(d, i) != ((o[k].flags & SF_ACCENT) ? LV_HARD : vel_lvl(o[k].vel)))
                    return 0;
        } else {
            const step_t *s = &n->step[k];
            if (memcmp(s->note, o[k].note, 4) || s->n != o[k].n || s->time != o[k].time || s->flags != o[k].flags ||
                s->vel != o[k].vel || s->lvl || s->rat)
                return 0;
        }
    }
    return 1;
}

/* track t converted from format 2 / 1 has the old values where they belong */
static int track_ok_v2(const proj_trk_t *n, const proj_trk_v2_t *o, uint32_t t)
{
    uint32_t k;
    int ok = (t == TRK_DRUM ? n->engine == 0 && n->preset == 0 : n->engine == o->engine && n->preset == o->preset) &&
             steps_ok(n, o->step, t == TRK_DRUM);
    for (k = 0; k <= P_DETUNE; k++)
        if (k != P_SSWING && k != P_ASWING)
            ok &= n->p[k] == oldv(t, k);
    ok &= n->p[P_SLCR] == 0 && n->p[P_SLPAT] == TP[P_SLPAT].def && n->p[P_SLRATE] == TP[P_SLRATE].def &&
          n->p[P_SLDEPTH] == TP[P_SLDEPTH].def && n->p[P_CHORD] == 0 && n->p[P_FXOFF] == 0;
    for (k = 0; k < 8u; k++)
        ok &= n->p[PJ_E0 + k] == oldv(t, 45u + k);
    return ok;
}

int main(void)
{
    static project_v3_t v3;
    static project_v2_t v2;
    static project_v1_t v1;
    static project_t q, q2;
    static union {
        project_t v4;
        project_v3_t v3;
        project_v2_t v2;
        project_v1_t v1;
    } buf;
    uint32_t i, t;
    int bad = 0, ok;

#if FELUCCA_ANALOG2
    bad += check("layout: CHORD, FXOFF, ANALOG 2's sixteen before P_E0 (67), the stored 69 (ENV2's 6 out, in 3 words)",
                 P_CHORD + 1 == P_FXOFF && P_FXOFF + 1 == P_A2WAVE && P_A2SDTN + 1 == P_A2ESUS && P_A2ESDT + 1 == P_E0 &&
                 P_E0 == 67 && P_COUNT == PROJ_NP_V5 + 16u && PJ_NP == PROJ_NP_V8 && PROJ_XN == 6u && PROJ_XW == 3u && PJ_E0 == 61 && PROJ_NP_V5 == PROJ_NP_V4 + 1u && PROJ_NP_V4 == PROJ_NP_V3 + 1u &&
                 P_SLDEPTH + 1 == P_CHORD);
#else
    bad += check("layout: CHORD, FXOFF just before P_E0 (51), P_COUNT = format 4's + 1",
                 P_CHORD + 1 == P_FXOFF && P_FXOFF + 1 == P_E0 && P_E0 == 51 && P_COUNT == PROJ_NP_V4 + 1u &&
                 PROJ_NP_V4 == PROJ_NP_V3 + 1u && P_SLDEPTH + 1 == P_CHORD);
#endif
    /* .noinit (app.ld NOINIT, 0x3D50 B): the 4 slots and the rest (fm1_crash 64, felucca_dbg 76, bootguard 12,
     * panel 32, settings 20 = 204 B in the 2026-10-05 build, nm): 256 B kept for them */
    bad += check("today's format fits one flash object; 4 slots + 256 B fit .noinit", sizeof(project_t) <= 4096u - 256u &&
                 4u * sizeof(project_t) + 256u <= 0x3D50u);

    /* format 3 (SLOOP 1.x) */
    memset(&v3, 0, sizeof v3);
    v3.magic = PROJ_MAGIC_V3;
    v3.size = sizeof v3;
    for (i = 0; i < PROJ_NG_V3; i++)
        v3.g[i] = (int16_t)(300 + i);
    v3.g[G_SWING] = 50;
    v3.sel = 3;
    for (t = 0; t < NTRK; t++)
        fill_v3_track(&v3.t[t], t);
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    memcpy(&buf, &v3, sizeof v3);
    ok = proj_import(&q, &buf, (int)sizeof v3);
    bad += check("FUN3 -> today's: converted, valid slot", ok && proj_ok(&q) && q.magic == PROJ_MAGIC);
    ok = q.sel == 3 && q.g[G_SWING] == 40;
    for (i = 0; i < PROJ_NG_V3; i++)
        ok &= i == G_SWING || q.g[i] == (int16_t)(300 + i);
    for (i = PROJ_NG_V3; i < PJ_NG; i++)
        ok &= q.g[i] == GP[i].def;
    bad += check("FUN3 -> today's: globals (swing 50 -> 40: the MPC scale), the new ones default", ok);
    ok = 1;
    for (t = 0; t < NTRK; t++) {
        const proj_trk_t *n = &q.t[t];
        uint32_t k;
        ok &= (t == TRK_DRUM ? n->engine == 0 : n->engine == OLD_ENG[t]) && steps_ok(n, v3.t[t].step, t == TRK_DRUM);
        for (k = 0; k < PROJ_NP_V3 - 8u; k++)
            if (k != P_SSWING && k != P_ASWING)
                ok &= n->p[k] == oldv(t, k);
        ok &= n->p[P_SSWING] == 40 && n->p[P_ASWING] == 80 && n->p[P_CHORD] == 0;
        for (k = 0; k < 8u; k++)
            ok &= n->p[PJ_E0 + k] == oldv(t, PROJ_NP_V3 - 8u + k);
    }
    bad += check("FUN3 -> today's: parameters (P_E0.. moved), steps, drum notes -> lanes", ok);

    /* format 2, as written before the SLICER */
    memset(&v2, 0, sizeof v2);
    v2.magic = PROJ_MAGIC_V2;
    v2.size = sizeof v2;
    for (i = 0; i < PROJ_NG_V2; i++)
        v2.g[i] = (int16_t)(500 + i);
    v2.sel = 2;
    for (t = 0; t < NTRK; t++)
        fill_v2_track(&v2.t[t], t);
    v2.sum = proj_hash(&v2, sizeof v2 - 4u);
    bad += check("FUN2 image is 2552 bytes (as stored)", sizeof v2 == 2552u);
    memcpy(&buf, &v2, sizeof v2);
    ok = proj_import(&q, &buf, (int)sizeof v2);
    bad += check("FUN2 -> today's: converted, valid slot", ok && proj_ok(&q) && q.magic == PROJ_MAGIC);
    ok = q.sel == 2;
    for (i = 0; i < PROJ_NG_V2; i++)
        ok &= i == G_SWING || q.g[i] == (int16_t)(500 + i);
    bad += check("FUN2 -> today's: globals and selected track", ok);
    ok = 1;
    for (t = 0; t < NTRK; t++)
        ok &= track_ok_v2(&q.t[t], &v2.t[t], t);
    bad += check("FUN2 -> today's: every parameter mapped, SLICER OFF, CHORD OFF (4 tracks)", ok);
    bad += check("FUN2 -> today's: engine bytes kept (WHEEL 7, ANALOG 0, TRIO 6), drum 0",
                 q.t[0].engine == 7 && q.t[1].engine == 0 && q.t[2].engine == 6 && q.t[3].engine == 0 &&
                 str_eq(ENGINES[7]->name, "WHEEL") && str_eq(ENGINES[6]->name, "TRIO") && NENGINES > 8);

    /* format 4 (SLOOP 2.0 .. 2.2): every value at its id, P_FXOFF its default (the effects on), E0.. moved */
    {
        static project_v4_t v4;
        memset(&v4, 0, sizeof v4);
        v4.magic = PROJ_MAGIC_V4;
        v4.size = sizeof v4;
        for (i = 0; i < PJ_NG; i++)
            v4.g[i] = (int16_t)(400 + i);
        v4.sel = 2;
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < PROJ_NP_V4; i++)
                v4.t[t].p[i] = oldv(t, i);
            v4.t[t].engine = (uint8_t)(t == 1u ? 9u : t);
            v4.t[t].preset = (uint8_t)(t + 1u);
            v4.t[t].step[4].note[0] = (uint8_t)(60u + t);
            v4.t[t].step[4].n = 1;
            v4.t[t].step[4].lvl = 0x0E;
            v4.t[t].step[4].rat = 0x03;
        }
        v4.sum = proj_hash(&v4, sizeof v4 - 4u);
        memcpy(&buf, &v4, sizeof v4);
        ok = proj_import(&q, &buf, (int)sizeof v4) && proj_ok(&q) && q.magic == PROJ_MAGIC && q.sel == 2 &&
             q.g[G_DUST] == 400 + G_DUST && q.g[G_VIEW] == 400 + G_VIEW;
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < P_FXOFF; i++)                /* (SUPER -> ANALOG: the level trim is ANALOG's) */
                ok &= q.t[t].p[i] == oldv(t, i) || (FELUCCA_ANALOG2 && t == 1u && i == P_ED_FX);
            ok &= q.t[t].p[P_FXOFF] == 0;
            ok &= !memcmp(&q.t[t].step[4], &v4.t[t].step[4], sizeof q.t[t].step[4]);
#if FELUCCA_ANALOG2
            if (t == 1u) {                               /* SUPER (9), preset 2 -> ANALOG SUPER CHRD, its values */
                const preset_t *pr = &ENG_ANALOG.presets[PROJ_A2_SUPER + 2u];
                ok &= q.t[t].engine == 0 && q.t[t].preset == PROJ_A2_SUPER + 2u &&
                      q.t[t].p[P_A2SWRM] == oldv(t, PROJ_NP_V4 - 8u) && q.t[t].p[P_A2SDTN] == oldv(t, PROJ_NP_V4 - 7u) &&
                      q.t[t].p[P_A2DRFT] == oldv(t, PROJ_NP_V4 - 5u) && q.t[t].p[PJ_E0 + 4] == oldv(t, PROJ_NP_V4 - 3u) &&
                      q.t[t].p[PJ_E0 + 5] == oldv(t, PROJ_NP_V4 - 2u) && q.t[t].p[P_A2FTYP] == oldv(t, PROJ_NP_V4 - 1u) &&
                      q.t[t].p[PJ_E0] == pr->e[0] && q.t[t].p[PJ_E0 + 2] == pr->e[2] && q.t[t].p[PJ_E0 + 7] == pr->e[7];
                continue;
            }
#endif
            for (i = 0; i < 8u; i++)
                ok &= q.t[t].p[PJ_E0 + i] == oldv(t, PROJ_NP_V4 - 8u + i);
            ok &= q.t[t].engine == v4.t[t].engine && q.t[t].preset == v4.t[t].preset;
        }
#if FELUCCA_ANALOG2
        bad += check("FUN4 -> FUN7: values at their ids, FX on, E0..E7 moved, steps, SUPER -> ANALOG swarm", ok);
#else
        bad += check("FUN4 -> FUN6: values at their ids, FX on, E0..E7 moved, steps", ok);
#endif
        v4.t[3].p[7]++;
        memcpy(&buf, &v4, sizeof v4);
        bad += check("FUN4 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v4));
        memcpy(&q2, &q, sizeof q);
    }

#if FELUCCA_ANALOG2
    {   /* format 5 (SLOOP plus): every value at its id, ANALOG 2's parameters their defaults, E0.. moved */
        typedef struct { int16_t p[PROJ_NP_V5]; uint8_t engine, preset; step_t step[NSTEP]; } t5_t;
        typedef struct { uint32_t magic, size; int16_t g[PJ_NG]; uint8_t sel, rsv[3]; t5_t t[NTRK]; uint32_t sum; } p5_t;
        static p5_t v5;
        static union { p5_t v5; project_t q; } b5;
        _Static_assert(sizeof(p5_t) == 3120u, "format 5 as it was stored");
        memset(&v5, 0, sizeof v5);
        v5.magic = PROJ_MAGIC_V5;
        v5.size = sizeof v5;
        for (i = 0; i < PJ_NG; i++)
            v5.g[i] = (int16_t)(500 + i);
        v5.sel = 1;
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < PROJ_NP_V5; i++)
                v5.t[t].p[i] = oldv(t, i);
            v5.t[t].engine = (uint8_t)t;
            v5.t[t].preset = (uint8_t)(t + 2u);
            v5.t[t].step[7].note[0] = (uint8_t)(50u + t);
            v5.t[t].step[7].n = 1;
        }
        v5.sum = proj_hash(&v5, sizeof v5 - 4u);
        memcpy(&b5, &v5, sizeof v5);
        ok = proj_import(&q2, &b5, (int)sizeof v5) && proj_ok(&q2) && q2.magic == PROJ_MAGIC && q2.sel == 1 &&
             q2.g[G_DUST] == 500 + G_DUST;
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i <= P_FXOFF; i++)
                ok &= q2.t[t].p[i] == oldv(t, i);
            for (i = P_A2WAVE; i < PJ_E0; i++)             /* (the drum track's first nine: the parts' ENV2, 0) */
                ok &= q2.t[t].p[i] == (t == TRK_DRUM && i < P_A2WAVE + NPART * PROJ_XW ? 0 : TP[i].def);
            for (i = 0; i < 8u; i++)
                ok &= q2.t[t].p[PJ_E0 + i] == oldv(t, PROJ_NP_V5 - 8u + i);
            ok &= q2.t[t].engine == t && q2.t[t].preset == t + 2u && !memcmp(&q2.t[t].step[7], &v5.t[t].step[7], sizeof v5.t[t].step[7]);
        }
        bad += check("FUN5 -> FUN7: values at their ids, ANALOG 2's defaults, E0..E7 moved, steps", ok);
        v5.t[0].engine = 10;                             /* DX7 in SLOOP plus: FM6 now */
        v5.sum = proj_hash(&v5, sizeof v5 - 4u);
        memcpy(&b5, &v5, sizeof v5);
        ok = proj_import(&q2, &b5, (int)sizeof v5) && q2.t[0].engine == ENG_IX_FM6 && !q2.fm6_has &&
             q2.fm6_fn[0][0] < 0 && q2.t[0].p[PJ_E0] == oldv(0, PROJ_NP_V5 - 8u);
        bad += check("FUN5 -> FUN7: a DX7 track plays FM6 (its VOICE), FM6 functions default", ok);
        v5.t[2].p[3]++;
        memcpy(&b5, &v5, sizeof v5);
        bad += check("FUN5 with a bad checksum: refused", !proj_import(&q2, &b5, (int)sizeof v5));
        memcpy(&q2, &q, sizeof q);
    }
    {   /* SUPER's presets are ANALOG's 16..20, in order (project.c PROJ_A2_SUPER) */
        static const char *const SUP[5] = {"SUPER LEAD", "SUPER PAD", "SUPER CHRD", "SUPER PLCK", "HOOVER SAW"};
        ok = ENG_ANALOG.npresets >= PROJ_A2_SUPER + 5u;
        for (i = 0; ok && i < 5u; i++)
            ok &= !strcmp(ENG_ANALOG.presets[PROJ_A2_SUPER + i].name, SUP[i]);
        bad += check("SUPER's presets 0..4 = ANALOG's 16..20 (old SUPER tracks)", ok);
    }
    {   /* FM6's FUN6 (test builds): format 5 + the FM6 voices, SUPER 9, FM6 10 */
        typedef struct { int16_t p[PROJ_NP_V5]; uint8_t engine, preset; step_t step[NSTEP]; } t6_t;
        typedef struct {
            uint32_t magic, size; int16_t g[PJ_NG]; uint8_t sel, rsv[3]; t6_t t[NTRK];
            uint8_t fm6[NPART][128]; uint8_t fm6_on[NPART], fm6_has; int8_t fm6_fn[NPART][16]; uint32_t sum;
        } p6_t;
        static p6_t v6;
        static union { p6_t v6; project_t q; } b6;
        static const int16_t SV[8] = {5, 60, 81, 12, 3, 101, 18, 1};   /* SUPR SDTN MIX DRFT SUB CUT RES FTYP */
        const preset_t *pr = &ENG_ANALOG.presets[PROJ_A2_SUPER + 4u];
        memset(&v6, 0, sizeof v6);
        v6.magic = PROJ_MAGIC_V6;
        v6.size = sizeof v6;
        for (i = 0; i < PJ_NG; i++)
            v6.g[i] = (int16_t)(600 + i);
        v6.sel = 2;
        for (t = 0; t < NTRK; t++)
            for (i = 0; i < PROJ_NP_V5; i++)
                v6.t[t].p[i] = oldv(t, i);
        v6.t[0].engine = 10;                             /* FM6 */
        v6.t[1].engine = 9;                              /* SUPER, HOOVER SAW */
        v6.t[1].preset = 4;
        memcpy(&v6.t[1].p[PROJ_NP_V5 - 8u], SV, sizeof SV);
        v6.t[2].engine = 6;
        for (i = 0; i < 128u; i++)
            v6.fm6[0][i] = (uint8_t)(i * 7u);
        v6.fm6_on[0] = 0x3D;
        v6.fm6_has = 1;
        memset(v6.fm6_fn, 0xFF, sizeof v6.fm6_fn);
        v6.fm6_fn[0][0] = 5;
        v6.sum = proj_hash(&v6, sizeof v6 - 4u);
        memcpy(&b6, &v6, sizeof v6);
        ok = sizeof v6 != sizeof(project_t) && proj_import(&q2, &b6, (int)sizeof v6) && proj_ok(&q2) &&
             q2.magic == PROJ_MAGIC && q2.sel == 2 && q2.g[G_DUST] == 600 + G_DUST &&
             q2.t[0].engine == ENG_IX_FM6 && !memcmp(q2.fm6[0], v6.fm6[0], 128) && q2.fm6_on[0] == 0x3D &&
             q2.fm6_has == 1 && q2.fm6_fn[0][0] == 5 && q2.fm6_fn[1][0] < 0 && q2.t[2].engine == 6 &&
             q2.t[0].p[P_A2WAVE] == TP[P_A2WAVE].def && q2.t[2].p[PJ_E0 + 3] == oldv(2, PROJ_NP_V5 - 5u);
        bad += check("FM6's FUN6 -> FUN7: FM6 part and its voice, values by count", ok);
        ok = q2.t[1].engine == 0 && q2.t[1].preset == PROJ_A2_SUPER + 4u && q2.t[1].p[P_A2SWRM] == 5 &&
             q2.t[1].p[P_A2SDTN] == 60 && q2.t[1].p[P_A2DRFT] == 12 && q2.t[1].p[PJ_E0 + 4] == 101 &&
             q2.t[1].p[PJ_E0 + 5] == 18 && q2.t[1].p[P_A2FTYP] == 1 && q2.t[1].p[PJ_E0] == pr->e[0] &&
             q2.t[1].p[PJ_E0 + 2] == pr->e[2] && q2.t[1].p[P_A2WAVE] == 2 && q2.t[1].p[P_A2SEMI] == -12;
        bad += check("FM6's FUN6 -> FUN7: a SUPER track on ANALOG's swarm (HOOVER SAW, its own values)", ok);
        v6.fm6[2][5] ^= 1u;
        memcpy(&b6, &v6, sizeof v6);
        bad += check("FM6's FUN6 with a bad checksum: refused", !proj_import(&q2, &b6, (int)sizeof v6));
    }
    {   /* ANALOG 2's FUN6 (test builds): today's tracks, FM6's numbering (9: was DX7), no FM6 voices */
        typedef struct { uint32_t magic, size; int16_t g[PJ_NG]; uint8_t sel, rsv[3]; proj_trk_t t[NTRK]; uint32_t sum; } p6_t;
        static p6_t v6;
        static union { p6_t v6; project_t q; } b6;
        memset(&v6, 0, sizeof v6);
        v6.magic = PROJ_MAGIC_V6;
        v6.size = sizeof v6;
        v6.sel = 1;
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < PJ_NP; i++)
                v6.t[t].p[i] = oldv(t, i);
            v6.t[t].engine = (uint8_t)(t == 0u ? 9u : t);
        }
        v6.sum = proj_hash(&v6, sizeof v6 - 4u);
        memcpy(&b6, &v6, sizeof v6);
        memset(&v6.t[TRK_DRUM].p[P_A2WAVE], 0, NPART * PROJ_XW * 2u);   /* (what the import leaves there: ENV2's 0s) */
        ok = proj_import(&q2, &b6, (int)sizeof v6) && proj_ok(&q2) && q2.sel == 1 && !q2.fm6_has &&
             !memcmp(q2.t, v6.t, sizeof q2.t) && q2.t[0].engine == ENG_IX_FM6;
        bad += check("ANALOG 2's FUN6 -> FUN7: tracks as stored (9 = FM6), no FM6 voice", ok);
    }
#endif
    /* a FUN5 round trip: stored as is (an engine added since: 8) */
    q.t[1].engine = 8;
    q.t[0].step[3].lvl = 0x9C;
    q.t[0].step[3].rat = 0x27;
    dstep_set(&q.t[TRK_DRUM].dstep[5], 13, LV_GHOST, 2);
    q.sum = proj_sum(&q);
    memcpy(&buf, &q, sizeof q);
    q.t[2].p[P_FXOFF] = 1;
    q.sum = proj_sum(&q);
    memcpy(&buf, &q, sizeof q);
    bad += check("today's format round trip: as stored (levels, ratchets, lanes, engine 8)",
                 proj_import(&q2, &buf, (int)sizeof q) && !memcmp(&q, &q2, sizeof q) && q2.t[1].engine == 8);

    /* damaged / wrong size */
    v2.t[1].p[3]++;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v2));
    v2.t[1].p[3]--;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a wrong length: refused", !proj_import(&q2, &buf, (int)sizeof v2 - 2));
    memcpy(&buf, &q, sizeof q);
    buf.v4.magic = PROJ_MAGIC_V3;
    bad += check("FUN5 size with a FUN3 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));
    memcpy(&buf, &v3, sizeof v3);
    buf.v3.t[2].step[7].vel ^= 1u;
    bad += check("FUN3 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v3));

    /* format 1: one instrument -> track 1, the others their defaults */
    memset(&v1, 0, sizeof v1);
    v1.magic = PROJ_MAGIC_V1;
    v1.size = sizeof v1;
    for (i = 0; i < PROJ_NG_V2; i++)
        v1.g[i] = (int16_t)(700 + i);
    fill_v2_track(&v1.t, 0);
    v1.sum = proj_hash(&v1, sizeof v1 - 4u);
    memcpy(&buf, &v1, sizeof v1);
    ok = proj_import(&q, &buf, (int)sizeof v1) && proj_ok(&q) && track_ok_v2(&q.t[0], &v1.t, 0) && q.g[5] == 705;
    for (t = 1; t < NTRK; t++)
        ok &= q.t[t].preset == 0xFF && q.t[t].p[P_SLCR] == 0 && q.t[t].p[P_LEVEL] == TP[P_LEVEL].def &&
              q.t[t].p[PJ_E0] == ENGINES[trk_def_engine(t)]->edit[0].def &&
              (t == TRK_DRUM ? dstep_mask(&q.t[t].dstep[0]) == 0u : q.t[t].step[0].time == ST_REST);
    bad += check("FUN1 -> today's: track 1 mapped, tracks 2..4 defaults", ok);

    /* capture / apply: the working project round trip */
    host_tracks_init();
    for (t = 0; t < NTRK; t++)
        trk[t].p[P_SLEN] = (int16_t)(5 + t);
    trk[1].step[2].n = 2, trk[1].step[2].note[0] = 60, trk[1].step[2].note[1] = 64, trk[1].step[2].time = ST_NOTE;
    trk[1].step[2].lvl = 0x0D;
    dstep_set(&TDRUM->dstep[9], 4, LV_SOFT, 1);
    song.g[G_DUST] = 33;
    trk[3].p[P_FXOFF] = 1;
    proj_capture(&q, &tdl);
    host_tracks_init();
    proj_apply(&q, &tdl, 1);
    ok = trk[2].p[P_SLEN] == 7 && trk[1].step[2].n == 2 && trk[1].step[2].lvl == 0x0D && song.g[G_DUST] == 33 &&
         dstep_has(&TDRUM->dstep[9], 4) && dstep_lvl(&TDRUM->dstep[9], 4) == LV_SOFT && dstep_rat(&TDRUM->dstep[9], 4) == 1u &&
         trk[3].p[P_FXOFF] == 1 && trk[0].p[P_FXOFF] == 0;
    bad += check("the working project: capture -> apply round trip (levels, lanes, DUST, FX off)", ok);
    {   /* SYNC: a project from before the clock has 0 there (INT): it stays INT; the clock followed (G_MIDI) is
         * a status, never saved; a new one is AUTO */
        project_t o = q;
        o.g[G_SYNC] = 0;
        o.g[G_MIDI] = FELUCCA_MIDI_CH ? 0 : 2;       /* (with MIDI_CH the slot holds the channels: 0 = defaults) */
        song.g[G_MIDI] = 1;
        proj_apply(&o, &tdl, 1);
        ok = song.g[G_SYNC] == SYNC_INT && song.g[G_MIDI] == 1;
        song.g[G_SYNC] = SYNC_TRS;
        proj_capture(&o, &tdl);
        ok &= o.g[G_SYNC] == SYNC_TRS && o.g[G_MIDI] == 0 && GP[G_SYNC].def == SYNC_AUTO;
        bad += check("SYNC: an old project (0) stays INT, USB / TRS kept, CLK not saved, new: AUTO", ok);
    }

#if !FELUCCA_ANALOG2                                 /* (ANALOG 2: FUN5 -> FUN7 above) */
    /* format 5 -> 6: the same tracks, no FM6 voice (the parts load their VOICE), the functions' defaults */
    {
        static project_v5_t v5;
        memset(&v5, 0, sizeof v5);
        v5.magic = PROJ_MAGIC_V5;
        v5.size = sizeof v5;
        memcpy(v5.g, q.g, sizeof v5.g);
        v5.sel = 1;
        memcpy(v5.t, q.t, sizeof v5.t);
        v5.t[0].engine = (uint8_t)ENG_IX_FM6;               /* (engine 10: DX7 in SLOOP plus, FM6 now) */
        v5.sum = proj_hash(&v5, sizeof v5 - 4u);
        ok = proj_import(&q2, &v5, (int)sizeof v5) && proj_ok(&q2) && q2.magic == PROJ_MAGIC && q2.sel == 1 &&
             !memcmp(q2.t, v5.t, sizeof q2.t) && !q2.fm6_has && q2.fm6_fn[0][0] < 0 && q2.fm6_fn[2][0] < 0;
        bad += check("FUN5 -> FUN6: tracks as stored, no FM6 voice, FM6 functions default", ok);
        v5.t[1].p[2]++;
        bad += check("FUN5 with a bad checksum: refused", !proj_import(&q2, &v5, (int)sizeof v5));
    }
#endif

    /* FM6: the parts' voices (edits and switches) and functions saved and loaded back */
    {
        int16_t *ed = fm6_ed[0];
        static int16_t keep[FM6_NP];
        host_tracks_init();
        host_preset(&trk[0], ENG_IX_FM6, 3);              /* BELLS */
        trk[0].eng_req = trk[0].engine = (uint8_t)ENG_IX_FM6;
        fm6_sync(&trk[0], 0);                            /* the buffer loaded from VOICE */
        ed[FV_ALG] = 21;
        ed[FM6_OPB(3) + FO_OL] = 42;
        ed[FV_NAME] = 'Q';
        ed[FV_ON + 1] = 0;                               /* OP2 off */
        ed[FN_PTIME] = 77;
        ed[FN_PMODE] = 1;
        memcpy(keep, ed, sizeof keep);
        proj_capture(&q, &tdl);
        ok = q.fm6_has == 1u && q.fm6_on[0] == 0x3Du && q.fm6_fn[0][FN_PTIME - FN_PBUP] == 77;
        bad += check("FM6 part captured: its voice (packed), the switches, the functions; other parts none", ok);
        fm6_from_rom(ed, &FM6_INIT);
        fm6_fn_reset(ed);
        fm6_cur[0] = 0;
        proj_apply(&q, &tdl, 1);
        ok = !memcmp(ed, keep, sizeof keep) && fm6_cur[0] == trk[0].p[P_E0] + 1 && fm6_fnok[0];
        bad += check("FM6 part applied: the voice with its edits, switches, functions (VOICE not reloaded)", ok);
        trk[0].eng_req = 0;                              /* not FM6 when captured: nothing kept, VOICE afresh */
        proj_capture(&q, &tdl);
        ok = !(q.fm6_has & 1u);
        trk[0].eng_req = (uint8_t)ENG_IX_FM6;
        q.t[0].engine = (uint8_t)ENG_IX_FM6;
        proj_apply(&q, &tdl, 1);
        bad += check("FM6 part from a project without its voice: VOICE loads afresh", ok && fm6_cur[0] == 0);
    }
#if FELUCCA_ANALOG2
    {   /* ANALOG 2's ENV2 (FUNB): each part's SUS2 REL2 and amounts PIT SHP OSC2 SDTN, a byte each in the drum
         * track's ANALOG 2 slots (three words a part); FLT's amount (FENV) stored with the part; the drum track's own
         * ANALOG 2 values its defaults. FUNA / FUN9 (SUS2 REL2 DST2 a word each): DST2 X with AMT2 A -> A on X;
         * FUN8 (no ENV2 extras): 0, the AD envelope */
        uint32_t t;
        static project_t va, keep;
        host_tracks_init();
        for (t = 0; t < NPART; t++) {
            trk[t].p[P_A2ESUS] = (int16_t)(10 + t);
            trk[t].p[P_A2EREL] = (int16_t)(120 + t);
            trk[t].p[P_A2EPIT] = (int16_t)(-64 + (int)t);
            trk[t].p[P_A2ESHP] = (int16_t)(63 - (int)t);
            trk[t].p[P_A2EOS2] = (int16_t)(-1 - (int)t);
            trk[t].p[P_A2ESDT] = (int16_t)(5 * t);
            trk[t].p[P_A2FENV] = (int16_t)(-20 - (int)t);
            trk[t].p[P_A2FATK] = (int16_t)(30 + t);
            trk[t].p[P_E7] = (int16_t)(40 + t);
        }
        proj_capture(&q, &rt_dl);
        ok = sizeof q == 3640u && q.magic == PROJ_MAGIC && PROJ_MAGIC == 0x46554E42u && PROJ_MAGIC_VA == 0x46554E41u &&
             (uint16_t)q.t[TRK_DRUM].p[P_A2WAVE + 3u] == (11u | 121u << 8) &&
             (uint16_t)q.t[TRK_DRUM].p[P_A2WAVE + 4u] == (0xC1u | 62u << 8) &&
             (uint16_t)q.t[TRK_DRUM].p[P_A2WAVE + 5u] == (0xFEu | 5u << 8) &&
             q.t[1].p[P_A2FENV] == -21 && q.t[1].p[P_A2FATK] == 31 && q.t[1].p[PJ_E0 + 7] == 41;
        bad += check("ENV2 (FUNB, 3,640 bytes): part 1's SUS2|REL2, PIT|SHP, OSC2|SDTN a byte each in the drum track", ok);
        for (t = 0; t < NTRK; t++)
            for (i = P_A2ESUS; i < P_E0; i++)
                trk[t].p[i] = 0;
        for (t = 0; t < NTRK; t++)
            trk[t].p[P_E7] = 0;
        proj_apply(&q, &rt_dl, 1);
        ok = 1;
        for (t = 0; t < NPART; t++)
            ok &= trk[t].p[P_A2ESUS] == (int16_t)(10 + t) && trk[t].p[P_A2EREL] == (int16_t)(120 + t) &&
                  trk[t].p[P_A2EPIT] == (int16_t)(-64 + (int)t) && trk[t].p[P_A2ESHP] == (int16_t)(63 - (int)t) &&
                  trk[t].p[P_A2EOS2] == (int16_t)(-1 - (int)t) && trk[t].p[P_A2ESDT] == (int16_t)(5 * t) &&
                  trk[t].p[P_A2FENV] == (int16_t)(-20 - (int)t) && trk[t].p[P_A2FATK] == (int16_t)(30 + t);
        ok &= trk[1].p[P_E7] == 41 && TDRUM->p[P_A2WAVE] == TP[P_A2WAVE].def && TDRUM->p[P_A2FDEC] == TP[P_A2FDEC].def;
        for (i = P_A2ESUS; i < P_E0; i++)
            ok &= TDRUM->p[i] == TP[i].def;
        bad += check("ENV2: applied back to the parts (every byte, the negative amounts), E0..E7 at their ids, the drum track's defaults", ok);
        keep = q;
        {   /* FUNA: each DST2 (and one out of range: clamped to SDTN, as a load clamped it) with AMT2 */
            static const int16_t DST[3][NPART] = {{1, 2, 3}, {4, 0, 9}, {-2, 3, 1}};
            static const int16_t AMT[3][NPART] = {{37, -64, 63}, {-5, 22, 11}, {44, 0, -63}};
            uint32_t r;
            for (r = 0; r < 3u; r++) {
                va = keep;
                for (t = 0; t < NPART; t++) {
                    int16_t *w = &va.t[TRK_DRUM].p[P_A2WAVE + 3u * t];
                    w[0] = (int16_t)(7 + t), w[1] = (int16_t)(90 + t), w[2] = DST[r][t];
                    va.t[t].p[P_A2FENV] = AMT[r][t];
                }
                va.magic = PROJ_MAGIC_VA;
                va.sum = proj_sum(&va);
                memset(&proj_va, 0, sizeof proj_va);
                ok = proj_import(&q2, &va, (int)sizeof va) && proj_ok(&q2) && q2.magic == PROJ_MAGIC &&
                     proj_va.from == va.sum && proj_va.to == q2.sum;
                proj_apply(&q2, &rt_dl, 1);
                for (t = 0; t < NPART; t++) {
                    int32_t d = DST[r][t] < 0 ? 0 : DST[r][t] > 4 ? 4 : DST[r][t], k, a = AMT[r][t];
                    static const uint8_t ID[5] = {P_A2FENV, P_A2EPIT, P_A2ESHP, P_A2EOS2, P_A2ESDT};
                    ok &= trk[t].p[P_A2ESUS] == (int16_t)(7 + t) && trk[t].p[P_A2EREL] == (int16_t)(90 + t);
                    for (k = 0; k < 5; k++)
                        ok &= trk[t].p[ID[k]] == (k == d ? a : 0);
                    ok &= proj_va.dst[t] == (d && a ? d : 0);
                    ok &= trk[t].p[P_A2FATK] == (int16_t)(30 + t) && (t != 1u || trk[t].p[P_E7] == 41);
                }
                for (i = 0; i < NSTEP; i++)
                    ok &= !memcmp(&q2.t[0].step[i], &keep.t[0].step[i], sizeof keep.t[0].step[i]);
                bad += check(r == 0u ? "FUNA -> FUNB: DST2 PITCH SHAPE OSC2 with AMT2: that amount, the others 0; SUS2 REL2 kept" :
                             r == 1u ? "FUNA -> FUNB: DST2 SDTN, CUT (FLT keeps it), 9 (clamped: SDTN)" :
                                       "FUNA -> FUNB: DST2 -2 (clamped: CUT), an amount 0 (nothing moves), -63", ok);
            }
            va.t[1].p[3]++;
            bad += check("FUNA with a bad checksum: refused", !proj_import(&q2, &va, (int)sizeof va));
            va.t[1].p[3]--;
            va.sum = proj_sum(&va);
        }
        {   /* the same project as formats 8 and 9 wrote it: 3,840 B, the drum lanes inline (here 0), FNV at the end */
            static uint8_t v8[PROJ_V8_N];
            uint32_t m, sz = PROJ_V8_N, h;
            memcpy(v8, &va, PROJ_V7_N);                /* (FUN9: FUNA's ENV2 words: round 2 above) */
            memset(v8 + PROJ_V7_N, 0, sizeof(dlanes_t));
            m = PROJ_MAGIC_V9;
            memcpy(v8, &m, 4);
            memcpy(v8 + 4, &sz, 4);
            h = proj_hash(v8, PROJ_V8_N - 4u);
            memcpy(v8 + PROJ_V8_N - 4u, &h, 4);
            ok = proj_import(&q2, v8, (int)sizeof v8) && proj_ok(&q2) && q2.magic == PROJ_MAGIC && q2.dl_hash == 0;
            proj_apply(&q2, &rt_dl, 1);
            ok &= trk[0].p[P_A2EPIT] == 0 && trk[0].p[P_A2FENV] == 44 && trk[1].p[P_A2ESDT] == 0 && trk[1].p[P_A2FENV] == 0 &&
                  trk[1].p[P_A2ESHP] == 0 && trk[2].p[P_A2EPIT] == -63 && trk[2].p[P_A2FENV] == 0 && trk[2].p[P_A2ESUS] == 9;
            bad += check("FUN9 -> FUNB: ENV2 converted as FUNA's, its (empty) lanes no record", ok);
            m = PROJ_MAGIC_V8;
            memcpy(v8, &m, 4);
            ((int16_t *)(void *)(v8 + __builtin_offsetof(project_t, t[TRK_DRUM].p)))[P_A2WAVE + 6u] = 64;
            h = proj_hash(v8, PROJ_V8_N - 4u);         /* (format 8: the drum track's own FDEC there) */
            memcpy(v8 + PROJ_V8_N - 4u, &h, 4);
            ok = proj_import(&q2, v8, (int)sizeof v8) && proj_ok(&q2) && q2.magic == PROJ_MAGIC;
            for (t = 0; t < NPART * 3u; t++)
                ok &= q2.t[TRK_DRUM].p[P_A2WAVE + t] == 0;
            ok &= !memcmp(q2.t[1].p, va.t[1].p, sizeof va.t[1].p);
            proj_apply(&q2, &rt_dl, 1);
            for (i = P_A2ESUS; i < P_E0; i++)
                ok &= trk[1].p[i] == 0;
            ok &= trk[1].p[P_A2FATK] == 31 && trk[0].p[P_A2FENV] == 44;
            bad += check("FUN8 -> FUNB: as stored, ENV2's SUS2 REL2 and amounts 0 (the AD envelope it had; FENV kept)", ok);
            v8[PROJ_V8_N - 4u] ^= 1u;
            bad += check("FUN8 with a bad checksum: refused", !proj_import(&q2, v8, (int)sizeof v8));
        }
    }
#endif

#if FELUCCA_ANALOG2
    {   /* user presets of SLOOP plus (bank "UPB1": SUPER 9, DX7 10, SLICE 11) -> today's numbers on load */
        static up_bank_t bk;
        static const int16_t SV[8] = {4, 70, 81, 20, 3, 99, 17, 1};   /* SUPR SDTN MIX DRFT SUB CUT RES FTYP */
        const preset_t *pr = &ENG_ANALOG.presets[A2_SUPER0];
        int16_t v[P_COUNT], def[P_COUNT];
        up_rec_t *r;
        for (i = 0; i < P_COUNT; i++)
            def[i] = TP[i].def;
        memset(&bk, 0, sizeof bk);
        bk.magic = UP_BANK_MAGIC_V1;
        bk.rsize = sizeof(up_rec_t);
        bk.nslot = UP_PER_BANK;
        for (i = 0; i < 5u; i++) {
            r = &bk.r[i];
            r->used = UP_USED;
            r->ver = 1;
            r->np = PROJ_NP_V5;
            r->name[0] = (char)('A' + i);
            for (t = 0; t < PROJ_NP_V5; t++)
                r->p[t] = oldv(i, t);
            r->note[0] = (uint8_t)(60u + i);
        }
        bk.r[0].engine = 9;                              /* SUPER */
        memcpy(&bk.r[0].p[PROJ_NP_V5 - 8u], SV, sizeof SV);
        bk.r[1].engine = 10;                             /* DX7 */
        bk.r[2].engine = 11;                             /* SLICE (FELUCCA_SLICE builds) */
        bk.r[3].engine = 6;                              /* TRIO: as it was */
        bk.r[4].used = 0;                                /* an empty slot stays empty */
        bk.r[4].engine = 9;
        memcpy(&up_bank[0], &bk, sizeof bk);
        up_bank_check(0, (int)sizeof bk);
        r = up_rec(0);
        up_params(r, v, def);
        ok = up_bank[0].magic == UP_BANK_MAGIC && r->engine == 0 && r->np == P_COUNT && up_valid(r) &&
             v[P_A2SWRM] == 4 && v[P_A2SDTN] == 70 && v[P_A2DRFT] == 20 && v[P_E4] == 99 && v[P_E5] == 17 &&
             v[P_A2FTYP] == 1 && v[P_E0] == pr->e[0] && v[P_E2] == pr->e[2] && v[P_E7] == pr->e[7] &&
             v[P_LEVEL] == oldv(0, P_LEVEL) && v[P_FXOFF] == oldv(0, P_FXOFF) && v[P_A2WAVE] == def[P_A2WAVE] &&
             r->note[0] == 60 && r->name[0] == 'A';
        bad += check("UPB1 user preset on SUPER -> ANALOG on the swarm (its values kept, today's P_COUNT)", ok);
        ok = up_rec(1)->engine == ENG_IX_FM6 && up_valid(up_rec(1)) && up_rec(1)->np == PROJ_NP_V5 &&
             up_rec(1)->p[3] == oldv(1, 3) && up_rec(2)->engine == 10u && up_rec(3)->engine == 6u &&
             up_rec(3)->p[PROJ_NP_V5 - 1u] == oldv(3, PROJ_NP_V5 - 1u) && !up_valid(up_rec(4)) &&
             up_rec(4)->engine == 9u;
        bad += check("UPB1 user presets: DX7 -> FM6 (listed again), SLICE 11 -> 10, the rest as stored", ok);
        memcpy(&up_bank[1], &up_bank[0], sizeof bk);
        up_bank_check(1, (int)sizeof bk);                /* a UPB2 bank: read as it is, no second migration */
        ok = !memcmp(&up_bank[1], &up_bank[0], sizeof bk);
        bk.rsize = 100;
        memcpy(&up_bank[1], &bk, sizeof bk);
        up_bank_check(1, (int)sizeof bk);
        bad += check("UPB2 bank read as stored; a UPB1 bank of another shape reads empty", ok && !up_bank[1].magic);
        memset(up_bank, 0, sizeof up_bank);
    }
#if FELUCCA_SL24_SAFE
    {   /* SLOOP 2.4's user presets (UPB1, UP_VER 1, np 61: P_TFLT P_STRUM P_VLEAD at 50..52, P_E0 at 53; FM6 9, SLICE 10,
         * our numbers): engines as stored, 0..49 as stored, 50.. their defaults, E0..E7 from 53 */
        static up_bank_t bk;
        int16_t v[P_COUNT], def[P_COUNT];
        memset(&bk, 0, sizeof bk);
        bk.magic = UP_BANK_MAGIC_V1, bk.rsize = sizeof(up_rec_t), bk.nslot = UP_PER_BANK;
        for (i = 0; i < P_COUNT; i++)
            def[i] = TP[i].def;
        for (i = 0; i < 3u; i++) {
            up_rec_t *r = &bk.r[i];
            uint32_t k;
            r->used = UP_USED, r->ver = 1, r->np = 61, r->name[0] = (char)('P' + i);
            r->engine = (uint8_t)(i == 0 ? 9u : i == 1 ? 10u : 6u);   /* FM6, SLICE, TRIO */
            for (k = 0; k < 61u; k++)
                r->p[k] = (int16_t)(k + 3u * i);
            r->p[50] = -40;                              /* their TFLT: not our P_FXOFF */
        }
        memcpy(&up_bank[0], &bk, sizeof bk);
        up_bank_check(0, (int)sizeof bk);
        ok = up_rec(0)->engine == 9u && up_rec(1)->engine == 10u && up_rec(2)->engine == 6u && up_valid(up_rec(0));
        up_params(up_rec(1), v, def);
        ok &= v[P_LEVEL] == 3 && v[P_CHORD] == 49 + 3 && v[P_FXOFF] == def[P_FXOFF] && v[P_E0] == 53 + 3 &&
              v[P_E7] == 60 + 3;
        bad += check("SLOOP 2.4 user presets (np 61): FM6 stays FM6, SLICE SLICE; TFLT.. not read as FX OFF; E0..E7", ok);
        memset(up_bank, 0, sizeof up_bank);
    }
#endif
    {   /* UP_VER 1 records of np 72 (SUS2 REL2 DST2 a word each): DST2 with AMT2 -> that amount; UP_VER 2: ENV2's
         * extras packed, a round trip of every value */
        static up_rec_t r;
        int16_t v[P_COUNT], w[P_COUNT], def[P_COUNT];
        static const uint8_t ID[5] = {P_A2FENV, P_A2EPIT, P_A2ESHP, P_A2EOS2, P_A2ESDT};
        int32_t d, k;
        for (i = 0; i < P_COUNT; i++)
            def[i] = TP[i].def;
        for (d = 0; d < 5; d++) {
            memset(&r, 0, sizeof r);
            r.used = UP_USED, r.ver = 1, r.np = 72, r.name[0] = 'X';
            for (i = 0; i < 72u; i++)
                r.p[i] = oldv(1, i) % 50;
            r.p[P_A2FENV] = -33;
            r.p[61] = 70, r.p[62] = 5, r.p[63] = (int16_t)d;
            ok = up_valid(&r);
            up_params(&r, v, def);
            ok &= v[P_A2ESUS] == 70 && v[P_A2EREL] == 5 && v[P_LEVEL] == oldv(1, 0) % 50 && v[P_A2SDTN] == oldv(1, P_A2SDTN) % 50;
            for (k = 0; k < 5; k++)
                ok &= v[ID[k]] == (k == d ? -33 : 0);
            for (i = 0; i < 8u; i++)
                ok &= v[P_E0 + i] == oldv(1, 64u + i) % 50;
            bad += check("UP_VER 1, np 72: DST2 d with AMT2 -> amount on d alone, E0..E7 from 64", ok);
        }
        for (i = 0; i < P_COUNT; i++)
            v[i] = (int16_t)((int)(i * 7u % 120u) - 60);
        v[P_A2ESUS] = 127, v[P_A2EREL] = 0, v[P_A2EPIT] = -64, v[P_A2ESHP] = 63, v[P_A2EOS2] = -1, v[P_A2ESDT] = 1;
        memset(&r, 0, sizeof r);
        r.used = UP_USED, r.name[0] = 'Y';
        up_vals_put(&r, v);
        up_params(&r, w, def);
        ok = r.ver == 2u && r.np == P_COUNT && up_valid(&r) && !memcmp(v, w, sizeof v) && UP_NS(P_COUNT) == 72u &&
             sizeof(up_rec_t) == 192u;
        bad += check("UP_VER 2: 75 values in the record's 72 (ENV2's six in three words), read back as written", ok);
        r.np = 80;
        ok = !up_valid(&r);
        r.np = P_COUNT, r.ver = 3;
        bad += check("UP_VER 2 of a size beyond the record, UP_VER 3: not valid", ok && !up_valid(&r));
    }
#endif

    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
