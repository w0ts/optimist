/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal (X0X: charlesvestal/fm1-x0x 80b7d40; dsp/drum909*, dsp/drum808*) */
/* The X0X kits (FELUCCA_DRUM_X909: kit UID 37 "X0X 909", FELUCCA_DRUM_X808: UID 38 "X0X 808"; registry.h), the
 * integer side; included by drums.c, before drum_on. Ported from X0X by Charles Vestal (GPL-3.0): its circuit-
 * modelled TR-909 (from 9W9 by athousanddetails, itself grown out of ER-99 by Matthew Cieplak; BD SD toms RS CP
 * modelled, the hi-hats, ride and crash ER-99's samples) and TR-808 (from 8W8 by athousanddetails; 16 sounds,
 * the rim shot after sc808). The models are float: x0x/x0x_drums.c, a unit of its own (X0X's FPU flags), which
 * this file talks to with integers only (x0x_*). X0X's break player is not used. Optimist: style kits (UIDs 39..46,
 * X0X_STYLE: X9 TECH, X9 HOUSE, X9 UKG, X9 ACID, X8 TRAP, X8 BOOM, X8 ELEC, X8 MIAMI), a machine's voices with SOUND
 * settings of their own; and any voice on any lane (drum_edit.c DL_X909 / DL_X808, x0x_code).
 *
 * A GM note plays one of the machine's sounds (X9_NOTE / X8_NOTE), some as a variant (offsets under the lane's:
 * KICK 2 a longer kick, PEDAL HAT a shorter closed hat, SNARE 2 a brighter snare...). The 16 lanes:
 *   909  KICK BD, KICK 2 BD (longer), SNARE SD, CLAP CP, CLOSED HAT CH, OPEN HAT OH, PEDAL HAT CH (shorter), RIM RS,
 *        SNARE 2 SD (brighter), LOW TOM LT, HI TOM HT, CRASH CR, RIDE RD; SHAKER, CONGA, COWBELL: the 909 has
 *        none, they play the stand-in's (the synthesised 909 kit's) as every note it lacks
 *   808  KICK BD, KICK 2 BD (the long boom), SNARE SD, CLAP CP, CLOSED HAT CH, OPEN HAT OH, PEDAL HAT CH (shorter), RIM RS,
 *        SNARE 2 SD (brighter), LOW TOM LT, HI TOM HT, CRASH CY, RIDE CY (shorter, higher), SHAKER MA (maracas),
 *        CONGA MC (mid conga), COWBELL CB; MIDI also reaches MT, LC, HC and CL (claves, note 75)
 * Each machine voice is a channel (24: the 909's 11, the 808's 13 output lanes): one hit at a time, as on the
 * machines (a retrigger restarts the voice; CH cuts OH). The lane that played it gives its sends (drum_sends.c),
 * LEVEL and CUT- (a low-pass, as on samples); TUNE DECAY SNAP CLICK DRIVE (and CUT on the 808's BD: its
 * Tone) set the model's own parameters at the hit (x0x_drums.c X9_POT / X8_POT), BEND does not apply. Velocity
 * as X0X plays it: 100 (a step) is X0X's unaccented step, 127 (HARD) its accent. */
#define X0X_NCH 24u
uint32_t x0x_hit(uint32_t snd, uint32_t vel, const int8_t *ofs, uint32_t at);   /* x0x/x0x_drums.c (float) */
uint32_t x0x_block(uint32_t n);
void x0x_render(uint32_t ch, int32_t *out, uint32_t n, int32_t gain, int add);
void x0x_off(void);
#if FELUCCA_CPU_GUARD && FELUCCA_DRUM_X808
void x0x_tails(uint32_t shorter);                  /* (the CPU guard's quality level: the 808's tails at -40 dB) */
#endif
#ifndef __PI32V2__
#include "x0x/x0x_drums.c"                         /* (the host tests: one unit) */
#endif

#define XN_NONE 0xFFu                              /* a note the machine lacks: the stand-in kit plays it */
#define XN(snd, var) (uint8_t)((snd) | (var) << 5)
/* GM notes 35..81 -> the 909's voice (DR_* order: BD SD LT MT HT RS CP CH OH CR RD) and a variant */
static const uint8_t X9_NOTE[81 - 35 + 1] = {
    /* 35 */ XN(0, 1), XN(0, 0), XN(5, 0), XN(1, 0), XN(6, 0), XN(1, 3), XN(2, 4), XN(7, 0), XN(2, 0), XN(7, 2),
    /* 45 */ XN(3, 4), XN(8, 0), XN(3, 0), XN(4, 0), XN(9, 0), XN(4, 5), XN(10, 0), XN(9, 4), XN(10, 7), XN_NONE,
    /* 55 */ XN(9, 6), XN_NONE, XN(9, 5), XN_NONE, XN(10, 4), XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE,
    /* 65 */ XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE,
    /* 75 */ XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE};
/* GM notes -> the 808's sound (D8S_*: BD SD LT MT HT LC MC HC RS CL MA CP CB CH OH CY) and a variant */
static const uint8_t X8_NOTE[81 - 35 + 1] = {
    /* 35 */ XN(0, 1), XN(0, 0), XN(8, 0), XN(1, 0), XN(11, 0), XN(1, 3), XN(2, 4), XN(13, 0), XN(2, 0), XN(13, 2),
    /* 45 */ XN(3, 4), XN(14, 0), XN(3, 0), XN(4, 0), XN(15, 0), XN(4, 5), XN(15, 7), XN(15, 6), XN(15, 7), XN(10, 0),
    /* 55 */ XN(15, 6), XN(12, 0), XN(15, 0), XN_NONE, XN(15, 7), XN(7, 5), XN(7, 0), XN(6, 6), XN(6, 0), XN(5, 0),
    /* 65 */ XN_NONE, XN_NONE, XN(12, 0), XN_NONE, XN(10, 0), XN(10, 0), XN_NONE, XN_NONE, XN_NONE, XN_NONE,
    /* 75 */ XN(9, 0), XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE, XN_NONE};
/* the variants 1..7 (offsets DE_* added under the lane's own; TUNE in the sound's units, below) */
static const int8_t X9_VAR[8][DE_N] = {
    {0}, {6, 12, 0, -10}, {0, -24}, {3, -10, 20}, {-3}, {3}, {0, -30}, {5, -15}};   /* -, KICK 2, PEDAL HAT, SNARE 2,
                                                                                      * lower, higher, short, bell */
static const int8_t X8_VAR[8][DE_N] = {
    {0}, {-2, 30, 0, -10}, {0, -24}, {3, -10, 16, 0, 0, 10}, {-12}, {12}, {0, -30}, {2, -20}};   /* ..., ride */
/* what the SOUND pages show per sound: a bit per DE_* value, XS_SEMI: TUNE in semitones (else in steps: the
 * 909 kick's own TUNE, 2.6 pots a step; the 808's toms and congas, 1/12 semitone), XS_TONE: CUT is the sound's
 * tone control (else CUT - is a low-pass on its output) */
#define XS_SEMI 0x100u
#define XS_TONE 0x200u
#define XS_BASE ((1u << DE_TUNE) | (1u << DE_CUT) | (1u << DE_DRIVE) | (1u << DE_LEVEL))
#define XS_D (1u << DE_DECAY)
#define XS_S (1u << DE_SNAP)
#define XS_C (1u << DE_CLICK)
static const uint16_t X9_SHOW[11] = {
    XS_BASE | XS_D | XS_C, XS_BASE | XS_D | XS_S | XS_SEMI, XS_BASE | XS_D | XS_C | XS_SEMI,
    XS_BASE | XS_D | XS_C | XS_SEMI, XS_BASE | XS_D | XS_C | XS_SEMI, XS_BASE | XS_SEMI, XS_BASE | XS_D | XS_SEMI,
    XS_BASE | XS_D | XS_SEMI, XS_BASE | XS_D | XS_SEMI, XS_BASE | XS_D | XS_SEMI, XS_BASE | XS_D | XS_SEMI};
static const uint16_t X8_SHOW[16] = {
    XS_BASE | XS_D | XS_C | XS_SEMI | XS_TONE, XS_BASE | XS_D | XS_S | XS_SEMI,
    XS_BASE | XS_D, XS_BASE | XS_D, XS_BASE | XS_D, XS_BASE | XS_D, XS_BASE | XS_D, XS_BASE | XS_D,
    XS_BASE | XS_D | XS_SEMI, XS_BASE | XS_D | XS_SEMI, XS_BASE | XS_D | XS_C | XS_SEMI, XS_BASE | XS_D | XS_SEMI,
    XS_BASE | XS_D | XS_SEMI, XS_BASE | XS_D | XS_SEMI, XS_BASE | XS_D | XS_SEMI, XS_BASE | XS_D | XS_SEMI};
static const char *const X9_SND_NAME[11] = {"BD", "SD", "LT", "MT", "HT", "RS", "CP", "CH", "OH", "CR", "RD"};
static const char *const X8_SND_NAME[16] = {"BD", "SD", "LT", "MT", "HT", "LC", "MC", "HC",
                                            "RS", "CL", "MA", "CP", "CB", "CH", "OH", "CY"};

/* kit k's sound for note: XN() code, XN_NONE (not this machine's, or a kit not built) */
static uint32_t x0x_note(uint32_t k, uint32_t note)
{
    uint32_t c;
    k = DRUM_UID_XMACH(k);
    if (note < 35u || note > 81u)
        return XN_NONE;
    if (FELUCCA_DRUM_X909 && k == DRUM_UID_X909) {
        c = X9_NOTE[note - 35u];
        if (!FELUCCA_X909_CYM && c != XN_NONE && ((c & 31u) == 9u || (c & 31u) == 10u))
            return XN_NONE;                         /* (no ride / crash samples in this build) */
        return c;
    }
    if (FELUCCA_DRUM_X808 && k == DRUM_UID_X808)
        return X8_NOTE[note - 35u];
    return XN_NONE;
}
/* Optimist: lane l's X0X sound on machine k: the voice its source names (DL_X909 / DL_X808: any voice on any lane,
 * as it is, no variant), else the kit's sound for note */
static uint32_t x0x_code(uint32_t k, uint32_t l, uint32_t note)
{
    k = DRUM_UID_XMACH(k);
#if FELUCCA_DRUM_KITS
    uint32_t s = l < DRUM_LANES ? dl.src[l] : 0u;
    if (FELUCCA_DRUM_X909 && k == DRUM_UID_X909 && s >= DL_X909 && s < DL_X909 + DL_X909_N)
        return !FELUCCA_X909_CYM && s - DL_X909 >= 9u ? XN_NONE : XN(s - DL_X909, 0);   /* (CR RD: no samples) */
    if (FELUCCA_DRUM_X808 && k == DRUM_UID_X808 && s >= DL_X808 && s < DL_X808 + DL_X808_N)
        return XN(s - DL_X808, 0);
#endif
    (void)l;
    return x0x_note(k, note);
}
/* the kit that plays the notes an X0X kit lacks: the synthesised 909 / 808, else the first sampled kit */
static uint32_t x0x_standin(uint32_t k)
{
    k = DRUM_UID_XMACH(k);
    if (FELUCCA_DRUM_SYNTH)
        return k == DRUM_UID_X909 ? DRUM_SAMPLED + 1u : DRUM_SAMPLED;
    return DRUM_SFIRST;
}
/* what lane l shows on the SOUND pages when it plays X0X kit k (XS_*), 0 = not an X0X sound */
static uint32_t x0x_show(uint32_t k, uint32_t l)
{
    uint32_t c = x0x_code(k, l & 15u, LANE_NOTE[l & 15u]);
    k = DRUM_UID_XMACH(k);
    if (c == XN_NONE)
        return 0;
    return k == DRUM_UID_X909 ? X9_SHOW[c & 31u] : X8_SHOW[c & 15u];
}
/* Optimist: the style kits (UIDs 39..46, DRUM_UID_XSTYLE): a machine's voices with their own SOUND settings, as
 * offsets (DE_*: TUNE in the voice's steps, DECAY SNAP CLICK CUT DRIVE as on the SOUND pages, LEVEL dB) per lane, over
 * the kit's own (its note's variant); a lane's own edits add to them. {style, lane, DE_*, offset} */
enum { XS_TECH, XS_HOUSE, XS_UKG, XS_ACID, XS_TRAP, XS_BOOM, XS_ELEC, XS_MIAMI };
static const int8_t X0X_STYLE[][4] = {
    /* X9 TECH: a short punchy kick with more drive and click, a driven snare and clap, tight hats */
    {XS_TECH, 0, DE_DECAY, -12}, {XS_TECH, 0, DE_DRIVE, 24}, {XS_TECH, 0, DE_CLICK, 12}, {XS_TECH, 0, DE_LEVEL, 1},
    {XS_TECH, 2, DE_DRIVE, 14}, {XS_TECH, 3, DE_DRIVE, 12}, {XS_TECH, 4, DE_DECAY, -12}, {XS_TECH, 5, DE_DECAY, -10},
    {XS_TECH, 6, DE_DECAY, -10}, {XS_TECH, 9, DE_DRIVE, 16}, {XS_TECH, 10, DE_DRIVE, 16},
    /* X9 HOUSE: a rounder, longer kick, a snappier snare, loose hats, a clap with more tail */
    {XS_HOUSE, 0, DE_DECAY, 8}, {XS_HOUSE, 0, DE_CLICK, -6}, {XS_HOUSE, 0, DE_DRIVE, -8}, {XS_HOUSE, 2, DE_SNAP, 12},
    {XS_HOUSE, 3, DE_DECAY, 10}, {XS_HOUSE, 4, DE_DECAY, 8}, {XS_HOUSE, 5, DE_DECAY, 12}, {XS_HOUSE, 7, DE_LEVEL, 2},
    {XS_HOUSE, 11, DE_DECAY, 8}, {XS_HOUSE, 12, DE_DECAY, 8},
    /* X9 UKG: a short higher kick, a tight bright snare and rim, very tight hats, a short clap */
    {XS_UKG, 0, DE_DECAY, -8}, {XS_UKG, 0, DE_TUNE, 3}, {XS_UKG, 2, DE_TUNE, 2}, {XS_UKG, 2, DE_SNAP, 16},
    {XS_UKG, 2, DE_DECAY, -12}, {XS_UKG, 3, DE_DECAY, -10}, {XS_UKG, 4, DE_DECAY, -20}, {XS_UKG, 5, DE_DECAY, -16},
    {XS_UKG, 6, DE_DECAY, -16}, {XS_UKG, 7, DE_TUNE, 3}, {XS_UKG, 7, DE_LEVEL, 2},
    /* X9 ACID: everything through more drive */
    {XS_ACID, 0, DE_DRIVE, 32}, {XS_ACID, 0, DE_DECAY, -4}, {XS_ACID, 2, DE_DRIVE, 24}, {XS_ACID, 3, DE_DRIVE, 24},
    {XS_ACID, 4, DE_DRIVE, 20}, {XS_ACID, 4, DE_DECAY, -6}, {XS_ACID, 5, DE_DRIVE, 20}, {XS_ACID, 7, DE_DRIVE, 20},
    {XS_ACID, 9, DE_DRIVE, 24}, {XS_ACID, 10, DE_DRIVE, 24},
    /* X8 TRAP: a long, low, dark boom, a snappy higher snare, tight hats */
    {XS_TRAP, 0, DE_DECAY, 30}, {XS_TRAP, 0, DE_TUNE, -2}, {XS_TRAP, 0, DE_CUT, -10}, {XS_TRAP, 1, DE_DECAY, 34},
    {XS_TRAP, 2, DE_SNAP, 12}, {XS_TRAP, 2, DE_TUNE, 2}, {XS_TRAP, 3, DE_LEVEL, 1}, {XS_TRAP, 4, DE_DECAY, -20},
    {XS_TRAP, 5, DE_DECAY, -12}, {XS_TRAP, 6, DE_DECAY, -16},
    /* X8 BOOM: the longest kick, driven, a longer snare, short hats */
    {XS_BOOM, 0, DE_DECAY, 40}, {XS_BOOM, 0, DE_DRIVE, 16}, {XS_BOOM, 0, DE_TUNE, -3}, {XS_BOOM, 1, DE_DECAY, 40},
    {XS_BOOM, 1, DE_DRIVE, 16}, {XS_BOOM, 2, DE_DECAY, 10}, {XS_BOOM, 4, DE_DECAY, -8}, {XS_BOOM, 5, DE_DECAY, -6},
    /* X8 ELEC: a short clicky kick with drive, a snappy snare, louder clap and cowbell, short toms */
    {XS_ELEC, 0, DE_DECAY, -12}, {XS_ELEC, 0, DE_CLICK, 24}, {XS_ELEC, 0, DE_DRIVE, 10}, {XS_ELEC, 2, DE_SNAP, 20},
    {XS_ELEC, 2, DE_TUNE, 2}, {XS_ELEC, 3, DE_LEVEL, 2}, {XS_ELEC, 4, DE_DECAY, -10}, {XS_ELEC, 9, DE_DECAY, -12},
    {XS_ELEC, 10, DE_DECAY, -12}, {XS_ELEC, 15, DE_LEVEL, 2},
    /* X8 MIAMI: a deep long bass kick, tight hats, a higher clap */
    {XS_MIAMI, 0, DE_DECAY, 26}, {XS_MIAMI, 0, DE_TUNE, -4}, {XS_MIAMI, 0, DE_CUT, -12}, {XS_MIAMI, 1, DE_DECAY, 44},
    {XS_MIAMI, 1, DE_TUNE, -4}, {XS_MIAMI, 3, DE_TUNE, 2}, {XS_MIAMI, 4, DE_DECAY, -12}, {XS_MIAMI, 5, DE_DECAY, -12},
    {XS_MIAMI, 6, DE_DECAY, -12},
};
_Static_assert(XS_MIAMI + 1 == DRUM_NXSTYLE, "a style per kit UID 39..46");
/* style kit k's offset of lane l's value d (0: another kit, or none) */
static int32_t x0x_style(uint32_t k, uint32_t l, uint32_t d)
{
    uint32_t i;
    if (k < DRUM_UID_XSTYLE || k >= DRUM_UID_XSTYLE + DRUM_NXSTYLE)
        return 0;
    for (i = 0; i < sizeof X0X_STYLE / sizeof X0X_STYLE[0]; i++)
        if ((uint32_t)X0X_STYLE[i][0] == k - DRUM_UID_XSTYLE && (uint32_t)X0X_STYLE[i][1] == l &&
            (uint32_t)X0X_STYLE[i][2] == d)
            return X0X_STYLE[i][3];
    return 0;
}
static int32_t x0x_var_decay(uint32_t k, uint32_t l)    /* the DECAY its variant and style add (the graph) */
{
    uint32_t c = x0x_code(k, l & 15u, LANE_NOTE[l & 15u]);
    return c == XN_NONE ? 0 : (DRUM_UID_XMACH(k) == DRUM_UID_X909 ? X9_VAR : X8_VAR)[c >> 5][DE_DECAY] +
                                  x0x_style(k, l & 15u, DE_DECAY);
}
static const char *x0x_snd_name(uint32_t k, uint32_t l)
{
    uint32_t c = x0x_code(k, l & 15u, LANE_NOTE[l & 15u]);
    k = DRUM_UID_XMACH(k);
    return c == XN_NONE ? "" : k == DRUM_UID_X909 ? X9_SND_NAME[c & 31u] : X8_SND_NAME[c & 15u];
}

/* per channel: the lane that played it (its note: the sends), its LEVEL (Q12, 0 = 0 dB) and CUT - (a one-pole
 * coefficient, 0 = none) and its state; the last sample of each path (the declick tail) */
static struct {
    uint8_t note[X0X_NCH];
    int32_t cut[X0X_NCH], flt[X0X_NCH], lg[X0X_NCH], last[X0X_NCH + 1];   /* last[X0X_NCH]: the shared sum */
    uint32_t live;                                 /* a bit per channel sounding or with a hit due (x0x_sounding) */
} xc;

/* an X0X voice sounds, or a hit is due in this block: drums_mix's glides (FELUCCA_GLIDE) and the quiet test
 * (project.c audio_quiet) count the X0X channels, which are not drums.v voices */
AINL uint32_t x0x_sounding(void) { return xc.live; }   /* (inlined: drums_mix runs from RAM) */

/* a hit of note on X0X kit k (lane l); 0: the machine lacks it (the stand-in plays it) */
static int x0x_on(uint32_t k, uint32_t note, uint32_t vel, uint32_t l)
{
    uint32_t c = x0x_code(k, l, note), ch, i, st = k;
    const int8_t *u = dl_ofs(l), *v;
    int8_t o[DE_N];
    if (c == XN_NONE)
        return 0;
    k = DRUM_UID_XMACH(k);                          /* (a style kit: its machine, its offsets below) */
    v = (k == DRUM_UID_X909 ? X9_VAR : X8_VAR)[c >> 5];
    for (i = 0; i < DE_N; i++)
        o[i] = (int8_t)clamp(v[i] + x0x_style(st, l, i) + (u ? u[i] : 0), -64, 63);
    ch = x0x_hit(k == DRUM_UID_X909 ? (c & 31u) : 16u + (c & 15u), vel, o, ev_ofs < CTL ? ev_ofs : 0u);
    if (ch >= X0X_NCH)
        return 0;
    xc.note[ch] = (uint8_t)note;
    xc.live |= 1u << ch;                           /* (due in this block: drums_mix runs drums_x0x) */
    xc.lg[ch] = o[DE_LEVEL] ? (int32_t)(pow2_q16(clamp(o[DE_LEVEL], -24, 6) * 32) >> 4) : 0;   /* 2^(dB / 6.02) */
    xc.cut[ch] = o[DE_CUT] < 0 && !(((k == DRUM_UID_X909 ? X9_SHOW[c & 31u] : X8_SHOW[c & 15u])) & XS_TONE)
                     ? ds_onepole((uint32_t)clamp(127 + 2 * o[DE_CUT], 20, 127)) : 0;
    return 1;
}

/* every X0X voice stops now, its last sample fading (the declick tail) */
static void x0x_all_off(void)
{
    uint32_t c;
    x0x_off();
    xc.live = 0;
    for (c = 0; c <= X0X_NCH; c++) {
        drums.tail += xc.last[c];
        xc.last[c] = 0;
    }
}

/* n samples of a path (a channel, or the channels summed) into the mix as drums_mix mixes a voice: the track's
 * mute / solo fade, the meter, pan, the USB stem, the sends (r d c, pre: drums_mix's); -> the meter's peak */
static int32_t x0x_out(const int32_t *b, uint32_t n, int32_t *ml, int32_t *mr, int32_t *rev, int32_t *mono,
                       int32_t r, int32_t d, int32_t c, int32_t pre, int32_t gl, int32_t gr, int32_t pk, int32_t *last)
{
    uint32_t i;
    int32_t a0 = drums.a0, da = drums.a1 - drums.a0, s = 0;
    for (i = 0; i < n; i++) {
        s = b[i];
        if (a0 | da)                                /* (muted or fading: the track's attenuation) */
            s = mulq15(s, 32767 - a0 - ((da * (int32_t)i) >> CTL_LOG2));
        DSEND_KEEP(i, s);
        if (s > pk || -s > pk)
            pk = s < 0 ? -s : s;
        if (mono) {
            mono[i] += s;
            continue;
        }
        ml[i] += (s * gl) >> 12;
        mr[i] += (s * gr) >> 12;
#if FELUCCA_USB_AUDIO
        track_capture[i * NTRK + TRK_DRUM] += s;
#endif
        if (r)
            rev[i] += mulq15(s, r);
    }
    *last = s;
    DSEND_POST(0u, n, r, d, c, pre);
    return pk;
}

/* the X0X channels of this block into the mix (from XIP: drums_mix calls it through FAR). A channel whose lane has
 * no CUT and the sum's sends (the lanes' if all alike, else a lane's as it is) is rendered into one sum with its gain (the drum level x its LEVEL): one pass of the
 * mix for all of them; the others each on their own */
static int32_t x0x_buf[CTL], x0x_sum[CTL];
static __attribute__((noinline)) int32_t drums_x0x(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *mono, uint32_t n,
                                                   int32_t on, int32_t lvl, int32_t pre, int32_t gl,
                                                   int32_t gr, int32_t pk)
{
    uint32_t mask = x0x_block(n), ch, i, summed = 0;
    int32_t r, d, c;
    xc.live = mask;                                 /* (the channels sounding or due now) */
    int32_t r0 = dsend_one(on), r0x = dsend_rdef(on);   /* (the lanes all alike: their reverb send, nothing else; r0x: a lane as it is) */
    if (r0 < 0) {
        int32_t d0, c0;
        dsend_of(76u, on, r0x, &r0, &d0, &c0);   /* (else the sum is the lanes as they are: REV 4, nothing else) */
    }
    for (ch = 0; ch < X0X_NCH && (mask >> ch); ch++) {
        int32_t g;
        if (!((mask >> ch) & 1u)) {
            xc.last[ch] = 0;
            continue;
        }
        g = xc.lg[ch] ? (lvl * xc.lg[ch]) >> 12 : lvl;
        dsend_of(xc.note[ch], on, r0x, &r, &d, &c);
        if (!xc.cut[ch] && r == r0 && !d && !c) {
            x0x_render(ch, x0x_sum, n, g, summed++ != 0);
            xc.last[ch] = 0;
            continue;
        }
        x0x_render(ch, x0x_buf, n, g, 0);
        if (xc.cut[ch])                             /* the lane's CUT - */
            for (i = 0; i < n; i++) {
                xc.flt[ch] += mulq15(x0x_buf[i] - xc.flt[ch], xc.cut[ch]);
                x0x_buf[i] = xc.flt[ch];
            }
        pk = x0x_out(x0x_buf, n, ml, mr, rev, mono, r, d, c, pre, gl, gr, pk, &xc.last[ch]);
    }
    for (; ch < X0X_NCH; ch++)
        xc.last[ch] = 0;
    if (summed)
        pk = x0x_out(x0x_sum, n, ml, mr, rev, mono, r0, 0, 0, pre, gl, gr, pk, &xc.last[X0X_NCH]);
    else
        xc.last[X0X_NCH] = 0;
    return pk;
}
