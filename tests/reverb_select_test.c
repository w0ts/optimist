/* SPDX-License-Identifier: GPL-3.0-only */
/* The reverb's algorithms picked on the device (firmware/src/rev_type.c, fx.c rev_bus): built by tests/run_tests.sh
 * with several sets of FELUCCA_REV_ROOM / _PLATE / _FDN8 / _AIRWIN / FELUCCA_SPRING (and REV_HALF):
 *   list     TYPE lists the algorithms built, in order (ROOM, PLATE, FDN8, VTINY, SPRING), one name each; the REVERB
 *            page and its cell only with two or more (one built: no page, no cell), its value an index into them
 *   switch   every ordered pair of built algorithms, the old one ringing with a send going: the old tank fades out
 *            over REV_FADE blocks (bounded, its steps no larger than before, ~0 at the end), the shared line and
 *            every tank's state are cleared at the switch, the new one starts from silence, rings, then (the send
 *            off) rings out to exactly 0 and the bus goes idle (the idle skip's conditions all hold); TYPE turned
 *            while the bus is idle switches at once
 *   project  TYPE saved in project_t.rsv[0] (bit 0 SPRING, bit 1 PLATE, bit 2 FDN8, both AIRWIN, none ROOM) and
 *            loaded back; older projects (0 ROOM, 1 SPRING) load as they were; one asking for an algorithm not built
 *            plays the first one built, MISSING names it ("REVERB FDN8"), a save keeps it, until TYPE is turned
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i)
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/project.c"
#define MISS_SCAN_ONLY 1
#include "../firmware/src/miss.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("reverb select: %-80s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static const char *const LONG[RT_N] = {"ROOM", "SPRING", "PLATE", "FDN8", "VTINY"};

static uint32_t seed = 12345;
static int32_t noise(void)
{
    seed = seed * 1664525u + 1013904223u;
    return ((int32_t)(seed >> 16) - 32768) / 2;
}
/* one block of the buses, the reverb's send = noise (send) or 0; out: the wet, l and r */
static int bus(int send, int32_t *wl, int32_t *wr)
{
    static const int32_t z[CTL];
    int32_t rin[CTL];
    uint32_t i;
    int r;
    for (i = 0; i < CTL; i++)
        rin[i] = send ? noise() : 0;
    r = fx_buses(z, z, rin, wl, wr, CTL);
    if (!r)                                            /* (all idle: the wet was not written, it is 0) */
        for (i = 0; i < CTL; i++)
            wl[i] = wr[i] = 0;
    return r;
}
static int line_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_line / 2u; i++)
        if (rev_line[i])
            return 0;
    return 1;
}
/* every condition fx_buses skips the bus on (its lines written 0 for their longest, the filters at 0) */
static int bus_idle(void)
{
    return fx.rev_q >= REV_Q && !REV_LP_BUSY() && !REV_HALF_BUSY()
#if FELUCCA_SPRING
           && sp.q >= SP_LEN && !(sp.lp | sp.hp)
#endif
        ;
}
#if REV_MULTI
static void select_type(uint32_t c) { bp_set[BPS_RTYPE] = (int16_t)rev_index(c); }
#define TYPE_SET(i) (bp_set[BPS_RTYPE] = (int16_t)(i))
#else
static void select_type(uint32_t c) { (void)c; }   /* (one built: no TYPE) */
#define TYPE_SET(i) ((void)(i))
#endif
/* rings out with no send: blocks until the wet was exactly 0 for 1 s and the bus idle, or -1 */
static int32_t ring_out(uint32_t max_blocks)
{
    int32_t wl[CTL], wr[CTL];
    uint32_t b, i, quiet = 0;
    for (b = 0; b < max_blocks; b++) {
        int32_t any = 0;
        bus(0, wl, wr);
        for (i = 0; i < CTL; i++)
            any |= wl[i] | wr[i];
        quiet = any ? 0u : quiet + 1u;
        if (quiet * CTL >= FS && bus_idle())
            return (int32_t)b;
    }
    return -1;
}

static void t_list(void)
{
    uint32_t i, p, found = 0, ok = 1;
    for (i = 0; i < REV_NLIST; i++)
        ok &= i == 0 || REV_ALGO[i] > REV_ALGO[i - 1] || REV_ALGO[i] == RT_SPRING;
    check(REV_NLIST == REV_NALGO && ok && REV_ALGO[0] == REV_FIRST,
          "TYPE: the algorithms built, ROOM PLATE FDN8 SPRING order, the first one REV_FIRST");
    for (p = 0; p < NPAGES; p++)
        if (!strcmp(PAGES[p].title, "REVERB")) {
#if REV_MULTI
            int16_t *vp;
            const param_desc_t *d = page_desc(&PAGES[p], 0, &vp);
            found = 1;
            ok = d && vp == &bp_set[BPS_RTYPE] && d->fmt == F_ENUM && d->min == 0 && d->max == (int16_t)REV_NLIST - 1;
            for (i = 0; ok && i < REV_NLIST; i++)
                ok = !strcmp(d->names[i], REV_ALGO[i] == RT_SPRING ? "SPRNG" : LONG[REV_ALGO[i]]);
            check(ok && PAGES[p].scope == SC_BPSET && PAGES[p].id[0] == BPS_RTYPE,
                  "TYPE's cell: an enum of the algorithms built, by name");
#else
            found = 1;
#endif
        }
    check(found == REV_MULTI, REV_MULTI ? "the REVERB page (FX family): there, two or more built"
                                         : "one algorithm built: no REVERB page, no TYPE cell");
}

#if REV_MULTI
/* an ordered pair: from a (ringing) to b */
static void t_pair(uint32_t a, uint32_t b)
{
    int32_t wl[CTL], wr[CTL];
    char what[120];
    uint32_t k, i, n = 0;
    int32_t peak = 0, step = 0, fpeak = 0, fstep = 0, end = 0, start = 0, prev = 0, ok_clear = 0, faded = 0;
    int32_t idle;
    select_type(a);
    ring_out(40u * FS / CTL);                         /* (a running and silent) */
    for (k = 0; k < 2u * FS / CTL; k++) {             /* 2 s of a with a send: its own level and steps */
        bus(1, wl, wr);
        for (i = 0; i < CTL; i++) {
            int32_t x = wl[i] > 0 ? wl[i] : -wl[i], d = wl[i] - prev;
            peak = x > peak ? x : peak;
            if (k > FS / CTL)
                step = (d > 0 ? d : -d) > step ? (d > 0 ? d : -d) : step;
            prev = wl[i];
        }
    }
    select_type(b);                                    /* TYPE turned, the send going on */
    for (k = 0; k < REV_FADE + 2u; k++) {
        uint32_t before = rsel.type;
        bus(1, wl, wr);
        for (i = 0; i < CTL; i++) {
            int32_t x = wl[i] > 0 ? wl[i] : -wl[i], d = wl[i] - prev;
            if (k < REV_FADE) {                         /* (the fade's blocks) */
                fpeak = x > fpeak ? x : fpeak;
                fstep = (d > 0 ? d : -d) > fstep ? (d > 0 ? d : -d) : fstep;
                if (k == REV_FADE - 1u && i >= CTL - 4u)
                    end = x > end ? x : end;
            } else if (k == REV_FADE && i < 4u) {
                start = x > start ? x : start;
            }
            prev = wl[i];
        }
        if (before == a && rsel.type == b && !faded) {
            faded = (int32_t)k + 1;
            ok_clear = line_clear() && fx.rev_q == FX_Q_MAX;   /* (cleared at the switch) */
        }
    }
    snprintf(what, sizeof what, "%s -> %s: fades over %u blocks, bounded, no step larger than before", LONG[a],
             LONG[b], REV_FADE);
    check(faded == (int32_t)REV_FADE && fpeak <= peak && fstep <= step + peak / 64 && peak > 0, what);
    snprintf(what, sizeof what, "%s -> %s: the fade ends at ~0 (%d of %d), the new tank starts from silence (%d)",
             LONG[a], LONG[b], end, peak, start);
    check(end <= peak / 50 + 2 && start <= peak / 50 + 2 && ok_clear, what);
    for (k = 0; k < FS / CTL; k++) {                   /* 1 s of b with the send */
        bus(1, wl, wr);
        for (i = 0; i < CTL; i++)
            n += wl[i] != 0;
    }
    snprintf(what, sizeof what, "%s -> %s: the new one plays", LONG[a], LONG[b]);
    check(rsel.type == b && n > FS / 2, what);
    idle = ring_out(60u * FS / CTL);
    snprintf(what, sizeof what, "%s -> %s: rings out to exactly 0, the bus idle (%.2f s)", LONG[a], LONG[b],
             idle < 0 ? -1.0 : idle * (double)CTL / FS - 1.0);
    check(idle >= 0, what);
    (void)n;
}

#endif

static void t_switch(void)
{
    uint32_t a, b;
    int32_t wl[CTL], wr[CTL];
    song.g[G_RSIZE] = 90;
    song.g[G_RDAMP] = 50;
    (void)a, (void)b;
#if REV_MULTI
    for (a = 0; a < REV_NLIST; a++)
        for (b = 0; b < REV_NLIST; b++)
            if (a != b)
                t_pair(REV_ALGO[a], REV_ALGO[b]);
    {                                                  /* idle: the change at once, nothing written */
        select_type(REV_ALGO[0]);
        ring_out(60u * FS / CTL);
        select_type(REV_ALGO[1]);
        bus(0, wl, wr);
        check(rsel.type == REV_ALGO[1] && !rsel.fade && line_clear() && bus_idle(),
              "TYPE turned on an idle bus: switched within the block, nothing to fade, still idle");
        select_type(REV_ALGO[0]);
        bus(0, wl, wr);
    }
#else
    {                                                  /* one built: it plays, rings out, idles */
        uint32_t k, i, n = 0;
        for (k = 0; k < FS / CTL; k++) {
            bus(1, wl, wr);
            for (i = 0; i < CTL; i++)
                n += wl[i] != 0;
        }
        check(n > FS / 2 && ring_out(60u * FS / CTL) >= 0, "one algorithm: it plays, rings out to exactly 0, the bus idle");
    }
#endif
}

static void t_project(void)
{
    static project_t pj;
    static dlrec_t dl0;
    static const uint8_t BYTE[RT_N] = {0, 1, 2, 4, 6};
    uint32_t c;
    char what[120];
    for (c = 0; c < RT_N; c++) {
        int32_t i = rev_index(c);
        rev_defaults();
        if (i >= 0) {                                  /* built: saved, loaded back */
            select_type(c);
            proj_capture(&pj, &dl0);
            rev_defaults();
            proj_apply(&pj, &dl0, 1);
            snprintf(what, sizeof what, "project: %s saved (rsv[0] %u) and loaded back", LONG[c], pj.rsv[0]);
            check(pj.rsv[0] == BYTE[c] && rev_sel() == (uint32_t)i && rev_orph == 0xFF, what);
            miss_scan();
            snprintf(what, sizeof what, "project: %s built: nothing missing", LONG[c]);
            check(miss_cnt == 0, what);
        } else {                                       /* not built: the first one, MISSING, kept by a save */
            char nm[32];
            proj_capture(&pj, &dl0);
            pj.rsv[0] = BYTE[c];
            proj_apply(&pj, &dl0, 1);
            miss_scan();
            *miss_name(nm, miss_m[0]) = 0;
            snprintf(what, sizeof what, "project asks for %s (not built): plays %s, MISSING: %s", LONG[c],
                     LONG[REV_ALGO[0]], miss_cnt ? nm : "-");
            check(rev_sel() == 0 && rev_cur() == REV_ALGO[0] && miss_cnt == 1 &&
                  !strcmp(nm, c == RT_ROOM ? "REVERB ROOM" : c == RT_SPRING ? "REVERB SPRING" :
                              c == RT_PLATE ? "REVERB PLATE" : c == RT_FDN8 ? "REVERB FDN8" : "REVERB VTINY"), what);
            proj_capture(&pj, &dl0);
            snprintf(what, sizeof what, "... a save keeps %s (rsv[0] %u)", LONG[c], pj.rsv[0]);
            check(pj.rsv[0] == BYTE[c], what);
            if (REV_NLIST > 1u) {
                TYPE_SET(1);                           /* TYPE turned: the user's choice now */
                miss_scan();
                TYPE_SET(0);
                proj_capture(&pj, &dl0);
                miss_scan();
                check(miss_cnt == 0 && pj.rsv[0] == BYTE[REV_ALGO[0]],
                      "... TYPE turned away and back: no longer missing, saved as playing");
            }
        }
    }
    proj_capture(&pj, &dl0);                           /* older projects: 0 ROOM, 1 SPRING */
    pj.rsv[0] = 0;
    proj_apply(&pj, &dl0, 1);
    check(rev_cur() == REV_ALGO[0] && (FELUCCA_REV_ROOM ? rev_orph == 0xFF : rev_orph == RT_ROOM),
          "an older project (rsv[0] 0): ROOM, as before (ROOM not built: the first, ROOM kept)");
    pj.rsv[0] = 1;
    proj_apply(&pj, &dl0, 1);
    check(FELUCCA_SPRING ? rev_cur() == RT_SPRING && rev_orph == 0xFF : rev_cur() == REV_ALGO[0] && rev_orph == RT_SPRING,
          "an older SPRING project (rsv[0] 1): SPRING (not built: the first, SPRING kept)");
    rev_defaults();
    check(rev_sel() == 0 && rev_orph == 0xFF, "NEW: the first algorithm built, nothing asked for");
}

int main(void)
{
    host_tracks_init();
    t_list();
    t_switch();
    t_project();
    printf("reverb select: %u algorithms (mask %u), REV_HALF %d: %d failed\n", (unsigned)REV_NLIST, (unsigned)REV_MASK,
           FELUCCA_REV_HALF, fails);
    return fails;
}
