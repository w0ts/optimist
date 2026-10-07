/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * USB audio from Melodee (https://github.com/keremimo/melodee, e459da5), GPL-3.0-only:
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio). Ported to SLOOP (FELUCCA_USB_AUDIO). */
/* UAC1 stereo playback and four-channel capture. No hardware dependencies:
 * callers serialize USB service and each short audio block. Capture takes
 * isolated track stems, so DAW monitoring cannot feed itself through capture.
 *
 * The I2S clock is independent of USB SOF. A low-pass ring-fill servo adjusts
 * capture packet lengths and the playback endpoint's explicit 10.14 feedback.
 * USB and I2S both run at the native 44.1 kHz rate; no resampler is needed.
 * SLOOP: the two rings (12 KiB) live in .pool, the rest of RAM is tight. */
#include <stdint.h>
#define UA_RATE 44100u
#define UA_CAP_CHANNELS 4u
#define UA_MAX_FRAMES 45u                      /* ceil(44.1 + maximum feedback correction) */
#define UA_PLAY_PACKET (UA_MAX_FRAMES * 2u * 3u)
#define UA_PACKET (UA_MAX_FRAMES * UA_CAP_CHANNELS * 3u)
#define UA_RING 1024u
#define UA_TARGET 512u
#define UA_NOMINAL ((UA_RATE * 16384u) / 1000u)

static struct {
    uint8_t play_alt, cap_alt, play_ready, cap_ready;
    uint32_t pw, pr, cw, cr, cap_frac;
    int32_t play_fill_q8, cap_fill_q8;
    uint32_t play_underruns, play_overruns, cap_underruns, cap_overruns, bad_packets;
    uint32_t rx_packets, tx_packets, missed_frames;
} ua;
static int16_t ua_play[UA_RING * 2u] __attribute__((section(".pool")));
static int16_t ua_cap[UA_RING * UA_CAP_CHANNELS] __attribute__((section(".pool")));

#ifndef FELUCCA_UA_RESAMPLE
#define FELUCCA_UA_RESAMPLE 0
#endif
#if FELUCCA_UA_RESAMPLE
/* Capture resampler (FELUCCA_UA_RESAMPLE), after X0X 80b7d40 (charlesvestal/fm1-x0x, Charles Vestal,
 * GPL-3.0-only: uac_tap / uac_window / uac_packet in usb.c), in fixed point (this firmware has no float).
 * The I2S clock is the FM-1's own (X0X measured ~44,145..44,180 Hz against USB). The servo above sends
 * what the ring holds, so the host sees a device running at the I2S rate: a recorder is happy, but a
 * host passing the input straight to an output (live monitoring) has two clocks, and X0X found it
 * dropping out. Here the stems are resampled (cubic Hermite) to the host's clock and every packet is
 * the plain 44.1 pattern (44 / 45 frames). The step (input frames per output frame, Q24) is steered by
 * the ring's low-passed fill: proportional, plus a learned trim (the clocks' drift).
 * tests/usb_audio_clock_test.c. Playback keeps its explicit feedback (the host adapts to it). */
#define UA_RS_ONE (1u << 24)
static struct {
    uint32_t step, pos;                        /* Q24: input frames per output; the next output after h[1] */
    int32_t trim;                              /* Q24: the learned clock ratio - 1 */
    uint32_t acc, win;                         /* 44.1 frames a ms (0.1 steps); SOFs in the trim window */
    int16_t h[4][UA_CAP_CHANNELS];             /* the last four input frames, oldest first */
} ua_rs;

static void ua_rs_reset(void)
{
    uint32_t i, c;
    ua_rs.step = UA_RS_ONE + (uint32_t)ua_rs.trim; /* (the trim survives a stream restart: same clocks) */
    ua_rs.pos = 0;
    ua_rs.acc = ua_rs.win = 0;
    for (i = 0; i < 4u; i++)
        for (c = 0; c < UA_CAP_CHANNELS; c++)
            ua_rs.h[i][c] = 0;
}

/* once a USB frame (ua_transmit): fill too high -> take input faster (fewer frames out per frame in).
 * A step change d moves the fill ~44 d frames a ms; proportional 1e-5 a frame (168 / 256 per Q8 frame)
 * settles in a few seconds, an error of 50 frames is 0.05 % (under a cent); every 128 ms the trim
 * integrates 1e-6 a frame of error (as X0X's window of ~190 ms), within +-0.8 %. */
static void ua_rs_steer(void)
{
    int32_t e = ua.cap_fill_q8 - (int32_t)UA_TARGET * 256;    /* frames, Q8 */
    if (e > 400 * 256)
        e = 400 * 256;
    if (e < -400 * 256)
        e = -400 * 256;
    if (++ua_rs.win >= 128u) {
        ua_rs.win = 0;
        ua_rs.trim += e / 16;
        if (ua_rs.trim > 134218)
            ua_rs.trim = 134218;
        if (ua_rs.trim < -134218)
            ua_rs.trim = -134218;
    }
    ua_rs.step = (uint32_t)((int32_t)UA_RS_ONE + ua_rs.trim + e * 21 / 32);
}

/* one cubic Hermite sample between x0 and x1 at t (Q15) */
static int32_t ua_rs_cubic(int32_t xm, int32_t x0, int32_t x1, int32_t x2, int32_t t)
{
    int64_t c1 = x1 - xm, c2 = 2 * xm - 5 * x0 + 4 * x1 - x2, c3 = (x2 - xm) + 3 * (x0 - x1), v;
    v = (c3 * t >> 15) + c2;
    v = (v * t >> 15) + c1;
    v = (v * t >> 15) + 2 * (int64_t)x0;               /* 2 y */
    return (int32_t)((v + 1) >> 1);
}
#endif

static int32_t ua_clip(int32_t x) { return clamp(x, -32768, 32767); }   /* (dsp_common.h) */

static uint32_t ua_sample_bytes(uint8_t alt) { return alt == 2u ? 3u : 2u; }

/* Packed little-endian PCM24 uses the same full-scale level as PCM16. The
 * low byte is discarded on playback; recording pads it with zero. */
static int16_t ua_decode(const uint8_t *p, uint32_t width)
{
    p += width - 2u;
    return (int16_t)(p[0] | (uint16_t)p[1] << 8);
}

static void ua_encode(uint8_t *p, uint32_t width, int16_t sample)
{
    if (width == 3u)
        *p++ = 0;
    p[0] = (uint8_t)sample;
    p[1] = (uint8_t)((uint16_t)sample >> 8);
}

static void ua_play_reset(void)
{
    ua.pw = ua.pr = 0;
    ua.play_ready = 0;
    ua.play_fill_q8 = UA_TARGET * 256;
}

static void ua_cap_reset(void)
{
    ua.cw = ua.cr = ua.cap_frac = 0;
    ua.cap_ready = 0;
    ua.cap_fill_q8 = UA_TARGET * 256;
#if FELUCCA_UA_RESAMPLE
    ua_rs_reset();
#endif
}

static void ua_reset(void)
{
    ua.play_alt = ua.cap_alt = 0;
    ua_play_reset();
    ua_cap_reset();
}

static void ua_receive(const uint8_t *p, uint32_t bytes)
{
    uint32_t i, width = ua_sample_bytes(ua.play_alt), frame = width * 2u;
    uint32_t n = bytes / frame;
    if (!ua.play_alt)
        return;
    if (n > UA_MAX_FRAMES || bytes % frame) {
        ua.bad_packets++;
        return;
    }
    if (ua.pw - ua.pr + n > UA_RING) {
        ua.play_overruns++;
        ua_play_reset();                       /* re-prime, never replay stale data */
    }
    for (i = 0; i < n; i++) {
        uint32_t at = (ua.pw & (UA_RING - 1u)) * 2u;
        ua_play[at] = ua_decode(p + frame * i, width);
        ua_play[at + 1u] = ua_decode(p + frame * i + width, width);
        ua.pw++;
    }
    ua.rx_packets++;
}

/* Called once per observed USB frame, not once per poll or audio callback. */
static void ua_sof(void)
{
    if (ua.play_ready)
        ua.play_fill_q8 += ((int32_t)(ua.pw - ua.pr) * 256 - ua.play_fill_q8) / 32;
    if (ua.cap_ready)
        ua.cap_fill_q8 += ((int32_t)(ua.cw - ua.cr) * 256 - ua.cap_fill_q8) / 32;
}

static uint32_t ua_rate(int32_t error_q8)
{
    int32_t correction = error_q8 / 16;         /* fill error / 1024, in 10.14 */
    if (correction > 8192)
        correction = 8192;
    if (correction < -8192)
        correction = -8192;
    return (uint32_t)((int32_t)UA_NOMINAL + correction);
}

static uint32_t ua_feedback(void)
{
    return ua_rate(UA_TARGET * 256 - ua.play_fill_q8);
}

static uint32_t ua_transmit(uint8_t *p)
{
    uint32_t i, n, take = 0, width = ua_sample_bytes(ua.cap_alt);
#if FELUCCA_UA_RESAMPLE
    n = UA_RATE / 1000u;                        /* the plain 44.1 pattern; the resampler holds the fill */
    ua_rs.acc += UA_RATE % 1000u;
    if (ua_rs.acc >= 1000u) {
        ua_rs.acc -= 1000u;
        n++;
    }
    ua_rs_steer();
#else
    ua.cap_frac += ua_rate(ua.cap_fill_q8 - UA_TARGET * 256);
    n = ua.cap_frac >> 14;
    ua.cap_frac &= 16383u;
#endif
    if (!ua.cap_ready && ua.cw - ua.cr >= UA_TARGET)
        ua.cap_ready = 1;
    if (ua.cap_ready) {
        if (ua.cw - ua.cr >= n)
            take = 1;
        else {
            ua.cap_underruns++;
            ua_cap_reset();
        }
    }
    for (i = 0; i < n; i++) {
        int16_t sample[UA_CAP_CHANNELS] = {0};
        uint32_t ch;
        if (take) {
            uint32_t at = (ua.cr++ & (UA_RING - 1u)) * UA_CAP_CHANNELS;
            for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
                sample[ch] = ua_cap[at + ch];
        }
        for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
            ua_encode(p + (i * UA_CAP_CHANNELS + ch) * width, width, sample[ch]);
    }
    return n * width * UA_CAP_CHANNELS;
}

/* Stereo Q15 monitor plus four pre-master mono stems; playback follows MASTER.
 * Call in blocks of CTL (32), with USB service excluded during this copy.
 * Not inlined: the emulator's USB audio test taps the stems at its entry. */
static __attribute__((noinline)) void ua_audio(int32_t *out, const int32_t *tracks, uint32_t n, uint32_t master_q12)
{
    uint32_t i, capture = ua.cap_alt, playback = 0;
    if (capture && ua.cw - ua.cr + n + 2u * FELUCCA_UA_RESAMPLE > UA_RING) {   /* (the resampler: +1 or 2) */
        ua.cap_overruns++;
        ua_cap_reset();
    }
    if (ua.play_alt) {
        if (!ua.play_ready && ua.pw - ua.pr >= UA_TARGET)
            ua.play_ready = 1;
        if (ua.play_ready) {
            if (ua.pw - ua.pr >= n)
                playback = 1;
            else {
                ua.play_underruns++;
                ua_play_reset();
            }
        }
    }
    for (i = 0; i < n; i++) {
        uint32_t ci = ((ua.cw + i) & (UA_RING - 1u)) * UA_CAP_CHANNELS;
        uint32_t pi = ((ua.pr + i) & (UA_RING - 1u)) * 2u;
        int32_t l = out[2u * i], r = out[2u * i + 1u];
        if (capture) {
            uint32_t ch;
#if FELUCCA_UA_RESAMPLE
            for (ch = 0; ch < UA_CAP_CHANNELS; ch++) {   /* a frame in; as many out as the step gives */
                ua_rs.h[0][ch] = ua_rs.h[1][ch];
                ua_rs.h[1][ch] = ua_rs.h[2][ch];
                ua_rs.h[2][ch] = ua_rs.h[3][ch];
                ua_rs.h[3][ch] = (int16_t)ua_clip(tracks[i * UA_CAP_CHANNELS + ch]);
            }
            while (ua_rs.pos < UA_RS_ONE) {             /* between h[1] and h[2] */
                int32_t t = (int32_t)(ua_rs.pos >> 9);
                ci = (ua.cw++ & (UA_RING - 1u)) * UA_CAP_CHANNELS;
                for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
                    ua_cap[ci + ch] = (int16_t)ua_clip(ua_rs_cubic(ua_rs.h[0][ch], ua_rs.h[1][ch], ua_rs.h[2][ch],
                                                                   ua_rs.h[3][ch], t));
                ua_rs.pos += ua_rs.step;
            }
            ua_rs.pos -= UA_RS_ONE;
#else
            for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
                ua_cap[ci + ch] = (int16_t)ua_clip(tracks[i * UA_CAP_CHANNELS + ch]);
#endif
        }
        if (playback) {
            l += (ua_play[pi] * (int32_t)master_q12) >> 12;
            r += (ua_play[pi + 1u] * (int32_t)master_q12) >> 12;
            out[2u * i] = ua_clip(l);
            out[2u * i + 1u] = ua_clip(r);
        }
    }
    if (capture && !FELUCCA_UA_RESAMPLE)
        ua.cw += n;                             /* (the resampler advanced it per frame out) */
    if (playback)
        ua.pr += n;
}
