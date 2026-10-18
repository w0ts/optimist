/* SPDX-License-Identifier: GPL-3.0-only */
/* Slot-relative automation (seq/auto_slot.c, always built since 2026-10-18): a track's locks and motion on the values of the
 * device in its insert slot S1 / S2 by (slot, parameter), on the lane tests' host (every switch of the store on):
 *   ids        AUTO_SLOTP(s, i) 0xDC .. 0xFB: after the stored tail's 16, below the pseudo-parameters; well formed,
 *              a list of them stored and read back byte for byte
 *   lookup     sa_vp / sa_find: EQ3's bands in the slot holding it, none in another slot, none for a track value
 *   play       a lock heard on its step only, then the base; a hold until STOP; a lock over a hold, then the hold's value
 *   api        sa_kind_toggle both ways, sa_drop, sa_knob (not recording: the locks / holds in force let go; recording:
 *              a hold at the step playing, heard at once, its base the value before the card)
 *   a swap     another type into the slot: that slot's events gone, its value back; the other slot's kept (ruled)
 * Exit status: the number of failed checks. Run by tests/run_tests.sh. */
#include "lane_host.h"

static int bad;
static void check(const char *what, int ok)
{
    printf("slot: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define T (&trk[0])
#define LO 0u                                          /* b of slot 0's LO, slot 0's MID */
#define MID 1u
static void wait_step(track_t *t, uint32_t step)
{
    uint32_t g = 0;
    int32_t out[CTL * 2];
    while (t->seq_abs != SEQ_NONE && t->seq_idx == step && g++ < 100000u)
        mix_block(out, CTL);
    while (!(t->seq_abs != SEQ_NONE && t->seq_idx == step) && g++ < 100000u)
        mix_block(out, CTL);
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
        AUTO_L(i)->n = 0;
    }
    auto_w.on = 0;
    song.g[G_BPM] = 120;
    song.rec = 0;
    song.playing = 0;
    ins_load(0, 1, FXT_NONE);
    ins_load(0, 0, FXT_EQ3);
    EQ3_V(0)[LO] = 0, EQ3_V(0)[MID] = 0;
}

static void t_ids(void)
{
    static uint8_t b[AUTO_TRK_ENC_MAX];
    static auto_list_t a, c;
    uint32_t s, i, n, ok = 1;
    const uint8_t *p;
    a.n = 0;
    for (s = 0; s < 2u; s++)
        for (i = 0; i < 16u; i++) {
            auto_ev_t e = {(uint8_t)(i | AUTO_ONLY), (uint8_t)AUTO_SLOTP(s, i), (int8_t)(i - 8)};
            ok &= AUTO_IS_SLOTP(e.param) && AUTO_SLOTP_B(e.param) == (s << 4 | i) && !AUTO_IS_LANE(e.param) &&
                  !AUTO_PSEUDO(e.param) && auto_ev_ok(&e);
            if (s == 0u && i < 8u)
                (void)auto_put(&a, e.place, e.param, e.value);
        }
    check("AUTO_SLOTP(s, i): 0xDC .. 0xFB, slot and parameter back, not a lane's nor a pseudo-parameter, well formed",
          ok && AUTO_SLOTP(0, 0) == 0xDCu && AUTO_SLOTP(1, 15) == 0xFBu && !AUTO_IS_SLOTP(0xDB) && !AUTO_IS_SLOTP(AUTO_COND));
    n = auto_enc_trk(&a, b);
    p = b;
    check("a list of them stored and read back byte for byte", auto_dec_trk(&c, &p, b + n) && p == b + n &&
          !memcmp(&a, &c, 1u + 3u * a.n));
}

static void t_lookup(void)
{
    const param_desc_t *d;
    reset();
    check("sa_vp: slot 0 holds EQ3: its band i is eq3_v's (LO .. FREQ H), none past them", sa_vp(0, 0, 0, &d) == &EQ3_V(0)[0] &&
          d == &EQ_D[0] && sa_vp(0, 0, 4, &d) == &EQ3_V(0)[4] && !sa_vp(0, 0, EQV_N, &d));
    check("... slot 1 (empty): none; another track's slot 0 (not EQ3): none", !sa_vp(0, 1, 0, &d) && !sa_vp(1, 0, 0, &d));
    check("sa_find: EQ3's MID -> slot 0's 1; a track value -> -1", sa_find(0, &EQ3_V(0)[1]) == 1 && sa_find(0, &T->p[P_LEVEL]) < 0);
    ins_load(0, 1, FXT_EQ3);                           /* (into slot 1: the slots swap) */
    check("EQ3 moved to slot 1: its MID is slot 1's 1 (0x11)", ins_slot[0][1] == FXT_EQ3 && sa_find(0, &EQ3_V(0)[1]) == 0x11);
}

static void t_play(void)
{
    int32_t lo = EQ_D[LO].min, mid = EQ_D[MID].max, mid2 = EQ_D[MID].min;
    uint32_t ok;
    reset();
    fm1_irq_off();
    (void)sa_lock_set(T, 2, LO, lo);
    (void)sa_put(T, 4, MID, mid);                      /* (a hold of MID from step 5) */
    (void)sa_lock_set(T, 5, MID, mid2);                /* (a lock over it on step 6) */
    fm1_irq_on();
    auto_w.on |= 1u;
    transport_req = 1;
    wait_step(T, 2);
    ok = EQ3_V(0)[LO] == lo && EQ3_V(0)[MID] == 0;
    wait_step(T, 3);
    check("a lock: heard on its step (LO KILL on step 3), the base after", ok && EQ3_V(0)[LO] == 0);
    wait_step(T, 4);
    ok = EQ3_V(0)[MID] == mid;
    wait_step(T, 5);
    ok &= EQ3_V(0)[MID] == mid2;
    wait_step(T, 6);
    check("a hold (MID +6 from step 5), a lock over it on step 6, the hold's value after", ok && EQ3_V(0)[MID] == mid);
    seq_stop();
    check("STOP: the values back to the base, nothing in force", EQ3_V(0)[MID] == 0 && EQ3_V(0)[LO] == 0 && !sa_hm[0] && !sa_lm[0]);
}

static void t_api(void)
{
    int32_t v;
    reset();
    fm1_irq_off();
    (void)sa_lock_set(T, 3, LO, -5);
    check("YES on a lock: a hold, its value kept, PLAY on", sa_kind_toggle(T, 3, LO) == 1u && sa_hold_get(T, 3, LO, &v) &&
          v == -5 && !sa_lock_get(T, 3, LO, &v) && (auto_w.on & 1u));
    check("... and back: a lock", sa_kind_toggle(T, 3, LO) == 2u && sa_lock_get(T, 3, LO, &v) && v == -5);
    (void)sa_put(T, 3, LO, 2);
    sa_drop(T, 3, LO);
    check("sa_drop: both kinds gone", !sa_lock_get(T, 3, LO, &v) && !sa_hold_get(T, 3, LO, &v) && !AUTO_L(0)->n);
    check("sa_kind_toggle with no event: 0", sa_kind_toggle(T, 3, LO) == 0u);
    fm1_irq_on();
    sa_hm[0] = 1u << MID, sa_lm[0] = 1u << MID;
    check("sa_knob not recording: 0 (the card keeps its value), the hold and lock in force let go",
          sa_knob(0, MID, 3, 0) == 0 && !sa_hm[0] && !sa_lm[0] && !AUTO_L(0)->n);
    song.rec = 1u;
    transport_req = 1;
    wait_step(T, 2);
    EQ3_V(0)[MID] = 4;                                 /* (the card writes the value, then calls sa_knob) */
    check("sa_knob recording: a hold near the step playing, heard at once, PLAY on",
          sa_knob(0, MID, 4, -2) == 1 && AUTO_L(0)->n == 1u && AUTO_IS_SLOTP(AUTO_L(0)->ev[0].param) &&
          AUTO_L(0)->ev[0].value == 4 && ((sa_hm[0] >> MID) & 1u) && (auto_w.on & 1u));
    seq_stop();
    check("... STOP: back to its base, the value before the card (-2)", EQ3_V(0)[MID] == -2);
    song.rec = 0;
}

static void t_swap(void)
{
    int32_t v;
    reset();
    fm1_irq_off();
    (void)sa_lock_set(T, 1, LO, -7);
    (void)sa_put(T, 2, 0x10u | LO, 3);                 /* (slot 1's: kept by slot 0's swap) */
    (void)auto_put(AUTO_L(0), 0, mot_sid(P_LEVEL), 50);  /* (a track value's: kept) */
    fm1_irq_on();
    sa_lm[0] = 1u << LO, sa_base[0][LO] = 0, EQ3_V(0)[LO] = -7;   /* (the lock in force) */
    ins_load(0, 0, FXT_NONE);
    check("another type into slot 0: its events gone, its value back (ruled: cleared); slot 1's and the track's kept",
          !sa_lock_get(T, 1, LO, &v) && sa_hold_get(T, 2, 0x10u | LO, &v) && v == 3 && AUTO_L(0)->n == 2u &&
          EQ3_V(0)[LO] == 0 && !sa_lm[0]);
}

int main(void)
{
    (void)auto_for(&proj_tmp.cur, 1), (void)auto_for(&sec_stage_p, 1);
    auto_init();
    printf("slot: SLOT_AUTO %d\n", (int)SLOT_AUTO);
    t_ids();
    t_lookup();
    t_play();
    t_api();
    t_swap();
    printf("slot test %s (%d failed)\n", bad ? "FAILED" : "passed", bad);
    return bad;
}
