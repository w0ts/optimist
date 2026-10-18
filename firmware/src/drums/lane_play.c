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
/* LFO 2 and ENV2 (2026-10-18, the user: "we really need env2 and lfo2 on drumlanes"; ruled: per hit, as the LFO): a lane's
 * second LFO, as the first (dlfo2, DLFO_DESC), and its ENV2: from its restart (RTRG: PLAY, or every 1 / 2 / 4 bars from
 * PLAY) it rises over ATK 16th steps, falls over DEC to SUS and stays; a hit plays the envelope's level at its place in
 * the transport (stopped: its start) on two slots as the LFO's. A slope is a 1 / steps from the main loop (denv_k): no
 * divide at the hit. Both stored after the LFO in each lane's entry (dlfo_encode: DLFO_ESIZE 24, a reader of before
 * takes its 8) */
typedef struct {
    uint8_t atk, dec, sus, rtrg;           /* ATK DEC 0..64 16th steps, SUS 0..127, RTRG 0 PLAY 1 BAR 2 2BAR 3 4BAR */
    lfo_slot_t s[LFO_NSLOT];
} denv_t;
_Static_assert(sizeof(denv_t) == 8u, "a lane's ENV2: 8 bytes");
#define DENV_DEF_INIT {0, 16, 0, 1, {{0, 0}, {0, 0}}}
static const denv_t DENV_DEF = DENV_DEF_INIT;
static dlfo_t dlfo2[DRUM_LANES] = {[0 ... DRUM_LANES - 1] = DLFO_DEF_INIT};
static denv_t denv[DRUM_LANES] = {[0 ... DRUM_LANES - 1] = DENV_DEF_INIT};
static uint16_t denv_k[DRUM_LANES][2] = {[0 ... DRUM_LANES - 1] = {0, 32768u / 16u}};   /* 32768 / ATK, 32768 / DEC (0: none) */
static const char *const N_DRTRG[4] = {"PLAY", "1BAR", "2BAR", "4BAR"};
/* the fields after the LFO's 8 (DLF_*): LFO 2's 8 (DLFO_DESC), then ENV2's ATK DEC SUS RTRG DEST AMT DEST2 AMT2 */
enum { DLF2_0 = DLF_N, DLE_0 = 2 * DLF_N, DLE_ATK = DLE_0, DLE_DEC, DLE_SUS, DLE_RTRG, DLE_DEST1, DLE_AMT1, DLE_DEST2,
       DLE_AMT2, DLF_ALL };
static const param_desc_t DENV_DESC[8] = {
    {"ATK", F_INT, 0, 64, 0, 0, "st"}, {"DEC", F_INT, 0, 64, 16, 0, "st"}, PD("SUS", F_PCT, 0, 127, 0),
    PE("RTRG", N_DRTRG, 1), PE("DEST", N_DLDEST, 0), PD("AMT", F_BIPCT, LFO_AMT_MIN, LFO_AMT_MAX, 0),
    PE("DEST2", N_DLDEST, 0), PD("AMT2", F_BIPCT, LFO_AMT_MIN, LFO_AMT_MAX, 0)};
static const param_desc_t *dlfo_desc(uint32_t f) { return f < DLE_0 ? &DLFO_DESC[f % DLF_N] : &DENV_DESC[(f - DLE_0) & 7u]; }
static void denv_fix(denv_t *c, uint32_t l)
{
    uint32_t i;
    c->atk = (uint8_t)(c->atk > 64u ? 64u : c->atk);
    c->dec = (uint8_t)(c->dec > 64u ? 64u : c->dec);
    c->sus = (uint8_t)(c->sus & 127u);
    c->rtrg = (uint8_t)(c->rtrg & 3u);
    for (i = 0; i < LFO_NSLOT; i++)
        c->s[i] = lfo_slot_fix(c->s[i].dest, c->s[i].amt, DE_N);
    denv_k[l % DRUM_LANES][0] = (uint16_t)(c->atk ? 32768u / c->atk : 0u);
    denv_k[l % DRUM_LANES][1] = (uint16_t)(c->dec ? 32768u / c->dec : 0u);
}
static int dlfo_is_def(const dlfo_t *c) { return !memcmp(c, &DLFO_DEF, sizeof *c); }
static int dlane_is_def(uint32_t l)        /* lane l's LFO, LFO 2 and ENV2 all at their defaults */
{
    return dlfo_is_def(&dlfo[l]) && dlfo_is_def(&dlfo2[l]) && !memcmp(&denv[l], &DENV_DEF, sizeof denv[l]);
}
/* lane l's LFO, LFO 2 or ENV2 acts (a slot with a destination and an amount): computed at its hits */
static int dlfo_live(uint32_t l)
{
    return l < DRUM_LANES && (lfo_slots_on(dlfo[l].s) || lfo_slots_on(dlfo2[l].s) || lfo_slots_on(denv[l].s));
}
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
    const dlfo_t *c = f >= DLF2_0 && f < DLE_0 ? &dlfo2[l % DRUM_LANES] : dlfo_get(l);
    if (f >= DLE_0) {                                  /* ENV2 */
        const denv_t *e = &denv[l % DRUM_LANES];
        switch (f) {
        case DLE_ATK: return e->atk;
        case DLE_DEC: return e->dec;
        case DLE_SUS: return e->sus;
        case DLE_RTRG: return e->rtrg;
        case DLE_DEST1: case DLE_DEST2: return e->s[(f - DLE_DEST1) / 2u].dest;
        case DLE_AMT1: case DLE_AMT2: return e->s[(f - DLE_AMT1) / 2u].amt;
        default: return 0;
        }
    }
    f %= DLF_N;                                        /* (LFO 2: the LFO's fields) */
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
static void dlfo_put(dlfo_t *c, uint32_t f, int32_t v)   /* an LFO's field f (DLF_*) to v (in range) */
{
    switch (f) {
    case DLF_RATE: c->rate = (int8_t)v; break;
    case DLF_WAVE: c->wave = (uint8_t)v; break;
    case DLF_PHASE: c->phase = (uint8_t)v; break;
    case DLF_FADE: c->fade = (uint8_t)v; break;
    case DLF_DEST1: case DLF_DEST2: c->s[(f - DLF_DEST1) / 2u].dest = (uint8_t)v; break;
    default: c->s[(f - DLF_AMT1) / 2u].amt = (int8_t)v; break;
    }
}
static void dlfo_field_set(uint32_t l, uint32_t f, int32_t v)
{
    dlfo_t c;
    if (f >= DLF_ALL)
        return;
    v = clamp(v, dlfo_desc(f)->min, dlfo_desc(f)->max);
    if (f >= DLE_0) {                                  /* ENV2 */
        denv_t e = denv[l % DRUM_LANES];
        switch (f) {
        case DLE_ATK: e.atk = (uint8_t)v; break;
        case DLE_DEC: e.dec = (uint8_t)v; break;
        case DLE_SUS: e.sus = (uint8_t)v; break;
        case DLE_RTRG: e.rtrg = (uint8_t)v; break;
        case DLE_DEST1: case DLE_DEST2: e.s[(f - DLE_DEST1) / 2u].dest = (uint8_t)v; break;
        default: e.s[(f - DLE_AMT1) / 2u].amt = (int8_t)v; break;
        }
        denv_fix(&e, l);
        fm1_irq_off();
        denv[l % DRUM_LANES] = e;
        dlfo_touch();
        fm1_irq_on();
        return;
    }
    if (f >= DLF2_0) {                                 /* LFO 2 */
        c = dlfo2[l % DRUM_LANES];
        dlfo_put(&c, f - DLF2_0, v);
        dlfo_fix(&c);
        fm1_irq_off();
        dlfo2[l % DRUM_LANES] = c;
        dlfo_touch();
        fm1_irq_on();
        return;
    }
    c = *dlfo_get(l);
    dlfo_put(&c, f, v);
    dlfo_set(l, &c);
}

/* ---- the stored form (the record: storage/dlfo_rec.c)
 *   byte 0      version, DLFO_VER (a newer version: refused whole, every LFO off)
 *   byte 1      the bytes a lane takes, DLFO_ESIZE (24 since 2026-10-18, 8 before; a reader takes what it knows of an
 *               entry, skips the rest; fewer: the rest at their defaults)
 *   byte 2, 3   the lanes stored, bit l (little endian): those not at the defaults
 *   then each   the LFO: rate (int8), wave, phase, fade, dest 1, amount 1 (int8), dest 2, amount 2 (int8); LFO 2 the same
 *               8; ENV2: atk, dec, sus, rtrg, dest 1, amount 1 (int8), dest 2, amount 2 (int8)
 * Nothing to keep (every lane at the defaults): no record (0 bytes), as a project from before. */
#define DLFO_VER 1u
#define DLFO_ESIZE 24u
#define DLFO_HEAD 4u
#define DLFO_ENC_MAX (DLFO_HEAD + DRUM_LANES * DLFO_ESIZE)
_Static_assert(DLFO_ENC_MAX <= 255u, "the lanes' stored form: its length a byte (dlfo_rec.c)");
static uint8_t *dlfo_out(const dlfo_t *c, uint8_t *o)  /* an LFO's 8 bytes -> o, past them */
{
    o[0] = (uint8_t)c->rate, o[1] = c->wave, o[2] = c->phase, o[3] = c->fade;
    o[4] = c->s[0].dest, o[5] = (uint8_t)c->s[0].amt, o[6] = c->s[1].dest, o[7] = (uint8_t)c->s[1].amt;
    return o + 8;
}
static void dlfo_in(dlfo_t *c, const uint8_t *b)       /* 8 bytes -> an LFO (dlfo_fix after) */
{
    c->rate = (int8_t)b[0], c->wave = b[1], c->phase = b[2], c->fade = b[3];
    c->s[0].dest = b[4], c->s[0].amt = (int8_t)b[5], c->s[1].dest = b[6], c->s[1].amt = (int8_t)b[7];
}
static uint32_t dlfo_encode(uint8_t *o)
{
    uint32_t l, n = DLFO_HEAD, m = 0;
    o[0] = DLFO_VER, o[1] = DLFO_ESIZE;
    for (l = 0; l < DRUM_LANES; l++) {
        uint8_t *q;
        const denv_t *e = &denv[l];
        if (dlane_is_def(l))
            continue;
        m |= 1u << l;
        q = dlfo_out(&dlfo2[l], dlfo_out(&dlfo[l], o + n));
        q[0] = e->atk, q[1] = e->dec, q[2] = e->sus, q[3] = e->rtrg;
        q[4] = e->s[0].dest, q[5] = (uint8_t)e->s[0].amt, q[6] = e->s[1].dest, q[7] = (uint8_t)e->s[1].amt;
        n += DLFO_ESIZE;
    }
    o[2] = (uint8_t)m, o[3] = (uint8_t)(m >> 8);
    return m ? n : 0u;
}
/* n bytes at a (n 0: none) -> the working LFOs (the IRQ off or the ISR: a load); 0 refused (every LFO off) */
static int dlfo_decode(const uint8_t *a, uint32_t n)
{
    uint32_t l, m, es, k = 0, at = DLFO_HEAD;
    int ok = n >= DLFO_HEAD && a[0] == DLFO_VER && a[1] != 0u;
    for (l = 0; l < DRUM_LANES; l++) {
        dlfo[l] = dlfo2[l] = DLFO_DEF;
        denv[l] = DENV_DEF;
        denv_fix(&denv[l], l);
    }
    if (ok) {
        m = (uint32_t)a[2] | (uint32_t)a[3] << 8, es = a[1];
        for (l = 0; l < DRUM_LANES; l++)
            k += (m >> l) & 1u;
        ok = n == DLFO_HEAD + k * es;
        for (l = 0; ok && l < DRUM_LANES; l++)
            if ((m >> l) & 1u) {
                uint8_t b[DLFO_ESIZE];
                denv_t *e = &denv[l];
                (void)dlfo_out(&DLFO_DEF, b), (void)dlfo_out(&DLFO_DEF, b + 8);   /* (a shorter entry: the rest at the */
                b[16] = DENV_DEF.atk, b[17] = DENV_DEF.dec, b[18] = DENV_DEF.sus, b[19] = DENV_DEF.rtrg;   /* defaults) */
                b[20] = b[21] = b[22] = b[23] = 0;
                memcpy(b, a + at, es < DLFO_ESIZE ? es : DLFO_ESIZE);
                dlfo_in(&dlfo[l], b), dlfo_in(&dlfo2[l], b + 8);
                dlfo_fix(&dlfo[l]), dlfo_fix(&dlfo2[l]);
                e->atk = b[16], e->dec = b[17], e->sus = b[18], e->rtrg = b[19];
                e->s[0].dest = b[20], e->s[0].amt = (int8_t)b[21], e->s[1].dest = b[22], e->s[1].amt = (int8_t)b[23];
                denv_fix(e, l);
                at += es;
            }
    }
    dlfo_touch();
    return ok || !n;
}

#if DLFO_ON
/* the blocks counted while stopped (events_block, once a block: one increment, only with a live LFO) */
#define DLFO_BLOCK() do { if (dlfo_on && !song.playing) dlfo_free++; } while (0)
/* lane l's LFO c (dlfo or dlfo2) now (Q15, its fade in): at a hit (the ISR) */
static int32_t dlfo_now(const dlfo_t *c, uint32_t l)
{
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
/* lane l's ENV2 now (0..32767): at a hit (the ISR), from its place since PLAY in 1/256 of a 16th step (playing; stopped:
 * its start), wrapped at its RTRG (1, 2, 4 bars of 16 steps: a power of two); the slopes denv_k's, no divide but by the
 * constant BEAT_U */
static int32_t denv_now(uint32_t l)
{
    const denv_t *e = &denv[l];
    uint32_t p = song.playing ? clk_beat * 1024u + (clk_pos * 1024u) / BEAT_U : 0u, a = (uint32_t)e->atk << 8;
    uint32_t d = (uint32_t)e->dec << 8, sus = (uint32_t)e->sus * 258u;
    if (e->rtrg)
        p &= (4096u << (e->rtrg - 1u)) - 1u;
    if (p < a)
        return (int32_t)((p * denv_k[l][0]) >> 8);
    p -= a;
    if (p < d)
        return (int32_t)(32767u - ((((p * denv_k[l][1]) >> 8) * (32767u - sus)) >> 15));
    return (int32_t)sus;
}
_Static_assert(BEAT_U <= 0xFFFFFFFFu / 1024u, "clk_pos x 1024 fits 32 bits");
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
/* lane l's offsets a hit now plays, into w (two words: DE_N bytes), the one formula of the hit (dl_play_hit, the ISR) and
 * of the UIs' live values (the Optimist cards' actual bar: the main loop reads the same RAM, writes nothing): the patch,
 * then only the values that have something: the holds, the locks over them (both stored clamped), the LFO's pairs on top
 * (clamped). -> 0 nothing acts on l (w untouched: the patch, dl_ofs, as before) */
AINL uint32_t dl_play_ofs(uint32_t l, uint32_t *w)
{
    int8_t *o = (int8_t *)w;
    uint32_t v;
#if DLA_ON
    uint32_t m;
#endif
    if (l >= DRUM_LANES || !(DLA_ANY(l) | ((uint32_t)(dlfo_on >> l) & 1u)))
        return 0;                          /* (none on l: dl_ofs, as before) */
    __builtin_memcpy(w, __builtin_assume_aligned(dl.ofs[l], 4), DE_N);   /* (two words: dl is aligned; libc.c memcpy copies bytes) */
#if DLA_ON
    for (m = dla.hm[l], v = 0; m; m >>= 1, v++)
        if (m & 1u)
            o[v] = dla.hv[l][v];
    for (m = dla.lm[l], v = 0; m; m >>= 1, v++)
        if (m & 1u)
            o[v] = dla.lv[l][v];
#endif
#if DLFO_ON
    if ((dlfo_on >> l) & 1u) {                         /* the LFO, LFO 2, ENV2 (each on its two slots) */
        uint32_t g;
        for (g = 0; g < 3u; g++) {
            const lfo_slot_t *sl = g == 0u ? dlfo[l].s : g == 1u ? dlfo2[l].s : denv[l].s;
            int32_t m = 0, a[LFO_NSLOT];
            uint32_t d[LFO_NSLOT], n = 0, k;
            if (!lfo_slots_on(sl))
                continue;
            m = g == 2u ? denv_now(l) : dlfo_now(g ? &dlfo2[l] : &dlfo[l], l);
            n = m ? lfo_slots_pairs(sl, d, a) : 0u;
            for (k = 0; k < n; k++) {
                v = d[k] - 1u;
                o[v] = (int8_t)clamp(o[v] + lfo_slot_mod(m, a[k], DE_MAX[v] - DE_MIN[v]), DE_MIN[v], DE_MAX[v]);
            }
        }
    }
#endif
    return 1;
}
/* lane l's offsets for this hit (drum_on, the ISR): dl_play_ofs's, kept for the hit's voice (dl_ofs_play) */
static void dl_play_hit(uint32_t l)
{
    dlp_l = 0xFFu;
    if (!dl_play_ofs(l, dlp.w))
        return;
    dlp_p = dlp.w[0] | dlp.w[1] ? dlp_o : 0;       /* (all 0: the kit as it is, dl_ofs's contract) */
    dlp_l = (uint8_t)l;
}
#endif
