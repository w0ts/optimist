/* SPDX-License-Identifier: GPL-3.0-only */
/* The note generators (seq/gen.c, FELUCCA_SEQTOOLS): a layer the sequencer plays over the pattern.
 *   1. EUCLID in REPLACE = the old EUCLID writer (op_tools.c eu_fn before feat/note-generators, copied below) for the
 *      same values, on random synth patterns (NOTEs, TIEs, carry steps, RESTs) and drum lanes
 *   2. the modes on a drum lane: ADD the union, MASK the pattern's hits where it hits
 *   3. WRITE = heard: the sequencer's MIDI out (every note on / off, its block) over 4 passes with the generator ON,
 *      then after WRITE with it OFF, the same; with chance events, conditions, nudges and locks on the steps
 *   4. determinism: RANDOM plays the same at another rng() state; two passes alike; PLAY again from step 1
 *   5. a step's chance event holds for a generated note (0 %: never), its condition too
 *   6. zero cost off: no generator, the effective step is the pattern's own (the pointer)
 *   7. the stored form: GEN_TLV through the FX record (fxr_encode / fxr_decode), none: every slot off
 * Exit status: the number of failed checks. */
#define FELUCCA_MOTION 1
#define FELUCCA_PLOCK 1
#define FELUCCA_MICRO 1
#define FELUCCA_FILLS 1
#define FELUCCA_CHANCE 1
#define FELUCCA_COND 1
#define FELUCCA_SEQTOOLS 1
#define GEN_BUILT 1                     /* (the generators without the Optimist UI around them) */
#define FELUCCA_MIDI_OUT 1
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
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
#include "../firmware/src/storage/motion_flash.c"

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
    printf("gen: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---- 1. the old EUCLID writer (op_tools.c at 17af3ff0: eu_hit, eu_src, eu_fn) */
static uint32_t ref_hits, ref_steps, ref_rot;
static int ref_gate(const step_t *s) { return s->time == ST_NOTE && s->n; }
static void ref_clr(step_t *s)
{
    memset(s, 0, sizeof *s);
    s->time = ST_REST;
}
static int ref_hit(uint32_t i)
{
    uint32_t n = ref_steps ? ref_steps : 1u, p = (i + n - ref_rot % n) % n;
    return (p * ref_hits) % n < ref_hits;
}
static const step_t *ref_src(const step_t *src, uint32_t i)
{
    uint32_t j = i;
    while (j && src[j].time == ST_TIE)
        j--;
    return ref_gate(&src[j]) && (j == i || src[i].time == ST_TIE) ? &src[j] : 0;
}
static void ref_fn(track_t *t, uint32_t len, const step_t *src, uint32_t l)
{
    uint32_t i;
    if (is_drum(t)) {
        for (i = 0; i < len; i++)
            if (!ref_hit(i))
                dstep_clr(&t->dstep[i], l);
            else if (!dstep_has(&t->dstep[i], l))
                dstep_set(&t->dstep[i], l, LV_NORM, 0);
        return;
    }
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        const step_t *o = ref_src(src, i);
        if (ref_hit(i)) {
            if (o != &src[i]) {
                int32_t r = 60 + t->p[P_ROOT] % 12;
                ref_clr(s);
                s->time = ST_NOTE;
                s->vel = 100;
                if (o)
                    memcpy(s->note, o->note, sizeof s->note), s->n = o->n;
                else
                    s->note[0] = (uint8_t)(r > 66 ? r - 12 : r), s->n = 1;
            } else if (i && !ref_gate(&t->step[i - 1u]) && t->step[i - 1u].time != ST_TIE) {
                s->flags &= (uint8_t)~SF_CARRY;
            }
        } else if (!(s->time == ST_TIE && i && (t->step[i - 1u].time == ST_TIE || ref_gate(&t->step[i - 1u])))) {
            ref_clr(s);
        }
    }
}
static uint32_t lcg = 12345u;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1103515245u + 12345u; return (lcg >> 8) % (n ? n : 1u); }
static void rnd_pattern(track_t *t, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        uint32_t r = rnd(10);
        memset(s, 0, sizeof *s);
        s->time = r < 4u ? ST_NOTE : r < 7u ? ST_TIE : ST_REST;
        if (s->time == ST_NOTE) {
            s->n = (uint8_t)(1u + rnd(2));
            s->note[0] = (uint8_t)(48u + rnd(24)), s->note[1] = (uint8_t)(48u + rnd(24));
            s->vel = (uint8_t)(60u + rnd(60));
            s->flags = (uint8_t)(rnd(3) ? 0u : SF_CARRY) | (uint8_t)(rnd(4) ? 0u : SF_ACCENT);
            s->lvl = (uint8_t)rnd(256), s->rat = (uint8_t)(rnd(4) ? 0u : rnd(256));
            s->ngate[0] = (uint8_t)(rnd(3) ? 0u : rnd(128));
        }
    }
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_ROOT] = (int16_t)rnd(12);
}
static void euclid_ref_tests(void)
{
    static step_t src[NSTEP], w[NSTEP];
    track_t *t = &trk[0], *d = TDRUM;
    uint32_t run, i, same = 1, dsame = 1;
    for (run = 0; run < 400u; run++) {
        uint32_t len = 1u + rnd(NSTEP), l = rnd(DRUM_SLANES);
        gen_reset_all();
        rnd_pattern(t, len);
        memcpy(src, t->step, sizeof src);
        gen[0].on = 1, gen[0].mode = GM_REPLACE;
        gen[0].p[0] = (uint8_t)rnd(20), gen[0].p[1] = (uint8_t)(1u + rnd(20)), gen[0].p[2] = (uint8_t)rnd(20);
        eu_gfit(&gen[0], len);
        ref_hits = gen[0].p[0], ref_steps = gen[0].p[1], ref_rot = gen[0].p[2];
        gen_sync();
        gen_render(t, w, ~0u);
        ref_fn(t, len, src, 0);
        for (i = 0; i < len; i++)
            same &= !memcmp(&w[i], &t->step[i], sizeof w[i]);
        memcpy(t->step, src, sizeof src);
        /* the drum track's lane l */
        for (i = 0; i < NSTEP; i++) {
            memset(&d->dstep[i], 0, sizeof d->dstep[i]);
            if (rnd(2))
                dstep_set(&d->dstep[i], rnd(DRUM_SLANES), rnd(4), rnd(4));
            if (rnd(2))
                dstep_set(&d->dstep[i], l, rnd(4), rnd(4));
        }
        d->p[P_SLEN] = (int16_t)len;
        gen[0].on = 0;
        gen[NPART + l] = gen[0];
        gen[NPART + l].on = 1;
        gen_sync();
        gen_render(d, w, ~0u);
        ref_fn(d, len, 0, l);
        for (i = 0; i < len; i++)
            dsame &= !memcmp(&((const dstep_t *)(const void *)w)[i], &d->dstep[i], sizeof(dstep_t));
    }
    check("EUCLID REPLACE = the old EUCLID writer: 400 random synth patterns (NOTE TIE carry REST), every LEN 1..64", same);
    check("... and 400 drum lanes (the ACCENT lane among them), the other lanes as they were", dsame);
    gen_reset_all();
    steps_clear(t), steps_clear(d);
}

/* ---- 2. the modes on a drum lane */
static void mode_tests(void)
{
    static step_t w[NSTEP];
    const dstep_t *o = (const dstep_t *)(const void *)w;
    track_t *d = TDRUM;
    uint32_t i, a = 0, m = 0, r = 0, pat = 0, hits = 0;
    steps_clear(d);
    d->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i += 3u)
        dstep_set(&d->dstep[i], 2, LV_SOFT, 1), pat |= 1u << i;
    gen_reset_all();
    gen[NPART + 2].on = 1;                              /* EUCLID 4 of 16: 0 4 8 12 */
    for (i = 0; i < 16u; i++)
        hits |= (uint32_t)eu_gstep(&gen[NPART + 2], i, 16, 0, &(gen_ev_t){0, 0, 0}) << i;
    gen_sync();
    gen_render(d, w, ~0u);
    for (i = 0; i < 16u; i++)
        r |= (uint32_t)dstep_has(&o[i], 2) << i;
    gen[NPART + 2].mode = GM_ADD;
    gen_render(d, w, ~0u);
    for (i = 0; i < 16u; i++)
        a |= (uint32_t)dstep_has(&o[i], 2) << i;
    gen[NPART + 2].mode = GM_MASK;
    gen_render(d, w, ~0u);
    for (i = 0; i < 16u; i++)
        m |= (uint32_t)dstep_has(&o[i], 2) << i;
    check("REPLACE: the hits alone; ADD: the union; MASK: the pattern where it hits", r == hits && a == (hits | pat) && m == (hits & pat));
    check("... MASK keeps the pattern's level and ratchet (SOFT x2 on step 1)", dstep_lvl(&o[0], 2) == LV_SOFT && dstep_rat(&o[0], 2) == 1u);
    gen[NPART + 2].type = GT_PULSE, gen[NPART + 2].mode = GM_ADD;
    gen[NPART + 2].p[0] = 4, gen[NPART + 2].p[1] = 1, gen[NPART + 2].p[2] = 3;
    gen_render(d, w, ~0u);
    check("PULSE 4 from 2, x3: hits on 2 6 10 14 played x3, the pattern's as they were (ADD)",
          dstep_has(&o[1], 2) && dstep_rat(&o[1], 2) == 2u && dstep_has(&o[13], 2) && dstep_has(&o[3], 2) && dstep_rat(&o[3], 2) == 1u &&
          !dstep_has(&o[2], 2));
    gen_reset_all();
    steps_clear(d);
}

/* ---- the sequencer's MIDI out, a block each event */
typedef struct { uint32_t blk, pkt; } mev_t;
static mev_t evs[2][6000];
static uint32_t nev[2];
static void play_capture(uint32_t which, uint32_t beats)
{
    uint32_t blk = 0;
    int32_t out[CTL * 2];
    nev[which] = 0;
    for (blk = 0; blk < NTRK; blk++)                    /* (an LFO's wrap draws rng(): each run from the same phase, so */
        trk[blk].lfo_ph = 0;                            /* the chance rolls are the same draws) */
    blk = 0;
    rng_state = 0x1234567u;
    clk_beat = clk_pos = 0;
    mo_r = mo_w;
    transport_req = 1;
    while (clk_beat < beats) {
        mix_block(out, CTL);
        while (mo_r != mo_w) {
            if (nev[which] < 6000u)
                evs[which][nev[which]].blk = blk, evs[which][nev[which]++].pkt = midi_out_q[mo_r % MQ];
            mo_r++;
        }
        blk++;
    }
    seq_stop();
    while (mo_r != mo_w)
        mo_r++;
}
static int same_stream(void)
{
    return nev[0] == nev[1] && !memcmp(evs[0], evs[1], nev[0] * sizeof evs[0][0]);
}
static uint32_t ons(uint32_t which)                    /* T1's note ons (channel 1) */
{
    uint32_t i, n = 0;
    for (i = 0; i < nev[which]; i++)
        n += (evs[which][i].pkt & 0xFFFFu) == 0x9009u;
    return n;
}
static void heard_setup(void)
{
    uint32_t i, k;
    host_tracks_init();
    for (k = 0; k < NTRK; k++) {
        steps_clear(&trk[k]);
        trk[k].seq_abs = SEQ_NONE;
        AUTO_L(k)->n = 0;
    }
    auto_w.on = 0;
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
    song.g[G_BPM] = 120;
    usb.config = 1;
    bp_set[BPS_MOUT] = 1;
    gen_reset_all();
    for (i = 0; i < 16u; i++)                           /* T1: notes, ties, rests */
        if (i % 5u == 0u) {
            step_t *s = &trk[0].step[i];
            s->time = ST_NOTE, s->n = 1, s->note[0] = (uint8_t)(50u + i), s->vel = 90;
        } else if (i % 5u == 1u)
            trk[0].step[i].time = ST_TIE;
    trk[0].p[P_SLEN] = 16;
    TDRUM->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i += 2u)
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
    (void)step_chance_set(&trk[0], 4, 50);              /* (on a step the generator fills) */
    (void)step_chance_set(TDRUM, 3, 40);
    (void)step_cond_set(&trk[0], 8, 1);                 /* 1:2 */
    (void)step_micro_set(&trk[0], 12, 9);               /* a nudge */
    (void)lock_set(&trk[0], 2, P_PAN, 30);
    auto_touch_all();
}
static void heard_tests(void)
{
    static const uint8_t TY[3] = {GT_EUCLID, GT_RANDOM, GT_PULSE};
    uint32_t m, ty, ok = 1, more = 1;
    for (ty = 0; ty < 3u; ty++)
        for (m = 0; m < GM_N; m++) {
            heard_setup();
            gen_defaults(&gen[0], TY[ty]);
            gen_defaults(&gen[NPART + 4], TY[ty]);
            gen[0].on = gen[NPART + 4].on = 1;
            gen[0].mode = gen[NPART + 4].mode = (uint8_t)m;
            gen[0].p[0] = (uint8_t)(TY[ty] == GT_EUCLID ? 5u : TY[ty] == GT_RANDOM ? 60u : 3u);
            gen_sync();
            play_capture(0, 16);                        /* 4 passes */
            (void)gen_write(&trk[0], 0);
            (void)gen_write(TDRUM, NPART + 4);
            gen[0].on = gen[NPART + 4].on = 0;
            gen_sync();
            play_capture(1, 16);
            if (!same_stream()) {
                uint32_t j;
                for (j = 0; j < nev[0] && j < nev[1] && evs[0][j].blk == evs[1][j].blk && evs[0][j].pkt == evs[1][j].pkt; j++)
                    ;
                printf("  type %u mode %u: %u events heard, %u after WRITE; first difference #%u: blk %u %08x / blk %u %08x\n", ty, m,
                       nev[0], nev[1], j, evs[0][j].blk, evs[0][j].pkt, evs[1][j].blk, evs[1][j].pkt);
                ok = 0;
            }
            more &= nev[0] > 16u;
        }
    check("WRITE = heard: the MIDI out (every note on / off, its block) with the generator ON = after WRITE, OFF "
          "(EUCLID RANDOM PULSE x REPLACE ADD MASK; T1 and a drum lane; chance, condition, nudge, lock on the steps)", ok && more);
}

/* ---- 4. determinism, 5. chance and conditions, 6. off */
static void det_tests(void)
{
    uint32_t i;
    step_t x;
    heard_setup();
    gen[0].type = GT_RANDOM, gen[0].p[0] = 50, gen[0].p[1] = 7, gen[0].p[2] = 30, gen[0].on = 1;
    gen_sync();
    play_capture(0, 16);
    rng_state = 0x9999u;
    {
        static mev_t keep[6000];
        uint32_t n = nev[0];
        memcpy(keep, evs[0], sizeof keep);
        AUTO_L(0)->n = 0, AUTO_L(TRK_DRUM)->n = 0;      /* (no chance: no rng() in the stream at all) */
        auto_touch_all();
        play_capture(0, 16);
        rng_state = 0xABCDEFu;
        nev[1] = 0;
        {
            int32_t out[CTL * 2];
            uint32_t blk = 0;
            clk_beat = clk_pos = 0;
            mo_r = mo_w;
            transport_req = 1;
            while (clk_beat < 16u) {
                mix_block(out, CTL);
                while (mo_r != mo_w) {
                    if (nev[1] < 6000u)
                        evs[1][nev[1]].blk = blk, evs[1][nev[1]++].pkt = midi_out_q[mo_r % MQ];
                    mo_r++;
                }
                blk++;
            }
            seq_stop();
        }
        check("RANDOM: the same notes at another rng() state (its own hash; the chance rolls undisturbed)", same_stream());
        {
            uint32_t half = nev[0] / 4u, j, ok = nev[0] % 4u == 0u, b0 = evs[0][0].blk, bp;
            bp = nev[0] ? evs[0][half].blk - b0 : 0;
            for (j = 0; ok && j < half; j++)
                ok = evs[0][j].pkt == evs[0][j + half].pkt;
            check("... each pass as the one before (4 passes alike)", ok && bp > 0u && n > 0u);
        }
    }
    {   /* PLAY again after a stop part way through: from step 1, as the first time */
        heard_setup();
        AUTO_L(0)->n = 0, AUTO_L(TRK_DRUM)->n = 0;
        auto_touch_all();
        gen[0].type = GT_RANDOM, gen[0].p[0] = 50, gen[0].p[1] = 3, gen[0].on = 1;
        gen[NPART].on = 1, gen[NPART].p[0] = 3, gen[NPART].p[1] = 7;
        gen_sync();
        play_capture(0, 8);
        play_capture(1, 3);                             /* (stopped 3 beats in: 12 of 16 steps) */
        play_capture(1, 8);
        check("PLAY again after a stop mid-loop: the generators from step 1 (the same notes as the first PLAY)", same_stream());
    }
    /* 5. a chance event at 0 % and a failing condition hold for a generated note */
    heard_setup();
    AUTO_L(0)->n = 0;
    steps_clear(&trk[0]);
    trk[0].p[P_SLEN] = 4;
    gen[0].type = GT_PULSE, gen[0].p[0] = 1, gen[0].p[1] = 0, gen[0].p[2] = 1, gen[0].on = 1;   /* a note on every step */
    gen_sync();
    play_capture(0, 4);
    (void)step_chance_set(&trk[0], 1, 0);
    (void)step_cond_set(&trk[0], 2, 1);                 /* 1:2: passes 1 and 3 only */
    auto_touch_all();
    play_capture(1, 4);
    check("a step's chance event (0 %) silences the generated note there, its condition (1:2) every other pass",
          ons(0) == 16u && ons(1) == 16u - 4u - 2u);
    if (ons(0) != 16u || ons(1) != 10u)
        printf("  T1's notes: %u, then %u\n", ons(0), ons(1));
    /* 6. off: the pattern's own step, no copy */
    gen_reset_all();
    check("off: the step played is the pattern's own (no copy, one byte tested)",
          gen_eff(&trk[0], 3, &x) == &trk[0].step[3] && gen_deff(TDRUM, 3, (dstep_t *)(void *)&x) == &TDRUM->dstep[3] && !gen_trk);
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
}

/* ---- 7. the stored form */
static void tlv_tests(void)
{
    static uint8_t rec[FXR_MAX];
    gen_t a, b;
    uint32_t n, s;
    int ok = 1;
    heard_setup();
    AUTO_L(0)->n = 0, AUTO_L(TRK_DRUM)->n = 0;
    n = fxr_encode(rec);
    check("every slot off at its defaults: no GEN_TLV", (n == 0u || !memchr(rec + FXR_HEAD, GEN_TLV, 1)) && gen_tlv(rec) == 0u);
    for (s = 0; s < GEN_NSLOT; s++) {                   /* the longest: every slot set */
        gen[s].type = (uint8_t)(s % GT_N), gen[s].mode = (uint8_t)(s % GM_N), gen[s].on = (uint8_t)(s & 1u);
        gen[s].p[0] = (uint8_t)(1u + s % 4u), gen[s].p[1] = (uint8_t)(1u + s % 3u), gen[s].p[2] = (uint8_t)(1u + s % 2u);
    }
    gen_sync();
    a = gen[5];
    n = fxr_encode(rec);
    gen_reset_all();
    ok = n > 0u && n <= FXR_MAX && fxr_decode(rec, n, 1);
    b = gen[5];
    for (s = 0; s < GEN_NSLOT; s++)
        ok &= gen[s].type == s % GT_N && gen[s].mode == s % GM_N && gen[s].on == (s & 1u);
    check("GEN_TLV in the FX record: every slot back (type, mode, on, values), the longest within FXR_MAX",
          ok && !memcmp(&a, &b, sizeof a) && (gen_trk & 2u));
    (void)fxr_decode(0, 0, 0);
    check("... a project without one: every slot off at its defaults", !gen_trk && gen_is_default(&gen[7]));
}

int main(void)
{
    euclid_ref_tests();
    mode_tests();
    heard_tests();
    det_tests();
    tlv_tests();
    printf(bad ? "gen test FAILED (%d)\n" : "gen test passed\n", bad);
    return bad;
}
