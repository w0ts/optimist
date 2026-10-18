/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * FM6 engine: Kerem Kilic (Melodee, github.com/keremimo/melodee, eng_fm6.c at e459da5), GPL-3.0-only;
 * ported to SLOOP (it takes the DX7 engine's place). Synthesis after Dexed / MSFA: see below and LICENSING.md */
/* FM6: six-operator FM that plays DX7 voices the way Dexed plays them.
 *
 * In SLOOP: NVOICE (8) voices a part, in the shared budget of 8 (Melodee: 16, as Dexed); SLOOP has no MIDI
 * pitch bend, wheel, foot, breath, aftertouch or CC 5 / 65 input, so the DX7 controllers rest at 0 (their
 * function settings are kept: a voice dump, projects); the operator editor is the UI's (op_fm6.c, ui_fm6.c), the
 * voice's other pages are params.c's (FM6 pages).
 *
 * A part's sound is one DX7 voice in an edit buffer, fm6_ed: the 155 parameters of a
 * DX7 single-voice dump (VCED order, OP6 first), the switches of OP1..OP6, then the part's
 * DX7 function settings (pitch bend, portamento, the controllers' ranges and targets). VOICE
 * (P_E0) picks where the voice is loaded from: 0 INIT VOICE, then the voice packs this build has (builder items
 * FM6_PK_*, 16 voices each; eng_fm6_pk_*.h). The operator editor edits the buffer, so does a DX7 single-voice dump
 * over USB-MIDI (fm6_sysex.c), and projects and user presets save it. A DX7 32-voice dump is held in RAM as a pick
 * list at the end of the track's PRESETS (nbank.c) until power-off.
 *
 * The synthesis is Dexed's MODERN (MSFA, 24-bit), restated in C so that a part renders the samples Dexed renders
 * (Dexed's MARK I and OPL resolutions, and Melodee's MOD / M.TIM / C.TIM controls, were dropped 2026-10-18).
 * Envelopes in the log domain with the DX7 attack curve
 * and static times, output, level and rate key scaling, velocity, the 32 algorithms, the LFO,
 * pitch envelope, pitch bend, portamento and the controllers (wheel, foot, breath, aftertouch
 * to pitch, amplitude and EG bias) follow Dexed's code and its DX7 measurements, at 44.1 kHz
 * in Dexed's 64-sample blocks (two of Melodee's 32-sample control ticks); voices are chosen
 * and handed over as Dexed does (16 of them). tests/fm6_parity.sh renders scores through both
 * and compares the samples. Tables: tools/gen_tables.py ("DX7 data": Apache License 2.0 /
 * GPL-3.0-or-later, see LICENSING.md).
 *
 * Units: logs in Q24 (1 << 24 = one octave or 6 dB); an operator's output is Q24 with
 * 1 << 24 = a unit sine, added to the next operator's phase as 1 << 24 = one cycle
 * (OUTPUT 99 at the top of its envelope: 2.0, a 4 pi index). */

/* ------------------------------------------------------------ the voice --- */
enum { FO_R1, FO_R2, FO_R3, FO_R4, FO_L1, FO_L2, FO_L3, FO_L4, FO_BP, FO_LD, FO_RD, FO_LC, FO_RC, FO_RS,
       FO_AMS, FO_KVS, FO_OL, FO_MODE, FO_CRS, FO_FINE, FO_DET, FO_N };   /* one operator */
enum { FV_PR = 126, FV_PL = 130, FV_ALG = 134, FV_FB, FV_OKS, FV_LFS, FV_LFD, FV_LPMD, FV_LAMD, FV_LFKS,
       FV_LFW, FV_LPMS, FV_TRNSP, FV_NAME, FV_ON = 155 };
/* the part's function settings, after the voice: pitch bend range up / down and step (0 =
 * smooth), portamento (PEDAL: while CC 65 is down, ON) and its time (CC 5 sets it) and glissando,
 * then wheel, foot, breath and aftertouch: range and target (bit 0 pitch, 1 amplitude, 2 EG
 * bias), and Dexed's velocity scaling to the DX7's range */
enum { FN_PBUP = 161, FN_PBDN, FN_PBSTEP, FN_PMODE, FN_PTIME, FN_GLISS, FN_MWR, FN_MWA, FN_FCR, FN_FCA,
       FN_BCR, FN_BCA, FN_ATR, FN_ATA, FN_VNORM, FM6_NP };
#define FM6_NFN (FM6_NP - FN_PBUP)
#define FM6_OPB(n) ((6u - (n)) * FO_N)                   /* buffer offset of OP n (1..6): OP6 comes first */
static int16_t fm6_ed[NPART][FM6_NP] IN_POOL_AUDIO;      /* the parts' voices (int16: the pages edit them; the
                                                         * control blocks read them) */
static int16_t fm6_cur[NPART];                           /* VOICE + 1 the buffer was loaded from, 0 = load it */
static uint8_t fm6_fnok[NPART];                          /* the function settings are set (else: the defaults) */

/* a built-in voice: a DX7 packed voice (VMEM, 118 bytes) and its name, 10 characters and a NUL (so that the VOICE list
 * can point at it); eng_fm6_rom.h has the macros (OP, VOICE ..) and INIT VOICE, the packs (tools/gen_fm6_packs.py) their
 * 16 voices each. The first 118 bytes and the name sit where a DX7 VMEM voice has them: fm6_unpack reads either */
typedef struct {
    uint8_t v[118];
    char name[11];
} fm6_rom_t;
_Static_assert(sizeof(fm6_rom_t) == 129, "FM6: a built-in voice is a packed DX7 voice and a NUL-ended name");
#include "eng_fm6_rom.h"                                 /* FM6_INIT */
#if FELUCCA_FM6_PK_KEYS
#include "eng_fm6_pk_keys.h"                             /* FM6_PK_KEYS_Z, _AT, _N */
#endif
#if FELUCCA_FM6_PK_BASS
#include "eng_fm6_pk_bass.h"
#endif
#if FELUCCA_FM6_PK_BELLS
#include "eng_fm6_pk_bells.h"
#endif
#if FELUCCA_FM6_PK_PADS
#include "eng_fm6_pk_pads.h"
#endif
#if FELUCCA_FM6_PK_80S
#include "eng_fm6_pk_80s.h"
#endif
#if FM6_NPACKS
#include "eng_fm6_pk_dict.h"                             /* FM6_PK_DICT: what every pack voice may copy from */
#endif
#undef OP
#undef OPX
#undef OP_OFF
#undef PEG
#undef PEG_FLAT
#undef VOICE
#undef LFO
#undef NO_LFO
#include "../../../hal/fm1_dsp_asm.h"                         /* FELUCCA_ASM: the operator loops in pi32v2 asm */
#define FM6_PKV 16u                                      /* voices a pack */
#define FM6_NPKV (FM6_NPACKS * FM6_PKV)                  /* pack voices in this build */
#define FM6_NVOICE (1u + FM6_NPKV)                       /* VOICE: INIT VOICE, then the packs built, in this order */
/* a pack: its voices' VMEM bytes compressed (tools/fm6_packs/fm6z.py, 2026-10-18: ~3.9 KB less flash for the five), where
 * each voice's start, their names (uncompressed: the lists show them) */
typedef struct {
    const uint8_t *z;
    const uint16_t *at;
    const char (*n)[11];
} fm6_pack_t;
#define FM6_PK(P) {FM6_PK_##P##_Z, FM6_PK_##P##_AT, FM6_PK_##P##_N},
static const fm6_pack_t FM6_PACK[FM6_NPACKS + 1u] = {   /* (the last ends it: no pack, no empty array) */
    FIF(FELUCCA_FM6_PK_KEYS)(FM6_PK(KEYS)) FIF(FELUCCA_FM6_PK_BASS)(FM6_PK(BASS)) FIF(FELUCCA_FM6_PK_BELLS)(FM6_PK(BELLS))
    FIF(FELUCCA_FM6_PK_PADS)(FM6_PK(PADS)) FIF(FELUCCA_FM6_PK_80S)(FM6_PK(80S)) {0, 0, 0}};
static const uint8_t FM6_PACK_NO[FM6_NPACKS + 1u] = {  /* each pack built: its fixed number (what a project stores) */
    FIF(FELUCCA_FM6_PK_KEYS)(0,) FIF(FELUCCA_FM6_PK_BASS)(1,) FIF(FELUCCA_FM6_PK_BELLS)(2,) FIF(FELUCCA_FM6_PK_PADS)(3,)
    FIF(FELUCCA_FM6_PK_80S)(4,) 0};
/* VOICE as a project or a user preset stores it: 0 INIT VOICE; a pack voice 64 + its pack's fixed number x 16 + its place
 * (the list index depends on the packs a build has, the stored number does not; a pack this build lacks: INIT VOICE,
 * the voice itself kept inline); 1..63 and the rest were the R and U voices of the builds before the packs: INIT VOICE,
 * so an old number never lands on a pack voice (the voice itself is stored inline too) */
#define FM6_PACK_BASE 64u
static int16_t fm6_voice_store(int32_t v)
{
    uint32_t i = (uint32_t)v - 1u;
    return (int16_t)(v >= 1 && v < (int32_t)FM6_NVOICE ? FM6_PACK_BASE + FM6_PACK_NO[i / FM6_PKV] * FM6_PKV + i % FM6_PKV : 0);
}
static int16_t fm6_voice_load(int32_t s)
{
    uint32_t pk, n = (uint32_t)s - FM6_PACK_BASE;
    if (s < (int32_t)FM6_PACK_BASE || n >= 5u * FM6_PKV)
        return 0;
    for (pk = 0; pk < FM6_NPACKS; pk++)
        if (FM6_PACK_NO[pk] == n / FM6_PKV)
            return (int16_t)(1u + pk * FM6_PKV + n % FM6_PKV);
    return 0;
}
static const fm6_pack_t *fm6_pk(uint32_t k) { return &FM6_PACK[(k / FM6_PKV) % (FM6_NPACKS ? FM6_NPACKS : 1u)]; }
static const char *fm6_pack_name(uint32_t k) { return fm6_pk(k)->n[k % FM6_PKV]; }   /* pack voice k's name, NUL-ended */
/* pack voice k (0 .. FM6_NPKV - 1) into r: its 118 VMEM bytes decoded (fm6z.py's format: a token, the literals, the match
 * by its distance back over FM6_PK_DICT and the bytes decoded so far), its name. Callers own r: the render loads a part's
 * voice into its own (fm6_load), the lists into theirs */
static void fm6_pk_get(uint32_t k, fm6_rom_t *r)
{
#if FM6_NPACKS
    const fm6_pack_t *pk = fm6_pk(k);
    const uint8_t *b = pk->z + pk->at[k % FM6_PKV];
    uint8_t *o = r->v;
    uint32_t n = 0, t, c, l, d;
    while (n < sizeof r->v) {
        t = *b++;
        l = t >> 4;
        if (l == 15u)
            do
                l += c = *b++;
            while (c == 255u);
        for (; l && n < sizeof r->v; l--)
            o[n++] = *b++;
        if (n >= sizeof r->v)
            break;
        d = (uint32_t)b[0] | (uint32_t)b[1] << 8;
        b += 2;
        l = (t & 15u) + 4u;
        if ((t & 15u) == 15u)
            do
                l += c = *b++;
            while (c == 255u);
        for (; l && n < sizeof r->v; l--, n++)
            o[n] = n >= d ? o[n - d] : FM6_PK_DICT[sizeof FM6_PK_DICT + n - d];
    }
    memcpy(r->name, fm6_pack_name(k), sizeof r->name);
#else
    (void)k;
    *r = FM6_INIT;
#endif
}


/* parameter ranges in buffer order */
static const uint8_t FM6_OPMAX[FO_N] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
static const uint8_t FM6_GMAX[FV_NAME - FV_PR] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
static const uint8_t FM6_FNMAX[FM6_NFN] = {12, 12, 12, 1, 127, 1, 99, 7, 99, 7, 99, 7, 99, 7, 1};
static const uint8_t FM6_FNDEF[FM6_NFN] = {3, 3, 0, 0, 0, 0, 99, 1, 0, 0, 0, 0, 0, 0, 0};   /* Dexed's, wheel: vibrato */
static int32_t fm6_min(uint32_t i) { return i >= FV_NAME && i < FV_NAME + 10u ? 32 : 0; }
static int32_t fm6_max(uint32_t i)
{
    return i < FV_PR ? FM6_OPMAX[i % FO_N] : i < FV_NAME ? FM6_GMAX[i - FV_PR] : i < FV_ON ? 126
         : i < FN_PBUP ? 1 : FM6_FNMAX[i - FN_PBUP];
}
static void fm6_set(int16_t *ed, uint32_t i, int32_t v) { ed[i] = (int16_t)clamp(v, fm6_min(i), fm6_max(i)); }

static void fm6_fn_reset(int16_t *ed)                    /* the function settings' defaults */
{
    uint32_t i;
    for (i = 0; i < FM6_NFN; i++)
        ed[FN_PBUP + i] = FM6_FNDEF[i];
}

/* DX7 packed voice (128 bytes, VMEM) <-> buffer. Unpacking clamps every value; packing a
 * switched-off operator keeps its level (the switches are not part of a DX7 voice) */
static void fm6_unpack(int16_t *ed, const uint8_t *b)
{
    uint32_t n, i;
    for (n = 1; n <= 6u; n++) {
        const uint8_t *s = b + (6u - n) * 17u;
        uint32_t o = FM6_OPB(n);
        for (i = 0; i < 11u; i++)
            fm6_set(ed, o + i, s[i] & 127);
        fm6_set(ed, o + FO_LC, s[11] & 3);
        fm6_set(ed, o + FO_RC, (s[11] >> 2) & 3);
        fm6_set(ed, o + FO_RS, s[12] & 7);
        fm6_set(ed, o + FO_DET, (s[12] >> 3) & 15);
        fm6_set(ed, o + FO_AMS, s[13] & 3);
        fm6_set(ed, o + FO_KVS, (s[13] >> 2) & 7);
        fm6_set(ed, o + FO_OL, s[14] & 127);
        fm6_set(ed, o + FO_MODE, s[15] & 1);
        fm6_set(ed, o + FO_CRS, (s[15] >> 1) & 31);
        fm6_set(ed, o + FO_FINE, s[16] & 127);
    }
    for (i = 0; i < 8u; i++)
        fm6_set(ed, FV_PR + i, b[102 + i] & 127);
    fm6_set(ed, FV_ALG, b[110] & 31);
    fm6_set(ed, FV_FB, b[111] & 7);
    fm6_set(ed, FV_OKS, (b[111] >> 3) & 1);
    for (i = 0; i < 4u; i++)
        fm6_set(ed, FV_LFS + i, b[112 + i] & 127);
    fm6_set(ed, FV_LFKS, b[116] & 1);
    fm6_set(ed, FV_LFW, (b[116] >> 1) & 7);
    fm6_set(ed, FV_LPMS, (b[116] >> 4) & 7);
    fm6_set(ed, FV_TRNSP, b[117] & 127);
    for (i = 0; i < 10u; i++)
        fm6_set(ed, FV_NAME + i, b[118 + i] & 127);
    for (i = 0; i < 6u; i++)
        ed[FV_ON + i] = 1;
}

static void fm6_from_rom(int16_t *ed, const fm6_rom_t *r) { fm6_unpack(ed, (const uint8_t *)r); }

static void fm6_pack(uint8_t *b, const int16_t *ed)
{
    uint32_t n, i;
    for (n = 1; n <= 6u; n++) {
        uint8_t *d = b + (6u - n) * 17u;
        const int16_t *s = &ed[FM6_OPB(n)];
        for (i = 0; i < 11u; i++)
            d[i] = (uint8_t)s[i];
        d[11] = (uint8_t)(s[FO_LC] | s[FO_RC] << 2);
        d[12] = (uint8_t)(s[FO_RS] | s[FO_DET] << 3);
        d[13] = (uint8_t)(s[FO_AMS] | s[FO_KVS] << 2);
        d[14] = (uint8_t)s[FO_OL];
        d[15] = (uint8_t)(s[FO_MODE] | s[FO_CRS] << 1);
        d[16] = (uint8_t)s[FO_FINE];
    }
    for (i = 0; i < 9u; i++)
        b[102 + i] = (uint8_t)ed[FV_PR + i];                    /* pitch EG, ALG */
    b[111] = (uint8_t)(ed[FV_FB] | ed[FV_OKS] << 3);
    for (i = 0; i < 4u; i++)
        b[112 + i] = (uint8_t)ed[FV_LFS + i];
    b[116] = (uint8_t)(ed[FV_LFKS] | ed[FV_LFW] << 1 | ed[FV_LPMS] << 4);
    b[117] = (uint8_t)ed[FV_TRNSP];
    for (i = 0; i < 10u; i++)
        b[118 + i] = (uint8_t)ed[FV_NAME + i];
}

static void fm6_name(char *d, const int16_t *ed)         /* the voice name, trailing spaces cut */
{
    uint32_t i, n = 0;
    for (i = 0; i < 10u; i++) {
        d[i] = (char)ed[FV_NAME + i];
        if (d[i] != ' ')
            n = i + 1u;
    }
    d[n] = 0;
}

/* VOICE v -> the built-in voice (0 INIT VOICE; a number this build has no voice for plays INIT VOICE: a pack left
 * out, an older build's R or U voice whose patch the project or user preset did not keep), decoded into one buffer
 * read before the next call: the host tests' (the firmware loads through fm6_load, names through fm6_pack_name) */
static const fm6_rom_t *fm6_rom(uint32_t voice)
{
    static fm6_rom_t r;
    if (!voice || voice >= FM6_NVOICE)
        return &FM6_INIT;
    fm6_pk_get(voice - 1u, &r);
    return &r;
}

static void fm6_load(uint32_t p, uint32_t voice)                /* VOICE -> part p's buffer */
{
    fm6_rom_t r;                                         /* (the render's own: fm6_pack_voice's is the main loop's) */
    voice = voice < FM6_NVOICE ? voice : 0u;
    if (voice)
        fm6_pk_get(voice - 1u, &r);
    fm6_from_rom(fm6_ed[p], voice ? &r : &FM6_INIT);
    fm6_cur[p] = (int16_t)(voice + 1u);
}

/* ---------------------------------------------------------- DX7 data --- */
/* DX7 data, measured for MSFA (Copyright 2012 Google Inc.) and Dexed (Copyright 2013-2017
 * Pascal Gauthier), Apache License 2.0: output levels below 20; the static times of the
 * envelope (samples at 44.1 kHz, rates 0..76); the velocity curve; the exponential
 * key-scaling curve; pitch-modulation sensitivity; the pitch envelope's rates and steps
 * (1/32 octave). The sine, 2^x, frequency, MARK I / OPL, detune, LFO and portamento tables
 * are in melodee_tables.h (tools/gen_tables.py) */
static const uint8_t FM6_LEVELLUT[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};
static const int32_t FM6_STATICS[77] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600,
    584325, 507150, 502740, 441000, 418950, 352800, 308700, 286650, 253575, 220500,
    220500, 176400, 145530, 145530, 125685, 110250, 110250, 88200, 88200, 74970,
    61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562,
    22050, 18522, 17640, 15435, 14112, 13230, 11025, 9261, 9261, 7717,
    6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690,
    2249, 1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837,
    837, 705, 573, 573, 529, 441, 441};
static const uint8_t FM6_VELOCITY[64] = {
    0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
    166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202, 205,
    206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232, 233, 235,
    237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};
static const uint8_t FM6_EXPSCALE[33] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33,
                                         39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};
static const uint8_t FM6_PMS[8] = {0, 10, 20, 33, 55, 92, 153, 255};
static const uint32_t FM6_AMS[4] = {0, 4342338, 7171437, 16777216};   /* Q24 */
static const uint8_t FM6_PEG_RATE[100] = {
    1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
    12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49, 51, 53, 54, 56,
    58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110,
    115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};
static const int8_t FM6_PEG_STEP[100] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31,
    -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11,
    -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127};
_Static_assert(CTL * 2 == FM6_N, "FM6: a Dexed block is two control ticks");

/* The 32 algorithms as Dexed (MSFA fm_core.cc) has them, operators in the order they run (OP6 ..
 * OP1): bits 0-1 where the output goes (0 = the voice, 1 / 2 = bus A / B), bit 2 adds to it,
 * bits 4-5 the bus that modulates the operator, bits 6 + 7 its own feedback (bit 7 alone: the
 * end of a feedback loop through OP6, algorithms 4 and 6). tests/fm6_test.c checks the
 * modulation graph of each against the DX7 diagrams */
static const uint8_t FM6_ALG[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14},
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14},
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04},
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}};
static int fm6_carrier(uint32_t alg, uint32_t n) { return !(FM6_ALG[alg & 31u][6u - n] & 3u); }   /* OP n: to the voice */

/* A project or a user preset from before 2026-10-18 kept MOD, M.TIM and C.TIM as EDIT values over its voice: MOD moved the
 * modulators' levels (0.375 dB a step), M.TIM / C.TIM the modulators' / carriers' envelope rates (-t / 5 steps of the
 * rate index, 64 / 41 rate steps each). Now the voice carries them: done once into the voice just loaded (the stored values
 * then read 0). MOD becomes the nearest output level step (0.75 dB, a half step away from 0): exact in the upper range of
 * the level scale, approximate below level 20 */
static void fm6_bake_old(int16_t *ed, int32_t mod, int32_t mtim, int32_t ctim)
{
    uint32_t j, i, alg = (uint32_t)ed[FV_ALG] & 31u;
    for (j = 0; j < 6u; j++) {
        int16_t *op = &ed[j * FO_N];
        int car = !(FM6_ALG[alg][j] & 3u);
        int32_t dq = -(car ? ctim : mtim) / 5, dr = (dq * 64 + (dq < 0 ? -20 : 20)) / 41;
        if (!car && mod)
            fm6_set(ed, j * FO_N + FO_OL, op[FO_OL] + (mod + (mod < 0 ? -1 : 1)) / 2);
        if (dr)
            for (i = 0; i < 4u; i++)
                fm6_set(ed, j * FO_N + FO_R1 + i, op[FO_R1 + i] + dr);
    }
}

/* ------------------------------------------------ Dexed's lookups (exact) --- */
static uint64_t fm6_mulhi(uint64_t a, uint64_t b);

/* SLOOP: the sine table is built in RAM at boot (or before the first FM6 block), the 2^x and
 * frequency tables are figured where they are read, from 2^(i / 1024) = FM6_P2A x FM6_P2B: each value
 * equals Dexed's table entry (tools/gen_tables.py, tests/fm6_tables_test.c), ~16 KB of flash less */
static int32_t FM6_SIN[1025] IN_POOL_AUDIO;               /* (built at boot; the operators read it) */
static uint8_t fm6_tab_ok;

static uint64_t fm6_p2(uint32_t i)                       /* 2^(i / 1024), i = 0..1024, Q60 */
{
    return i >= 1024u ? 1ull << 61 : fm6_mulhi(FM6_P2A[i >> 5], FM6_P2B[i & 31u]);
}
static inline uint32_t fm6_exp2_at(uint32_t i) { return (uint32_t)((fm6_p2(i) + (1u << 29)) >> 30); }   /* exp2.cc */
static inline int32_t fm6_freq_at(uint32_t i)            /* freqlut.cc: 2^(i / 1024) 2^44 / FS, rounded */
{
    return (int32_t)((fm6_mulhi(fm6_p2(i) << 2, FM6_FREQ_M) + (1ull << 32)) >> 33);
}

static void fm6_tables_init(void)
{
    int64_t u = 1 << 30, v = 0, u2;
    uint32_t i;
    for (i = 0; i < 512u; i++) {                         /* sin.cc: by rotation */
        FM6_SIN[i] = (int32_t)((v + 32) >> 6);
        FM6_SIN[i + 512u] = -(int32_t)((v + 32) >> 6);
        u2 = (u * FM6_SIN_C - v * FM6_SIN_S + (1 << 29)) >> 30;
        v = (u * FM6_SIN_S + v * FM6_SIN_C + (1 << 29)) >> 30;
        u = u2;
    }
    FM6_SIN[1024] = 0;
    fm6_tab_ok = 1;
}

static void fm6_boot(void)                               /* main.c, at start-up */
{
    fm6_tables_init();
}

static inline int32_t fm6_sin(int32_t ph)                /* Q24 phase -> Q24 (MSFA Sin::lookup) */
{
    uint32_t i = ((uint32_t)ph >> 14) & 1023u;
    int32_t y0 = FM6_SIN[i];
    return y0 + (int32_t)(((int64_t)(FM6_SIN[i + 1u] - y0) * (ph & 0x3FFF)) >> 14);
}

static int32_t fm6_exp2(int32_t x)                       /* 2^x, Q24 -> Q24 (Exp2::lookup), x > -26 << 24 */
{
    uint32_t i = ((uint32_t)x >> 14) & 1023u;
    uint32_t e0 = fm6_exp2_at(i), e1 = fm6_exp2_at(i + 1u);
    int32_t y = (int32_t)e0 + (int32_t)(((int64_t)(int32_t)(e1 - e0) * (x & 0x3FFF)) >> 14);
    return y >> (6 - (x >> 24));
}

static int32_t fm6_freq(int32_t lf)                      /* log frequency (Q24, Hz) -> phase step (Freqlut) */
{
    uint32_t i = ((uint32_t)lf & 0xFFFFFFu) >> 14;
    int32_t f0 = fm6_freq_at(i), y = f0 + (int32_t)(((int64_t)(fm6_freq_at(i + 1u) - f0) * (lf & 0x3FFF)) >> 14);
    int32_t sh = 20 - (lf >> 24);
    return sh <= 0 ? y : sh > 31 ? 0 : y >> sh;
}

/* 2^(x / 65536) in Q16, x from -16 to +15.99 octaves (the FIXED frequency display, params.c) */
static uint32_t fm6_pow2(int32_t x)
{
    int32_t ip = x >> 16;
    uint32_t y = (uint32_t)fm6_exp2((x & 0xFFFF) << 8) << 6;   /* Q30 */
    if (ip > 15)
        return 0xFFFFFFFFu;
    return ip >= 14 ? y << (ip - 14) : ip < -16 ? 0u : y >> (14 - ip);
}

/* a 32-bit value as the nearest float holds it (Dexed's pitch bend is figured in float) */
static int32_t fm6_f32(int32_t x)
{
    uint32_t a = x < 0 ? (uint32_t)-x : (uint32_t)x, sh = 0, r, q;
    while ((a >> sh) >= (1u << 24))
        sh++;
    if (sh) {
        r = a & ((1u << sh) - 1u);
        q = a >> sh;
        if (r > 1u << (sh - 1u) || (r == 1u << (sh - 1u) && (q & 1u)))
            q++;
        a = q << sh;
    }
    return x < 0 ? -(int32_t)a : (int32_t)a;
}

/* AMS: Dexed takes this share of the level, pt / 2^24, pt = exp((float)sa / 262144 * 0.07 + 12.2), in
 * doubles. Figured here exactly (every sa, tests: fm6_ams_test): the argument rounded as Dexed's doubles
 * round it, then e^x = e^(a / 16) e^(b / 256) e^r to 2^-60 */
static const uint64_t FM6_EXPA[73] = {                   /* e^(a / 16), a = 195..267: Q63 mantissa */
    0xbfb7f02b61b2beceu, 0xcc15527cd7647e60u, 0xd93edb2321f0312cu, 0xe741b4bfbc5fdb7au,
    0xf62be35734bc7827u, 0x8306292b2dd32d3fu, 0x8b7971bf77bcbe0fu, 0x94783f655873a4cbu,
    0x9e0b91aa62b6bf88u, 0xa83cfcad293ab022u, 0xb316b2b2299b4246u, 0xbea38e56ea65fcafu,
    0xcaef1d6d80358746u, 0xd805ac8b564aa9aau, 0xe5f45356ca62140au, 0xf4c9019fea69488au,
    0x824946a8b6f3e449u, 0x8ab060a3ee9fb198u, 0x93a2368edf93c407u, 0x9d27bafe4cf114abu,
    0xa74a7441cd4916b9u, 0xb21485eae56c5b8au, 0xbd90baf17242b974u, 0xc9ca907f86ef32f4u,
    0xd6ce416f8c041c3du, 0xe4a8d2881edbe8d1u, 0xf3681f81edfaffe1u, 0x818d74724cb66a73u,
    0x89e871643a923469u, 0x92cd6245f82eb77cu, 0x9c452cc62c5a5005u, 0xa6594979598f8d18u,
    0xb113cd533c8ac78du, 0xbc7f73bc9722598fu, 0xc8a7a94f7e037c45u, 0xd5989744e68696a8u,
    0xe35f2f9ee31ca5b1u, 0xf2093a1bb995b8fau, 0x80d2b0ff63b58ab8u, 0x8921a25e7f0bb27cu,
    0x91f9c0cdd2993006u, 0x9b63e528812fa978u, 0xa5697a5bc48c98c2u, 0xb01486d2a382d5c6u,
    0xbb6fb67d32e6caedu, 0xc786657d69543779u, 0xd464ab843385094bu, 0xe21767ea2740196bu,
    0xf0ac4e8fee94d18du, 0x8018fac9a670d97du, 0x885bf1f339e79d1eu, 0x9127506c2065b5f4u,
    0x9a83e24e764775cau, 0xa47b04f3db5df385u, 0xaf16b053945cfd25u, 0xba6180fb56d0d2ccu,
    0xc666c2acb93c6363u, 0xd3327ba9e5b2a48fu, 0xe0d178bcdc8eb64fu, 0xef515a054f8e2dcau,
    0xfec0a099e439d252u, 0x87975e8540010249u, 0x90560f6910c83e7au, 0x99a52263dd391d63u,
    0xa38de74f3da90b42u, 0xae1a47c38a42cd04u, 0xb954d10246dd1458u, 0xc548be8445a0a268u,
    0xd20205360f81d45eu, 0xdf8d5f6dcfe5cea2u, 0xedf859a6ba5e1d7du, 0xfd51600ea6f02d97u,
    0x86d3e679b9d307e5u};
static const uint8_t FM6_EXPAE[73] = {17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 24};   /* its scale: 2^17..2^24 */
static const uint64_t FM6_EXPB[16] = {                   /* e^(b / 256), Q63 */
    0x8000000000000000u, 0x808040155aabbbe9u, 0x810100ab00222d86u, 0x81824241b103b504u,
    0x8204055aaef1c8bdu, 0x82864a77bd1036e1u, 0x8309121b2086e8a7u, 0x838c5cc7a104277eu,
    0x84102b00893f64c7u, 0x84947d49a77c8498u, 0x851954274e0fac0au, 0x859eb01e53e19398u,
    0x862491b414f45e15u, 0x86aaf96e72e8f4b3u, 0x8731e7d3d584e8bau, 0x87b95d6b2b38db4cu};

static uint64_t fm6_mulhi(uint64_t a, uint64_t b)        /* (a * b) >> 64 */
{
    uint32_t a1 = (uint32_t)(a >> 32), a0 = (uint32_t)a, b1 = (uint32_t)(b >> 32), b0 = (uint32_t)b;
    uint64_t m = (uint64_t)a1 * b0, n = (uint64_t)a0 * b1, l = (uint64_t)a0 * b0;
    return (uint64_t)a1 * b1 + (m >> 32) + (n >> 32) + (((l >> 32) + (uint32_t)m + (uint32_t)n) >> 32);
}

static uint32_t fm6_ams_pt(uint32_t sa)
{
    const uint64_t M7 = 5044031582654956u, M12 = 6867989431740006u;   /* 0.07 = M7 2^-56, 12.2 = M12 2^-49 */
    uint64_t lo = (uint64_t)sa * (uint32_t)M7, mid = (uint64_t)sa * (uint32_t)(M7 >> 32), l, q, x, r, d, m, r2;
    uint32_t h, s = 0, k, e, t, dd, rem, half;
    l = lo + (mid << 32);                                /* P = sa M7 in units of 2^-74: h:l, under 2^78 */
    h = (uint32_t)(mid >> 32) + (l < lo);
    for (t = h ? h : (uint32_t)(l >> 32); t; t >>= 1)
        s++;
    s += h ? 64u : 32u;
    s = s > 53u ? s - 53u : 0u;                          /* P rounds to 53 bits: q 2^s, s <= 24 */
    q = l;
    if (s) {
        q = (uint64_t)((uint32_t)(l >> 32) >> s | h << (32u - s)) << 32 | ((uint32_t)l >> s | (uint32_t)(l >> 32) << (32u - s));
        rem = (uint32_t)l & ((1u << s) - 1u);
        half = 1u << (s - 1u);
        q += rem > half || (rem == half && (q & 1u));
    }
    /* + 12.2, rounded to the double: 2^-49 steps under 16, 2^-48 from 16 (P >= 2^25 (2^53 - M12)) */
    dd = 25u - s;
    if (dd <= 10u && q >= (2139264564000986u << dd)) {
        dd++;
        x = M12 >> 1;
    } else {
        x = M12;
    }
    rem = (uint32_t)q & ((1u << dd) - 1u);
    half = 1u << (dd - 1u);
    x += (uint64_t)((uint32_t)(q >> 32) >> dd) << 32 | ((uint32_t)q >> dd | (dd < 32u ? (uint32_t)(q >> 32) << (32u - dd) : 0u));
    x += rem > half || (rem == half && (x & 1u));
    if (dd > 25u - s)
        x <<= 1;                                         /* x 2^49 */
    k = (uint32_t)(x >> 41);                             /* e^x = e^(a / 16) e^(b / 256) e^r */
    r = (x & ((1ull << 41) - 1u)) << 15;                 /* r, Q64 (under 1 / 256) */
    r2 = fm6_mulhi(r, r);
    d = fm6_mulhi(r2, r);                                /* e^r - 1 = r + r^2 / 2 + r^3 / 6 + ... */
    d = r + (r2 >> 1) + fm6_mulhi(d, 3074457345618258603u) + fm6_mulhi(fm6_mulhi(d, r), 768614336404564651u) +
        fm6_mulhi(fm6_mulhi(fm6_mulhi(d, r), r), 153722867280912930u);
    m = fm6_mulhi(FM6_EXPA[(k >> 4) - 195u], FM6_EXPB[k & 15u]);   /* Q62 */
    e = FM6_EXPAE[(k >> 4) - 195u];
    if (m >> 63) {
        m >>= 1;
        e++;
    }
    m += fm6_mulhi(m, d);
    return (uint32_t)(m >> 32) >> (30u - e);
}

/* -------------------------------------------------- scaling (DX7 rules) --- */
static int32_t fm6_scaleout(int32_t l) { return l >= 20 ? 28 + l : FM6_LEVELLUT[l < 0 ? 0 : l]; }

static int32_t fm6_curve(int32_t group, int32_t depth, int32_t curve)   /* key level scaling, 0.75 dB steps */
{
    int32_t s = curve == 0 || curve == 3 ? (group * depth * 329) >> 12
                                         : (FM6_EXPSCALE[group > 32 ? 32 : group] * depth * 329) >> 15;
    return curve < 2 ? -s : s;
}

/* an operator's output level for a note and velocity, in 0.023 dB steps (99 * 32 = full) */
static int32_t fm6_outlevel(const int16_t *op, uint32_t note, uint32_t vel)
{
    int32_t off = (int32_t)note - op[FO_BP] - 17, l, v;
    l = fm6_scaleout(op[FO_OL]) + (off >= 0 ? fm6_curve((off + 1) / 3, op[FO_RD], op[FO_RC])
                                            : fm6_curve(-(off - 1) / 3, op[FO_LD], op[FO_LC]));
    v = FM6_VELOCITY[(vel > 127u ? 127u : vel) >> 1] - 239;
    l = ((l > 127 ? 127 : l) << 5) + (((op[FO_KVS] * v + 7) >> 3) << 4);
    return l < 0 ? 0 : l;
}

static int32_t fm6_rscale(const int16_t *op, uint32_t note)   /* key rate scaling, in qrate units */
{
    int32_t x = (int32_t)note / 3 - 7;
    return (op[FO_RS] * clamp(x, 0, 31)) >> 3;
}

static int32_t fm6_logfreq(const int16_t *op, uint32_t note)   /* an operator's pitch for a note (osc_freq) */
{
    int32_t lf;
    if (op[FO_MODE]) {                                  /* fixed: 10 ^ (COARSE % 4 + FINE / 100) Hz */
        lf = (4458616 * ((op[FO_CRS] & 3) * 100 + op[FO_FINE])) >> 3;
        return lf + (op[FO_DET] > 7 ? 13457 * (op[FO_DET] - 7) : 0);
    }
    lf = 50857777 + ((1 << 24) / 12) * (int32_t)note;
    lf += (int32_t)((FM6_DETUNE[note] * (op[FO_DET] - 7)) >> 24) + FM6_COARSE[op[FO_CRS] & 31];
    return lf + FM6_FINE[op[FO_FINE]];
}

/* ------------------------------------------------------------ envelopes --- */
typedef struct {
    int32_t level, target, inc, hold;                    /* Q24 log2; samples a static segment has left */
    uint8_t ix, rising;                                  /* segment 0..3 (L1..L4), 4 = done */
} fm6_eg_t;

typedef struct {                                         /* a pitch envelope (PitchEnv) */
    int32_t pl, pt, pi;                                  /* level, target, step (Q24 octaves) */
    uint8_t pix, prise, pr[4], pv[4];                    /* segment, direction, rates and levels (at the key) */
} fm6_peg_t;

typedef struct fm6_voice {                               /* a voice; operators in Dexed's order: 0 = OP6 */
    fm6_eg_t eg[6];
    uint32_t ph[6];                                      /* phase, Q24 = a cycle */
    int32_t fq[6], gout[6], g[6], dg[6];                 /* step; gain at the block's end, now, per sample */
    int32_t base[6], porta[6];                           /* the note's log frequency; portamento's */
    int16_t ol[6];                                       /* output level (note, velocity), microsteps */
    int8_t rs[6];                                        /* rate scaling */
    uint8_t plan[6];                                     /* this block's routing (FM6_P_*) */
    int32_t fb[2];                                       /* the feedback operator's last two outputs */
    fm6_peg_t pe;                                        /* the pitch envelope */
    uint8_t down, sub, note, vel, fbs, quiet, played;
    uint8_t frozen;                                      /* stopped as by Dexed's panic: not kept running */
    uint8_t still;                                       /* over and at rest: 1 only time goes on, 2 nothing matters */
    int32_t spb;                                         /* the bend and tune it was at rest with */
} fm6_voice_t;
/* MONO / LEGATO: Dexed keys one of its 16 voices for every key and hands the sounding state from
 * voice to voice (transferSignal / transferState). FM6 plays the note on one voice and keeps what
 * else Dexed's voices hold: the note, its key-down order, whether it counts as playing, its
 * portamento pitch and feedback memory (fm6_key, fm6_note_on, fm6_legato) */
typedef struct {
    uint8_t note, fplay;                                 /* note + 1 (0: never keyed); playing, as it was left */
    int32_t seq, porta[6], fb[2];
    fm6_peg_t pe;
} fm6_mono_t;
/* a part's running FM6 state: its voices and its 16 MONO keys. In the part's engine memory (engines.c part_mem,
 * the pool: a part plays one engine at a time, PHYS / GRAIN / ACID share it), zeroed when the part switches to FM6
 * from another engine that used it (part_mem_claim) */
typedef struct {
    fm6_voice_t v[NVOICE];
    fm6_mono_t ms[16];
} fm6_mem_t;
AINL fm6_mem_t *fm6_mem(uint32_t p);                     /* (engines.c, after every engine) */
#define fm6_v(p) (fm6_mem(p)->v)
#define fm6_ms(p) (fm6_mem(p)->ms)
#define FM6_SILENT 1638400                               /* an envelope level the operators no longer render (-96 dB) */
enum { FM6_P_ADD = 4, FM6_P_FB = 0x40, FM6_P_RUN = 0x80 };   /* plan: bits 0-1 out bus, 4-5 in bus */

/* segment ix begins (Env::advance); where it would not move (or an attack to L1 0) the DX7 waits */
static void fm6_eg_go(fm6_eg_t *e, const int16_t *op, int32_t ol, int32_t rs, uint32_t ix)
{
    e->ix = (uint8_t)ix;
    if (ix < 4u) {
        int32_t lv = op[FO_L1 + ix], a = ((fm6_scaleout(lv) >> 1) << 6) + ol - 4256;
        int32_t q = clamp(((op[FO_R1 + ix] * 41) >> 6) + rs, 0, 63);
        e->target = (a < 16 ? 16 : a) << 16;
        e->rising = e->target > e->level;
        e->hold = 0;
        if (e->target == e->level || (!ix && !lv)) {
            int32_t r = clamp(op[FO_R1 + ix] + rs, 0, 99);
            e->hold = r < 77 ? FM6_STATICS[r] : 20 * (99 - r);
            if (r < 77 && !ix && !lv)
                e->hold /= 20;
        }
        e->inc = (4 + (q & 3)) << (2 + FM6_LG_N + (q >> 2));
    }
}

/* one block of an operator envelope (Env::getsample); returns its level. Segments 0..2 run
 * while the key is down, 3 after the release */
static int32_t fm6_eg_step(fm6_eg_t *e, const int16_t *op, int32_t ol, int32_t rs, int down)
{
    if (e->hold) {
        e->hold -= FM6_N;
        if (e->hold <= 0) {
            e->hold = 0;
            fm6_eg_go(e, op, ol, rs, e->ix + 1u);
        }
    }
    if ((e->ix < 3u || (e->ix < 4u && !down)) && !e->hold) {
        if (e->rising) {                                 /* the DX7 attack: from -48 dB, faster when low */
            if (e->level < (1716 << 16))
                e->level = 1716 << 16;
            e->level += (((17 << 24) - e->level) >> 24) * e->inc;
            if (e->level >= e->target) {
                e->level = e->target;
                fm6_eg_go(e, op, ol, rs, e->ix + 1u);
            }
        } else {
            e->level -= e->inc;
            if (e->level <= e->target) {
                e->level = e->target;
                fm6_eg_go(e, op, ol, rs, e->ix + 1u);
            }
        }
    }
    return e->level;
}

static void fm6_peg_go(fm6_peg_t *e, uint32_t ix)        /* pitch EG segment ix (PitchEnv::advance) */
{
    e->pix = (uint8_t)ix;
    if (ix < 4u) {
        e->pt = FM6_PEG_STEP[e->pv[ix]] * (1 << 19);
        e->prise = e->pt > e->pl;
        e->pi = FM6_PEG_RATE[e->pr[ix]] * FM6_PEG_UNIT;
    }
}

static void fm6_peg_set(fm6_peg_t *e, const int16_t *ed)  /* a key: the voice's pitch EG starts from L4 (set) */
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        e->pr[k] = (uint8_t)ed[FV_PR + k];
        e->pv[k] = (uint8_t)ed[FV_PL + k];
    }
    e->pl = FM6_PEG_STEP[e->pv[3]] * (1 << 19);
    fm6_peg_go(e, 0);
}

static int32_t fm6_peg_step(fm6_peg_t *e, int down)      /* pitch envelope, Q24 octaves */
{
    if (e->pix < 3u || (e->pix < 4u && !down)) {
        if (e->prise) {
            e->pl += e->pi;
            if (e->pl >= e->pt) {
                e->pl = e->pt;
                fm6_peg_go(e, e->pix + 1u);
            }
        } else {
            e->pl -= e->pi;
            if (e->pl <= e->pt) {
                e->pl = e->pt;
                fm6_peg_go(e, e->pix + 1u);
            }
        }
    }
    return e->pl;
}

/* ----------------------------------------------------- the part (Dexed's) --- */
static struct {
    uint32_t ph, dly;                                    /* LFO phase; delay ramp (DX7 two-slope) */
    int32_t val, depth;                                  /* Q24, 0..1 << 24 */
    uint8_t rnd;
} fm6_lfo[NPART];
static struct {
    int32_t pb;                                          /* pitch bend, Q24 */
    int32_t pmod, amod, emod;                            /* the controllers' modulation (Dexed Controllers) */
    int32_t prate;                                       /* portamento step per block */
    uint32_t clock;                                      /* Dexed blocks */
    uint32_t hash;                                       /* of the voice, to see edits */
    uint8_t tick, trig, pon, rr, lav, steal, trn, panic; /* rr: chooseNote's start; lav: last voice + 1 */
    uint8_t mcur, mlav, mnew, mlive;                     /* MONO: chooseNote's start, last keyed, taken (+ 1), live */
    int32_t mseq;
    uint32_t amd, pt[4];                                 /* AMS: the shares for this modulation (pok: figured) */
    uint8_t pok;
} fm6_pt[NPART];

static void fm6_lfo_step(uint32_t p, const int16_t *ed)  /* one block of the LFO: Lfo::getsample, getdelay */
{
    uint32_t inc = FM6_LFO_INC[ed[FV_LFS]], ph = fm6_lfo[p].ph + inc, a = 99u - (uint32_t)ed[FV_LFD], d1, d2, d;
    int32_t x;
    fm6_lfo[p].ph = ph;
    switch (ed[FV_LFW]) {
    case 0:                                              /* triangle */
        x = (int32_t)((ph >> 7) ^ (uint32_t)-(int32_t)(ph >> 31)) & ((1 << 24) - 1);
        break;
    case 1:                                              /* saw down */
        x = (int32_t)((~ph ^ (1u << 31)) >> 8);
        break;
    case 2:                                              /* saw up */
        x = (int32_t)((ph ^ (1u << 31)) >> 8);
        break;
    case 3:                                              /* square */
        x = (int32_t)(((~ph) >> 7) & (1u << 24));
        break;
    case 4:                                              /* sine */
        x = (1 << 23) + (fm6_sin((int32_t)(ph >> 8)) >> 1);
        break;
    default:                                             /* sample & hold */
        if (ph < inc)
            fm6_lfo[p].rnd = (uint8_t)(fm6_lfo[p].rnd * 179u + 17u);
        x = ((fm6_lfo[p].rnd ^ 0x80) + 1) << 16;
        break;
    }
    fm6_lfo[p].val = x;
    if (a == 99u) {                                      /* DELAY 0 */
        d1 = d2 = 0xFFFFFFFFu;
    } else {
        a = (16u + (a & 15u)) << (1u + (a >> 4));
        d1 = FM6_LFO_UNIT * a;
        d2 = FM6_LFO_UNIT * (a & 0xFF80u ? a & 0xFF80u : 0x80u);
    }
    d = fm6_lfo[p].dly + (fm6_lfo[p].dly < (1u << 31) ? d1 : d2);
    if (d < fm6_lfo[p].dly) {                            /* past the top */
        fm6_lfo[p].depth = 1 << 24;
        return;
    }
    fm6_lfo[p].dly = d;
    fm6_lfo[p].depth = d < (1u << 31) ? 0 : (int32_t)((d >> 7) & ((1u << 24) - 1u));
}

/* a controller's share (Controllers::applyMod: CC x RANGE / 100, as Dexed's float figures it) */
static int32_t fm6_ctl(int32_t cc, int32_t range)
{
    return cc * range / 100 - ((cc == 100 && (range == 53 || range == 59)) || (cc == 75 && range == 84));
}

static void fm6_ghost(track_t *t, voice_t *v, struct fm6_voice *s);
/* The DX7 controllers of each part: pitch bend (signed 14-bit), wheel, foot (CC 4), breath (CC 2), channel
 * aftertouch (0..127), the portamento pedal (CC 65 down), from SLOOP's MIDI controller layer (midi_control.c):
 * its MIDI_EXPR_HOOK(t, c) calls fm6_midi_expr, its MIDI_CC_HOOK(ch, cc, v) fm6_midi_ptime for CC 5 (seq.c).
 * The generic bend / wheel vibrato of voice.c stays out of FM6's pitch (vmod_t.plog leaves it out) */
static struct {
    int16_t bend;
    uint8_t wheel, foot, breath, press, porta;
} fm6_in[NPART];
static void fm6_midi_expr(uint32_t p, int32_t bend, uint32_t wheel, uint32_t foot, uint32_t breath, uint32_t press,
                          uint32_t porta)
{
    if (p >= NPART)
        return;
    fm6_in[p].bend = (int16_t)clamp(bend, -8192, 8191);
    fm6_in[p].wheel = (uint8_t)(wheel & 127u);
    fm6_in[p].foot = (uint8_t)(foot & 127u);
    fm6_in[p].breath = (uint8_t)(breath & 127u);
    fm6_in[p].press = (uint8_t)(press & 127u);
    fm6_in[p].porta = (uint8_t)(porta != 0u);
}
static void fm6_midi_ptime(uint32_t p, uint32_t v)       /* CC 5: portamento time (FM6 parts keep it, as Dexed) */
{
    if (p < NPART)
        fm6_ed[p][FN_PTIME] = (int16_t)(v & 127u);
}

/* per part and block: the controllers, and every other block (or at a key-down retrigger) the LFO */
static void fm6_ctl_block(track_t *t, const int16_t *ed)
{
    uint32_t p = (uint32_t)(t - trk), k, egs = 0;
    int32_t raw = fm6_in[p].bend, cc[4], m;
    cc[0] = fm6_in[p].wheel;                             /* wheel, foot, breath, aftertouch (fm6_midi_expr) */
    cc[1] = fm6_in[p].foot;
    cc[2] = fm6_in[p].breath;
    cc[3] = fm6_in[p].press;
    fm6_pt[p].pmod = fm6_pt[p].amod = fm6_pt[p].emod = 0;
    for (k = 0; k < 4u; k++) {                           /* wheel, foot, breath, aftertouch */
        m = fm6_ctl(cc[k], ed[FN_MWR + 2u * k]);
        if (ed[FN_MWA + 2u * k] & 1)
            fm6_pt[p].pmod = m > fm6_pt[p].pmod ? m : fm6_pt[p].pmod;
        if (ed[FN_MWA + 2u * k] & 2)
            fm6_pt[p].amod = m > fm6_pt[p].amod ? m : fm6_pt[p].amod;
        if (ed[FN_MWA + 2u * k] & 4)
            fm6_pt[p].emod = m > fm6_pt[p].emod ? m : fm6_pt[p].emod;
        egs |= (uint32_t)ed[FN_MWA + 2u * k] & 4u;
    }
    if (!egs)
        fm6_pt[p].emod = 127;
    if (!raw) {                                          /* pitch bend */
        fm6_pt[p].pb = 0;
    } else if (!ed[FN_PBSTEP]) {
        fm6_pt[p].pb = fm6_f32(raw * 2048 * (raw > 0 ? ed[FN_PBUP] : ed[FN_PBDN])) / 12;
    } else {
        int32_t stp = 12 / ed[FN_PBSTEP];
        fm6_pt[p].pb = ((raw * stp / 8191) * (8191 / stp)) * 2048;
    }
    fm6_pt[p].pon = (uint8_t)(ed[FN_PMODE] || fm6_in[p].porta);
    fm6_pt[p].prate = !fm6_pt[p].pon ? FM6_PORTA[0] : ed[FN_GLISS] ? FM6_GLISS[ed[FN_PTIME]] : FM6_PORTA[ed[FN_PTIME]];
    if (fm6_pt[p].trig || !(fm6_pt[p].tick++ & 1u)) {   /* Dexed's 64-sample grid, restarted by a retrigger */
        fm6_pt[p].trig = 0;
        fm6_pt[p].tick = 1;
        fm6_pt[p].clock++;
        fm6_lfo_step(p, ed);
        for (k = 0; k < NVOICE; k++)                     /* voices over, as Dexed runs them on */
            if (fm6_v(p)[k].played && !fm6_v(p)[k].frozen && !t->v[k].active)
                fm6_ghost(t, &t->v[k], &fm6_v(p)[k]);
    }
}

/* ------------------------------------------------------ the operators --- */
/* The one synthesis, Dexed's MODERN (MSFA): an operator is a sine of the Q24 phase (fm6_sin) times its gain. */
#if DUAL_RENDER                                    /* dual core (dual.c): two parts render at once, */
static int32_t fm6_bus_c[2][2][CTL] IN_POOL_AUDIO, fm6_sum_c[2][CTL] IN_POOL_AUDIO;   /* each core its own operator buses */
#define fm6_bus (fm6_bus_c[fm1_cnum() & 1u])
#define fm6_sum (fm6_sum_c[fm1_cnum() & 1u])
#else
static int32_t fm6_bus[2][CTL] IN_POOL_AUDIO, fm6_sum[CTL] IN_POOL_AUDIO;
#endif

#define FM6_SIN_G(x) ((int32_t)(((int64_t)fm6_sin(x) * g) >> 24))
/* SLOOP: with FELUCCA_ASM (the target) the operator loops run as the pi32v2 asm of hal/fm1_dsp_asm.h (asm_fm_*, the voice
 * output asm_fm6_out): the same operations in the same order, bit-identical. The C loops are the reference (the host
 * build, FELUCCA_ASM=0) and, with FELUCCA_ASM_CHECK=1 (a verification build, not for release), run next to the
 * asm on a copy: fm6_asm_check counts the calls and the blocks that differ (play_check peek:fm6_asm_check:2). */
#ifndef FELUCCA_ASM_CHECK
#define FELUCCA_ASM_CHECK 0
#endif
#if FELUCCA_ASM_CHECK && !FELUCCA_ASM
#error "FELUCCA_ASM_CHECK needs FELUCCA_ASM"
#endif
#if !FELUCCA_ASM || FELUCCA_ASM_CHECK
/* an operator over n samples, modulated by in[] (0: none), added to or into out[] */
#define FM6_LOOP(Y)                                                                                \
    do {                                                                                           \
        if (add)                                                                                   \
            for (i = 0; i < n; i++) {                                                              \
                g += dg;                                                                           \
                out[i] += (Y);                                                                     \
                ph += fq;                                                                          \
            }                                                                                      \
        else                                                                                       \
            for (i = 0; i < n; i++) {                                                              \
                g += dg;                                                                           \
                out[i] = (Y);                                                                      \
                ph += fq;                                                                          \
            }                                                                                      \
    } while (0)
/* the feedback operator: its last two outputs, averaged, modulate it */
#define FM6_LOOP_FB(Y)                                                                             \
    do {                                                                                           \
        if (add)                                                                                   \
            for (i = 0; i < n; i++) {                                                              \
                g += dg;                                                                           \
                m = (y0 + y) >> sh;                                                                \
                y0 = y;                                                                            \
                y = (Y);                                                                           \
                out[i] += y;                                                                       \
                ph += fq;                                                                          \
            }                                                                                      \
        else                                                                                       \
            for (i = 0; i < n; i++) {                                                              \
                g += dg;                                                                           \
                m = (y0 + y) >> sh;                                                                \
                y0 = y;                                                                            \
                y = (Y);                                                                           \
                out[i] = y;                                                                        \
                ph += fq;                                                                          \
            }                                                                                      \
    } while (0)
#if FELUCCA_ASM
#define FM6_REF(name) name##_c
#else
#define FM6_REF(name) name
#endif
static void FM6_REF(fm6_op)(fm6_voice_t *s, uint32_t k, int32_t *out, const int32_t *in, int add, uint32_t n)
{
    uint32_t ph = s->ph[k], i, fq = (uint32_t)s->fq[k];
    int32_t g = s->g[k], dg = s->dg[k];
    if (in)
        FM6_LOOP(FM6_SIN_G((int32_t)(ph + (uint32_t)in[i])));
    else
        FM6_LOOP(FM6_SIN_G((int32_t)ph));
    s->ph[k] = ph;
    s->g[k] = g;
}

static void FM6_REF(fm6_op_fb)(fm6_voice_t *s, uint32_t k, int32_t *out, int add, uint32_t n)
{
    uint32_t ph = s->ph[k], i, fq = (uint32_t)s->fq[k], sh = s->fbs + 1u;
    int32_t g = s->g[k], dg = s->dg[k], y0 = s->fb[0], y = s->fb[1], m;
    FM6_LOOP_FB(FM6_SIN_G((int32_t)(ph + (uint32_t)m)));
    s->ph[k] = ph;
    s->g[k] = g;
    s->fb[0] = y0;
    s->fb[1] = y;
}
#endif

#if FELUCCA_ASM
#if FELUCCA_ASM_CHECK
struct { uint32_t calls, bad; } fm6_asm_check;          /* read by the emulator (play_check peek:fm6_asm_check:2) */
static void fm6_asm_cmp(const int32_t *a, const int32_t *b, uint32_t n)
{
    uint32_t i, bad = 0;
    for (i = 0; i < n; i++)
        bad |= (uint32_t)(a[i] != b[i]);
    fm6_asm_check.calls++;
    fm6_asm_check.bad += bad;
}
#endif
/* an operator over n (1 .. CTL) samples in asm */
static void fm6_op(fm6_voice_t *s, uint32_t k, int32_t *out, const int32_t *in, int add, uint32_t n)
{
#if FELUCCA_ASM_CHECK
    static int32_t ref[CTL];
    static fm6_voice_t rs;
    memcpy(ref, out, n * sizeof ref[0]);
    rs = *s;
    fm6_op_c(&rs, k, ref, in, add, n);
#endif
    if (in)
        asm_fm_mod(out, in, (int32_t)s->ph[k], s->fq[k], s->g[k], s->dg[k], FM6_SIN, (int32_t)n, add);
    else
        asm_fm_pure(out, (int32_t)s->ph[k], s->fq[k], s->g[k], s->dg[k], FM6_SIN, (int32_t)n, add);
    s->ph[k] += (uint32_t)s->fq[k] * n;                  /* (what the loop left in its registers) */
    s->g[k] += s->dg[k] * (int32_t)n;
#if FELUCCA_ASM_CHECK
    fm6_asm_cmp(out, ref, n);
    fm6_asm_cmp((const int32_t *)&s->ph[k], (const int32_t *)&rs.ph[k], 1);
    fm6_asm_cmp(&s->g[k], &rs.g[k], 1);
#endif
}

static void fm6_op_fb(fm6_voice_t *s, uint32_t k, int32_t *out, int add, uint32_t n)
{
#if FELUCCA_ASM_CHECK
    static int32_t ref[CTL];
    static fm6_voice_t rs;
    memcpy(ref, out, n * sizeof ref[0]);
    rs = *s;
    fm6_op_fb_c(&rs, k, ref, add, n);
#endif
    asm_fm_fb(out, (int32_t)s->ph[k], s->fq[k], s->g[k], s->dg[k], s->fb, s->fbs, FM6_SIN, (int32_t)n, add);
    s->ph[k] += (uint32_t)s->fq[k] * n;
    s->g[k] += s->dg[k] * (int32_t)n;
#if FELUCCA_ASM_CHECK
    fm6_asm_cmp(out, ref, n);
    fm6_asm_cmp(s->fb, rs.fb, 2);
    fm6_asm_cmp((const int32_t *)&s->ph[k], (const int32_t *)&rs.ph[k], 1);
#endif
}
#endif /* FELUCCA_ASM */

/* the block's routing (FmCore / EngineMsfa render): which operators sound, into what, from what, their gain
 * ramps; operators below the threshold only keep time */
static void fm6_plan(fm6_voice_t *s, const int32_t *lv, uint32_t alg, uint32_t fbshift)
{
    uint32_t k, has = 1u, fb_on = fbshift < 16u;
    for (k = 0; k < 6u; k++) {
        uint32_t f = FM6_ALG[alg][k], out = f & 3u, in = (f >> 4) & 3u, add = f & 4u, run;
        int32_t g1 = s->gout[k], g2 = fm6_exp2(lv[k] - (14 << 24));
        run = g1 >= 1120 || g2 >= 1120;
        s->gout[k] = g2;
        s->plan[k] = 0;
        if (!run) {
            if (!add)
                has &= ~(1u << out);
            s->ph[k] += (uint32_t)s->fq[k] << FM6_LG_N;
            continue;
        }
        if (!((has >> out) & 1u))
            add = 0;
        if (in && !((has >> in) & 1u))
            in = 0;
        s->g[k] = g1;
        s->dg[k] = (g2 - g1 + (FM6_N >> 1)) >> FM6_LG_N;
        s->plan[k] = (uint8_t)(FM6_P_RUN | out | add | in << 4);
        if (!in && (f & 0xc0u) == 0xc0u && fb_on) {
            s->plan[k] |= FM6_P_FB;
            s->fbs = (uint8_t)fbshift;
        }
        has |= 1u << out;
    }
}

static void fm6_run(fm6_voice_t *s, uint32_t n)          /* the planned operators over n samples into fm6_sum */
{
    uint32_t k;
#if FELUCCA_ASM
    if (n == 32u)
        asm_zero32(fm6_sum);
    else
#endif
        for (k = 0; k < n; k++)
            fm6_sum[k] = 0;
    for (k = 0; k < 6u; k++) {
        uint32_t pl = s->plan[k], o = pl & 3u, in = (pl >> 4) & 3u;
        int32_t *out = o ? fm6_bus[o - 1u] : fm6_sum;
        if (!(pl & FM6_P_RUN))
            continue;
        if (pl & FM6_P_FB)
            fm6_op_fb(s, k, out, pl & FM6_P_ADD, n);
        else
            fm6_op(s, k, out, in ? fm6_bus[in - 1u] : 0, pl & FM6_P_ADD, n);
    }
}

/* ------------------------------------------------------------ the voice --- */
static fm6_voice_t *fm6_state(const track_t *t, const voice_t *v) { return &fm6_v(t - trk)[v - t->v]; }
static uint32_t fm6_note(const track_t *t, const voice_t *v)   /* the note the DX7 rules see: + TRANSPOSE */
{
    return (uint32_t)clamp((int32_t)v->note + fm6_ed[t - trk][FV_TRNSP] - 24, 0, 127);
}

/* the operators' pitch, level and rate scaling for the voice's note and velocity (Dx7Note::init / update) */
static void fm6_keyed(fm6_voice_t *s, const int16_t *ed, uint32_t note)
{
    uint32_t k;
    for (k = 0; k < 6u; k++) {
        const int16_t *op = &ed[k * FO_N];
        s->ol[k] = (int16_t)fm6_outlevel(op, note, s->vel);
        s->rs[k] = (int8_t)fm6_rscale(op, note);
        s->base[k] = fm6_logfreq(op, note);
    }
}

/* Dexed's "playing" (Dx7Note::isPlaying: an output operator's envelope not done) */
static int fm6_playing(const track_t *t, uint32_t i)
{
    const fm6_voice_t *s = &fm6_v(t - trk)[i];
    const int16_t *ed = fm6_ed[t - trk];
    uint32_t k;
    if (!s->played)
        return 0;
    for (k = 0; k < 6u; k++)
        if ((FM6_ALG[ed[FV_ALG] & 31][k] & 4u) && (s->eg[k].ix < 4u || ed[k * FO_N + FO_L4] > 0))
            return 1;
    return 0;
}

/* POLY: the voice for a key (chooseNote): free over key-up over the same note, the oldest key first */
static uint32_t fm6_alloc(track_t *t, uint32_t note)
{
    uint32_t p = (uint32_t)(t - trk), k = fm6_pt[p].rr % NVOICE, best = k, i;
    int32_t bs = -1;
    for (i = 0; i < NVOICE; i++) {
        int32_t sc = (fm6_playing(t, k) ? 0 : 4) + (t->v[k].gate ? 0 : 2) + (fm6_v(p)[k].played && t->v[k].note == note);
        if (sc > bs || (sc == bs && t->v[k].age < t->v[best].age)) {
            best = k;
            bs = sc;
        }
        k = (k + 1u) % NVOICE;
    }
    fm6_pt[p].rr = (uint8_t)((best + 1u) % NVOICE);
    fm6_pt[p].steal = (uint8_t)fm6_playing(t, best);
    return best;
}

static void fm6_sync(track_t *t, const voice_t *self);

/* where portamento starts, as Dexed's initPortamento finds it: the last voice keyed (still going:
 * its pitch now; after a MONO key-up handed the note on, as it was then); 0 = no portamento */
static int fm6_psrc(uint32_t p, const int16_t *ed, int32_t *src)
{
    uint32_t k, l = fm6_pt[p].lav;
    if (!l || !fm6_v(p)[l - 1u].played || !fm6_pt[p].pon || ed[FN_PTIME] <= 0)
        return 0;
    for (k = 0; k < 6u; k++)
        src[k] = fm6_v(p)[l - 1u].porta[k];
    return 1;
}

/* MONO: Dexed's voice c is keyed and its key is down (but for the key going down now, + 1) */
static int fm6_mkd(const track_t *t, uint32_t c, uint32_t now)
{
    uint32_t p = (uint32_t)(t - trk), i;
    if (!fm6_ms(p)[c].note || fm6_ms(p)[c].note == now)
        return 0;
    for (i = 0; i < 16u; i++)                            /* the key's latest voice */
        if (fm6_ms(p)[i].note == fm6_ms(p)[c].note && fm6_ms(p)[i].seq > fm6_ms(p)[c].seq)
            return 0;
    for (i = 0; i < t->nmono; i++)
        if (t->mono_stack[i] + 1u == fm6_ms(p)[c].note)
            return 1;
    return 0;
}

/* MONO: the sounding state leaves Dexed's live voice for voice c (+ 1) */
static void fm6_mhand(track_t *t, uint32_t c)
{
    uint32_t p = (uint32_t)(t - trk), l = fm6_pt[p].mlive, k;
    fm6_voice_t *s = &fm6_v(p)[0];
    if (l) {                                             /* the one left keeps what it had */
        fm6_ms(p)[l - 1u].fplay = (uint8_t)(t->v[0].active || s->played ? fm6_playing(t, 0) : 0);
        for (k = 0; k < 6u; k++)
            fm6_ms(p)[l - 1u].porta[k] = s->porta[k];
        fm6_ms(p)[l - 1u].fb[0] = s->fb[0];
        fm6_ms(p)[l - 1u].fb[1] = s->fb[1];
        fm6_ms(p)[l - 1u].pe = s->pe;
    }
    fm6_pt[p].mlive = (uint8_t)c;
    fm6_pt[p].mnew = (uint8_t)c;
}

static void fm6_mtake(track_t *t, fm6_voice_t *s)        /* MONO: the voice plays Dexed's voice mnew */
{
    uint32_t p = (uint32_t)(t - trk), c = fm6_pt[p].mnew, k;
    if (!c)
        return;
    for (k = 0; k < 6u; k++)
        s->porta[k] = fm6_ms(p)[c - 1u].porta[k];
    s->fb[0] = fm6_ms(p)[c - 1u].fb[0];
    s->fb[1] = fm6_ms(p)[c - 1u].fb[1];
    s->pe = fm6_ms(p)[c - 1u].pe;
    fm6_pt[p].mnew = 0;
}

/* a key goes down in MONO / LEGATO: Dexed keys a voice for it (chooseNote, init, initPortamento);
 * it takes the sound if it is the only key or above the one sounding (else it waits, keyed) */
static void fm6_key(track_t *t, uint32_t note)
{
    uint32_t p = (uint32_t)(t - trk), i, k = fm6_pt[p].mcur % 16u, c = k, l, nt;
    const int16_t *ed = fm6_ed[p];
    int32_t bs = -1;
    fm6_sync(t, 0);
    l = fm6_pt[p].mlive;
    for (i = 0; i < 16u; i++) {
        int32_t sc = ((l == k + 1u ? fm6_playing(t, 0) : fm6_ms(p)[k].fplay) ? 0 : 4) +
                     (fm6_mkd(t, k, note + 1u) ? 0 : 2) +
                     (fm6_ms(p)[k].note == note + 1u);
        if (sc > bs || (sc == bs && fm6_ms(p)[k].seq < fm6_ms(p)[c].seq)) {
            c = k;
            bs = sc;
        }
        k = (k + 1u) % 16u;
    }
    fm6_pt[p].mcur = (uint8_t)((c + 1u) % 16u);
    nt = (uint32_t)clamp((int32_t)note + ed[FV_TRNSP] - 24, 0, 127);
    for (k = 0; k < 6u; k++)                             /* init: the note's pitch */
        fm6_ms(p)[c].porta[k] = fm6_logfreq(&ed[k * FO_N], nt);
    if (fm6_pt[p].mlav && fm6_ms(p)[fm6_pt[p].mlav - 1u].note && fm6_pt[p].pon && ed[FN_PTIME] > 0) {
        uint32_t a = fm6_pt[p].mlav - 1u;                /* initPortamento: from the last keyed voice */
        for (k = 0; k < 6u; k++)
            fm6_ms(p)[c].porta[k] = l == a + 1u && a != c ? fm6_v(p)[0].porta[k] : fm6_ms(p)[a].porta[k];
    }
    fm6_ms(p)[c].note = (uint8_t)(note + 1u);
    fm6_ms(p)[c].seq = ++fm6_pt[p].mseq;
    fm6_ms(p)[c].fplay = 1;                              /* keyed: its envelopes start (Dexed counts it playing) */
    fm6_peg_set(&fm6_ms(p)[c].pe, ed);
    fm6_pt[p].mnew = 0;
    if (l && l != c + 1u && fm6_mkd(t, l - 1u, 0) && fm6_ms(p)[l - 1u].note > note + 1u)
        return;                                          /* a higher key sounds: this one waits */
    fm6_mhand(t, c + 1u);
    fm6_pt[p].mlav = (uint8_t)(c + 1u);
}

/* a key goes down (DexedAudioProcessor::keydown, Dx7Note::init) */
static void fm6_note_on(track_t *t, voice_t *v)
{
    uint32_t p = (uint32_t)(t - trk), k, i, vi = (uint32_t)(v - t->v), note;
    fm6_voice_t *s = fm6_state(t, v);
    const int16_t *ed = fm6_ed[p];
    int mono = t->p[P_VOICE] != V_POLY, steal = fm6_pt[p].steal || (mono && s->played), ps;
    int32_t src[6];
    fm6_pt[p].steal = 0;
    fm6_sync(t, v);
    ps = fm6_psrc(p, ed, src);                           /* before this voice takes the note */
    for (i = 0; i < NVOICE; i++)                         /* the first key down: the LFO restarts */
        if (i != vi && t->v[i].gate)
            break;
    if (i == NVOICE) {
        if (ed[FV_LFKS])
            fm6_lfo[p].ph = (1u << 31) - 1u;
        fm6_lfo[p].dly = 0;
        fm6_pt[p].trig = 1;
    }
    s->vel = (uint8_t)(ed[FN_VNORM] ? v->vel * 7874015 / 10000000 : v->vel);
    s->note = v->note;
    note = fm6_note(t, v);
    fm6_keyed(s, ed, note);
    for (k = 0; k < 6u; k++) {
        s->eg[k].level = 0;
        fm6_eg_go(&s->eg[k], &ed[k * FO_N], s->ol[k], s->rs[k], 0);
        s->porta[k] = s->base[k];
    }
    fm6_peg_set(&s->pe, ed);
    s->down = 1;
    if (ed[FV_OKS] && !steal)                            /* KEY SYNC (not on a stolen voice: no click) */
        for (k = 0; k < 6u; k++)
            s->ph[k] = 0, s->gout[k] = 0;
    if (ps && !mono)                                     /* portamento from the last voice's pitch */
        for (k = 0; k < 6u; k++)
            s->porta[k] = src[k];
    if (mono)
        fm6_mtake(t, s);
    if (!mono && !ed[FV_OKS])                            /* the same note sounding: its phases */
        for (i = 0; i < NVOICE; i++)
            if (i != vi && fm6_playing(t, i) && fm6_v(p)[i].played && t->v[i].note == v->note) {
                for (k = 0; k < 6u; k++)
                    s->ph[k] = fm6_v(p)[i].ph[k];
                break;
            }
    fm6_pt[p].lav = (uint8_t)(vi + 1u);
    s->played = 1;
    s->sub = 0;
    s->quiet = 0;
    s->frozen = 0;
    s->still = 0;
}

/* MONO / LEGATO without a new attack: the note changes, the envelopes go on (Dexed's mono: the
 * voice keyed for the note takes over the sounding one's state, transferState), with that voice's
 * portamento pitch and feedback memory: a new key (fm6_key), or the highest key still down after
 * a key-up */
static void fm6_legato(track_t *t, voice_t *v)
{
    uint32_t p = (uint32_t)(t - trk), k, c;
    fm6_voice_t *s = fm6_state(t, v), keep = *s;
    const int16_t *ed = fm6_ed[p];
    fm6_sync(t, v);
    if (!fm6_pt[p].mnew) {                               /* a key-up: the voice keyed for this key */
        for (c = 0; c < 16u && !(fm6_ms(p)[c].note == v->note + 1u && fm6_mkd(t, c, 0)); c++)
            ;
        if (c < 16u)
            fm6_mhand(t, c + 1u);
    }
    s->note = v->note;
    fm6_keyed(s, ed, fm6_note(t, v));
    for (k = 0; k < 6u; k++) {                           /* the old note's levels and rates stay */
        s->ol[k] = keep.ol[k];
        s->rs[k] = keep.rs[k];
        s->porta[k] = s->base[k];
    }
    fm6_peg_set(&s->pe, ed);
    fm6_mtake(t, s);
}

/* the voice amplitude: Melodee's ADSR, until the voice is over: released, and every output
 * operator's envelope (and the feedback operator's) gone under what any engine renders, for
 * good. Dexed keeps computing such a voice; FM6 keeps only its control path running (fm6_ghost) */
static int32_t fm6_amp(track_t *t, voice_t *v, int32_t adsr)
{
    if (fm6_state(t, v)->quiet < 2u)
        return adsr;
    v->stage = 0;
    v->active = 0;
    return 0;
}

/* a new Dexed block: key-up, the envelopes, pitch and gains (Dx7Note::compute), the routing */
static void fm6_control(track_t *t, voice_t *v, fm6_voice_t *s, const vmod_t *m)
{
    uint32_t p = (uint32_t)(t - trk), alg, k, dead = 1;
    const int16_t *ed = fm6_ed[p];
    int32_t lv[6], lfo = fm6_lfo[p].val, dly = fm6_lfo[p].depth, pm, sens, pbase, amd, a1, a2, tune;
    uint32_t pmd = ((uint32_t)ed[FV_LPMD] * 165u) >> 6;
    alg = (uint32_t)ed[FV_ALG] & 31u;
    if (!v->gate && s->down) {                           /* key up: every envelope to its fourth segment */
        s->down = 0;
        for (k = 0; k < 6u; k++)
            fm6_eg_go(&s->eg[k], &ed[k * FO_N], s->ol[k], s->rs[k], 3);
        fm6_peg_go(&s->pe, 3);
    }
    /* pitch: the LFO (PMD x PMS, after the delay, or a controller's), the pitch envelope, bend */
    sens = FM6_PMS[ed[FV_LPMS]] * (lfo - (1 << 23));
    {
        int32_t p1 = (int32_t)(((int64_t)(pmd * (uint32_t)dly) * sens) >> 39);
        int32_t p2 = (int32_t)(((int64_t)fm6_pt[p].pmod * sens) >> 14);
        p1 = p1 < 0 ? -p1 : p1;
        p2 = p2 < 0 ? -p2 : p2;
        pm = p1 > p2 ? p1 : p2;
        pm = fm6_peg_step(&s->pe, s->down) + (sens < 0 ? -pm : pm);
    }
    tune = song.g[G_TUNE] * 13981;                       /* cents, Q24 */
    pbase = fm6_pt[p].pb + tune;
    pm += pbase + m->plog;                               /* + the part's glide, LFO / ENV pitch, unison */
    /* amplitude: the LFO (AMD, after the delay) or a controller's, at least the EG bias */
    lfo = (1 << 24) - lfo;
    a1 = (int32_t)(((int64_t)((((uint32_t)ed[FV_LAMD] * 165u) >> 6) * (uint32_t)dly) >> 8) * lfo >> 24);
    a2 = (int32_t)(((int64_t)fm6_pt[p].amod * lfo) >> 7);
    amd = a1 > a2 ? a1 : a2;
    a1 = (1 << 24) - ((fm6_pt[p].emod + 1) << 17);
    amd = (uint32_t)a1 > (uint32_t)amd ? a1 : amd;
    for (k = 0; k < 6u; k++) {
        const int16_t *op = &ed[k * FO_N];
        int32_t rs = s->rs[k], level;
        if (!ed[FV_ON + 5u - k]) {                       /* switched off: the envelope runs on, silent */
            fm6_eg_step(&s->eg[k], op, s->ol[k], rs, s->down);
            lv[k] = 0;
            continue;
        }
        if (op[FO_MODE]) {
            s->fq[k] = fm6_freq(s->base[k] + pbase);
        } else {
            int32_t b = s->base[k];
            if (s->porta[k] != s->base[k]) {             /* portamento towards the note */
                int32_t cur = s->porta[k], up = cur < s->base[k], np = cur + (up ? fm6_pt[p].prate : -fm6_pt[p].prate);
                b = cur;
                if (ed[FN_GLISS])
                    b -= (b - 50857777) % ((1 << 24) / 12);
                if ((up && np > s->base[k]) || (!up && np < s->base[k]))
                    np = s->base[k];
                s->porta[k] = np;
            }
            s->fq[k] = fm6_freq(b + pm);
        }
        level = fm6_eg_step(&s->eg[k], op, s->ol[k], rs, s->down);
        if (FM6_AMS[op[FO_AMS]]) {                       /* AMS: the modulation takes a part of the level */
            uint32_t a = (uint32_t)op[FO_AMS], pt;
            if (fm6_pt[p].amd != (uint32_t)amd) {        /* one modulation a block for the part: 3 shares */
                fm6_pt[p].amd = (uint32_t)amd;
                fm6_pt[p].pok = 0;
            }
            if (!((fm6_pt[p].pok >> a) & 1u)) {
                uint32_t sa = (uint32_t)(((uint64_t)(uint32_t)amd * FM6_AMS[a]) >> 24);
                fm6_pt[p].pt[a] = sa ? fm6_ams_pt(sa) : 198789u;   /* exp(12.2) */
                fm6_pt[p].pok |= (uint8_t)(1u << a);
            }
            pt = fm6_pt[p].pt[a];
            level -= (int32_t)(((uint64_t)(uint32_t)level * ((uint64_t)pt << 4)) >> 28);
        }
        lv[k] = level;
    }
    for (k = 0; k < 6u; k++) {                           /* the output operators all gone for good? */
        const fm6_eg_t *e = &s->eg[k];               /* (and the feedback one: its memory outlives the note) */
        if ((fm6_carrier(alg, 6u - k) || ((FM6_ALG[alg][k] & 0xc0u) == 0xc0u && ed[FV_FB])) &&
            (s->down || ed[k * FO_N + FO_L4] > 0 || e->level >= FM6_SILENT || e->ix < 3u || (e->ix == 3u && e->rising)))
            dead = 0;
    }
    s->quiet = dead ? (uint8_t)(s->quiet + (s->quiet < 2u)) : 0;
    fm6_plan(s, lv, alg, ed[FV_FB] ? 8u - (uint32_t)ed[FV_FB] : 16u);
}

/* a voice that is over: its envelopes, pitch, gains and phases go on a Dexed block, without the
 * samples (none of them sounds), so that its next note starts where Dexed's would (KEY SYNC off,
 * a voice taken while playing) */
/* nothing moves a voice at rest: no LFO or controller pitch (PMS 0, or no depth), no amplitude
 * modulation where an operator has AMS */
static int fm6_quiet(uint32_t p, const int16_t *ed)
{
    uint32_t k, ams = 0;
    int32_t dly = fm6_lfo[p].depth;
    for (k = 0; k < 6u; k++)
        ams |= (uint32_t)ed[k * FO_N + FO_AMS];
    return (!ed[FV_LPMS] || (!fm6_pt[p].pmod && (!ed[FV_LPMD] || !dly))) &&
           (!ams || (!fm6_pt[p].amod && fm6_pt[p].emod == 127 && (!ed[FV_LAMD] || !dly)));
}

static void fm6_ghost(track_t *t, voice_t *v, fm6_voice_t *s)
{
    static const vmod_t m0 = {.shape = 64 << 8};
    uint32_t p = (uint32_t)(t - trk), k;
    const int16_t *ed = fm6_ed[p];
    int32_t pb = fm6_pt[p].pb + song.g[G_TUNE] * 13981;
    int quiet;
    if (s->still == 2u)
        return;                                          /* key sync, not playing: its next note starts afresh */
    quiet = fm6_quiet(p, ed);
    if (s->still && quiet && pb == s->spb) {             /* at rest: only time goes on */
        for (k = 0; k < 6u; k++)
            s->ph[k] += (uint32_t)s->fq[k] << FM6_LG_N;
        return;
    }
    fm6_control(t, v, s, &m0);
    for (k = 0; k < 6u; k++)
        if (s->plan[k] & FM6_P_RUN)
            s->ph[k] += (uint32_t)s->fq[k] << FM6_LG_N;
    s->sub = 0;
    s->still = 0;
    s->spb = pb;
    if (quiet && !s->down && s->pe.pix >= 4u) {          /* every envelope done, portamento there */
        for (k = 0; k < 6u && s->eg[k].ix >= 4u && s->porta[k] == s->base[k]; k++)
            ;
        if (k == 6u)
            s->still = (uint8_t)(ed[FV_OKS] && t->p[P_VOICE] == V_POLY && !fm6_playing(t, (uint32_t)(v - t->v)) ? 2 : 1);
    }
}

/* the voice into the part's output (with FELUCCA_ASM: asm_fm6_out, which computes the same; this is the reference) */
#if !FELUCCA_ASM || FELUCCA_ASM_CHECK
static void fm6_out_c(int32_t *out, const vmod_t *m, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {                            /* a voice clips at 16 unit sines, as in Dexed */
        int32_t x = clamp(fm6_sum[i], -(1 << 28), (1 << 28) - 1);
        out[i] += (int32_t)(((int64_t)x * (amp_at(m, i) * VOICE_FS)) >> 40);   /* a carrier at OUTPUT 99: VOICE_FS */
    }
}
#endif

static void fm6_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    fm6_voice_t *s = fm6_state(t, v);
    if (!s->sub)
        fm6_control(t, v, s, m);
    s->sub ^= 1u;
    fm6_run(s, n);
#ifdef FM6_TAP
    FM6_TAP(fm6_sum, n);                                 /* tests/fm6_parity.c: the voice before the output */
#endif
#if FELUCCA_ASM
    if (n) {                                             /* the loop below in asm (hal/fm1_dsp_asm.h) */
        int32_t d = m->amp1 - m->amp0;
#if FELUCCA_ASM_CHECK
        static int32_t ref[CTL];
        if (n <= CTL) {
            memcpy(ref, out, n * sizeof ref[0]);
            fm6_out_c(ref, m, n);
        }
#endif
        asm_fm6_out(out, fm6_sum, m->amp0 * 32 + (d < 0 ? CTL - 1 : 0), d, VOICE_FS, (int32_t)n);
#if FELUCCA_ASM_CHECK
        if (n <= CTL)
            fm6_asm_cmp(out, ref, n);
#endif
    }
#else
    fm6_out_c(out, m, n);
#endif
}

/* the part's voice against what the voices play: VOICE changed (or the buffer was never loaded) ->
 * load it, as a program change: the notes stop, as on a voice dump or a new TRANSPOSE; any other
 * edit reaches the sounding voices (Dx7Note::update: held notes go on from L3). Before a key-down
 * (but for its own voice) and at every block */
static void fm6_sync(track_t *t, const voice_t *self)
{
    uint32_t p = (uint32_t)(t - trk), want = (uint32_t)t->p[P_E0] < FM6_NVOICE ? (uint32_t)t->p[P_E0] : 0u, i, h = 2166136261u;
    int16_t *ed = fm6_ed[p];
    if (!fm6_tab_ok)
        fm6_tables_init();                               /* (boot does it: fm6_boot; host builds here) */
    if (!fm6_fnok[p]) {
        fm6_fn_reset(ed);
        fm6_fnok[p] = 1;
    }
    if (fm6_cur[p] != (int32_t)want + 1) {
        fm6_load(p, want);
        fm6_pt[p].panic = 1;
    }
    for (i = 0; i < FN_PBUP; i++)
        h = (h ^ (uint16_t)ed[i]) * 16777619u;
    if (h == fm6_pt[p].hash && !fm6_pt[p].panic)
        return;
    {
        int panic = fm6_pt[p].panic || (fm6_pt[p].hash && ed[FV_TRNSP] != fm6_pt[p].trn);
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &t->v[i];
            fm6_voice_t *s = &fm6_v(p)[i];
            uint32_t k;
            if (v == self || !(v->active || (s->played && !s->frozen)))
                continue;                                /* (voices over too: Dexed keeps them live) */
            s->still = 0;
            if (panic) {                                 /* the notes stop (one block's fade), as Dexed's panic */
                if (v->active) {
                    v->gate = 0;
                    v->stage = 4;
                }
                s->frozen = 1;
                for (k = 0; k < 6u; k++)
                    s->ph[k] = 0, s->gout[k] = 0;
                continue;
            }
            fm6_keyed(s, ed, fm6_note(t, v));
            if (s->down)
                for (k = 0; k < 6u; k++)
                    fm6_eg_go(&s->eg[k], &ed[k * FO_N], s->ol[k],
                              s->rs[k], 2);
        }
    }
    fm6_pt[p].hash = h;
    fm6_pt[p].trn = (uint8_t)ed[FV_TRNSP];
    fm6_pt[p].panic = 0;
}

static void fm6_block(track_t *t)                        /* per part and block: the voice, controllers, LFO */
{
    fm6_sync(t, 0);
    fm6_ctl_block(t, fm6_ed[t - trk]);
}

/* the part after its voices: Dexed's DC filter on the voices' sum (PluginFx: y = x - x' + (1 - 126 / FS) y',
 * about 20 Hz); with no voice it starts afresh */
static struct {
    int32_t x1, y1;                                      /* the last input; the last output << 8 */
    int32_t l1, l2, h1, h2;                              /* LPF's and HPF's trapezoidal SVF states (fm6_flt) */
} fm6_dc[NPART];
/* LPF RES HPF (2026-10-18, the user: "a filter after the fm6 ... I know it is not standard"): on the part after the DC
 * filter, not on each voice; EDIT 6..8 (EDIT 2..5 were MOD M.TIM C.TIM ENGINE until f0f74c5f: a load bakes what an old
 * project holds there into the voice, so they stay empty). 0 is off (open) for LPF and HPF, as every FM6 sound stored before has it, so an old
 * sound plays sample for sample (nothing runs); LPF closes the low-pass from 20 kHz with the amount (its cutoff 127 - LPF on
 * the SVF's scale), RES its peak, HPF opens a high-pass (no peak). Half the signal through the SVF (its +-150000 range: the
 * voices' sum reaches 8 x VOICE_FS), doubled back */
static void fm6_flt(track_t *t, uint32_t p, int32_t *out, uint32_t n)
{
    int32_t lpf = t->p[P_E5], res = t->p[P_E6], hpf = t->p[P_E7], l1, l2, h1, h2;
    tsvf_t cl, ch;
    uint32_t i;
    if (lpf <= 0 && hpf <= 0) {
        fm6_dc[p].l1 = fm6_dc[p].l2 = fm6_dc[p].h1 = fm6_dc[p].h2 = 0;   /* (off: from rest when it comes back) */
        return;
    }
    tsvf_coef(&cl, (127 - clamp(lpf, 0, 127)) << 8, clamp(res, 0, 127));
    tsvf_coef(&ch, clamp(hpf, 0, 127) << 8, 0);
    l1 = fm6_dc[p].l1, l2 = fm6_dc[p].l2, h1 = fm6_dc[p].h1, h2 = fm6_dc[p].h2;
    for (i = 0; i < n; i++) {
        int32_t x = out[i] >> 1, bp;
        if (lpf > 0)
            x = tsvf_lp(&cl, x, &l1, &l2);
        if (hpf > 0) {
            int32_t lo = tsvf_tick(&ch, x, &h1, &h2, &bp);
            x = x - 2 * bp - lo;                         /* (the high-pass: in - k bp - lp, k 2 at RES 0) */
        }
        out[i] = x * 2;
    }
    fm6_dc[p].l1 = l1, fm6_dc[p].l2 = l2, fm6_dc[p].h1 = h1, fm6_dc[p].h2 = h2;
}

static void fm6_post(track_t *t, int32_t *out, uint32_t n, uint32_t nr)
{
    uint32_t p = (uint32_t)(t - trk), i;
    int32_t x1 = fm6_dc[p].x1, y = fm6_dc[p].y1;
    if (!nr) {
        fm6_dc[p].x1 = fm6_dc[p].y1 = 0;
        fm6_dc[p].l1 = fm6_dc[p].l2 = fm6_dc[p].h1 = fm6_dc[p].h2 = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t x = out[i];
        y = (int32_t)(((int64_t)(x - x1) * 256 + (((int64_t)y * 1070673990) >> 30)));
        x1 = x;
        out[i] = (y + 128) >> 8;
    }
    fm6_dc[p].x1 = x1;
    fm6_dc[p].y1 = y;
    fm6_flt(t, p, out, n);
}

/* ------------------------------------------------------------ the engine --- */
/* VOICE: INIT VOICE, then the packs this build has (the names are the voices' own, NUL-ended in their records) */
#define FM6_PKN(a) a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]
static const char *const N_FM6V[FM6_NVOICE] = {
    FM6_INIT.name, FIF(FELUCCA_FM6_PK_KEYS)(FM6_PKN(FM6_PK_KEYS_N),) FIF(FELUCCA_FM6_PK_BASS)(FM6_PKN(FM6_PK_BASS_N),)
    FIF(FELUCCA_FM6_PK_BELLS)(FM6_PKN(FM6_PK_BELLS_N),) FIF(FELUCCA_FM6_PK_PADS)(FM6_PKN(FM6_PK_PADS_N),)
    FIF(FELUCCA_FM6_PK_80S)(FM6_PKN(FM6_PK_80S_N),)};

/* The ADSR opens at once and rings 10 s: the DX7 envelopes shape the sound and end the voice. Without a pack there is
 * the INIT VOICE only: three starts from it (the packs' voices are in PRESETS after the user presets, nbank.c). */
static const preset_t FM6_PRESETS[] = {
    /* VOICE - - - - - - - */
    {"INIT VOICE", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 0, 0, 0)},
    {"INIT WIDE", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 40, 30, 50)},
    {"INIT MONO", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 1, FX(0, 0, 10, 10)},
};

static const engine_t ENG_FM6 = {
    "FM6", {"PATCH", "FILT"},                            /* (EDIT 2: the filter after the part, fm6_flt) */
    {
        {"VOICE", F_ENUM, 0, FM6_NVOICE - 1, 0, N_FM6V, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},                     /* (EDIT 2..5: empty, an old project's MOD .. ENGINE baked) */
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"LPF", F_PCT, 0, 127, 0, 0, 0},                 /* (fm6_flt: 0 open) */
        {"RES", F_PCT, 0, 127, 0, 0, 0},
        {"HPF", F_PCT, 0, 127, 0, 0, 0},                 /* (0 off) */
    },
    FM6_PRESETS, sizeof(FM6_PRESETS) / sizeof(FM6_PRESETS[0]), -1, fm6_note_on, fm6_render,
    0x5D7F, {P_E0, P_E5, P_E6, P_E7}, NVOICE,             /* (MAIN: VOICE LPF RES HPF) */ fm6_amp, 0, fm6_block, 1, fm6_alloc, fm6_legato, fm6_key,
    fm6_post,
};

/* ------------------------------------------------------ DX7 SysEx in --- */
/* Two Yamaha frames are taken, collected by the USB ISR (usb.c sysex_byte) for the main loop (fm6_sysex.c fm6_service):
 *   F0 43 0n 00 01 1B <155 bytes> <checksum> F7    a voice (VCED): replaces the voice of the FM6 track
 *   F0 43 0n 09 20 00 <4096 bytes> <checksum> F7   32 voices (VMEM): the pick list at the end of the track's PRESETS
 * The 6 header bytes tell which one is coming; the voice goes into fm6_vox, the bank's data and checksum into
 * fm6_pick (the list itself: it is kept, in RAM, until power-off). One frame at a time: a frame arriving while one
 * waits for the main loop is dropped. Anything else (editor traffic, other Yamaha messages) is let go at once */
#define FM6_RX 4104u                                     /* the longest frame: the bank (usb.c: FM6 is built) */
#define FM6_VOXN 163u
#if FELUCCA_FM6_SYSEX
static uint8_t fm6_vox[FM6_VOXN] IN_POOL;               /* the voice frame; the header of a bank */
static uint8_t fm6_pick[4097] IN_POOL;                  /* the bank: 32 packed voices and their checksum */
static volatile uint8_t fm6_pick_ok;                    /* fm6_pick holds a bank whose checksum was right */
static volatile uint8_t fm6_rx_ready;                   /* 1 a voice, 2 a bank waits for the main loop */
static uint8_t fm6_rx_on, fm6_rx_kind;
static uint32_t fm6_rx_n;
#else
#define fm6_pick_ok 0
#endif

static void fm6_sx_byte(uint8_t b)
{
#if FELUCCA_FM6_SYSEX
    if (b == 0xF0) {
        fm6_rx_on = !fm6_rx_ready;
        if (fm6_rx_on)
            fm6_rx_n = fm6_rx_kind = 0;                  /* a pending frame belongs to the main loop */
    }
    if (!fm6_rx_on)
        return;
    if (fm6_rx_n < 6u) {
        fm6_vox[fm6_rx_n++] = b;
        if (fm6_rx_n == 2u && b != 0x43)
            fm6_rx_on = 0;                               /* not Yamaha */
        else if (fm6_rx_n == 6u) {                       /* the header says what comes */
            const uint8_t *h = fm6_vox;
            fm6_rx_kind = (uint8_t)((h[2] & 0xF0u) == 0 && h[3] == 0x00 && h[4] == 0x01 && h[5] == 0x1B ? 1u :
                                    (h[2] & 0xF0u) == 0 && h[3] == 0x09 && h[4] == 0x20 && h[5] == 0x00 ? 2u : 0u);
            fm6_rx_on = fm6_rx_kind != 0;
            if (fm6_rx_kind == 2u)
                fm6_pick_ok = 0;                         /* (the old list goes when a new bank starts) */
        }
        return;
    }
    if (b == 0xF7) {
        fm6_rx_on = 0;
        if ((fm6_rx_kind == 1u && fm6_rx_n == FM6_VOXN - 1u) || (fm6_rx_kind == 2u && fm6_rx_n == FM6_RX - 1u)) {
            RING_PUBLISH();
            fm6_rx_ready = fm6_rx_kind;
        }
        return;
    }
    if (fm6_rx_kind == 1u && fm6_rx_n < FM6_VOXN - 1u)
        fm6_vox[fm6_rx_n++] = b;
    else if (fm6_rx_kind == 2u && fm6_rx_n < FM6_RX - 1u)
        fm6_pick[fm6_rx_n++ - 6u] = b;
    else
        fm6_rx_on = 0;                                   /* too long */
#else
    (void)b;
#endif
}
