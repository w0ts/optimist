/* SPDX-License-Identifier: GPL-3.0-only */
/* POLY notes' own ends (firmware/src/seq/ngate.h: in RAM a byte a note in step_t, stored as step-only events 0x78 + the
 * note's slot) beside the drum lanes' events (0x7C + lane << 3 | value, the drum track), on a simulated NOR with the
 * section log and the patterns (tests/lane_host.h), every switch of the store on:
 *   ids        the four ids lie past every build's stored track ids and below the lanes': no id is both
 *   capture    proj_capture writes the own ends as events into the buffer's store (synth tracks only); the working lists
 *              never hold one; proj_apply folds them back into the steps and out of the lists; the lanes' events as
 *              they were
 *   old        a project without the events (an older build's, or saved with REC QUANTIZE ON): every own end 0
 *   save/load  a scene saved and loaded; the autosave (MOTN + the extras record) read back
 *   patterns   a track's pattern stored, copied to another synth track and launched there: its own ends follow; the
 *              drum track's pattern never copied to a synth track (nor the other way)
 *   editor     TRACK_STEP bytes: the carry flag (hi bit 2) and the four own ends (v12), a write keeps them; AUTO_GET of a
 *              synth track lists none of them, AUTO_SET refuses their ids; a lane id on the drum track still taken
 *   2.4        the export says the own ends are lost (SX24_NGATE, lost2 bit 5), not with none
 *   full       a list of 127 events: one own end stored, the next one not (it plays the step's GATE after a load)
 * Exit status: the number of failed checks. Run by tests/run_tests.sh. */
#include "lane_host.h"
#include "../firmware/src/io/editor/ed_steps.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("ngate: %-104s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static project_t gp;
static dlrec_t gd;

/* tracks 1 and 2: POLY notes over each other (A 60 over B 64 as seq.c rec_own_end writes it) with own ends; the drum
 * track: a lane lock and a lane hold */
static void setup(void)
{
    uint32_t k;
    seq_stop();
    host_tracks_init();
    for (k = 0; k < NTRK; k++) {
        steps_clear(&trk[k]);
        AUTO_L(k)->n = 0;
        trk[k].p[P_SLEN] = 16;
    }
    auto_w.on = 0;
    for (k = 0; k < 2u; k++) {
        track_t *t = &trk[k];
        t->p[P_VOICE] = V_POLY;
        put_step(t, 2, 1, (const uint8_t[]){60}, ST_NOTE, 0);
        put_step(t, 3, 0, 0, ST_TIE, 0);
        put_step(t, 4, 2, (const uint8_t[]){64, 60}, ST_NOTE, 0);
        put_step(t, 5, 0, 0, ST_TIE, 0);
        put_step(t, 6, 2, (const uint8_t[]){64, 60}, ST_NOTE, 0);
        put_step(t, 7, 1, (const uint8_t[]){64}, ST_NOTE, 0);
        t->step[4].flags = t->step[6].flags = t->step[7].flags = SF_CARRY;
        t->step[6].ngate[1] = (uint8_t)(52u + k);   /* A's own end in step 6 */
        t->step[7].ngate[0] = (uint8_t)(30u + k);   /* B's in step 7 */
    }
    (void)auto_put(AUTO_L(0), 1u | AUTO_ONLY, P_PAN, -20);   /* (a track lock beside them) */
    (void)auto_put(AUTO_L(TRK_DRUM), 3u | AUTO_ONLY, AUTO_LANE(4, DE_DECAY), -40);
    (void)auto_put(AUTO_L(TRK_DRUM), 5u, AUTO_LANE(4, DE_TUNE), 5);
    auto_w.on = 1u << TRK_DRUM;
}
static int ends_are(uint32_t k, uint32_t a, uint32_t b)
{
    return trk[k].step[6].ngate[1] == a && trk[k].step[7].ngate[0] == b && ngate_count(trk[k].step) == (a != 0) + (b != 0);
}
static int lanes_kept(void)
{
    int32_t v, w;
    return auto_get(AUTO_L(TRK_DRUM), 3u | AUTO_ONLY, AUTO_LANE(4, DE_DECAY), &v) && v == -40 &&
           auto_get(AUTO_L(TRK_DRUM), 5u, AUTO_LANE(4, DE_TUNE), &w) && w == 5 && AUTO_L(TRK_DRUM)->n == 2u;
}
static int lists_clean(void)                             /* no working list holds an own end */
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < AUTO_L(k)->n; i++)
            if (AUTO_IS_NGATE(AUTO_L(k)->ev[i].param))
                return 0;
    return 1;
}
static void wipe(void)                                   /* other work: no own end, no event */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        steps_clear(&trk[k]);
        AUTO_L(k)->n = 0;
    }
    auto_w.on = 0;
}

static void t_ids(void)
{
    uint32_t p, both = 0, ng = 0;
    for (p = 0; p < 256u; p++) {
        both += AUTO_IS_NGATE(p) && AUTO_IS_LANE(p);
        ng += AUTO_IS_NGATE(p);
    }
    check("the own ends' ids 0x78..0x7B: four, past every build's stored track ids, below the lanes' (none is both)",
          ng == 4u && !both && AUTO_NGATE0 >= MOT_TAIL_END && AUTO_NGATE0 + 4u == AUTO_LANE0 && !AUTO_PSEUDO(AUTO_NGATE0 + 3u));
}
static void t_capture(void)
{
    const auto_store_t *m;
    int32_t v;
    setup();
    proj_capture(&gp, &gd);
    m = auto_of(&gp);
    check("capture: the buffer's store holds tracks 1 and 2's own ends as events (step 6 slot 1, step 7 slot 0)",
          m && auto_get(&m->l[0], 6u | AUTO_ONLY, AUTO_NGATE0 + 1u, &v) && v == 52 &&
          auto_get(&m->l[0], 7u | AUTO_ONLY, AUTO_NGATE0, &v) && v == 30 &&
          auto_get(&m->l[1], 6u | AUTO_ONLY, AUTO_NGATE0 + 1u, &v) && v == 53 && m->l[2].n == 0u &&
          m->l[TRK_DRUM].n == 2u);
    check("capture: the working lists hold none (the steps hold them)", lists_clean() && ends_are(0, 52, 30) && AUTO_L(0)->n == 1u);
    wipe();
    proj_apply(&gp, &gd, 1);
    check("apply: the own ends back in the steps, out of the lists; the track lock and the lanes' events as they were",
          ends_are(0, 52, 30) && ends_are(1, 53, 31) && !ngate_count(trk[2].step) && lists_clean() && AUTO_L(0)->n == 1u &&
          auto_get(AUTO_L(0), 1u | AUTO_ONLY, P_PAN, &v) && v == -20 && lanes_kept());
    /* an older project: the same steps, a store without the events */
    setup();
    proj_capture(&gp, &gd);
    {
        static step_t scratch[NSTEP];
        auto_store_t *w = auto_for(&gp, 0);
        uint32_t k;
        for (k = 0; k < NTRK; k++)
            (void)ngate_fold(scratch, &w->l[k], k);     /* (the events taken out of the store, put nowhere) */
    }
    wipe();
    proj_apply(&gp, &gd, 1);
    check("an older project (no own-end events): every note the step's GATE (own ends 0), its carry steps as stored",
          !ngate_count(trk[0].step) && !ngate_count(trk[1].step) && (trk[0].step[6].flags & SF_CARRY) && trk[0].step[6].n == 2u &&
          lanes_kept());
    /* a drum list given own-end events (never written so): dropped there, the lanes' kept */
    setup();
    proj_capture(&gp, &gd);
    (void)auto_put(&auto_for(&gp, 0)->l[TRK_DRUM], 3u | AUTO_ONLY, AUTO_NGATE0, 77);
    wipe();
    proj_apply(&gp, &gd, 1);
    check("an own-end event on the drum track's list: dropped at the load, the lanes' events untouched", lists_clean() && lanes_kept());
}
static void t_saveload(void)
{
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    memset(pat_cur, PAT_NONE, NTRK);
    live_sec = -1;
    setup();
    project_save(1);
    wipe();
    project_load(1);
    check("a scene saved and loaded: the own ends, the lanes' events", !strcmp(last_msg, "LOADED") && ends_are(0, 52, 30) &&
          ends_are(1, 53, 31) && lists_clean() && lanes_kept());
    setup();
    proj_capture(&gp, &gd);
    (void)st_save(OBJ_AUTOSAVE, &gp, sizeof gp);
    motion_flash_write(OBJ_AUTOSAVE, &gp);
    (void)sx_log_put(SX_ID_AUTO, gp.sum, &gp, 0);
    wipe();
    memset(auto_for(&gp, 1), 0, sizeof(auto_store_t));
    motion_flash_read(OBJ_AUTOSAVE, &gp);
    sx_log_get(SX_ID_AUTO, gp.sum, &gp);
    proj_apply(&gp, &gd, 1);
    check("the autosave (MOTN + the extras record) read back: the own ends, the lanes' events", ends_are(0, 52, 30) &&
          ends_are(1, 53, 31) && lists_clean() && lanes_kept());
}
static void t_patterns(void)
{
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    memset(pat_cur, PAT_NONE, NTRK);
    live_sec = -1;
    setup();
    check("track 1's pattern stored in slot 2", !pat_store_slot(0, 1));
    check("copied to track 3's slot 4", !pat_copy(0, 1, 2, 3));
    check("the drum track's pattern never copied to a synth track, nor the other way",
          !pat_store_slot(TRK_DRUM, 1) && pat_copy(TRK_DRUM, 1, 2, 5) && pat_copy(0, 1, TRK_DRUM, 5));
    pat_launch(2, 3, PW_NOW);
    check("launched on track 3: track 1's own ends there, the drum track's lanes untouched", ends_are(2, 52, 30) &&
          ends_are(0, 52, 30) && lists_clean() && lanes_kept());
}
static void t_editor(void)
{
    uint8_t a[16];
    uint32_t i, sid = AUTO_NGATE0;
    setup();
    ed_n = 0;
    ed_step_out(&trk[0], 6, 1);
    check("TRACK_STEP bytes (v12): {64 60}, the carry flag on hi bit 2, the own ends 0 52 0 0",
          ed_n == 15u && ed_out[0] == 2u && ed_out[1] == 64u && ed_out[2] == 60u && (ed_out[9] & 4u) && ed_out[11] == 0u &&
          ed_out[12] == 52u && ed_out[13] == 0u);
    ed_n = 0;
    ed_step_out(&trk[0], 6, 0);
    check("... to an editor that did not ask (the STEPS push without WATCH bit 4): 11 bytes", ed_n == 11u);
    memcpy(a, ed_out, 11);
    a[7] = 90;                                         /* (the velocity changed by a v11 editor: no own-end bytes) */
    ed_step_in(&trk[0], 6, a, 11);
    check("a v11 write (11 bytes, the same notes): the own ends and the carry flag kept",
          trk[0].step[6].ngate[1] == 52u && (trk[0].step[6].flags & SF_CARRY) && trk[0].step[6].vel == 90u);
    ed_n = 0;
    ed_step_out(&trk[0], 6, 1);
    memcpy(a, ed_out, 15);
    a[12] = 20;
    ed_step_in(&trk[0], 6, a, 15);
    check("a v12 write: its own ends taken (A 20)", trk[0].step[6].ngate[1] == 20u && trk[0].step[6].ngate[0] == 0u);
    a[0] = 0;
    ed_n = 0;
    (void)ed_stepx(ED_AUTO_GET, a, 1);
    check("AUTO_GET of track 1: its track lock only (no own end)", ed_out[1] == 1u && ed_out[5] == 1u && ed_out[7] == P_PAN);
    a[0] = 0, a[1] = 0, a[2] = 6 | AUTO_ONLY, a[3] = (uint8_t)(sid & 127u), a[4] = (uint8_t)(sid >> 7), a[5] = 40, a[6] = 64;
    ed_n = 0;
    (void)ed_stepx(ED_AUTO_SET, a, 7);
    check("AUTO_SET of an own-end id: refused (rc 1), the list as it was", ed_out[2] == 1u && AUTO_L(0)->n == 1u && lists_clean());
    a[0] = TRK_DRUM, sid = AUTO_LANE(4, DE_CUT), a[3] = (uint8_t)(sid & 127u), a[4] = (uint8_t)(sid >> 7);
    ed_n = 0;
    (void)ed_stepx(ED_AUTO_SET, a, 7);
    i = AUTO_L(TRK_DRUM)->n;
    check("AUTO_SET of a lane id on the drum track: still taken", ed_out[2] == 0u && i == 3u);
}
static uint32_t get78(uint32_t *w2)
{
    uint8_t a[5] = {0, 0, 0, 0, 1};
    ed_n = 0;
    (void)ed_sl24(78, a, 5u);
    *w2 = ed_n >= 17u ? ed_g7(ed_out + ed_n - 2u, 2) : 0xFFFFu;
    return ed_n;
}
static void t_sl24(void)
{
    uint32_t w2;
    setup();
    (void)get78(&w2);
    check("2.4 export: the own ends lost, lost2 bit 5 (SX24_NGATE), the lanes' bit 2 beside it", (w2 & 32u) && (w2 & 4u) &&
          SX24_NGATE == 1u << (14 + 5));
    setup();
    trk[0].step[6].ngate[1] = trk[0].step[7].ngate[0] = trk[1].step[6].ngate[1] = trk[1].step[7].ngate[0] = 0;
    (void)get78(&w2);
    check("... none: bit 5 clear", !(w2 & 32u));
}
static void t_full(void)
{
    auto_list_t l;
    uint32_t i, cut;
    setup();
    l.n = 0;
    for (i = 0; i < AUTO_MAX - 1u; i++)
        (void)auto_put(&l, i % NSTEP | AUTO_ONLY, i / NSTEP ? P_LEVEL : P_PAN, 1);
    cut = ngate_unfold(&l, trk[0].step, 0);
    check("a list of 127 events: one own end stored, the other not (it plays the step's GATE after a load)",
          cut == 1u && l.n == AUTO_MAX);
}

int main(void)
{
    (void)auto_for(&proj_tmp.cur, 1), (void)auto_for(&sec_stage_p, 1);
    auto_init();
    t_ids();
    t_capture();
    t_saveload();
    t_patterns();
    t_editor();
    t_sl24();
    t_full();
    printf("ngate store test %s (%d failed)\n", bad ? "FAILED" : "passed", bad);
    return bad;
}
