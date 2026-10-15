/* SPDX-License-Identifier: GPL-3.0-only */
/* ENV DEST's and ENV2 DEST's two slots (core.h P_N1DA .., P_N2DA ..; 2026-10-14) and the depths they replaced: the
 * conversions and the stored form a project and a user preset keep. Header only (params.c, upreset.c's host test), after
 * lfo_store.h (lfo_cl).
 *
 * Before the slots ENV was wired to the amplitude and had a depth on FLT and SHP (P_ED_FLT P_ED_SHP, -64..63; ENV DEST
 * PIT is the pitch drop, not ENV: it stays, P_ED_PIT). ENV2 (ANALOG's) had five: FLT (P_A2FENV) PIT SHP OSC2 SDTN
 * (P_A2EPIT ..). They come in as slots that sound the same: ENV's A on AMP at 127, B on the heavier of FLT and SHP at AMT
 * 2 x the depth; ENV2's two heaviest (|depth|, a tie: the first) in their order. A depth left out is counted (lost).
 *
 * The stored form, in those same words of a track's stored values (a project, a user preset): the depths themselves
 * while they read back as these very slots (a project of before saves byte for byte; a new sound, ENV on AMP alone, is
 * ENV DEST at 0 0 as before); else tagged: ENV DEST FLT = ENV_TAG | DEST A | DEST B << 6, SHP = AMT A's byte | AMT B's
 * byte << 8; ENV2's FLT = ENV_TAG | DEST A | DEST B << 6, its PIT = AMT A, SHP = AMT B, OSC2 SDTN 0. A destination this
 * build lacks: OFF. At run time those words are 0 (nothing reads them: the slots act) */
#ifndef FELUCCA_ENV_STORE_H
#define FELUCCA_ENV_STORE_H
#define ENV_TAG 0x5000u                                /* (out of a depth's range, -64..63: high nibble 0 or F) */
#define ENV_TAGM 0xF000u
#define ENV_AMP_FULL 127                               /* AMP at 127: the envelope itself (ENV's default) */
_Static_assert(LT_N <= 64, "a DEST in 6 bits (the tagged form)");

/* ENV's depths f (FLT), sh (SHP) -> slots s[4] (DEST A, AMT A, DEST B, AMT B); -> the depths left out (0 or 1) */
static inline uint32_t env1_from_depths(int32_t f, int32_t sh, int16_t *s)
{
    f = lfo_cl(f, -64, 63), sh = lfo_cl(sh, -64, 63);
    s[0] = LT_AMP, s[1] = ENV_AMP_FULL, s[2] = LT_OFF, s[3] = 0;
    if (f || sh) {
        int32_t useflt = (f < 0 ? -f : f) >= (sh < 0 ? -sh : sh);
        s[2] = useflt ? LT_FLT : LT_SHP, s[3] = (int16_t)(2 * (useflt ? f : sh));
    }
    return f && sh;
}
/* ENV2's depths a[5] (FLT PIT SHP OSC2 SDTN) -> slots s[4]; -> the depths left out */
static const uint8_t ENV2_LEG_DEST[5] = {LT_FLT, LT_PIT, LT_SHP, LT_OSC2, LT_SDTN};
static inline uint32_t env2_from_depths(const int16_t *d, int16_t *s)
{
    int32_t v[5], w[5];
    uint32_t i, a = 5u, b = 5u, lost = 0;
    for (i = 0; i < 5u; i++)
        v[i] = lfo_cl(d[i], -64, 63), w[i] = v[i] < 0 ? -v[i] : v[i];
    for (i = 0; i < 5u; i++)
        if (w[i] && (a == 5u || w[i] > w[a]))
            a = i;
    for (i = 0; i < 5u; i++)
        if (i != a && w[i] && (b == 5u || w[i] > w[b]))
            b = i;
    for (i = 0; i < 5u; i++)
        lost += i != a && i != b && v[i] != 0;
    if (b < a) {
        uint32_t x = a;
        a = b, b = x;
    }
    s[0] = s[2] = LT_OFF, s[1] = s[3] = 0;
    if (a < 5u)
        s[0] = ENV2_LEG_DEST[a], s[1] = (int16_t)(2 * v[a]);
    if (b < 5u)
        s[2] = ENV2_LEG_DEST[b], s[3] = (int16_t)(2 * v[b]);
    return lost;
}
/* a slot pair read from outside, inside its ranges (a destination this build lacks: OFF) */
static inline void env_slots_fix(int16_t *s)
{
    uint32_t k;
    for (k = 0; k < 2u; k++) {
        s[2u * k + 1u] = (int16_t)lfo_cl(s[2u * k + 1u], LFO_AMT_LO, LFO_AMT_HI);
        if ((uint32_t)s[2u * k] >= LT_NB)
            s[2u * k] = LT_OFF, s[2u * k + 1u] = 0;
    }
}
static inline uint32_t env_tagged(int32_t w) { return ((uint32_t)(uint16_t)w & ENV_TAGM) == ENV_TAG; }
static inline int32_t env_tag(const int16_t *s)
{
    return (int32_t)(int16_t)(ENV_TAG | ((uint32_t)s[0] & 63u) | ((uint32_t)s[2] & 63u) << 6);
}
static inline void env_untag(int32_t w, int32_t amts, int16_t *s)   /* (amts: AMT A's byte | AMT B's << 8) */
{
    s[0] = (int16_t)((uint32_t)w & 63u), s[2] = (int16_t)((uint32_t)w >> 6 & 63u);
    s[1] = (int16_t)(int8_t)(uint8_t)((uint32_t)amts & 0xFFu), s[3] = (int16_t)(int8_t)(uint8_t)((uint32_t)amts >> 8 & 0xFFu);
    env_slots_fix(s);
}

/* a track's values v (P_COUNT, today's layout) with ENV DEST's and ENV2's words in their stored form (the depths, or
 * tagged) -> the slots in v (P_N1DA .., P_N2DA ..), those words 0; -> the depths left out (two kept) */
static inline uint32_t env_load(int16_t *v)
{
    uint32_t lost = 0;
    if (env_tagged(v[P_ED_FLT]))
        env_untag(v[P_ED_FLT], v[P_ED_SHP], &v[P_N1DA]);
    else
        lost += env1_from_depths(v[P_ED_FLT], v[P_ED_SHP], &v[P_N1DA]);
    v[P_ED_FLT] = v[P_ED_SHP] = 0;
#if FELUCCA_ANALOG2
    if (env_tagged(v[P_A2FENV])) {
        env_untag(v[P_A2FENV], (uint32_t)(uint8_t)v[P_A2EPIT] | (uint32_t)(uint8_t)v[P_A2ESHP] << 8, &v[P_N2DA]);
    } else {
        int16_t d[5];
        d[0] = v[P_A2FENV], d[1] = v[P_A2EPIT], d[2] = v[P_A2ESHP], d[3] = v[P_A2EOS2], d[4] = v[P_A2ESDT];
        lost += env2_from_depths(d, &v[P_N2DA]);
    }
    v[P_A2FENV] = v[P_A2EPIT] = v[P_A2ESHP] = v[P_A2EOS2] = v[P_A2ESDT] = 0;
#endif
    return lost;
}
/* the slots in v -> ENV DEST's and ENV2's words in their stored form (the depths when they read back as these slots
 * exactly, else tagged) */
static inline void env_store(int16_t *v)
{
    int16_t back[4];
    const int16_t *s = &v[P_N1DA];
    int32_t f = 0, sh = 0;
    uint32_t k, ok = 1;
    for (k = 0; k < 2u; k++) {                         /* the depths they could be */
        if (s[2u * k] == LT_FLT)
            f += s[2u * k + 1u] / 2;
        else if (s[2u * k] == LT_SHP)
            sh += s[2u * k + 1u] / 2;
    }
    (void)env1_from_depths(f, sh, back);
    for (k = 0; k < 4u; k++)
        ok &= back[k] == s[k];
    if (ok) {
        v[P_ED_FLT] = (int16_t)f, v[P_ED_SHP] = (int16_t)sh;
    } else {
        v[P_ED_FLT] = (int16_t)env_tag(s);
        v[P_ED_SHP] = (int16_t)(uint16_t)(((uint32_t)s[1] & 0xFFu) | ((uint32_t)s[3] & 0xFFu) << 8);
    }
#if FELUCCA_ANALOG2
    {
        int16_t d[5] = {0, 0, 0, 0, 0};
        uint32_t i;
        s = &v[P_N2DA];
        for (k = 0; k < 2u; k++)
            for (i = 0; i < 5u; i++)
                if (s[2u * k] == ENV2_LEG_DEST[i])
                    d[i] = (int16_t)lfo_cl(d[i] + s[2u * k + 1u] / 2, -64, 63);
        (void)env2_from_depths(d, back);
        for (ok = 1, k = 0; k < 4u; k++)
            ok &= back[k] == s[k];
        if (ok) {
            v[P_A2FENV] = d[0], v[P_A2EPIT] = d[1], v[P_A2ESHP] = d[2], v[P_A2EOS2] = d[3], v[P_A2ESDT] = d[4];
        } else {
            v[P_A2FENV] = (int16_t)env_tag(s);
            v[P_A2EPIT] = (int16_t)(int8_t)s[1], v[P_A2ESHP] = (int16_t)(int8_t)s[3], v[P_A2EOS2] = v[P_A2ESDT] = 0;
        }
    }
#endif
}
#endif
