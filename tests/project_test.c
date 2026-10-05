/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project formats (firmware/src/project.c, -DPROJ_HOST part). Format 6 ("FUN6": format 5
 * and the FM6 parts' voices, their operator switches and DX7 functions) is written; format 5 ("FUN5":
 * 10-byte steps with levels and ratchets, the drum track's 16 lanes, P_CHORD, P_FXOFF) is written;
 * format 4 ("FUN4", SLOOP 2.0 .. 2.2, before P_FXOFF), format 3 ("FUN3", SLOOP 1.x), format 2 ("FUN2", 53 parameters per track) and format 1 ("FUN1"), built
 * byte for byte as the firmware stored them, convert: every old value at its parameter, the parameters
 * added since at their defaults, the swings onto the MPC scale (x 0.8), synth steps as they were, the
 * drum track's notes onto its lanes (accent: hard), globals, selection, the engine bytes (kept; the
 * drum track's 0); damaged ones are refused. Run by tests/run_tests.sh (needs build/gen). */
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
        ok &= n->p[P_E0 + k] == oldv(t, 45u + k);
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

    bad += check("layout: CHORD, FXOFF just before P_E0 (51), P_COUNT = format 4's + 1",
                 P_CHORD + 1 == P_FXOFF && P_FXOFF + 1 == P_E0 && P_E0 == 51 && P_COUNT == PROJ_NP_V4 + 1u &&
                 PROJ_NP_V4 == PROJ_NP_V3 + 1u && P_SLDEPTH + 1 == P_CHORD);
    bad += check("format 5 fits one flash object; 4 slots fit .noinit", sizeof(project_t) <= 4096u - 256u &&
                 4u * sizeof(project_t) < 0x3D50u - 1024u);

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
    bad += check("FUN3 -> FUN5: converted, valid format 5 slot", ok && proj_ok(&q) && q.magic == PROJ_MAGIC);
    ok = q.sel == 3 && q.g[G_SWING] == 40;
    for (i = 0; i < PROJ_NG_V3; i++)
        ok &= i == G_SWING || q.g[i] == (int16_t)(300 + i);
    for (i = PROJ_NG_V3; i < G_COUNT; i++)
        ok &= q.g[i] == GP[i].def;
    bad += check("FUN3 -> FUN5: globals (swing 50 -> 40: the MPC scale), the new ones default", ok);
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
            ok &= n->p[P_E0 + k] == oldv(t, PROJ_NP_V3 - 8u + k);
    }
    bad += check("FUN3 -> FUN5: parameters (P_E0.. moved), steps, drum notes -> lanes", ok);

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
    bad += check("FUN2 -> FUN5: converted, valid format 5 slot", ok && proj_ok(&q) && q.magic == PROJ_MAGIC);
    ok = q.sel == 2;
    for (i = 0; i < PROJ_NG_V2; i++)
        ok &= i == G_SWING || q.g[i] == (int16_t)(500 + i);
    bad += check("FUN2 -> FUN5: globals and selected track", ok);
    ok = 1;
    for (t = 0; t < NTRK; t++)
        ok &= track_ok_v2(&q.t[t], &v2.t[t], t);
    bad += check("FUN2 -> FUN5: every parameter mapped, SLICER OFF, CHORD OFF (4 tracks)", ok);
    bad += check("FUN2 -> FUN5: engine bytes kept (WHEEL 7, ANALOG 0, TRIO 6), drum 0",
                 q.t[0].engine == 7 && q.t[1].engine == 0 && q.t[2].engine == 6 && q.t[3].engine == 0 &&
                 str_eq(ENGINES[7]->name, "WHEEL") && str_eq(ENGINES[6]->name, "TRIO") && NENGINES > 8);

    /* format 4 (SLOOP 2.0 .. 2.2): every value at its id, P_FXOFF its default (the effects on), E0.. moved */
    {
        static project_v4_t v4;
        memset(&v4, 0, sizeof v4);
        v4.magic = PROJ_MAGIC_V4;
        v4.size = sizeof v4;
        for (i = 0; i < G_COUNT; i++)
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
            for (i = 0; i < P_FXOFF; i++)
                ok &= q.t[t].p[i] == oldv(t, i);
            ok &= q.t[t].p[P_FXOFF] == 0;
            for (i = 0; i < 8u; i++)
                ok &= q.t[t].p[P_E0 + i] == oldv(t, PROJ_NP_V4 - 8u + i);
            ok &= q.t[t].engine == v4.t[t].engine && q.t[t].preset == v4.t[t].preset &&
                  !memcmp(&q.t[t].step[4], &v4.t[t].step[4], sizeof q.t[t].step[4]);
        }
        bad += check("FUN4 -> FUN5: values at their ids, FX on, E0..E7 moved, steps", ok);
        v4.t[3].p[7]++;
        memcpy(&buf, &v4, sizeof v4);
        bad += check("FUN4 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v4));
        memcpy(&q2, &q, sizeof q);
    }

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
    bad += check("FUN5 -> FUN5: as stored (levels, ratchets, lanes, engine 8)",
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
              q.t[t].p[P_E0] == ENGINES[trk_def_engine(t)]->edit[0].def &&
              (t == TRK_DRUM ? dstep_mask(&q.t[t].dstep[0]) == 0u : q.t[t].step[0].time == ST_REST);
    bad += check("FUN1 -> FUN5: track 1 mapped, tracks 2..4 defaults", ok);

    /* capture / apply: the working project round trip */
    host_tracks_init();
    for (t = 0; t < NTRK; t++)
        trk[t].p[P_SLEN] = (int16_t)(5 + t);
    trk[1].step[2].n = 2, trk[1].step[2].note[0] = 60, trk[1].step[2].note[1] = 64, trk[1].step[2].time = ST_NOTE;
    trk[1].step[2].lvl = 0x0D;
    dstep_set(&TDRUM->dstep[9], 4, LV_SOFT, 1);
    song.g[G_DUST] = 33;
    trk[3].p[P_FXOFF] = 1;
    proj_capture(&q);
    host_tracks_init();
    proj_apply(&q, 1);
    ok = trk[2].p[P_SLEN] == 7 && trk[1].step[2].n == 2 && trk[1].step[2].lvl == 0x0D && song.g[G_DUST] == 33 &&
         dstep_has(&TDRUM->dstep[9], 4) && dstep_lvl(&TDRUM->dstep[9], 4) == LV_SOFT && dstep_rat(&TDRUM->dstep[9], 4) == 1u &&
         trk[3].p[P_FXOFF] == 1 && trk[0].p[P_FXOFF] == 0;
    bad += check("the working project: capture -> apply round trip (levels, lanes, DUST, FX off)", ok);

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
        proj_capture(&q);
        ok = q.fm6_has == 1u && q.fm6_on[0] == 0x3Du && q.fm6_fn[0][FN_PTIME - FN_PBUP] == 77;
        bad += check("FM6 part captured: its voice (packed), the switches, the functions; other parts none", ok);
        fm6_from_rom(ed, &FM6_INIT);
        fm6_fn_reset(ed);
        fm6_cur[0] = 0;
        proj_apply(&q, 1);
        ok = !memcmp(ed, keep, sizeof keep) && fm6_cur[0] == trk[0].p[P_E0] + 1 && fm6_fnok[0];
        bad += check("FM6 part applied: the voice with its edits, switches, functions (VOICE not reloaded)", ok);
        trk[0].eng_req = 0;                              /* not FM6 when captured: nothing kept, VOICE afresh */
        proj_capture(&q);
        ok = !(q.fm6_has & 1u);
        trk[0].eng_req = (uint8_t)ENG_IX_FM6;
        q.t[0].engine = (uint8_t)ENG_IX_FM6;
        proj_apply(&q, 1);
        bad += check("FM6 part from a project without its voice: VOICE loads afresh", ok && fm6_cur[0] == 0);
    }

    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
