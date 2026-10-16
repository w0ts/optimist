/* SPDX-License-Identifier: GPL-3.0-only */
/* ENV 1 MOD's depths and ENV2 DEST's two slots (core.h P_E1PIT .., P_N2DA ..) and the forms they had. Header only
 * (params.c, upreset.c's host test), after lfo_store.h (lfo_cl) and env1_cards.h (the engines' cards).
 *
 * ENV 1: before 2026-10-14 it was wired to the amplitude and had a depth on FLT and SHP (P_ED_FLT P_ED_SHP, -64..63;
 * ENV DEST PIT is the pitch drop, not ENV: it stays, DROP). From 2026-10-14 to 16 it had two free slots (ENV DEST, the
 * ids P_E1PIT .. P_E1Z were its DEST A AMT A DEST B AMT B: a project's modulation record MREC_TAG 0xD2, user presets
 * UP_VER 3) with the depths' words at 0. Since 2026-10-16 it is the engine's own envelope again (env1_cards.h): the
 * amplitude always, FLT SHP in their words, PIT and the moves in P_E1PIT .. P_E1Z. A file of before 2026-10-14 is read
 * as it is; one of the slots folds them (env1_fold).
 * ENV2 (ANALOG's) had five depths: FLT (P_A2FENV) PIT SHP OSC2 SDTN (P_A2EPIT ..). A project, a preset, a user preset of
 * before comes in as slots that sound the same (env_load): the two heaviest (|depth|, a tie: the first) in their order.
 * A depth left out is counted (lost). The slots are stored as values of their own (project.c's modulation record,
 * upreset.c); those words are 0 at run time and in what is saved. env_store writes the slots back as the depths they
 * are, as far as they go (a SLOOP bank's record, the tests) */
#ifndef FELUCCA_ENV_STORE_H
#define FELUCCA_ENV_STORE_H
#include "env1_cards.h"                                /* (the engines' cards: by slot, no engine table needed) */
#define ENV_AMP_FULL 127                               /* AMP at 127: the envelope itself (ENV2's gain, env_gain) */
#ifndef UP_HOST
static uint8_t env1_drop;                              /* the parts (a bit each) whose ENV DEST slots of before the last load
                                                        * could not keep (env1_fold: TOOLS > MISS "ENV DEST T2") */
#endif

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
/* ENV2's DEST A on engine slot e when it does nothing (OFF, or at 0): the engine's default (the filter, or its
 * nearest: env1_cards.h), at 0. Every sound has the filter one turn away (the user, 2026-10-16) */
static __attribute__((noinline)) void env2_dest_def(int16_t *v, uint32_t e)
{
#if FELUCCA_ANALOG2
    if (v[P_N2DA] == LT_OFF || !v[P_N2AA])
        v[P_N2DA] = (int16_t)env2_def_dest(e), v[P_N2AA] = 0;
#else
    (void)v, (void)e;
#endif
}
/* a track's values v (P_COUNT, today's layout) with ENV2's words holding depths of before -> its slots in v (P_N2DA ..),
 * those words 0; -> the depths left out (two kept). ENV 1's words are its depths as they are */
static inline uint32_t env_load(int16_t *v)
{
    uint32_t lost = 0;
#if FELUCCA_ANALOG2
    {
        int16_t d[5];
        d[0] = v[P_A2FENV], d[1] = v[P_A2EPIT], d[2] = v[P_A2ESHP], d[3] = v[P_A2EOS2], d[4] = v[P_A2ESDT];
        lost += env2_from_depths(d, &v[P_N2DA]);
    }
    v[P_A2FENV] = v[P_A2EPIT] = v[P_A2ESHP] = v[P_A2EOS2] = v[P_A2ESDT] = 0;
#else
    (void)v;
#endif
    return lost;
}
/* ENV2's slots in v -> its words as the depths they are (ENV2's five; another destination, an odd AMT halved toward 0,
 * AMP left out); -> 1 when they read back as other slots (not all of them said so). ENV 1's are depths already */
static inline uint32_t env_store(int16_t *v)
{
    uint32_t bad = 0;
#if FELUCCA_ANALOG2
    int16_t back[4], d[5] = {0, 0, 0, 0, 0};
    const int16_t *s = &v[P_N2DA];
    uint32_t i, k;
    for (k = 0; k < 2u; k++)
        for (i = 0; i < 5u; i++)
            if (s[2u * k] == ENV2_LEG_DEST[i])
                d[i] = (int16_t)lfo_cl(d[i] + s[2u * k + 1u] / 2, -64, 63);
    (void)env2_from_depths(d, back);
    for (k = 0; k < 4u; k++)
        bad |= back[k] != s[k];
    v[P_A2FENV] = d[0], v[P_A2EPIT] = d[1], v[P_A2ESHP] = d[2], v[P_A2EOS2] = d[3], v[P_A2ESDT] = d[4];
#else
    (void)v;
#endif
    return bad;
}

/* ENV DEST's two slots of 2026-10-14 .. 16 (DEST A AMT A DEST B AMT B, in v's P_E1PIT .. P_E1Z as they were stored) onto
 * ENV 1 MOD in v, those four ids then its PIT and moves at 0: AMP is wired (an AMT of 127 its default; another cannot be
 * kept), FLT SHP PIT at AMT / 2 into their depths (2 x the depth of before: the same sound but an odd AMT's LSB);
 * anything else to ENV2's slots when both do nothing (ENV's ATK DEC SUS REL copied into ATK2 DEC2 SUS2 REL2, so it moves
 * as it did). -> the slots that could not be kept (lost) */
static __attribute__((noinline)) uint32_t env1_fold(int16_t *v)
{
    static const uint8_t TO[3] = {P_E1PIT, P_ED_FLT, P_ED_SHP};   /* (LT_PIT LT_FLT LT_SHP) */
    int16_t s[4];
    uint32_t k, n = 0, lost = 0;
    for (k = 0; k < 4u; k++)
        s[k] = v[P_E1PIT + k], v[P_E1PIT + k] = 0;
    for (k = 0; k < 4u; k += 2u) {
        uint32_t d = (uint32_t)s[k];
        int32_t a = s[k + 1u];
        if (d == LT_OFF || d >= LT_NB || !a)
            continue;
        if (d == LT_AMP)
            lost += a != ENV_AMP_FULL;                  /* (ENV 1 is the amplitude itself) */
        else if (d <= LT_SHP)
            v[TO[d - 1u]] = (int16_t)lfo_cl(v[TO[d - 1u]] + a / 2, -64, 63);
        else
            s[2u * n] = (int16_t)d, s[2u * n + 1u] = (int16_t)a, n++;   /* (kept for ENV2, in place) */
    }
    if (!n)
        return lost;
#if FELUCCA_ANALOG2
    if ((v[P_N2DA] == LT_OFF || !v[P_N2AA]) && (v[P_N2DB] == LT_OFF || !v[P_N2AB])) {
        v[P_A2FATK] = v[P_ATK], v[P_A2FDEC] = v[P_DEC], v[P_A2ESUS] = v[P_SUS], v[P_A2EREL] = v[P_REL];
        for (k = 0; k < 4u; k++)
            v[P_N2DA + k] = k < 2u * n ? s[k] : 0;      /* (LT_OFF: 0) */
        return lost;
    }
#endif
    return lost + n;
}
#endif
