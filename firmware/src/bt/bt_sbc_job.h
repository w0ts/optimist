/* SPDX-License-Identifier: GPL-3.0-only */
/* The Bluetooth audio stream's SBC encoding as a job of the dual render (system/dual.c, bt/bt_acl.c bt_audio_feed).
 * 2026-11-01 (the user: "cpu0 hit 100% while cpu1 never exceeded 45%"): a 128-frame SBC frame was encoded inside the
 * audio interrupt on CPU0 every fourth mix block, after the mix (~4x a part's per-sample cost in that block, host
 * count): a peak on CPU0 alone. With the second core up, the block that completes a frame keeps it (BA_SBC_JOB: its
 * PCM stays in ba_pcm_acc); the next block's render encodes it as one of its jobs, on whichever core dual_split
 * gives it, while the other renders parts; bt_audio_feed then sends it before it takes that block's samples. The
 * frame's bytes are the same, it leaves one mix block (0.73 ms) later. CPU1 down (or a frame that ends inside a
 * block): encoded inline, as before. Included by system/dual.c and bt/bt_acl.c. */
#ifndef BT_SBC_JOB_H
#define BT_SBC_JOB_H
#include <stdint.h>
#ifndef FELUCCA_BENCH_SBC
#define FELUCCA_BENCH_SBC 0                        /* (a bench build: SBC frames as a stream's, dual.c, bt_acl.c ba_sbc_bench) */
#endif
#if defined(FELUCCA_BT_AUDIO) && FELUCCA_BT_AUDIO && DUAL_RENDER
#define BA_SBC_JOB 1
static struct {
    volatile uint8_t pend;                         /* a complete frame in ba_pcm_acc, not yet sent (bt_audio_feed) */
    volatile uint8_t done;                         /* its bytes are in `frame` (the job ran: either core) */
    uint8_t late;                                  /* keep the next complete frame for a job (dual.c: CPU1 is up) */
    uint32_t len;                                  /* its length */
    uint8_t frame[128];
} sbcj;
static void ba_sbc_job(void);                      /* bt_acl.c: encode the kept frame (XIP: dual.c calls it FAR) */
#else
#define BA_SBC_JOB 0
#endif
#endif
