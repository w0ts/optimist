/* SPDX-License-Identifier: GPL-3.0-only */
/* Writes the golden SLOOP 2.5 / 2.6 files of tests/sl25_safety_test.c and tests/sl26_import_test.c with SLOOP's OWN
 * types: built against isod89/sloop-fm1 firmware/src/core.h and arranger.h, its project_t, persist_t, panel_t and
 * dsu_bank_t cut out of its project.c, panel.c and drum_synth.c as they are (tests/sl25_extract.py). Not run by
 * run_tests.sh (it needs their tree); to make them again, for each of v2.5 (fa9ce57) and v2.6 (d30d655):
 *
 *   R=<sloop-fm1 checkout at the tag>/firmware/src; D=<a scratch directory>
 *   python3 tests/sl25_extract.py $R $D/sl25_types.h
 *   cc '-D__attribute__(x)=' -DSL_VER=25 -I$R -I$D -o $D/gen tests/sl25_gen.c && $D/gen tests   (v2.6: -DSL_VER=26)
 *
 * 2.5 writes tests/sl25_fun5.bin (a project: FUN5, 3840 B) and tests/sl25_settings.bin (the settings object: persist_t,
 * 88 B, then the SYN kits, 1464 B); 2.6 tests/sl26_fun6.bin (FUN6, 3816 B) and tests/sl26_settings.bin (and the MIDI
 * part, 40 B). What each holds: see main (the tests check it). */
#include <stdio.h>
#include <string.h>
#include "core.h"                               /* SLOOP's: P_*, G_*, ENGI_*, step_t, plock_t, NLOCK, FC_*, MICRO_* */
#include "arranger.h"
#include "sl25_types.h"                         /* SLOOP's panel_t, dsu_bank_t, proj_trk_t, project_t, persist_t */
#if SL_VER == 25
_Static_assert(sizeof(project_t) == 3840u && P_COUNT == 61 && P_E0 == 53 && ENGI_PHYS == 10u && ENGI_NOISE == 11u,
               "SLOOP 2.5's FUN5, its engines");
#else
_Static_assert(sizeof(project_t) == 3816u && P_COUNT == 71 && P_E0 == 63 && P_ITYPE == 53 && P_FTYPE == 58 &&
               ENGI_PHYS == 10u, "SLOOP 2.6's FUN6");
#define ML_N 16u                                /* (seq.c) */
#endif
_Static_assert(sizeof(persist_t) == 88u && sizeof(dsu_bank_t) == 1464u, "their settings object: 88 B, then the kits");

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
static int put(const char *dir, const char *name, const void *b, size_t n)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "wb")) || fwrite(b, n, 1, f) != 1)
        return 1;
    fclose(f);
    printf("%s: %u B\n", path, (unsigned)n);
    return 0;
}

/* the settings object: persist_t (palette 2, low cut on, the default panel, a song order, a lights word with NOTES lit,
 * SYNC USB, MIDI OUT SEQ) then 4 SYN kits (names SYNA..SYND, every sound byte a pattern) */
static size_t settings(uint8_t *o)
{
    persist_t p;
    dsu_bank_t k;
    uint32_t i, j;
    memset(&p, 0, sizeof p);
    p.magic = SL_PERSIST_MAGIC, p.palette = 2, p.lowcut = 1, p.zoom = 0;
    p.panel.magic = 0x50414E35u;                /* "PAN5" */
    for (i = 0; i < NB; i++)
        p.panel.btn[i] = (uint8_t)i;
    for (i = 0; i < NE; i++)
        p.panel.enc[i] = (uint8_t)i, p.panel.dir[i] = 1;
    p.arrangement.count = 3;
    for (i = 0; i < 16u; i++)
        p.arrangement.entry[i].scene = (uint8_t)(i % 3u), p.arrangement.entry[i].bars = 2;
    p.lights = 0x0000C1A5u | 1u << 8 | 1u << 12 | 1u << 14;
    memset(&k, 0, sizeof k);
    k.magic = DSU_MAGIC, k.ver = 1, k.n = DSU_N;
    for (i = 0; i < DSU_N; i++) {
        uint8_t *s = (uint8_t *)k.k[i].s;
        memcpy(k.k[i].name, "SYNA", 4), k.k[i].name[3] = (char)('A' + i);
        k.k[i].crush = (uint8_t)i, k.k[i].src = (uint8_t)(i * 3u);
        for (j = 0; j < sizeof k.k[i].s; j++)
            s[j] = (uint8_t)(j * 5u + i + 1u);
    }
    memcpy(o, &p, sizeof p);
    memcpy(o + sizeof p, &k, sizeof k);
#if SL_VER == 26
    {   /* the MIDI part (project.c midi_part_fill): CC 20 learned on P_PAN of track 2, channels 2 3 4, drums 11 | OTHER
         * OFF, BEND 7 */
        uint8_t *m = o + sizeof p + sizeof k;
        uint16_t e = (uint16_t)(20u | (1u + P_PAN) << 9);
        memset(m, 0, 2u * ML_N + 8u);
        m[0] = (uint8_t)e, m[1] = (uint8_t)(e >> 8);
        m[2u * ML_N] = 2, m[2u * ML_N + 1u] = 3, m[2u * ML_N + 2u] = 4, m[2u * ML_N + 3u] = 11u | 1u << 7;
        m[2u * ML_N + 4u] = 7;
        return sizeof p + sizeof k + 2u * ML_N + 8u;
    }
#endif
    return sizeof p + sizeof k;
}

int main(int argc, char **argv)
{
    static project_t q;
    static uint8_t set[4096];
    const char *dir = argc > 1 ? argv[1] : ".";
    int k, i;
    size_t n;
    memset(&q, 0, sizeof q);
    q.magic = SL_VER == 25 ? 0x46554E35u : 0x46554E36u, q.size = sizeof q;
    for (i = 0; i < PROJ_NG; i++)
        q.g[i] = (int16_t)(i + 1);
    q.g[G_ROUTE] = 1;
    q.sel = 1, q.drdly = 77;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *t = &q.t[k];
#if SL_VER == 25
        for (i = 0; i < P_COUNT; i++)
            t->p[i] = (int16_t)(k * 7 + i % 40);
        t->p[P_TFLT] = -20, t->p[P_STRUM] = 15, t->p[P_VLEAD] = 1;
        for (i = 0; i < 8; i++)
            t->p[P_E0 + i] = (int16_t)(10 + i + k);
#else
        for (i = 0; i < (int)PROJ_NC6; i++)     /* P_LEVEL .. P_VLEAD */
            t->p[i] = (int16_t)(k * 7 + i % 40);
        t->p[P_TFLT] = -20, t->p[P_STRUM] = 15, t->p[P_VLEAD] = 1;
        for (i = 0; i < 8; i++)
            t->e[i] = (int16_t)(10 + i + k);
        for (i = 0; i < (int)PROJ_NX6; i++)     /* 2.6's ten: INSERT, FTYPE, ESYNC, LFO 2 (LOST but FTYPE) */
            t->x[i] = (uint8_t)(1 + i + k);
#endif
        for (i = 0; i < NSTEP; i++) {
            uint8_t *s = (uint8_t *)&t->step[i];
            int j;
            for (j = 0; j < 10; j++)
                s[j] = (uint8_t)(i * 3 + j + k);
        }
        for (i = 0; i < NLOCK; i++)
            lock(t, i, LOCK_FREE, 0, 0);
#if SL_VER == 25
        t->micro[3] = (int8_t)(-5 - k), t->micro[10] = MICRO_MAX;
        t->fill[0] = (uint8_t)(FC_FILL << 2 | FC_NOFILL << 4);
#else
        t->mf[3] = (uint8_t)((uint32_t)(-5 - k) & 63u), t->mf[10] = (uint8_t)(MICRO_MAX & 63);
        t->mf[1] = (uint8_t)(FC_FILL << 6), t->mf[2] = (uint8_t)(FC_NOFILL << 6), t->mf[12] = (uint8_t)(3u << 6 | 2u);
#endif
        lock(t, 0, 2, P_PAN, -30 + k);           /* kept */
        lock(t, 1, 5, P_E0 + 1, 7);              /* EDIT 2: ours P_E1 (dropped on an engine not ours) */
        lock(t, 2, 6, P_LEVEL, 100);
#if SL_VER == 26
        lock(t, 3, 7, P_ITYPE, 4);               /* 2.6's own: LOST */
        lock(t, 4, 8, P_LSYNC, 2);
#endif
    }
#if SL_VER == 25
    q.t[0].engine = ENGI_PHYS, q.t[0].preset = 4;   /* PHYS, THUMB PNO */
    q.t[0].p[P_E0] = 2;                              /* MODEL SYMP */
    q.t[1].engine = ENGI_NOISE, q.t[1].preset = 2;   /* NOISE: not ours */
    q.t[2].engine = 6, q.t[2].preset = 2;            /* TRIO */
    q.t[3].p[P_E0] = 43;                             /* the drum track: SYN2 (DRUM_SYN + 1) */
#else
    q.t[0].engine = 0, q.t[0].preset = 3;            /* ANALOG, filter TYPE HP */
    q.t[0].x[P_FTYPE - P_ITYPE] = 2;
    q.t[1].engine = ENGI_PHYS, q.t[1].preset = 1;    /* PHYS */
    q.t[2].engine = 6, q.t[2].preset = 2;            /* TRIO: its TYPE (BP) is not heard, LOST */
    q.t[2].x[P_FTYPE - P_ITYPE] = 1;
    q.t[3].e[0] = 7;                                 /* the drum track: kit 7 (606) */
    q.rsv[0] = 0x80u | 3u | 1u << 4, q.rsv[1] = 9;  /* GLO > TEXTURE (project.c proj_tx_put): LOST */
    q.g[G_DRCH] = (int16_t)(100 | (64 + 5) << 7), q.g[G_INFO] = 40;
#endif
    q.sum = fnv(&q, sizeof q - 4u);
    n = settings(set);
#if SL_VER == 25
    return put(dir, "sl25_fun5.bin", &q, sizeof q) || put(dir, "sl25_settings.bin", set, n);
#else
    return put(dir, "sl26_fun6.bin", &q, sizeof q) || put(dir, "sl26_settings.bin", set, n);
#endif
}
