/* SPDX-License-Identifier: GPL-3.0-only */
/* The reverb bus fuzzed (2026-10-29, the user on the FM-1: "the FDN8 reverb stops working randomly"): FDN8 in the pool
 * (REV_POOL) beside the ROOM (TYPE picks), the CPU guard built, driven a block at a time (fx_buses, as cpuguard_test.c)
 * through random episodes: every SIZE and DAMP value, full-scale noise, a full-scale square, DC at either rail, sends far
 * past int16 (four parts, the drums and the USB input summed: +-2^21), silence and bursts; mid-tail SIZE / DAMP turns,
 * TYPE switches, REV RET at 0 and back, the return's mute, the guard's REVERB level on and off.
 *   build/host/reverb_fuzz_test [SECONDS] [SEED]      (default 240 s of the bus and a quarter of it of the mix, seed 1;
 *   tests/run_tests.sh: the default; 32 seeds x 2 h ran on the farm, 2026-10-29: no failure)
 * Checks, every block:
 *   state    FDN8's gains 1 .. 32767 (a loop gain under unity: no runaway), its damping memories 0 .. 32767, the send's
 *            gain at most 13000, its filters and interpolating allpasses within int16 (they are held there), the ROOM's
 *            one-poles within int16;
 *   heard    a send of noise into a bus that is on (guard off, REV RET up, not muted, no TYPE fade under way) is heard
 *            within 0.25 s, every time: the reverb never goes silent while it is sent something;
 * and per episode, its send stopped: the tail rings out to exactly 0 (the bus idle) within its bound (SIZE <= 110:
 * 60 s), no ring cell left at a rail, the wet exactly 0 after. A failure prints the episode, the seed and the state. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "../firmware/src/system/cpuguard.c"

static int fails;
#define CHECK(c, ...)                                                                                   \
    do {                                                                                                \
        if (!(c)) {                                                                                     \
            if (fails < 20) {                                                                           \
                printf("reverb_fuzz: FAIL line %d (episode %u, seed %u): ", __LINE__, ep, seed0);       \
                printf(__VA_ARGS__);                                                                    \
                printf("\n");                                                                           \
            }                                                                                           \
            fails++;                                                                                    \
        }                                                                                               \
    } while (0)

static uint32_t seed0, ep, rs;
static uint32_t rnd(uint32_t n)
{
    rs = rs * 1664525u + 1013904223u;
    return (uint32_t)(((uint64_t)(rs >> 1) * n) >> 31);
}
#define BPS (FS / CTL)                                  /* blocks a second */

enum { IN_SILENT, IN_NOISE, IN_FULL, IN_SQUARE, IN_DCP, IN_DCN, IN_HUGE, IN_BURST, IN_N };
static int32_t in_blk[CTL], wl[CTL], wr[CTL];
static const int32_t zero[CTL];
static uint32_t ph;
static void make_in(uint32_t kind, uint32_t b)
{
    uint32_t i;
    for (i = 0; i < CTL; i++) {
        int32_t v = 0;
        ph = ph * 1103515245u + 12345u;
        switch (kind) {
        case IN_NOISE: v = (int32_t)(ph >> 18) - 8192; break;                /* -12 dBFS */
        case IN_FULL: v = (int32_t)(ph >> 16) - 32768; break;                 /* full scale */
        case IN_SQUARE: v = ((b * CTL + i) / 50u) & 1u ? 32767 : -32768; break;   /* 441 Hz, the rails */
        case IN_DCP: v = 32767; break;
        case IN_DCN: v = -32768; break;
        case IN_HUGE: v = (int32_t)(ph >> 10) - (1 << 21); break;             /* +-2^21: many sources summed */
        case IN_BURST: v = (b & 7u) == 0u ? (int32_t)(ph >> 16) - 32768 : 0; break;
        default: break;
        }
        in_blk[i] = v;
    }
}

static int bus_on(void)                                 /* the reverb runs and is heard (nothing turned it off) */
{
    return cg.level < CG_L_REVERB && song.g[G_RRET] && !(fx_rmute & 4u) && !rsel.fade && rgd.st == RG_ON;
}

static void check_state(void)
{
    uint32_t k;
    if (rsel.type == RT_FDN8 && rv.size >= 0) {
        for (k = 0; k < 8u; k++) {
            CHECK(rv.gain[k] >= 1 && rv.gain[k] < 32768, "FDN8 line %u: gain %d (SIZE %d)", k, rv.gain[k], rv.size);
            CHECK(rv.kl[k] <= 32767u, "FDN8 line %u: damping %u", k, rv.kl[k]);
            CHECK(rv.ap[k] >= -32768 && rv.ap[k] <= 32767, "FDN8 line %u: allpass %d", k, rv.ap[k]);
            CHECK(rv.lp[k] >= -65536 && rv.lp[k] <= 65536, "FDN8 line %u: low-pass %d", k, rv.lp[k]);
        }
        CHECK(rv.in > 0 && rv.in <= 13000, "FDN8: the send's gain %d", rv.in);
    }
#if FELUCCA_REV_ROOM
    for (k = 0; k < 4u; k++)
        CHECK(fx.line_lp[k] >= -65536 && fx.line_lp[k] <= 65536, "ROOM line %u: low-pass %d", k, fx.line_lp[k]);
#endif
}

static int32_t blk(uint32_t kind, uint32_t b)           /* one block -> its largest |wet| */
{
    uint32_t i;
    int32_t pk = 0;
    make_in(kind, b);
    if (!fx_buses(zero, zero, in_blk, wl, wr, CTL))
        memset(wl, 0, sizeof wl), memset(wr, 0, sizeof wr);
    for (i = 0; i < CTL; i++) {
        int32_t a = wl[i] < 0 ? -wl[i] : wl[i], c = wr[i] < 0 ? -wr[i] : wr[i];
        pk = a > pk ? a : pk;
        pk = c > pk ? c : pk;
    }
    check_state();
    return pk;
}
static int rev_idle(void) { return fx.rev_q >= REV_Q && !REV_LP_BUSY() && !REV_HALF_BUSY() && !rsel.fade; }
static uint32_t rails(void)                             /* ring cells at a rail */
{
    uint32_t i, n = 0;
    for (i = 0; i < REV_LINE_N; i++)
        n += rev_line[i] == 32767 || rev_line[i] == -32768;
    return n;
}

/* The whole mix (mix_block: the sequencer, the parts, the drums, their sends, the returns, the master), part 1 playing a
 * pattern with REV sent while the mixer is turned at random: the other tracks muted and soloed, part 1 soloed, MASTER,
 * SIZE / DAMP, TYPE, every part's and drum lane's REV, the drum track muted. Whenever part 1 sounds and nothing that is
 * meant to turn the reverb off is set (part 1 heard: not silent, FX on; REV RET up, the return not muted, the guard off),
 * the reverb's return (fx.c fx_ret_pk[2], what it adds to the wet) must be heard within 0.5 s */
static uint32_t mix_phase(uint32_t secs)
{
    static const uint8_t PAT[16] = {48, 0, 55, 0, 60, 0, 0, 63, 48, 0, 0, 58, 0, 60, 0, 0};
    track_t *t0 = &trk[0];
    uint32_t b, i, wait = 0, worst = 0, heard = 0;
    int32_t o[2 * CTL];
    host_tracks_init();
    memset(&cg, 0, sizeof cg);
    fx_rmute = 0;
    rev_sel_set((uint32_t)rev_index(RT_FDN8));
    host_preset(t0, 0, 4);
    host_preset(&trk[1], 1, 5);
    for (i = 0; i < 16u; i++) {
        uint8_t n = PAT[i], m = (uint8_t)(PAT[(i + 5u) & 15u] + 12u);
        put_step(t0, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, 0);
        put_step(&trk[1], i, PAT[(i + 5u) & 15u] ? 1u : 0u, &m, PAT[(i + 5u) & 15u] ? ST_NOTE : ST_REST, 0);
    }
    for (i = 0; i < 16u; i++) {
        uint8_t n = i % 4u == 0u ? 36 : 42;
        put_step(TDRUM, i, 1u, &n, ST_NOTE, 0);
    }
    t0->p[P_REV] = 100;
#ifdef FUZZ_MUTANT                                      /* (the check's own check: the return's input muted, unknown to
                                                         * bus_on, must fail) */
    fx_imute = 4u;
    fxs_lm_apply();
#endif
    transport_req = 1;
    for (b = 0; b < secs * BPS; b++) {
        int on = 0;
        if (b % (BPS / 2u) == 0u) {
            uint32_t op = rnd(12u);
            if (op == 0)
                trk[1 + rnd(2u)].p[P_MUTE] ^= 1;
            else if (op == 1)
                TDRUM->p[P_MUTE] ^= 1;
            else if (op == 2)
                song.solo = (uint8_t)(rnd(3u) ? 0u : rnd(2u) ? 1u : 1u << rnd(4u));   /* (none, part 1, or any) */
            else if (op == 3)
                song.g[G_RSIZE] = (int16_t)rnd(128u), song.g[G_RDAMP] = (int16_t)rnd(128u);
            else if (op == 4)
                rev_sel_set(rnd(3u) ? (uint32_t)rev_index(RT_FDN8) : 0u);
            else if (op == 5)
                song.master_q12 = (uint16_t)rnd(4097u);
            else if (op == 6)
                host_drum_rev((int32_t)rnd(128u));
            else if (op == 7)
                trk[rnd(NPART)].p[P_REV] = (int16_t)rnd(128u);
            else if (op == 8)
                t0->p[P_REV] = (int16_t)(20u + rnd(108u));
        }
        fx_ret_pk[2] = 0;
        mix_block(o, CTL);
        for (i = 0; i < NVOICE; i++)
            on |= t0->v[i].active;
        if (fx_ret_pk[2] > 0)
            heard++;
        if (trk_silent(t0) || !fx_on(t0) || !t0->p[P_REV] || !bus_on() || rev_want() != rsel.type || fx_ret_pk[2] > 0) {
            wait = 0;                                   /* (heard, or turned off: the count starts again) */
        } else if (on && ++wait > BPS / 2u) {           /* (the blocks part 1 sounded in, the return silent) */
            CHECK(0, "the mix: part 1 sounds with REV %d, the bus on, the return silent for %u blocks (SIZE %d type %u "
                  "solo %u)", t0->p[P_REV], wait, song.g[G_RSIZE], rsel.type, song.solo);
            wait = 0;
        }
        worst = wait > worst ? wait : worst;
    }
    CHECK(heard > secs * BPS / 4u, "the mix: the return heard at all (%u blocks of %u)", heard, secs * BPS);
    return worst;
}

int main(int argc, char **argv)
{
    uint32_t secs = argc > 1 ? (uint32_t)atoi(argv[1]) : 240u, total = 0, heard_waits = 0, worst_wait = 0, eps = 0;
    uint32_t idle_worst = 0;
    seed0 = argc > 2 ? (uint32_t)atoi(argv[2]) : 1u;
    rs = seed0 * 2654435761u + 1u;
    host_tracks_init();
    memset(&cg, 0, sizeof cg);
    fx_rmute = 0;
    rev_sel_set((uint32_t)rev_index(RT_FDN8));
    for (ep = 0; total < secs * BPS; ep++, eps++) {
        uint32_t nseg = 1u + rnd(6u), s, b;
        song.g[G_RSIZE] = (int16_t)rnd(128u);
        song.g[G_RDAMP] = (int16_t)rnd(128u);
        for (s = 0; s < nseg; s++) {
            uint32_t kind = rnd(IN_N), len = 1u + rnd(3u * BPS), wait = 0, ev = rnd(16u);
            int32_t pk;
            /* an event at the segment's start */
            if (ev == 0)
                song.g[G_RSIZE] = (int16_t)rnd(128u);
            else if (ev == 1)
                song.g[G_RDAMP] = (int16_t)rnd(128u);
            else if (ev == 2)
                rev_sel_set(rnd(4u) ? (uint32_t)rev_index(RT_FDN8) : 0u);   /* (mostly FDN8; now and then ROOM) */
            else if (ev == 3)
                song.g[G_RRET] = (int16_t)(rnd(3u) ? 127 : rnd(2u) ? 0 : 64);
            else if (ev == 4)
                fx_rmute = (uint8_t)(rnd(3u) ? 0u : 4u);
            else if (ev == 5)
                cg.level = (uint8_t)(rnd(3u) ? CG_L_OFF : CG_L_REVERB);
            for (b = 0; b < len; b++, total++) {
                if (ev == 6 && b == len / 2u)            /* (mid-segment: SIZE / DAMP turned in the tail) */
                    song.g[G_RSIZE] = (int16_t)rnd(128u), song.g[G_RDAMP] = (int16_t)rnd(128u);
                pk = blk(kind, total);
                if (kind == IN_NOISE || kind == IN_FULL || kind == IN_HUGE || kind == IN_SQUARE) {
                    if (!bus_on() || rev_want() != rsel.type || pk > 0) {
                        wait = 0;
                    } else if (++wait > BPS / 4u) {
                        CHECK(0, "sent %u (kind %u) for %u blocks, the bus on, the wet silent (SIZE %d DAMP %d type %u "
                              "rg %u q %u)", kind, kind, wait, song.g[G_RSIZE], song.g[G_RDAMP], rsel.type, rgd.st, fx.rev_q);
                        wait = 0;
                    }
                    worst_wait = wait > worst_wait ? wait : worst_wait;
                    heard_waits++;
                }
            }
        }
        /* the episode's end: the send stopped, the bus back on, SIZE <= 110 (a tail of a few seconds): idle within 60 s,
         * no cell at a rail, the wet exactly 0 */
        song.g[G_RSIZE] = (int16_t)rnd(111u);
        cg.level = CG_L_OFF;
        song.g[G_RRET] = 127;
        fx_rmute = 0;
        for (b = 0; b < 60u * BPS && !rev_idle(); b++, total++)
            (void)blk(IN_SILENT, total);
        idle_worst = b > idle_worst ? b : idle_worst;
        CHECK(rev_idle(), "the tail never rang out: 60 s after the send (SIZE %d DAMP %d type %u q %u)", song.g[G_RSIZE],
              song.g[G_RDAMP], rsel.type, fx.rev_q);
        CHECK(rails() == 0u, "%u ring cells at a rail once idle", rails());
        {
            int32_t pk = 0;
            for (b = 0; b < 4u; b++, total++) {
                int32_t p = blk(IN_SILENT, total);
                pk = p > pk ? p : pk;
            }
            CHECK(pk == 0, "idle, the wet not 0 (%d)", pk);
        }
    }
    {
        uint32_t mw = mix_phase(secs / 4u + 1u);
        printf("reverb_fuzz: the mix, %u s: the longest wait for the return %u blocks\n", secs / 4u + 1u, mw);
    }
    printf("reverb_fuzz: %u s of audio, %u episodes, seed %u: the longest wait for the wet %u blocks, the longest ring-out "
           "%.1f s; %s\n", total / BPS, eps, seed0, worst_wait, (double)idle_worst / BPS, fails ? "FAIL" : "ok");
    (void)heard_waits;
    return fails != 0;
}
