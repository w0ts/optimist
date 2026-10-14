/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Voice allocation, envelopes, LFO and per-track rendering. Runs in the
 * audio ISR.
 *
 * Each synth part has its own NVOICE voices (so MONO / LEGATO / UNISON keep using
 * v[0..]), but only NVOICE of all the parts' voices sound at once: a part that starts
 * a voice while the budget is full takes one from any part (voice_victim): the oldest
 * released voice, else the oldest extra UNISON voice, else the oldest held voice of a
 * POLY part that is not its lowest note. A voice taken from another part fades out
 * over one block (stage 4: the envelope goes to 0, the block's amplitude ramp
 * declicks it); one of the part's own is restarted in place. Extra UNISON
 * voices only start when there is room. The drum track has its own voices (drums.c). */
static uint32_t vage;                                   /* voice ages: one clock for every part */
/* engines that play recorded material (a position, not a phase): no phases kept or spread */
#if FELUCCA_SLICE
AINL int eng_sampled(const engine_t *e) { return ENG_IS(e, SAMPLE) || ENG_IS(e, SLICE); }
#else
AINL int eng_sampled(const engine_t *e) { return ENG_IS(e, SAMPLE); }
#endif
AINL int32_t lfo_wave(track_t *t, uint32_t ph)
{
    switch (t->p[P_LWAVE]) {
    case 1:
        return osc_tri(ph);
    case 2:
        return (int32_t)(ph >> 16) - 32768;
    case 3:
        return ph < 0x80000000u ? 32767 : -32767;
    case 4:
        return (int32_t)(t->lfo_rnd >> 16) - 32768;
    default:
        return osc_sine(ph);
    }
}

#if FELUCCA_DUAL >= 2
/* dual core (dual.c): the render draws no rng() (two cores render at once: a race on rng_state, and the draws
 * would come in the cores' order). The values a part's LFO takes at its wrap, drawn by CPU0 before the fork in the
 * single-core order (dual_lfo_draw): the same samples as the single-core build */
static uint32_t dual_lfo_rnd[NPART];
#endif
/* RATE below 0: one cycle per note value of the tempo, SLOOP 2.6's LFO 2 SYNC (after Felucca 1.4 by Leo Kuroshita;
 * its P_LSYNC n is our RATE -n, params.c N_LSYNC / LSYNC_N: -1 4BAR .. -10 1/32). Its cycle in 24ths of a beat as SLOOP's
 * LSYNC_PULSES; the phase moves by what the transport clock moves (clk_blk: the tempo, the nudge, an external
 * clock), so it stays with the steps; LSYNC_KT: 2^48 / the cycle in clock units (a beat is BEAT_U = FS x 60), the
 * step (clk_blk x K) >> 16, no divide in the ISR. A fresh phrase restarts it at PHS as before (SLOOP's TRIG NOTE) */
#define LSYNC_U (FS * 5u / 2u)                         /* a 24th of a beat, in clock units */
#define LSYNC_ONE 281474976710656ull                    /* 2^48 */
_Static_assert(BEAT_U == LSYNC_U * 24u, "a cycle of P 24ths of a beat is LSYNC_U x P units");
static const uint32_t LSYNC_KT[LSYNC_N] = {           /* 4BAR 2BAR 1BAR 1/2 1/4 1/8 8T 1/16 16T 1/32: P 384 .. 3 */
    (uint32_t)((LSYNC_ONE + LSYNC_U * 192u) / (LSYNC_U * 384u)), (uint32_t)((LSYNC_ONE + LSYNC_U * 96u) / (LSYNC_U * 192u)),
    (uint32_t)((LSYNC_ONE + LSYNC_U * 48u) / (LSYNC_U * 96u)), (uint32_t)((LSYNC_ONE + LSYNC_U * 24u) / (LSYNC_U * 48u)),
    (uint32_t)((LSYNC_ONE + LSYNC_U * 12u) / (LSYNC_U * 24u)), (uint32_t)((LSYNC_ONE + LSYNC_U * 6u) / (LSYNC_U * 12u)),
    (uint32_t)((LSYNC_ONE + LSYNC_U * 4u) / (LSYNC_U * 8u)), (uint32_t)((LSYNC_ONE + LSYNC_U * 3u) / (LSYNC_U * 6u)),
    (uint32_t)((LSYNC_ONE + LSYNC_U * 2u) / (LSYNC_U * 4u)), (uint32_t)((LSYNC_ONE + LSYNC_U * 3u / 2u) / (LSYNC_U * 3u))};
/* the phase step this block of an LFO at RATE r (LFO's and LFO 2's) */
AINL uint32_t lfo_inc_of(int32_t r)
{
    if (r >= 0)
        return LFO_INC[r & 127];
    return (uint32_t)(((uint64_t)clk_blk * LSYNC_KT[(uint32_t)(-r - 1) < LSYNC_N ? (uint32_t)(-r - 1) : 0u]) >> 16);
}
/* the phase step of t's LFO this block (track_lfo_tick; dual.c dual_lfo_draw: the same) */
AINL uint32_t lfo_inc(const track_t *t) { return lfo_inc_of(t->p[P_LRATE]); }

/* LFO DEST's slots (core.h LT_*), both LFOs. Two kinds of destination:
 *   PIT FLT SHP AMP  the voices' (track_render, every block): an LFO's slots summed per destination into dep[4] (AMT
 *                    units: 2 a depth of before; AMP: the depth), lfo_apply turns them into the block's offsets of the
 *                    pitch (1/4096 semitone), the cutoff and the shape (Q8) and a gain of the amplitude (Q15), the
 *                    same integer arithmetic as the four depths had (a project of before sounds the same, sample for
 *                    sample): (v dep 3) >> 8 is (v depth 3) >> 7. LFO 2's add to LFO's, its gain multiplies after
 *   the others       a value of the part moved for the block (lfo_over: pold_in / pold_out, around the mix of the
 *                    parts, both cores; LT_TAB below), from the LFOs' values of the block before
 * The macros (FELUCCA_MACROS: MOTION) move LFO's depths of PIT FLT SHP AMP as they did (macro.c MK_LFO: lfo_mac) */
typedef struct {
    int32_t m[3];                                       /* added to PIT (1/4096 semitone), FLT, SHP (Q8) */
    int32_t g1, g2;                                     /* the amplitude's gain from LFO, from LFO 2 (Q15; -1: none) */
} lfo_mod_t;
/* slots s[4] (DEST, AMT, DEST, AMT) -> dep[4] (PIT FLT SHP AMP: their AMTs summed) */
AINL void lfo_dep(const int16_t *s, int32_t *dep)
{
    uint32_t k;
    for (k = 0; k < 2u; k++)
        if (LT_CLASSIC(s[2u * k]))
            dep[s[2u * k] - LT_PIT] += s[2u * k + 1u];
}
/* an LFO at v (Q15: its POL, LFO's fade in) and u ((its wave + 1) / 2, 0..32767) on dep -> o's offsets, *g the gain
 * (AMP not 0: down from unity by u x AMT x 258, P_LD_AMP's of before; < 0 from the bottom) */
AINL void lfo_apply(const int32_t *dep, int32_t v, int32_t u, lfo_mod_t *o, int32_t *g)
{
    int32_t a = dep[3];
    o->m[0] += (v * dep[0] * 3) >> 8;
    o->m[1] += (v * dep[1]) >> 8;
    o->m[2] += (v * dep[2]) >> 8;
    if (a) {
        a = clamp(a, LFO_AMT_LO, LFO_AMT_HI);
        *g = 32767 - mulq15(a > 0 ? u : 32767 - u, a > 0 ? a * 258 : a > LFO_AMT_LO ? -a * 258 : 32767);
    }
}
/* slot k (0 A, 1 B) of part values p from first (P_L1DA, P_L2DA) acts on a value (lfo_over, not the voices') */
#define LT_OVER_AT(p, first, k) ((p)[(first) + 2 * (k)] >= LT_RES && (p)[(first) + 2 * (k) + 1])
#if FELUCCA_MACROS
static int16_t lfo_mac[NPART][4];                      /* LFO's depth of PIT FLT SHP AMP under a macro (AMT units) */
static uint16_t lfo_mac_m;                             /* bit 4 k + d: lfo_mac[k][d] stands for the slots' (macro.c) */
#endif

/* LFO 2 (FELUCCA_LFO2, core.h P_L2RATE .. P_L2AB): a second LFO a part, two slots as LFO's. Its state is the part's
 * (lfo2[], not in track_t: the drum track has none). Each block (track_render, either core): nothing at all while every
 * slot is OFF or at 0 (the phase waits); else the phase steps by RATE (free or synced, lfo_inc_of), S&H takes its next
 * value at each wrap from a hash of the last (no rng(): the same samples on either core, no draw order), the wave
 * (lfo_slot.h lfo_shape, LFO's five) and POL (UNI: (v + 1) / 2, SLOOP 2.6's) give v (Q15, kept for lfo_over), and its
 * slots of PIT FLT SHP AMP the block's offsets (lfo_apply). TRIG NOTE: a fresh phrase restarts it at phase 0
 * (trk_note_on, with LFO's); FREE: never. No divide, no 64-bit but the synced step (LFO's) */
#if FELUCCA_LFO2
#include "lfo_slot.h"
typedef struct {
    uint32_t ph;                                        /* the phase */
    uint32_t rnd;                                       /* S&H's value this cycle (the top 16 bits) */
    int32_t v;                                          /* its value of the last block (Q15, POL in; lfo_over) */
} lfo2_t;
static lfo2_t lfo2[NPART] = {{0, 0x80000000u, 0}, {0, 0x80000000u, 0}, {0, 0x80000000u, 0}};   /* (S&H at 0 till a wrap) */
_Static_assert(NPART == 3 && P_L2AA == P_L2DA + 1 && P_L2DB == P_L2DA + 2 && P_L2AB == P_L2DA + 3, "lfo2[]: one a part");
#define LFO2_ON(t) (((t)->p[P_L2DA] && (t)->p[P_L2AA]) || ((t)->p[P_L2DB] && (t)->p[P_L2AB]))   /* (else: nothing) */
/* part t's LFO 2 this block -> o (left as it is while it is off). In XIP flash, called through a pointer (FAR) by the
 * render, only while it acts: once a part a block, its code out of RAMTEXT (the ISR never runs while the flash is busy) */
static __attribute__((noinline)) void lfo2_tick(const track_t *t, lfo_mod_t *o)   /* (XIP: RAMTEXT is full) */
{
    const int16_t *p = t->p;
    lfo2_t *s = &lfo2[(uint32_t)(t - trk) % NPART];
    int32_t v, u, dep[4] = {0, 0, 0, 0};
    uint32_t old = s->ph;
    if (!LFO2_ON(t))
        return;
    s->ph += lfo_inc_of(p[P_L2RATE]);
    if (s->ph < old)
        s->rnd = lfo_rnd_of(s->rnd, (uint32_t)(t - trk));
    v = lfo_shape((uint32_t)p[P_L2WAVE], s->ph, s->rnd);
    u = (v + 32768) >> 1;                               /* 0..32767 */
    if (p[P_L2POL])
        v = u;                                          /* UNI: one way only, as far */
    s->v = v;
    lfo_dep(&p[P_L2DA], dep);
    lfo_apply(dep, v, u, o, &o->g2);                    /* (AMP, either POL: from u, SLOOP's) */
}
/* a fresh phrase on part k (trk_note_on): TRIG NOTE restarts it */
AINL void lfo2_retrig(const track_t *t)
{
    if (!t->p[P_L2TRIG])
        lfo2[(uint32_t)(t - trk) % NPART].ph = 0;
}
#else
#define LFO2_ON(t) ((void)(t), 0)
AINL void lfo2_tick(const track_t *t, lfo_mod_t *o) { (void)t, (void)o; }
#define lfo2_retrig(t) ((void)(t))
#endif
/* part t's LFO at lfo (Q15, its fade in) on the voices: its slots of PIT FLT SHP AMP (or a macro's depths) -> o */
AINL void lfo1_voices(const track_t *t, int32_t lfo, lfo_mod_t *o)
{
    int32_t dep[4] = {0, 0, 0, 0};
    lfo_dep(&t->p[P_L1DA], dep);
#if FELUCCA_MACROS
    if (lfo_mac_m) {
        uint32_t k = (uint32_t)(t - trk) % NPART, d;
        for (d = 0; d < 4u; d++)
            if ((lfo_mac_m >> (4u * k + d)) & 1u)
                dep[d] = lfo_mac[k][d];
    }
#endif
    lfo_apply(dep, lfo, (lfo + 32768) >> 1, o, &o->g1);
}

/* the other destinations (LT_RES ..): one entry each, how it acts (lfo_over). LK_P a part value (id; its TP range), LK_E
 * the engine's EDIT value id (its own range), LK_RES the engine's EDIT value named RES / RESO / Q, LK_XR the other LFO's
 * RATE (its free rates only: a synced one stays), LK_XA the other LFO's two AMTs (x 0..2). Phase 2 adds the FX sends and
 * the insert amounts here */
enum { LK_NONE, LK_P, LK_E, LK_RES, LK_XR, LK_XA, LK_INS };
static const uint8_t LT_TAB[LT_N][2] = {
    {LK_NONE, 0}, {LK_NONE, 0}, {LK_NONE, 0}, {LK_NONE, 0}, {LK_NONE, 0},   /* OFF PIT FLT SHP AMP: the voices' */
    {LK_RES, 0}, {LK_P, P_PAN}, {LK_P, P_LEVEL}, {LK_P, P_ATK}, {LK_P, P_DEC},
    {LK_E, 0}, {LK_E, 1}, {LK_E, 2}, {LK_E, 3}, {LK_E, 4}, {LK_E, 5}, {LK_E, 6}, {LK_E, 7},
    {LK_P, P_CHOR}, {LK_P, P_DLY}, {LK_P, P_REV}, {LK_INS, 0}, {LK_INS, 1},   /* (the sends, S1 S2's amounts) */
    {LK_XR, 0}, {LK_XA, 0}};
_Static_assert(LT_N <= 32, "a DEST in 5 bits (project.c l2_pack)");
#define LO_MAX 16u                                     /* the values moved in a block (12 slots, XAMT two each: more is
                                                        * left as authored) */
static struct {
    uint8_t n;
    uint8_t k[LO_MAX], id[LO_MAX];                     /* the part, the value */
    int16_t old[LO_MAX], put[LO_MAX];
} lfo_ov;
/* any part's slot acts on a value (lfo_over: pold_any) */
AINL uint32_t lfo_over_any(void)
{
    uint32_t k, a = 0;
    for (k = 0; k < NPART; k++) {
        const int16_t *p = trk[k].p;
        a |= (uint32_t)(LT_OVER_AT(p, P_L1DA, 0) || LT_OVER_AT(p, P_L1DA, 1));
#if FELUCCA_LFO2
        a |= (uint32_t)(LT_OVER_AT(p, P_L2DA, 0) || LT_OVER_AT(p, P_L2DA, 1));
#endif
    }
    return a;
}
/* part t's value id moved to v (its base kept for lfo_over_out) */
static void lo_put(track_t *t, uint32_t id, int32_t v)
{
    int16_t *q = &t->p[id];
    if (v == *q || lfo_ov.n >= LO_MAX)
        return;
    lfo_ov.k[lfo_ov.n] = (uint8_t)(t - trk), lfo_ov.id[lfo_ov.n] = (uint8_t)id;
    lfo_ov.old[lfo_ov.n] = *q;
    lfo_ov.put[lfo_ov.n++] = (int16_t)v;
    *q = (int16_t)v;
}
/* the index of engine e's EDIT value named RES / RESO / Q (8: none) */
static uint32_t lo_res(const engine_t *e)
{
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        const char *l = e->edit[i].label;
        if (l && ((l[0] == 'R' && l[1] == 'E' && l[2] == 'S') || (l[0] == 'Q' && !l[1])))
            return i;
    }
    return 8u;
}
/* the value LT_RES .. LT_INS2 (DEST d) of part t moves: its id into *q and its range (an engine's EDIT value its own), 0
 * for another destination (or no RES value on this engine) */
static const param_desc_t *lo_dest(const track_t *t, int32_t d, uint32_t *q)
{
    int32_t i;
    switch (LT_TAB[d % LT_N][0]) {
    case LK_P:
        *q = LT_TAB[d][1];
        return &TP[*q];
    case LK_RES:
    case LK_E:
        i = LT_TAB[d][0] == LK_RES ? (int32_t)lo_res(ENGINES[t->engine % NENGINES]) : LT_TAB[d][1];
        if (i >= 8)
            return 0;
        *q = P_E0 + (uint32_t)i;
        return &ENGINES[t->engine % NENGINES]->edit[i];
    case LK_INS:                                        /* (S1 / S2: the amount of the type it holds; empty: none) */
        *q = ins_amt((uint32_t)(t - trk), LT_TAB[d][1]);
        return *q < P_COUNT ? &TP[*q] : 0;
    default:
        return 0;
    }
}
/* a value at base (range ds) moved by an LFO at v (Q15) x AMT a: v x a over half the range at full (no divide),
 * clamped (op_effval.c: the same, for the bar) */
static int32_t lo_move(int32_t base, const param_desc_t *ds, int32_t a, int32_t v)
{
    return clamp(base + ((((v * a) >> 7) * (ds->max - ds->min)) >> 16), ds->min, ds->max);   /* ((v a) >> 7: +-1) */
}
/* part t's slot (DEST d, AMT a) of an LFO at v (Q15) on its value. xr / xa: the other LFO's RATE id, its first slot
 * (P_COUNT: none) */
static void lo_slot(track_t *t, int32_t d, int32_t a, int32_t v, uint32_t xr, uint32_t xa)
{
    const param_desc_t *ds;
    uint32_t q;
    int32_t m = (v * a) >> 7, i;                        /* -32768..32767: +-1 */
    ds = lo_dest(t, d, &q);
    if (ds) {
        if (ds->max > ds->min)
            lo_put(t, q, lo_move(t->p[q], ds, a, v));
        return;
    }
    switch (LT_TAB[d % LT_N][0]) {
    case LK_XR:
        if (xr >= P_COUNT || t->p[xr] < 0)
            return;                                     /* (a synced RATE stays its note value) */
        lo_put(t, xr, clamp(t->p[xr] + ((m * 127) >> 16), 0, 127));
        return;
    case LK_XA:
        for (i = 0; xa < P_COUNT && i < 2; i++)
            lo_put(t, xa + 2u * (uint32_t)i + 1u, clamp((t->p[xa + 2u * (uint32_t)i + 1u] * (32768 + m)) >> 15, LFO_AMT_LO, LFO_AMT_HI));
        return;
    default:
        return;
    }
}
/* the mix's start (pold_in): every part's slots on a value, from the LFOs' values of the block before */
static void lfo_over_in(void)
{
    uint32_t k, s;
    lfo_ov.n = 0;
    for (k = 0; k < NPART; k++) {
        track_t *t = &trk[k];
        int32_t v1 = mulq15(t->lfo_val, t->lfo_fade);
        for (s = 0; s < 2u; s++)
            if (LT_OVER_AT(t->p, P_L1DA, s))
#if FELUCCA_LFO2
                lo_slot(t, t->p[P_L1DA + 2u * s], t->p[P_L1AA + 2u * s], v1, P_L2RATE, P_L2DA);
#else
                lo_slot(t, t->p[P_L1DA + 2u * s], t->p[P_L1AA + 2u * s], v1, P_COUNT, P_COUNT);
#endif
#if FELUCCA_LFO2
        for (s = 0; s < 2u; s++)
            if (LT_OVER_AT(t->p, P_L2DA, s))
                lo_slot(t, t->p[P_L2DA + 2u * s], t->p[P_L2AA + 2u * s], lfo2[k].v, P_LRATE, P_L1DA);
#endif
    }
}
/* the mix's end (pold_out): the bases back (a value written meanwhile keeps that value) */
static void lfo_over_out(void)
{
    uint32_t i = lfo_ov.n;
    while (i--)
        if (trk[lfo_ov.k[i]].p[lfo_ov.id[i]] == lfo_ov.put[i])
            trk[lfo_ov.k[i]].p[lfo_ov.id[i]] = lfo_ov.old[i];
    lfo_ov.n = 0;
}

static HOT void track_lfo_tick(track_t *t)
{
    uint32_t old = t->lfo_ph;
    t->lfo_ph += lfo_inc(t);
    if (t->lfo_ph < old)
#if FELUCCA_DUAL >= 2
        t->lfo_rnd = dual_lfo_rnd[t - trk];
#else
        t->lfo_rnd = rng();
#endif
    t->lfo_val = lfo_wave(t, t->lfo_ph);
    if (t->lfo_fade < 32767) {
        int32_t step = (int32_t)(ENV_LIN[t->p[P_LFADE] & 127] >> 9);
        t->lfo_fade = t->p[P_LFADE] ? clamp(t->lfo_fade + (step ? step : 1), 0, 32767) : 32767;
    }
}

/* voices the engine may use (POLY and UNISON): its cap, else all of them */
static uint32_t trk_nvoice(const track_t *t)
{
    uint32_t c = ENGINES[t->engine]->poly;
    return c && c < NPOLY ? c : NPOLY;
}

/* ------------------------------------------------- the shared voice budget --- */
AINL uint32_t voices_busy(void)                       /* sounding voices of all parts (not the fading ones) */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
    return n;
}

static uint32_t lowest_held(const track_t *t)           /* index of the lowest held note (the bass), NVOICE = none */
{
    uint32_t i, low = NVOICE;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && (low == NVOICE || t->v[i].note < t->v[low].note))
            low = i;
    return low;
}

/* overload (SLOOP 2.3, after Felucca 1.0; audio.c asks for it when two halves in a row took > 85 % of their
 * time): fade one voice out over the next block (voice_kill: no click), one more each half while it lasts.
 * First the quietest releasing voice; else the oldest held one that is neither a POLY part's lowest note (the
 * bass) nor a MONO / LEGATO / UNISON part's lead (voice 0): a dense chord on a heavy engine thins out from the
 * top, bass and lead stay. (Before: the oldest held voice went into its release, any of them.) */
static uint32_t shed_count;
static void voice_kill(voice_t *v);

static __attribute__((noinline)) void shed_voice(void)   /* (cold: only when overloaded) */
{
    uint32_t p, i;
    voice_t *best = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && !v->gate && v->stage != 4u && (!best || v->env < best->env))
                best = v;
        }
    if (!best)
        for (p = 0; p < NPART; p++) {
            const track_t *t = &trk[p];
            uint32_t mode = (uint32_t)t->p[P_VOICE], low = mode == V_POLY ? lowest_held(t) : 0u;
            for (i = 0; i < NVOICE; i++) {
                voice_t *v = &trk[p].v[i];
                if (v->active && v->gate && v->stage != 4u && (mode == V_POLY ? i != low : i > 0u) &&
                    (!best || v->age < best->age))
                    best = v;
            }
        }
    if (best) {
        voice_kill(best);
        shed_count++;
    }
}

/* the voice to give up for a new one (see the top); soft: only released voices and
 * extra UNISON voices of other parts. Returns its index, *pp its part; NVOICE = none */
static uint32_t voice_victim(const track_t *self, int soft, track_t **pp)
{
    uint32_t p, i, best = NVOICE, cat = 4;
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        uint32_t mode = (uint32_t)t->p[P_VOICE], nu = mode == V_UNISON ? trk_nvoice(t) : 1u;
        uint32_t low = mode == V_POLY ? lowest_held(t) : NVOICE;
        if (soft && t == self)
            continue;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            uint32_t c;
            if (!v->active || v->stage == 4u)
                continue;
            if (!v->gate)
                c = 1;                                  /* released */
            else if (mode == V_UNISON && i > 0 && i < nu)
                c = 2;                                  /* an extra UNISON voice */
            else if (!soft && mode == V_POLY && i != low)
                c = 3;                                  /* held, not the bass */
            else
                continue;
            if (c < cat || (c == cat && v->age < (*pp)->v[best].age)) {
                cat = c;
                best = i;
                *pp = t;
            }
        }
    }
    return best;
}

static uint32_t voice_kills;                            /* voices given up (budget, overload): console, hostsim */
#define KILL_BLOCKS 2u                                  /* 1.5 ms: no click on a bass, little extra load */
static void voice_kill(voice_t *v)                      /* fade out over the next blocks (env_tick stage 4) */
{
    v->gate = 0;
    if (v->stage != 4u)
        v->kill = KILL_BLOCKS;
    v->stage = 4;
    voice_kills++;
}

/* the budget is full: free a voice for part t. 0 = nothing to take (soft) */
static int voice_room(track_t *t, int soft)
{
    track_t *vp = 0;
    uint32_t k;
    if (voices_busy() < NVOICE)
        return 1;
    k = voice_victim(t, soft, &vp);
    if (k == NVOICE)
        return !soft;                                   /* hard: over the budget (cannot happen with 3 parts) */
    voice_kill(&vp->v[k]);
    return 1;
}

/* POLY allocation (within the engine's voice cap). Same note: reuse its voice. Else a free voice: ROTATE takes
 * the next one round-robin (release tails ring out), REUSE (or any GLIDE) takes
 * the free voice whose last pitch is closest, so poly portamento moves each
 * voice the shortest way. Steal: the oldest released voice, else the oldest
 * held one, never the lowest held note (the bass). A free voice needs room in the
 * shared budget: when the voice to give up is one of this part's, it is restarted
 * in place instead. */
static voice_t *voice_alloc(track_t *t, uint32_t note)
{
    const engine_t *e = ENGINES[t->engine];
    uint32_t i, np = trk_nvoice(t), best = np, low = np, nfree = 0;
    int32_t bd = 0x7FFFFFFF;
    if (e->alloc) {                                     /* the engine's own choice (FM6: Dexed's) */
        voice_t *v = &t->v[e->alloc(t, note) % np];
        if (!v->active || v->stage == 4u)
            voice_room(t, 0);
        return v;
    }
    for (i = 0; i < np; i++) {
        if (t->v[i].active && t->v[i].note == note)
            return &t->v[i];
        nfree += !t->v[i].active;
    }
    if (nfree && voices_busy() >= NVOICE) {
        track_t *vp = 0;
        uint32_t k = voice_victim(t, 0, &vp);
        if (k < np && vp == t)
            return &t->v[k];                            /* our own: restart it in place (no click) */
        if (k < NVOICE)
            voice_kill(&vp->v[k]);
    }
    if (t->p[P_ALLOC] || t->p[P_GLIDE]) {
        for (i = 0; i < np; i++) {
            int32_t d = t->v[i].pitch_cur - (int32_t)note * 16;
            if (t->v[i].active)
                continue;
            d = d < 0 ? -d : d;
            if (!t->v[i].pitch_cur)
                d = 0x7FFFFFF0;                          /* never played: last choice */
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
    } else {
        for (i = 0; i < np && best == np; i++) {
            uint32_t k = (t->rr + i) % np;
            if (!t->v[k].active)
                best = k;
        }
        t->rr = (uint8_t)((best == np ? t->rr : best) + 1u) % np;
    }
    if (best < np)
        return &t->v[best];
    for (i = 0; i < np; i++)                             /* the lowest held note is protected */
        if (t->v[i].gate && (low == np || t->v[i].note < t->v[low].note))
            low = i;
    for (i = 0; i < np; i++)                             /* oldest released (not one fading out for another part) */
        if (!t->v[i].gate && t->v[i].stage != 4u && (best == np || t->v[i].age < t->v[best].age))
            best = i;
    if (best == np)
        for (i = 0; i < np; i++)                         /* oldest held, not the bass */
            if (i != low && (best == np || t->v[i].age < t->v[best].age))
                best = i;
    return &t->v[best == np ? 0 : best];
}

/* glide, in 1/4096 semitone a control tick (pitch_cur . pitch_frac): RATE = GLIDE is the time an octave
 * takes (any interval at that speed), TIME = GLIDE is the time of any glide. A sequencer SLIDE with
 * GLIDE 0 glides in SLIDE_T (TIME), as an acid slide */
#define SLIDE_T 54                                       /* TIME_MS index: ~50 ms */
static int32_t glide_ticks(int32_t g)                    /* control ticks of TIME_MS index g */
{
    int32_t k = (int32_t)ENV_LIN[g & 127];
    return k ? (int32_t)((1u << 24) / (uint32_t)k) : 1;
}
static void glide_set(track_t *t, voice_t *v, int glide)
{
    int32_t d, ticks;
    if (!glide || !v->pitch_cur) {
        v->pitch_cur = v->pitch16;
        v->pitch_frac = 0;
        v->gstep = 0;
        return;
    }
    if (t->p[P_GLMODE] || !t->p[P_GLIDE]) {
        d = (v->pitch16 << 8) - ((v->pitch_cur << 8) + v->pitch_frac);
        d = d < 0 ? -d : d;
        ticks = glide_ticks(t->p[P_GLIDE] ? t->p[P_GLIDE] : SLIDE_T);
        v->gstep = d / ticks > 0 ? d / ticks : 1;
    } else {
        v->gstep = 0;
    }
}

static void voice_start(track_t *t, voice_t *v, uint32_t note, uint32_t vel, int glide)
{
    const engine_t *e = ENGINES[t->engine];
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s0 = v->s[0], s1 = v->s[1], s4 = v->s[4], s5 = v->s[5], s6 = v->s[6], s7 = v->s[7];
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = (int32_t)note * 16;
    glide_set(t, v, glide);
    v->penv = 32767;                                    /* the pitch envelope starts over (not on legato) */
    v->ofs = 0;
    if (!sounding) {
        v->env = 0;
        v->env_out = 0;
        v->ofs = ev_ofs;                                /* a sequenced note inside the block: from its sample */
    }                                                   /* sounding: the attack starts from the current level */
    e->note_on(t, v);
    if (sounding && !eng_sampled(e)) {                 /* retrigger / steal: keep phases and filter states */
        v->ph[0] = ph0;                                 /* (resetting them clicks) */
        v->ph[1] = ph1;
        v->ph[2] = ph2;
#if FELUCCA_ANALOG2
        if (ENG_IS(e, ANALOG)) {                         /* ANALOG 2 (SUPER's states too): the filter, its */
            v->s[0] = s0;                               /* second stage, the last block's cutoff */
            v->s[1] = s1;
            v->s[4] = s4;
            v->s[5] = s5;
            v->s[6] = s6;
        } else if (ENG_IS(e, DIGITAL)) {
#else
        if (ENG_IS(e, ANALOG)) {
            v->s[0] = s0;
            v->s[1] = s1;
        } else if (ENG_IS(e, DIGITAL)) {
#endif
            v->s[5] = s5;
            v->s[6] = s6;
            v->s[7] = s7;                               /* op 4 phase; the modulator envelope restarts */
        } else if (ENG_IS(e, LOFI)) {
            v->s[0] = s0;
            v->s[4] = s4;
        } else if (ENG_IS(e, TRIO)) {
            v->s[0] = s0;                               /* filter */
            v->s[1] = s1;
            v->s[4] = s4;                               /* the sample waiting for its step corrections */
#if !FELUCCA_ANALOG2
        } else if (ENG_IS(e, SUPER)) {
            v->s[0] = s0;                               /* filter, its second stage, the sub phase */
            v->s[1] = s1;
            v->s[4] = s4;
            v->s[5] = s5;
            v->s[6] = s6;
#endif
        } else if (ENG_IS(e, PHASE)) {
            v->s[0] = s0;                               /* the WAVE / WAVE2 toggles go with the phases kept */
            v->s[1] = s1;
        }
    }
}

/* MONO / LEGATO / UNISON: one note on one voice (eight for UNISON, or the
 * engine's voice cap, spread by DETUNE over the same width) */
static void mono_play(track_t *t, uint32_t note, uint32_t vel, int retrig, int glide)
{
    uint32_t mode = (uint32_t)t->p[P_VOICE], nv = mode == V_UNISON ? CG_UNI(trk_nvoice(t)) : 1u, i;   /* (CPU guard) */
    for (i = 0; i < nv; i++) {
        voice_t *v = &t->v[i];
        int32_t k = 2 * (int32_t)i - (int32_t)(nv - 1u);   /* -7 .. 7 */
        if (nv > 1u && nv < NVOICE)
            k = k * (NVOICE - 1) / (int32_t)(nv - 1u);      /* fewer voices: the outer ones as wide */
        v->fine = nv > 1u ? k * t->p[P_DETUNE] * 56 / 889 : 0;   /* up to ~±40 cents */
        if (retrig || !v->active || !v->gate) {
            int was_sounding = v->active && v->stage != 0;
            if ((!v->active || v->stage == 4u) && !voice_room(t, i > 0))
                continue;                                   /* no room for this extra UNISON voice */
            /* level: about the same sum for 8 or 4 voices at random phases */
            voice_start(t, v, note, nv >= NVOICE ? vel * 36u / 100u : nv > 1u ? vel / 2u : vel, glide);
            if (nv > 1u && i && !eng_sampled(ENGINES[t->engine]) && !was_sounding) {   /* random start phases: */
                static uint32_t seed = 0x1234567u;          /* in phase they stack, evenly spread they cancel */
                seed = seed * 1664525u + 1013904223u;
                v->ph[0] += seed;
            }
        } else {                                            /* legato: new pitch, same envelope */
            v->note = (uint8_t)note;
            v->pitch16 = (int32_t)note * 16;
            if (vel > v->vel && nv == 1u)
                v->vel = (uint8_t)vel;
            glide_set(t, v, glide);
            if (ENGINES[t->engine]->legato)
                ENGINES[t->engine]->legato(t, v);
        }
    }
    t->mono_note = (uint8_t)note;
}

/* the note the mono stack asks for (PRIO: LAST / LOW / HIGH), 0 = none */
static uint32_t mono_pick(const track_t *t)
{
    uint32_t i, n;
    if (!t->nmono)
        return 0;
    n = t->mono_stack[t->nmono - 1u];
    for (i = 0; i < t->nmono; i++) {
        if (t->p[P_PRIO] == 1 && t->mono_stack[i] < n)
            n = t->mono_stack[i];
        if (t->p[P_PRIO] == 2 && t->mono_stack[i] > n)
            n = t->mono_stack[i];
    }
    return n;
}

static void mono_remove(track_t *t, uint32_t note)
{
    uint32_t j, k = 0;
    for (j = 0; j < t->nmono; j++)
        if (t->mono_stack[j] != note)
            t->mono_stack[k++] = t->mono_stack[j];
    t->nmono = (uint8_t)k;
}

#if FELUCCA_LIGHTS && FELUCCA_KEYLIT
/* the notes each synth part started since the UI last looked (menu NOTES: a short note lights its key for a few
 * frames, as a drum hit does; ui_studio.c pads_tick). SLOOP 2.4 (isod89/sloop-fm1 8d3823f voice.c note_hits).
 * Set here (mostly the audio ISR), taken by the UI with the IRQ off */
static volatile uint32_t note_hits[NPART][4];
#endif
#if FELUCCA_VIS
/* the visualiser (ui_vis.c): the tracks a note started on (bit per track), each one's last note, a kick */
static volatile uint8_t vis_hit, vis_kick_hit, vis_note[NTRK];
#endif

static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t any = 0, i, mode = (uint32_t)t->p[P_VOICE];
    if (trk_silent(t))
        return;                                         /* MUTE, or another track soloed */
#if FELUCCA_VIS
    vis_hit |= (uint8_t)(1u << ((uint32_t)(t - trk) & 3u));
    vis_note[(uint32_t)(t - trk) & 3u] = (uint8_t)note;
    if (is_drum(t) && (note == 35u || note == 36u))
        vis_kick_hit = 1;
#endif
    if (is_drum(t)) {                                   /* the drum track: GM drums (drums.c) */
        drum_on(note, vel);
        return;
    }
#if FELUCCA_LIGHTS && FELUCCA_KEYLIT
    if ((uint32_t)(t - trk) < NPART && note < 128u)
        note_hits[t - trk][note >> 5] |= 1u << (note & 31u);
#endif
    if (t->xf_on || t->eng_req != t->engine || t->pold) {   /* engine switch / sound load under way: after the fade */
        for (i = 0; i < t->xp_n && t->xp_note[i] != note; i++)
            ;
        if (i == t->xp_n && t->xp_n < 4u)
            t->xp_n++;
        i = i < t->xp_n ? i : t->xp_n - 1u;             /* (full: the last one is replaced) */
        t->xp_note[i] = (uint8_t)note;
        t->xp_vel[i] = (uint8_t)vel;
        return;
    }
    for (i = 0; i < NVOICE; i++)
        any |= t->v[i].gate;
    if (!any) {                                        /* fresh phrase: LFO retrigger and fade */
        t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
        t->lfo_fade = 0;
        lfo2_retrig(t);                                 /* (LFO 2: TRIG NOTE) */
    }
    if (mode == V_POLY) {
        voice_t *v = voice_alloc(t, note);
        t->nmono = 0;                                   /* no stale mono stack after a mode change */
        t->mono_note = 0;
        v->fine = 0;
        voice_start(t, v, note, vel, t->p[P_GLIDE] != 0);
        return;
    }
    {
        uint32_t sounding = t->mono_note && t->v[0].active && t->v[0].gate, want;
        mono_remove(t, note);
        if (t->nmono >= 8u)
            mono_remove(t, t->mono_stack[0]);
        t->mono_stack[t->nmono++] = (uint8_t)note;
        if (ENGINES[t->engine]->mono_key)
            ENGINES[t->engine]->mono_key(t, note);
        want = mono_pick(t);
        if (sounding && want == t->mono_note)
            return;                                     /* LOW / HIGH priority: this key does not win */
        if (mode == V_MONO)                             /* MONO: always retrigger, glide when GLIDE is set */
            mono_play(t, want, vel, 1, t->p[P_GLIDE] != 0 || t->slide_glide);
        else                                            /* LEGATO / UNISON: glide only between held notes */
            mono_play(t, want, vel, !sounding, sounding && (t->p[P_GLIDE] != 0 || t->slide_glide));
    }
}

#if FELUCCA_CHORDPLUS
/* STRUM (SLOOP 2.4, isod89/sloop-fm1 v2.4 8d3823f voice.c, GPL-3.0-only; P_STRUM, SCL 2): a chord's notes start one
 * after the other, |P_STRUM| ms apart (> 0 from the lowest up, as a guitar's down stroke; < 0 from the highest
 * down). The later ones wait here, in samples (strum_block, each audio block: the end of events_block); a note-off
 * before its start cancels it, a track's all-off (STOP) clears them. MIDI out is not delayed (the callers send
 * the chord's notes together, unstrummed) */
#define STQ 16u
static struct { uint8_t on, trk, note, vel; uint32_t left; } stq[STQ];
static void strum_cancel(const track_t *t, uint32_t note, int all)
{
    uint32_t i;
    for (i = 0; i < STQ; i++)
        if (stq[i].on && stq[i].trk == (uint8_t)(t - trk) && (all || stq[i].note == note))
            stq[i].on = 0;
}
#endif
static void trk_note_off(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, mode = (uint32_t)t->p[P_VOICE];
    if (is_drum(t))
        return;                                         /* one-shots */
#if FELUCCA_CHORDPLUS
    strum_cancel(t, note, 0);
#endif
    for (i = 0; i < t->xp_n; i++)                       /* not sounding yet (engine switch): forget it */
        if (t->xp_note[i] != note) {
            t->xp_note[k] = t->xp_note[i];
            t->xp_vel[k++] = t->xp_vel[i];
        }
    t->xp_n = (uint8_t)k;
    mono_remove(t, note);                               /* in any mode: a key let go after a VOICE change must not
                                                         * stay in the MONO stack (SLOOP 2.3 d691ba7, unchanged in 2.6;
                                                         * Felucca 1.0, by isod89 and Leo Kuroshita, GPL-3.0) */
    if (mode != V_POLY) {
        uint32_t nv = mode == V_UNISON ? trk_nvoice(t) : 1u;
        if (t->mono_note == note) {
            uint32_t next = mono_pick(t);
            if (next) {                                 /* fall back to a held note, legato */
                mono_play(t, next, t->v[0].vel, 0, t->p[P_GLIDE] != 0);
            } else {
                for (i = 0; i < nv; i++)
                    if (t->v[i].gate) {
                        t->v[i].gate = 0;
                        t->v[i].stage = 3;
                    }
                t->mono_note = 0;
            }
        }
    }
    for (i = 0; i < NVOICE; i++)                        /* POLY voices (also ones left from another mode: */
        if (t->v[i].note == note && t->v[i].gate &&     /* VOICE changed while they were held) */
            (mode == V_POLY || i >= (mode == V_UNISON ? trk_nvoice(t) : 1u) || t->mono_note != note)) {
            t->v[i].gate = 0;
            t->v[i].stage = 3;
        }
}

static void trk_all_off(track_t *t)
{
    uint32_t i;
#if FELUCCA_CHORDPLUS
    strum_cancel(t, 0, 1);
#endif
    for (i = 0; i < NVOICE; i++) {
        t->v[i].gate = 0;
        t->v[i].stage = t->v[i].active ? 3 : 0;
    }
    t->nmono = 0;
    t->mono_note = 0;
    t->xp_n = 0;
}

#if FELUCCA_CHORDPLUS
/* note i of a chord of n (notes[]): at once, or strummed (its rank: its place from the lowest, or the highest) */
static void trk_note_chord(track_t *t, const uint8_t *notes, uint32_t n, uint32_t i, uint32_t vel)
{
    int32_t s = t->p[P_STRUM];
    uint32_t rank = 0, j, q;
    if (!s || n < 2u || is_drum(t)) {
        trk_note_on(t, notes[i], vel);
        return;
    }
    for (j = 0; j < n; j++)                             /* its place: lowest first (down), highest first (up) */
        if (j != i && (s > 0 ? notes[j] < notes[i] || (notes[j] == notes[i] && j < i)
                             : notes[j] > notes[i] || (notes[j] == notes[i] && j < i)))
            rank++;
    if (!rank) {
        trk_note_on(t, notes[i], vel);
        return;
    }
    strum_cancel(t, notes[i], 0);
    for (q = 0; q < STQ && stq[q].on; q++)
        ;
    if (q == STQ) {                                     /* (full: at once) */
        trk_note_on(t, notes[i], vel);
        return;
    }
    stq[q].trk = (uint8_t)(t - trk);
    stq[q].note = (uint8_t)notes[i];
    stq[q].vel = (uint8_t)vel;
    stq[q].left = rank * (uint32_t)(s < 0 ? -s : s) * (uint32_t)FS / 1000u;
    stq[q].on = 1;
}
/* each audio block (events_block's end): the strummed notes due */
static void strum_block(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < STQ; i++)
        if (stq[i].on) {
            if (stq[i].left <= n) {
                stq[i].on = 0;
                trk_note_on(&trk[stq[i].trk % NTRK], stq[i].note, stq[i].vel);
            } else {
                stq[i].left -= n;
            }
        }
}
#else
#define trk_note_chord(t, notes, n, i, vel) trk_note_on(t, (notes)[i], vel)
#endif

/* engine switch, at each block start (events_block), before any note of the block. The UI writes
 * eng_req and the new engine's P_E0..P_E7 together (IRQ off). The part's sounding voices (released by
 * the preset change) then fade out over XF_BLOCKS blocks on the old engine, rendered with its own
 * parameters (pe_old: an engine never reads another engine's values, which index its tables); only
 * then the engine switches and the notes that came during the fade start on it. A part with nothing
 * sounding switches at once. */
#define XF_BLOCKS 8u                                    /* 8 x 32 samples: 5.8 ms (SLOOP 2.6, as Clipper: a bass
                                                         * fades without a thump; 2.9 ms before) */
/* SLOOP 2.6 (isod89/sloop-fm1 v2.6 d30d655 voice.c sound_change, GPL-3.0-only): a sound (a factory or user preset, a
 * native bank tone, an engine, NEW) about to load on part t: the sound as it is now kept for the fade (main loop,
 * before the new values are written). The voices sounding then fade out over XF_BLOCKS rendered with these values,
 * the part's DIST, FX slots, level and sends included (fx.c mix_part swaps them in), as for an engine switch: no jump
 * of a filter, a drive or a release in the middle of a note (a click). A fade under way keeps the sound it started
 * from. Not on a part that plays FM6: its patch lives in a buffer of its own (fm6_ed, swapped by fm6_sync on VOICE),
 * which the fade cannot keep; its notes stop with Dexed's one-block fade, as before */
static int16_t p_old[NPART][P_COUNT];
static void sound_change(track_t *t)
{
    uint32_t k = (uint32_t)(t - trk);
    if (k >= NPART || t->pold || ENG_IS(ENGINES[t->engine % NENGINES], FM6))
        return;
    memcpy(p_old[k], t->p, sizeof p_old[k]);           /* (the ISR reads p_old only once pold is set) */
    __asm__ volatile("" ::: "memory");
    t->pold = 1;
}
/* the sound before the load <-> part k's: an exchange (twice: as it was) */
static void pold_swap(uint32_t k)
{
    int16_t *a = trk[k].p, *b = p_old[k];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) {
        int16_t x = a[i];
        a[i] = b[i];
        b[i] = x;
    }
}
/* the mix of the parts (fx.c mix_block, dual.c mix_block_dual), a block of a fade (in flash: 8 blocks a load):
 * pold_in swaps each part fading after a sound load to its old sound and returns their mask; pold_out(mask) swaps
 * them back after the parts are mixed (track_render counts the fade down meanwhile: the mask, not the state) */
/* They also bracket the LFO slots on a part's values (lfo_over_in / lfo_over_out: bit 31 of the mask): the same two
 * calls on both cores' mix, the values moved for the parts' render and their mix (PAN, LVL), back before the main loop */
#define POLD_LFO 0x80000000u
AINL uint32_t pold_any(void)
{
    uint32_t k, a = 0;
    for (k = 0; k < NPART; k++)
        a |= trk[k].pold;
    return a | lfo_over_any();
}
static __attribute__((noinline)) uint32_t pold_in(void)
{
    uint32_t k, m = 0;
    for (k = 0; k < NPART; k++)
        if (trk[k].pold && trk[k].xf_on && trk[k].xf) {
            pold_swap(k);
            m |= 1u << k;
        }
    if (lfo_over_any()) {
        lfo_over_in();                                  /* (over the sound it plays: the old one while it fades) */
        m |= POLD_LFO;
    }
    return m;
}
static __attribute__((noinline)) void pold_out(uint32_t m)
{
    uint32_t k;
    if (m & POLD_LFO)
        lfo_over_out();
    for (k = 0; k < NPART; k++)
        if ((m >> k) & 1u)
            pold_swap(k);
}
static void engine_block(track_t *t)
{
    uint32_t i, any = 0;
    if (t->xf_on || t->eng_req != t->engine || t->pold) {
        if (!t->xf_on) {
            for (i = 0; i < NVOICE; i++)
                any |= t->v[i].active;
            if (any) {
                t->xf_on = 1;
                t->xf = XF_BLOCKS;
                return;
            }
        } else if (t->xf) {
            return;                                     /* still fading (track_render counts down) */
        }
        for (i = 0; i < NVOICE; i++) {                  /* faded to 0: gone */
            voice_t *v = &t->v[i];
            v->active = v->gate = 0;
            v->stage = 0;
            v->env = v->env_out = 0;
        }
        t->engine = t->eng_req % NENGINES;
        part_mem_claim((uint32_t)(t - trk), t->engine);   /* its engine memory (engines.c) */
        t->xf_on = 0;
        t->pold = 0;
        t->nmono = 0;
        t->mono_note = 0;
        {
            uint32_t n = t->xp_n;
            t->xp_n = 0;
            for (i = 0; i < n; i++)
                trk_note_on(t, t->xp_note[i], t->xp_vel[i]);
        }
    }
    for (i = 0; i < 8u; i++)                            /* the engine's own values, for a later fade */
        t->pe_old[i] = t->p[P_E0 + i];
}

/* one control tick (CTL samples) of the amplitude envelope; returns Q15 */
static HOT int32_t env_tick(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    int32_t sus = (int32_t)p[P_SUS] << 17;              /* Q24 */
    switch (v->stage) {
    case 1:
        v->env += (int32_t)ENV_LIN[p[P_ATK] & 127];
        if (v->env >= (1 << 24)) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += mulq16(sus - v->env, ENV_EXP[p[P_DEC] & 127]);
        break;
    case 3:
        v->env -= mulq16(v->env, ENV_EXP[p[P_REL] & 127]);
        if (v->env < (1 << 12)) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    case 4:                                             /* given up for another part: faded out over KILL_BLOCKS */
        if (v->kill > 1u) {
            v->env -= v->env / v->kill;
            v->kill--;
        } else {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
            v->kill = 0;
        }
        break;
    default:
        v->env = 0;
        break;
    }
    return v->env >> 9;
}

/* MIDI bend and mod wheel (from Melodee 670193c): ~3 ms smoothing with an exact end point */
AINL int32_t midi_slew(int32_t cur, int32_t target)
{
    int32_t d = target - cur;
    return cur + (d > 0 ? (d + 3) / 4 : -((-d + 3) / 4));
}

/* the part's live MIDI pitch, Q8 semitones: the bend + the wheel's vibrato (5 Hz, up to +-50 cents); HOT:
 * the mix (RAM) calls it */
static HOT int32_t __attribute__((noinline)) midi_pitch_tick(track_t *t, uint32_t n)
{
    int32_t pitch;
    if (!FELUCCA_MIDI_EXPR || !(t->bend_q8 | t->bend_target | t->wheel_q8 | t->wheel_target))
        return 0;                                       /* centred: the usual path stays cheap */
    t->bend_q8 = (int16_t)midi_slew(t->bend_q8, t->bend_target);
    t->wheel_q8 = (int16_t)midi_slew(t->wheel_q8, t->wheel_target);
    pitch = t->bend_q8;
    if (t->wheel_q8) {
        t->wheel_phase += 486958u * n;                  /* 5 Hz at 44.1 kHz */
        pitch += mulq15(osc_sine(t->wheel_phase), t->wheel_q8 / 254);
    }
    return pitch;
}

/* render one block of a part into out (cleared here); returns the voices rendered */
static HOT uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
    const engine_t *e = ENGINES[t->engine];
    const int16_t *p = t->p;
    uint32_t i;
    int32_t lfo = mulq15(t->lfo_val, t->lfo_fade);
    lfo_mod_t lm;                                       /* the LFOs on the voices this block (lfo1_voices, lfo2_tick) */
    int32_t midi = midi_pitch_tick(t, n) * 16;          /* MIDI bend + wheel, 1/4096 semitone */
    /* TUNE in cents: whole 1/16 semitones in the pitch, the rest as a fine factor (no dead zone) */
    int32_t tune = song.g[G_TUNE] >= 0 ? song.g[G_TUNE] * 16 / 100 : -((-song.g[G_TUNE] * 16 + 99) / 100);
    int32_t tune_fine = (song.g[G_TUNE] * 16 - tune * 100) * 2367 / 16000;   /* rest, in 1/4096 (1 ct = 2.367) */
    uint32_t nr = 0, fade = t->xf_on && t->xf;
    int16_t pe_new[8];
    for (i = 0; i < n; i++)
        out[i] = 0;
    if (fade)                                           /* engine switch: the old engine, its own values */
        for (i = 0; i < 8u; i++) {
            pe_new[i] = t->p[P_E0 + i];
            t->p[P_E0 + i] = t->pe_old[i];
        }
    lm.m[0] = lm.m[1] = lm.m[2] = 0, lm.g1 = lm.g2 = -1;   /* (field by field: no memcpy from XIP, RAM code) */
    lfo1_voices(t, lfo, &lm);
    track_lfo_tick(t);
    if (LFO2_ON(t))
        FAR(lfo2_tick)(t, &lm);
    if (e->block)                                      /* the engine's per-part work (DRAWBAR: bars, rotor) */
        e->block(t);
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        int32_t env, pitch;
        if (!v->active)
            continue;
        {
            env = env_tick(t, v);
            if (e->amp)                                 /* the engine's own amplitude curve */
                env = e->amp(t, v, env);
            m.envq15 = env;
            m.amp1 = e->vel_own ? env : mulq15(env, v->vel * 258);
            if (lm.g1 >= 0)
                m.amp1 = mulq15(m.amp1, lm.g1);
            if (lm.g2 >= 0)
                m.amp1 = mulq15(m.amp1, lm.g2);
            if (fade)                                   /* linear to 0 over the fade */
                m.amp1 = m.amp1 * (int32_t)(t->xf - 1u) / (int32_t)XF_BLOCKS;
            m.amp0 = v->env_out;
            v->env_out = m.amp1;
        }
        if (v->pitch_cur != v->pitch16 || v->pitch_frac) {   /* glide, in 1/4096 semitone */
            int32_t q = (v->pitch_cur << 8) + v->pitch_frac, d = (v->pitch16 << 8) - q;
            int32_t st = v->gstep ? v->gstep : (int32_t)((ENV_LIN[p[P_GLIDE] & 127] * 3u) >> 10);   /* RATE: an octave in GLIDE */
            if (st < 1)
                st = 1;
            q += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
            v->pitch_cur = q >> 8;
            v->pitch_frac = (uint8_t)(q & 255);
        }
        if (v->penv)                                    /* the pitch envelope: ~40 ms */
            v->penv = (int16_t)(v->penv - ((v->penv * 590 + 32767) >> 15));
        if (!env && !m.amp0 && v->stage == 2 && !eng_sampled(e))
            continue;                                   /* held at a silent sustain (SUS 0): nothing to render */
        {   /* the pitch in 1/4096 semitone: glide, LFO, the pitch envelope; the fraction goes into the increment */
            int32_t q = (v->pitch_cur << 8) + v->pitch_frac + ((v->penv * p[P_ED_PIT] * 3) >> 7) + lm.m[0] + midi;
            /* FM6's own pitch offset, without the MIDI bend / wheel: FM6 takes the controllers through
             * fm6_midi_expr (its DX7 bend range and wheel routing, as Melodee and Dexed) */
            m.plog = (int32_t)(((int64_t)(q - midi - (v->pitch16 << 8)) * 1398101) >> 12) + v->fine * 5909;   /* -> Q24 */
            pitch = (q >> 8) + tune;
            m.pitch16 = clamp(pitch, 0, 2047);
            m.inc = PITCH_INC[m.pitch16];
            q = (q & 255) * 3792 >> 16;                 /* the fraction, as fine (1/16 st = 14.8) */
            m.fine = midi && pitch == m.pitch16 ? q : 0;   /* (engines from pitch16: the bend's fraction) */
            m.fine_all = v->fine + tune_fine + q;       /* unison detune, fine tune and the fraction */
            if (m.fine_all)
                m.inc = fine_inc(m.inc, m.fine_all);
        }
        m.cutoff = lm.m[1] + ((m.envq15 * p[P_ED_FLT]) >> 7);
        if (v->vel > 110)                               /* accent opens the filter with the env */
            m.cutoff += (m.envq15 * 24) >> 7;
        m.shape = (64 << 8) + lm.m[2] + ((m.envq15 * p[P_ED_SHP]) >> 7);
        if (v->ofs) {                                   /* starts inside the block: the rest of it, the */
            uint32_t o = v->ofs < n ? v->ofs : n - 1u;  /* amplitude ramp reaching amp1 at its end */
            v->ofs = 0;
            m.amp1 = m.amp0 + (m.amp1 - m.amp0) * (int32_t)CTL / (int32_t)(n - o);
            e->render(t, v, out + o, n - o, &m);
        } else {
            e->render(t, v, out, n, &m);
        }
        nr++;
    }
    if (e->post)                                        /* the part after its voices (FM6: Dexed's DC filter) */
        e->post(t, out, n, nr);
    if (fade) {
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = pe_new[i];
        t->xf--;
    }
    return nr;
}
