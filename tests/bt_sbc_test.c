/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for the SBC fixed-point encoder (firmware/src/bt/bt_sbc.c). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "../firmware/src/bt/bt_sbc.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    struct bt_sbc_ctx ctx;
    int16_t pcm[BT_SBC_SAMPLES_PER_FRAME * 2];
    uint8_t out[512];
    uint32_t len;

    /* Generate stereo sine wave: Left 440 Hz, Right 880 Hz at 44.1 kHz */
    for (int i = 0; i < BT_SBC_SAMPLES_PER_FRAME; i++) {
        pcm[2 * i] = (int16_t)(16000.0 * sin(2.0 * M_PI * 440.0 * i / 44100.0));
        pcm[2 * i + 1] = (int16_t)(16000.0 * sin(2.0 * M_PI * 880.0 * i / 44100.0));
    }

    /* Test 1: Joint Stereo, Bitpool 53, 44.1 kHz */
    bt_sbc_init(&ctx, 44100, 53, 1);
    uint32_t exp_len = bt_sbc_frame_len(53, 1);
    check("SBC frame length (JS, BP53): 119 bytes", exp_len == 119);

    len = bt_sbc_encode(&ctx, pcm, out);
    check("Encode returns expected length (119 bytes)", len == 119);
    check("Syncword is 0x9C", out[0] == 0x9C);
    check("Bitpool in header matches 53", out[2] == 53);

    /* Test 2: Standard Stereo (non-joint), Bitpool 35, 44.1 kHz */
    bt_sbc_init(&ctx, 44100, 35, 0);
    exp_len = bt_sbc_frame_len(35, 0);
    check("SBC frame length (Stereo, BP35): 82 bytes", exp_len == 82);

    len = bt_sbc_encode(&ctx, pcm, out);
    check("Encode returns expected length (82 bytes)", len == 82);
    check("Syncword is 0x9C", out[0] == 0x9C);
    check("Bitpool in header matches 35", out[2] == 35);

    /* Test 3: Silence encoding */
    memset(pcm, 0, sizeof(pcm));
    bt_sbc_init(&ctx, 44100, 53, 1);
    len = bt_sbc_encode(&ctx, pcm, out);
    check("Encode silence successfully", len == 119 && out[0] == 0x9C);

    printf(fails ? "bt_sbc_test: %d FAILED\n" : "bt_sbc_test: all ok\n", fails);
    return fails != 0;
}
