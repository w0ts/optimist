/* SPDX-License-Identifier: GPL-3.0-only */
/* LFO DEST's two slots (core.h LT_*) and the four depths PIT FLT SHP AMP they replaced (2026-10): the conversions and
 * the stored form a project, a user preset and a SLOOP file keep. Header only (params.c, upreset.c: its host test has
 * no params.c), needs core.h */
#ifndef FELUCCA_LFO_STORE_H
#define FELUCCA_LFO_STORE_H
static inline int32_t lfo_cl(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* ---- LFO DEST's slots (core.h LT_*): from and to the four depths PIT FLT SHP AMP of before (their stored form) ---- */
/* the depths d[4] (PIT FLT SHP -64..63, AMP 0..127) -> slots s[4] (DEST A, AMT A, DEST B, AMT B): the two of the most
 * weight (|depth|, AMP's at half: its range is twice theirs; a tie: the first), in the depths' order, AMT 2 x depth (AMP:
 * the depth), so they sound exactly as before. -> the depths left out (not 0) */
static inline uint32_t lfo_from_depths(const int16_t *d, int16_t *s)
{
    int32_t v[4], w[4];
    uint32_t i, a = 4u, b = 4u, lost = 0;
    for (i = 0; i < 4u; i++) {
        v[i] = i == 3u ? lfo_cl(d[i], 0, 127) : lfo_cl(d[i], -64, 63);
        w[i] = i == 3u ? (v[i] + 1) >> 1 : v[i] < 0 ? -v[i] : v[i];
    }
    for (i = 0; i < 4u; i++)
        if (w[i] && (a == 4u || w[i] > w[a]))
            a = i;
    for (i = 0; i < 4u; i++)
        if (i != a && w[i] && (b == 4u || w[i] > w[b]))
            b = i;
    for (i = 0; i < 4u; i++)
        lost += i != a && i != b && v[i] != 0;
    if (b < a) {
        uint32_t x = a;
        a = b, b = x;
    }
    s[0] = s[1] = s[2] = s[3] = 0;
    if (a < 4u)
        s[0] = (int16_t)(LT_PIT + a), s[1] = (int16_t)(a == 3u ? v[a] : 2 * v[a]);
    if (b < 4u)
        s[2] = (int16_t)(LT_PIT + b), s[3] = (int16_t)(b == 3u ? v[b] : 2 * v[b]);
    return lost;
}
#define LFO_NOSND 1u                                   /* lfo_depths: not heard the same through depths */
#define LFO_NOFORM 2u                                  /* .. and not this layout (the depths read back otherwise) */
/* slots s[4] -> the depths d[4] they are (best effort: a destination other than PIT FLT SHP AMP or AMP < 0 left out,
 * an odd AMT on PIT FLT SHP halved toward 0); -> 0 when the depths read back as these very slots, else LFO_NOSND (not
 * heard so: another destination, AMP < 0) | LFO_NOFORM */
static inline uint32_t lfo_depths(const int16_t *s, int16_t *d)
{
    int16_t back[4];
    uint32_t k, r = 0;
    d[0] = d[1] = d[2] = d[3] = 0;
    for (k = 0; k < 2u; k++) {
        int32_t de = s[2u * k], a = s[2u * k + 1u], i = de - LT_PIT;
        if (de == LT_OFF || !a)
            continue;
        if (!LT_CLASSIC(de) || (de == LT_AMP && a < 0)) {
            r |= LFO_NOSND;
            continue;
        }
        d[i] = (int16_t)(de == LT_AMP ? lfo_cl(d[i] + a, 0, 127) : lfo_cl(d[i] + a / 2, -64, 63));
    }
    (void)lfo_from_depths(d, back);
    for (k = 0; k < 4u; k++)
        if (back[k] != s[k])
            r |= LFO_NOFORM;
    return r;
}
#define LFO_TAG 0x5A00u                                /* (out of every depth's range: -64..127) */
/* a part's LFO slots s[4] -> its four stored words w (P_LEG_PIT ..): the depths of before when they are these slots
 * exactly and LFO 2 keeps nothing there (spill 0): a project or a preset of before stays byte for byte, an older build
 * reads it as it sounds; else DEST A | LFO_TAG, AMT A, DEST B | LFO_TAG, AMT B's byte | spill << 8 (LFO 2's AMT B:
 * project.c l2_pack) */
static inline void lfo1_store(int16_t *w, const int16_t *s, int32_t spill)
{
    if (!spill && !lfo_depths(s, w))
        return;
    w[0] = (int16_t)(LFO_TAG | ((uint32_t)s[0] & 0xFFu));
    w[1] = s[1];
    w[2] = (int16_t)(LFO_TAG | ((uint32_t)s[2] & 0xFFu));
    w[3] = (int16_t)(((uint32_t)s[3] & 0xFFu) | ((uint32_t)spill & 0xFFu) << 8);
}
/* its four stored words w -> slots s[4] (inside their ranges; a destination this build lacks: OFF), *spill (0 when none);
 * -> the depths of before left out (only two kept) */
static inline uint32_t lfo1_load(const int16_t *w, int16_t *s, int32_t *spill)
{
    uint32_t k;
    *spill = 0;
    if (((uint16_t)w[0] & 0xFF00u) != LFO_TAG || ((uint16_t)w[2] & 0xFF00u) != LFO_TAG)
        return lfo_from_depths(w, s);
    s[1] = (int16_t)lfo_cl(w[1], LFO_AMT_LO, LFO_AMT_HI);
    s[3] = (int16_t)(int8_t)(uint8_t)w[3];
    *spill = (int8_t)(uint8_t)((uint16_t)w[3] >> 8);
    for (k = 0; k < 2u; k++) {
        s[2u * k] = (int16_t)((uint16_t)w[2u * k] & 0xFFu);
        if (s[2u * k] >= LT_NB)
            s[2u * k] = LT_OFF, s[2u * k + 1u] = 0;
    }
    return 0;
}

#endif
