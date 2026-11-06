/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card monitor (SYSTEM > SD STREAM, ui/optimist/op_sdmon.c; the user, 2026-11-05: "we should totally monitor how we
 * are doing on the SD read rate and sample playback somehow"). Included by sd_stream.c after the caches (sd_stream_rc.c).
 * Main loop only: ssm_poll() runs at the top of sst_go (every pass of the idle wait), and every SSM_MS reads
 *   the blocks read (the streams' runs and the caches': sst_st.blocks + sst_rcs.blocks) -> KB/s over the period,
 *   the streams sounding now, the least a ring holds of the streams the card still feeds (slots filled ahead of the voice,
 *   in percent of the ring),
 * and keeps each one's best (the most / the least) for SSM_HOLD_MS: the peak held, then the next period's value. The audio
 * never reads or writes any of it: it keeps counting sst_dry and sst_nostream, and the caches' late and miss, as before; the
 * monitor adds the changes it sees into its own totals (so a counter reset elsewhere, the console's sd stream, loses none and
 * a CLEAR here leaves the console's as they are). Counted from the clock's ticks (SD_NOW): a difference wraps cleanly.
 *   ssm_view(&v)    what the page shows;  ssm_clear()  the totals and the peaks from 0 */
#include "sd_mon.h"
#ifndef SSM_MS
#define SSM_MS 500u                                  /* a period */
#endif
typedef struct { uint32_t last, tot; } ssm_ctr_t;    /* a counter as last seen, and the changes added since the clear */
enum { SSM_DRY, SSM_NOS, SSM_LATE, SSM_MISS, SSM_CRC, SSM_DEMO, SSM_NCTR };
static struct {
    uint32_t t, blocks;                              /* the last sample's tick, the blocks read by then */
    uint32_t kbs, kbs_pk, kbs_t;                     /* KB/s in the last period, its peak held, since when (tick) */
    uint32_t n, n_pk, n_t;                           /* streams sounding, the most held */
    uint32_t fill, fill_lo, fill_t;                  /* the least ring fill now / held (percent, SSM_NO none) */
    uint32_t dry_t;                                  /* the tick sst_dry last moved */
    uint8_t dry_hot, live;                           /* it moved within SSM_HOLD_MS; the first sample taken */
    ssm_ctr_t c[SSM_NCTR];
} ssm;

static void ssm_errs(uint32_t *crc, uint32_t *demo)  /* the card driver's: blocks whose CRC16 failed, steps down for them */
{
#ifdef PLAT_SD_NSTEP                                 /* (the release: fm1-base's plat_sd_*) */
    plat_sd_errs(crc, demo);
#else
    *crc = sd_crc_fails;
    *demo = sd_demotions;
#endif
}

static void ssm_add(ssm_ctr_t *c, uint32_t cur)      /* + what the counter gained (it restarted below its last: all it has) */
{
    c->tot += cur >= c->last ? cur - c->last : cur;
    c->last = cur;
}

/* a best held for SSM_HOLD_MS: the new value when it beats the held one (more = 1: larger) or the hold ran out */
static uint32_t ssm_best(uint32_t held, uint32_t *at, uint32_t v, uint32_t now, uint32_t more)
{
    const uint32_t out = (uint32_t)(now - *at) >= SSM_HOLD_MS * SD_TICKS_MS;
    if (out || held == SSM_NO || (v != SSM_NO && (more ? v >= held : v <= held))) {
        *at = now;
        return v;
    }
    return held;
}

static void ssm_streams(uint32_t *n, uint32_t *fill)   /* streams sounding; the least ring fill of those the card feeds */
{
    uint32_t i, lo = SSM_NO;
    *n = 0;
    for (i = 0; i < SST_N; i++) {
        const sst_t *st = &sst[i];
        uint32_t slots;
        if (!st->on || (st->owner && !*st->owner))
            continue;
        (*n)++;
        if (st->pgen != st->gen || st->done)         /* (not seen by the main loop yet, or all of it in the head / read) */
            continue;
        slots = st->wr > st->rd ? st->wr - st->rd : 0u;
        slots = slots * 100u / sst_k;
        if (slots < lo)
            lo = slots > 100u ? 100u : slots;
    }
    *fill = lo;
}

static void ssm_sample(uint32_t now)                 /* one period ended at tick now */
{
    uint32_t blocks = sst_st.blocks + sst_rcs.blocks, dt_ms = (uint32_t)(now - ssm.t) / SD_TICKS_MS, crc, demo, dry;
    ssm_errs(&crc, &demo);
    if (!ssm.live)
        ssm.fill_lo = SSM_NO;
    ssm.kbs = ssm.live && blocks >= ssm.blocks ? (blocks - ssm.blocks) * 512u / dt_ms : 0u;   /* (B/ms = KB/s near enough) */
    ssm.blocks = blocks;
    ssm.t = now;
    ssm.live = 1;
    ssm.kbs_pk = ssm_best(ssm.kbs_pk, &ssm.kbs_t, ssm.kbs, now, 1);
    ssm_streams(&ssm.n, &ssm.fill);
    ssm.n_pk = ssm_best(ssm.n_pk, &ssm.n_t, ssm.n, now, 1);
    ssm.fill_lo = ssm_best(ssm.fill_lo, &ssm.fill_t, ssm.fill, now, 0);
    dry = ssm.c[SSM_DRY].tot;
    ssm_add(&ssm.c[SSM_DRY], sst_dry);
    ssm_add(&ssm.c[SSM_NOS], sst_nostream);
    ssm_add(&ssm.c[SSM_CRC], crc);
    ssm_add(&ssm.c[SSM_DEMO], demo);
    {
        uint32_t p, late = 0, miss = 0;
        for (p = 0; p < SST_NPART; p++) {
            late += sst_rc[p].late;
            miss += sst_rc[p].miss;
        }
        ssm_add(&ssm.c[SSM_LATE], late);
        ssm_add(&ssm.c[SSM_MISS], miss);
    }
    if (ssm.c[SSM_DRY].tot != dry)
        ssm.dry_t = now;
    ssm.dry_hot = ssm.c[SSM_DRY].tot && (uint32_t)(now - ssm.dry_t) < SSM_HOLD_MS * SD_TICKS_MS;
}

static void ssm_poll(void)                           /* sst_go's first line: a period passed -> a sample (a compare otherwise) */
{
    const uint32_t now = SD_NOW();
    if (!ssm.live || (uint32_t)(now - ssm.t) >= SSM_MS * SD_TICKS_MS)
        ssm_sample(now);
}

static void ssm_view(ssm_view_t *v)
{
    v->kbs = ssm.kbs;
    v->kbs_pk = ssm.kbs_pk > ssm.kbs ? ssm.kbs_pk : ssm.kbs;
    v->n = ssm.n;
    v->n_pk = ssm.n_pk > ssm.n ? ssm.n_pk : ssm.n;
    v->fill = ssm.live ? ssm.fill : SSM_NO;
    v->fill_lo = ssm.live ? ssm.fill_lo : SSM_NO;
    v->dry = ssm.c[SSM_DRY].tot;
    v->nos = ssm.c[SSM_NOS].tot;
    v->late = ssm.c[SSM_LATE].tot;
    v->miss = ssm.c[SSM_MISS].tot;
    v->crc = ssm.c[SSM_CRC].tot;
    v->demo = ssm.c[SSM_DEMO].tot;
    v->dry_hot = ssm.dry_hot;
}

static void ssm_clear(void)                          /* CLEAR: the totals and the peaks from 0 (the counters themselves go on) */
{
    uint32_t k;
    for (k = 0; k < SSM_NCTR; k++)
        ssm.c[k].tot = 0;
    ssm.kbs_pk = ssm.kbs;
    ssm.n_pk = ssm.n;
    ssm.fill_lo = ssm.fill;
    ssm.dry_hot = 0;
}
