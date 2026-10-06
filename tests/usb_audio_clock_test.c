/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal (fm1-x0x, charlesvestal/fm1-x0x 80b7d40, tests/host/uac_test.c) */
/* The USB audio capture (firmware/src/usb_audio_stream.c) against clocks that do not agree, after X0X's
 * uac_test.c: the FM-1's I2S renders 256-frame halves (eight 32-frame mix blocks back to back) at
 * RATE_IN while the host takes one capture packet a 1 ms frame by its own clock. A 1 kHz sine goes
 * into stem 1. After 20 s of settling:
 *   both modes:   no ring under- / overrun, no packet over UA_MAX_FRAMES;
 *   FELUCCA_UA_RESAMPLE=1 (the resampler): every packet is the plain 44.1 pattern (44 / 45 frames,
 *                 44,100 a second exactly), the tone arrives at 1 kHz by the host's clock (the
 *                 resampler took up the difference) and clean (residual under -70 dB);
 *   0 (the packet-size servo, Melodee's): printed only. The host receives RATE_IN frames a
 *                 second: what X0X found a live-monitoring host (Hosting AU) drops out on.
 * X0X measured its I2S at ~44,145..44,180 Hz against USB; Felucca's DAC divider gives 44,117.6.
 *   cc -O2 [-DFELUCCA_UA_RESAMPLE=1] tests/usb_audio_clock_test.c -lm */
#include <math.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)                /* the host: no .pool section (Mach-O), as hostsim.c */
#include "../firmware/src/usb_audio_stream.c"
#ifndef FELUCCA_UA_RESAMPLE
#define FELUCCA_UA_RESAMPLE 0
#endif

#define SECONDS 40
static int16_t host[44200 * SECONDS];

static int run(double rate_in)
{
    double t_in = 0, t_out = 0, ph = 0, err = 0, sig = 0, f, w;
    long nout = 0, a = 44100L * 20, i, zc = 0, frames_settled = 0;
    uint32_t bad_size = 0, glitches = 0, maxn = 0, minn = 1000, u0 = 0;
    int ok = 1, settled = 0;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.cap_alt = 1;                                   /* 16-bit stems */
    long ms = 0;                                      /* the host clock in whole frames (no float drift) */
    while (ms < SECONDS * 1000L) {
        t_out = ms / 1000.0;
        if (t_in <= t_out) {                          /* a half: eight mix blocks back to back */
            int b, k;
            for (b = 0; b < 8; b++) {
                int32_t out[64], tracks[32 * UA_CAP_CHANNELS];
                for (k = 0; k < 32; k++, ph += 1000.0 / rate_in) {
                    out[2 * k] = out[2 * k + 1] = 0;
                    tracks[4 * k] = (int32_t)lrint(sin(2 * M_PI * ph) * 16000.0);
                    tracks[4 * k + 1] = tracks[4 * k + 2] = tracks[4 * k + 3] = 0;
                }
                ua_audio(out, tracks, 32, 4096);
            }
            t_in += 256.0 / rate_in;
        } else {                                      /* the host's 1 ms frame */
            static uint8_t pkt[UA_PACKET];
            uint32_t bytes, n, k;
            ua_sof();
            bytes = ua_transmit(pkt);
            n = bytes / (2u * UA_CAP_CHANNELS);
            if (ms >= 20000L) {
                if (!settled) {
                    settled = 1;
                    u0 = ua.cap_underruns + ua.cap_overruns;
                }
                if (n != 44u && n != 45u)
                    bad_size++;
                maxn = n > maxn ? n : maxn;
                minn = n < minn ? n : minn;
                frames_settled += n;
            }
            for (k = 0; k < n && nout < (long)(sizeof host / sizeof host[0]); k++)
                host[nout++] = (int16_t)(pkt[k * 8] | pkt[k * 8 + 1] << 8);
            ms++;
        }
    }
    glitches = ua.cap_underruns + ua.cap_overruns - u0;
    for (i = a + 1; i < nout; i++)
        zc += (host[i - 1] < 0) != (host[i] < 0);
    f = zc / 2.0 / ((nout - a) / 44100.0);           /* by the host's clock: 44,100 frames a second */
    w = 2 * M_PI * f / 44100.0;
    for (i = a + 2; i < nout; i++) {                  /* a sine's next sample from the two before */
        double pred = 2 * cos(w) * host[i - 1] - host[i - 2];
        err += (host[i] - pred) * (host[i] - pred);
        sig += (double)host[i] * host[i];
    }
    printf("  I2S %.1f Hz: host gets %.1f frames/s, packets %u..%u, tone %.3f Hz, residual %.1f dB, %u glitches after 20 s\n",
           rate_in, frames_settled / (SECONDS - 20.0), minn, maxn, f, 10 * log10(err / sig), glitches);
    ok &= glitches == 0 && maxn <= UA_MAX_FRAMES;
    if (FELUCCA_UA_RESAMPLE)
        ok &= bad_size == 0 && frames_settled == 44100L * (SECONDS - 20) && fabs(f - 1000.0) < 0.05 &&
              10 * log10(err / sig) < -70.0;
    return ok;
}

int main(void)
{
    int ok;
    printf("USB audio capture against the I2S clock (%s):\n",
           FELUCCA_UA_RESAMPLE ? "FELUCCA_UA_RESAMPLE=1, resampler" : "packet-size servo");
    ok = run(44100.0) & run(44117.6) & run(44145.0) & run(44180.0) & run(44060.0);
    printf(ok ? "usb audio clock: ok\n" : "usb audio clock: FAILED\n");
    return !ok;
}
