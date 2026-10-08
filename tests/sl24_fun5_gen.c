/* SPDX-License-Identifier: GPL-3.0-only */
/* Writes tests/sl24_fun5.bin, the golden SLOOP 2.4 project of tests/sl24_import_test.c, with SLOOP 2.4's OWN types:
 * built against isod89/sloop-fm1 v2.4 (8d3823f) firmware/src/core.h, its project_t / proj_trk_t taken from its
 * project.c as they are (FUN5, 3840 B, FNV-1a sum). Not run by run_tests.sh (it needs their tree); to make it again:
 *
 *   R=<sloop-fm1 v2.4 checkout>/firmware/src
 *   sed -n '/^typedef struct {  *\/\* one track; the drum/,/^} project_t;/p' $R/project.c > /tmp/sl24_project_t.h
 *   cc '-D__attribute__(x)=' -I$R -I/tmp -o /tmp/gen tests/sl24_fun5_gen.c && /tmp/gen tests/sl24_fun5.bin
 *
 * What it holds (sl24_import_test.c checks each): see main. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/core/core.h"                       /* SLOOP 2.4's: P_*, G_*, step_t, plock_t, NLOCK, FC_*, NTRK, NSTEP */
#include "sl24_project_t.h"             /* SLOOP 2.4's proj_trk_t, project_t (project.c) */
_Static_assert(sizeof(project_t) == 3840u, "SLOOP 2.4's FUN5");

static uint32_t fnv(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t s = 0x811C9DC5u, i;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static void lock(proj_trk_t *t, int i, int step, int param, int val)
{
    t->lock[i].step = (uint8_t)step, t->lock[i].param = (uint8_t)param, t->lock[i].val = (int16_t)val;
}
static void fill(proj_trk_t *t, int i, int c) { t->fill[i / 4] = (uint8_t)(t->fill[i / 4] | c << (2 * (i % 4))); }

int main(int argc, char **argv)
{
    static project_t q;
    int k, i;
    FILE *f;
    memset(&q, 0, sizeof q);
    q.magic = 0x46554E35u, q.size = sizeof q;
    for (i = 0; i < G_COUNT; i++)
        q.g[i] = (int16_t)(i + 1);
    q.g[G_ROUTE] = 1;                            /* their MIDI IN = CLOCK: never our G_VIEW */
    q.sel = 2;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *t = &q.t[k];
        for (i = 0; i < P_COUNT; i++)
            t->p[i] = (int16_t)(k * 7 + i % 40);
        t->p[P_TFLT] = -20, t->p[P_STRUM] = 15, t->p[P_VLEAD] = 1;
        for (i = 0; i < 8; i++)
            t->p[P_E0 + i] = (int16_t)(10 + i + k);
        for (i = 0; i < NSTEP; i++) {
            uint8_t *s = (uint8_t *)&t->step[i];
            int j;
            for (j = 0; j < 10; j++)
                s[j] = (uint8_t)(i * 3 + j + k);
        }
        for (i = 0; i < NLOCK; i++)
            t->lock[i].step = LOCK_FREE;
        t->micro[3] = (int8_t)(-5 - k), t->micro[10] = MICRO_MAX;
        fill(t, 1, FC_FILL), fill(t, 2, FC_NOFILL);
        lock(t, 0, 2, P_PAN, -30 + k);           /* kept */
        lock(t, 1, 4, P_TFLT, 50);               /* their track filter: not ours yet, dropped */
        lock(t, 2, 5, P_E0 + 1, 7);              /* EDIT 2: ours P_E1 (FM6's and the drum track's differ) */
        lock(t, 3, 6, P_LEVEL, 100);
    }
    q.t[0].engine = 0, q.t[0].preset = 3;        /* ANALOG */
    q.t[1].engine = 9, q.t[1].preset = 1;        /* FM6: ALG 5, FB 2, .. PTCH F3 (ROUND BASS) */
    {
        static const int16_t E[8] = {5, 2, 10, -3, 4, 1, 64, 2};
        memcpy(&q.t[1].p[P_E0], E, sizeof E);
    }
    q.t[2].engine = 6, q.t[2].preset = 2;        /* TRIO */
    q.t[3].p[P_E0] = 7;                          /* the drum track: kit 7 (606) */
    lock(&q.t[3], 4, 8, P_E0, 12);               /* a kit lock: 12 (BOOMBAP) */
    lock(&q.t[3], 5, 9, P_E0, 38);               /* a kit lock on their USR2 (37 + 1): ours has no such kit */
    q.sum = fnv(&q, sizeof q - 4u);
    if (argc < 2 || !(f = fopen(argv[1], "wb")) || fwrite(&q, sizeof q, 1, f) != 1)
        return 1;
    fclose(f);
    printf("%s: SLOOP 2.4 FUN5, %u B, sum %08X\n", argv[1], (unsigned)sizeof q, (unsigned)q.sum);
    return 0;
}
