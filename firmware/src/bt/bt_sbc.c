/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The SBC encoder (bt_sbc.h): AOSP's, its files in bt/sbc/ as Android ships them (Apache-2.0, their own headers kept),
 * compiled here as one unit; this file only sets it as the A2DP stream is configured. */
#include "bt_sbc.h"
#ifndef __PI32V2__
#include <string.h>                                 /* (host tests; the firmware has its own memset) */
#endif
#include "sbc/sbc_analysis.c"
#include "sbc/sbc_dct.c"
#include "sbc/sbc_dct_coeffs.c"
#include "sbc/sbc_enc_bit_alloc_mono.c"
#include "sbc/sbc_enc_bit_alloc_ste.c"
#include "sbc/sbc_enc_coeffs.c"
#include "sbc/sbc_encoder.c"
#include "sbc/sbc_packing.c"

uint32_t bt_sbc_frame_len(uint8_t bitpool, uint8_t joint_stereo)
{   /* A2DP 1.3 12.9: 4 + (4 x subbands x channels) / 8 + ceil((join x subbands + blocks x bitpool) / 8), 2 channels */
    return 4u + 8u + ((joint_stereo ? 8u : 0u) + 16u * (uint32_t)bitpool + 7u) / 8u;
}

void bt_sbc_init(struct bt_sbc_ctx *ctx, uint32_t sample_rate, uint8_t bitpool, uint8_t joint_stereo)
{
    SBC_ENC_PARAMS *p = &ctx->p;
    uint32_t len;
    memset(p, 0, sizeof *p);
    if (bitpool < 2u || bitpool > 53u)
        bitpool = 53u;
    p->s16SamplingFreq = sample_rate == 48000u ? SBC_sf48000 : SBC_sf44100;
    p->s16ChannelMode = joint_stereo ? SBC_JOINT_STEREO : SBC_STEREO;
    p->s16NumOfSubBands = BT_SBC_SUBBANDS;
    p->s16NumOfChannels = BT_SBC_CHANNELS;
    p->s16NumOfBlocks = BT_SBC_BLOCKS;
    p->s16AllocationMethod = SBC_LOUDNESS;
    p->s16BitPool = bitpool;
    len = bt_sbc_frame_len(bitpool, joint_stereo);  /* (AOSP's init takes the bit rate and finds the bitpool from it) */
    p->u16BitRate = (uint16_t)((8u * len * sample_rate / 128u + 999u) / 1000u);
    p->Format = SBC_FORMAT_GENERAL;
    SBC_Encoder_Init(p);
    p->s16BitPool = bitpool;                        /* (exactly the agreed one: the rate's rounding may not land on it) */
}

uint32_t bt_sbc_encode(struct bt_sbc_ctx *ctx, const int16_t *pcm, uint8_t *out)
{
    return SBC_Encode(&ctx->p, (int16_t *)pcm, out);
}
