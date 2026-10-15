/* SPDX-License-Identifier: GPL-3.0-only */
/* ENV DEST's and ENV2 DEST's two slots (core.h P_N1DA .., P_N2DA ..; 2026-10-14) and the depths they replaced. Header
 * only (params.c, upreset.c's host test), after lfo_store.h (lfo_cl).
 *
 * Before the slots ENV was wired to the amplitude and had a depth on FLT and SHP (P_ED_FLT P_ED_SHP, -64..63; ENV DEST
 * PIT is the pitch drop, not ENV: it stays, DROP). ENV2 (ANALOG's) had five: FLT (P_A2FENV) PIT SHP OSC2 SDTN (P_A2EPIT
 * ..). A project, a preset, a user preset of before comes in as slots that sound the same (env_load): ENV's A on AMP at
 * 127, B on the heavier of FLT and SHP at AMT 2 x the depth; ENV2's two heaviest (|depth|, a tie: the first) in their
 * order. A depth left out is counted (lost). Today the slots are stored as values of their own (project.c's modulation
 * record, upreset.c UP_VER 3); those words are 0 at run time and in what is saved. env_store writes the slots back as
 * the depths they are, as far as they go (a SLOOP bank's record, the tests) */
#ifndef FELUCCA_ENV_STORE_H
#define FELUCCA_ENV_STORE_H
#define ENV_AMP_FULL 127                               /* AMP at 127: the envelope itself (ENV's default) */

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
/* a track's values v (P_COUNT, today's layout) with ENV DEST's and ENV2's words holding depths of before -> the slots
 * in v (P_N1DA .., P_N2DA ..), those words 0; -> the depths left out (two kept) */
static inline uint32_t env_load(int16_t *v)
{
    uint32_t lost = env1_from_depths(v[P_ED_FLT], v[P_ED_SHP], &v[P_N1DA]);
    v[P_ED_FLT] = v[P_ED_SHP] = 0;
#if FELUCCA_ANALOG2
    {
        int16_t d[5];
        d[0] = v[P_A2FENV], d[1] = v[P_A2EPIT], d[2] = v[P_A2ESHP], d[3] = v[P_A2EOS2], d[4] = v[P_A2ESDT];
        lost += env2_from_depths(d, &v[P_N2DA]);
    }
    v[P_A2FENV] = v[P_A2EPIT] = v[P_A2ESHP] = v[P_A2EOS2] = v[P_A2ESDT] = 0;
#endif
    return lost;
}
/* the slots in v -> ENV DEST's and ENV2's words as the depths they are (FLT SHP, ENV2's five; another destination, an
 * odd AMT halved toward 0, AMP left out); -> 1 when they read back as other slots (not all of them said so) */
static inline uint32_t env_store(int16_t *v)
{
    int16_t back[4];
    const int16_t *s = &v[P_N1DA];
    int32_t f = 0, sh = 0;
    uint32_t k, bad = 0;
    for (k = 0; k < 2u; k++) {
        if (s[2u * k] == LT_FLT)
            f += s[2u * k + 1u] / 2;
        else if (s[2u * k] == LT_SHP)
            sh += s[2u * k + 1u] / 2;
    }
    (void)env1_from_depths(f, sh, back);
    for (k = 0; k < 4u; k++)
        bad |= back[k] != s[k];
    v[P_ED_FLT] = (int16_t)lfo_cl(f, -64, 63), v[P_ED_SHP] = (int16_t)lfo_cl(sh, -64, 63);
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
        for (k = 0; k < 4u; k++)
            bad |= back[k] != s[k];
        v[P_A2FENV] = d[0], v[P_A2EPIT] = d[1], v[P_A2ESHP] = d[2], v[P_A2EOS2] = d[3], v[P_A2ESDT] = d[4];
    }
#endif
    return bad;
}
#endif
