/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DIGITAL: four-operator FM. */
/* Four sine operators, eight classic 4-operator algorithms (ALG = P_E0, EDIT 1 KNOB 1),
 * op 4 with feedback, one modulation INDEX shaped by a modulator envelope.
 * Phase modulation wraps naturally in the 32-bit phase. */
#if FELUCCA_DIG_TONE
static const char *const N_FMALG[] = {"1", "2", "3", "4", "5", "6", "7", "8",
                                      "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};   /* T1..T8: the TONE routings */
#else
static const char *const N_FMALG[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
#endif
static const char *const N_RATIO[] = {".5", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "14", "16"};
static const uint16_t RATIO_Q8[15] = {128, 256, 512, 768, 1024, 1280, 1536, 1792, 2048, 2304, 2560, 2816,
                                      3072, 3584, 4096};

static void digital_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[7] = 0;                 /* op 4 phase */
    v->s[4] = 1 << 24;           /* modulator envelope, Q24 */
    v->s[5] = v->s[6] = 0;       /* feedback history */
}

/* operator increment = carrier * ratio (Q8) without 32-bit overflow; kept below Nyquist */
AINL uint32_t fm_ratio_inc(uint32_t inc, uint32_t r)
{
    uint32_t lim = 0x73000000u / r;                      /* (inc >> 8) * r must stay < 0x73000000 */
    return (inc >> 8) > lim ? 0x73000000u : (inc >> 8) * r;
}

AINL uint32_t digital_mod(int32_t x, int32_t idx) { return (uint32_t)(x * idx) * 2065u; }

#if FELUCCA_DIG_TONE
/* TONE (FELUCCA_DIG_TONE, ALG T1..T8): DIGITAL in the manner of Elektron's Digitone (our routings and code; Optimist is
 * not affiliated with Elektron). Four operators in two groups: C the carrier, A its modulator, and the B pair (B1 B2);
 * each routing has two outputs, X and Y, and MIX crossfades them; FDBK feeds one operator back on itself. A LEV is A's
 * depth, steady (an ENV 1 card can move it); B LEV the B pair's, shaped by the track's envelope (it opens and closes
 * with the note). C sets the pitch with its ratio, A and B (B1 and B2 the same) theirs. The track's FLT and SHP and the
 * velocity move both depths, as they move IDX.
 *   T1  A > C, B2 > B1                 X C, Y B1        FB on A
 *   T2  B2 > B1 > A > C                X C, Y A         FB on B2
 *   T3  A + B1 > C, B2 > B1            X C, Y B1        FB on B2
 *   T4  A > C, B2 > B1 > C             X C, Y B2        FB on A
 *   T5  B2 > C, A, B1                  X C, Y A + B1    FB on B2
 *   T6  A > C, B1 and B2 alone         X C, Y B1 + B2   FB on A
 *   T7  A > B1 > C, B2 > C             X C, Y B1        FB on B2
 *   T8  every operator alone (organ)   X C + A, Y B1 + B2  FB on B2
 * Voice state as the classic routings: ph[0] C, ph[1] A, ph[2] B1, s[7] B2, s[5] s[6] the feedback's history. */
static void dig_tone_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t alg = (uint32_t)(p[P_E0] - 8) & 7u, i;
    uint32_t ic = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E1] % 15]), ia = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E2] % 15]);
    uint32_t ib = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E3] % 15]);
    int32_t mod = ((m->cutoff + m->shape - (64 << 8)) >> 8) + (v->vel - 96) / 4;
    int32_t la = clamp(p[P_E4] + mod, 0, 127), lb = clamp(((p[P_E5] * (m->envq15 >> 7)) >> 8) + mod, 0, 127);
    int32_t fb = p[P_E6], mix = clamp(p[P_E7], -64, 63), gy = (mix + 64) * 256, gx = 32767 - gy;   /* X .. Y */
    uint32_t pc = v->ph[0], pa = v->ph[1], p1 = v->ph[2], p2 = (uint32_t)v->s[7];
    int32_t fb1 = v->s[5], fb2 = v->s[6];
    for (i = 0; i < n; i++) {
        int32_t a, b1, b2, c, x, y, f = digital_mod((fb1 + fb2) >> 1, fb);
        switch (alg) {
        case 0:  a = sine_i(pa + f); fb2 = fb1; fb1 = a;
                 c = sine_i(pc + digital_mod(a, la)); b2 = sine_i(p2); b1 = sine_i(p1 + digital_mod(b2, lb));
                 x = c; y = b1; break;
        case 1:  b2 = sine_i(p2 + f); fb2 = fb1; fb1 = b2;
                 b1 = sine_i(p1 + digital_mod(b2, lb)); a = sine_i(pa + digital_mod(b1, lb));
                 c = sine_i(pc + digital_mod(a, la)); x = c; y = a; break;
        case 2:  b2 = sine_i(p2 + f); fb2 = fb1; fb1 = b2;
                 b1 = sine_i(p1 + digital_mod(b2, lb)); a = sine_i(pa);
                 c = sine_i(pc + digital_mod(a, la) + digital_mod(b1, lb)); x = c; y = b1; break;
        case 3:  a = sine_i(pa + f); fb2 = fb1; fb1 = a; b2 = sine_i(p2); b1 = sine_i(p1 + digital_mod(b2, lb));
                 c = sine_i(pc + digital_mod(a, la) + digital_mod(b1, lb)); x = c; y = b2; break;
        case 4:  b2 = sine_i(p2 + f); fb2 = fb1; fb1 = b2;
                 c = sine_i(pc + digital_mod(b2, la)); a = sine_i(pa + digital_mod(b2, lb)); b1 = sine_i(p1 + digital_mod(b2, lb));
                 x = c; y = (a + b1) >> 1; break;
        case 5:  a = sine_i(pa + f); fb2 = fb1; fb1 = a; c = sine_i(pc + digital_mod(a, la));
                 b1 = sine_i(p1); b2 = sine_i(p2); x = c; y = mulq15((b1 + b2) >> 1, lb << 8); break;
        case 6:  b2 = sine_i(p2 + f); fb2 = fb1; fb1 = b2; a = sine_i(pa);
                 b1 = sine_i(p1 + digital_mod(a, la));
                 c = sine_i(pc + digital_mod((b1 + b2) >> 1, lb)); x = c; y = b1; break;
        default: b2 = sine_i(p2 + f); fb2 = fb1; fb1 = b2; a = sine_i(pa); b1 = sine_i(p1); c = sine_i(pc);
                 x = (c + mulq15(a, la << 8)) >> 1; y = mulq15((b1 + b2) >> 1, lb << 8); break;
        }
        pc += ic;
        pa += ia;
        p1 += ib;
        p2 += ib;
        out[i] += mulq15(mulq15(mulq15(x, gx) + mulq15(y, gy), amp_at(m, i)), VOICE_FS);
    }
    v->ph[0] = pc;
    v->ph[1] = pa;
    v->ph[2] = p1;
    v->s[7] = (int32_t)p2;
    v->s[5] = fb1;
    v->s[6] = fb2;
}
/* TONE's names for EDIT 2..8 (the same ranges as the classic ones); "-" for the classic routings' EDIT 8 */
static const param_desc_t DT_DESC[7] = {
    PN("C", F_ENUM, 0, 14, 1, N_RATIO), PN("A", F_ENUM, 0, 14, 1, N_RATIO), PN("B", F_ENUM, 0, 14, 1, N_RATIO),
    PU("A LEV", F_PCT, 0, 127, 60, 0), PU("B LEV", F_PCT, 0, 127, 60, 0), PU("FDBK", F_PCT, 0, 127, 0, 0),
    PU("MIX", F_BIPCT, -64, 63, 0, 0)};
static const param_desc_t DT_NONE = PU("-", F_INT, -64, 63, 0, 0);
static const param_desc_t *digital_desc(const track_t *t, uint32_t k)
{
    if (t->p[P_E0] >= 8)
        return k ? &DT_DESC[k - 1u] : 0;
    return k == 7u ? &DT_NONE : 0;
}
#endif

static HOT void digital_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t alg = (uint32_t)p[P_E0] & 7u, i;
    uint32_t i1 = m->inc;
#if FELUCCA_DIG_TONE
    if (p[P_E0] >= 8) {                                         /* TONE (its own code in flash: FAR) */
        FAR(dig_tone_render)(t, v, out, n, m);
        return;
    }
#endif
    uint32_t i2 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E1] % 15]), i3 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E2] % 15]);
    uint32_t i4 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E3] % 15]);
    int32_t me, idx, fb = p[P_E6], fb1, fb2;                 /* fb1, fb2: op 4 feedback history */
    uint32_t ph0, ph1, ph2, ph4;
    /* modulator envelope: decays (MODDEC) towards 25 %, per block */
    v->s[4] += mulq16((1 << 22) - v->s[4], ENV_EXP[p[P_E5] & 127]);
    me = v->s[4] >> 9;                                         /* Q15 */
    idx = (p[P_E4] * me) >> 15;                                 /* 0..127 */
    idx = clamp(idx + ((m->cutoff + m->shape - (64 << 8)) >> 8) + (v->vel - 96) / 4, 0, 127);
    ph0 = v->ph[0];                                             /* state in locals: out[] may alias v->s[] */
    ph1 = v->ph[1];
    ph2 = v->ph[2];
    ph4 = (uint32_t)v->s[7];
    fb1 = v->s[5];
    fb2 = v->s[6];
    for (i = 0; i < n; i++) {
        int32_t o1, o2, o3, o4, s;
        o4 = sine_i(FELUCCA_SKIP && !fb ? ph4 : ph4 + digital_mod((fb1 + fb2) >> 1, fb));   /* (FB 0: no feedback term) */
        fb2 = fb1;
        fb1 = o4;
        switch (alg) {
        case 0:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i((ph1 + digital_mod(o3, idx)));
            s = sine_i((ph0 + digital_mod(o2, idx)));
            break;
        case 1:
            o3 = sine_i(ph2);
            o2 = sine_i((ph1 + digital_mod((o3 + o4) >> 1, idx)));
            s = sine_i((ph0 + digital_mod(o2, idx)));
            break;
        case 2:
            o3 = sine_i(ph2);
            o2 = sine_i((ph1 + digital_mod(o3, idx)));
            s = sine_i((ph0 + digital_mod((o2 + o4) >> 1, idx)));
            break;
        case 3:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            s = sine_i((ph0 + digital_mod((o2 + o3) >> 1, idx)));
            break;
        case 4:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            o1 = sine_i((ph0 + digital_mod(o2, idx)));
            s = (o1 + o3) >> 1;
            break;
        case 5:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i((ph1 + digital_mod(o4, idx)));
            o1 = sine_i((ph0 + digital_mod(o4, idx)));
            s = mulq15(o1 + o2 + o3, 10923);           /* / 3 without a divide */
            break;
        case 6:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            o1 = sine_i(ph0);
            s = mulq15(o1 + o2 + o3, 10923);           /* / 3 without a divide */
            break;
        default:
            o3 = sine_i(ph2);
            o2 = sine_i(ph1);
            o1 = sine_i(ph0);
            s = (o1 + o2 + o3 + o4) >> 2;
            break;
        }
        ph0 += i1;
        ph1 += i2;
        ph2 += i3;
        ph4 += i4;
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS);   /* full-scale sines: 6 dB below the others */
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[7] = (int32_t)ph4;
    v->s[5] = fb1;
    v->s[6] = fb2;
}

static const preset_t DIGITAL_PRESETS[] = {
    /* ALG R2 R3 R4 INDEX MODDEC FDBK - ; ALG 5 (4 here) = two stacks: 2 -> 1 and 4 -> 3 (the tine) */
    {"RHODES", {4, 1, 1, 14, 26, 35, 0, 0}, {0, 85, 35, 55}, 0, 0, FX(0, 30, 12, 26), XP(P_LEG_AMP + 1, 26, P_LRATE + 1, 84)},
    {"DX RHODES", {4, 1, 1, 14, 85, 45, 0, 0}, {0, 82, 40, 55}, 0, 0, FX(0, 45, 20, 30)},
    {"WURLI", {4, 1, 2, 3, 62, 35, 8, 0}, {0, 75, 30, 45}, 0, 0, FX(13, 20, 10, 20), XP(P_LEG_AMP + 1, 16, P_LRATE + 1, 89)},
    {"CLAV", {3, 1, 3, 5, 90, 25, 20, 0}, {0, 58, 34, 18}, 0, 0, FX(18, 10, 6, 10)},
    /* house piano: bright, fast hammer, the tine stack for the attack */
    {"M1 PIANO", {4, 1, 2, 13, 74, 30, 12, 0}, {0, 86, 30, 48}, 0, 0, FX(0, 24, 14, 26)},
    /* afro house / amapiano: round keys with a bell on top */
    {"AFRO KEYS", {4, 1, 1, 7, 40, 50, 0, 0}, {0, 82, 40, 60}, 0, 0, FX(0, 30, 26, 36)},
    {"TRAP BELL", {4, 8, 1, 4, 80, 70, 0, 0}, {0, 92, 0, 70}, 0, 0, FX(0, 0, 30, 40)},
    {"MUSIC BOX", {4, 4, 3, 9, 45, 75, 0, 0}, {0, 90, 0, 75}, 0, 0, FX(0, 10, 32, 45)},
    {"MARIMBA", {4, 4, 1, 1, 60, 30, 0, 0}, {0, 80, 0, 60}, 0, 0, FX(0, 0, 22, 30)},
    {"KALIMBA", {4, 4, 1, 6, 52, 24, 0, 0}, {0, 78, 0, 58}, 0, 0, FX(0, 0, 26, 38)},
    /* pluggnb / cloud rap: a soft bell, lots of space */
    {"PLUGG BELL", {4, 3, 1, 4, 56, 60, 0, 0}, {0, 90, 0, 82}, 0, 0, FX(0, 22, 36, 56)},
    {"GLASS PAD", {4, 2, 1, 5, 34, 100, 0, 0}, {62, 92, 112, 92}, 0, 0, FX(0, 50, 26, 62), XP(P_LEG_PIT + 1, 3, P_LRATE + 1, 40)},
    {"FM BASS", {0, 1, 1, 1, 34, 40, 8, 0}, {0, 60, 70, 25}, 0, 1, FX(0, 0, 0, 6), XP(P_GLIDE + 1, 36, P_TRANS + 1, -24)},
#if FELUCCA_DIG_TONE
    /* TONE: {ALG T1..T8 = 8..15, C, A, B, A LEV, B LEV, FDBK, MIX} */
    {"TONE KEYS", {8, 1, 1, 13, 40, 70, 0, -20}, {0, 84, 36, 56}, 0, 0, FX(0, 30, 14, 28)},
    {"TONE BELL", {9, 1, 6, 9, 50, 90, 10, 20}, {0, 92, 0, 74}, 0, 0, FX(0, 10, 30, 50)},
    {"TONE BASS", {11, 1, 1, 2, 70, 60, 30, -30}, {0, 62, 70, 24}, 0, 1, FX(10, 0, 0, 8), XP(P_TRANS + 1, -24)},
    {"TONE PAD", {13, 1, 2, 3, 30, 80, 0, 30}, {70, 96, 112, 90}, 0, 0, FX(0, 50, 22, 64)},
    {"TONE ORGAN", {15, 1, 2, 4, 90, 80, 20, 0}, {0, 64, 127, 20}, 0, 0, FX(0, 30, 0, 30)},
    {"TONE GROWL", {10, 1, 2, 1, 80, 100, 60, 40}, {0, 70, 90, 30}, 0, 1, FX(20, 0, 10, 15)},
#endif
};

static const engine_t ENG_DIGITAL = {
    "DIGITAL", {"OPS", "MOD"},
    {
        PN("ALG", F_ENUM, 0, 7 + 8 * FELUCCA_DIG_TONE, 0, N_FMALG),
        PN("R2", F_ENUM, 0, 14, 1, N_RATIO),
        PN("R3", F_ENUM, 0, 14, 1, N_RATIO),
        PN("R4", F_ENUM, 0, 14, 1, N_RATIO),
        PU("IDX", F_PCT, 0, 127, 60, 0),
        PU("MDEC", F_TIME, 0, 127, 60, 0),
        PU("FB", F_PCT, 0, 127, 0, 0),
#if FELUCCA_DIG_TONE
        PU("MIX", F_BIPCT, -64, 63, 0, 0),                     /* (TONE's; the classic routings show "-": digital_desc) */
#else
        PU("-", F_INT, 0, 0, 0, 0),
#endif
    },
    DIGITAL_PRESETS, sizeof(DIGITAL_PRESETS) / sizeof(DIGITAL_PRESETS[0]), -1, digital_note_on, digital_render,
    0xFD20, {P_E4, P_E5, P_E6, P_REL},
#if FELUCCA_DIG_TONE
    0, 0, digital_desc,
#endif
};
