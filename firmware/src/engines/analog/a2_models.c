/* SPDX-License-Identifier: GPL-3.0-only */
/* ANALOG 2's filter models (FLT 2 > MODEL; eng_analog2.c calls a2m_block). Float: like ACID's (acid/acid_dsp.c) this file
 * is its own translation unit, built with the FPU flags (tools/build.py: -mcpu=r3 -mfprev1 -ffp-contract=off) while the
 * rest of the firmware is integer-only; the host tests include it directly. FLT 2's MODEL and FTYP are one stored value,
 * P_A2FTYP = model * A2M_MODES + mode (params.c a2f_view): model 0 (SVF) is ANALOG 2's own filter, its modes LP12 LP24
 * BP HP as they were, so every stored sound plays as it did. A model's states sit in the voice's s[0..1], s[4..5] as the
 * bits of floats, s[7] the model they belong to (another one, or the SVF's integers there: cleared first):
 *   LADR  a transistor (Moog-style) 4-pole ladder: the zero-delay feedback solved linearly, a tanh at its input
 *         (drive grits it); the bass thins as RES rises (the passband 1 / (1 + k)); RES 127 rings (k 4.6)
 *   OTA   an OTA (IR3109, Juno-style) 4-pole: the input stays clean, the passband made up as RES rises
 *         (1 + k / 2), the tanh on the resonance's path alone: creamy, its self-oscillation round (k 4.2)
 *   DIOD  a diode (303-style) ladder: four coupled stages, each loading the one before, solved as a 4 x 4 system a
 *         segment, a tanh at its input: squelchy, the resonance thin and vocal (k 0..22)
 *   MS20  a Sallen-Key (MS-20-style) 2-pole: its band fed back through a tanh: it screams at high RES (k 0..2.3)
 * FTYP on LADR and OTA: the Oberheim Xpander's mixes of the ladder's input u and its stages y1..y4: LP12 y2, LP24 y4,
 * BP 4 (y2 - 2 y3 + y4), HP u - 2 y1 + y2, NOTCH u - 2 y1 + 2 y2. DIOD: its own (A2M_DMIX). MS20: its LP, BP, HP and
 * NOTCH, LP24 a further pole after its LP (18 dB). The coefficients twice a block, at each half's end cutoff. b[] in and
 * out as ANALOG 2's other filters (half scale; the soft knee after, a2_out). Each model a builder item (FELUCCA_A2_LADR
 * ..): a model not built is no entry of A2M_MODEL (the engine plays the SVF in the same mode: A2M_BUILT) */
#include "a2_models.h"
#include "../../dsp/dsp_float.h"

#define A2M_FS 44100.f                                /* (core.h FS) */
#define A2M_OCT 9.0590148f                            /* log2(16000 / 30): the cutoff curve's octaves */

typedef union { int32_t i; float f; } a2m_w;   /* (a state slot: an int32 of the voice holding a float) */
static inline __attribute__((always_inline)) float a2m_ld(int32_t w)
{
    a2m_w u;
    u.i = w;
    return u.f;
}
static inline __attribute__((always_inline)) int32_t a2m_st(float f)
{
    a2m_w u;
    u.f = f;
    return u.i;
}

/* tanh, Pade (3, 2): +-1 from |x| 3 on (its value there) */
static inline __attribute__((always_inline)) float a2m_tanh(float x)
{
    float x2;
    if (x > 3.f)
        return 1.f;
    if (x < -3.f)
        return -1.f;
    x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

/* g = tan(pi fc / FS) at cut (0..127 << 8): ANALOG's cutoff curve (tools/gen_tables.py SVF_G: 30 Hz .. 16 kHz, at most
 * 0.45 FS), worked out exactly (twice a block) */
static float a2m_g(int32_t cut)
{
    float fc = 30.f * fm_exp2f((float)cut * (A2M_OCT / (127.f * 256.f)));
    return fm_tanf(FM_PI * fm_minf(fc, 0.45f * A2M_FS) * (1.f / A2M_FS));
}

/* the float in and out of b[] (half scale: 32768 = 1.0), the output bounded (a runaway never reaches the knee's
 * integer limits) */
#define A2M_IN(i) ((float)b[i] * (1.f / 32768.f))
#define A2M_OUT(i, y)                                                                                       \
    do {                                                                                                    \
        float o_ = (y);                                                                                     \
        o_ = o_ > 32.f ? 32.f : o_ < -32.f ? -32.f : o_;                                                    \
        b[i] = (int32_t)(o_ * 32768.f);                                                                     \
    } while (0)
/* a trapezoidal one-pole low-pass: z its state, G = g / (1 + g); y its output */
#define A2M_LP1(in, z, G, y)                                                                                \
    do {                                                                                                    \
        float v_ = ((in) - (z)) * (G);                                                                      \
        y = v_ + (z);                                                                                       \
        z = y + v_;                                                                                         \
    } while (0)

#if FELUCCA_A2_LADR || FELUCCA_A2_OTA
/* the Xpander's mixes of u, y1, y2, y3, y4 (LP12 LP24 BP HP NOTCH) */
static const float A2M_LMIX[A2M_MODES][5] = {
    {0.f, 0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 4.f, -8.f, 4.f},
    {1.f, -2.f, 1.f, 0.f, 0.f}, {1.f, -2.f, 2.f, 0.f, 0.f}};
#endif

/* the 4-pole ladders LADR (ota 0) and OTA (ota 1) over n samples: y4 = G^4 u + S (S what the states give), u = x - k y4
 * solved for u; LADR saturates u, OTA the feedback k tanh(y4) */
#if FELUCCA_A2_LADR || FELUCCA_A2_OTA
static inline __attribute__((always_inline)) void a2m_ladder(int32_t *b, float *z, float g, float k, const float *mx,
                                                            const int ota, uint32_t n)
{
    float G = g / (1.f + g), H = 1.f - G, G4 = G * G * G * G, inv = 1.f / (1.f + k * G4), cin = ota ? 1.f + 0.5f * k : 1.f;
    float z1 = z[0], z2 = z[1], z3 = z[2], z4 = z[3];
    float m0 = mx[0], m1 = mx[1], m2 = mx[2], m3 = mx[3], m4 = mx[4];
    uint32_t i;
    for (i = 0; i < n; i++) {
        float x = A2M_IN(i) * cin, S = H * (G * (G * (G * z1 + z2) + z3) + z4), u, y1, y2, y3, y4;
        if (ota)
            u = x - k * a2m_tanh(G4 * ((x - k * S) * inv) + S);
        else
            u = a2m_tanh((x - k * S) * inv);
        A2M_LP1(u, z1, G, y1);
        A2M_LP1(y1, z2, G, y2);
        A2M_LP1(y2, z3, G, y3);
        A2M_LP1(y3, z4, G, y4);
        A2M_OUT(i, m0 * u + m1 * y1 + m2 * y2 + m3 * y3 + m4 * y4);
    }
    z[0] = z1, z[1] = z2, z[2] = z3, z[3] = z4;
}
#endif
#if FELUCCA_A2_LADR
static void a2m_ladr(int32_t *b, float *z, float g, int32_t res, uint32_t mode, uint32_t n)
{
    a2m_ladder(b, z, g, (float)res * (4.6f / 127.f), A2M_LMIX[mode], 0, n);
}
#else
#define a2m_ladr 0
#endif
#if FELUCCA_A2_OTA
static void a2m_ota(int32_t *b, float *z, float g, int32_t res, uint32_t mode, uint32_t n)
{
    a2m_ladder(b, z, g, (float)res * (4.2f / 127.f), A2M_LMIX[mode], 1, n);
}
#else
#define a2m_ota 0
#endif

#if FELUCCA_A2_DIOD
/* the diode ladder, its linear part as the circuit's equations (equal stages, each loading the one before), y' = wc
 * (A y + e1 u): y1' = u - 2 y1 + y2, y2' = y1 - 2 y2 + y3, y3' = y2 - 2 y3 + y4, y4' = y3 - y4 (the polynomial s^4 + 7 s^3
 * + 15 s^2 + 10 s + 1: it rings at k 18.4, at 1.196 wc: g scaled by A2D_GSC so it rings at the cutoff); trapezoidal:
 * y = P s + q u, P = (I - g A)^-1, q = g P e1 (a 4 x 4 inverse a segment), the states s <- 2 y - s; u = tanh((x - k P4 s)
 * / (1 + k q4)), the input made up by 1 + k / 2 (the 303 keeps its bass by its loop's high-pass, not modelled), the
 * output by 1 + k / 11 (its ringing otherwise -27 dBFS); k
 * 0..22. Its FTYP's mixes its own (A2M_DMIX: the stages are coupled, the Xpander's do not give the shapes) */
#define A2D_GSC (1.f / 1.196f)
static const float A2M_DMIX[A2M_MODES][5] = {   /* (found by search on the linear model at RES 0) */
    {0.f, 0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, -1.f, 3.f, -2.f},
    {-1.f, 4.f, -5.f, 1.f, 1.f}, {-1.f, 4.f, -7.f, 3.f, 0.f}};
static void a2m_diod_coef(float g, float *P, float *q)
{
    float M[4][8];
    int i, j, r;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 8; j++)
            M[i][j] = j == i + 4 ? 1.f : 0.f;
    M[0][1] = M[1][0] = M[1][2] = M[2][1] = M[2][3] = M[3][2] = -g;
    M[0][0] = M[1][1] = M[2][2] = 1.f + 2.f * g, M[3][3] = 1.f + g;
    for (i = 0; i < 4; i++) {                         /* Gauss-Jordan: diagonally dominant, no pivoting */
        float d = 1.f / M[i][i];
        for (j = 0; j < 8; j++)
            M[i][j] *= d;
        for (r = 0; r < 4; r++)
            if (r != i) {
                float f = M[r][i];
                for (j = 0; j < 8; j++)
                    M[r][j] -= f * M[i][j];
            }
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++)
            P[4 * i + j] = M[i][4 + j];
        q[i] = g * M[i][4];
    }
}
static void a2m_diod(int32_t *b, float *z, float g, int32_t res, uint32_t mode, uint32_t n)
{
    const float *mx = A2M_DMIX[mode];
    float P[16], q[4], k = (float)res * (22.f / 127.f), cin = 1.f + 0.5f * k, gout = 1.f + k * (1.f / 11.f), inv;
    float s1 = z[0], s2 = z[1], s3 = z[2], s4 = z[3];
    uint32_t i;
    a2m_diod_coef(g * A2D_GSC, P, q);
    inv = 1.f / (1.f + k * q[3]);
    for (i = 0; i < n; i++) {
        float u = a2m_tanh((A2M_IN(i) * cin - k * (P[12] * s1 + P[13] * s2 + P[14] * s3 + P[15] * s4)) * inv);
        float y1 = P[0] * s1 + P[1] * s2 + P[2] * s3 + P[3] * s4 + q[0] * u;
        float y2 = P[4] * s1 + P[5] * s2 + P[6] * s3 + P[7] * s4 + q[1] * u;
        float y3 = P[8] * s1 + P[9] * s2 + P[10] * s3 + P[11] * s4 + q[2] * u;
        float y4 = P[12] * s1 + P[13] * s2 + P[14] * s3 + P[15] * s4 + q[3] * u;
        s1 = 2.f * y1 - s1, s2 = 2.f * y2 - s2, s3 = 2.f * y3 - s3, s4 = 2.f * y4 - s4;
        A2M_OUT(i, gout * (mx[0] * u + mx[1] * y1 + mx[2] * y2 + mx[3] * y3 + mx[4] * y4));
    }
    z[0] = s1, z[1] = s2, z[2] = s3, z[3] = s4;
}
#else
#define a2m_diod 0
#endif

#if FELUCCA_A2_MS20
/* the Sallen-Key 2-pole (MS-20-style): y1' = wc (x - y1 + f), y2' = wc (y1 - y2), f = k tanh(y1 - y2) the band fed back
 * (H = 1 / (s^2 + (2 - k) s + 1) while linear: it rings from k 2 on; the tanh sets its level and screams). Zero delay:
 * the band solved linearly for the tanh's argument, then the stages with f; LP y2, BP y1 - y2, HP x + f - 2 bp - lp,
 * NOTCH the HP + LP, LP24 a further pole (18 dB); k 0..2.3 */
static void a2m_ms20(int32_t *b, float *z, float g, int32_t res, uint32_t mode, uint32_t n)
{
    float k = (float)res * (2.3f / 127.f), G = g / (1.f + g), h = 1.f / (1.f + g), dn = 1.f / (1.f - k * G * (1.f - G));
    float s1 = z[0], s2 = z[1], s4 = z[3];
    uint32_t i;
    for (i = 0; i < n; i++) {
        float x = A2M_IN(i), y1, y2, bp, f, l4;
        y1 = (G * x + (s1 - G * k * s2) * h) * dn;    /* (linear: the band for the tanh) */
        f = k * a2m_tanh((1.f - G) * y1 - s2 * h);
        y1 = G * (x + f) + s1 * h;
        y2 = G * y1 + s2 * h;
        s1 = 2.f * y1 - s1, s2 = 2.f * y2 - s2;
        bp = y1 - y2;
        A2M_LP1(y2, s4, G, l4);
        switch (mode) {                               /* (constant over the call: predicted) */
        case 0: A2M_OUT(i, y2); break;
        case 1: A2M_OUT(i, l4); break;
        case 2: A2M_OUT(i, 2.f * bp); break;
        case 3: A2M_OUT(i, x + f - 2.f * bp - y2); break;
        default: A2M_OUT(i, x + f - 2.f * bp); break;
        }
    }
    z[0] = s1, z[1] = s2, z[3] = s4;
}
#else
#define a2m_ms20 0
#endif

typedef void (*a2m_fn)(int32_t *b, float *z, float g, int32_t res, uint32_t mode, uint32_t n);
static const a2m_fn A2M_MODEL[A2M_N] = {0, a2m_ladr, a2m_ota, a2m_diod, a2m_ms20};

/* model mdl (LADR .. MS20) over a block of n samples (even), mode its FTYP, res RES (0..127): the states from s[] (a
 * model of another kind there, or a value gone wild: cleared), the cutoff c0 -> cut as two segments, the coefficients
 * at each one's end */
void a2m_block(int32_t *b, int32_t *s, int32_t c0, int32_t cut, uint32_t mdl, uint32_t mode, int32_t res, uint32_t n)
{
    float z[4];
    uint32_t j, h = n >> 1;
    if (mdl >= A2M_N || !A2M_MODEL[mdl] || mode >= A2M_MODES)
        return;
    if (s[7] != (int32_t)mdl) {
        s[0] = s[1] = s[4] = s[5] = 0;              /* (0: 0.0f too) */
        s[7] = (int32_t)mdl;
    }
    z[0] = a2m_ld(s[0]), z[1] = a2m_ld(s[1]), z[2] = a2m_ld(s[4]), z[3] = a2m_ld(s[5]);
    for (j = 0; j < 4u; j++)
        if (!(z[j] < 1e4f && z[j] > -1e4f))          /* (NaN fails both) */
            z[0] = z[1] = z[2] = z[3] = 0.f;
    for (j = 0; j < 2u; j++)
        A2M_MODEL[mdl](b + j * h, z, a2m_g(c0 + (((cut - c0) * (int32_t)(j + 1u)) >> 1)), res, mode, j ? n - h : h);
    s[0] = a2m_st(z[0]), s[1] = a2m_st(z[1]), s[4] = a2m_st(z[2]), s[5] = a2m_st(z[3]);
}
