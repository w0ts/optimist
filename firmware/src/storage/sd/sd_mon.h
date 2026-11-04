/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card monitor's view (sd_mon.c, included by sd_stream.c; SYSTEM > SD STREAM, ui/optimist/op_sdmon.c). Only types
 * and the functions the UI calls: the UI is compiled before the card's code in the unity build. Main loop only. */
#ifndef SD_MON_H
#define SD_MON_H
#include <stdint.h>

#define SSM_NO 0xFFFFFFFFu                           /* no stream fed by the card in the period */
#define SSM_HOLD_MS 10000u                           /* a peak is held this long */

typedef struct {
    uint32_t kbs, kbs_pk;                            /* KB/s read in the last period, the peak of the last 10 s */
    uint32_t n, n_pk;                                /* streams sounding now, the most in the last 10 s */
    uint32_t fill_lo;                                /* the least a ring holds of the streams the card feeds, percent, in
                                                      * the last 10 s (SSM_NO: none) */
    uint32_t dry, nos, late, miss, crc, demo;        /* since the boot or the last clear: streams that ran dry, notes with no
                                                      * ring free, GRAIN's and STRETCH's late and missed reads, blocks
                                                      * whose CRC failed, steps down of the card's clock for them */
    uint8_t dry_hot;                                 /* a stream ran dry within the last 10 s */
} ssm_view_t;

static void ssm_view(ssm_view_t *v);
static void ssm_clear(void);
#endif
