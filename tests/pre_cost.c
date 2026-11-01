/* SPDX-License-Identifier: GPL-3.0-only */
/* PRE's cost on the host (dual.c mix_block_dual: CPU0 before the fork), the user's project of 2026-11-01 (T1 ANALOG
 * 808 BOOM, T2 DIGITAL RHODES, T3 ANALOG 808 BOOM, the drums on the 808 kit) playing a pattern from the sequencer.
 * Each block is mix_block's (fx.c) steps one by one, each counted in instructions (proc_pid_rusage, the kernel's
 * count; the call's own cost taken off): the clear, the events (events_block with the LFOs on what the sequencer
 * reads), the duck, voices_busy and the LFOs' draws, the old sounds' fade; then the jobs (the drums, each part).
 * Prints each step's average and its largest block, the blocks over PRE_LIM, and each engine's note start
 * (trk_note_on from rest, a note and a 4-note chord) and first render.
 *   build/host/pre_cost [KIT_UID]   cc -O2 -w -I$HGEN -Ifirmware/src -Ifirmware/hal $(python3 tools/config_flags.py PROFILE)
 *   (sh: the flags split into words; zsh: ${=F}). env PRE_RUNS=n runs (default 5: the least count a block: the host's
 *   own noise only adds), PRE_LOG=n prints every block whose PRE passes n instructions
 * argv[1] "hash": the mix's FNV-1a over 30 s of the project instead (the bit-exact check of a change). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static uint64_t ic(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static uint64_t ic_over;

static uint32_t preset_named(uint32_t e, const char *name)
{
    uint32_t i;
    for (i = 0; i < ENGINES[e]->npresets; i++)
        if (!strcmp(ENGINES[e]->presets[i].name, name))
            return i;
    fprintf(stderr, "pre_cost: no preset %s\n", name);
    exit(2);
}

static void project(uint32_t kit)
{
    static const uint8_t BASS[16] = {36, 0, 0, 36, 0, 0, 39, 0, 36, 0, 0, 43, 0, 41, 0, 0};
    static const uint8_t BASS3[16] = {0, 0, 48, 0, 0, 0, 0, 51, 0, 0, 48, 0, 0, 0, 46, 0};
    static const uint8_t CH1[4] = {60, 63, 67, 70}, CH2[4] = {58, 62, 65, 68};
    uint32_t i, ea = eng_slot_built(0), ed = eng_slot_built(1);
    host_tracks_init();
    host_preset(&trk[0], ea, preset_named(ea, "808 BOOM"));
    host_preset(&trk[1], ed, preset_named(ed, "RHODES"));
    host_preset(&trk[2], ea, preset_named(ea, "808 BOOM"));
    TDRUM->p[P_E0] = (int16_t)kit;
    for (i = 0; i < 16u; i++) {
        uint8_t n = BASS[i], m = BASS3[i];
        put_step(&trk[0], i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, 0);
        put_step(&trk[2], i, m ? 1u : 0u, &m, m ? ST_NOTE : ST_REST, 0);
        put_step(&trk[1], i, i % 4u == 0u ? 4u : 0u, i % 8u ? CH2 : CH1, i % 4u == 0u ? ST_NOTE : ST_REST, 0);
    }
    for (i = 0; i < 16u; i++) {
        uint8_t n[4];
        uint32_t k = 0;
        if (i % 4u == 0u || i == 10u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38;
        n[k++] = i == 14u ? 46 : 42;
        put_step(TDRUM, i, k, n, ST_NOTE, i % 4u == 0u ? SF_ACCENT : 0u);
    }
    for (i = 0; i < 64u; i++) {                             /* (the engines switched: the fade over) */
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        host_main_services();
    }
}

enum { S_CLR, S_EVT, S_DUCK, S_LFO, S_POLD, S_PRE, S_DRUMS, S_P0, S_P1, S_P2, S_N };
static const char *const SN[S_N] = {"clear", "events", "duck", "busy+lfo", "pold_in", "PRE", "drums", "part 1", "part 2",
                                    "part 3"};

/* mix_block (fx.c) step by step; c[] each step's count */
static void block(int32_t *out, uint32_t n, uint64_t *c)
{
    uint64_t t;
    uint32_t i, sw, p;
    t = ic();
#if FELUCCA_USB_AUDIO
    capture_clear(n);
#endif
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    c[S_CLR] = ic() - t;
    t = ic();
    {
        uint32_t sq = lfo_seq_any() ? lfo_seq_in() : 0u;
        events_block(n);
        if (sq)
            lfo_seq_out();
    }
    c[S_EVT] = ic() - t;
    t = ic();
#if FELUCCA_MACROS
    mac_pre();
#endif
    if (FELUCCA_FX_DUCK)
        duck_block(n * (uint32_t)song.g[G_BPM]);
    c[S_DUCK] = ic() - t;
    t = ic();
    {
        volatile uint32_t vb = voices_busy(), r = 0;        /* (dual.c: dual_vbusy, dual_lfo_draw: draws read, not kept) */
        for (p = 0; p < NPART; p++)
            if (lfo1_next(&trk[p]) < trk[p].lfo_ph)
                r += p;
        (void)vb, (void)r;
    }
    c[S_LFO] = ic() - t;
    t = ic();
    sw = pold_any() ? pold_in() : 0u;
    c[S_POLD] = ic() - t;
    t = ic();
#if FELUCCA_DRUMS
    drums.a0 = TDRUM->att;
    drums.a1 = 32767 - gain_next(TDRUM);
    slicer_drums(mix_l, mix_r, send_r, n);
    dbus_run(n);
#endif
    c[S_DRUMS] = ic() - t;
    for (p = 0; p < NPART; p++) {
        t = ic();
        mix_part(&trk[p], n);
        c[S_P0 + p] = ic() - t;
    }
    if (sw)
        pold_out(sw);
#if FELUCCA_USB_AUDIO
    ut_mix(n);
#endif
    mix_finish(out, n);
#if FELUCCA_MACROS
    mac_post();
#endif
    for (i = 0; i < S_PRE; i++)
        c[i] = c[i] > ic_over ? c[i] - ic_over : 0;
    for (i = S_DRUMS; i < S_N; i++)
        c[i] = c[i] > ic_over ? c[i] - ic_over : 0;
    c[S_PRE] = c[S_CLR] + c[S_EVT] + c[S_DUCK] + c[S_LFO] + c[S_POLD];
}

/* a part's note start from rest: trk_note_on (PRE) and the first render (its job), the least of 4 */
static void note_start(const char *what, uint32_t e, uint32_t pi, uint32_t nn)
{
    static const uint8_t N[4] = {48, 51, 55, 58};
    track_t *t = &trk[0];
    uint64_t best_on = ~0ull, best_r = ~0ull, a;
    uint32_t r, i, j;
    int32_t o[2 * CTL];
    host_preset(t, e, pi);
    for (r = 0; r < 4u; r++) {
        for (i = 0; i < 200u; i++) {                       /* (back to rest: every note off, the release out) */
            if (i == 0u)
                trk_all_off(t);
            mix_block(o, CTL);
        }
        a = ic();
        for (j = 0; j < nn; j++)
            trk_note_on(t, N[j], 100u);
        a = ic() - a;
        best_on = a < best_on ? a : best_on;
        a = ic();
        mix_part(t, CTL);
        a = ic() - a;
        best_r = a < best_r ? a : best_r;
    }
    printf("note start %-16s %u note%s: trk_note_on %6llu instr, first render %6llu\n", what, nn, nn > 1 ? "s" : " ",
           (unsigned long long)(best_on - ic_over), (unsigned long long)(best_r - ic_over));
}

/* a drum hit's start (trk_note_on on the drum track: PRE) and the drums' next render, the least of 4 */
static void drum_start(uint32_t kit, uint32_t note)
{
    uint64_t best_on = ~0ull, best_r = ~0ull, a;
    uint32_t r, i;
    int32_t o[2 * CTL];
    TDRUM->p[P_E0] = (int16_t)kit;
    for (r = 0; r < 4u; r++) {
        for (i = 0; i < 400u; i++)
            mix_block(o, CTL);
        a = ic();
        trk_note_on(TDRUM, note, 100u);
        a = ic() - a;
        best_on = a < best_on ? a : best_on;
        a = ic();
        mix_block(o, CTL);
        a = ic() - a;
        best_r = a < best_r ? a : best_r;
    }
    printf("drum hit   kit %2u note %2u: trk_note_on %6llu instr, the next block (all of it) %6llu\n", kit, note,
           (unsigned long long)(best_on - ic_over), (unsigned long long)(best_r - ic_over));
}

int main(int argc, char **argv)
{
    uint32_t kit = argc > 1 && argv[1][0] != 'h' ? (uint32_t)atoi(argv[1]) : DRUM_UID_X808, b, i, nb = 30u * FS / CTL;
    uint64_t c[S_N], sum[S_N] = {0}, mx[S_N] = {0}, mxat[S_N] = {0}, worst[S_N] = {0}, wpre = 0, a;
    int32_t o[2 * CTL];
    for (i = 0, ic_over = ~0ull; i < 64u; i++) {
        a = ic();
        a = ic() - a;
        ic_over = a < ic_over ? a : ic_over;
    }
    project(kit);
    if (argc > 1 && argv[1][0] == 'h') {                   /* the mix's hash, 30 s */
        uint64_t h = 1469598103934665603ull;
        transport_req = 1;
        for (b = 0; b < nb; b++) {
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++)
                h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
        }
        printf("pre_cost: mix hash %016llx\n", (unsigned long long)h);
        return 0;
    }
    printf("pre_cost: kit %u (drum_kit %u, X0X %u), %u BPM, 30 s, the least of PRE_RUNS runs a block, the counter's own %llu instr taken off\n",
           kit, drum_kit(), (unsigned)DRUM_IS_X0X(drum_kit()), song.g[G_BPM], (unsigned long long)ic_over);
    {
        uint32_t runs = getenv("PRE_RUNS") ? (uint32_t)atoi(getenv("PRE_RUNS")) : 5u, r, k;
        uint32_t *best = malloc(sizeof(uint32_t) * nb * S_N), *got = malloc(sizeof(uint32_t) * nb * S_N);
        for (k = 0; k < nb * S_N; k++)
            best[k] = ~0u;
        for (r = 0; r < runs; r++) {                       /* (each run in a child: the same state from the same start) */
            int fd[2];
            pid_t pid;
            if (pipe(fd))
                return 1;
            fflush(stdout);
            if (!(pid = fork())) {
                close(fd[0]);
                transport_req = 1;
                for (b = 0; b < nb; b++) {
                    block(o, CTL, c);
                    for (i = 0; i < S_N; i++)
                        got[b * S_N + i] = (uint32_t)c[i];
                }
                for (k = 0; k < nb * S_N * 4u;) {
                    ssize_t w = write(fd[1], (char *)got + k, nb * S_N * 4u - k);
                    if (w <= 0)
                        _exit(1);
                    k += (uint32_t)w;
                }
                _exit(0);
            }
            close(fd[1]);
            for (k = 0; k < nb * S_N * 4u;) {
                ssize_t w = read(fd[0], (char *)got + k, nb * S_N * 4u - k);
                if (w <= 0)
                    return 1;
                k += (uint32_t)w;
            }
            close(fd[0]);
            waitpid(pid, 0, 0);
            for (k = 0; k < nb * S_N; k++)
                best[k] = got[k] < best[k] ? got[k] : best[k];
        }
        for (b = 0; b < nb; b++) {
            for (i = 0; i < S_N; i++) {
                c[i] = best[b * S_N + i];
                sum[i] += c[i];
                if (b && c[i] > mx[i])                     /* (block 0: PLAY's start, once) */
                    mx[i] = c[i], mxat[i] = b;
            }
            if (getenv("PRE_LOG") && b && c[S_PRE] > (uint64_t)atoi(getenv("PRE_LOG")))
                printf("blk %5u evt %6llu pold %6llu lfo %6llu duck %6llu clear %6llu\n", b, (unsigned long long)c[S_EVT],
                       (unsigned long long)c[S_POLD], (unsigned long long)c[S_LFO], (unsigned long long)c[S_DUCK],
                       (unsigned long long)c[S_CLR]);
            if (b && c[S_PRE] > wpre) {
                wpre = c[S_PRE];
                memcpy(worst, c, sizeof c);
            }
        }
        free(best), free(got);
    }
    printf("%-9s %10s %10s %8s %10s\n", "step", "avg", "max", "at blk", "in PRE max");
    for (i = 0; i < S_N; i++)
        printf("%-9s %10.0f %10llu %8llu %10llu\n", SN[i], (double)sum[i] / nb, (unsigned long long)mx[i],
               (unsigned long long)mxat[i], (unsigned long long)worst[i]);
    {
        uint32_t ea = eng_slot_built(0), ed = eng_slot_built(1);
        note_start("ANALOG 808 BOOM", ea, preset_named(ea, "808 BOOM"), 1);
        note_start("ANALOG 808 BOOM", ea, preset_named(ea, "808 BOOM"), 4);
        note_start("DIGITAL RHODES", ed, preset_named(ed, "RHODES"), 1);
        note_start("DIGITAL RHODES", ed, preset_named(ed, "RHODES"), 4);
        host_tracks_init();
        drum_start(kit, 36), drum_start(kit, 38), drum_start(kit, 42);
    }
    return 0;
}
