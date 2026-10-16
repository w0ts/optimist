/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' LFOs (drums/lane_play.c, core/lfo_slot.h, storage/dlfo_rec.c, sections/dlfo_rec_log.c; the user's
 * rulings of 2026-10-12: one LFO a lane, two DEST + AMT slots, off and free of cost while no slot acts, sampled at the
 * hit, restarted at PLAY):
 *   off        no LFO, an LFO whose slots have a DEST but AMT 0, or an AMT but DEST OFF: not computed (dlfo_on 0, the hit
 *              path untouched, no block counted); the render with such LFOs on every lane = the render without, sample
 *              for sample
 *   at the hit an LFO on DECAY: each hit hears patch + LFO x AMT, the LFO's value at that hit's block (a free RATE: the
 *              blocks since PLAY), the same as figured here; two slots on two values; both slots on one value add; a
 *              lane's lock and the LFO together (the lock replaces the patch, the LFO adds); the clamp
 *   sync       RATE 1BAR (synced): the bar's first hit hears the same value every bar for 8 bars, at 120 and 133 BPM
 *   restart    PLAY again: the first hits hear what they heard after the first PLAY; FADE: the first hits hear less
 *   stopped    a hit while stopped: the LFO moves on (blocks counted only then, only with a live LFO)
 *   the record its stored form: the lanes not at their defaults, 8 bytes each, read back equal; none: every LFO off; a
 *              newer version refused (every LFO off); a longer entry (a newer build's) read, a shorter one with the rest at
 *              the defaults; a bad length refused; out-of-range values brought back. Through a project buffer, a section
 *              (the log), the autosave; a section of an older firmware (no record): every LFO off
 *   2.4        the export says SX24_DLFO (lost2 bit 3; a v11 request: bit 13)
 *   cost       instructions per hit, and per block, on the host
 *   old        built with -DDLFO_ON=0: kept through the record, never heard, the render as without them, MISS: LANE LFO
 * Exit status: the number of failed checks. Run by tests/run_tests.sh (both builds). */
#include "lane_host.h"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("lfo: %-101s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

#define L 4u                                           /* CLOSED HAT */
static void reset(void)
{
    uint32_t i;
    seq_stop();
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        steps_clear(&trk[i]);
        trk[i].seq_abs = SEQ_NONE;
        trk[i].seq_idx = 0;
        trk[i].p[P_SLEN] = 8;
        AUTO_L(i)->n = 0;
    }
    auto_w.on = 0;
    auto_touch_all();
    memset(&drums, 0, sizeof drums);
    memset(&dl, 0, sizeof dl);
    (void)dlfo_decode(0, 0);
    dlfo_free = 0;
    drums_set = -2;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    song.rec = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    clk_beat = clk_pos = 0;
    song.playing = 0;
}
/* the hits of lane L: when (song.tick of their block), what they heard */
typedef struct { uint32_t tick, beat, pos; int8_t o[DE_N]; } hit_t;
static hit_t hits[4096];
static uint32_t nh;
static int16_t (*outbuf)[44100 * 2];
static uint32_t outn;
static void run_block_rec(int rec)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k, tick, beat = clk_beat, pos = clk_pos;
    mix_block(out, CTL);
    tick = song.tick - 1u;                             /* (the block's: events_block counts it at its end) */
    if (rec >= 0)
        for (k = 0; k < CTL * 2u && outn < 44100u * 2u; k++)
            outbuf[rec][outn++] = (int16_t)clamp(out[k], -32768, 32767);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && drums.v[k].note == LANE_NOTE_OF(L) && nh < 4096u) {
                const int8_t *o = dl_ofs_play(L);
                uint32_t v;
                hits[nh].tick = tick, hits[nh].beat = beat, hits[nh].pos = pos;
                for (v = 0; v < DE_N; v++)
                    hits[nh].o[v] = o ? o[v] : 0;
                nh++;
            }
}
static void run_block(void) { run_block_rec(-1); }
static void lane_on(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dstep_set(&TDRUM->dstep[i], L, LV_NORM, 0);
}
static void set_lfo(uint32_t l, int32_t rate, uint32_t wave, uint32_t phase, uint32_t fade, uint32_t d1, int32_t a1, uint32_t d2,
                    int32_t a2)
{
    dlfo_t c;
    c.rate = (int8_t)rate, c.wave = (uint8_t)wave, c.phase = (uint8_t)phase, c.fade = (uint8_t)fade;
    c.s[0].dest = (uint8_t)d1, c.s[0].amt = (int8_t)a1, c.s[1].dest = (uint8_t)d2, c.s[1].amt = (int8_t)a2;
    dlfo_set(l, &c);
}
/* the LFO of a free rate at block n, as the spec says (phase n x LFO_INC + PHS, no fade): Q15 */
static int32_t want_lfo(int32_t rate, uint32_t wave, uint32_t phase, uint32_t n)
{
    uint32_t ph = (uint32_t)((uint64_t)n * LFO_INC[rate] + ((uint64_t)phase << 25));
    int32_t v = wave == 2u ? (int32_t)(ph >> 16) - 32768 : wave == 3u ? (ph < 0x80000000u ? 32767 : -32767) : osc_sine(ph);
    return (v * 32767) >> 15;
}
static int32_t want_val(int32_t base, int32_t lfo, int32_t amt, uint32_t v)
{
    return clamp(base + ((lfo * amt * (DE_MAX[v] - DE_MIN[v])) >> 22), DE_MIN[v], DE_MAX[v]);
}

/* 1 s of 16 lanes, one each 16th, with an LFO on every lane (live: AMT -63, else AMT 0 with a DEST and AMT -63 with DEST
 * OFF) against none, each rendered in a child of this state: 1 the same, sample for sample */
static int render_same(int live)
{
    uint32_t k, l, first = 0xFFFFFFFFu;
    for (k = 0; k < 2u; k++) {
        pid_t pid = fork();
        if (!pid) {
            reset();
            for (l = 0; l < DRUM_LANES; l++)
                dstep_set(&TDRUM->dstep[l % 8u], l, LV_NORM, 0);
            if (!k)
                for (l = 0; l < DRUM_LANES; l++)
                    set_lfo(l, 100, 3, 64, 30, live ? DE_DECAY + 1u : l % 9u, live ? -63 : 0, 0, -63);
            outn = 0;
            transport_req = 1;
            while (outn < 44100u * 2u)
                run_block_rec((int)k);
            _exit(0);
        }
        waitpid(pid, 0, 0);
    }
    for (k = 0; k < 44100u * 2u && first == 0xFFFFFFFFu; k++)
        if (outbuf[0][k] != outbuf[1][k])
            first = k;
    return first == 0xFFFFFFFFu;
}
/* ---- off: nothing computed */
static void t_off(void)
{
    uint32_t l, i;
    reset();
    lane_on(8);
    for (l = 0; l < DRUM_LANES; l++)
        set_lfo(l, 30, 2, 0, 0, l % 9u, 0, 0, l & 1u ? 40 : -40);   /* (a DEST without AMT, an AMT without DEST) */
    check("LFOs with a DEST at AMT 0 or an AMT with DEST OFF: none computed (dlfo_on 0, dlfo_live 0)", !dlfo_on && !dlfo_any_live());
    for (i = 0; i < 100u; i++)
        run_block();
    check("... stopped: no block counted (no per-block work)", dlfo_free == 0u);
    nh = 0;
    transport_req = 1;
    while (clk_beat < 2u)
        run_block();
    check("... a hit: the hit path as before (no lane computed: dl_ofs, the patch)", nh >= 8u && dlp_l == 0xFFu && !hits[3].o[DE_DECAY]);
    seq_stop();
    check("... the render with them on every lane = the render without: sample for sample (1 s, 16 lanes)", render_same(0));
#if DLFO_ON
    check("(the same with live LFOs: differs)", !render_same(1));
#else
    check("old: the render with live LFOs on every lane = the render without (never heard)", render_same(1));
#endif
}

/* ---- at the hit */
static void t_hit(void)
{
    uint32_t i, ok = 1, n2 = 0;
    reset();
    lane_on(8);
    dl.ofs[L][DE_DECAY] = 10;
    set_lfo(L, 70, 2, 16, 0, DE_DECAY + 1u, 50, DE_TUNE + 1u, -30);   /* (SAW, PHS 16: decay +, tune -) */
    check("an LFO on DECAY (AMT 50) and TUNE (AMT -30): live, its lane computed (dlfo_on bit 4 only; old: none)", dlfo_live(L) &&
          dlfo_on == (DLFO_ON ? 1u << L : 0u));
    nh = 0;
    transport_req = 1;
    while (clk_beat < 4u)
        run_block();
    seq_stop();
    for (i = 0; i < nh; i++) {
        int32_t f = want_lfo(70, 2, 16, hits[i].tick);
        ok &= hits[i].o[DE_DECAY] == want_val(10, f, 50, DE_DECAY) && hits[i].o[DE_TUNE] == want_val(0, f, -30, DE_TUNE) &&
              !hits[i].o[DE_SNAP];
        n2 += i && hits[i].o[DE_DECAY] != hits[i - 1u].o[DE_DECAY];
    }
    printf("lfo: %u hits, DECAY heard %d %d %d %d .., TUNE %d %d %d %d ..\n", (unsigned)nh, hits[0].o[1], hits[1].o[1], hits[2].o[1],
           hits[3].o[1], hits[0].o[0], hits[1].o[0], hits[2].o[0], hits[3].o[0]);
#if DLFO_ON
    check("each hit hears the patch + the LFO's value at its block x AMT (DECAY 10 + 50, TUNE 0 - 30), as figured here", ok && nh >= 16u &&
          n2 >= 8u);
    check("the patch dl.ofs never written", dl.ofs[L][DE_DECAY] == 10 && !dl.ofs[L][DE_TUNE]);
    reset();
    lane_on(8);
    dl.ofs[L][DE_LEVEL] = -4;
    set_lfo(L, 90, 3, 0, 0, DE_LEVEL + 1u, 40, DE_LEVEL + 1u, 23);   /* (SQR: both slots on LEVEL add: 63) */
    (void)auto_put(AUTO_L(TRK_DRUM), 2u | AUTO_ONLY, AUTO_LANE(L, DE_LEVEL), -20);
    auto_w.on = 1u << TRK_DRUM;
    nh = 0;
    transport_req = 1;
    while (clk_beat < 2u)
        run_block();
    seq_stop();
    for (i = 0, ok = nh >= 8u; i < nh; i++) {
        int32_t f = want_lfo(90, 3, 0, hits[i].tick), step = (int32_t)((hits[i].beat * BEAT_U + hits[i].pos + BEAT_U / 8u) / (BEAT_U / 4u) % 8u);
        ok &= hits[i].o[DE_LEVEL] == want_val(step == 2 ? -20 : -4, f, 63, DE_LEVEL);
    }
    check("two slots on LEVEL add (40 + 23); a lane lock (-20 on step 3) replaces the patch there, the LFO adds; clamped -24..6",
          ok);
#else
    for (i = 0, ok = nh >= 16u; i < nh; i++)
        ok &= hits[i].o[DE_DECAY] == 10 && !hits[i].o[DE_TUNE];
    check("old (DLFO_ON 0): the LFO kept, never heard (every hit: the patch)", ok && dlfo_live(L) && !dlfo_on);
#endif
}

#if DLFO_ON
/* ---- sync, restart, fade, stopped */
static void t_sync(void)
{
    static const uint32_t BPM[2] = {120, 133};
    uint32_t b, i, ok = 1, k;
    for (b = 0; b < 2u; b++) {
        int32_t lo = 127, hi = -128;
        reset();
        song.g[G_BPM] = (int16_t)BPM[b];
        TDRUM->p[P_SLEN] = 16;
        dstep_set(&TDRUM->dstep[0], L, LV_NORM, 0);
        set_lfo(L, -3, 2, 0, 0, DE_DECAY + 1u, 63, 0, 0);            /* (1BAR, SAW from PHS 0) */
        nh = 0;
        transport_req = 1;
        while (clk_beat < 32u)
            run_block();
        seq_stop();
        for (i = 0; i < nh; i++)
            lo = hits[i].o[DE_DECAY] < lo ? hits[i].o[DE_DECAY] : lo, hi = hits[i].o[DE_DECAY] > hi ? hits[i].o[DE_DECAY] : hi;
        printf("lfo: 1BAR synced at %u BPM: the bar's first hit hears DECAY %d..%d over %u bars\n", (unsigned)BPM[b], lo, hi, (unsigned)nh);
        ok &= nh == 8u && hi - lo <= 1 && lo <= -60;
    }
    check("synced (RATE 1BAR): the bar's first hit hears the same LFO every bar (at most 1 apart: a block), 8 bars at 120 and 133 BPM, "
          "its cycle's start (SAW: the bottom)", ok);
    /* restart and fade */
    reset();
    lane_on(8);
    set_lfo(L, 40, 0, 32, 0, DE_SNAP + 1u, 60, 0, 0);
    for (k = 0; k < 2u; k++) {
        static int8_t first[2][4];
        nh = 0;
        transport_req = 1;
        run_block();                                   /* (PLAY: the clock from 0) */
        while (clk_beat < 3u)
            run_block();
        seq_stop();
        for (i = 0; i < 4u; i++)
            first[k][i] = hits[i].o[DE_SNAP];
        printf("lfo: PLAY %u: SNAP %d %d %d %d at ticks %u %u %u %u\n", (unsigned)k, first[k][0], first[k][1], first[k][2], first[k][3],
               (unsigned)hits[0].tick, (unsigned)hits[1].tick, (unsigned)hits[2].tick, (unsigned)hits[3].tick);
        if (k)
            check("PLAY again: the LFO from PHS again (the first four hits hear what they heard after the first PLAY)",
                  !memcmp(first[0], first[1], 4) && nh >= 4u);
        for (i = 0; i < 300u; i++)                    /* (stopped a while between) */
            run_block();
    }
    check("stopped with a live LFO: the blocks counted (a hit then hears it moving)", dlfo_free >= 300u);
    reset();
    lane_on(8);
    set_lfo(L, 0, 3, 0, 90, DE_SNAP + 1u, 63, 0, 0);    /* (the slowest SQR: +1 the whole time, FADE 90) */
    nh = 0;
    transport_req = 1;
    while (clk_beat < 8u)
        run_block();
    seq_stop();
    printf("lfo: FADE 90: SNAP heard %d %d %d %d .. %d\n", hits[0].o[DE_SNAP], hits[1].o[DE_SNAP], hits[2].o[DE_SNAP], hits[3].o[DE_SNAP],
           hits[nh - 1u].o[DE_SNAP]);
    check("FADE: the first hits after PLAY hear the LFO faded in (rising), the last its whole depth (62)", nh >= 16u &&
          hits[0].o[DE_SNAP] < hits[4].o[DE_SNAP] && hits[4].o[DE_SNAP] <= hits[nh - 1u].o[DE_SNAP] && hits[nh - 1u].o[DE_SNAP] == 62);
}
#endif

/* ---- the record */
static void t_record(void)
{
    static uint8_t a[DLFO_ENC_MAX + 32], b2[DLFO_ENC_MAX + 32];
    static dlfo_t want[DRUM_LANES];
    uint32_t n, l, k;
    reset();
    check("every lane at the defaults: no record (as a project from before)", dlfo_encode(a) == 0u);
    set_lfo(0, -10, 4, 127, 127, DE_LEVEL + 1u, -64, DE_TUNE + 1u, 63);
    set_lfo(7, 127, 1, 5, 0, DE_CUT + 1u, 1, 0, 0);
    set_lfo(9, 60, 0, 0, 0, 0, 0, DE_DRIVE + 1u, -1);
    memcpy(want, dlfo, sizeof want);
    n = dlfo_encode(a);
    check("three lanes set: a record of 4 + 3 x 8 bytes (version 1, 8 a lane, the lanes 0 7 9)", n == 28u && a[0] == 1u && a[1] == 8u &&
          a[2] == 0x81u && a[3] == 0x02u);
    (void)dlfo_decode(0, 0);
    check("... read back: every field equal, the other lanes at the defaults", dlfo_decode(a, n) && !memcmp(dlfo, want, sizeof want));
    check("... none (an older project): every LFO off, the defaults", dlfo_decode(0, 0) && !dlfo_any_live() && dlfo_is_def(&dlfo[0]));
    memcpy(b2, a, n);
    b2[0] = 2;
    check("a newer version: refused whole, every LFO off", !dlfo_decode(b2, n) && !dlfo_any_live());
    for (l = 0, k = 4; l < 3u; l++) {                  /* (a newer build's 10 bytes a lane: the 8 known, 2 skipped) */
        memcpy(b2 + k, a + 4u + 8u * l, 8);
        b2[k + 8u] = 0xEE, b2[k + 9u] = 0xEE;
        k += 10u;
    }
    b2[0] = 1, b2[1] = 10;
    check("a longer entry (10 bytes a lane, a newer build's): the first 8 read, the rest skipped", dlfo_decode(b2, k) &&
          !memcmp(dlfo, want, sizeof want));
    for (l = 0, k = 4; l < 3u; l++) {                  /* (6 bytes a lane: no second slot) */
        memcpy(b2 + k, a + 4u + 8u * l, 6);
        k += 6u;
    }
    b2[1] = 6;
    check("a shorter entry (6 bytes): the second slot at its default (OFF)", dlfo_decode(b2, k) && dlfo[0].s[0].dest == DE_LEVEL + 1u &&
          !dlfo[0].s[1].dest && !dlfo[9].s[1].dest);
    check("a length that is not 4 + lanes x entry: refused, every LFO off", !dlfo_decode(a, n - 1u) && !dlfo_any_live());
    memcpy(b2, a, n);
    b2[4] = 0x80, b2[5] = 9, b2[8] = 12, b2[6] = 200;  /* (rate -128, wave 9, dest 12, phase 200) */
    check("values out of range brought back (RATE -10, WAVE SIN, PHS 72, DEST OFF)", dlfo_decode(b2, n) && dlfo[0].rate == -10 &&
          !dlfo[0].wave && dlfo[0].phase == (200u & 127u) && !dlfo[0].s[0].dest);
    /* through a project buffer, a section, the autosave */
    {
        static project_t gp;
        static dlrec_t gd;
        memset(nor, 0xFF, sizeof nor);
        sec_pend_clear();
        sec_boot();
        memset(pat_cur, PAT_NONE, NTRK);
        live_sec = -1;
        (void)dlfo_decode(a, n);
        proj_capture(&gp, &gd);
        (void)dlfo_decode(0, 0);
        proj_apply(&gp, &gd, 1);
        check("a project buffer: captured with its LFOs, applied back", !memcmp(dlfo, want, sizeof want));
        project_save(2);
        (void)dlfo_decode(0, 0);
        project_load(2);
        check("a section saved and loaded (its LFO record in the log, keyed by the section's record)", !strcmp(last_msg, "LOADED") &&
              !memcmp(dlfo, want, sizeof want) && slg_has(DLR_ID0 + 2u));
        (void)slg_put(DLR_ID0 + 2u, sec_rbuf, 0, 0);    /* (as an older firmware saved it: no LFO record) */
        project_load(2);
        check("... the same section without its LFO record (an older firmware's): every LFO off", !dlfo_any_live() && dlfo_is_def(&dlfo[7]));
        (void)dlfo_decode(a, n);
        project_save(3);
        (void)dlfo_decode(0, 0);
        project_save(4);                               /* (another section: none) */
        project_load(3);
        k = !memcmp(dlfo, want, sizeof want);
        project_load(4);
        check("... a section saved with LFOs, another without: each loads its own (the other: every LFO off)", k && !dlfo_any_live());
        (void)dlfo_decode(a, n);
        proj_capture(&gp, &gd);
        (void)dlr_log_put(DLR_ID_AUTO, gp.sum, &gp, 0);
        memset(dlr_for(&gp, 1), 0, sizeof(dlr_store_t));
        (void)dlfo_decode(0, 0);
        dlr_log_get(DLR_ID_AUTO, gp.sum, &gp);
        proj_apply(&gp, &gd, 1);
        check("the autosave's LFO record (id 138) read back as autosave_resume does", !memcmp(dlfo, want, sizeof want));
        dlr_log_get(DLR_ID_AUTO, gp.sum ^ 1u, &gp);     /* (another project's key) */
        proj_apply(&gp, &gd, 1);
        check("... with another project's key: none (every LFO off)", !dlfo_any_live());
    }
}

/* ---- 2.4, MISS */
static uint32_t get78(uint32_t flag, int withflag, uint32_t *w2)
{
    uint8_t a[5] = {0, 0, 0, 0, (uint8_t)flag};
    ed_n = 0;
    (void)ed_sl24(78, a, withflag ? 5u : 4u);
    *w2 = withflag && ed_n >= 17u ? ed_g7(ed_out + ed_n - 2u, 2) : 0u;
    return ed_n >= 15u ? ed_g7(ed_out + 10, 2) : 0xFFFFu;
}
static void t_sl24_miss(void)
{
    uint32_t w1, w2, i, n, has = 0;
    reset();
    set_lfo(3, 50, 0, 0, 0, DE_LEVEL + 1u, 0, 0, 0);    /* (not live) */
    w1 = get78(1, 1, &w2);
    check("2.4 export, no LFO acting: no SX24_DLFO", !(w2 & 8u));
    set_lfo(3, 50, 0, 0, 0, DE_LEVEL + 1u, 10, 0, 0);
    w1 = get78(1, 1, &w2);
    check("... a lane LFO acting: lost2 bit 3 (SX24_DLFO), bit 13 clear", (w2 & 8u) && !(w1 & SX24_MOTION));
    w1 = get78(0, 0, &w2);
    check("... a v11 request: on bit 13 (its motion)", (w1 & SX24_MOTION) != 0u);
    n = miss_scan();
    for (i = 0; i < n; i++)
        has |= miss_m[i] == MS_ITEM(MS_FX, TRK_DRUM, MF_LLFO);
#if DLFO_ON
    check("MISS: a build that plays them lists nothing for them", !has);
#else
    check("old: MISS lists LANE LFO", has);
#endif
}

#if DLFO_ON
/* ---- the cost (host instructions, -O2): a hit's dl_play_hit; a block's DLFO_BLOCK; a busy kit's block */
static void t_cost(void)
{
    uint32_t i, l, rounds = 20000;
    uint64_t t0, base, lfo2, full, blk0, blk1;
    reset();
    dl.ofs[L][DE_DECAY] = 5;
    dla.any = 0;
    t0 = instr_now();
    for (i = 0; i < rounds; i++)
        DL_PLAY_HIT(L);                                /* (drum_on's test: nothing on any lane) */
    base = instr_now() - t0;
    set_lfo(L, 70, 2, 0, 40, DE_DECAY + 1u, 50, DE_TUNE + 1u, -30);
    song.playing = 1, song.tick = 1000;
    t0 = instr_now();
    for (i = 0; i < rounds; i++)
        dl_play_hit(L);
    lfo2 = instr_now() - t0;
    set_lfo(L, -8, 4, 0, 40, DE_DECAY + 1u, 50, DE_TUNE + 1u, -30);   /* (synced, S&H, a fade) */
    dla.hm[L] = 0x0F, dla.lm[L] = 0xF0;
    t0 = instr_now();
    for (i = 0; i < rounds; i++)
        dl_play_hit(L);
    full = instr_now() - t0;
    dla.hm[L] = dla.lm[L] = 0;
    song.playing = 0;
    printf("lfo cost (host instructions a hit, the measuring loop's 3 or so with it): nothing on any lane %.1f, LFO 2 slots %.1f, synced S&H LFO + 4 holds + 4 locks %.1f\n",
           (double)base / rounds, (double)lfo2 / rounds, (double)full / rounds);
    /* a busy kit: 16 lanes on every 16th, each with an LFO (2 slots) and a lane hold; the block's whole cost against none */
    for (l = 0; l < 2u; l++) {
        uint32_t s, k;
        reset();
        TDRUM->p[P_SLEN] = 16;
        for (s = 0; s < 16u; s++)
            for (k = 0; k < DRUM_LANES; k++)
                dstep_set(&TDRUM->dstep[s], k, LV_NORM, 0);
        if (l) {
            for (k = 0; k < DRUM_LANES; k++) {
                set_lfo(k, (int32_t)(20u + k * 5u), k % 5u, k * 8u, 0, DE_DECAY + 1u, 40, DE_CUT + 1u, -20);
                for (s = 0; s < 16u; s++)
                    (void)auto_put(AUTO_L(TRK_DRUM), s, AUTO_LANE(k, DE_TUNE), (int32_t)(s % 5u));
            }
            auto_w.on = 1u << TRK_DRUM;
        }
        transport_req = 1;
        run_block();
        t0 = instr_now();
        for (i = 0; i < 2000u; i++)
            run_block();
        if (l)
            blk1 = instr_now() - t0;
        else
            blk0 = instr_now() - t0;
        seq_stop();
    }
    printf("lfo cost (host instructions a block, 16 lanes on every 16th at 120 BPM, 2000 blocks): none %.0f, 16 LFOs + 16 lanes' "
           "motion %.0f (+%.0f, %.2f %%)\n", (double)blk0 / 2000.0, (double)blk1 / 2000.0, (double)(blk1 - blk0) / 2000.0,
           100.0 * (double)(blk1 - blk0) / (double)blk0);
    check("the cost measured (the numbers above)", base > 0u || !instr_now());
}
#endif

int main(void)
{
    outbuf = mmap(0, 2u * sizeof *outbuf, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    (void)auto_for(&proj_tmp.cur, 1), (void)auto_for(&sec_stage_p, 1);
    (void)dlr_for(&proj_tmp.cur, 1), (void)dlr_for(&sec_stage_p, 1);
    auto_init();
    printf("lfo: DLFO_ON %d\n", (int)DLFO_ON);
    t_off();                                           /* (both builds: the render with idle LFOs = without) */
    t_hit();
#if DLFO_ON
    t_sync();
#endif
    t_record();
    t_sl24_miss();
#if DLFO_ON
    t_cost();
#endif
    printf("lfo test %s (%d failed)\n", bad ? "FAILED" : "passed", bad);
    return bad;
}
