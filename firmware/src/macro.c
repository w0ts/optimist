/* SPDX-License-Identifier: GPL-3.0-only
 * Performance macros after Flowstate's macro.c and arrange.c by Zakaria Chowdhury (zakariachowdhury/flowstate-fm1
 * 3962560, GPL-3.0-only): four knobs with fixed meanings, each moving several parameters at once from a home where
 * it moves nothing, applied in the audio ISR over the authored values; ENERGY's bands. Rewritten for Optimist: a
 * fixed table instead of a World's mappings, no curves, no smoothing classes, no cross-macro rules. */
/* GLO > MACRO (FELUCCA_MACROS, registry.h; docs/BUILDER.md, OPTIMIST.md): COLOR (dark .. bright), MOTION (still ..
 * alive), SPACE (close .. huge), ENERGY (sparse .. intense), each -64..63, 0 = home: nothing moves.
 *
 * Where they are kept: the drum track's ENV DEST FLT / PIT / SHP and LFO DEST FLT values (MAC_ID, params.c), which
 * the drum track never reads (its sounds have their own envelopes; only the synth parts' voices read them). So the
 * positions are in every project, song section, backup and the sections log with no format change, older projects
 * load at 0 (TP's default), a build without the switch keeps them, and motion recording (FELUCCA_MOTION) records
 * them as the drum track's steps (macro_ui.c).
 *
 * How they act (mac_pre / mac_post, fx.c mix_block): after the sequencer's events of the block and before any
 * part renders, each target of MAC_ROWS that a macro off home moves gets its effective value written over its base
 * (the authored one, saved); after the mix, every base is written back (unless something wrote the parameter in
 * between: then that value stays). The main loop (UI, editor, autosave, sections) only ever sees the authored
 * values; the sequencer and motion recording too (they run before mac_pre). All four at home: mac_pre returns at
 * once, nothing is written, the sound is the one without the switch.
 *
 * A row is one target with an amount per macro: hi at +63, lo at -64 (MAC_S: the base scaled toward 0, a depth
 * or a send fading out), in the target's own steps, linear in between, summed over the macros and clamped to the
 * target's range (delay feedback to 100 %). ROLE: per engine its brightness parameter (MAC_BRIGHT; a narrow range
 * takes the amount shifted down); SPREAD: track 1 the amount down, track 3 up (pan: stereo width), track 2 only
 * the scaling. The rows' amounts are figured again only when a position moves (mac_rows). Cost (emulator, 96 MHz,
 * fm-va-studio): ~2,100 instructions a block (~65 a sample) while a macro is off home, ~90 a block at home. */
#define MAC_S (-128)                                   /* lo: the base scaled toward 0 */
enum { MK_PART, MK_ROLE, MK_GLOB, MK_SPREAD, MK_DREV };   /* the synth parts' P_*, their engine's role, song.g[], pan,
                                                           * the drum lanes' REV (drum_sends.c dsend_mac) */
typedef struct {
    uint8_t kind, id;
    int8_t hi[4], lo[4];                               /* COLOR MOTION SPACE ENERGY */
} mac_row_t;
#define MR(k, i, c, m, s, e, lc, lm, ls, le) {k, i, {c, m, s, e}, {lc, lm, ls, le}}
static const mac_row_t MAC_ROWS[] = {
    /*          target              hi: COLOR MOTN SPACE ENRGY   lo: COLOR MOTN  SPACE  ENRGY */
    MR(MK_ROLE, 0,                       48,   0,    0,   16,       -48,    0,     0,   -24),
    MR(MK_PART, P_LD_FLT,                 0,  32,    0,    0,         0, MAC_S,    0,     0),
    MR(MK_PART, P_LD_SHP,                 0,  20,    0,    0,         0, MAC_S,    0,     0),
    MR(MK_PART, P_LD_AMP,                 0,  12,    0,    0,         0, MAC_S,    0,     0),
    MR(MK_PART, P_LD_PIT,                 0,   1,    0,    0,         0, MAC_S,    0,     0),
    MR(MK_PART, P_LRATE,                  0,  20,    0,    0,         0,   -20,    0,     0),
#if FELUCCA_ANALOG2
    MR(MK_PART, P_A2DRFT,                 0,  40,    0,    0,         0, MAC_S,    0,     0),
#endif
    MR(MK_PART, P_DIST,                   0,   0,    0,   24,         0,     0,    0, MAC_S),
    MR(MK_PART, P_CHOR,                   0,   0,   24,    0,         0,     0, MAC_S,    0),
    MR(MK_PART, P_DLY,                    0,   0,   36,    0,         0,     0, MAC_S,    0),
    MR(MK_PART, P_REV,                    0,   0,   48,    0,         0,     0, MAC_S,    0),
    MR(MK_SPREAD, P_PAN,                  0,   0,   28,    0,         0,     0, MAC_S,    0),
    MR(MK_GLOB, G_DCOLOR,                40,   0,    0,    0,       -50,     0,    0,     0),
    MR(MK_GLOB, G_CDEPTH,                 0,  40,    0,    0,         0, MAC_S,    0,     0),
    MR(MK_GLOB, G_RSIZE,                  0,   0,   30,    0,         0,     0,  -40,     0),
    MR(MK_GLOB, G_DFDBK,                  0,   0,   24,    0,         0,     0,  -30,     0),
    MR(MK_DREV, P_REV,                    0,   0,   40,    0,         0,     0, MAC_S,    0),   /* (was GLO > DRUMS REV) */
    MR(MK_GLOB, G_DRLVL,                  0,   0,    0,   10,         0,     0,    0,   -30),
};
#define MAC_NROWS (sizeof MAC_ROWS / sizeof MAC_ROWS[0])
#define MAC_DFDBK_MAX 100                              /* the delay's feedback: never past 100 % by a macro */

/* per engine (by UID) its brightness: EDIT k (low 4 bits) and a right shift of the amount (high 4 bits) for a narrow
 * range; 0xFF none. Read every block (the render's own parameters, none read only at a note's start) */
#if FELUCCA_ANALOG2
static const uint8_t MAC_BRIGHT[ENG_UID_N] = {
    0x04, 0x04, 0x02, 0x07, 0x04, 0x04, 0x05,          /* ANALOG CUT, DIGITAL IDX, PHASE DCW, LOFI TONE, SAMPLE CUT,
                                                        * VOICE BUZZ, TRIO CUT */
    0x33, 0x07, 0x11, 0x07, 0x02, 0x00};               /* WHEEL TOP (-8..8), GRAIN TONE, FM6 MOD, SLICE TONE, PHYS
                                                        * BRIT, ACID CUT */
#else
static const uint8_t MAC_BRIGHT[ENG_UID_N] = {0x04, 0x04, 0x02, 0x07, 0x04, 0x04, 0x05, 0x33, 0x07, 0x05, 0x11, 0x07};
#endif                                                 /* (SUPER CUT at 9) */

#define MAC_MAX 42                                     /* the writes of one block: 12 rows x 3 parts + 5 globals (+ room) */
static struct {
    uint8_t n;
    int16_t *p[MAC_MAX];
    int16_t old[MAC_MAX], put[MAC_MAX];                /* the base, the value written */
} mov;
static struct {                                        /* the rows at the positions x (figured when they move) */
    int8_t x[4];
    uint8_t ok;
    uint8_t sc[MAC_NROWS];                             /* each row's scaling of the base, /64 (64: none) */
    int16_t add[MAC_NROWS];                            /* then its offset, in the target's steps */
} mrow;

/* row r at positions x: its scaling of the base (/64, 64 none) and its offset (the target's steps), the macros summed */
static void mac_amt(const mac_row_t *r, const int32_t *x, int32_t *scp, int32_t *addp)
{
    uint32_t m;
    int32_t add = 0, sc = 64;
    for (m = 0; m < 4u; m++) {
        if (x[m] > 0)
            add += r->hi[m] * x[m] / 63;
        else if (x[m] < 0 && r->lo[m] == MAC_S)
            sc = sc * (64 + x[m]) / 64;
        else if (x[m] < 0)
            add += r->lo[m] * -x[m] / 64;
    }
    *scp = sc;
    *addp = add;
}

/* the rows at positions x: the scalings (multiplied), the offsets (summed) */
static void mac_rows(const int32_t *x)
{
    uint32_t i, m;
    for (i = 0; i < MAC_NROWS; i++) {
        int32_t sc, add;
        mac_amt(&MAC_ROWS[i], x, &sc, &add);
        mrow.sc[i] = (uint8_t)sc;
        mrow.add[i] = (int16_t)add;
    }
    for (m = 0; m < 4u; m++)
        mrow.x[m] = (int8_t)x[m];
    mrow.ok = 1;
}

/* a base scaled, offset and clamped to lo..hi (one definition for the audio ISR and the UI) */
static int32_t mac_apply(int32_t v, int32_t sc, int32_t add, int32_t lo, int32_t hi)
{
    if (sc == 64 && !add)
        return v;
    if (sc != 64)
        v = v * sc / 64;
    return clamp(v + add, lo, hi);
}

/* the delay feedback's top: never past MAC_DFDBK_MAX by a macro (a base already past it stays where it is) */
static int32_t mac_glob_hi(uint32_t id, int32_t base)
{
    int32_t hi = GP[id].max;
    if (id == G_DFDBK && hi > MAC_DFDBK_MAX)
        hi = base > MAC_DFDBK_MAX ? base : MAC_DFDBK_MAX;
    return hi;
}

/* *p's effective value (scaled, offset, clamped to lo..hi) written, its base kept for mac_post */
static void mac_put(int16_t *p, int32_t sc, int32_t add, int32_t lo, int32_t hi)
{
    int32_t v = mac_apply(*p, sc, add, lo, hi);
    if (v == *p || mov.n >= MAC_MAX)
        return;
    mov.p[mov.n] = p;
    mov.old[mov.n] = *p;
    mov.put[mov.n++] = (int16_t)v;
    *p = (int16_t)v;
}

/* the audio ISR, after the block's events (fx.c mix_block): the effective values in */
static void mac_pre(void)
{
    int32_t x[4];
    uint32_t i, k, m, any = 0, moved = !mrow.ok;
    mov.n = 0;
    dsend_msc = 64, dsend_madd = 0;                    /* (the drum lanes' REV: as authored) */
    for (m = 0; m < 4u; m++) {
        x[m] = clamp(TDRUM->p[MAC_ID[m]], -64, 63);
        any |= (uint32_t)x[m];
        moved |= x[m] != mrow.x[m];
    }
    if (!any)
        return;                                        /* (home: nothing written) */
    if (moved)
        mac_rows(x);
    for (i = 0; i < MAC_NROWS; i++) {
        const mac_row_t *r = &MAC_ROWS[i];
        int32_t sc = mrow.sc[i], add = mrow.add[i], lo = TP[r->id].min, hi = TP[r->id].max;
        if (!add && sc == 64)
            continue;                                  /* (no macro off home moves this one) */
        if (r->kind == MK_DREV) {                      /* every drum lane's REV, as the voices take it */
            dsend_msc = (int16_t)sc, dsend_madd = (int16_t)add;
            continue;
        }
        if (r->kind == MK_GLOB) {
            int16_t *p = &song.g[r->id];
            mac_put(p, sc, add, GP[r->id].min, mac_glob_hi(r->id, *p));
            continue;
        }
        for (k = 0; k < NPART; k++) {
            track_t *t = &trk[k];
            if (r->kind == MK_ROLE) {
                uint32_t b = MAC_BRIGHT[eng_uid(t->engine % NENGINES) % ENG_UID_N];
                const param_desc_t *d = &ENGINES[t->engine % NENGINES]->edit[b & 7u];
                if (b != 0xFFu)
                    mac_put(&t->p[P_E0 + (b & 7u)], sc, add >> (b >> 4), d->min, d->max);
            } else {
                mac_put(&t->p[r->id], sc, r->kind == MK_SPREAD ? add * ((int32_t)k - 1) : add, lo, hi);
            }
        }
    }
}

/* the audio ISR, after the mix: the bases back (a parameter written meanwhile keeps that value) */
static void mac_post(void)
{
    uint32_t i = mov.n;
    while (i--)
        if (*mov.p[i] == mov.put[i])
            *mov.p[i] = mov.old[i];
    mov.n = 0;
}

/* The UI's side (the main loop: draws, the editor): the value a macro off home makes of an authored one, from the
 * same rows and the same mac_amt / mac_apply the audio ISR writes with, from the positions as saved (the drum track's
 * MAC_ID values). Nothing the ISR holds is read, nothing is written. base comes back when no macro moves it. */
static int mac_home(int32_t *x)
{
    uint32_t m;
    for (m = 0; m < 4u; m++)
        x[m] = clamp(TDRUM->p[MAC_ID[m]], -64, 63);
    return (x[0] | x[1] | x[2] | x[3]) == 0;
}

/* track k's parameter id (P_*; a synth part: the drum track's are its own) as it plays when its authored value is base */
static int32_t mac_effective(uint32_t k, uint32_t id, int32_t base)
{
    int32_t x[4], sc, add;
    uint32_t i;
    if (k >= NPART || id >= P_COUNT || mac_home(x))
        return base;
    for (i = 0; i < MAC_NROWS; i++) {
        const mac_row_t *r = &MAC_ROWS[i];
        if (r->kind == MK_ROLE) {
            uint32_t b = MAC_BRIGHT[eng_uid(trk[k].engine % NENGINES) % ENG_UID_N];
            const param_desc_t *d = &ENGINES[trk[k].engine % NENGINES]->edit[b & 7u];
            if (b == 0xFFu || id != P_E0 + (b & 7u))
                continue;
            mac_amt(r, x, &sc, &add);
            return mac_apply(base, sc, add >> (b >> 4), d->min, d->max);
        }
        if ((r->kind == MK_PART || r->kind == MK_SPREAD) && r->id == id) {
            mac_amt(r, x, &sc, &add);
            return mac_apply(base, sc, r->kind == MK_SPREAD ? add * ((int32_t)k - 1) : add, TP[id].min, TP[id].max);
        }
    }
    return base;
}

/* global id (G_*) as it plays when its authored value is base */
static int32_t mac_effective_g(uint32_t id, int32_t base)
{
    int32_t x[4], sc, add;
    uint32_t i;
    if (id >= G_COUNT || mac_home(x))
        return base;
    for (i = 0; i < MAC_NROWS; i++) {
        const mac_row_t *r = &MAC_ROWS[i];
        if (r->kind != MK_GLOB || r->id != id)
            continue;
        mac_amt(r, x, &sc, &add);
        return mac_apply(base, sc, add, GP[id].min, mac_glob_hi(id, base));
    }
    return base;
}

/* the drum lanes' REV send: its scaling (/64) and offset (0..127 steps) under the macros (64 and 0: none) */
static void mac_drev(int32_t *scp, int32_t *addp)
{
    int32_t x[4];
    uint32_t i;
    *scp = 64, *addp = 0;
    if (mac_home(x))
        return;
    for (i = 0; i < MAC_NROWS; i++)
        if (MAC_ROWS[i].kind == MK_DREV)
            mac_amt(&MAC_ROWS[i], x, scp, addp);
}

/* a drum lane's REV send, 0..127 in a synth track's steps, as it plays when authored as send (SPACE moves it) */
static int32_t mac_effective_drev(int32_t send)
{
    int32_t sc, add;
    mac_drev(&sc, &add);
    return DSEND_RLVL_AT(send, sc, add);               /* (as the voices take it: drum_sends.c) */
}

#if FELUCCA_ENERGY
/* ENERGY's bands (FELUCCA_ENERGY, an option of MACROS; after Flowstate's arrange.c): the ENERGY position picks one
 * of five bands, with a hysteresis of EN_HYST around each edge, taken on the beat (a knob turned mid-beat changes
 * the drums from the next beat). The drum track's steps play through the band (a copy of the step, never the
 * pattern itself):
 * Edges at -40, -14, +14, +40, each crossed EN_HYST past it (from home: band 0 below -43, 1 from -43, 2 from -17,
 * 3 from +17, 4 from +43; back home from either side at -11 / +11):
 *   0  only kick, kick 2, snare, clap, on the eighths; every hit a level softer; no ratchets
 *   1  the other lanes on the eighths only; ghost notes left out; no ratchets
 *   2  the pattern as written (home: the step itself, nothing copied)
 *   3  every hit a level harder (ghost -> soft -> norm -> hard)
 *   4  as 3, the closed / pedal hats and the shaker ratchet x2, and every second pass of the pattern (8 steps or
 *      more) ends with a snare fill over its last four steps
 * The synth tracks are not thinned: Optimist's tracks have no roles to choose layers by (GLO mutes them). */
#define EN_CORE 0x000Fu                                /* kick, kick 2, snare, clap */
#define EN_HATS (1u << 4 | 1u << 6 | 1u << 13)          /* hat, pedal, shaker */
#define EN_SNARE 2u
#define EN_HYST 3
static const int8_t EN_EDGE[4] = {-40, -14, 14, 40};   /* band k + 1 starts at EDGE[k] */
static const uint8_t EN_UP[4] = {LV_HARD, LV_SOFT, LV_NORM, LV_HARD};     /* (LV_NORM GHOST SOFT HARD) a level up */
static const uint8_t EN_DOWN[4] = {LV_SOFT, LV_GHOST, LV_GHOST, LV_NORM};
static const uint8_t EN_FILL[4] = {LV_SOFT, LV_NORM, LV_NORM, LV_HARD};
static struct {
    uint8_t band;
    uint32_t beat;
    dstep_t cur;                                       /* the playing step through the band */
    const dstep_t *ptr;                                /* &cur while it plays one, else 0 (the step itself) */
} en = {2u, 0xFFFFFFFFu, {{0}}, 0};

/* the band of position x, from band cur: an edge is crossed EN_HYST past it */
static uint32_t en_band(int32_t x, uint32_t cur)
{
    uint32_t k, b = 0;
    for (k = 0; k < 4u; k++)
        if (x >= EN_EDGE[k] + (k >= cur ? EN_HYST : -EN_HYST))
            b = k + 1u;
    return b;
}

/* the audio ISR (seq.c seq_tick, the drum track's step idx starts): the step as the band plays it */
static const dstep_t *en_step(const track_t *t, const dstep_t *s, uint32_t idx)
{
    uint32_t b, m, l, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    if (clk_beat != en.beat || !idx) {
        en.beat = clk_beat;
        en.band = (uint8_t)en_band(clamp(TDRUM->p[MAC_ID[3]], -64, 63), en.band);
    }
    en.ptr = 0;
    b = en.band;
    if (b == 2u)
        return s;
    m = dstep_mask(s);
    if (b < 2u && (idx & 1u))
        m &= b ? EN_CORE : 0u;                         /* (the sixteenths between the eighths) */
    if (!b)
        m &= EN_CORE;
    memset(&en.cur, 0, sizeof en.cur);
    for (l = 0; m; l++, m >>= 1) {
        uint32_t lv = dstep_lvl(s, l), rat = b < 2u ? 0u : dstep_rat(s, l);
        if (!(m & 1u) || (b == 1u && lv == LV_GHOST))
            continue;
        lv = b == 0u ? EN_DOWN[lv] : b > 2u ? EN_UP[lv] : lv;
        if (b == 4u && !rat && ((EN_HATS >> l) & 1u))
            rat = 1u;
        dstep_set(&en.cur, l, lv, rat);
    }
    if (b == 4u && len >= 8u && (t->pass & 1u) && idx >= len - 4u)
        dstep_set(&en.cur, EN_SNARE, EN_FILL[idx - (len - 4u)], idx >= len - 2u);
    en.ptr = &en.cur;
    return &en.cur;
}
#define EN_STEP(t, s, idx) en_step(t, s, idx)
#define EN_CUR(s) (en.ptr ? en.ptr : (s))
#endif
