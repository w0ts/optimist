/* SPDX-License-Identifier: GPL-3.0-only */
/* GATER (2026-10-14): a further insert type (insfx.c), a tempo-synced rhythmic volume gate (a trance gate), one
 * instance a track (the parts, the drum bus: one gain on its L and R), a few bytes each: any number at once.
 *   DEPTH   P_TGATE, the slot's amount (0 off): how far a closed step closes, 127 silent
 *   PATTERN P_TGPAT, one of IGT_NPAT built-in patterns of 16 steps (IGT_PAT; names: params.c N_TGPAT)
 *   RATE    P_TGRATE, a step: 1/8 1/16 1/32 8T 16T 32T (params.c N_SLDIV, the SLICER's)
 *   SHAPE   P_TGSHP, 0 hard .. 127 smooth: the gain's ramps, ~1.1 ms (the shortest, no click) up to half a step
 * A step 'x' is a pulse: open from its start to its end, or to its middle when the next step is an 'x' too (a gap
 * between two pulses: xxxx at 1/16 chops in sixteenths); '.' closed. The gain follows the open / closed target at
 * a bounded slope (a step of at most 1/48 of full scale a sample: no click at an edge, a change of pattern, rate or
 * depth, the transport). The phase: playing, the transport clock's (fx.c clk_beat clk_pos, at the end of the block
 * being rendered: its start is clk_blk units before), so step 0 is the sequencer's step 0 from PLAY, at any tempo,
 * nudged or following an external clock; stopped, it runs on at the tempo shown (clk_blk). Swing (the track's +
 * the global, as seq.c grid_at): on a straight RATE equal to the track's step DIV, the odd steps start late. No
 * divide (div_audit), no packed SIMD. At 0 and open: nothing runs, the block is the input bit for bit. */
#if FELUCCA_INS_GATER
#define IGT_NPAT 16u
/* bit k = step k ('x' open), each in the order played: step 0 first. Named for RATE 1/16 (params.c N_TGPAT) */
_Static_assert(sizeof N_TGPAT / sizeof N_TGPAT[0] == IGT_NPAT, "a name a pattern (params.c N_TGPAT)");
static const uint16_t IGT_PAT[IGT_NPAT] = {
    0xFFFF,   /*  0 16TH  xxxxxxxxxxxxxxxx  1/16 chops */
    0x5555,   /*  1 8TH   x.x.x.x.x.x.x.x.  1/8 chops */
    0x1111,   /*  2 4TH   x...x...x...x...  quarters */
    0x0101,   /*  3 HALF  x.......x.......  halves */
    0x4444,   /*  4 OFF8  ..x...x...x...x.  offbeat 1/8 */
    0xAAAA,   /*  5 OFF16 .x.x.x.x.x.x.x.x  offbeat 1/16 */
    0x9249,   /*  6 DOT8  x..x..x..x..x..x  dotted 1/8 */
    0x1041,   /*  7 DOT4  x.....x.....x...  dotted 1/4 */
    0x6D6D,   /*  8 TRNC1 x.xx.xx.x.xx.xx.  the classic trance gate */
    0xB6DB,   /*  9 TRNC2 xx.xx.xx.xx.xx.x */
    0xB777,   /* 10 TRNC3 xxx.xxx.xxx.xx.x */
    0xDAB5,   /* 11 TRNC4 x.x.xx.x.x.xx.xx */
    0xDDDD,   /* 12 GALOP x.xxx.xxx.xxx.xx  gallop */
    0xBBBB,   /* 13 RVGAL xx.xxx.xxx.xxx.x  reverse gallop */
    0x1449,   /* 14 CLAVE x..x..x...x.x...  son clave 3-2 */
    0x4949,   /* 15 TRESL x..x..x.x..x..x.  tresillo, twice */
};
/* RATE: steps a beat; the phase is Q32 over the 16 steps (2^28 a step): a beat moves it DEN 2^28, a clock unit
 * (BEAT_U a beat) DEN 2^28 / BEAT_U = (IGT_K >> 21) */
static const uint8_t IGT_DEN[6] = {2, 4, 8, 3, 6, 12};
#define IGT_K(d) ((uint32_t)(((uint64_t)(d) << 49) / BEAT_U))   /* (constants: d 2^49 / BEAT_U < 2^32) */
static const uint32_t IGT_KC[6] = {IGT_K(2), IGT_K(4), IGT_K(8), IGT_K(3), IGT_K(6), IGT_K(12)};
_Static_assert(BEAT_U < (1u << 22) && IGT_K(12) > IGT_K(8), "clk_pos IGT_KC < 2^54, IGT_K(12) < 2^32");
#define IGT_STEP (1u << 28)
#define IGT_PAIR (2u * IGT_STEP)
#define IGT_SMAX (32768 / 48)                           /* the steepest slope: 48 samples (1.1 ms) from open to closed */
#define IGT_SMIN 4u                                     /* the gentlest: 8192 samples (186 ms; a slow tempo, smooth) */
/* SHAPE: the ramp is 2^((SHAPE - 127) / 16) / 2 of a step; its slope 32768 / (that x the step's samples) is the
 * phase step x 2^((127 - SHAPE) / 16) / 2^12: 2^(i / 16), Q14 */
static const uint16_t IGT_EXP16[16] = {16384, 17109, 17867, 18658, 19484, 20347, 21247, 22188,
                                       23170, 24196, 25267, 26386, 27554, 28774, 30048, 31379};
typedef struct {
    int32_t c;                                          /* the closure, Q15: 0 open (the gain 1, at rest) .. 32768 */
    uint32_t ph;                                        /* the phase at the end of the last block (stopped: runs on) */
} ifx_gt_t;
static ifx_gt_t ifx_gate[NTRK];                         /* the parts', the drum bus's (IFX_BUS) */
_Static_assert(IFX_BUS == NPART && NTRK == NPART + 1u, "ifx_gate: a part each, then the drum bus");

AINL uint32_t igt_slope(uint32_t shape, uint32_t inc)  /* the closure's step a sample (1 .. IGT_SMAX) */
{
    uint32_t e = 127u - (shape > 127u ? 127u : shape);
    uint64_t s = ((uint64_t)inc * IGT_EXP16[e & 15u]) << (e >> 4);   /* (inc < 2^20: < 2^42) */
    s >>= 26;
    return s < IGT_SMIN ? IGT_SMIN : s > (uint64_t)IGT_SMAX ? (uint32_t)IGT_SMAX : (uint32_t)s;
}
typedef struct {
    uint32_t ph, inc, sl;                               /* the block's first phase, a sample's step, the slope */
} igt_blk_t;
/* the even step's length in a pair (Q32 phase): a step, the swing's units more on track t's own straight grid */
AINL uint32_t igt_swing(const track_t *t, uint32_t r)
{
    uint32_t d = (uint32_t)t->p[P_SDIV];
    if (r >= 3u || d >= 4u || DIV_DEN[d] != IGT_DEN[r])
        return IGT_STEP;
    return IGT_STEP + (uint32_t)clamp(t->p[P_SSWING] + song.g[G_SWING], 0, 100) * (IGT_STEP / 200u);
}
/* the block's phase at its first sample, a sample's step, the slope of track t's SHAPE (flash, FAR: its 64-bit
 * products stay out of the RAM code) */
static __attribute__((noinline)) void igt_block(ifx_gt_t *s, const track_t *t, uint32_t r, igt_blk_t *o)
{
    uint32_t kc = IGT_KC[r], d = (uint32_t)(((uint64_t)clk_blk * kc) >> 21), p0;
    if (song.playing) {
        uint32_t beat = clk_beat, pos = clk_pos;        /* (at the block's end: events_block ran) */
        p0 = beat * ((uint32_t)IGT_DEN[r] << 28) + (uint32_t)(((uint64_t)pos * kc) >> 21) - d;
    } else {
        p0 = s->ph;
    }
    s->ph = p0 + d;
    o->ph = p0;
    o->inc = d >> CTL_LOG2;                             /* (a block is CTL samples) */
    o->sl = igt_slope((uint32_t)t->p[P_TGSHP], o->inc);
}
/* track t's GATER at depth a on n samples of l (r: the drum bus's right, 0 a part) */
static HOT2 __attribute__((noinline)) void ifx_gater(ifx_gt_t *s, const track_t *t, int32_t a, int32_t *l, int32_t *r,
                                                     uint32_t n)
{
    igt_blk_t bk;
    uint32_t i, rt = (uint32_t)t->p[P_TGRATE] % 6u, pat, nx, b1, h0, h1, ph;
    int32_t c = s->c, cd = a >= 127 ? 32768 : a * 258, sl;
    if (!a && !c)
        return;                                         /* (off and open: the block as it is) */
    FAR(igt_block)(s, t, rt, &bk);
    ph = bk.ph, sl = a ? (int32_t)bk.sl : IGT_SMAX;    /* (off: open as fast as no click allows, then at rest) */
    pat = IGT_PAT[(uint32_t)t->p[P_TGPAT] % IGT_NPAT];
    nx = (pat >> 1 | pat << 15) & 0xFFFFu;              /* bit k: step k + 1 is open */
    b1 = igt_swing(t, rt);
    h0 = b1 >> 1, h1 = (b1 >> 1) + IGT_STEP;            /* the middles: (0 + b1) / 2, (b1 + IGT_PAIR) / 2 */
    for (i = 0; i < n; i++, ph += bk.inc) {
        uint32_t q = ph & (IGT_PAIR - 1u), k = (ph >> 28) & 14u, end;
        int32_t g;
        if (q < b1)
            end = (nx >> k & 1u) ? h0 : b1;
        else
            k++, end = (nx >> k & 1u) ? h1 : IGT_PAIR;
        c += clamp(((pat >> k & 1u) && q < end ? 0 : cd) - c, -sl, sl);
        if (!c)
            continue;                                   /* (open: the sample as it is) */
        g = (32768 - c) << 1;                           /* (Q16) */
        l[i] = mulq16(l[i], (uint32_t)g);
        if (r)
            r[i] = mulq16(r[i], (uint32_t)g);
    }
    s->c = c;
}
/* GATER's picture (GR_GATE: ui/optimist/op_graph.c, ui/sloop/ui_draw.c; main loop, flash). Track t's gain at each of
 * IGT_PN columns, IGT_PW a step over the 16 steps: 255 open .. 0 shut at DEPTH, as ifx_gater makes it (a pulse a step, or
 * half of it before another pulse; a ramp of 2^((SHAPE - 127) / 16) / 2 of a step, 1 px at the least; the swing and the
 * tempo are left out). DEPTH 0 (off) is drawn at 127, the gate it would be. Two passes: the second starts where the first ended */
#define IGT_PW 14u
#define IGT_PN (16u * IGT_PW)
static void igt_profile(const track_t *t, uint8_t *g)
{
    uint32_t pat = IGT_PAT[(uint32_t)t->p[P_TGPAT] % IGT_NPAT], nx = (pat >> 1 | pat << 15) & 0xFFFFu, i, pass;
    uint32_t e = 127u - (uint32_t)clamp(t->p[P_TGSHP], 0, 127), sl = ((uint32_t)IGT_EXP16[e & 15u] << (e >> 4)) / 448u;
    int32_t a = t->p[P_TGATE], c = 0, cd, s = (int32_t)(sl > 256u ? 256u : sl);
    a = a <= 0 ? 127 : a;
    cd = a >= 127 ? 256 : (a * 258) >> 7;
    for (pass = 0; pass < 2u; pass++)
        for (i = 0; i < IGT_PN; i++) {
            uint32_t k = i / IGT_PW, x = i - k * IGT_PW;
            int32_t open = (pat >> k & 1u) && (!(nx >> k & 1u) || x < IGT_PW / 2u);
            c += clamp((open ? 0 : cd) - c, -s, s);
            if (pass)
                g[i] = (uint8_t)(255 - (c > 255 ? 255 : c));
        }
}
/* the step track k's gate is at (0 .. 15). Playing: from the transport's clock as igt_block takes it (so a silent track, whose
 * gate is not run, still shows it); stopped: its phase at the end of the last block, which runs on */
static uint32_t igt_step(uint32_t k)
{
    uint32_t r = (uint32_t)trk[k % NTRK].p[P_TGRATE] % 6u, ph;
    if (song.playing)
        ph = clk_beat * ((uint32_t)IGT_DEN[r] << 28) + (uint32_t)(((uint64_t)clk_pos * IGT_KC[r]) >> 21);
    else
        ph = ifx_gate[k % NTRK].ph;
    return (ph >> 28) & 15u;
}
#endif
