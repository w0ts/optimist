/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed-point single-profile SBC encoder (Bluetooth A2DP 1.3 §4.3):
 * 44.1 kHz / 48 kHz, 16 blocks, 8 subbands, Joint Stereo, Loudness allocation.
 * Portable C, no dynamic memory allocation, constant RAM footprint. */
#ifndef BT_SBC_H
#define BT_SBC_H
#include <stdint.h>

#define BT_SBC_SUBBANDS 8
#define BT_SBC_BLOCKS 16
#define BT_SBC_CHANNELS 2
#define BT_SBC_SAMPLES_PER_FRAME (BT_SBC_BLOCKS * BT_SBC_SUBBANDS)  /* 128 stereo frames */

struct bt_sbc_ctx {
    int32_t shift[BT_SBC_CHANNELS][80];
    int32_t sb_sample[BT_SBC_BLOCKS][BT_SBC_CHANNELS][BT_SBC_SUBBANDS];
    uint8_t scale_factor[BT_SBC_CHANNELS][BT_SBC_SUBBANDS];
    uint8_t join[BT_SBC_SUBBANDS];
    uint8_t bits[BT_SBC_CHANNELS][BT_SBC_SUBBANDS];
    uint32_t sample_rate;   /* 44100 or 48000 */
    uint8_t bitpool;        /* 2..53, default 53 */
    uint8_t joint_stereo;   /* 1: joint stereo, 0: stereo */
};

/* Initialize SBC encoder context */
void bt_sbc_init(struct bt_sbc_ctx *ctx, uint32_t sample_rate, uint8_t bitpool, uint8_t joint_stereo);

/* Encode one SBC frame (128 stereo PCM samples, 16-bit signed interleaved L, R).
 * Writes encoded frame into `out`. Returns number of bytes written. */
uint32_t bt_sbc_encode(struct bt_sbc_ctx *ctx, const int16_t *pcm, uint8_t *out);

/* Returns calculated SBC frame length in bytes for given configuration */
uint32_t bt_sbc_frame_len(uint8_t bitpool, uint8_t joint_stereo);

#endif
