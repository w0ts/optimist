/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed-point single-profile SBC encoder (Bluetooth A2DP 1.3 §4.3).
 * Optimized for embedded systems: zero runtime malloc, Q15 fixed-point math, div-audit clean. */
#include "bt_sbc.h"
#ifndef __PI32V2__
#include <string.h>                                 /* (host tests; the firmware has its own memset) */
#endif

#define SBC_SYNCWORD 0x9Cu

/* Proto filter coefficients C[80] scaled by 2^15 (A2DP Spec Table 4.15) */
static const int16_t sbc_proto_8[80] = {
    0, 2, 4, 7, 12, 20, 31, 46,
    66, 92, 126, 169, 224, 292, 376, 478,
    600, 746, 917, 1116, 1344, 1604, 1897, 2224,
    2586, 2983, 3415, 3880, 4376, 4899, 5444, 6006,
    6578, 7151, 7717, 8267, 8790, 9276, 9713, 10091,
    10398, 10626, 10764, 10803, 10736, 10555, 10255, 9829,
    9274, 8587, 7767, 6815, 5732, 4524, 3197, 1761,
    228, -1386, -3062, -4781, -6520, -8254, -9953, -11588,
    -13127, -14539, -15792, -16857, -17704, -18306, -18641, -18688,
    -18429, -17849, -16936, -15682, -14081, -12133, -9841, -7213
};

/* Cosine modulation matrix cos((k + 0.5) * (i + 4) * PI / 8) * 2^14 */
static const int16_t sbc_cos_8[8][16] = {
    { 15137, 12051, 6270, -6270, -12051, -15137, -15137, -12051, -6270, 6270, 12051, 15137, 15137, 12051, 6270, -6270 },
    { 12051, -12051, -15137, -6270, 6270, 15137, 12051, -12051, -15137, -6270, 6270, 15137, 12051, -12051, -15137, -6270 },
    { 6270, -15137, 6270, 12051, -15137, 12051, 6270, -15137, 6270, 12051, -15137, 12051, 6270, -15137, 6270, 12051 },
    { -6270, -6270, 12051, 15137, -12051, -6270, -6270, 12051, 15137, -12051, -6270, -6270, 12051, 15137, -12051, -6270 },
    { -12051, 6270, 12051, -15137, -6270, 12051, -12051, 6270, 12051, -15137, -6270, 12051, -12051, 6270, 12051, -15137 },
    { -15137, 15137, -12051, 6270, 6270, -12051, 15137, -15137, 12051, -6270, -6270, 12051, -15137, 15137, -12051, 6270 },
    { -15137, 12051, -6270, -6270, 12051, -15137, 15137, -12051, 6270, 6270, -12051, 15137, -15137, 12051, -6270, -6270 },
    { -12051, -6270, -12051, 6270, 15137, 6270, -12051, -6270, -12051, 6270, 15137, 6270, -12051, -6270, -12051, 6270 }
};

/* Loudness frequency sensitivity offsets (44.1 kHz, 8 subbands) */
static const int8_t sbc_offset44_8[8] = { -1, 0, 0, 0, 0, 0, 0, 1 };

void bt_sbc_init(struct bt_sbc_ctx *ctx, uint32_t sample_rate, uint8_t bitpool, uint8_t joint_stereo)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->sample_rate = sample_rate;
    ctx->bitpool = (bitpool >= 2 && bitpool <= 53) ? bitpool : 53;
    ctx->joint_stereo = joint_stereo ? 1 : 0;
}

uint32_t bt_sbc_frame_len(uint8_t bitpool, uint8_t joint_stereo)
{
    /* Joint stereo: 4 (header) + 1 (join) + 8 (scale factors) + ceil(16 * bitpool / 8) */
    /* Stereo: 4 (header) + 8 (scale factors) + ceil(16 * bitpool / 8) */
    uint32_t base = 4u + (joint_stereo ? 1u : 0u) + 8u;
    uint32_t bits = 16u * (uint32_t)bitpool;
    return base + ((bits + 7u) >> 3);
}

/* Bitwriter helper */
struct bit_writer {
    uint8_t *buf;
    uint32_t bit_pos;
};

static void put_bits(struct bit_writer *bw, uint32_t val, uint32_t nbits)
{
    while (nbits > 0) {
        uint32_t byte_idx = bw->bit_pos >> 3;
        uint32_t bit_offset = bw->bit_pos & 7u;
        uint32_t space = 8u - bit_offset;
        uint32_t take = (nbits < space) ? nbits : space;
        uint32_t shift = nbits - take;
        uint8_t chunk = (uint8_t)((val >> shift) & ((1u << take) - 1u));
        
        bw->buf[byte_idx] |= (uint8_t)(chunk << (space - take));
        bw->bit_pos += take;
        nbits -= take;
    }
}

/* Fixed-point subband analysis for one channel and one block of 8 samples */
static void sbc_analyze_8(int32_t shift[80], const int16_t in[8], int32_t out_sb[8])
{
    int32_t z[80];
    int32_t y[16];
    int i, k;

    /* Shift history buffer by 8 samples and insert new samples in reverse order */
    for (i = 79; i >= 8; i--)
        shift[i] = shift[i - 8];
    for (i = 0; i < 8; i++)
        shift[7 - i] = (int32_t)in[i];

    /* Windowing: Z[i] = shift[i] * C[i] */
    for (i = 0; i < 80; i++)
        z[i] = (int32_t)(((int64_t)shift[i] * sbc_proto_8[i]) >> 15);

    /* Partial summation Y[k] = sum(Z[k + 16*j]) for j=0..4 */
    for (i = 0; i < 16; i++) {
        y[i] = z[i] + z[i + 16] + z[i + 32] + z[i + 48] + z[i + 64];
    }

    /* Matrix modulation */
    for (k = 0; k < 8; k++) {
        int64_t acc = 0;
        for (i = 0; i < 16; i++)
            acc += (int64_t)sbc_cos_8[k][i] * y[i];
        out_sb[k] = (int32_t)(acc >> 14);
    }
}

static uint8_t calc_scale_factor(int32_t max_val)
{
    uint8_t sf = 0;
    if (max_val < 0) max_val = -max_val;
    while (sf < 15 && (max_val >> (sf + 1)) > 0)
        sf++;
    return sf;
}

uint32_t bt_sbc_encode(struct bt_sbc_ctx *ctx, const int16_t *pcm, uint8_t *out)
{
    int blk, ch, sb;
    struct bit_writer bw;
    uint32_t flen = bt_sbc_frame_len(ctx->bitpool, ctx->joint_stereo);
    
    memset(out, 0, flen);
    bw.buf = out;
    bw.bit_pos = 0;

    /* 1. Subband analysis across all 16 blocks */
    for (blk = 0; blk < BT_SBC_BLOCKS; blk++) {
        int16_t in_ch[BT_SBC_CHANNELS][8];
        for (sb = 0; sb < 8; sb++) {
            uint32_t idx = 2u * (uint32_t)(blk * 8 + sb);
            in_ch[0][sb] = pcm[idx];
            in_ch[1][sb] = pcm[idx + 1];
        }
        for (ch = 0; ch < BT_SBC_CHANNELS; ch++) {
            sbc_analyze_8(ctx->shift[ch], in_ch[ch], ctx->sb_sample[blk][ch]);
        }
    }

    /* 2. Joint stereo decision & matrixing (if JS enabled) */
    memset(ctx->join, 0, sizeof(ctx->join));
    if (ctx->joint_stereo) {
        for (sb = 0; sb < 7; sb++) { /* Subband 7 is never joined */
            int32_t max_l = 0, max_r = 0, max_m = 0, max_s = 0;
            for (blk = 0; blk < BT_SBC_BLOCKS; blk++) {
                int32_t l = ctx->sb_sample[blk][0][sb];
                int32_t r = ctx->sb_sample[blk][1][sb];
                int32_t m = (l + r) >> 1;
                int32_t s = (l - r) >> 1;
                if (l < 0) l = -l; if (l > max_l) max_l = l;
                if (r < 0) r = -r; if (r > max_r) max_r = r;
                if (m < 0) m = -m; if (m > max_m) max_m = m;
                if (s < 0) s = -s; if (s > max_s) max_s = s;
            }
            uint8_t sf_lr = (uint8_t)(calc_scale_factor(max_l) + calc_scale_factor(max_r));
            uint8_t sf_ms = (uint8_t)(calc_scale_factor(max_m) + calc_scale_factor(max_s));
            if (sf_ms < sf_lr) {
                ctx->join[sb] = 1;
                for (blk = 0; blk < BT_SBC_BLOCKS; blk++) {
                    int32_t l = ctx->sb_sample[blk][0][sb];
                    int32_t r = ctx->sb_sample[blk][1][sb];
                    ctx->sb_sample[blk][0][sb] = (l + r) >> 1;
                    ctx->sb_sample[blk][1][sb] = (l - r) >> 1;
                }
            }
        }
    }

    /* 3. Scale factor calculation */
    for (ch = 0; ch < BT_SBC_CHANNELS; ch++) {
        for (sb = 0; sb < 8; sb++) {
            int32_t max_val = 0;
            for (blk = 0; blk < BT_SBC_BLOCKS; blk++) {
                int32_t v = ctx->sb_sample[blk][ch][sb];
                if (v < 0) v = -v;
                if (v > max_val) max_val = v;
            }
            ctx->scale_factor[ch][sb] = calc_scale_factor(max_val);
        }
    }

    /* 4. Bit allocation (Loudness method) */
    {
        int16_t need[BT_SBC_CHANNELS][8];
        int max_need = 0;
        int bitcount = 0;
        int target_bits = (int)ctx->bitpool;

        memset(ctx->bits, 0, sizeof(ctx->bits));
        for (ch = 0; ch < BT_SBC_CHANNELS; ch++) {
            for (sb = 0; sb < 8; sb++) {
                if (ctx->scale_factor[ch][sb] == 0)
                    need[ch][sb] = -558;
                else {
                    need[ch][sb] = (int16_t)(ctx->scale_factor[ch][sb] - sbc_offset44_8[sb]);
                    if (need[ch][sb] > max_need) max_need = need[ch][sb];
                }
            }
        }

        /* Distribute bits iteratively */
        int loudness = max_need;
        while (bitcount < target_bits && loudness > -100) {
            for (ch = 0; ch < BT_SBC_CHANNELS; ch++) {
                for (sb = 0; sb < 8; sb++) {
                    if (need[ch][sb] >= loudness && ctx->bits[ch][sb] < 16) {
                        if (ctx->bits[ch][sb] == 0) {
                            if (target_bits - bitcount >= 2) {
                                ctx->bits[ch][sb] = 2;
                                bitcount += 2;
                            }
                        } else if (target_bits - bitcount >= 1) {
                            ctx->bits[ch][sb]++;
                            bitcount++;
                        }
                    }
                }
            }
            loudness--;
        }
    }

    /* 5. Pack Header */
    put_bits(&bw, SBC_SYNCWORD, 8);
    /* Flags: Sampling Freq (2b), Blocks (2b: 11 = 16 blocks), Channel Mode (2b: 01 for JS, 10 for Stereo),
     * Alloc Method (1b: 1 for Loudness), Subbands (1b: 1 for 8 subbands) */
    uint8_t sf_code = (ctx->sample_rate == 48000) ? 0x01u : 0x00u; /* 00: 44.1k, 01: 48k */
    uint8_t cm_code = ctx->joint_stereo ? 0x01u : 0x02u;
    uint8_t flags = (uint8_t)((sf_code << 6) | (0x03u << 4) | (cm_code << 2) | (1u << 1) | 1u);
    put_bits(&bw, flags, 8);
    put_bits(&bw, ctx->bitpool, 8);

    /* Joint stereo join flags (if JS) */
    if (ctx->joint_stereo) {
        for (sb = 0; sb < 7; sb++)
            put_bits(&bw, ctx->join[sb], 1);
        put_bits(&bw, 0, 1); /* Reserved/Subband 7 0-bit */
    }

    /* Scale factors: 4 bits each (nibbles) */
    for (ch = 0; ch < BT_SBC_CHANNELS; ch++) {
        for (sb = 0; sb < 8; sb++) {
            put_bits(&bw, ctx->scale_factor[ch][sb] & 0x0Fu, 4);
        }
    }

    /* 6. Quantize and pack subband samples */
    for (blk = 0; blk < BT_SBC_BLOCKS; blk++) {
        for (ch = 0; ch < BT_SBC_CHANNELS; ch++) {
            for (sb = 0; sb < 8; sb++) {
                uint8_t nbits = ctx->bits[ch][sb];
                if (nbits >= 2) {
                    int32_t val = ctx->sb_sample[blk][ch][sb];
                    uint8_t sf = ctx->scale_factor[ch][sb];
                    /* Quantization formula: ((val / 2^(sf+1)) + 1) * (2^nbits - 1) / 2 */
                    int32_t scaled = (val >> (sf + 1));
                    if (scaled < -1) scaled = -1;
                    if (scaled > 1) scaled = 1;
                    uint32_t levels = (1u << nbits) - 1u;
                    int32_t quant = ((scaled + 1) * (int32_t)levels) >> 1;
                    if (quant < 0) quant = 0;
                    if ((uint32_t)quant > levels) quant = (int32_t)levels;
                    put_bits(&bw, (uint32_t)quant, nbits);
                }
            }
        }
    }

    /* Pad remaining bits in last byte */
    if (bw.bit_pos & 7u)
        put_bits(&bw, 0, 8u - (bw.bit_pos & 7u));

    return flen;
}
