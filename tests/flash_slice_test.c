/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* hal/fm1_flash_slice.h on a model of the P25Q80H (datasheet §5.4, §10.36 - 10.38): a sector erase in short
 * interrupts-off windows, suspended (75h) between them, resumed (7Ah). Checked: the longest window stays near
 * FLS_SLICE_US (an audio half is 5.8 ms; the whole erase was one 17.2 ms window), the sector always ends erased and
 * never suspended, the audio buffer is silenced only by a window that holds the rest of an erase; and the parts that do
 * not behave: one ignoring the suspend, one without a SUS bit, one suspending late, one losing its progress at every
 * suspend, one whose erase ends as the suspend comes, WEL not set, never ready. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ---- the part ---- */
enum { M_NORMAL, M_NO_SUSPEND, M_NO_SUS_BIT, M_LATE_SUSPEND, M_LATE_STICKY, M_RESTARTS, M_NO_WEL, M_STUCK };
static struct {
    uint32_t now;                   /* ticks, 24 per us */
    int mode;
    uint32_t erase_need;            /* ticks of erasing the erase needs */
    uint32_t erase_done;            /* ticks erased so far */
    uint32_t last;                  /* the time the model was last advanced */
    uint8_t busy, suspended, wel, erased, sus_pending;
    uint32_t sus_at;                /* when a requested suspend takes effect */
    uint32_t suspends, resumes, resumes_idle, erases, invals;
} nor;

static void nor_advance(void)
{
    uint32_t dt = nor.now - nor.last;
    nor.last = nor.now;
    if (!nor.busy || nor.suspended)
        return;
    if (nor.sus_pending && nor.now >= nor.sus_at) {     /* (the erase runs on until the suspend takes effect) */
        uint32_t ran = dt - (nor.now - nor.sus_at);
        nor.erase_done += ran;
        nor.sus_pending = 0;
        if (nor.erase_done >= nor.erase_need && nor.mode != M_STUCK) {
            nor.busy = 0;
            nor.erased = 1;
            return;
        }
        nor.suspended = 1;
        if (nor.mode == M_RESTARTS)
            nor.erase_done = 0;
        return;
    }
    nor.erase_done += dt;
    if (nor.erase_done >= nor.erase_need && nor.mode != M_STUCK) {
        nor.busy = 0;
        nor.sus_pending = 0;            /* (ended before the suspend took effect: nothing suspended) */
        nor.erased = 1;
    }
}
static void tick(uint32_t us) { nor.now += us * 24u; nor_advance(); }

#define FLS_INL static inline
#define FLS_NOW() (tick(0), nor.now)
static uint32_t sr1(void)
{
    tick(1);
    return (uint32_t)((nor.busy && !nor.suspended) || nor.sus_pending) | (uint32_t)nor.wel << 1;
}
static uint32_t sr2(void)
{
    tick(1);
    return nor.suspended && nor.mode != M_NO_SUS_BIT ? 0x04u : 0u;
}
static void erase_cmd(uint32_t off)
{
    (void)off;
    tick(1);
    if (!nor.wel || nor.busy)
        return;
    nor.busy = 1; nor.erased = 0; nor.erase_done = 0; nor.wel = 0; nor.erases++;
}
static void nor_cmd(uint32_t c)
{
    tick(1);
    if (c == 0x06u) {
        if (nor.mode != M_NO_WEL && !(nor.busy && !nor.suspended))
            nor.wel = 1;
    } else if (c == 0x75u) {
        if (!nor.busy || nor.suspended || nor.mode == M_NO_SUSPEND)
            return;
        nor.suspends++;
        nor.sus_pending = 1;
        nor.sus_at = nor.now + (nor.mode == M_LATE_SUSPEND || nor.mode == M_LATE_STICKY ? 150u : 20u) * 24u;
    } else if (c == 0x7Au) {
        if (nor.suspended) {
            nor.suspended = 0;
            nor.resumes++;
        } else if (nor.mode != M_LATE_STICKY) {
            nor.sus_pending = 0;                /* (a suspend not taken yet: cancelled) */
            nor.resumes_idle++;
        }
    }
}
#define FLS_SR1() sr1()
#define FLS_SR2() sr2()
#define FLS_ERASE(off) erase_cmd(off)
#define FLS_CMD(c) nor_cmd(c)
#define FLS_INVAL(off) ((void)(off), nor.invals++)

/* ---- the caller: windows and the gaps between them (the audio interrupt runs, 3 ms) ---- */
static uint32_t win_max_us, gaps;
#define FLS_WINDOW_FN window_call
#define FLS_GAP() (gaps++, tick(3000u))
#include "../firmware/hal/fm1_flash_slice.h"
static int window_call(uint32_t off, uint32_t op, fls_job_t *j)
{
    uint32_t t0 = nor.now, us;
    int rc;
    j->windows++;
    rc = fls_window(off, op, j);
    us = (nor.now - t0) / 24u;
    j->took_us += us;
    if (us > win_max_us)
        win_max_us = us;
    return rc;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int32_t quiet[8];
static fls_job_t job;
static int run(int mode, uint32_t erase_us, uint32_t slice_us)
{
    uint32_t i;
    memset(&nor, 0, sizeof nor);
    nor.mode = mode;
    nor.erase_need = erase_us * 24u;
    win_max_us = gaps = 0;
    for (i = 0; i < 8u; i++)
        quiet[i] = 0x1234;
    memset(&job, 0, sizeof job);
    job.slice_us = slice_us;
    job.quiet = quiet;
    job.quiet_n = 8;
    return fls_erase(0x97000u, &job);
}
static int quiet_kept(void) { return quiet[0] == 0x1234 && quiet[7] == 0x1234; }
static int silenced(void) { return quiet[0] == 0 && quiet[7] == 0; }

int main(void)
{
    int rc;
    /* the datasheet's typical erase, 8 ms, and the 17.2 ms the FM-1 showed */
    rc = run(M_NORMAL, 8000, FLS_SLICE_US);
    CHECK(rc == 0 && nor.erased && !nor.busy && !nor.suspended, "8 ms: rc %d erased %d", rc, nor.erased);
    CHECK(win_max_us <= FLS_SLICE_US + 60u, "8 ms: longest window %u us", win_max_us);
    CHECK(job.windows >= 8u && job.windows <= 10u && !job.held && quiet_kept(), "8 ms: %u windows held %d", job.windows, job.held);
    CHECK(nor.erases == 1u && job.nosus == 0u, "8 ms: %u erase commands, %u nosus", nor.erases, job.nosus);
    printf("8 ms erase: %u windows, longest %u us, erase time %u us\n", job.windows, win_max_us, job.took_us);

    rc = run(M_NORMAL, 17186, FLS_SLICE_US);
    CHECK(rc == 0 && nor.erased && !nor.suspended, "17 ms: rc %d", rc);
    CHECK(win_max_us <= FLS_SLICE_US + 60u, "17 ms: longest window %u us", win_max_us);
    CHECK(!job.held && quiet_kept(), "17 ms: held %d", job.held);
    printf("17.2 ms erase: %u windows, longest %u us (was one window of 17186 us)\n", job.windows, win_max_us);

    /* one window (slice 0): today's behaviour, silenced */
    rc = run(M_NORMAL, 8000, 0);
    CHECK(rc == 0 && nor.erased && job.windows == 1u && job.held && silenced(), "one window: rc %d windows %u", rc, job.windows);
    CHECK(nor.suspends == 0u, "one window: %u suspends", nor.suspends);

    /* a part that ignores 75h: one window holds the rest, silenced, erased */
    rc = run(M_NO_SUSPEND, 8000, FLS_SLICE_US);
    CHECK(rc == 0 && nor.erased && job.windows == 1u && job.held && silenced(), "no suspend: rc %d windows %u held %d", rc, job.windows, job.held);

    /* a part without the SUS bit: still resumed and finished */
    rc = run(M_NO_SUS_BIT, 8000, FLS_SLICE_US);
    CHECK(rc == 0 && nor.erased && !nor.suspended && !job.held && job.nosus > 0u, "no SUS bit: rc %d nosus %u", rc, job.nosus);
    CHECK(win_max_us <= FLS_SLICE_US + 60u, "no SUS bit: longest window %u us", win_max_us);

    /* a suspend later than FLS_SUS_US: cancelled by a resume, the window holds the rest; never left suspended */
    rc = run(M_LATE_SUSPEND, 8000, FLS_SLICE_US);
    CHECK(rc == 0 && nor.erased && !nor.suspended && !nor.busy && job.held && silenced(), "late suspend: rc %d erased %d susp %d", rc, nor.erased, nor.suspended);

    /* ... and one that suspends after that resume all the same: seen by its SUS bit in the holding window, resumed */
    rc = run(M_LATE_STICKY, 8000, FLS_SLICE_US);
    CHECK(rc == 0 && nor.erased && !nor.suspended && !nor.busy && job.held && nor.resumes == 1u, "late sticky: rc %d erased %d susp %d resumes %u", rc, nor.erased, nor.suspended, nor.resumes);

    /* progress lost at each suspend: FLS_SLICES_MAX windows, then the rest in one */
    rc = run(M_RESTARTS, 8000, FLS_SLICE_US);
    CHECK(rc == 0 && nor.erased && !nor.suspended && job.windows == FLS_SLICES_MAX + 1u && job.held, "restarts: rc %d windows %u", rc, job.windows);

    /* the erase ends just as the suspend comes (the suspend's 20 us): resumed for nothing, done */
    rc = run(M_NORMAL, FLS_SLICE_US + 10u, FLS_SLICE_US);
    CHECK(rc == 0 && nor.erased && !nor.suspended && !job.held && quiet_kept() && job.windows == 2u, "ends at the suspend: rc %d windows %u susp %u took %u", rc, job.windows, nor.suspends, job.took_us);
    CHECK(nor.resumes_idle == 1u && job.nosus == 1u, "ends at the suspend: %u idle resumes, nosus %u", nor.resumes_idle, job.nosus);

    /* errors */
    rc = run(M_NO_WEL, 8000, FLS_SLICE_US);
    CHECK(rc == -3 && nor.erases == 0u && quiet_kept(), "no WEL: rc %d", rc);
    rc = run(M_STUCK, 8000, FLS_SLICE_US);
    CHECK(rc == -4 && job.held && silenced(), "never ready: rc %d", rc);
    CHECK(job.windows == FLS_SLICES_MAX + 1u, "never ready: %u windows", job.windows);

    printf(fails ? "flash_slice_test: %d FAILED\n" : "flash_slice_test: all passed\n", fails);
    return fails != 0;
}
