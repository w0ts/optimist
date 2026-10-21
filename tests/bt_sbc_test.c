/* SPDX-License-Identifier: GPL-3.0-only */
/* The A2DP stream's SBC encoder (firmware/src/bt/bt_sbc.c: AOSP's behind our interface) against a decoder that is not
 * ours: AOSP's (tests/ref/sbc_dec, Apache-2.0, test only). Two tones (1 kHz left, 440 Hz right) for 2 s, encoded as the
 * FM-1 sets the stream, every frame decoded, the tones measured back. Our encoder of before passed a test of its own
 * frames and wrote frames no decoder took (2026-10-20, libsbc on a Raspberry Pi: every frame refused). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "../firmware/src/bt/bt_sbc.c"
#include "ref/sbc_dec/oi_codec_sbc.h"

static int fails;
static void check(const char *what, int ok)
{
    printf("bt_sbc: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

#define SECS 2u
#define NFR (SECS * 44100u / 128u)
static int16_t pcm_in[NFR * 256u], pcm_out[NFR * 256u];

static double tone(const int16_t *x, uint32_t n, uint32_t stride, double f)   /* amplitude of f in x */
{
    double c = 0, s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        c += x[i * stride] * cos(2 * M_PI * f * i / 44100.0), s += x[i * stride] * sin(2 * M_PI * f * i / 44100.0);
    return 2 * hypot(c, s) / n;
}

/* encode the tones as (bitpool, joint stereo; from the middle on bp2 if not 0, as bt_acl.c's FEC fallback switches it
 * between packets), decode every frame -> 1 all right; *hdr the first frame's 4 bytes, *flen the last frame's */
static int round_trip(uint8_t bitpool, uint8_t js, uint8_t bp2, uint8_t hdr[4], uint32_t *flen, double amp[4],
                      uint32_t *bad)
{
    static struct bt_sbc_ctx c;
    static uint8_t stream[NFR * 128u];
    static uint32_t dec_data[CODEC_DATA_WORDS(2, SBC_CODEC_FAST_FILTER_BUFFERS)];
    OI_CODEC_SBC_DECODER_CONTEXT dc;
    uint32_t fr, len = 0, at = 0, got = 0;
    const OI_BYTE *p;
    uint32_t left;
    bt_sbc_init(&c, 44100, bitpool, js);
    for (fr = 0; fr < NFR; fr++) {
        if (bp2 && fr == NFR / 2u)
            c.p.s16BitPool = bp2;
        len = bt_sbc_encode(&c, pcm_in + fr * 256u, stream + at);
        if (fr == 0)
            memcpy(hdr, stream, 4);
        at += len;
    }
    *flen = len;
    *bad = 0;
    if (OI_CODEC_SBC_DecoderReset(&dc, dec_data, sizeof dec_data, 2, 2, FALSE) != OI_OK)
        return 0;
    p = stream;
    left = at;
    while (left > 0u && got < NFR) {
        uint32_t pb = 256u * sizeof(int16_t);
        if (OI_CODEC_SBC_DecodeFrame(&dc, &p, &left, pcm_out + got * 256u, &pb) != OI_OK) {
            (*bad)++;
            break;
        }
        got++;
    }
    {
        uint32_t skip = 4096u, n = (NFR - 32u) * 128u - skip;   /* (the filters' delay settled) */
        amp[0] = tone(pcm_out + 2u * skip, n, 2, 1000.0), amp[1] = tone(pcm_out + 2u * skip, n, 2, 440.0);
        amp[2] = tone(pcm_out + 2u * skip + 1u, n, 2, 440.0), amp[3] = tone(pcm_out + 2u * skip + 1u, n, 2, 1000.0);
    }
    return got == NFR && !*bad;
}

int main(void)
{
    uint8_t hdr[4];
    uint32_t i, flen, bad;
    double a[4];
    char what[160];
    static const struct { uint8_t bitpool, js; } SETS[] = {{53, 1}, {53, 0}, {35, 1}};
    for (i = 0; i < NFR * 128u; i++) {
        pcm_in[2u * i] = (int16_t)(12000.0 * sin(2 * M_PI * 1000.0 * i / 44100.0));
        pcm_in[2u * i + 1u] = (int16_t)(12000.0 * sin(2 * M_PI * 440.0 * i / 44100.0));
    }
    for (i = 0; i < sizeof SETS / sizeof SETS[0]; i++) {
        int ok = round_trip(SETS[i].bitpool, SETS[i].js, 0, hdr, &flen, a, &bad);
        uint8_t want1 = (uint8_t)(0x80u | 0x30u | (SETS[i].js ? 0x0Cu : 0x08u) | 0x01u);   /* 44.1k, 16 blocks, JS / stereo, loudness, 8 */
        snprintf(what, sizeof what, "bitpool %u %s: header %02X %02X %02X (want 9C %02X %02X), frame %u B (bt_sbc_frame_len %u)",
                 SETS[i].bitpool, SETS[i].js ? "joint stereo" : "stereo", hdr[0], hdr[1], hdr[2], want1, SETS[i].bitpool, flen,
                 bt_sbc_frame_len(SETS[i].bitpool, SETS[i].js));
        check(what, hdr[0] == 0x9Cu && hdr[1] == want1 && hdr[2] == SETS[i].bitpool && flen == bt_sbc_frame_len(SETS[i].bitpool, SETS[i].js));
        snprintf(what, sizeof what, "... AOSP's decoder takes all %u frames (%u refused)", NFR, bad);
        check(what, ok);
        snprintf(what, sizeof what, "... the tones back: L 1 kHz %.0f, R 440 Hz %.0f of 12000; crosstalk %.0f / %.0f", a[0], a[2], a[1], a[3]);
        check(what, fabs(a[0] - 12000) < 120 && fabs(a[2] - 12000) < 120 && a[1] < 120 && a[3] < 120);
    }
    {
        int ok = round_trip(46, 1, 28, hdr, &flen, a, &bad);
        snprintf(what, sizeof what, "bitpool 46 -> 28 mid-stream (the FEC fallback): frames %u B then (want %u, 3 in a DM5's 224 B)",
                 flen, bt_sbc_frame_len(28, 1));
        check(what, flen == bt_sbc_frame_len(28, 1) && 3u * flen + 17u <= 224u);
        snprintf(what, sizeof what, "... AOSP's decoder takes all %u frames across the change (%u refused)", NFR, bad);
        check(what, ok);
    }
    printf("bt_sbc: %d failed\n", fails);
    return fails != 0;
}
