/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/audio_hang_stress.c with HOST_DUAL (and FELUCCA_DUAL=2): the second core as a real thread. mix_block here is
 * system/dual.c mix_block_dual's shape (this tree's): events, macros, duck, the busy voices and the LFO draws before
 * the fork; then the jobs (parts 1..3, the drums, the reverb's kept block) split at random between this thread
 * ("CPU0") and the worker ("CPU1"), rendered at the same time into their own accumulators (the drums onto the buses),
 * a join, the sums, mix_finish. Built with -fsanitize=thread it names every shared value both cores write at once.
 * Included after ui_optimist_test.c. */
#include <stdatomic.h>
static uint32_t dual_vbusy;                            /* (dual.c: the swarm's copies, the same on both cores) */
static mixacc_t hd_acc[2];
static atomic_uint hd_req, hd_done;
static uint32_t hd_mask;
static uint32_t hd_rs = 1;                             /* (its own random: the split) */
#define HD_JOBS (NPART + 1u + (REV_LATE ? 1u : 0u))
#define HD_DRUMS (1u << NPART)
#define HD_REV (1u << (NPART + 1u))

static void hd_clear(mixacc_t *A, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        A->send_c[i] = A->send_d[i] = A->send_r[i] = A->mix_l[i] = A->mix_r[i] = 0;
}
static void hd_run(uint32_t mask, uint32_t n, mixacc_t *A)    /* dual.c dual_run */
{
    uint32_t p;
    for (p = 0; p < HD_JOBS; p++)
        if ((mask >> p) & 1u) {
            if (p < NPART) {
                mix_part(&trk[p], n, A);
            } else if (p == NPART) {
                slicer_drums(mix_l, mix_r, send_r, n);
                dbus_run(n);
            } else {
#if REV_LATE
                if (!rvj.done)
                    rev_job();
#endif
            }
        }
}
static void *hd_cpu1(void *arg)
{
    sigset_t s;
    (void)arg;
    hd_core = 1;
    sigfillset(&s);
    pthread_sigmask(SIG_BLOCK, &s, 0);                  /* (the "interrupts" are CPU0's) */
    for (;;) {
        uint32_t r;
        while ((r = atomic_load(&hd_req)) == atomic_load(&hd_done))
            ;
        hd_clear(&hd_acc[1], CTL);
        hd_run(hd_mask, CTL, &hd_acc[1]);
        atomic_store(&hd_done, r);
    }
    return 0;
}
static void hd_start(void)
{
    pthread_t c1;
    pthread_create(&c1, 0, hd_cpu1, 0);
}
static void dual_lfo_draw_host(void)                   /* dual.c dual_lfo_draw */
{
    uint32_t p;
    for (p = 0; p < NPART; p++) {
        const track_t *t = &trk[p];
        if ((uint32_t)(t->lfo_ph + lfo_inc(t)) < t->lfo_ph)
            dual_lfo_rnd[p] = rng();
    }
}
static void mix_block(int32_t *out, uint32_t n)
{
    mixacc_t *A = &hd_acc[0], *B = &hd_acc[1];
    uint32_t i, mask, jobs, sw, req;
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    hd_clear(A, n);
    events_block(n);
#if FELUCCA_MACROS
    mac_pre();
#endif
    if (FELUCCA_FX_DUCK)
        duck_block(n * (uint32_t)song.g[G_BPM]);
    dual_vbusy = voices_busy();
    dual_lfo_draw_host();
    sw = pold_any() ? pold_in() : 0u;
    drums.a0 = TDRUM->att;
    drums.a1 = 32767 - gain_next(TDRUM);
    jobs = (1u << (NPART + 1u)) - 1u;
#if REV_LATE
    rev_late = 1;
    jobs |= rvj.pend ? HD_REV : 0u;
#endif
    hd_rs = hd_rs * 1664525u + 1013904223u;
    mask = (hd_rs >> 9) & jobs;                         /* (any split: the device's follows the costs) */
    hd_mask = mask;
    req = atomic_load(&hd_req) + 1u;
    atomic_store(&hd_req, req);                         /* (post: the block's state before it, seq_cst) */
    hd_run(jobs & ~mask, n, A);
    while (atomic_load(&hd_done) != req)                /* (join) */
        ;
    for (i = 0; i < n; i++) {
        send_c[i] += A->send_c[i] + B->send_c[i];
        send_d[i] += A->send_d[i] + B->send_d[i];
        send_r[i] += A->send_r[i] + B->send_r[i];
        mix_l[i] += A->mix_l[i] + B->mix_l[i];
        mix_r[i] += A->mix_r[i] + B->mix_r[i];
    }
    if (sw)
        pold_out(sw);
    mix_finish(out, n);
#if FELUCCA_MACROS
    mac_post();
#endif
}
