/* SPDX-License-Identifier: GPL-3.0-only */
/* JUNO CHORUS, the generic FX unit (firmware/src/fx/junocho/juno_cho.h; fm-106's MN3009 model, which it matches sample
 * for sample: the harness todo1014/juno/harness/cho_vs_fm106.c outside the repo):
 *   - MIX 0 gives the input itself (L = R = in);
 *   - each MODE at MIX 127: bounded, L and R differ (stereo from mono), the three modes differ, the render's FNV-1a 64 as
 *     below (a change of the sound fails: update the hashes with the change);
 *   - the mid / side form: mid + side = L, mid - side = R (within 1);
 *   - the headroom shift: sh 3 renders the same as sh 0 on an input 8x larger (within 8);
 *   - its cost, printed: host instructions a sample.
 * build/host/juno_cho_test; exit status: the number of failed checks. */
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../firmware/src/fx/junocho/juno_cho.h"

static int bad;
static void check(const char *what, int ok)
{
    printf("juno_cho: %-90s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define N (32u * 2756u)                                 /* 2 s of 32-sample blocks */
static int32_t in[N], l[N], r[N], l2[N], r2[N];
static juno_cho_t c;
static void source(int32_t scale)                       /* two saws and some noise, peaks ~10000 */
{
    uint32_t i, s = 5;
    for (i = 0; i < N; i++) {
        s = s * 1664525u + 1013904223u;
        in[i] = ((int32_t)((i * 37u) % 1000u) * 6 - 3000 + (int32_t)((i * 53u) % 700u) * 6 - 2100 + (int32_t)(s >> 22) - 512) * scale;
    }
}
static void run(uint32_t mode, uint32_t mix, int32_t *L, int32_t *R, uint32_t sh)
{
    uint32_t i;
    memset(&c, 0, sizeof c);
    for (i = 0; i < N; i += 32u)
        juno_cho_run(&c, mode, mix, in + i, L + i, R + i, 32, sh);
}
static uint64_t fnv(const int32_t *a, const int32_t *b)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i;
    for (i = 0; i < N; i++)
        h = ((h ^ (uint32_t)a[i]) * 1099511628211ull ^ (uint32_t)b[i]) * 1099511628211ull;
    return h;
}
static uint64_t inow(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}

int main(void)
{
    static const uint64_t GOLD[3] = {0xb693353380f4c0f9ull, 0xf3f19e3e0b829ce3ull, 0x59b06b9b2bc8252full};
    uint64_t h[3];
    uint32_t m, i, ok = 1, ms = 1, shok = 1;
    int upd = getenv("GOLDEN_UPDATE") && atoi(getenv("GOLDEN_UPDATE"));
    source(1);
    run(0, 0, l, r, 0);
    for (i = 0; i < N; i++)
        ok &= l[i] == in[i] && r[i] == in[i];
    check("MIX 0: the input itself, L and R", ok);
    for (m = 0; m < 3u; m++) {
        uint32_t st = 0, bounded = 1;
        uint64_t t0 = inow();
        run(m, 127, l, r, 0);
        printf("juno_cho: MODE %u: %.1f host instructions a sample\n", m, (double)(inow() - t0) / N);
        for (i = 0; i < N; i++)
            st += l[i] != r[i], bounded &= abs(l[i]) < 32768 && abs(r[i]) < 32768;
        h[m] = fnv(l, r);
        printf("juno_cho: MODE %u: %016llx\n", m, (unsigned long long)h[m]);
        check(m == 0 ? "MODE I: bounded, L and R differ" : m == 1 ? "MODE II: bounded, L and R differ" : "MODE I+II: bounded, L and R differ",
              bounded && st > N / 2);
    }
    check("the three modes differ", h[0] != h[1] && h[1] != h[2] && h[0] != h[2]);
    if (!upd)
        check("each mode renders as before (the hashes in this file)", h[0] == GOLD[0] && h[1] == GOLD[1] && h[2] == GOLD[2]);
    run(1, 127, l, r, 0);
    memcpy(l2, in, sizeof in);
    memset(&c, 0, sizeof c);
    for (i = 0; i < N; i += 32u)
        juno_cho_ms(&c, 1, 127, l2 + i, r2 + i, 32, 0);
    for (i = 0; i < N; i++)
        ms &= abs(l2[i] + r2[i] - l[i]) <= 1 && abs(l2[i] - r2[i] - r[i]) <= 1;
    check("mid / side: mid + side = L, mid - side = R (within 1)", ms);
    source(8);
    run(2, 100, l2, r2, 3);
    source(1);
    run(2, 100, l, r, 0);
    for (i = 0; i < N; i++)
        shok &= abs(l2[i] - 8 * l[i]) <= 8 && abs(r2[i] - 8 * r[i]) <= 8;
    check("sh 3 on an input 8x larger: the same, 8x (within 8)", shok);
    printf("juno_cho: %d failed\n", bad);
    return bad;
}
