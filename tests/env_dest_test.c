/* SPDX-License-Identifier: GPL-3.0-only */
/* ENV DEST's and ENV2 DEST's two slots (core.h P_N1DA .., P_N2DA ..; voice.c env_plan / env_pre / env_post, env_store.h),
 * the shared destination list (LT_*). Run by tests/run_tests.sh with LFO2=1 and LFO2=0:
 *   default   a new track, INIT, a preset: ENV A on AMP at 127, nothing else; the depths of before -> slots
 *   amp       nothing is wired: ENV off AMP plays at full while ENV runs (its release too), then ends; ENV2 alone on AMP
 *             shapes the voice and keeps it alive until ENV2 has died away; ENV on AMP at -128 upside down
 *   plain     ENV A AMP 127 + B FLT renders as the depth of before (ENV DEST FLT d = AMT 2 d), sample for sample, on
 *             the plain path and on env_post (another slot that does nothing forcing it)
 *   values    an envelope on an engine's value (E5) and on ENV's DEC moves it for the voice as it renders, the authored
 *             value back after each render; on PAN (the part's): moved for the block from the newest voice
 *   mod2      ENV2 on ENV's amounts (E1 AMT), LFO on ENV2's (E2 AMT), the envelopes on each other's times: 3000 blocks,
 *             every value in range, the authored ones untouched (no runaway)
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static int32_t o_[CTL];
static void init(uint32_t e, uint32_t pi)
{
    host_tracks_init();
    memset(trk[0].v, 0, sizeof trk[0].v);              /* (no voice of a run before: the same start each time) */
    vage = 0, trk[0].lfo_ph = 0, trk[0].lfo_val = 0;
    rng_state = 0x1234567u;
    host_preset(&trk[0], e, pi);
    trk[0].pold = 0;
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
}
/* n blocks of part 0 between pold_in and pold_out (as the mix does) -> the sum of |samples| */
static int64_t run(uint32_t n, uint64_t *h)
{
    int64_t s = 0;
    uint32_t b, i;
    for (b = 0; b < n; b++) {
        uint32_t m = pold_any() ? pold_in() : 0u;
        track_render(&trk[0], o_, CTL);
        if (m)
            pold_out(m);
        for (i = 0; i < CTL; i++) {
            s += o_[i] < 0 ? -o_[i] : o_[i];
            if (h)
                *h = (*h ^ (uint32_t)o_[i]) * 1099511628211ull;
        }
    }
    return s;
}
static uint32_t active(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += trk[0].v[i].active;
    return n;
}

static void t_default(void)
{
    uint32_t k, ok = 1;
    for (k = 0; k < 5u; k++) {
        init(k % 2u, k);
        ok &= trk[0].p[P_N1DA] == LT_AMP && trk[0].p[P_N1AA] == 127 && !trk[0].p[P_ED_FLT] && !trk[0].p[P_ED_SHP];
    }
    ok &= TP[P_N1DA].def == LT_AMP && TP[P_N1AA].def == 127 && !TP[P_N1DB].def && !TP[P_N1AB].def;
    check(ok, "default: ENV A on AMP at 127 (presets, the table's defaults); ENV DEST's words 0 at run time");
    init(0, 4);                                        /* ACID 303: ENV DEST FLT 52 */
    ok = trk[0].p[P_N1DB] == LT_FLT && trk[0].p[P_N1AB] == 104;
#if FELUCCA_ANALOG2
    init(0, 22);                                       /* LP24 BASS: ENV2 FLT 44 */
    ok &= trk[0].p[P_N2DA] == LT_FLT && trk[0].p[P_N2AA] == 88 && trk[0].p[P_N2DB] == LT_OFF;
#endif
    check(ok, "default: a preset's ENV DEST FLT 52 -> slot B FLT 104; ENV2 FLT 44 -> ENV2 A FLT 88");
}

static void t_amp(void)
{
    int64_t a, b;
    uint32_t n;
    init(0, 2);                                        /* SUB BASS: SUS 118 */
    trk[0].p[P_REL] = 30;
    trk[0].p[P_N1DA] = LT_FLT, trk[0].p[P_N1AA] = 0;  /* (ENV on nothing: the voice at full) */
    trk_note_on(&trk[0], 48, 127);
    a = run(200, 0);
    init(0, 2);
    trk[0].p[P_REL] = 30;
    trk_note_on(&trk[0], 48, 127);
    b = run(200, 0);
    check(a > b, "amp: ENV on no AMP: the voice louder than through ENV (SUS 118: full)");
    init(0, 2);
    trk[0].p[P_REL] = 30, trk[0].p[P_N1DA] = LT_OFF;
    trk_note_on(&trk[0], 48, 127);
    run(50, 0);
    trk_note_off(&trk[0], 48);
    for (n = 0; n < 4000u && active(); n++)
        run(1, 0);
    check(n < 4000u && n > 10u, "amp: ENV off AMP: the voice lives through ENV's release, then ends");
#if FELUCCA_ANALOG2
    init(0, 2);
    trk[0].p[P_REL] = 0;                               /* (ENV ends at once on the note-off) */
    trk[0].p[P_N1DA] = LT_OFF;
    trk[0].p[P_N2DA] = LT_AMP, trk[0].p[P_N2AA] = 127;
    trk[0].p[P_A2FATK] = 0, trk[0].p[P_A2FDEC] = 60, trk[0].p[P_A2ESUS] = 127, trk[0].p[P_A2EREL] = 50;
    trk_note_on(&trk[0], 48, 127);
    run(50, 0);
    trk_note_off(&trk[0], 48);
    run(20, 0);
    a = run(20, 0);
    check(active() && a > 0, "amp: ENV2 alone on AMP: still sounding in ENV2's release after ENV ended");
    for (n = 0; n < 20000u && active(); n++)
        run(1, 0);
    check(n < 20000u, "amp: ... and the voice ends once ENV2 has died away");
#endif
    init(0, 2);
    trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127;
    trk_note_on(&trk[0], 48, 127);
    run(100, 0);
    b = run(20, 0);
    init(0, 2);
    trk[0].p[P_N1AA] = -128;                           /* (upside down: silent at ENV's top) */
    trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127;
    trk_note_on(&trk[0], 48, 127);
    run(100, 0);
    a = run(20, 0);
    check(a * 50 < b, "amp: ENV on AMP at -128: about silent while ENV is at its top (under 1/50 of AMT 127's)");
}

static void t_plain(void)
{
    uint64_t h1 = 1469598103934665603ull, h2 = h1;
    init(0, 4);                                        /* ACID 303: AMP 127 + FLT 104 (the plain path) */
    trk_note_on(&trk[0], 40, 100);
    run(300, &h1);
    init(0, 4);
#if FELUCCA_ANALOG2
    trk[0].p[P_N2DA] = LT_SDTN, trk[0].p[P_N2AA] = 1;  /* (ENV2 on SDTN 1, a swarm of none: nothing heard, the slow path) */
#else
    trk[0].p[P_N1DA] = LT_AMP, trk[0].p[P_N1AA] = 127; /* (no ENV2: the plain path twice) */
#endif
    trk_note_on(&trk[0], 40, 100);
    run(300, &h2);
    check(h1 == h2, "plain: the plain path and env_post render ENV on AMP + FLT the same, sample for sample");
}

static void t_values(void)
{
    int16_t e5, dec;
    uint64_t h1 = 1469598103934665603ull, h2 = h1;
    init(0, 4);
    e5 = trk[0].p[P_E5], dec = trk[0].p[P_DEC];
    trk[0].p[P_N1DB] = LT_E5, trk[0].p[P_N1AB] = 127;  /* ENV on RES (E6 is ANALOG's RES: E5 is its CUT) */
    trk_note_on(&trk[0], 40, 100);
    run(100, &h1);
    check(trk[0].p[P_E5] == e5, "values: ENV on E5 (CUT): the authored value back after each render");
    init(0, 4);
    trk_note_on(&trk[0], 40, 100);
    run(100, &h2);
    check(h1 != h2, "values: ... and heard (another sound than without the slot)");
    init(0, 4);
    trk[0].p[P_N1DB] = LT_DEC, trk[0].p[P_N1AB] = -128;
    trk_note_on(&trk[0], 40, 100);
    run(200, 0);
    check(trk[0].p[P_DEC] == dec, "values: ENV on its own DEC: moved per voice, back after");
    init(0, 4);
    trk[0].p[P_N1DB] = LT_PAN, trk[0].p[P_N1AB] = 127;
    trk_note_on(&trk[0], 40, 100);
    run(5, 0);
    {
        uint32_t m = pold_in();
        int16_t pan = trk[0].p[P_PAN];
        pold_out(m);
        check(pan > 0 && trk[0].p[P_PAN] == 0, "values: ENV on PAN: the part's, moved for the block (the newest voice), back after");
    }
}

static void t_mod2(void)
{
    uint32_t b, i, ok = 1;
    int16_t keep[P_COUNT];
    init(0, 4);
    trk[0].p[P_N1DB] = LT_DEC2, trk[0].p[P_N1AB] = 127;
#if FELUCCA_ANALOG2
    trk[0].p[P_N2DA] = LT_N1AMT, trk[0].p[P_N2AA] = -128;
    trk[0].p[P_N2DB] = LT_ATK, trk[0].p[P_N2AB] = 127;
#endif
    trk[0].p[P_L1DA] = LT_N2AMT, trk[0].p[P_L1AA] = 127, trk[0].p[P_L1DB] = LT_SUS, trk[0].p[P_L1AB] = 100;
    trk[0].p[P_LRATE] = 120;
    memcpy(keep, trk[0].p, sizeof keep);
    for (b = 0; b < 3000u; b++) {
        uint32_t m;
        if (b % 97u == 0u)
            trk_note_on(&trk[0], 40 + b % 12u, 100);
        if (b % 97u == 60u)
            trk_note_off(&trk[0], 40 + (b - 60u) % 12u);
        m = pold_any() ? pold_in() : 0u;
        for (i = 0; i < P_E0; i++)
            ok &= trk[0].p[i] >= TP[i].min && trk[0].p[i] <= TP[i].max;
        track_render(&trk[0], o_, CTL);
        if (m)
            pold_out(m);
        for (i = 0; i < CTL; i++)
            ok &= o_[i] > -(1 << 24) && o_[i] < (1 << 24);
    }
    ok &= !memcmp(keep, trk[0].p, sizeof keep);
    check(ok, "mod of mod: the envelopes and an LFO on each other's amounts and times: in range, bounded, authored kept");
}

static void t_names(void)
{
    char v[16];
    const char *u;
    param_format(&TP[P_N1DA], LT_AMP, v, &u);
    check(!strcmp(v, "AMP") && TP[P_N1DA].max == LT_NB - 1 && TP[P_N1AA].min == -128 && TP[P_N1AA].max == 127,
          "names: ENV DEST's DEST the shared list (AMP), AMT -128..127");
    param_format(&TP[P_N1DA], LT_N2AMT, v, &u);
    check(!strcmp(v, "E2 AMT"), "names: E2 AMT");
}

int main(void)
{
    t_default();
    t_amp();
    t_plain();
    t_values();
    t_mod2();
    t_names();
    printf("env_dest_test (LFO2=%d): %d failed\n", FELUCCA_LFO2, fails);
    return fails;
}
