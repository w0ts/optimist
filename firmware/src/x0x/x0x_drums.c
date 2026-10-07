/* SPDX-License-Identifier: GPL-3.0-only */
/* The X0X drum kits' float side (FELUCCA_DRUM_X909 / FELUCCA_DRUM_X808; firmware/src/drum_x0x.c is the integer
 * side). Ported from X0X by Charles Vestal (charlesvestal/fm1-x0x 80b7d40, GPL-3.0-only): its TR-909 (drum909.c,
 * from 9W9 by athousanddetails, itself grown out of ER-99 by Matthew Cieplak, GPL-3.0; hi-hat, ride and crash
 * samples from ER-99) and its TR-808 (drum808.c, from 8W8 by athousanddetails, GPL-3.0; the rim shot after sc808
 * by Yoshinosuke Horiuchi / Sam Aaron, MIT). X0X's break player is not used.
 *
 * The rest of the firmware is integer-only: this file is a translation unit of its own, built with X0X's FPU flags
 * (-mcpu=r3 -mfprev1 -ffp-contract=off; tools/build.py) as the ACID engine's acid/acid_dsp.c is, and only the
 * audio ISR runs it (drum_on, drums_mix, drums_off). The host tests include it directly.
 *
 * Instead of X0X's engine (one mono mix per machine with its own sends), every voice of a machine is a channel:
 * the integer side mixes each channel as the drum lane that played it (its level, CUT, sends, the track's
 * mute / solo, pan, peak). Channels 0..10: the 909's voices (DR_BD .. DR_RD); 11..23: the 808's output lanes
 * (L_BD .. L_CY: a tom and a conga of the same track share one). A hit carries its sample offset in the next block,
 * as the other drum kits do; the voice is triggered there (x0x_render renders the channel up to it first). */
#include <stdint.h>
/* Optimist: no fused multiply-add here on the host either (the device has none and builds -ffp-contract=off; a host
 * cc fuses by default): the host tests then compute the device's samples, and the block forms the per-sample ones */
#pragma STDC FP_CONTRACT OFF
#ifndef FELUCCA_DRUM_X909
#define FELUCCA_DRUM_X909 0
#endif
#ifndef FELUCCA_DRUM_X808
#define FELUCCA_DRUM_X808 0
#endif
#ifndef FELUCCA_X909_CYM
#define FELUCCA_X909_CYM 1
#endif
#define X0X_909_CYM FELUCCA_X909_CYM
#if FELUCCA_DRUM_X909
#include "drum909.c"
#endif
#if FELUCCA_DRUM_X808
#include "drum808.c"
#endif
/* X0X's table macros whose names the firmware uses otherwise (the host tests build one unit) */
#undef DIST
#undef DRIVE
#undef DRV
#undef DST
#undef EXP
#undef LEVEL
#undef LIN
#undef LVL
#undef TRK
#undef P_DIST
#undef P_DLY
#undef P_REV

#define X0X_NCH 24u                /* (drum_x0x.c) */
#define X0X_CH808 11u              /* the 808's first channel */
#define X0X_BLK 32                 /* the most samples a block has (CTL) */
/* 1.0 of a voice -> the drum track's integer scale: each machine's kick at a step's velocity peaks as the
 * synthesised 909 / 808 kit's kick does (tests/x0x_drums_test.c measures it); the 808 at its kit level 127, as X0X
 * sets it */
#define X909_GAIN 16800.0f
#define X808_GAIN 104000.0f
#define X0X_LIM 1048576.0f

typedef struct {
    int8_t ofs[8];                 /* TUNE DECAY SNAP CLICK BEND CUT DRIVE LEVEL (drum_edit.c DE_*) */
    uint8_t snd, vel, at, on;
} x0x_pend_t;

static struct {
    uint8_t ready;
    x0x_pend_t pend[X0X_NCH];
    float buf[X0X_BLK], scratch[X0X_BLK];
#if FELUCCA_DRUM_X909
    const float *nz;               /* this block's shared noise (drum909_render's) */
#endif
#if FELUCCA_DRUM_X808
    float bus[X0X_BLK], pair[X0X_BLK];   /* this block's metal bank */
#endif
} xd;
#if FELUCCA_DRUM_X909
static drum909_t x909 __attribute__((section(".pool")));
#endif
#if FELUCCA_DRUM_X808
static drum808_t x808 __attribute__((section(".pool")));
#endif

static void x0x_ready(void)
{
    if (xd.ready)
        return;
#if FELUCCA_DRUM_X909
    drum909_init(&x909);
#endif
#if FELUCCA_DRUM_X808
    drum808_init(&x808);
    drum808_set(&x808, D8_KIT, 0, 127);           /* kit level: X0X's factory setting */
#endif
    xd.ready = 1;
}

static int x0x_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- the 909: the lane's offsets on 9W9's pots (their index in each voice's list, -1 = none) */
#if FELUCCA_DRUM_X909
/* per voice: TUNE pot, its pots per TUNE step x16 (a semitone on the EXP pitch curves), DECAY, SNAP, CLICK, DRIVE */
static const int16_t X9_POT[DR_NUM][6] = {
    /* BD */ {0, 42, 2, -1, 1, 6},      /* TUNE: 9W9's (the sweep), 2.6 pots a step; CLICK: Attack */
    /* SD */ {0, 130, 1, 2, -1, 4},     /* DECAY: Tone (the noise's decay); SNAP: Snappy */
    /* LT */ {0, 200, 1, -1, 3, 4},     /* CLICK: Attack (the stick) */
    /* MT */ {0, 263, 1, -1, 3, 4},
    /* HT */ {0, 270, 1, -1, 3, 4},
    /* RS */ {1, 169, -1, -1, -1, 2},
    /* CP */ {1, 153, 2, -1, -1, 3},    /* DECAY: Tail */
    /* CH */ {2, 42, 0, -1, -1, 3},
    /* OH */ {2, 42, 0, -1, -1, 3},
    /* CR */ {0, 42, 2, -1, -1, 3},
    /* RD */ {0, 42, 2, -1, -1, 3},
};

static void x9_pot(int voice, int i, int delta)
{
    int v;
    if (i < 0)
        return;
    v = x0x_clamp(drum909_param(voice, i)->def + delta, 0, 127);
    if (drum909_get(&x909, voice, i) != v)
        drum909_set(&x909, voice, i, v);
}

static void x9_trigger(const x0x_pend_t *p)
{
    const int16_t *k = X9_POT[p->snd];
    x9_pot(p->snd, k[0], (p->ofs[0] * k[1] + (p->ofs[0] < 0 ? -8 : 8)) / 16);
    x9_pot(p->snd, k[2], 2 * p->ofs[1]);
    x9_pot(p->snd, k[3], 2 * p->ofs[2]);
    x9_pot(p->snd, k[4], 2 * p->ofs[3]);
    x9_pot(p->snd, k[5], 2 * p->ofs[6]);
    /* X0X's sequencer: an unaccented step at 88 / 127, an accent at 1.0; ours: 100 = a step, 127 = HARD */
    drum909_trigger(&x909, p->snd, p->vel >= 127u ? 1.0f : (float)p->vel * 0.00693f);
}

static int x9_on(int v)
{
    if (v <= DR_HT)
        return v < 2 ? x909.bt[v].mute > 0 : x909.tom[v - DR_LT].mute > 0;
    if (v == DR_RS)
        return x909.rim.mute > 0;
    if (v == DR_CP)
        return x909.clap.mute > 0;
    return x909.smp[v - DR_CH].mute > 0 && x909.smp[v - DR_CH].playing;
}

/* voice v's samples [from, to) into out (zeroed by the caller) */
static void x9_run(int v, float *out, int from, int to)
{
    d9_bus_t bus;
    const float *nz = xd.nz + from;
    int n = to - from;
    if (n <= 0 || !x9_on(v))
        return;
    bus.dry = out + from;
    bus.rev = bus.dly = 0;
    bus.srev = bus.sdly = 0.0f;
    switch (v) {
    case DR_BD: d9_render_bd(&x909.bt[DR_BD], nz, &bus, n); break;
    case DR_SD: d9_render_sd(&x909.bt[DR_SD], nz, &bus, n); break;
    case DR_LT: case DR_MT: case DR_HT: d9_render_tom(&x909.tom[v - DR_LT], &x909.bt[v], nz, &bus, n); break;
    case DR_RS: d9_render_rim(&x909.rim, nz, &bus, n); break;
    case DR_CP: d9_render_clap(&x909.clap, nz, &bus, n); break;
    default: d9_render_smp(&x909.smp[v - DR_CH], &bus, n); break;
    }
}

/* the block's shared noise, as drum909_render makes it (every block, sounding or not: the same noise) */
static void x9_block(int n)
{
    drum909_t *d = &x909;
    int i;
    if (d->nz_pos + n > DR_NZ_BUF) {
        for (i = 0; i < DR_NZ_HIST; ++i)
            d->nz_buf[i] = d->nz_buf[d->nz_pos - DR_NZ_HIST + i];
        d->nz_pos = DR_NZ_HIST;
    }
    xd.nz = d->nz_buf + d->nz_pos;
    d->nz_pos += n;
    for (i = 0; i < n; ++i)
        d->nz_buf[d->nz_pos - n + i] = d9_noise(&d->noise);
}
#endif

/* ---- the 808: the lane's offsets on 8W8's pots (per sound: slots LEVEL TUNE DECAY DRIVE DIST REV DLY X1 X2) */
#if FELUCCA_DRUM_X808
/* per sound: its pots per TUNE step x16, the SNAP / CLICK / CUT (its tone) slot (0 = none) */
static const uint8_t X8_POT[D8S_NUM][4] = {
    /* BD */ {85, 0, D8P_X2, D8P_X1},   /* TUNE: semitones (+-12); CLICK: Attack; CUT: Tone */
    /* SD */ {85, D8P_X1, 0, 0},        /* SNAP: Snappy (its Tone stays centred, as 8W8 has it) */
    /* LT */ {42, 0, 0, 0},             /* toms, congas: TUNE in 1/12 semitones (their range is +-2) */
    /* MT */ {42, 0, 0, 0}, {42, 0, 0, 0}, {42, 0, 0, 0}, {42, 0, 0, 0}, {42, 0, 0, 0},
    /* RS */ {85, 0, 0, 0}, /* CL */ {85, 0, 0, 0},
    /* MA */ {85, 0, D8P_X1, 0},        /* CLICK: Attack */
    /* CP */ {85, 0, 0, 0}, /* CB */ {85, 0, 0, 0}, /* CH */ {85, 0, 0, 0}, /* OH */ {85, 0, 0, 0},
    /* CY */ {85, 0, 0, 0},
};
/* the track that plays a sound and its switch (8W8: LT/MT/HT tom|conga, RS rim|clave, CP clap|maracas) */
static const uint8_t X8_TRACK[D8S_NUM][2] = {
    {D8_BD, 0}, {D8_SD, 0}, {D8_LT, 0}, {D8_MT, 0}, {D8_HT, 0}, {D8_LT, 1}, {D8_MT, 1}, {D8_HT, 1},
    {D8_RS, 0}, {D8_RS, 1}, {D8_CP, 1}, {D8_CP, 0}, {D8_CB, 0}, {D8_CH, 0}, {D8_OH, 0}, {D8_CY, 0}};
/* a sound's output lane (its channel - X0X_CH808) */
static const uint8_t X8_LANE[D8S_NUM] = {L_BD, L_SD, L_T0, L_T1, L_T2, L_T0, L_T1, L_T2,
                                         L_RS, L_CL, L_MA, L_CP, L_CB, L_CH, L_OH, L_CY};

static void x8_pot(int s, int slot, int delta)
{
    int v = x0x_clamp(k_spec[s][slot].def + delta, 0, 127);
    if (x808.pot[s][slot] != v)
        set_pot(&x808, s, slot, v);
}

static void x8_trigger(const x0x_pend_t *p)
{
    const uint8_t *k = X8_POT[p->snd];
    int s = p->snd;
    float vel = p->vel >= 127u ? 1.0f : (float)p->vel * 0.00693f;
    x8_pot(s, D8P_TUNE, (p->ofs[0] * k[0] + (p->ofs[0] < 0 ? -8 : 8)) / 16);
    x8_pot(s, D8P_DECAY, 2 * p->ofs[1]);
    if (k[1])
        x8_pot(s, k[1], 2 * p->ofs[2]);
    if (k[2])
        x8_pot(s, k[2], 2 * p->ofs[3]);
    if (k[3])
        x8_pot(s, k[3], 2 * p->ofs[5]);
    x8_pot(s, D8P_DRIVE, 2 * p->ofs[6]);
    x808.sw[X8_TRACK[s][0]] = X8_TRACK[s][1];
    /* X0X: the 808's unaccented hit sits at D8_VEL_NORMAL (engine.c s_drum) */
    drum808_trigger(&x808, X8_TRACK[s][0], vel >= 0.999f ? 1.0f : vel * (D8_VEL_NORMAL / (88.0f / 127.0f)));
}

/* lane l's samples [from, to) into out (zeroed by the caller), as drum808_render runs it */
static void x8_run(int l, float *out, int from, int to)
{
    d8_lane_t *ln = &x808.lane[l];
    int m = to - from, mm = m, got, i;
    if (m <= 0 || !lane_on(&x808, l))
        return;
    if (ln->cstep < 0.0f) {                       /* 8W8 stops calling a voice whose choke ran out */
        float c = ln->cg;
        for (i = 0; i < m; i++) {
            c += ln->cstep;
            if (c <= 0.0f) {
                mm = i + 1;
                break;
            }
        }
    }
    got = voice_run(&x808, l, xd.buf, mm, xd.bus + from, xd.pair + from);
    lane_mix(&x808, ln, xd.buf, got, out + from, xd.scratch, xd.scratch);
#if D8_TAIL_DB
    lane_tail(&x808, l, xd.buf, got);
#endif
}

static int x8_metal(int l) { return l == L_CB || l == L_CH || l == L_OH || l == L_CY; }
#endif

/* ---- the interface (integers only) */

/* a hit of sound snd (0..10: the 909's DR_*; 16..31: the 808's D8S_* + 16) at velocity vel (1..127), with the
 * lane's offsets ofs (DE_*, 0 = none), at sample `at` of the next block -> its channel (0xFF: not built) */
uint32_t x0x_hit(uint32_t snd, uint32_t vel, const int8_t *ofs, uint32_t at)
{
    uint32_t ch = 0xFFu, i;
    x0x_pend_t *p;
    x0x_ready();
#if FELUCCA_DRUM_X909
    if (snd < DR_NUM)
        ch = snd;
#endif
#if FELUCCA_DRUM_X808
    if (snd >= 16u && snd < 16u + D8S_NUM)
        ch = X0X_CH808 + X8_LANE[snd - 16u];
#endif
    if (ch == 0xFFu)
        return ch;
    p = &xd.pend[ch];
    p->snd = (uint8_t)(snd & 15u);
    p->vel = (uint8_t)(vel > 127u ? 127u : vel ? vel : 1u);
    p->at = (uint8_t)(at < X0X_BLK ? at : 0u);
    p->on = 1;
    for (i = 0; i < 8u; i++)
        p->ofs[i] = ofs ? ofs[i] : 0;
    return ch;
}

static void x0x_trigger(uint32_t ch)
{
    x0x_pend_t *p = &xd.pend[ch];
    p->on = 0;
#if FELUCCA_DRUM_X909
    if (ch < X0X_CH808)
        x9_trigger(p);
#endif
#if FELUCCA_DRUM_X808
    if (ch >= X0X_CH808)
        x8_trigger(p);
#endif
}

/* a block of n (<= 32) samples starts: the shared noise and metal bank; -> a bit per channel that sounds or has a
 * hit due (x0x_render each of them, once) */
uint32_t x0x_block(uint32_t n)
{
    uint32_t mask = 0, c;
    if (!xd.ready)
        return 0;
    for (c = 0; c < X0X_NCH; c++)
        if (xd.pend[c].on)
            mask |= 1u << c;
#if FELUCCA_DRUM_X909
    for (c = 0; c < DR_NUM; c++)
        if (x9_on((int)c))
            mask |= 1u << c;
    if (mask & ((1u << DR_NUM) - 1u))             /* (X0X runs the noise on through silence too: only its filters'
                                                    * warm-up reads it, and any noise is as good there) */
        x9_block((int)n);
#endif
#if FELUCCA_DRUM_X808
    {
        uint32_t metal = 0;
        for (c = 0; c < L_NUM; c++)
            if (lane_on(&x808, (int)c) || xd.pend[X0X_CH808 + c].on) {
                mask |= 1u << (X0X_CH808 + c);
                metal |= (uint32_t)x8_metal((int)c);
            }
        if (metal)                                /* the shared bank runs only while a metal voice sounds */
            bank_block(&x808.bank, xd.bus, xd.pair, (int)n);   /* (Optimist: by blocks, the same bus) */
    }
#endif
    (void)n;
    return mask;
}

/* channel ch's n samples (its hit due: from its offset) at gain (Q15: 32767 = the drum track's scale) into out,
 * or added to it */
void x0x_render(uint32_t ch, int32_t *out, uint32_t n, int32_t gain, int add)
{
    float y[X0X_BLK], g;
    uint32_t i, at = xd.pend[ch].on ? xd.pend[ch].at : (uint32_t)n;
    if (n > X0X_BLK)
        n = X0X_BLK;
    if (at > n)
        at = n;
    for (i = 0; i < n; i++)
        y[i] = 0.0f;
    for (int seg = 0; seg < 2; seg++) {
        int from = seg ? (int)at : 0, to = seg ? (int)n : (int)at;
        if (seg && xd.pend[ch].on)
            x0x_trigger(ch);
#if FELUCCA_DRUM_X909
        if (ch < X0X_CH808)
            x9_run((int)ch, y, from, to);
#endif
#if FELUCCA_DRUM_X808
        if (ch >= X0X_CH808)
            x8_run((int)(ch - X0X_CH808), y, from, to);
#endif
    }
    g = (ch < X0X_CH808 ? X909_GAIN : X808_GAIN) * ((float)gain * (1.0f / 32768.0f));
    for (i = 0; i < n; i++) {
        float s = y[i] * g;
        int32_t v;
        s = s > X0X_LIM ? X0X_LIM : s < -X0X_LIM ? -X0X_LIM : s;
        v = (int32_t)s;
        out[i] = add ? out[i] + v : v;
    }
}

#if FELUCCA_CPU_GUARD && FELUCCA_DRUM_X808 && D8_TAIL_DB
/* Optimist: the CPU guard's quality level (cpuguard.c): the 808's tails end 40 dB under the hit's peak, not 60 */
void x0x_tails(uint32_t shorter) { d8_tail_k = shorter ? 1.0e-2f : D8_TAIL_K; }
#endif

/* every voice stops now (MIDI All Sound Off; the integer side fades what they played) */
void x0x_off(void)
{
    uint32_t c;
    if (!xd.ready)
        return;
    for (c = 0; c < X0X_NCH; c++)
        xd.pend[c].on = 0;
#if FELUCCA_DRUM_X909
    for (c = 0; c < 5u; c++)
        if (c < 2u)
            x909.bt[c].mute = 0;
        else
            x909.tom[c - 2u].mute = 0;
    x909.rim.mute = x909.clap.mute = 0;
    for (c = 0; c < 4u; c++)
        x909.smp[c].mute = 0;
#endif
#if FELUCCA_DRUM_X808
    for (c = 0; c < L_NUM; c++)
        voice_stop(&x808, (int)c);
#endif
}
