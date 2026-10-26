/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' FX (FELUCCA_LANE_FX; seq/lane_fx.c, auto.h AUTO_IS_LFX, auto.c lfx_get / lfx_set), on the lane harness
 * (every switch of the automation store on):
 *   events    lfx_set writes a lane's FX as a hold event of a pseudo-parameter, its default removes it; the drum track
 *             only; the step tools leave it (a clear of motion and locks, a step's events dropped, the marks)
 *   ROLL      x2 .. x8: every hit of the lane that many times in its step (voice and MIDI), the other lanes as before
 *   CHANCE    0 %: the lane silent (its rolls too), the others play; 50 %: about half of 64 hits
 *   FILL      FILL ONLY: silent until a fill is held, then heard; NO FILL: the other way round
 *   NUDGE     late: the lane's hit later in its step by the nudge; early: in the step before, by the nudge, once (not
 *             again on its own step); the other lanes on the grid
 * Exit status: the number of failed checks. Run by tests/run_tests.sh. */
#define FELUCCA_LANE_FX 1
#define FELUCCA_COND 1
#include "lane_host.h"

static int bad;
static void check(const char *what, int ok)
{
    printf("lane fx: %-104s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define SLEN (BEAT_U / 4u)                             /* a 16th at 120 BPM, in clock units */
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
        AUTO_L(i)->n = 0;
    }
    auto_w.on = 0;
    auto_touch_all();
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

/* what one pass of 8 steps played: per step and lane the hits (voice), the MIDI note-ons, and where in the pass (clock
 * units) the step's first hit came */
static uint32_t hn[8][DRUM_LANES], mn[8][DRUM_LANES], at0[8][DRUM_LANES];
static uint8_t v0[8][DRUM_LANES], vlast[8][DRUM_LANES], vmin[8][DRUM_LANES], vmax[8][DRUM_LANES];   /* the hits' velocities */
static void heard_clear(void)
{
    memset(hn, 0, sizeof hn), memset(mn, 0, sizeof mn), memset(at0, 0xFF, sizeof at0);
    memset(v0, 0, sizeof v0), memset(vlast, 0, sizeof vlast), memset(vmin, 0xFF, sizeof vmin), memset(vmax, 0, sizeof vmax);
}
static uint32_t early = SLEN / 16u;                    /* a hit this much before a step counts as its (the nudge tests: more) */
static uint32_t step_at(uint32_t at) { return (at + early) / SLEN % 8u; }
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k, l, at = (clk_beat % 2u) * BEAT_U + clk_pos;
    mix_block(out, CTL);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a) {
                for (l = 0; l < DRUM_LANES && LANE_NOTE_OF(l) != drums.v[k].note; l++)
                    ;
                if (l == DRUM_LANES)
                    continue;
                {
                    uint32_t st = step_at(at);
                    uint8_t v = drums.v[k].vel;
                    if (!hn[st][l]++)
                        at0[st][l] = at, v0[st][l] = v;
                    vlast[st][l] = v;
                    vmin[st][l] = v < vmin[st][l] ? v : vmin[st][l];
                    vmax[st][l] = v > vmax[st][l] ? v : vmax[st][l];
                }
            }
    while (mo_r != mo_w) {
        uint32_t p = midi_out_q[mo_r % MQ], st = (p >> 8) & 0xF0u, n = (p >> 16) & 0x7Fu, v = p >> 24;
        mo_r++;
        if (st != 0x90u || !v)
            continue;
        for (l = 0; l < DRUM_LANES && LANE_NOTE_OF(l) != n; l++)
            ;
        if (l < DRUM_LANES)
            mn[step_at(at)][l]++;
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
/* the kick on steps 1 and 5, the snare on step 3, the hat on every step */
static void setup(void)
{
    uint32_t i;
    reset();
    dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[4], 0, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[2], 2, LV_NORM, 0);
    for (i = 0; i < 8u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
}
static int set(uint32_t l, uint32_t i, int32_t v) { return lfx_set(TDRUM, l, i, v); }

static void t_events(void)
{
    track_t *t = TDRUM;
    setup();
    check("the defaults: no event (no ROLL, NUDGE 0, ALWAYS, 100 %)",
          !AL(t)->n && lfx_get(t, 3, LFX_I_ROLL) == 0 && lfx_get(t, 3, LFX_I_CHANCE) == 100 && lfx_get(t, 3, LFX_I_FILL) == FC_NORM);
    check("lane 3: ROLL x3 (2), CHANCE 40, FILL ONLY, NUDGE -8: four hold events of the pseudo-parameters, read back",
          set(3, LFX_I_ROLL, 2) && set(3, LFX_I_CHANCE, 40) && set(3, LFX_I_FILL, FC_FILL) && set(3, LFX_I_NUDGE, -8) &&
          AL(t)->n == 4u && AUTO_IS_LFX(&AL(t)->ev[0]) && !(AL(t)->ev[0].place & AUTO_ONLY) && AUTO_LFX_L(&AL(t)->ev[0]) == 3u &&
          lfx_get(t, 3, LFX_I_ROLL) == 2 && lfx_get(t, 3, LFX_I_CHANCE) == 40 && lfx_get(t, 3, LFX_I_FILL) == FC_FILL &&
          lfx_get(t, 3, LFX_I_NUDGE) == -8 && lfx_get(t, 4, LFX_I_ROLL) == 0);
    check("... each back to its default: its event gone", set(3, LFX_I_CHANCE, 100) && set(3, LFX_I_NUDGE, 0) && AL(t)->n == 2u);
    check("a synth track, lane 10: refused, nothing written", !lfx_set(&trk[0], 3, LFX_I_ROLL, 2) && !set(10, LFX_I_ROLL, 2) &&
          !AL(&trk[0])->n && AL(t)->n == 2u);
    {
        uint8_t m[NSTEP];
        uint32_t i, any = 0;
        auto_marks_all(t, m);
        for (i = 0; i < NSTEP; i++)
            any |= m[i];
        check("a lane's FX marks no step (lane 3: no dot on step 3)", !any && !auto_step_marks(t, 3));
    }
    (void)auto_put(AL(t), 3u | AUTO_ONLY, AUTO_CHANCE, 50);
    check("the step tools leave it: step 3's events dropped (both kinds), the motion and locks cleared",
          auto_step_clear(t, 3, AUTO_HOLDS | AUTO_ONLYS) == 1u && AL(t)->n == 2u && auto_clear_where(t, -1, -1) == 0u &&
          AL(t)->n == 2u && auto_count(AL(t), AUTO_HOLDS) == 0u);
    {
        motion_store_t ms;
        check("the old motion form: left out", auto_to_motion(&ms, AUTO_L(0), 0) && ms.count == 0u);
    }
    {
        uint8_t b[AUTO_TRK_ENC_MAX];
        auto_list_t l2;
        const uint8_t *p = b;
        uint32_t n = auto_enc_trk(AL(t), b);
        check("stored and read back as they were (a pattern's list)", auto_dec_trk(&l2, &p, b + n) && l2.n == 2u &&
              !memcmp(l2.ev, AL(t)->ev, 6));
    }
}
static void t_roll(void)
{
    setup();
    set(4, LFX_I_ROLL, 3);                              /* the hat x4 */
    set(2, LFX_I_ROLL, 5);                              /* the snare x8 */
    play_pass();
    printf("lane fx: hat hits on step 1..3: %u %u %u, snare on step 3: %u, kick on 1: %u\n", hn[0][4], hn[1][4], hn[2][4], hn[2][2], hn[0][0]);
    check("ROLL x4 on the hat: 4 hits in each of its steps (voice and MIDI)", hn[0][4] == 4u && hn[5][4] == 4u && mn[3][4] == 4u);
    printf("lane fx: snare x8: voice %u MIDI %u\n", hn[2][2], mn[2][2]);
    check("ROLL x8 on the snare: 8 hits in its step", hn[2][2] == 8u && mn[2][2] == 8u);
    check("the kick untouched: 1 hit on steps 1 and 5, none elsewhere", hn[0][0] == 1u && hn[4][0] == 1u && !hn[1][0]);
    setup();
    dstep_set(&TDRUM->dstep[2], 2, LV_NORM, 3);         /* the snare's own ratchet x4 */
    set(2, LFX_I_ROLL, 1);                              /* ROLL x2: the larger wins */
    play_pass();
    check("a hit's own ratchet x4 against ROLL x2: 4 hits", hn[2][2] == 4u);
}
static void t_chance(void)
{
    uint32_t i, n = 0;
    setup();
    set(4, LFX_I_CHANCE, 0);
    set(2, LFX_I_ROLL, 2);
    set(2, LFX_I_CHANCE, 0);
    play_pass();
    for (i = 0; i < 8u; i++)
        n += hn[i][4] + mn[i][4];
    check("CHANCE 0 %: the hat silent (voice and MIDI), the snare's ROLL silent with it; the kick plays",
          !n && !hn[2][2] && hn[0][0] == 1u && hn[4][0] == 1u);
    setup();
    set(4, LFX_I_CHANCE, 50);
    for (i = 0, n = 0; i < 8u; i++) {
        uint32_t s;
        play_pass();
        for (s = 0; s < 8u; s++)
            n += hn[s][4];
    }
    printf("lane fx: CHANCE 50 %%: %u of 64 hat hits\n", n);
    check("CHANCE 50 %: about half of 64 hat hits (16..48)", n >= 16u && n <= 48u);
}
static void t_fill(void)
{
    setup();
    set(2, LFX_I_FILL, FC_FILL);
    set(0, LFX_I_FILL, FC_NOFILL);
    play_pass();
    check("FILL ONLY (the snare): silent while no fill; NO FILL (the kick): heard", !hn[2][2] && hn[0][0] == 1u && hn[4][0] == 1u);
    fill_held = 1;
    play_pass();
    check("a fill held: the snare heard, the kick silent", hn[2][2] == 1u && !hn[0][0] && !hn[4][0] && hn[1][4] == 1u);
    fill_held = 0;
}
static void t_nudge(void)
{
    uint32_t g;
    early = SLEN / 3u;                                  /* (a hit up to a third of a step early is its step's) */
    setup();
    play_pass();
    g = at0[2][2];                                      /* the snare on the grid */
    setup();
    set(2, LFX_I_NUDGE, 16);                            /* a quarter step late */
    play_pass();
    printf("lane fx: snare at %u on the grid, %u nudged +16 (a step %u)\n", g, at0[2][2], SLEN);
    check("NUDGE +16: the snare a quarter step later, once; the hat on the grid",
          hn[2][2] == 1u && at0[2][2] >= g + SLEN / 4u - SLEN / 16u && at0[2][2] <= g + SLEN / 4u + SLEN / 16u &&
          hn[2][4] == 1u && at0[2][4] <= g + SLEN / 16u);
    setup();
    set(2, LFX_I_NUDGE, -16);                           /* a quarter step early */
    play_pass();
    printf("lane fx: snare nudged -16 at %u\n", at0[2][2]);
    check("NUDGE -16: the snare a quarter step earlier, once (not again on its own step), voice and MIDI",
          hn[2][2] == 1u && mn[2][2] == 1u && at0[2][2] + SLEN / 4u >= g - SLEN / 16u && at0[2][2] + SLEN / 4u <= g + SLEN / 16u);
    setup();
    set(4, LFX_I_NUDGE, -16);
    set(4, LFX_I_ROLL, 1);
    play_pass();
    check("NUDGE -16 with ROLL x2 on the hat: 2 hits in each step", hn[0][4] == 2u && hn[3][4] == 2u && hn[7][4] == 2u);
    early = SLEN / 16u;
}

static void t_page2(void)
{
    uint32_t i, lo = 127, hi = 0, g, n;
    setup();
    set(4, LFX_I_VRND, 20);
    for (i = 0; i < 4u; i++) {
        uint32_t st;
        play_pass();
        for (st = 0; st < 8u; st++)
            lo = vmin[st][4] < lo ? vmin[st][4] : lo, hi = vmax[st][4] > hi ? vmax[st][4] : hi;
    }
    printf("lane fx: VEL RAND 20 on the hat (NORM 100): %u .. %u\n", lo, hi);
    check("VEL RAND 20: the hat's velocities spread within 80..120 (both sides), one hit a step", lo >= 80u && hi <= 120u && lo < 95u && hi > 105u && hn[3][4] == 1u);
    setup();
    play_pass();
    g = at0[2][2];
    setup();
    set(2, LFX_I_FLAM, 4);                              /* 20 ms */
    early = SLEN / 3u;
    play_pass();
    early = SLEN / 16u;
    printf("lane fx: FLAM 20 ms: the snare's first sound %u before the grid, %u sounds, velocities %u then %u\n", g - at0[2][2], hn[2][2], v0[2][2], vlast[2][2]);
    n = 20u * (FS / 1000u) * 120u;                      /* (20 ms in clock units) */
    check("FLAM 20 ms: a grace note ~20 ms before the snare's hit at 60 %, then the hit at full velocity",
          hn[2][2] == 2u && mn[2][2] == 2u && g - at0[2][2] + SLEN / 64u >= n && g - at0[2][2] <= n + SLEN / 64u && v0[2][2] == 60u && vlast[2][2] == 100u);
    setup();
    set(4, LFX_I_ROLL, 3);
    set(4, LFX_I_RAMP, 100);
    play_pass();
    printf("lane fx: ROLL x4 RAMP +100: first %u last %u\n", v0[3][4], vlast[3][4]);
    check("ROLL x4 RAMP +100: fades in, the first hit the softest, the last at full", hn[3][4] == 4u && v0[3][4] < 10u && vlast[3][4] == 100u);
    set(4, LFX_I_RAMP, -50);
    play_pass();
    check("RAMP -50: fades out, the first at full, the last at half", v0[3][4] == 100u && vlast[3][4] == 50u);
    setup();
    set(2, LFX_I_EVERY, 1);                             /* 1:2 */
    transport_req = 1;
    heard_clear();
    while (clk_beat < 8u)                               /* (four passes of 8 steps, two beats each) */
        run_block();
    seq_stop();
    clk_beat = clk_pos = 0;
    printf("lane fx: EVERY 1:2: the snare %u times in 4 passes, the kick %u\n", hn[2][2], hn[0][0]);
    check("EVERY 1:2: the snare on 2 of 4 passes, the kick on all", hn[2][2] == 2u && hn[0][0] == 4u);
}

int main(void)
{
    (void)auto_for(&proj_tmp.cur, 1), (void)auto_for(&sec_stage_p, 1);
    auto_init();
    t_events();
    t_roll();
    t_chance();
    t_fill();
    t_nudge();
    t_page2();
    printf("lane fx: %s (%d failed)\n", bad ? "FAILED" : "passed", bad);
    return bad;
}
