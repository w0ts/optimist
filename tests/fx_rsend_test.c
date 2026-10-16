/* SPDX-License-Identifier: GPL-3.0-only */
/* The sends between the returns (2026-10-16; fx_slots.c rs_amt, fx.c rs_block / fx_buses / rt_line): left to right only,
 * A -> B, A -> C, B -> C, rendered in the returns' order whatever lines they own:
 *   order    three returns that pass their send through (FILT open, DIST and DYN at 0: 100 % wet, unity), a sine sent to A's
 *            line only: A alone 1x; A -> B 2x; A -> B -> C 3x (C hears B's output, A's send in it: B ran before C); A -> C
 *            and B -> C; at one half 1.5x; the lines in another order (A on CHO's line 0, C on REV's 2) the same
 *   level    a send takes its return's output after its RET (A's RET at 0: B hears nothing); ramped (no step at a change)
 *   buses    REV in A sending to DLY in B: the delay sounds from A's output alone (its own send silent: woken, rs_wake)
 *   idle     every send at 0: the chain plan ends (rs_on 0), the same samples as a build of before (no chain code run)
 *   record   RSND_TLV round trips; the default writes none; a record without it (an old project) loads every send at 0
 *   feedback (RET_FB) B -> A: A hears B's output of the block before (32 samples late, half a send's gain); worst case
 *            (every send 127, REV SIZE 127, DLY FDBK 120, full-scale noise): bounded, no overflow; every send back at 0:
 *            the tails end, idle; DC in a loop blocked; the stored form 6 bytes
 *   cost     instructions a half (256 frames) of the returns' render, by proc_pid_rusage: every send 0, the forward sends,
 *            the feedback ones (printed: the report's table)
 * Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"
#ifdef __APPLE__
#include <libproc.h>                                    /* (proc_pid_rusage: the cost lines) */
#include <unistd.h>
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("fx_rsend: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
/* one block of the returns (fx.c rets_mix) with a sine (amplitude a) on line j's send; -> the block's peak of the mix (L) */
static int32_t blk(uint32_t j, int32_t a, uint32_t b0, int *lr)
{
    int32_t *snd[3] = {send_c, send_d, send_r}, pk = 0;
    uint32_t i, k;
    for (k = 0; k < 3u; k++)
        for (i = 0; i < CTL; i++)
            snd[k][i] = k == j ? (int32_t)lrint(a * sin(2 * M_PI * 441.0 * (b0 * CTL + i) / FS)) : 0;
    for (i = 0; i < CTL; i++)
        mix_l[i] = mix_r[i] = 0;
    rets_mix(CTL);
    for (i = 0; i < CTL; i++) {
        pk = abs(mix_l[i]) > pk ? abs(mix_l[i]) : pk;
        if (lr)
            *lr &= mix_l[i] == mix_r[i];
    }
    return pk;
}
/* the steady peak of the mix with the sine on line j (40 blocks: the ramps done; the last 20 measured) */
static int32_t run(uint32_t j)
{
    uint32_t i;
    int32_t pk = 0, p;
    for (i = 0; i < 40u; i++) {
        p = blk(j, 8000, i, 0);
        pk = i >= 20u && p > pk ? p : pk;
    }
    return pk;
}
static int near(int32_t v, double want) { return fabs(v - want) <= want * 0.02 + 4.0; }
static void sends(int ab, int ac, int bc) { rs_amt[RS_AB] = (int16_t)ab, rs_amt[RS_AC] = (int16_t)ac, rs_amt[RS_BC] = (int16_t)bc; }
#if FELUCCA_RET_FB
static void fbsends(int ba, int ca, int cb) { rs_amt[RS_BA] = (int16_t)ba, rs_amt[RS_CA] = (int16_t)ca, rs_amt[RS_CB] = (int16_t)cb; }
#else
static void fbsends(int ba, int ca, int cb) { (void)ba, (void)ca, (void)cb; }
#endif
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
/* the default returns (REV DLY CHO), every line fed a sine (the tracks' sends: every bus runs anyway): instructions a half
 * (HALF_FRAMES / CTL blocks) of rets_mix, the median of 5 runs of 400 halves */
static double cost_half(void)
{
    double r[5], t;
    uint32_t k, b, i, j;
    for (k = 0; k < 5u; k++) {
        uint64_t a;
        for (b = 0; b < 64u; b++) {                     /* (settle: the ramps, the tails) */
            for (j = 0; j < 3u; j++)
                for (i = 0; i < CTL; i++)
                    (j == 0 ? send_c : j == 1 ? send_d : send_r)[i] = (int32_t)(4000.0 * sin(0.05 * (b * CTL + i) + j));
            for (i = 0; i < CTL; i++)
                mix_l[i] = mix_r[i] = 0;
            rets_mix(CTL);
        }
        a = instr_now();
        for (b = 0; b < 400u * (HALF_FRAMES / CTL); b++) {
            for (j = 0; j < 3u; j++)
                for (i = 0; i < CTL; i++)
                    (j == 0 ? send_c : j == 1 ? send_d : send_r)[i] = (int32_t)(4000.0 * sin(0.05 * (b * CTL + i) + j));
            for (i = 0; i < CTL; i++)
                mix_l[i] = mix_r[i] = 0;
            rets_mix(CTL);
        }
        r[k] = (double)(instr_now() - a) / 400.0;
    }
    for (k = 0; k < 5u; k++)                            /* (sort: the median) */
        for (b = k + 1u; b < 5u; b++)
            if (r[b] < r[k])
                t = r[k], r[k] = r[b], r[b] = t;
    return r[2];
}
/* returns A B C passing their send through: three types RT_OK holds of DIST FILT DYN DUST, at their open point */
static int unity_layout(void)
{
    static const uint8_t T[] = {FXT_FILT, FXT_DIST, FXT_DYN};
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        if (!(RT_OK >> T[k] & 1u))
            return 0;
        fxs_load(1u + k, T[k]);
        fxs_ramt[1u + k] = 0;                            /* (FILT 0: open; DIST, DYN 0: through) */
    }
    return 1;
}

#if FELUCCA_RET_FB
#define DB(v) (20.0 * log10((v) / 32767.0 + 1e-9))
/* every send 127 (fb: the feedback ones too), REV SIZE size, DLY FDBK fdbk: 2 s of full-scale noise into every line, then
 * 18 s of silence -> the peak, the peak of the last 2 s */
static void loud(int size, int fdbk, int fb, int32_t *pk, int32_t *top)
{
    uint32_t b2, i, n, seed = 1;
    song.g[G_RSIZE] = (int16_t)size, song.g[G_DFDBK] = (int16_t)fdbk;
    sends(127, 127, 127);
    fbsends(fb ? 127 : 0, fb ? 127 : 0, fb ? 127 : 0);
    *pk = *top = 0;
    for (b2 = 0; b2 < 44100u * 20u / CTL; b2++) {
        int32_t *snd[3] = {send_c, send_d, send_r};
        for (n = 0; n < 3u; n++)
            for (i = 0; i < CTL; i++) {
                seed = seed * 1664525u + 1013904223u;
                snd[n][i] = b2 < 44100u * 2u / CTL ? (int32_t)(seed >> 16) - 32768 : 0;
            }
        memset(mix_l, 0, sizeof mix_l), memset(mix_r, 0, sizeof mix_r);
        rets_mix(CTL);
        for (i = 0; i < CTL; i++) {
            int32_t a = abs(mix_l[i]) > abs(mix_r[i]) ? abs(mix_l[i]) : abs(mix_r[i]);
            *pk = a > *pk ? a : *pk;
            *top = b2 > 44100u * 18u / CTL && a > *top ? a : *top;
        }
    }
    sends(0, 0, 0);
    fbsends(0, 0, 0);
    for (b2 = 0; b2 < 44100u * 60u / CTL; b2++) {       /* (rung out before the next) */
        int32_t *snd[3] = {send_c, send_d, send_r};
        for (n = 0; n < 3u; n++)
            memset(snd[n], 0, sizeof send_c);
        rets_mix(CTL);
    }
}
#endif
int main(void)
{
    uint8_t b[FXR_MAX];
    uint32_t n, la, i;
    int32_t a1, p;
    int lr = 1;
    host_tracks_init();
    fxs_set(FXS_DEF);
    song.g[G_CRET] = song.g[G_DRET] = song.g[G_RRET] = 127;
    {   /* the cost, instructions a half (256 frames) of the returns' render (the report's table) */
        double c0, c1, c2;
        fxs_set(FXS_DEF);
        sends(0, 0, 0), fbsends(0, 0, 0);
        c0 = cost_half();
        sends(64, 64, 64);
        c1 = cost_half();
        fbsends(64, 64, 64);
        c2 = cost_half();
        sends(0, 0, 0), fbsends(0, 0, 0);
        c2 = FELUCCA_RET_FB ? c2 : 0.0;
        printf("fx_rsend: cost a half (host instructions, REV DLY CHO, every line fed): every send 0 %.0f; A -> B, A -> C, "
               "B -> C on +%.0f; and the feedback sends +%.0f more\n", c0, c1 - c0, FELUCCA_RET_FB ? c2 - c1 : 0.0);
        for (i = 0; i < 44100u * 60u / CTL; i++)         /* (every bus rung out: the checks below start from idle) */
            blk(0, 0, i, 0);
        check("... every send back at 0: no chain planned", !rs_on);
    }
    check("default: every send between the returns at 0, no chain planned, nothing to keep", !rs_amt[0] && !rs_amt[1] &&
          !rs_amt[2] && !rs_on && fxr_encode(b) == 0u);
    if (!unity_layout()) {
        printf("fx_rsend: FILT DIST DYN not all built: the order checks skipped\n");
    } else {
        la = fxs_line[1];
        check("FILT DIST DYN into A B C: on lines 2 1 0 (REV's, DLY's, CHO's: A renders last in the buses' own order)",
              fxs_line[1] == 2u && fxs_line[2] == 1u && fxs_line[3] == 0u);
        sends(0, 0, 0);
        a1 = run(la);
        check("A alone: its send through (100 % wet, unity)", near(a1, 8000));
        sends(127, 0, 0);
        p = run(la);
        check("A -> B at 127: A's output and B's (A's in it): 2x", near(p, 2.0 * a1 * 32766 / 32768));
        sends(127, 0, 127);
        p = run(la);
        check("A -> B -> C: C hears B, which heard A first (one order: A, B, C): 3x", near(p, a1 * (1.0 + 2.0 * 32766 / 32768)));
        sends(0, 127, 0);
        p = run(la);
        check("A -> C alone: 2x", near(p, 2.0 * a1 * 32766 / 32768));
        sends(127, 127, 127);
        p = run(la);
        check("A -> B, A -> C, B -> C: A + B (A) + C (A + B (A)): 4x", near(p, a1 * (1.0 + 3.0 * 32766 / 32768)));
        sends(64, 0, 0);
        p = run(la);
        check("A -> B at 64: 1.5x (a track's send law, 64 x 258 / 32768)", near(p, a1 * (1.0 + 64.0 * 258 / 32768)));
        for (i = 0, lr = 1; i < 4u; i++)
            blk(la, 8000, i, &lr);
        check("... mono in, L = R out", lr);
        sends(0, 0, 127);
        p = run(la);
        check("B -> C with nothing in B: A alone (B's send carries B's output only)", near(p, a1));
        sends(127, 0, 0);
        song.g[G_CRET + la] = 0;
        p = run(la);
        check("A's RET at 0: A silent, and B hears nothing from it (a send is after its return's level)", p == 0);
        song.g[G_CRET + la] = 127;
        sends(0, 0, 0);
        run(la);
        p = blk(la, 8000, 0, 0);
        check("every send back at 0: the chain plan ends (rs_on 0: no chain code runs), A alone", !rs_on && near(run(la), a1));
        {   /* a send's ramp: the first block after a change does not jump to the new gain */
            int32_t s0;
            sends(127, 0, 0);
            s0 = blk(la, 8000, 0, 0);
            p = run(la);
            check("... a send ramped over its first block (no step)", s0 < p && s0 > a1 / 2);
        }
        /* the lines in another order: A on line 0 (CHO's), B on 1, C on 2: swap A and C's types (lines follow the slots) */
        sends(0, 0, 0);
        fxs_load(1, FXT_DYN);                           /* (DYN from C: the two swap, each keeping its line) */
        fxs_ramt[1] = fxs_ramt[3] = 0;
        check("swapped: DYN into A, FILT into C, each keeping its line (A on line 0, C on 2); the sends stay positional", fxs_slot[1] == FXT_DYN && fxs_slot[3] == FXT_FILT);
        sends(127, 0, 127);
        p = run(fxs_line[1]);
        check("... A -> B -> C after the swap: 3x again", near(p, a1 * (1.0 + 2.0 * 32766 / 32768)));
        sends(0, 0, 0);
        run(0);
        fxs_set(FXS_DEF);
        for (i = 0; i < RT_TAIL + 2u; i++)
            blk(0, 0, i, 0);
    }
    {   /* the buses: REV in A (line 2), DLY in B (line 1); only the reverb's line is sent; the delay wakes from A's output */
        int32_t pd = 0, pn = 0;
        fxs_set(FXS_DEF);
        song.g[G_DTIME] = 10;
        sends(0, 0, 0);
        for (i = 0; i < 400u; i++) {
            fx.dly_w += 0;
            p = blk(2, i < 40u ? 8000 : 0, i, 0);
            pn = i > 300u && p > pn ? p : pn;
        }
        for (i = 0; i < 2000u; i++)
            blk(2, 0, i, 0);
        sends(127, 0, 0);
        for (i = 0; i < 400u; i++) {
            p = blk(2, i < 40u ? 8000 : 0, i, 0);
            pd = i > 300u && p > pd ? p : pd;
        }
        check("REV in A sending to DLY in B: the delay sounds from A's output (woken: its own send silent), louder than A alone",
              !FELUCCA_FX_REVERB || !FELUCCA_FX_DELAY || pd > pn);
        sends(0, 0, 0);
    }
#if FELUCCA_RET_FB
    fxs_set(FXS_DEF);
    sends(0, 0, 0);
    for (i = 0; i < 44100u * 60u / CTL; i++)            /* (every bus rung out: the impulse below alone) */
        blk(0, 0, i, 0);
    if (unity_layout()) {   /* feedback: B -> A, one block late, half the gain; the returns pass their send through */
        int32_t hit[2][CTL];
        uint32_t lb = fxs_line[2], b2;
        sends(0, 0, 0);
        fbsends(127, 0, 0);
        for (i = 0; i < 4u; i++)
            blk(lb, 0, i, 0);                           /* (the ramp done on silence) */
        for (b2 = 0; b2 < 2u; b2++) {                   /* an impulse into B's line in block 0, nothing in block 1 */
            int32_t *snd[3] = {send_c, send_d, send_r};
            for (n = 0; n < 3u; n++)
                memset(snd[n], 0, sizeof send_c);
            if (!b2)
                snd[lb][5] = 6000;
            memset(mix_l, 0, sizeof mix_l), memset(mix_r, 0, sizeof mix_r);
            rets_mix(CTL);
            memcpy(hit[b2], mix_l, sizeof hit[b2]);
        }
        printf("fx_rsend: impulse: block 0 [4..6] %d %d %d, block 1 [4..6] %d %d %d\n", hit[0][4], hit[0][5], hit[0][6], hit[1][4], hit[1][5], hit[1][6]);
        check("B -> A (RET_FB): block 0 has B's impulse (under the loop limiter) alone; A hears it the next block, at the same place (32 samples on), about a quarter (127: 0.25, through A)",
              abs(hit[0][5] - 6000) < 400 && hit[1][5] > hit[0][5] * 15 / 100 && hit[1][5] < hit[0][5] * 27 / 100 && hit[1][5] > 3 * abs(hit[1][4]) && hit[1][5] > 3 * abs(hit[1][6]));
        fbsends(0, 0, 0);
        for (i = 0; i < 4u; i++)
            blk(lb, 0, i, 0);
        check("... every feedback send back at 0: no chain planned (rs_on 0)", !rs_on);
        {   /* DC into the loop A -> B -> A: blocked, the loop's output settles to 0 with a constant in */
            int32_t pk = 0;
            sends(127, 0, 0);
            fbsends(127, 0, 0);
            for (i = 0; i < 3000u; i++) {
                int32_t *snd[3] = {send_c, send_d, send_r};
                for (n = 0; n < 3u; n++)
                    memset(snd[n], 0, sizeof send_c);
                for (n = 0; n < CTL; n++)
                    snd[la][n] = i < 1000u ? 3000 : 0;
                memset(mix_l, 0, sizeof mix_l), memset(mix_r, 0, sizeof mix_r);
                rets_mix(CTL);
                for (n = 0; i > 2900u && n < CTL; n++)
                    pk = abs(mix_l[n]) > pk ? abs(mix_l[n]) : pk;
            }
            check("... DC into A with A -> B -> A at 127: no DC goes round (silent after the input stops)", pk < 16);
        }
        sends(0, 0, 0);
        fbsends(0, 0, 0);
        for (i = 0; i < 8u; i++)
            blk(0, 0, i, 0);
        fxs_set(FXS_DEF);
        for (i = 0; i < RT_TAIL + 2u; i++)
            blk(0, 0, i, 0);
    }
    if (FELUCCA_FX_REVERB && FELUCCA_FX_DELAY && FELUCCA_FX_CHORUS) {   /* the worst case and a long one */
        int16_t sz = song.g[G_RSIZE], fd = song.g[G_DFDBK];
        int32_t pk, top, pk0, top0, pkm, topm;
        uint32_t idle = 0, b2;
        fxs_set(FXS_DEF);
        loud(127, 120, 0, &pk0, &top0);                 /* (the forward sends alone, the longest tails) */
        loud(127, 120, 1, &pk, &top);
        printf("fx_rsend: every send 127, SIZE 127 (near-freeze), FDBK 120, 2 s of full-scale noise in every line: forward "
               "alone peak %.1f dBFS, after 18 s %.1f dBFS; with the feedback sends peak %.1f dBFS, after 18 s %.1f dBFS\n",
               DB(pk0), DB(top0), DB(pk), DB(top));
        check("worst case: bounded (under 24 dBFS before the master, far from any overflow), the copies within 1.0 FS",
              pk < 32767 * 16 && abs(rs_fbv[0][0]) <= 32767 && abs(rs_fbv[1][0]) <= 32767 && abs(rs_fbv[2][0]) <= 32767);
        loud(100, 90, 1, &pkm, &topm);
        printf("fx_rsend: every send 127 (feedback too), SIZE 100, FDBK 90: peak %.1f dBFS, after 18 s %.1f dBFS\n", DB(pkm), DB(topm));
        sends(0, 0, 0);
        fbsends(0, 0, 0);
        song.g[G_RSIZE] = 100, song.g[G_DFDBK] = 90;
        for (b2 = 0; b2 < 44100u * 90u / CTL && !idle; b2++) {
            int32_t *snd[3] = {send_c, send_d, send_r};
            for (n = 0; n < 3u; n++)
                memset(snd[n], 0, sizeof send_c);
            memset(mix_l, 0, sizeof mix_l), memset(mix_r, 0, sizeof mix_r);
            rets_mix(CTL);
            idle = !rs_on && fx.rev_q >= REV_Q && fx.dly_q >= DLY_LEN && fx.cho_q >= CHO_LEN;
        }
        check("... every send back at 0 (SIZE 100, FDBK 90): the chain ends, every bus rings out to idle (within 90 s)", idle);
        song.g[G_RSIZE] = sz, song.g[G_DFDBK] = fd;
    }
#endif
    {   /* the record */
        uint32_t m;
        fxs_set(FXS_DEF);
        sends(100, 0, 7);
        n = fxr_encode(b);
        check("record: sends set: a record (the default layout), RSND_TLV 0x4B", n > FXR_HEAD);
        for (m = FXR_HEAD; m + 1u < n && b[m] != RSND_TLV; m += 2u + b[m + 1u])
            ;
        check("... the TLV: A -> B, A -> C, B -> C (RET_FB: then the three feedback sends)", m + 4u < n + 1u && b[m] == RSND_TLV &&
              b[m + 1u] == RS_N && b[m + 2u] == 100u &&
              b[m + 3u] == 0u && b[m + 4u] == 7u);
        sends(0, 0, 0);
        fxr_decode(b, n, 1);
        check("... decoded (a load)", rs_amt[RS_AB] == 100 && rs_amt[RS_AC] == 0 && rs_amt[RS_BC] == 7);
        fxr_decode(b, n, 0);
        check("... a song section's apply keeps them (as the returns' levels)", rs_amt[RS_AB] == 100 && rs_amt[RS_BC] == 7);
        {   /* a longer form (a later build's further sends): those known taken, the rest skipped */
            uint8_t c[FXR_MAX];
            uint32_t e = m + 2u + b[m + 1u];
            memcpy(c, b, e);
            c[e] = 9, c[e + 1u] = 9, c[e + 2u] = 9;
            memcpy(c + e + 3u, b + e, n - e);
            c[m + 1u] = (uint8_t)(b[m + 1u] + 3u);
            fxr_decode(c, n + 3u, 1);
            check("... a longer RSND_TLV (a later build's further sends): the ones known taken, the rest skipped",
                  rs_amt[RS_AB] == 100 && rs_amt[RS_BC] == 7);
        }
#if FELUCCA_RET_FB
        sends(0, 0, 0), fbsends(11, 0, 13);
        n = fxr_encode(b);
        for (m = FXR_HEAD; m + 1u < n && b[m] != RSND_TLV; m += 2u + b[m + 1u])
            ;
        sends(0, 0, 0), fbsends(0, 0, 0);
        fxr_decode(b, n, 1);
        check("... the feedback sends alone: a record, 6 bytes, decoded", b[m + 1u] == 6u && rs_amt[RS_BA] == 11 &&
              rs_amt[RS_CA] == 0 && rs_amt[RS_CB] == 13);
        {   /* the form a build without RET_FB writes (3 bytes): the feedback sends at 0 */
            b[m + 1u] = 3u;
            b[m + 2u] = 100;
            memmove(b + m + 5u, b + m + 8u, n - (m + 8u));
            fxr_decode(b, n - 3u, 1);
            check("... a 3-byte RSND_TLV (a build without RET_FB): the feedback sends at 0", rs_amt[RS_AB] == 100 &&
                  !rs_amt[RS_BA] && !rs_amt[RS_CB]);
        }
#endif
        fxr_decode(0, 0, 1);
        check("no record (a project of before): every send at 0", !rs_amt[0] && !rs_amt[1] && !rs_amt[2]);
        sends(5, 6, 7);
        fxr_decode(b, FXR_HEAD, 1);                      /* (a header alone: the sends of an old record) */
        check("an old record (no RSND_TLV): every send at 0", !rs_amt[0] && !rs_amt[1] && !rs_amt[2]);
    }
    printf("fx_rsend: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
