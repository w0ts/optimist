/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The SBC encoder the A2DP stream sends (bt_acl.c): AOSP's (Android's Bluedroid, bt/sbc/, Apache-2.0, Broadcom 1999-2012)
 * behind this interface, set as the FM-1 configures the stream: 44.1 / 48 kHz, 16 blocks, 8 subbands, joint stereo or
 * stereo, loudness. Our own encoder of before wrote frames no decoder takes (2026-10-20: libsbc refused every one: the
 * header's codes, no CRC, the quantisation); AOSP's decodes clean with libsbc (two tones back at 0.1 %, -57 dB between
 * the channels) and with AOSP's own decoder (tests/bt_sbc_test.c). */
#ifndef BT_SBC_H
#define BT_SBC_H
#include <stdint.h>
#include "sbc/sbc_encoder.h"

#define BT_SBC_SUBBANDS 8
#define BT_SBC_BLOCKS 16
#define BT_SBC_CHANNELS 2
#define BT_SBC_SAMPLES_PER_FRAME (BT_SBC_BLOCKS * BT_SBC_SUBBANDS)  /* 128 stereo frames */

struct bt_sbc_ctx {
    SBC_ENC_PARAMS p;
};

/* the encoder set: sample rate 44100 or 48000, bitpool 2..53 (the stream's agreed maximum), joint stereo or stereo */
void bt_sbc_init(struct bt_sbc_ctx *ctx, uint32_t sample_rate, uint8_t bitpool, uint8_t joint_stereo);
/* one frame from 128 stereo samples (16-bit, L R interleaved) into out -> its bytes */
uint32_t bt_sbc_encode(struct bt_sbc_ctx *ctx, const int16_t *pcm, uint8_t *out);
/* a frame's length for this bitpool (44.1 / 48 kHz, 16 blocks, 8 subbands) */
uint32_t bt_sbc_frame_len(uint8_t bitpool, uint8_t joint_stereo);

#endif
