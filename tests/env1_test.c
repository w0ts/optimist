/* SPDX-License-Identifier: GPL-3.0-only */
/* ENV 1 MOD (core.h P_E1PIT .., env1_cards.h; voice.c env_plan / env_pre / env_post) and ENV2 DEST's two slots (P_N2DA
 * ..). ENV 1 is the engine's own envelope: the amplitude always wired, four depth knobs made per engine. Run by
 * tests/run_tests.sh with LFO2=1 and LFO2=0 (every engine built):
 *   cards     every engine: four cards, each a path (FLT SHP PIT), a move of an EDIT value (P_E1X ..) or an alias of one;
 *             the ids distinct, a filter card where the engine has a filter path, ENV2's default DEST A per engine
 *   default   a new sound, INIT, every preset: ENV 1's PIT and moves at 0, its FLT SHP the table's (ACID 303: FLT 52),
 *             ENV2's DEST A the engine's default at 0
 *   render    every engine, every card: at 0 the render is the default's, bit for bit; at a depth it is heard (a path the
 *             engine reads, a move); a move's value back after each render; an alias moves nothing of its own
 *   amp       ENV 1 is the amplitude: the voice ends after its release whatever the depths; ENV2 on AMP on top of it
 *   plain     ENV 1's FLT on the plain path and on env_post (another slot that does nothing forcing it): the same, sample
 *             for sample; PIT +-63: about +-31.5 semitones
 *   e1amt     E1 AMT: an LFO on it scales the depths for the block (back after), ENV2 on it per voice; bounded
 *   fold      ENV DEST's slots of 2026-10-14 .. 16 (env_store.h env1_fold): AMP dropped (127: nothing lost), FLT SHP PIT
 *             folded at AMT / 2, the rest to a free ENV2 (ATK .. REL copied) or lost
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <unistd.h>
#include <sys/wait.h>

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
static int64_t brun(uint32_t n, uint64_t *h)
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
#define H0 1469598103934665603ull

static void t_cards(void)
{
    uint32_t e, k, ok = 1, okf = 1, oke = 1;
    for (e = 0; e < NENGINES; e++) {
        uint32_t ids[4], nflt = 0, nmv = 0;
        for (k = 0; k < 4u; k++) {
            uint32_t c = env1_card(e, k), j;
            ids[k] = env1_id(e, k);
            ok &= ids[k] < P_COUNT;
            for (j = 0; j < k; j++)
                ok &= ids[j] != ids[k];
            nflt += E1K_KIND(c) == E1K_FLT;
            nmv += E1K_KIND(c) == 0x40u;
            if (E1K_KIND(c) == 0x40u || E1K_KIND(c) == 0x50u)
                ok &= ENGINES[e]->edit[E1K_ARG(c)].max > ENGINES[e]->edit[E1K_ARG(c)].min;   /* (a real value) */
        }
        ok &= nmv <= E1_NMV;
        okf &= nflt == 1u || ENG_IS(ENGINES[e], DRAWBAR) || ENG_IS(ENGINES[e], FM6) || ENG_IS(ENGINES[e], ACID) ||
               ENG_IS(ENGINES[e], CZ) || ENG_IS(ENGINES[e], PHASE);
        oke &= env2_def_dest(e) == (ENG_IS(ENGINES[e], DRAWBAR) ? LT_E4 : ENG_IS(ENGINES[e], FM6) ? LT_SHP :
                                    ENG_IS(ENGINES[e], CZ) ? LT_E2 : LT_FLT);
    }
    check(ok, "cards: every engine four cards, distinct ids, at most three moves, each move / alias a real EDIT value");
    check(okf, "cards: a FLT card on every engine with a filter path (not WHEEL FM6 ACID CZ PHASE)");
    check(oke, "cards: ENV2's default DEST A the filter, WHEEL TOP, FM6 SHP (MOD), CZ DCW (ACID: FLT, its CUT)");
}

static void t_default(void)
{
    uint32_t e, k, ok = 1, ok2 = 1;
    for (e = 0; e < NENGINES; e++)
        for (k = 0; k < ENGINES[e]->npresets; k++) {
            init(e, k);
            ok &= !trk[0].p[P_E1PIT] && !trk[0].p[P_E1X] && !trk[0].p[P_E1Y] && !trk[0].p[P_E1Z];
#if FELUCCA_ANALOG2
            ok2 &= trk[0].p[P_N2DA] == (int16_t)env2_def_dest(e) || trk[0].p[P_N2AA] != 0;
            ok2 &= trk[0].p[P_N2AA] != 0 || trk[0].p[P_N2DB] == LT_OFF;
#endif
        }
    check(ok, "default: every preset: ENV 1's PIT and moves at 0");
    check(ok2, "default: ENV2's DEST A the engine's default (at 0) unless the preset uses ENV2");
    init(0, 4);                                        /* ACID 303: ENV DEST FLT 52 */
    ok = trk[0].p[P_ED_FLT] == 52;
#if FELUCCA_ANALOG2
    init(0, 22);                                       /* LP24 BASS: ENV2 FLT 44 */
    ok &= trk[0].p[P_N2DA] == LT_FLT && trk[0].p[P_N2AA] == 88 && trk[0].p[P_N2DB] == LT_OFF;
#endif
    check(ok, "default: a preset's ENV FLT 52 stays its FLT depth; ENV2 FLT 44 -> ENV2 A FLT 88");
}

/* engine e, card k at depth d: the render of 120 blocks of a held note -> its hash; *same: the values after = before.
 * In a child process: every engine's running state (its part memory, a rotor, a chorus) as the parent has it, the
 * same start for each render */
static int32_t wave_ = -1;
static uint8_t xid_[2];                                /* (card_run: two more values set, id 0 = none) */
static int16_t xv_[2];                             /* (card_run: EDIT 1 set to it, -1 the preset's) */
static uint64_t card_run(uint32_t e, uint32_t pi, uint32_t k, int32_t d, uint32_t *same)
{
    uint64_t r[2] = {0, 0};
    int fd[2];
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (!pid) {
        uint64_t h = H0;
        int16_t keep[P_COUNT];
        uint32_t id = env1_id(e, k), i;
        init(e, pi);
        if (wave_ >= 0)
            trk[0].p[P_E0] = (int16_t)wave_;
        for (i = 0; i < 2u; i++)
            if (xid_[i])
                trk[0].p[xid_[i]] = xv_[i];
        if (d && id < P_COUNT && E1K_KIND(env1_card(e, k)) != 0x50u)
            trk[0].p[id] = (int16_t)d;
        trk[0].p[P_SUS] = 100, trk[0].p[P_DEC] = 60;
        memcpy(keep, trk[0].p, sizeof keep);
        trk_note_on(&trk[0], 52, 100);
        brun(120, &h);
        r[0] = h, r[1] = !memcmp(keep, trk[0].p, sizeof keep);
        if (write(fd[1], r, sizeof r) != (ssize_t)sizeof r)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (pid < 0 || read(fd[0], r, sizeof r) != (ssize_t)sizeof r)
        r[0] = 0, r[1] = 0;
    close(fd[0]);
    if (pid > 0)
        waitpid(pid, 0, 0);
    *same = (uint32_t)r[1];
    return r[0];
}
static void t_render(void)
{
    uint32_t e, k, ok0 = 1, okh = 1, okb = 1, same;
    char bad[400] = "";
    for (e = 0; e < NENGINES; e++) {
        uint64_t h0 = card_run(e, 0, 0, 0, &same);
        if (ENG_IS(ENGINES[e], SLICE) && !SLC_BREAK.len) {
            printf("     (SLICE: no BREAK in the host headers, silent: its cards not rendered)\n");
            continue;
        }
        uint32_t pi;
        for (k = 0; k < 4u; k++) {
            uint32_t c = env1_card(e, k), kind = E1K_KIND(c);
            uint64_t h;
            if (kind == 0x50u)
                continue;                               /* (an alias: the EDIT value itself, checked by t_cards) */
            h = card_run(e, 0, k, 0, &same);
            if (h != h0)
                printf("     (at 0: %s card %u differs)\n", ENGINES[e]->name, k + 1u);
            ok0 &= h == h0;
            for (pi = 0; pi < ENGINES[e]->npresets; pi++) {   /* (heard on one of its presets: a PW on a saw is not) */
                uint64_t g0 = pi ? card_run(e, pi, k, 0, &same) : h0;
                if (card_run(e, pi, k, 50, &same) != g0 || card_run(e, pi, k, -50, &same) != g0)
                    break;
                okb &= same;
            }
            for (wave_ = 0; pi == ENGINES[e]->npresets && wave_ <= ENGINES[e]->edit[0].max; wave_++) {   /* (or on one of
                                                         * its EDIT 1 values: TRIO's PW on a pulse set) */
                uint64_t g0 = card_run(e, 0, k, 0, &same);
                if (card_run(e, 0, k, 50, &same) != g0 || card_run(e, 0, k, -50, &same) != g0)
                    pi = 0;
            }
            wave_ = -1;
            if (pi == ENGINES[e]->npresets) {
                okh = 0;
                if (strlen(bad) < 360u)
                    sprintf(bad + strlen(bad), " %s/%u", ENGINES[e]->name, k + 1u);
            }
            okb &= same;
        }
    }
    check(ok0, "render: every engine, every card at 0: the default's render, bit for bit");
    printf("     (not heard:%s)\n", bad[0] ? bad : " none");
    check(okh, "render: every path and move card heard at +-50 on every engine");
    check(okb, "render: the values after the render the authored ones (a move written back after each voice)");
}

/* ACID's own filter on the shared FLT destination (eng_acid.c: CUT), ENV2's default DEST A there (2026-10-16) */
static void t_acid(void)
{
#if FELUCCA_ENG_ACID
    uint32_t same, e = ENG_IX_ACID;
    uint64_t h0 = card_run(e, 0, 0, 0, &same), h1, h2;
    xid_[0] = P_L1DA, xv_[0] = LT_FLT, xid_[1] = P_L1AA, xv_[1] = 100;
    h1 = card_run(e, 0, 0, 0, &same);
    xid_[0] = P_L1DA, xv_[0] = LT_RES;
    h2 = card_run(e, 0, 0, 0, &same);
    check(h1 != h0 && h2 != h0 && h1 != h2, "acid: an LFO on FLT moves the 303's CUT, on RES its RESO");
#if FELUCCA_ANALOG2
    xid_[0] = P_N2AA, xv_[0] = 120, xid_[1] = P_A2FDEC, xv_[1] = 40;
    h1 = card_run(e, 0, 0, 0, &same);
    check(h1 != h0, "acid: ENV2 on its default DEST A (FLT: the 303's CUT) at 120: heard");
#endif
    xid_[0] = xid_[1] = 0;
#endif
}

static void t_amp(void)
{
    uint32_t n;
    int64_t a, b;
    init(0, 2);                                        /* SUB BASS */
    trk[0].p[P_REL] = 30, trk[0].p[P_E1PIT] = 20, trk[0].p[P_E1X] = -40, trk[0].p[P_ED_FLT] = 63;
    trk_note_on(&trk[0], 48, 127);
    brun(50, 0);
    trk_note_off(&trk[0], 48);
    for (n = 0; n < 4000u && active(); n++)
        brun(1, 0);
    check(n < 4000u && n > 10u, "amp: ENV 1 is the amplitude: the voice ends after its release (with PIT, a move, FLT on)");
#if FELUCCA_ANALOG2
    init(0, 2);
    trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127;
    trk_note_on(&trk[0], 48, 127);
    brun(100, 0);
    b = brun(20, 0);
    init(0, 2);
    trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127;
    trk[0].p[P_N2DA] = LT_AMP, trk[0].p[P_N2AA] = -128;  /* (ENV2 upside down on AMP: silent at its top) */
    trk[0].p[P_A2FATK] = 0, trk[0].p[P_A2FDEC] = 60, trk[0].p[P_A2ESUS] = 127;
    trk_note_on(&trk[0], 48, 127);
    brun(100, 0);
    a = brun(20, 0);
    check(a * 50 < b, "amp: ENV2 on AMP at -128 on top of ENV 1: about silent while ENV2 is at its top");
#else
    (void)a, (void)b;
#endif
}

static void t_plain(void)
{
    uint64_t h1 = H0, h2 = H0;
    init(0, 4);                                        /* ACID 303: FLT 52 (the plain path) */
    trk_note_on(&trk[0], 40, 100);
    brun(300, &h1);
    init(0, 4);
#if FELUCCA_ANALOG2
    trk[0].p[P_N2DA] = LT_SDTN, trk[0].p[P_N2AA] = 1;  /* (ENV2 on SDTN 1, a swarm of none: nothing heard, the slow path) */
#else
    trk[0].p[P_E1Z] = 5;                               /* (ANALOG has no third move: nothing heard, the slow path) */
#endif
    trk_note_on(&trk[0], 40, 100);
    brun(300, &h2);
    check(h1 == h2, "plain: the plain path and env_post render ENV 1's FLT the same, sample for sample");
    {   /* PIT 63 at ENV's top: the pitch +31.5 st (env_post: whole 1/16 semitones, as the PIT slots) */
        env_plan_t pl;
        env_vo_t vo;
        vmod_t m;
        lfo_mod_t lm = {{0, 0, 0, 0, 0}, -1, -1};
        uint32_t nw = 0;
        init(0, 2);
        trk[0].p[P_E1PIT] = 63;
        env_plan(&trk[0], &pl);
        memset(&vo, 0, sizeof vo);
        trk_note_on(&trk[0], 48, 100);
        env_post(&trk[0], &trk[0].v[0], &pl, &vo, 32767, &nw, &m, &lm);
        check(pl.slow && vo.pit >= 503 && vo.pit <= 505 && vo.amp == 32767, "plain: PIT 63: +31.5 st at ENV's top, the amplitude ENV itself");
        trk[0].p[P_E1PIT] = -64;
        env_plan(&trk[0], &pl);
        env_post(&trk[0], &trk[0].v[0], &pl, &vo, 32767, &nw, &m, &lm);
        check(vo.pit <= -511 && vo.pit >= -513, "plain: PIT -64: -32 st");
    }
}

static void t_e1amt(void)
{
    uint32_t b, i, ok = 1, okp = 1;
    int16_t keep[P_COUNT];
    init(0, 4);
    trk[0].p[P_E1PIT] = 30, trk[0].p[P_E1X] = -40;
    trk[0].p[P_L1DA] = LT_N1AMT, trk[0].p[P_L1AA] = 127, trk[0].p[P_LRATE] = 120;
#if FELUCCA_ANALOG2
    trk[0].p[P_N2DA] = LT_N1AMT, trk[0].p[P_N2AA] = -128;
    trk[0].p[P_N2DB] = LT_ATK, trk[0].p[P_N2AB] = 127;
#endif
    memcpy(keep, trk[0].p, sizeof keep);
    for (b = 0; b < 2000u; b++) {
        uint32_t m;
        if (b % 97u == 0u)
            trk_note_on(&trk[0], 40 + b % 12u, 100);
        if (b % 97u == 60u)
            trk_note_off(&trk[0], 40 + (b - 60u) % 12u);
        m = pold_any() ? pold_in() : 0u;
        if (b == 20u)                                   /* (the LFO at its top: the depths scaled up for the block) */
            okp &= m && trk[0].p[P_ED_FLT] != keep[P_ED_FLT];
        for (i = 0; i < P_COUNT; i++)
            if (i != P_VOICES && (trk[0].p[i] < track_desc(&trk[0], i)->min || trk[0].p[i] > track_desc(&trk[0], i)->max)) {
                if (ok)
                    printf("     (block %u: value %u at %d)\n", b, i, trk[0].p[i]);
                ok = 0;
            }
        track_render(&trk[0], o_, CTL);
        if (m)
            pold_out(m);
        for (i = 0; i < CTL; i++)
            ok &= o_[i] > -(1 << 24) && o_[i] < (1 << 24);
    }
    for (i = 0; i < P_COUNT; i++)
        if (keep[i] != trk[0].p[i]) {
            printf("     (value %u: %d, authored %d)\n", i, trk[0].p[i], keep[i]);
            ok = 0;
        }
    check(okp, "e1amt: an LFO on E1 AMT moves ENV 1's depths for the block");
    check(ok, "e1amt: LFO and ENV2 on E1 AMT, ENV2 on ATK: every value in range, bounded, the authored ones back");
}

/* ENV2's slots on a value as ENV DEST's were (env_dest_test of 2026-10-14): per voice (E5, DEC), the part's (PAN) */
static void t_env2(void)
{
#if FELUCCA_ANALOG2
    int16_t e5, dec;
    uint64_t h1 = H0, h2 = H0;
    init(0, 4);
    e5 = trk[0].p[P_E5], dec = trk[0].p[P_DEC];
    trk[0].p[P_A2FATK] = 0, trk[0].p[P_A2FDEC] = 60, trk[0].p[P_A2ESUS] = 100;
    trk[0].p[P_N2DB] = LT_E5, trk[0].p[P_N2AB] = 127;  /* ENV2 on E5 (ANALOG's CUT) */
    trk_note_on(&trk[0], 40, 100);
    brun(100, &h1);
    check(trk[0].p[P_E5] == e5, "env2: ENV2 on E5 (CUT): the authored value back after each render");
    init(0, 4);
    trk[0].p[P_A2FATK] = 0, trk[0].p[P_A2FDEC] = 60, trk[0].p[P_A2ESUS] = 100;
    trk_note_on(&trk[0], 40, 100);
    brun(100, &h2);
    check(h1 != h2, "env2: ... and heard");
    init(0, 4);
    trk[0].p[P_N2DB] = LT_DEC, trk[0].p[P_N2AB] = -128;
    trk_note_on(&trk[0], 40, 100);
    brun(200, 0);
    check(trk[0].p[P_DEC] == dec, "env2: ENV2 on ENV's DEC: moved per voice, back after");
    init(0, 4);
    trk[0].p[P_A2FATK] = 0, trk[0].p[P_A2ESUS] = 127;
    trk[0].p[P_N2DB] = LT_PAN, trk[0].p[P_N2AB] = 127;
    trk_note_on(&trk[0], 40, 100);
    brun(5, 0);
    {
        uint32_t m = pold_in();
        int16_t pan = trk[0].p[P_PAN];
        pold_out(m);
        check(pan > 0 && trk[0].p[P_PAN] == 0, "env2: ENV2 on PAN: the part's, moved for the block (the newest voice), back after");
    }
#endif
}

/* v: part 0's values (its preset), ENV DEST's slots s in P_E1PIT .. P_E1Z as a file of 2026-10-14 .. 16 stored them */
static int16_t fv[P_COUNT];
static uint32_t fold4(int16_t d0, int16_t a0, int16_t d1, int16_t a1)
{
    fv[P_E1PIT] = d0, fv[P_E1X] = a0, fv[P_E1Y] = d1, fv[P_E1Z] = a1;
    return env1_fold(fv);
}
static void t_fold(void)
{
    uint32_t lost, ok;
    init(0, 0);
    memcpy(fv, trk[0].p, sizeof fv);
    lost = fold4(LT_AMP, 127, LT_FLT, 104);
    check(!lost && fv[P_ED_FLT] == 52 && !fv[P_E1PIT] && !fv[P_E1X] && !fv[P_E1Y] && !fv[P_E1Z],
          "fold: AMP 127 + FLT 104 -> FLT 52, ENV 1's PIT and moves 0 (every preset of 2026-10-14 .. 16)");
    memcpy(fv, trk[0].p, sizeof fv);
    lost = fold4(LT_SHP, -60, LT_PIT, 41);
    check(lost == 0 && fv[P_ED_SHP] == -30 && fv[P_E1PIT] == 20, "fold: SHP -60 -> SHP -30, PIT 41 -> PIT 20 (odd: an LSB)");
    memcpy(fv, trk[0].p, sizeof fv);
    lost = fold4(LT_AMP, 90, LT_OFF, 0);
    check(lost == 1, "fold: AMP 90 lost (ENV 1 is the amplitude itself)");
    memcpy(fv, trk[0].p, sizeof fv);
    fv[P_ATK] = 11, fv[P_DEC] = 22, fv[P_SUS] = 33, fv[P_REL] = 44;
    lost = fold4(LT_RES, 70, LT_E3, -20);
#if FELUCCA_ANALOG2
    ok = !lost && fv[P_N2DA] == LT_RES && fv[P_N2AA] == 70 && fv[P_N2DB] == LT_E3 && fv[P_N2AB] == -20;
    ok &= fv[P_A2FATK] == 11 && fv[P_A2FDEC] == 22 && fv[P_A2ESUS] == 33 && fv[P_A2EREL] == 44;
    check(ok, "fold: RES 70 + E3 -20 -> ENV2's slots (free: DEST A FLT at 0), ENV's ATK DEC SUS REL copied into ENV2's");
    memcpy(fv, trk[0].p, sizeof fv);
    fv[P_N2DB] = LT_PIT, fv[P_N2AB] = 10;              /* (ENV2 busy) */
    lost = fold4(LT_PAN, 70, LT_E3, -20);
    check(lost == 2 && fv[P_N2DB] == LT_PIT && fv[P_N2AB] == 10, "fold: ENV2 busy: both lost, ENV2 as it was");
    memcpy(fv, trk[0].p, sizeof fv);
    lost = fold4(LT_FLT, 30, LT_PAN, 50);
    ok = !lost && fv[P_ED_FLT] == 15 && fv[P_N2DA] == LT_PAN && fv[P_N2AA] == 50 && fv[P_N2DB] == LT_OFF && !fv[P_N2AB];
    check(ok, "fold: FLT 30 + PAN 50 -> FLT 15, PAN to ENV2's slot A");
#else
    check(lost == 2, "fold: no ENV2 in this build: RES, E3 lost");
    (void)ok;
#endif
    memcpy(fv, trk[0].p, sizeof fv);
    fv[P_ED_FLT] = 60;
    lost = fold4(LT_FLT, 40, 99, 5);                   /* (a DEST past the list: nothing) */
    check(!lost && fv[P_ED_FLT] == 63, "fold: onto a depth already there: added, clamped; a DEST past the list ignored");
}

int main(void)
{
    t_cards();
    t_default();
    t_render();
    t_acid();
    t_amp();
    t_plain();
    t_e1amt();
    t_env2();
    t_fold();
    printf("env1_test: %d failed\n", fails);
    return fails;
}
