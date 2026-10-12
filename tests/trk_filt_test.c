/* SPDX-License-Identifier: GPL-3.0-only */
/* The track FILTER (SLOOP 2.4's P_TFLT; firmware/src/fx.c tflt_*, FELUCCA_TRK_FILT), after isod89/sloop-fm1 v2.4
 * (8d3823f) tests/seq2_test.c t_tflt:
 *   part       LP darkens a part, HP thins it; back at 0 it opens and is bypassed (mode 0)
 *   drums      on the drum track: the dry bus AND its sends (a lane's DLY send through a closed LP: darker)
 *   centre     every FILT at 0: the busy mix sample for sample the build without the switch ("hash" mode: the
 *              runner compares both builds' hashes)
 *   project    FILT of each track (and STRUM, VLEAD) through a project and back; all 0: the drum track's slots as
 *              before (no tag); a project of before with values in those slots: all 0
 *   cost       instructions per sample (proc_pid_rusage): the busy mix at 0, every track engaged
 * RENDER=dir: trk-filter-sweep.wav, the busy mix with every track swept 0 -> LP -> 0 -> HP -> 0 (12 s). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i)
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/storage/project.c"
#include <libproc.h>

static int fails;
static void check(int ok, const char *what)
{
    printf("trk_filt: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint64_t insn(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}

static void busy_song(uint32_t parts, int drums)
{
    static const uint8_t ACID[16] = {36, 36, 48, 36, 0, 36, 39, 36, 36, 0, 46, 36, 34, 36, 0, 41};
    static const uint8_t AM[4] = {57, 60, 64, 67};
    static const uint8_t LEAD[16] = {76, 0, 79, 0, 81, 0, 79, 76, 0, 74, 0, 76, 79, 0, 84, 0};
    track_t *t1 = &trk[0], *t2 = &trk[1], *t3 = &trk[2], *td = TDRUM;
    uint32_t i, k;
    seq_stop();
    transport_req = 0;
    host_tracks_init();
#if FELUCCA_TRK_FILT
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_TFLT] = 0;
    memset(tflt, 0, sizeof tflt);
#endif
    (void)k;
    host_preset(t1, 0, 4);
    host_preset(t2, 1, 5);
    host_preset(t3, 3, 0);
    for (i = 0; i < 16u && (parts & 1u); i++) {
        uint8_t n = ACID[i];
        put_step(t1, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
    for (i = 0; i < 16u && (parts & 2u); i++)
        put_step(t2, i, i % 8u == 0u ? 4u : 0u, AM, i % 8u == 0u ? ST_NOTE : i % 8u < 7u ? ST_TIE : ST_REST, 0);
    for (i = 0; i < 16u && (parts & 4u); i++) {
        uint8_t n = LEAD[i];
        put_step(t3, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, 0);
    }
    for (i = 0; i < 16u && drums; i++) {
        uint8_t n[4];
        uint32_t j = 0;
        if (i % 4u == 0u)
            n[j++] = 36;
        if (i == 4u || i == 12u)
            n[j++] = 38;
        n[j++] = i % 4u == 2u ? 46 : 42;
        put_step(td, i, j, n, ST_NOTE, 0);
    }
    host_drum_rev(24);
    song.master_q12 = 2048u;
    transport_req = 1;
}

/* brightness: RMS of the first difference over the RMS (a high-pass on the signal: higher = brighter) */
typedef struct { double d2, x2; int32_t prev; } bright_t;
static void br_add(bright_t *b, const int32_t *x, uint32_t n, uint32_t stride)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t v = x[i * stride];
        b->d2 += (double)(v - b->prev) * (v - b->prev), b->x2 += (double)v * v, b->prev = v;
    }
}
static double br(const bright_t *b) { return b->x2 > 0 ? sqrt(b->d2 / b->x2) : 0; }

/* blocks of the song, the mix's brightness (and the delay send's: snd) after the first second */
static double run(uint32_t secs, double *rms, double *snd)
{
    bright_t m = {0, 0, 0}, s = {0, 0, 0};
    uint32_t f;
    int32_t o[2 * CTL];
    for (f = 0; f < secs * FS; f += CTL) {
        mix_block(o, CTL);
        if (f >= FS) {
            br_add(&m, o, CTL, 2);
            br_add(&s, send_d, CTL, 1);
        }
    }
    if (rms)
        *rms = sqrt(m.x2 / ((secs - 1) * FS));
    if (snd)
        *snd = br(&s);
    return br(&m);
}

#if FELUCCA_TRK_FILT
static void t_part(void)
{
    double b0, blp, bhp, r0, rlp, rhp;
    busy_song(5u, 0);
    b0 = run(3, &r0, 0);
    busy_song(5u, 0);
    trk[0].p[P_TFLT] = trk[2].p[P_TFLT] = -50;
    blp = run(3, &rlp, 0);
    busy_song(5u, 0);
    trk[0].p[P_TFLT] = trk[2].p[P_TFLT] = 50;
    bhp = run(3, &rhp, 0);
    printf("trk_filt: parts 1 + 3 (bass, lead): brightness %.3f, LP -50 %.3f, HP +50 %.3f; RMS %.0f %.0f %.0f\n", b0, blp, bhp, r0, rlp, rhp);
    check(blp < b0 * 0.6, "a part: FILT -50 (low-pass) darkens it");
    check(bhp > b0 * 1.3 && rhp < r0 && tflt[2].mode == 1, "a part: FILT +50 (high-pass) thins it (brighter, less level)");
    trk[2].p[P_TFLT] = 0;
    run(2, 0, 0);
    check(tflt[2].mode == 0, "a part: back at 0, it opens and is bypassed again");
    trk[2].p[P_TFLT] = -50;
    trk[2].p[P_FXOFF] = 1;
    run(2, 0, 0);
    check(tflt[2].mode == 0, "a part: FX bypass (P_FXOFF) opens it (glides open, then off)");
    trk[2].p[P_FXOFF] = 0;
}

static void t_drums(void)
{
    double b0, blp, s0, slp;
    uint32_t l;
    busy_song(0, 1);
    for (l = 0; l < DRUM_LANES; l++)                    /* every lane: a delay send too */
        dsend[l] = dsend_word(dsend_near(24), 20, 0);
    b0 = run(3, 0, &s0);
    busy_song(0, 1);
    for (l = 0; l < DRUM_LANES; l++)
        dsend[l] = dsend_word(dsend_near(24), 20, 0);
    TDRUM->p[P_TFLT] = -60;
    blp = run(3, 0, &slp);
    printf("trk_filt: drums: brightness %.3f -> %.3f with LP -60; their DLY send %.3f -> %.3f\n", b0, blp, s0, slp);
    check(blp < b0 * 0.6 && tflt[TRK_DRUM].mode == -1, "the drum track: LP darkens the drum bus");
    check(s0 > 0 && slp < s0 * 0.6, "the drum track: and its sends (a lane's DLY send, not only REV as 2.4)");
    TDRUM->p[P_TFLT] = 0;
    run(2, 0, 0);
    check(tflt[TRK_DRUM].mode == 0, "the drum track: back at 0, open and bypassed");
}

#endif

/* SLOOP 2.6 (isod89/sloop-fm1 v2.6 d30d655 fx.c djf_run, GPL-3.0-only): a low-pass that starts under a sounding note
 * starts where the signal is, not at 0 (a click). A 110 Hz sine at about a held note's level (the filter's own range)
 * already sounding, then the filter turned to -1 from off (a low-pass, still fully open): the largest difference of
 * its first block from the sine, over the sine's peak. From 0 the output lacks the sine's level for its first samples
 * (a step of up to the whole level: the click); started from its input it differs by what the open filter takes off.
 * which: 0 a part's FILTER (tflt, the part's 3-bit range), 1 the drum track's (its 5 states, the 2-bit range), 2 the
 * master's DJ filter (djf_process, both sides) */
static double start_jump(uint32_t which)
{
    int32_t x[CTL], y[CTL], z[CTL];
    uint32_t i, ch, nch = which == 1 ? 5u : which == 2 ? 2u : 1u, sh = which == 1 ? 2u : 3u;
    double d = 0;
    for (i = 0; i < CTL; i++)
        x[i] = (int32_t)(60000.0 * sin(2.0 * M_PI * 110.0 * (i + 40) / FS)) << (which == 2 ? 0 : sh);   /* (mid-swing) */
    if (which == 2) {
#if FELUCCA_FX_DJF
        memset(&djf, 0, sizeof djf);
        song.g[G_FILT] = -1;
        memcpy(y, x, sizeof y), memcpy(z, x, sizeof z);
        djf_process(y, z, CTL);
        song.g[G_FILT] = 0;
        for (i = 0; i < CTL; i++) {
            double e = fabs((double)y[i] - x[i]), f = fabs((double)z[i] - x[i]);
            d = e > d ? e : d;
            d = f > d ? f : d;
        }
#endif
        return d / 60000.0;
    }
#if FELUCCA_TRK_FILT
    {
        tflt_t t;
        tsvf_t c;
        memset(&t, 0, sizeof t);
        if (!tflt_block(&t, -1, &c))
            return 99;
        for (ch = 0; ch < nch; ch++) {
            memcpy(y, x, sizeof y);
            tflt_run(&t, &c, y, CTL, ch, sh);
            for (i = 0; i < CTL; i++) {
                double e = fabs((double)y[i] - x[i]);
                d = e > d ? e : d;
            }
        }
    }
#endif
    return d / (60000.0 * (1 << sh));
}
static void t_start_click(void)
{
    static const char *const W[3] = {"a part's FILTER", "the drum track's FILTER", "the master's DJ FILT"};
    uint32_t w;
    for (w = 0; w < 3u; w++) {
        double r;
        if (w < 2u && !FELUCCA_TRK_FILT)
            continue;
        if (w == 2u && !FELUCCA_FX_DJF)
            continue;
        r = start_jump(w);
        printf("trk_filt: %s turned to a low-pass under a sounding sine: its first block off the sine by %.3f of its peak at most\n", W[w], r);
        check(r < 0.05, w == 0 ? "a part: a low-pass starting under a held note does not click (SLOOP 2.6)"
                     : w == 1 ? "the drum track: a low-pass starting under the drums does not click (SLOOP 2.6)"
                              : "the master DJ FILT: a low-pass starting under the mix does not click (SLOOP 2.6)");
    }
}

static uint32_t mix_hash(void)
{
    uint32_t f, i, h = 2166136261u;
    int32_t o[2 * CTL];
    busy_song(7u, 1);
    for (f = 0; f < 4u * FS; f += CTL) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            h = (h ^ (uint32_t)o[i]) * 16777619u;
    }
    return h;
}

#if FELUCCA_TRK_FILT
static void t_project(void)
{
    static project_t pj, pj0;
    static dlrec_t dl;
    static const int16_t F[NTRK] = {-30, 63, 0, -64}, S[NPART] = {-60, 0, 45}, V[NPART] = {1, 0, 1};
    uint32_t k;
    int ok = 1;
    host_tracks_init();
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_TFLT] = trk[k].p[P_STRUM] = trk[k].p[P_VLEAD] = 0;
    proj_capture(&pj0, &dl);
    check(pj0.t[TRK_DRUM].p[P_A2SDTN] == TP[P_A2SDTN].def && pj0.t[TRK_DRUM].p[P_GLMODE] == 0,
          "project: all at 0: the drum track's slots as every build writes them (no tag)");
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_TFLT] = F[k];
        if (k < NPART)
            trk[k].p[P_STRUM] = S[k], trk[k].p[P_VLEAD] = V[k];
    }
    proj_capture(&pj, &dl);
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_TFLT] = trk[k].p[P_STRUM] = trk[k].p[P_VLEAD] = 0;
    proj_apply(&pj, &dl, 1);
    for (k = 0; k < NTRK; k++) {
        ok &= trk[k].p[P_TFLT] == F[k];
        if (k < NPART)
            ok &= trk[k].p[P_STRUM] == S[k] && trk[k].p[P_VLEAD] == V[k];
    }
    ok &= TDRUM->p[P_GLMODE] == TP[P_GLMODE].def && TDRUM->p[P_DETUNE] == TP[P_DETUNE].def && TDRUM->p[P_A2SDTN] == TP[P_A2SDTN].def;
    check(ok, "project: FILT of each track, STRUM and VLEAD of each part saved and loaded (drum slots back to theirs)");
    pj0.t[TRK_DRUM].p[P_GLMODE] = 1, pj0.t[TRK_DRUM].p[P_DETUNE] = 90;   /* a project of before: its drum values */
    pj0.sum = proj_sum(&pj0);
    trk[0].p[P_TFLT] = 9;
    proj_apply(&pj0, &dl, 1);
    check(trk[0].p[P_TFLT] == 0 && TDRUM->p[P_GLMODE] == 1 && TDRUM->p[P_DETUNE] == 90,
          "project: one of before (no tag): FILT 0, the drum track's own values as they were");
}

static void t_cost(void)
{
    uint32_t m, f, k;
    double ipc[2] = {0, 0};
    uint64_t a, b;
    int32_t o[2 * CTL];
    for (m = 0; m < 2u; m++) {
        busy_song(7u, 1);
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_TFLT] = (int16_t)(m ? (k & 1u ? 40 : -40) : 0);
        for (f = 0; f < FS; f += CTL)
            mix_block(o, CTL);
        a = insn();
        for (f = 0; f < 4u * FS; f += CTL)
            mix_block(o, CTL);
        b = insn();
        ipc[m] = (b - a) / (4.0 * FS);
    }
    if (ipc[0] > 0)
        printf("trk_filt: cost: the busy mix %.1f instructions / sample with every FILT at 0, %.1f with all four engaged "
               "(+%.1f: ~%.1f a part, the drum bus 5 channels)\n", ipc[0], ipc[1], ipc[1] - ipc[0], (ipc[1] - ipc[0]) / 8.0);
}

static void sweep(const char *dir)
{
    char path[600];
    FILE *w;
    const uint32_t frames = 12u * FS;
    uint32_t f, i, k;
    snprintf(path, sizeof path, "%s/trk-filter-sweep.wav", dir);
    if (!(w = fopen(path, "wb"))) {
        printf("trk_filt: cannot write %s\n", path);
        fails++;
        return;
    }
    busy_song(7u, 1);
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL], v;
        double t = (double)f / FS;                      /* 0-1 s open, 1-5 LP closing and back, 6-10 HP, open */
        v = t < 1 || t >= 11 ? 0 : t < 6 ? (int32_t)(-64 * sin((t - 1) / 5 * M_PI)) : (int32_t)(63 * sin((t - 6) / 5 * M_PI));
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_TFLT] = (int16_t)v;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(w, o[2 * i], o[2 * i + 1]);
    }
    fclose(w);
    printf("trk_filt: wrote %s (every track: 0, LP to -64 and back, HP to +63 and back, 0)\n", path);
}

#endif
int main(int argc, char **argv)
{
    const char *dir = getenv("RENDER");
    if (argc > 1 && !strcmp(argv[1], "hash")) {
        printf("%08x\n", mix_hash());
        return 0;
    }
#if FELUCCA_TRK_FILT
    fxs_load(1, FXT_FILT);                              /* (decision D6: the FILTER is heard in an FX slot: S2's) */
    t_part();
    t_drums();
    t_project();
    t_cost();
    t_start_click();
    if (dir)
        sweep(dir);
#else
    (void)dir;
#endif
    printf("trk_filt: %d failed\n", fails);
    return fails != 0;
}
