/* SPDX-License-Identifier: GPL-3.0-only */
/* The FX returns (2026-10-12; core.h G_CRET G_DRET G_RRET, fx.c fx_buses, fx_rec.c FXRET_TLV): each send effect's wet
 * level into the master, 0..127, 127 unity.
 *   audio    the buses alone (fx_buses: noise sent, the wet kept), every run in a child from a fresh state:
 *            at one half (64) the chorus' and the reverb's wet is unity's halved exactly (x >> 1), the delay's within 2;
 *            at 0 the bus' wet is silent and the bus is not run (its line all 0, the reverb off: RG_OFF, its indices
 *            still); turned to 0 in a tail: silent from that block, the line cleared, and turned up again with nothing
 *            sent: still silent (no old echo comes back); with the CPU guard (FELUCCA_CPU_GUARD) its fade at REV RET
 *            64 is unity's fade halved (within 1). Unity itself: the goldens (tests/regress.c) do not move.
 *   record   at unity nothing is kept (no record); other levels round trip; a song section (all 0) keeps the returns
 *            playing; a cut TLV, no record (a project from before): unity
 *   stores   PROJECT SAVE / LOAD keeps them; a project saved at unity has no FX record and loads unity
 * Run by tests/run_tests.sh. */
#include <sys/mman.h>
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"

static struct { int force; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/storage/sections/sections.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("fx_returns: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---- the buses alone */
#define NB 600u                                         /* blocks a run (0.44 s) */
enum { BC = 1, BD = 2, BR = 4 };                        /* the buses sent to */
typedef struct {
    int32_t l[NB * CTL], r[NB * CTL];
    uint8_t line_clear[NB];                             /* after the block: the bus' line all 0 (the one bus sent to) */
    uint8_t rg[NB];                                     /* the reverb's RG_* after the block */
    uint32_t idx[NB];                                   /* the bus' running index after the block (ROOM: line 0's) */
} run_t;
static run_t *R;                                        /* (shared with the children) */
typedef struct {
    int bus;                                            /* BC BD BR */
    int ret;                                            /* the bus' return from block 0 */
    uint32_t send_to;                                   /* blocks sent noise */
    uint32_t zero_at, up_at;                            /* the return to 0 from zero_at, back to ret from up_at (0: never) */
    uint32_t guard_at;                                  /* (FELUCCA_CPU_GUARD) the guard's REVERB level from here (0: never) */
    uint32_t mute_at, unmute_at;                        /* the return's MUTE (fx.c fx_rmute) from mute_at, off from unmute_at (0: never) */
} plan_t;
static int line_clear(int bus)
{
    uint32_t i;
    int32_t o = 0;
    if (bus == BC)
        for (i = 0; i < sizeof cho_buf / 2u; i++)
            o |= cho_buf[i];
    else if (bus == BD)
        for (i = 0; i < sizeof dly_buf / 2u; i++)
            o |= dly_buf[i];
    else
        for (i = 0; i < sizeof rev_line / 2u; i++)
            o |= rev_line[i];
    return o == 0;
}
static uint32_t bus_idx(int bus)
{
#if FELUCCA_REV_ROOM
    if (bus == BR)
        return fx.line_i[0];
#endif
    return bus == BC ? fx.cho_w : bus == BD ? fx.dly_w : 0u;
}
static void run(const plan_t *p)
{
    pid_t c = fork();
    if (c == 0) {
        static int32_t s[CTL], z[CTL], wl[CTL], wr[CTL];
        uint32_t b, i, rnd = 12345u;
        int16_t *g = &song.g[p->bus == BC ? G_CRET : p->bus == BD ? G_DRET : G_RRET];
        host_tracks_init();
        fx_rmute = 0;
        song.g[G_DFDBK] = 100, song.g[G_DTIME] = 3;     /* (a long delay tail, 1/32: echoes before the sends end) */
        *g = (int16_t)p->ret;
        for (b = 0; b < NB; b++) {
            if (p->zero_at && b == p->zero_at)
                *g = 0;
            if (p->up_at && b == p->up_at)
                *g = (int16_t)p->ret;
            if (p->mute_at && b == p->mute_at)
                fx_rmute = (uint8_t)(1u << (p->bus == BC ? 0u : p->bus == BD ? 1u : 2u));
            if (p->unmute_at && b == p->unmute_at)
                fx_rmute = 0;
#if FELUCCA_CPU_GUARD
            if (p->guard_at && b == p->guard_at)
                cg.level = CG_L_REVERB;
#endif
            for (i = 0; i < CTL; i++) {
                rnd = rnd * 1664525u + 1013904223u;
                s[i] = b < p->send_to ? (int32_t)(rnd >> 16) - 32768 : 0;
                z[i] = 0;
            }
            if (!fx_buses(p->bus == BC ? s : z, p->bus == BD ? s : z, p->bus == BR ? s : z, wl, wr, CTL))
                memset(wl, 0, sizeof wl), memset(wr, 0, sizeof wr);
            memcpy(R->l + b * CTL, wl, sizeof wl);
            memcpy(R->r + b * CTL, wr, sizeof wr);
            R->line_clear[b] = (uint8_t)line_clear(p->bus);
            R->rg[b] = rgd.st;
            R->idx[b] = bus_idx(p->bus);
        }
        _exit(0);
    }
    waitpid(c, 0, 0);
}
static run_t *keep(void)                                /* a copy of the last run */
{
    run_t *k = malloc(sizeof *k);
    memcpy(k, R, sizeof *k);
    return k;
}
static int silent_from(const run_t *x, uint32_t b0)
{
    uint32_t i;
    for (i = b0 * CTL; i < NB * CTL; i++)
        if (x->l[i] || x->r[i])
            return 0;
    return 1;
}
static int heard(const run_t *x, uint32_t b0, uint32_t b1)
{
    uint32_t i;
    for (i = b0 * CTL; i < b1 * CTL; i++)
        if (x->l[i] || x->r[i])
            return 1;
    return 0;
}
/* the largest |half - (unity >> 1)|; exact: 0 */
static int32_t half_err(const run_t *u, const run_t *h)
{
    uint32_t i;
    int32_t e = 0, d;
    for (i = 0; i < NB * CTL; i++) {
        d = h->l[i] - (u->l[i] >> 1);
        e = d < 0 ? -d > e ? -d : e : d > e ? d : e;
        d = h->r[i] - (u->r[i] >> 1);
        e = d < 0 ? -d > e ? -d : e : d > e ? d : e;
    }
    return e;
}

static void t_audio(void)
{
    static const char *const NM[5] = {"", "CHO", "DLY", "", "REV"};
    static const int BUS[3] = {BC, BD, BR};
    uint32_t k, b;
    for (k = 0; k < 3u; k++) {
        int bus = BUS[k];
        char w[160];
        run_t *u, *h;
        plan_t p = {bus, 127, 200, 0, 0, 0};
        run(&p);
        u = keep();
        snprintf(w, sizeof w, "%s RET at unity: the bus heard (the test hears it)", NM[bus]);
        check(w, heard(u, 0, NB));
        p.ret = 64;
        run(&p);
        h = keep();
        if (bus == BD) {
            snprintf(w, sizeof w, "DLY RET at one half: its wet unity's halved, within 2 (its MIX x RET: dly_block)");
            check(w, half_err(u, h) <= 2);
        } else {
            snprintf(w, sizeof w, "%s RET at one half: its wet unity's halved exactly (x >> 1), sample for sample", NM[bus]);
            check(w, half_err(u, h) == 0);
        }
        p.ret = 0;
        run(&p);
        {
            int ok = silent_from(R, 0) && R->line_clear[NB - 1u];
            for (b = 1; b < NB; b++)
                ok &= bus != BR || (R->rg[b] != RG_ON && R->rg[b] != RG_FADE && R->idx[b] == R->idx[0]);
            ok &= bus != BR || R->rg[NB - 1u] == RG_OFF;
            snprintf(w, sizeof w, "%s RET at 0, noise sent: silent, its line all 0 (the bus not run%s)", NM[bus],
                     bus == BR ? ": the tank off, its index still" : "");
            check(w, ok);
        }
        p.ret = 127, p.zero_at = 260, p.up_at = 0;      /* (sends end at 200: in the tail) */
        run(&p);
        {
            int ok = heard(R, 200, 260) && silent_from(R, 260);
            for (b = 260; b < NB && !R->line_clear[b]; b++)
                ;
            snprintf(w, sizeof w, "%s RET turned to 0 in the tail: silent from that block, the line clear %u blocks on, then still",
                     NM[bus], b - 260u);
            ok &= b < NB && R->line_clear[NB - 1u];
            for (; bus != BR && b + 1u < NB; b++)
                ok &= R->idx[b + 1u] == R->idx[b] + CTL;   /* (the indices move on: the bus' clock) */
            check(w, ok);
        }
        p.up_at = 400;                                  /* back up, nothing sent */
        run(&p);
        snprintf(w, sizeof w, "%s RET back up with nothing sent: still silent (no old tail comes back)", NM[bus]);
        check(w, silent_from(R, 260));
        p.zero_at = 0, p.up_at = 0;
        {   /* the return's MUTE (fx_rmute): silent into the master, the bus runs on */
            plan_t m = {bus, 127, 300, 0, 0, 0, 100, 200};
            int ok;
            run(&m);
            ok = heard(R, 0, 100) && !heard(R, 100, 200) && heard(R, 200, 300);
            snprintf(w, sizeof w, "%s MUTE from block 100 to 200, noise sent throughout: heard, silent, heard again", NM[bus]);
            check(w, ok);
            if (bus != BC) {
                plan_t t = {bus, 127, 150, 0, 0, 0, 100, 160};
                run(&t);
                snprintf(w, sizeof w, "%s MUTE over the end of the sends, off in the tail: the tail was running meanwhile and is heard (a RET of 0 would have cleared it)", NM[bus]);
                check(w, !heard(R, 100, 160) && heard(R, 160, 260));
            }
            m.ret = 64, m.mute_at = 1, m.unmute_at = 0;
            run(&m);
            snprintf(w, sizeof w, "%s MUTE with RET at one half: silent", NM[bus]);
            check(w, silent_from(R, 1));
        }
        free(u);
        free(h);
    }
#if FELUCCA_CPU_GUARD
    {   /* the guard's fade and REV RET together */
        plan_t p = {BR, 127, 300, 0, 0, 200};
        run_t *u, *h;
        int ok;
        run(&p);
        u = keep();
        p.ret = 64;
        run(&p);
        h = keep();
        ok = half_err(u, h) <= 1 && heard(u, 200, 200 + REV_GFADE) && silent_from(u, 200 + REV_GFADE) && silent_from(h, 200 + REV_GFADE);
        check("the CPU guard's REVERB level at REV RET 64: its fade unity's halved (within 1), then off", ok);
        free(u);
        free(h);
    }
#endif
}

/* ---- the record */
static void rets(int c, int d, int r)
{
    song.g[G_CRET] = (int16_t)c, song.g[G_DRET] = (int16_t)d, song.g[G_RRET] = (int16_t)r;
}
static int rets_are(int c, int d, int r)
{
    return song.g[G_CRET] == c && song.g[G_DRET] == d && song.g[G_RRET] == r;
}
static void t_record(void)
{
    uint8_t b[FXR_MAX];
    uint32_t n;
    host_tracks_init();
    fxs_set(FXS_DEF);
    dlm_none();
    check("power-on: every return at unity (127, GP's default)", rets_are(127, 127, 127) && GP[G_RRET].def == 127);
    check("at unity: nothing to keep (no record)", fxr_encode(b) == 0u);
    rets(10, 64, 0);
    n = fxr_encode(b);
    check("CHO 10 DLY 64 REV 0: a record with their TLV (0x41, CHO DLY REV)",
          n == FXR_HEAD + 6u && b[FXR_HEAD] == 0x41u && b[FXR_HEAD + 1u] == 4u && b[FXR_HEAD + 2u] == 10u &&
          b[FXR_HEAD + 3u] == 64u && b[FXR_HEAD + 4u] == 0u && b[FXR_HEAD + 5u] == 0u);
    rets(127, 127, 127);
    check("... decoded back (a load)", fxr_decode(b, n, 1) && rets_are(10, 64, 0));
    rets(1, 2, 3);
    check("... a song section (all 0): the returns playing kept", fxr_decode(b, n, 0) && rets_are(1, 2, 3));
    b[FXR_HEAD + 1u] = 2u;
    check("... a TLV too short (a damaged one): unity", fxr_decode(b, n - 2u, 1) && rets_are(127, 127, 127));
    rets(5, 5, 5);
    check("no record (a project from before): unity", !fxr_decode(0, 0, 1) && rets_are(127, 127, 127));
    rets(127, 127, 127);
    fx_rmute = 5;
    n = fxr_encode(b);
    check("a return muted at unity: a record (0x41, 4 bytes, the mute bits last)", n == FXR_HEAD + 6u && b[FXR_HEAD] == 0x41u && b[FXR_HEAD + 1u] == 4u && b[FXR_HEAD + 5u] == 5u);
    fx_rmute = 0;
    check("... decoded back (a load): lines CHO and REV muted", fxr_decode(b, n, 1) && fx_rmute == 5 && rets_are(127, 127, 127));
    fx_rmute = 2;
    check("... a song section (all 0): the mute playing kept", fxr_decode(b, n, 0) && fx_rmute == 2);
    fx_rmute = 7;
    check("no record: every return unmuted", !fxr_decode(0, 0, 1) && fx_rmute == 0);
    rets(10, 64, 0);
    n = fxr_encode(b);
    b[FXR_HEAD + 1u] = 3u;
    fx_rmute = 7;
    check("an older 3 byte TLV (a project from before the mute): the levels, unmuted", fxr_decode(b, n - 1u, 1) && rets_are(10, 64, 0) && fx_rmute == 0);
    rets(127, 127, 127);
}

/* ---- the stores */
static void t_stores(void)
{
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    song.playing = 0, transport_req = 0;
    host_tracks_init();
    fxs_set(FXS_DEF);
    rets(20, 127, 90);
    project_save(3);
    check("SAVE D with CHO 20 REV 90: saved, its FX record written", !strcmp(last_msg, "SAVED") && slg_has(FXR_ID0 + 3));
    rets(127, 127, 127);
    project_load(3);
    check("... LOAD D: CHO 20 DLY 127 REV 90", rets_are(20, 127, 90));
    rets(127, 127, 127);
    project_save(4);
    check("SAVE E at unity: no FX record", !strcmp(last_msg, "SAVED") && !slg_has(FXR_ID0 + 4));
    rets(0, 0, 0);
    project_load(4);
    check("... LOAD E: unity", rets_are(127, 127, 127));
    slg_boot();
    rets(127, 127, 127);
    project_load(3);
    check("after a restart (the log scanned again): D loads its returns", rets_are(20, 127, 90));
    rets(127, 127, 127);
}


/* ---- long runs (2026-10-12, the user's FM-1: "once after some time the reverb stops producing any sound"): ten
 * minutes of blocks with the reverb sent all along, REV RET to 0 and back (and to a half) now and then, and in a
 * FELUCCA_CPU_GUARD build the guard's REVERB level held for 2 s now and then (as cpuguard.c would): whenever the return
 * is up and the level off, the reverb must be heard again within 1 s, and never silent longer */
static void t_longrun(void)
{
    pid_t c = fork();
    if (c == 0) {
        static int32_t s[CTL], z[CTL], wl[CTL], wr[CTL];
        const uint32_t BPS = FS / CTL, NBL = 600u * BPS;   /* blocks a second, ten minutes */
        uint32_t b, i, rnd = 99u, gate = 0, quiet = 0, longest = 0, toggles = 0, guards = 0, fails = 0;
        uint32_t ret_until = 0, guard_until = 0;
        host_tracks_init();
        song.g[G_RRET] = 127;
        for (b = 0; b < NBL; b++) {
            int on;
            if (b % (17u * BPS) == 5u * BPS && b) {       /* every 17 s: REV RET 0 for 1 s (or a half for 1 s) */
                song.g[G_RRET] = (int16_t)(toggles & 1u ? 64 : 0);
                ret_until = b + BPS;
                toggles++;
            }
            if (ret_until && b == ret_until) {
                song.g[G_RRET] = 127;
                ret_until = 0;
            }
#if FELUCCA_CPU_GUARD
            if (b % (23u * BPS) == 11u * BPS && b) {      /* every 23 s: the guard's REVERB level for 2 s */
                cg.level = CG_L_REVERB;
                guard_until = b + 2u * BPS;
                guards++;
            }
            if (guard_until && b == guard_until) {
                cg.level = CG_L_OFF;
                guard_until = 0;
            }
#endif
            for (i = 0; i < CTL; i++) {
                rnd = rnd * 1664525u + 1013904223u;
                s[i] = ((int32_t)(rnd >> 16) - 32768) >> 2;
                z[i] = 0;
            }
            if (!fx_buses(z, z, s, wl, wr, CTL))
                memset(wl, 0, sizeof wl), memset(wr, 0, sizeof wr);
            on = 0;
            for (i = 0; i < CTL; i++)
                on |= wl[i] | wr[i];
            if (song.g[G_RRET] && !guard_until && !ret_until) {
                gate++;
                quiet = on ? 0 : quiet + 1;
                longest = quiet > longest ? quiet : longest;
                if (quiet == BPS) {
                    printf("fx_returns: the reverb silent 1 s at block %u (REV RET %d, rg %u)\n", b, song.g[G_RRET],
                           rgd.st);
                    fails++;
                }
            } else {
                quiet = 0;
            }
        }
        printf("fx_returns: long run: %u blocks, %u REV RET changes, %u guard levels, the longest silence with the "
               "return up and the guard off %u blocks\n", NBL, toggles, guards, longest);
        fflush(stdout);
        _exit(fails || !gate || toggles < 30u ? 1 : 0);
    }
    {
        int st = 0;
        waitpid(c, &st, 0);
        check("long run: the reverb heard again within 1 s after every REV RET 0 -> up and guard level, never silent "
              "longer", WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }
}

int main(void)
{
    R = mmap(0, sizeof *R, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (R == MAP_FAILED)
        return 2;
    t_audio();
    t_record();
    t_stores();
    t_longrun();
    printf(bad ? "fx_returns: %d FAILED\n" : "fx_returns: all ok\n", bad);
    return bad != 0;
}
