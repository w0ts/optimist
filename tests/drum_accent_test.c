/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum track's ACCENT lane (FELUCCA_DRUM_ACCENT; core.h DRUM_ACC, drums.c dacc_vel, seq/ngate.h, the user's ruling
 * of 2026-10-14: "it is a drum track, just no audio output"), on a simulated NOR with the section log and the patterns,
 * every switch of the store on:
 *   lane       lane 16 is a lane for the step helpers (on, level, ratchet, clear), apart from the 16 sounds' bits;
 *              dstep_mask stays the sounds', dstep_lanes has it
 *   velocity   dacc_vel: + the ACCENT amount scaled by the accent step's own level, at most 127; amount 0 or no accent:
 *              as it was
 *   play       an accented step's hits (every lane, their ratchets) sound and go to MIDI OUT at the raised velocity, the
 *              other steps' as before; the lane itself makes no voice and no MIDI note; amount 0: no change; a lock of
 *              the amount on the step; the step's chance 0: nothing at all; ENERGY keeps the accent
 *   input      the lane played live (drum_input): recorded into the step, no voice; erased (EDIT + its key) as a lane
 *   undo       a recorded accent undone and redone with the step
 *   store      capture: the accent steps as events AUTO_ACC in the buffer's store, never in a working list; apply: back
 *              into the steps; a project without them (older): no accent, the amount its default (stored 0); a scene
 *              saved and loaded; the autosave read back; a pattern stored and launched; the autosave hash sees an accent
 *   editor     DRUM_STEP: bit 16 of on, the fifth groups' bits 4..5; a write with them sets the lane, one without clears it
 *   2.4        the export says the accents are lost (SX24_DACC, lost2 bit 12), not without any; the ACCENT amount's slot
 *              comes back from a 2.4 round trip as it went
 * Exit status: the number of failed checks. Run by tests/run_tests.sh. */
#include "lane_host.h"
#include "../firmware/src/io/editor/ed_steps.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("accent: %-104s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static project_t gp;
static dlrec_t gd;

#define SLEN (BEAT_U / 4u)                             /* a 16th at 120 BPM, in clock units */
static void clear_lists(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        AUTO_L(k)->n = 0;
    auto_w.on = 0;
    auto_touch_all();
}
static void reset(void)
{
    uint32_t i;
    seq_stop();
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        steps_clear(&trk[i]);
        trk[i].seq_abs = SEQ_NONE;
        trk[i].seq_idx = 0;
        trk[i].p[P_SLEN] = 8;
    }
    TDRUM->p[P_DACC] = DACC_DEF;
    clear_lists();
    memset(&drums, 0, sizeof drums);
    memset(&dl, 0, sizeof dl);
    drums_set = -2;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    song.rec = 0;
    rec_wait = 0;
    fill_held = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    clk_beat = clk_pos = 0;
    song.playing = 0;
    usb.config = 1;
    bp_set[BPS_MOUT] = 1;
    mo_w = mo_r = 0;
}

/* what one pass of 8 steps played: per step and lane, the hits' voice velocities (the last one) and their count, the
 * MIDI note-ons' velocities (the last one) and their count */
static uint8_t hv[8][DRUM_LANES], hn[8][DRUM_LANES], mv[8][DRUM_LANES], mn[8][DRUM_LANES];
static uint32_t voices_other;                          /* voices of a note no lane plays (the ACCENT lane's: none) */
static void heard_clear(void)
{
    memset(hv, 0, sizeof hv), memset(hn, 0, sizeof hn), memset(mv, 0, sizeof mv), memset(mn, 0, sizeof mn);
    voices_other = 0;
}
static uint32_t step_at(uint32_t at) { return (at + SLEN / 8u) / SLEN % 8u; }
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k, l, at = clk_beat * BEAT_U + clk_pos;
    mix_block(out, CTL);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a) {
                for (l = 0; l < DRUM_LANES && LANE_NOTE_OF(l) != drums.v[k].note; l++)
                    ;
                if (l == DRUM_LANES) {
                    voices_other++;
                    continue;
                }
                hv[step_at(at)][l] = drums.v[k].vel;
                hn[step_at(at)][l]++;
            }
    while (mo_r != mo_w) {
        uint32_t p = midi_out_q[mo_r % MQ], st = (p >> 8) & 0xF0u, n = (p >> 16) & 0x7Fu, v = p >> 24;
        mo_r++;
        if (st != 0x90u || !v)
            continue;
        for (l = 0; l < DRUM_LANES && LANE_NOTE_OF(l) != n; l++)
            ;
        if (l < DRUM_LANES)
            mv[step_at(at)][l] = (uint8_t)v, mn[step_at(at)][l]++;
    }
}
static void play_pass(void)                            /* PLAY, two passes; the second one heard */
{
    transport_req = 1;
    while (clk_beat < 2u)
        run_block();
    heard_clear();
    while (clk_beat < 4u)
        run_block();
    seq_stop();
    clk_beat = clk_pos = 0;
}

static void t_lane(void)
{
    dstep_t s;
    memset(&s, 0, sizeof s);
    dstep_set(&s, 0, LV_SOFT, 1);
    dstep_set(&s, DRUM_ACC, LV_HARD, 2);
    check("lane 16: on, its level and ratchet; lane 0 as it was set; the sounds' mask without it, the lanes' with it",
          dstep_has(&s, DRUM_ACC) && dstep_lvl(&s, DRUM_ACC) == LV_HARD && dstep_rat(&s, DRUM_ACC) == 2u &&
          dstep_lvl(&s, 0) == LV_SOFT && dstep_rat(&s, 0) == 1u && dstep_mask(&s) == 1u &&
          dstep_lanes(&s) == (1u | 1u << DRUM_ACC));
    dstep_clr(&s, DRUM_ACC);
    check("lane 16 cleared: lane 0 kept", !dstep_has(&s, DRUM_ACC) && !s.acc && dstep_has(&s, 0) && dstep_lanes(&s) == 1u);
    dstep_set(&s, DRUM_ACC, LV_NORM, 0);
    dstep_clr(&s, 0);
    check("lane 0 cleared: lane 16 kept", dstep_has(&s, DRUM_ACC) && !dstep_mask(&s) && dstep_lanes(&s) == 1u << DRUM_ACC);
    check("a stored drum step: 10 bytes (the accent not in it), a RAM one 11", sizeof(dstep_sn_t) == 10u && sizeof(dstep_t) == 11u &&
          NSTEP * sizeof(dstep_t) <= sizeof trk[0].step);
}
static void t_velocity(void)
{
    const uint8_t on = 1u, ghost = 1u | LV_GHOST << 1, soft = 1u | LV_SOFT << 1, hard = 1u | LV_HARD << 1;
    check("NORM 100 + ACCENT 27 (default): 127; a GHOST hit 42 + 27: 69; a HARD hit stays 127",
          dacc_vel(100, on, DACC_DEF) == 127u && dacc_vel(42, on, DACC_DEF) == 69u && dacc_vel(127, on, DACC_DEF) == 127u);
    check("the accent step's own level scales it: GHOST x0.42, SOFT x0.72, HARD x1.27 (Q7: 54 92 163 / 128)",
          dacc_vel(42, ghost, 40) == 42u + (40u * 54u >> 7) && dacc_vel(42, soft, 40) == 42u + (40u * 92u >> 7) &&
          dacc_vel(42, hard, 40) == 42u + (40u * 163u >> 7));
    check("no accent, amount 0 (or below): the velocity as it was; above the range: as DACC_MAX; at most 127",
          dacc_vel(72, 0, 60) == 72u && dacc_vel(72, on, 0) == 72u && dacc_vel(72, on, -5) == 72u &&
          dacc_vel(1, on, 500) == 127u && dacc_vel(100, on, 127) == 127u);
}
/* kick on steps 1 and 5, snare (SOFT, ratchet x2) on step 5, the hat on step 3, the accent on step 5 */
static void setup_play(void)
{
    reset();
    dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[4], 0, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[4], 2, LV_SOFT, 1);
    dstep_set(&TDRUM->dstep[2], 4, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[4], DRUM_ACC, LV_NORM, 0);
}
static void t_play(void)
{
    setup_play();
    play_pass();
    printf("accent: step 1 kick %u, step 5 kick %u snare %u (%u hits, MIDI %u x%u), step 3 hat %u\n", hv[0][0], hv[4][0],
           hv[4][2], hn[4][2], mv[4][2], mn[4][2], hv[2][4]);
#if FELUCCA_DRUM_ACCENT
    check("the accented step: the kick 100 -> 127, the SOFT snare 72 -> 99, both its ratchet hits too",
          hv[4][0] == 127u && hn[4][0] == 1u && hv[4][2] == 99u && hn[4][2] == 2u);
    check("... MIDI OUT the same velocities (each hit)", mv[4][0] == 127u && mv[4][2] == 99u && mn[4][2] == 2u);
#else
    check("DRUM_ACCENT off: the accented step's hits as they are (100, SOFT 72)", hv[4][0] == 100u && hv[4][2] == 72u);
#endif
    check("the other steps as before: the kick on step 1 at 100, the hat on step 3 at 100 (voice and MIDI)",
          hv[0][0] == 100u && hv[2][4] == 100u && mv[0][0] == 100u && mv[2][4] == 100u && hn[0][0] == 1u);
    check("the ACCENT lane makes no voice of its own", voices_other == 0u);
    TDRUM->p[P_DACC] = 0;
    play_pass();
    check("ACCENT amount 0: the accented step plays as an unaccented one", hv[4][0] == 100u && hv[4][2] == 72u && mv[4][0] == 100u);
    TDRUM->p[P_DACC] = DACC_DEF;
    (void)auto_put(AUTO_L(TRK_DRUM), 4u | AUTO_ONLY, P_DACC, 10);   /* (a lock of the amount on step 5) */
    auto_touch(TRK_DRUM);
    play_pass();
#if FELUCCA_DRUM_ACCENT
    check("a lock of the ACCENT amount (10) on the accented step: the kick 110, the snare 82; the amount back after it",
          hv[4][0] == 110u && hv[4][2] == 82u && TDRUM->p[P_DACC] == DACC_DEF);
#endif
    clear_lists();
    (void)auto_pseudo_set(AUTO_L(TRK_DRUM), 4, AUTO_CHANCE, 0);   /* (the step's chance: never) */
    auto_touch(TRK_DRUM);
    play_pass();
    check("the step's chance 0: no hit of it at all (the accent follows its step)", !hn[4][0] && !hn[4][2] && hn[0][0] == 1u);
    clear_lists();
    dstep_clr(&TDRUM->dstep[4], 0);
    dstep_clr(&TDRUM->dstep[4], 2);
    play_pass();
    check("an accent on a step without hits: nothing sounds there, the others as before", !hn[4][0] && !hn[4][2] &&
          hv[0][0] == 100u && voices_other == 0u);
#if FELUCCA_ENERGY
    {
        dstep_t s;
        memset(&s, 0, sizeof s);
        dstep_set(&s, 0, LV_NORM, 0);
        dstep_set(&s, DRUM_ACC, LV_SOFT, 0);
        TDRUM->p[MAC_ID[3]] = 63;
        check("ENERGY's band: the step as it plays keeps its accent", en_step(TDRUM, &s, 1)->acc == s.acc);
        TDRUM->p[MAC_ID[3]] = 0;
    }
#endif
}
/* ACCENT amount as motion (a hold event: recorded knob moves) and the ACCENT lane's sequencer mute (seq_mute.c) */
static void t_motion_mute(void)
{
    uint8_t o[4];
    setup_play();
    check("the ACCENT amount is a value motion records (the drum track's P_E1; its kit P_E0 too since one list with the locks)",
          motion_param(TDRUM, P_DACC) && motion_param(TDRUM, P_E0) && lock_ok(TDRUM, P_E0));
    (void)auto_put(AUTO_L(TRK_DRUM), 3u, P_DACC, 10);    /* (a hold from step 4: the amount 10 until the pass's end) */
    auto_w.on = 1u << TRK_DRUM;
    auto_touch(TRK_DRUM);
    play_pass();
#if FELUCCA_DRUM_ACCENT
    check("a motion of the ACCENT amount (10 from step 4): the accented step 5's kick 110, its SOFT snare 82",
          hv[4][0] == 110u && hv[4][2] == 82u && hv[0][0] == 100u);
#endif
    clear_lists();
    TDRUM->p[P_DACC] = DACC_DEF;
    smute_lane = 0;
    smute_set_lane(DRUM_ACC, 1);
    play_pass();
    check("the ACCENT lane's sequencer muted: the accented step plays unaccented (100, SOFT 72), every hit still there",
          smute_lane_on(DRUM_ACC) && !smute_lane_on(0) && smute_lane == 1u << DRUM_ACC && hv[4][0] == 100u &&
          hv[4][2] == 72u && hn[4][2] == 2u && hn[0][0] == 1u);
    check("the mute's TLV with it: 4 bytes, the 4th bit 0; read back", smute_tlv(o) == 4u && o[3] == 1u && !o[1] && !o[2] &&
          (smute_lane = 0, smute_untlv(o, 4), smute_lane == 1u << DRUM_ACC));
    smute_lane = 1u << 2;
    check("without it: the 3 bytes of before (an older build reads them as ever)", smute_tlv(o) == 3u && o[1] == 4u);
    o[3] = 1;
    smute_untlv(o, 3);
    check("a 3-byte TLV (before): the ACCENT lane plays", !smute_lane_on(DRUM_ACC) && smute_lane_on(2));
    smute_lane = 0;
    check("smute_toggle(SMUTE_ACC): the ACCENT lane on, off; lane 0 and the tracks untouched",
          smute_toggle(SMUTE_ACC) == 1 && smute_lane == 1u << DRUM_ACC && smute_toggle(SMUTE_ACC) == 0 && !smute_lane && !smute_trk);
    play_pass();
#if FELUCCA_DRUM_ACCENT
    check("unmuted: accented again", hv[4][0] == 127u);
#endif
}
static void t_input(void)
{
    uint32_t a;
    reset();
    TDRUM->p[P_SLEN] = 8;
    song.sel = TRK_DRUM;
    song.rec = 1u << TRK_DRUM;
    transport_req = 1;
    while (clk_beat < 1u)
        run_block();
    a = drums.age;
    drum_input(DRUM_ACC, LV_NORM, 0, 1);
    check("the ACCENT lane played while recording: on in a step, no voice started", drums.age == a &&
          (dstep_lanes(&TDRUM->dstep[0]) | dstep_lanes(&TDRUM->dstep[1]) | dstep_lanes(&TDRUM->dstep[2]) |
           dstep_lanes(&TDRUM->dstep[3]) | dstep_lanes(&TDRUM->dstep[4]) | dstep_lanes(&TDRUM->dstep[5]) |
           dstep_lanes(&TDRUM->dstep[6]) | dstep_lanes(&TDRUM->dstep[7])) == 1u << DRUM_ACC);
    seq_stop();
    song.rec = 0;
    {
        uint32_t i, any = 0;
        for (i = 0; i < 8u; i++)
            dstep_set(&TDRUM->dstep[i], DRUM_ACC, LV_NORM, 0), dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
        er_trk = TRK_DRUM;
        er_lanes = 1u << DRUM_ACC;
        for (i = 0; i < 8u; i++)
            erase_step(TDRUM, i);
        er_lanes = 0;
        for (i = 0; i < 8u; i++)
            any |= dstep_has(&TDRUM->dstep[i], DRUM_ACC) | (dstep_has(&TDRUM->dstep[i], 0) << 1);
        check("erase (EDIT + the lane's key): the ACCENT lane goes, the kick stays", any == 2u);
    }
}
static void t_undo(void)
{
    reset();
    undo_clear();
    undo_mark(TDRUM, (undo_sess += 4u) | 3u);
    dstep_set(&TDRUM->dstep[3], DRUM_ACC, LV_HARD, 0);
    undo_close();
    check("undo: the accent set on step 4 is gone", undo_apply(0) == 1 && !dstep_has(&TDRUM->dstep[3], DRUM_ACC));
    (void)undo_apply(1);
    check("redo: back, with its level", dstep_has(&TDRUM->dstep[3], DRUM_ACC) && dstep_lvl(&TDRUM->dstep[3], DRUM_ACC) == LV_HARD);
}

/* accents on steps 2 (NORM) and 7 (GHOST, ratchet x3), a kick on 2, the amount 40, a track lock beside them */
static void setup_store(void)
{
    reset();
    dstep_set(&TDRUM->dstep[1], 0, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[1], DRUM_ACC, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[6], DRUM_ACC, LV_GHOST, 2);
    TDRUM->p[P_DACC] = 40;
    (void)auto_put(AUTO_L(TRK_DRUM), 3u | AUTO_ONLY, AUTO_LANE(4, DE_DECAY), -40);
}
static int store_back(void)
{
    int32_t v;
    uint32_t i, n = 0;
    for (i = 0; i < NSTEP; i++)
        n += dstep_has(&TDRUM->dstep[i], DRUM_ACC);
    return n == 2u && TDRUM->dstep[1].acc == 1u && dstep_lvl(&TDRUM->dstep[6], DRUM_ACC) == LV_GHOST &&
           dstep_rat(&TDRUM->dstep[6], DRUM_ACC) == 2u && dstep_has(&TDRUM->dstep[1], 0) && TDRUM->p[P_DACC] == 40 &&
           AUTO_L(TRK_DRUM)->n == 1u && auto_get(AUTO_L(TRK_DRUM), 3u | AUTO_ONLY, AUTO_LANE(4, DE_DECAY), &v) && v == -40;
}
static int lists_clean(void)
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < AUTO_L(k)->n; i++)
            if (AUTO_L(k)->ev[i].param == AUTO_ACC)
                return 0;
    return 1;
}
static void wipe(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        steps_clear(&trk[k]);
    TDRUM->p[P_DACC] = 99;
    clear_lists();
}
static void t_store(void)
{
    const auto_store_t *m;
    int32_t v;
    uint32_t h;
    check("the ACCENT id 0x77: past every build's stored track ids, below the note ends, not a pseudo-parameter",
          AUTO_ACC >= MOT_TAIL_END && AUTO_ACC + 1u == AUTO_NGATE0 && !AUTO_PSEUDO(AUTO_ACC) && !AUTO_IS_LANE(AUTO_ACC));
    setup_store();
    h = auto_hash();
    dstep_set(&TDRUM->dstep[9], DRUM_ACC, LV_NORM, 0);
    check("the autosave hash sees an accent", auto_hash() != h);
    dstep_clr(&TDRUM->dstep[9], DRUM_ACC);
    proj_capture(&gp, &gd);
    m = auto_of(&gp);
    check("capture: the buffer's store holds the accents as events (step 2: 1, step 7: GHOST x3 = 1 | 1 << 1 | 2 << 3)",
          m && auto_get(&m->l[TRK_DRUM], 1u | AUTO_ONLY, AUTO_ACC, &v) && v == 1 &&
          auto_get(&m->l[TRK_DRUM], 6u | AUTO_ONLY, AUTO_ACC, &v) && v == (1 | LV_GHOST << 1 | 2 << 3) &&
          m->l[TRK_DRUM].n == 3u);
    check("capture: the ACCENT amount stored from its default (40 - 27), the working lists without the accents",
          gp.t[TRK_DRUM].p[PJ_E0 + (P_DACC - P_E0)] == 40 - DACC_DEF && lists_clean() && AUTO_L(TRK_DRUM)->n == 1u);
    wipe();
    proj_apply(&gp, &gd, 1);
    check("apply: the accents back in the steps (level, ratchet), out of the lists; the amount; the lane lock as it was",
          store_back() && lists_clean());
    /* an older project: no events, its drum P_E1 0 */
    setup_store();
    proj_capture(&gp, &gd);
    {
        static step_t scratch[NSTEP];
        (void)ngate_fold(scratch, &auto_for(&gp, 0)->l[TRK_DRUM], 0);   /* (the events taken out, put nowhere) */
        gp.t[TRK_DRUM].p[PJ_E0 + (P_DACC - P_E0)] = 0;
        gp.sum = proj_sum(&gp);
        auto_for(&gp, 0)->psum = gp.sum;
    }
    wipe();
    proj_apply(&gp, &gd, 1);
    {
        uint32_t i, n = 0;
        for (i = 0; i < NSTEP; i++)
            n += dstep_has(&TDRUM->dstep[i], DRUM_ACC);
        check("an older project (no accent events, drum P_E1 0): no accent anywhere, the amount its default 27, the kick kept",
              !n && TDRUM->p[P_DACC] == DACC_DEF && dstep_has(&TDRUM->dstep[1], 0));
    }
    /* a part's list given an accent event (never written so): dropped */
    setup_store();
    proj_capture(&gp, &gd);
    (void)auto_put(&auto_for(&gp, 0)->l[0], 2u | AUTO_ONLY, AUTO_ACC, 1);
    wipe();
    proj_apply(&gp, &gd, 1);
    check("an accent event on a part's list: dropped at the load, the drum track's as they were", lists_clean() && store_back());
}
static void t_saveload(void)
{
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    memset(pat_cur, PAT_NONE, NTRK);
    live_sec = -1;
    setup_store();
    project_save(1);
    wipe();
    project_load(1);
    check("a scene saved and loaded: the accents, the amount", !strcmp(last_msg, "LOADED") && store_back() && lists_clean());
    setup_store();
    proj_capture(&gp, &gd);
    (void)st_save(OBJ_AUTOSAVE, &gp, sizeof gp);
    motion_flash_write(OBJ_AUTOSAVE, &gp);
    (void)sx_log_put(SX_ID_AUTO, gp.sum, &gp, 0);
    wipe();
    memset(auto_for(&gp, 1), 0, sizeof(auto_store_t));
    motion_flash_read(OBJ_AUTOSAVE, &gp);
    sx_log_get(SX_ID_AUTO, gp.sum, &gp);
    proj_apply(&gp, &gd, 1);
    check("the autosave (MOTN + the extras record) read back: the accents, the amount", store_back() && lists_clean());
}
static void t_patterns(void)
{
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    memset(pat_cur, PAT_NONE, NTRK);
    live_sec = -1;
    setup_store();
    check("the drum track's pattern stored in slot 2", !pat_store_slot(TRK_DRUM, 1));
    steps_clear(TDRUM);
    pat_launch(TRK_DRUM, 1, PW_NOW);
    {
        uint32_t i, n = 0;
        for (i = 0; i < NSTEP; i++)
            n += dstep_has(&TDRUM->dstep[i], DRUM_ACC);
        check("launched back: its accents (level, ratchet) in the steps, none in the list", n == 2u &&
              dstep_rat(&TDRUM->dstep[6], DRUM_ACC) == 2u && dstep_has(&TDRUM->dstep[1], DRUM_ACC) && lists_clean());
    }
}
static void t_editor(void)
{
    uint8_t a[14];
    setup_store();
    ed_n = 0;
    ed_dstep_out(&TDRUM->dstep[6]);
    check("DRUM_STEP bytes: the ACCENT lane on bit 16 (third byte bit 2), its ratchet x3 on the rat's fifth group bits 4..5",
          ed_n == 13u && (ed_out[2] & 4u) && ((ed_out[12] >> 4) & 3u) == 2u && ((ed_out[7] >> 4) & 3u) == LV_GHOST);
    memset(a, 0, sizeof a);
    a[0] = 9, a[1] = 1, a[3] = 4u, a[8] = (uint8_t)(LV_SOFT << 4), a[13] = 1u << 4;
    ed_n = 0;
    ed_dstep_in(&TDRUM->dstep[9], a);
    check("a DRUM_STEP write with the bits: the kick and the ACCENT lane (SOFT, x2) on step 10",
          dstep_has(&TDRUM->dstep[9], 0) && dstep_lvl(&TDRUM->dstep[9], DRUM_ACC) == LV_SOFT &&
          dstep_rat(&TDRUM->dstep[9], DRUM_ACC) == 1u && dstep_has(&TDRUM->dstep[9], DRUM_ACC));
    a[3] = 0, a[8] = 0, a[13] = 0;
    ed_dstep_in(&TDRUM->dstep[9], a);
    check("... one without them (an older editor): the accent off, the kick on", dstep_has(&TDRUM->dstep[9], 0) &&
          !dstep_has(&TDRUM->dstep[9], DRUM_ACC));
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
    setup_store();
    clear_lists();
    (void)get78(&w2);
    check("2.4 export: the accents lost, lost2 bit 12 (SX24_DACC)", (w2 & (1u << 12)) && SX24_DACC == 1u << (14 + 12));
    reset();
    dstep_set(&TDRUM->dstep[1], 0, LV_NORM, 0);
    (void)get78(&w2);
    check("... none: bit 12 clear", !(w2 & (1u << 12)));
}

int main(void)
{
    (void)auto_for(&proj_tmp.cur, 1), (void)auto_for(&sec_stage_p, 1);
    auto_init();
    t_lane();
    t_velocity();
    t_play();
    t_motion_mute();
    t_input();
    t_undo();
    t_store();
    t_saveload();
    t_patterns();
    t_editor();
    t_sl24();
    printf("accent test %s (%d failed)\n", bad ? "FAILED" : "passed", bad);
    return bad;
}
