/* SPDX-License-Identifier: GPL-3.0-only */
/* Reads what tests/sl24_export_test.c exported (sl24_export.bin, sl24_persist.bin) with SLOOP 2.4's OWN types, built
 * against isod89/sloop-fm1 v2.4 (8d3823f) firmware/src/core.h and arranger.h, its project_t / proj_trk_t, persist_t
 * and panel_t taken from its project.c and panel.c as they are; checks them as 2.4 loads and restores them (proj_ok:
 * magic, size, FNV-1a sum; settings_restore: PER3, palette, low cut, zoom, panel_valid, arr_valid). Not run by
 * run_tests.sh (it needs their tree); to run it (the export test's files in D):
 *
 *   R=<sloop-fm1 v2.4 checkout>/firmware/src; S=<a scratch dir>
 *   sed -n '/^typedef struct {  *\/\* one track; the drum/,/^} project_t;/p' $R/project.c > $S/sl24_project_t.h
 *   sed -n '/^enum { B_FX/,/^} panel_t;/p' $R/panel.c > $S/sl24_panel_t.h
 *   sed -n '/^typedef struct {$/,/^} persist_t;/p' $R/project.c | sed -n '/uint32_t magic, palette/,$p' > $S/p.h
 *   (echo 'typedef struct {'; cat $S/p.h) > $S/sl24_persist_t.h
 *   cc '-D__attribute__(x)=' -I$R -I$S -o $S/check24 tests/sl24_export_check24.c && $S/check24 D
 */
#include <stdio.h>
#include <string.h>
#define FELUCCA_ARRANGER 1
#include "core.h"                       /* SLOOP 2.4's: P_*, G_*, step_t, plock_t, NLOCK, NTRK, NSTEP, MICRO_* */
#include "arranger.h"                   /* its arr_config_t, arr_valid */
#include "sl24_project_t.h"             /* its proj_trk_t, project_t (project.c) */
#include "sl24_panel_t.h"               /* its panel_t (panel.c) */
#include "sl24_persist_t.h"             /* its persist_t (project.c) */
_Static_assert(sizeof(project_t) == 3840u && sizeof(persist_t) == 88u, "SLOOP 2.4's FUN5 and settings");

static int bad;
static void check(const char *what, int ok)
{
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t fnv(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t s = 0x811C9DC5u, i;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static int panel_valid(const panel_t *q)           /* its project.c panel_valid, as it is */
{
    uint32_t i, b = 0, e = 0;
    if (q->magic != PANEL_MAGIC)
        return 0;
    for (i = 0; i < NB; i++) {
        if (q->btn[i] >= 14u || (b >> q->btn[i]) & 1u)
            return 0;
        b |= 1u << q->btn[i];
    }
    for (i = 0; i < NE; i++) {
        if (q->enc[i] >= 7u || (e >> q->enc[i]) & 1u || (q->dir[i] != 1 && q->dir[i] != -1))
            return 0;
        e |= 1u << q->enc[i];
    }
    return 1;
}

int main(int argc, char **argv)
{
    static project_t q;
    static persist_t s;
    char p[512];
    FILE *f;
    uint32_t k, i, ok;
    if (argc < 2)
        return 2;
    snprintf(p, sizeof p, "%s/sl24_export.bin", argv[1]);
    if (!(f = fopen(p, "rb")) || fread(&q, 1, sizeof q, f) != sizeof q)
        return 1;
    fclose(f);
    snprintf(p, sizeof p, "%s/sl24_persist.bin", argv[1]);
    if (!(f = fopen(p, "rb")) || fread(&s, 1, sizeof s, f) != sizeof s)
        return 1;
    fclose(f);
    check("2.4's proj_ok: FUN5, 3840, its FNV sum", q.magic == 0x46554E35u && q.size == sizeof q &&
                                                       q.sum == fnv(&q, sizeof q - 4u));
    check("globals: BPM 123, MIDI / SYNC / ROUTE 0 (settings of the FM-1 there), DRREV 16, track 2 selected",
          q.g[G_BPM] == 123 && !q.g[G_MIDI] && !q.g[G_SYNC] && !q.g[G_ROUTE] && q.g[G_DRREV] == 16 && q.sel == 1);
    for (k = 0, ok = 1; k < NTRK; k++) {
        for (i = 0; i <= P_CHORD; i++)
            ok &= q.t[k].p[i] == (int16_t)(k * 11u + i * 3u) - 20;
        ok &= !q.t[k].p[P_TFLT] && !q.t[k].p[P_STRUM] && !q.t[k].p[P_VLEAD] && q.t[k].micro[5] == -7 - (int)k;
        ok &= ((q.t[k].fill[1] >> 4) & 3u) == 1u;       /* (step 6: FILL) */
        for (i = 0; i < NLOCK; i++)
            ok &= q.t[k].lock[i].step == LOCK_FREE || (q.t[k].lock[i].step < NSTEP && q.t[k].lock[i].param < P_COUNT);
    }
    check("tracks: P_LEVEL .. P_CHORD, TFLT STRUM VLEAD 0, nudge, fill, every lock on one of its parameters", ok);
    ok = q.t[0].engine == 0 && q.t[0].p[P_E0 + 2] == 22 && q.t[1].engine == 9 && q.t[1].p[P_E7] == 5 &&
         !q.t[1].p[P_E0] && q.t[2].engine == 2 && q.t[3].p[P_E0] == 5;
    check("ANALOG E2 22; FM6 PTCH F6 (WOOD BARS), ALG PAT; CZ -> PHASE; the drum kit 808", ok);
    ok = 0;
    for (i = 0; i < NLOCK; i++)
        ok += (q.t[0].lock[i].step == 2 && q.t[0].lock[i].param == P_PAN && q.t[0].lock[i].val == -10) +
              (q.t[0].lock[i].step == 4 && q.t[0].lock[i].param == P_E0 + 2 && q.t[0].lock[i].val == 9) +
              (q.t[3].lock[i].step == 9 && q.t[3].lock[i].param == P_E0 && q.t[3].lock[i].val == 5);
    check("locks in 2.4's ids: PAN on step 3, ANALOG's EDIT 3 (its P_E0 + 2), the kit lock 808", ok == 3);
    check("2.4's settings_restore: PER3, palette < 5, low cut / zoom 0 / 1, panel_valid, arr_valid",
          s.magic == 0x50455233u && s.palette == 2 && s.lowcut == 1 && !s.zoom && panel_valid(&s.panel) &&
          arr_valid(&s.arrangement, 15u));
    check("... its lights word: KEYS C (1), NOTES lit, SYNC USB, MIDI OUT SEQ", (s.lights & 0xF0u) == 0x10u &&
          (s.lights >> 8 & 1u) && (s.lights >> 12 & 3u) == 1u && (s.lights >> 14 & 1u));
    printf("sl24 export check (2.4's types) %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
