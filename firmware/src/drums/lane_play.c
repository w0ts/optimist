/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes at the hit (the user, 2026-10-12: D2, D6), included by seq.c after the automation store: what a hit of
 * lane l plays over the patch dl.ofs (drum_edit.c dl_ofs_play), computed once a hit by drum_on (dl_play_hit, the ISR):
 *   a lock      the step playing's (auto.c auto_step: dla.lv, dla.lm), wins
 *   a hold      the motion's last value (dla.hv, dla.hm), else the patch's
 *   the LFO     the lane's own (below), added on top
 * then clamped into the value's range (DE_MIN..DE_MAX). A lane with none of them: dl_play_hit leaves it alone and the hit
 * reads dl_ofs as before (no other work). XIP: none of it is in RAM code.
 *
 * The lanes' LFOs (D6, the user's option A: one LFO a lane, 16): RATE (P_LRATE's: 0..127 free, -1..-10 one cycle a note
 * value of the tempo, voice.c LSYNC_KT), WAVE (SIN TRI SAW SQR S&H), PHS (where its cycle is at PLAY), FADE (its fade in
 * from PLAY), then two destination slots (core/lfo_slot.h: DEST OFF or one of the 8 lane values TUNE .. LEVEL, AMT
 * -64..63; the user, 2026-10-12). An LFO whose slots are all OFF or at AMT 0 is off and costs nothing (dlfo_on). A live
 * one has no tick either: its value is figured at the lane's hit from the transport (D8: it restarts at PLAY, at PHS):
 * playing, from the blocks since PLAY (song.tick) or, synced, from the beats and units (clk_beat, clk_pos), the same
 * phase a per-block tick would reach; stopped, from the blocks counted since (dlfo_free, the only per-block work: one
 * increment while stopped with a live LFO). S&H draws its value from a hash of the cycle (lfo_rnd_of): no rng() at a hit.
 *
 * Kept with the project in a record of its own (D6: the FX record has no room; storage/dlfo_rec.c, its stored form
 * dlfo_encode below), so an older firmware ignores it and an older project reads with every LFO off. Every build keeps
 * them (dl's rule); a build without the drum editor (DLFO_ON 0) plays none, and MISS says LANE LFO. */
#include "../core/lfo_slot.h"

typedef struct {
    int8_t rate;                           /* -LSYNC_N..-1 synced (params.c N_LSYNC), 0..127 free (LFO_INC) */
    uint8_t wave;                          /* SIN TRI SAW SQR S&H (params.c N_LWAVE) */
    uint8_t phase;                         /* 0..127: its cycle's place at PLAY (x 1/128) */
    uint8_t fade;                          /* 0..127: its fade in from PLAY (ENV_LIN, as P_LFADE), 0 none */
    lfo_slot_t s[LFO_NSLOT];               /* dest 0 OFF, 1..DE_N the lane value DE_* + 1; amount -64..63 */
} dlfo_t;
_Static_assert(sizeof(dlfo_t) == 8u && LFO_NSLOT == 2u, "a lane's LFO: 8 bytes (rate wave phase fade, 2 x dest amount)");
#define DLFO_RATE_DEF 60                   /* (TP[P_LRATE].def: a synth LFO's) */
#define DLFO_DEF_INIT {DLFO_RATE_DEF, 0, 0, 0, {{0, 0}, {0, 0}}}
static const dlfo_t DLFO_DEF = DLFO_DEF_INIT;
static dlfo_t dlfo[DRUM_LANES] = {[0 ... DRUM_LANES - 1] = DLFO_DEF_INIT};   /* the working project's */
static uint32_t dlfo_free;                 /* blocks counted while stopped (events_block: DLFO_BLOCK) */

/* the fields as the UIs show them (one cell each), their ranges and names */
enum { DLF_RATE, DLF_WAVE, DLF_PHASE, DLF_FADE, DLF_DEST1, DLF_AMT1, DLF_DEST2, DLF_AMT2, DLF_N };
static const char *const N_DLDEST[DE_N + 1u] = {"OFF", "TUNE", "DECAY", "SNAP", "CLICK", "BEND", "CUT", "DRIVE", "LEVEL"};
static const param_desc_t DLFO_DESC[DLF_N] = {
    PD("RATE", F_LFOHZ, -LSYNC_N, 127, DLFO_RATE_DEF), PE("WAVE", N_LWAVE, 0), PD("PHS", F_INT, 0, 127, 0),
    PD("FADE", F_TIME, 0, 127, 0), PE("DEST", N_DLDEST, 0), PD("AMT", F_BIPCT, LFO_AMT_MIN, LFO_AMT_MAX, 0),
    PE("DEST2", N_DLDEST, 0), PD("AMT2", F_BIPCT, LFO_AMT_MIN, LFO_AMT_MAX, 0)};
_Static_assert(sizeof N_LWAVE / sizeof N_LWAVE[0] == LFO_WAVES, "the waves: lfo_slot.h's");

/* c inside its ranges */
static void dlfo_fix(dlfo_t *c)
{
    uint32_t i;
    c->rate = (int8_t)clamp(c->rate, -LSYNC_N, 127);
    c->wave = (uint8_t)(c->wave < LFO_WAVES ? c->wave : 0u);
    c->phase = (uint8_t)(c->phase & 127u);
    c->fade = (uint8_t)(c->fade & 127u);
    for (i = 0; i < LFO_NSLOT; i++)
        c->s[i] = lfo_slot_fix(c->s[i].dest, c->s[i].amt, DE_N);
}
static int dlfo_is_def(const dlfo_t *c) { return !memcmp(c, &DLFO_DEF, sizeof *c); }
/* lane l's LFO acts (a slot with a destination and an amount): computed at its hits */
static int dlfo_live(uint32_t l) { return l < DRUM_LANES && lfo_slots_on(dlfo[l].s); }
static int dlfo_any_live(void)
{
    uint32_t l;
    for (l = 0; l < DRUM_LANES; l++)
        if (dlfo_live(l))
            return 1;
    return 0;
}
/* the LFOs changed (the main loop with the IRQ off, a load): which lanes compute theirs */
static void dlfo_touch(void)
{
    uint32_t l, m = 0;
    for (l = 0; l < DRUM_LANES; l++)
        m |= (uint32_t)dlfo_live(l) << l;
    dlfo_on = (uint16_t)(DLFO_ON ? m : 0u);
#if DLA_ON || DLFO_ON
    dlp_l = 0xFFu;                         /* (the next hit computes again) */
#endif
}
/* ---- the UIs' calls (the main loop) */
static const dlfo_t *dlfo_get(uint32_t l) { return &dlfo[l % DRUM_LANES]; }
static void dlfo_set(uint32_t l, const dlfo_t *c)
{
    dlfo_t n = *c;
    dlfo_fix(&n);
    fm1_irq_off();
    dlfo[l % DRUM_LANES] = n;
    dlfo_touch();
    fm1_irq_on();
}
static int32_t dlfo_field(uint32_t l, uint32_t f)
{
    const dlfo_t *c = dlfo_get(l);
    switch (f) {
    case DLF_RATE: return c->rate;
    case DLF_WAVE: return c->wave;
    case DLF_PHASE: return c->phase;
    case DLF_FADE: return c->fade;
    case DLF_DEST1: case DLF_DEST2: return c->s[(f - DLF_DEST1) / 2u].dest;
    case DLF_AMT1: case DLF_AMT2: return c->s[(f - DLF_AMT1) / 2u].amt;
    default: return 0;
    }
}
static void dlfo_field_set(uint32_t l, uint32_t f, int32_t v)
{
    dlfo_t c = *dlfo_get(l);
    if (f >= DLF_N)
        return;
    v = clamp(v, DLFO_DESC[f].min, DLFO_DESC[f].max);
    switch (f) {
    case DLF_RATE: c.rate = (int8_t)v; break;
    case DLF_WAVE: c.wave = (uint8_t)v; break;
    case DLF_PHASE: c.phase = (uint8_t)v; break;
    case DLF_FADE: c.fade = (uint8_t)v; break;
    case DLF_DEST1: case DLF_DEST2: c.s[(f - DLF_DEST1) / 2u].dest = (uint8_t)v; break;
    default: c.s[(f - DLF_AMT1) / 2u].amt = (int8_t)v; break;
    }
    dlfo_set(l, &c);
}

/* ---- the stored form (the record: storage/dlfo_rec.c)
 *   byte 0      version, DLFO_VER (a newer version: refused whole, every LFO off)
 *   byte 1      the bytes a lane takes, DLFO_ESIZE (8; a newer build may add: a reader takes the first 8, skips the rest;
 *               fewer: the rest at their defaults)
 *   byte 2, 3   the lanes stored, bit l (little endian): those not at the defaults
 *   then each   rate (int8), wave, phase, fade, dest 1, amount 1 (int8), dest 2, amount 2 (int8)
 * Nothing to keep (every lane at the defaults): no record (0 bytes), as a project from before. */
#define DLFO_VER 1u
#define DLFO_ESIZE 8u
#define DLFO_HEAD 4u
#define DLFO_ENC_MAX (DLFO_HEAD + DRUM_LANES * DLFO_ESIZE)
static uint32_t dlfo_encode(uint8_t *o)
{
    uint32_t l, n = DLFO_HEAD, m = 0;
    o[0] = DLFO_VER, o[1] = DLFO_ESIZE;
    for (l = 0; l < DRUM_LANES; l++) {
        const dlfo_t *c = &dlfo[l];
        if (dlfo_is_def(c))
            continue;
        m |= 1u << l;
        o[n++] = (uint8_t)c->rate, o[n++] = c->wave, o[n++] = c->phase, o[n++] = c->fade;
        o[n++] = c->s[0].dest, o[n++] = (uint8_t)c->s[0].amt, o[n++] = c->s[1].dest, o[n++] = (uint8_t)c->s[1].amt;
    }
    o[2] = (uint8_t)m, o[3] = (uint8_t)(m >> 8);
    return m ? n : 0u;
}
/* n bytes at a (n 0: none) -> the working LFOs (the IRQ off or the ISR: a load); 0 refused (every LFO off) */
static int dlfo_decode(const uint8_t *a, uint32_t n)
{
    uint32_t l, m, es, k = 0, at = DLFO_HEAD;
    int ok = n >= DLFO_HEAD && a[0] == DLFO_VER && a[1] != 0u;
    for (l = 0; l < DRUM_LANES; l++)
        dlfo[l] = DLFO_DEF;
    if (ok) {
        m = (uint32_t)a[2] | (uint32_t)a[3] << 8, es = a[1];
        for (l = 0; l < DRUM_LANES; l++)
            k += (m >> l) & 1u;
        ok = n == DLFO_HEAD + k * es;
        for (l = 0; ok && l < DRUM_LANES; l++)
            if ((m >> l) & 1u) {
                uint8_t b[DLFO_ESIZE];
                dlfo_t *c = &dlfo[l];
                b[0] = (uint8_t)DLFO_DEF.rate, b[1] = DLFO_DEF.wave, b[2] = DLFO_DEF.phase, b[3] = DLFO_DEF.fade;
                b[4] = b[5] = b[6] = b[7] = 0;                /* (a shorter entry: the rest at the defaults) */
                memcpy(b, a + at, es < DLFO_ESIZE ? es : DLFO_ESIZE);
                c->rate = (int8_t)b[0], c->wave = b[1], c->phase = b[2], c->fade = b[3];
                c->s[0].dest = b[4], c->s[0].amt = (int8_t)b[5], c->s[1].dest = b[6], c->s[1].amt = (int8_t)b[7];
                dlfo_fix(c);
                at += es;
            }
    }
    dlfo_touch();
    return ok || !n;
}

#if DLFO_ON
/* the blocks counted while stopped (events_block, once a block: one increment, only with a live LFO) */
#define DLFO_BLOCK() do { if (dlfo_on && !song.playing) dlfo_free++; } while (0)
/* lane l's LFO now (Q15, its fade in): at a hit (the ISR) */
static int32_t dlfo_now(uint32_t l)
{
    const dlfo_t *c = &dlfo[l];
    uint32_t n = song.playing ? song.tick : dlfo_free, f = 32767;
    uint64_t p;
    if (c->rate >= 0) {
        p = (uint64_t)n * LFO_INC[c->rate & 127];
    } else {
        uint32_t s = (uint32_t)(-c->rate - 1), kt = LSYNC_KT[s < LSYNC_N ? s : 0u];
        if (song.playing)                          /* (the beats since PLAY and the units into this one) */
            p = (uint64_t)clk_beat * (((uint64_t)BEAT_U * kt) >> 16) + (((uint64_t)clk_pos * kt) >> 16);
        else                                       /* (stopped: the blocks at the tempo shown) */
            p = (uint64_t)n * (((uint64_t)CTL * (uint32_t)song.g[G_BPM] * kt) >> 16);
    }
    p += (uint64_t)c->phase << 25;
    if (c->fade && song.playing) {                 /* (as voice.c track_lfo_tick: ENV_LIN >> 9 a block, from PLAY) */
        uint32_t st = ENV_LIN[c->fade & 127u] >> 9;
        uint64_t fv = (uint64_t)n * (st ? st : 1u);
        f = fv < 32767u ? (uint32_t)fv : 32767u;
    }
    return (lfo_shape(c->wave, (uint32_t)p, c->wave == LFO_WAVE_SH ? lfo_rnd_of((uint32_t)(p >> 32), l) : 0u) * (int32_t)f) >> 15;
}
#else
#define DLFO_BLOCK() ((void)0)
#endif

#if DLA_ON || DLFO_ON
static union {
    int8_t o[DE_N];                        /* the offsets of the hit dl_play_hit computed (dlp_p) */
    uint32_t w[DE_N / 4u];                 /* (copied and tested by words: dl is aligned) */
} dlp;
_Static_assert(DE_N == 8u && sizeof dl.ofs[0] == 8u, "a lane's offsets: two words");
#define dlp_o dlp.o
/* lane l's offsets for this hit (drum_on, the ISR): the patch, then only the values that have something: the holds, the
 * locks over them (both stored clamped), the LFO's pairs on top (clamped) */
static void dl_play_hit(uint32_t l)
{
    uint32_t v, m;
    dlp_l = 0xFFu;
    if (l >= DRUM_LANES || !(DLA_ANY(l) | ((uint32_t)(dlfo_on >> l) & 1u)))
        return;                            /* (none on l: dl_ofs, as before) */
    memcpy(dlp.w, dl.ofs[l], sizeof dlp.w);
#if DLA_ON
    for (m = dla.hm[l], v = 0; m; m >>= 1, v++)
        if (m & 1u)
            dlp_o[v] = dla.hv[l][v];
    for (m = dla.lm[l], v = 0; m; m >>= 1, v++)
        if (m & 1u)
            dlp_o[v] = dla.lv[l][v];
#endif
#if DLFO_ON
    if ((dlfo_on >> l) & 1u) {
        int32_t lfo = dlfo_now(l), a[LFO_NSLOT];
        uint32_t d[LFO_NSLOT], n = lfo ? lfo_slots_pairs(dlfo[l].s, d, a) : 0u, k;
        for (k = 0; k < n; k++) {
            v = d[k] - 1u;
            dlp_o[v] = (int8_t)clamp(dlp_o[v] + lfo_slot_mod(lfo, a[k], DE_MAX[v] - DE_MIN[v]), DE_MIN[v], DE_MAX[v]);
        }
    }
#endif
    dlp_p = dlp.w[0] | dlp.w[1] ? dlp_o : 0;       /* (all 0: the kit as it is, dl_ofs's contract) */
    dlp_l = (uint8_t)l;
}
#endif
