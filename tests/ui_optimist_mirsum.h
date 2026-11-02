/* The mirror's self-check (io/editor/ed_mirror_sum.h, SUMS) in the UI host test (MIRROR_CHECK): the device's side as ed_mirror.c
 * runs it, over a lossy link model (MIRROR_SUMS=1: tests/ui_optimist_mirsum_run.h). Included after ed_mirror_enc.h. The truth
 * (mt_fb) is every write the mirror is given (a muted one not: the client never gets it), as a client with a perfect link
 * would hold it */
#ifdef MIRROR_CHECK
#include "../firmware/src/io/editor/ed_mirror_sum.h"
static void op_repaint(void);
static void ms_tick(void);                              /* (ui_optimist_mirsum_run.h: each frame, the link and both ends) */
static uint16_t mt_fb[240 * 240];                       /* the device's picture as the client must hold it (RGB565) */
enum { MS_RATE = 9000, MS_LAG = 3000, MS_QMAX = 1u << 18 };
static struct {
    int on, nosum;                                      /* the lossy link on; MIRROR_SUMS=off: no SUMS (the control) */
    int lose_clear, nolose;                             /* MIRROR_SUMS=page: the next whole-panel clear lost; nothing else lost */
    int behind;                                         /* the lag rule refused a frame: nothing encoded until the catch-up */
    uint8_t q[MS_QMAX];                                 /* the link: frames as (len lo, len hi, flag, bytes), drained at MS_RATE */
    uint32_t qw, qr, qbytes, credit;                    /* write, read; bytes waiting; bytes the link may still carry this tick */
    uint32_t seed;
    int arm, cap, retry;                                /* a SUMS check: the next frame hashed; hashing; checks again (another frame) */
    uint32_t sum_ms, soon_ms, mark_ms, catch_ms, fix_ms;
    uint8_t mark_seq, soon, fix_due;
    uint32_t gaps, bads, sums, sum_bytes, sum_valid, asks, ask_bands, fixes, catchups, voids, bytes, refused;
} ms;
static void ms_truth(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px, uint32_t c)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) {
            uint32_t v = px ? px[j * w + i] : c;
            mt_fb[(y + j) * 240u + x + i] = (uint16_t)(px ? (v >> 8 | v << 8) & 0xFFFFu : v);
        }
    msum_see(x, y, w, h, px, c);
}
static int ms_link(void) { return ms.on; }
static uint32_t ms_rand(uint32_t n) { ms.seed = ms.seed * 1664525u + 1013904223u; return (ms.seed >> 8) % n; }
/* a frame onto the link (f, n: F0 .. F7): lost on the way (a gap) or with a byte changed (a bad sum) as the measured pattern,
 * ~2 gaps and ~1 bad sum each MS_RATE bytes */
static void ms_out(const uint8_t *f, uint32_t n)
{
    uint32_t k, flag = 0;
    static const uint8_t WHOLE[8] = {0, 0, 0, 0, 112, 1, 112, 1};   /* (x, y, w, h: 0, 0, 240, 240, 2 x 7 bit each) */
    if (ms.qw - ms.qr + n + 3u > MS_QMAX)
        return;
    ms.bytes += n;
    if (ms.lose_clear && n >= 14u && (f[5] == 0u || f[5] == 8u) && !memcmp(f + 6, WHOLE, 8))
        flag = 1, ms.lose_clear = 0, ms.gaps++;         /* (MIRROR_SUMS=page: the page's clear lost on the way) */
    else if (ms.nolose)
        ;
    else if (ms_rand(MS_RATE) < 2u * n)
        flag = 1, ms.gaps++;
    else if (ms_rand(MS_RATE) < n && n > 10u)
        flag = 2, ms.bads++;
    ms.q[ms.qw++ % MS_QMAX] = (uint8_t)(n & 255u), ms.q[ms.qw++ % MS_QMAX] = (uint8_t)(n >> 8), ms.q[ms.qw++ % MS_QMAX] = (uint8_t)flag;
    for (k = 0; k < n; k++)
        ms.q[ms.qw++ % MS_QMAX] = f[k];
    ms.qbytes += n;
}
static int ms_refuse(void)                              /* (a frame starts: refused past the lag, ed_mirror.c mbr_put) */
{
    if (ms.qbytes <= MS_LAG)
        return 0;
    ms.behind = 1, ms.refused++;
    return 1;
}
static void mir_draw_begin(void)                        /* (op_input.c: the frame the check hashes) */
{
    if (!ms.arm)
        return;
    ms.arm = 0, ms.cap = 1;
    msum_begin();
}
static void mir_draw_end(void)
{
    uint32_t valid;
    uint8_t f[MSUM_LEN];
    if (!ms.cap)
        return;
    ms.cap = 0;
    if (!msum_end(&valid) || ms.behind) {
        ms.voids++;
        if (ms.retry++ < 3)
            ms.arm = 1, op_repaint();                   /* (another frame than the last check's: the next one) */
        return;
    }
    msum_frame(f, me.seq, valid);
    ms_out(f, MSUM_LEN);
    ms.sums++, ms.sum_bytes += MSUM_LEN, ms.sum_valid += (uint32_t)__builtin_popcount(valid);
    mc_frames++, mc_bytes += MSUM_LEN;
}
#endif
