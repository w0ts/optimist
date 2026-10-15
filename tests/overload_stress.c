/* SPDX-License-Identifier: GPL-3.0-only */
/* The chord-burst stress (docs/CPU-GUARD.md "Spikes"): the mix as the audio ISR renders it, a DMA half (HALF_FRAMES) at a
 * time, each half's cost counted by the kernel (proc_pid_rusage ri_instructions, instructions a sample; macOS), while the
 * keys are played as fast as a player recording live would: chords of 3..5 keys and runs of single notes on a POLY pad,
 * a bass line, the drums on 16ths (the 8-voice budget full most of the time: every new note steals one).
 *   build/host/overload_stress [SECS]       (tests/run_tests.sh builds it; FELUCCA_CPU_GUARD=1)
 * Prints the per-half cost distribution (mean, p50, p99, max: a spike over the mean), the cost of halves by the notes
 * started in them, then the guard fed with those costs as the ISR would (the clock set so the mean is LOAD_PCT of a half,
 * the host's instruction counts standing for the target's): how many halves run over 100 % (late, a drop-out), with the
 * guard's reactive path off (CG_REACT 0: as before) and on. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <libproc.h>
#include <sys/resource.h>
#include "../firmware/src/system/cpuguard.c"

#define LOAD_PCT 66u                     /* the mean load the clock is set for (the user's log: 63..67 % while recording) */

static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
    return 0;
}

static uint32_t os_seed = 12345u;
static uint32_t os_rnd(uint32_t n)
{
    os_seed = os_seed * 1103515245u + 12345u;
    return (os_seed >> 16) % n;
}

typedef struct {
    uint8_t note, part;
    uint32_t off;                        /* the half it is let go in */
} held_t;
static held_t held[64];
static uint32_t nheld;

static void setup(void)
{
    host_tracks_init();
    memset(&cg, 0, sizeof cg);
    host_preset(&trk[0], ENG_SLOT_ANALOG, 17);       /* the played part: SUPER PAD (swarm), POLY */
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_AMODE] = 0;
#if FELUCCA_ENG_FM6
    host_preset(&trk[1], ENG_IX_FM6, 0);             /* keys: FM6, POLY */
#else
    host_preset(&trk[1], ENG_SLOT_ANALOG, 13);
#endif
    trk[1].p[P_VOICE] = V_POLY;
    trk[1].p[P_AMODE] = 0;
    host_preset(&trk[2], ENG_SLOT_ANALOG, 2);        /* the bass: MONO */
    trk[2].p[P_VOICE] = V_MONO;
    trk[2].p[P_AMODE] = 0;
    nheld = 0;
    os_seed = 12345u;
}

/* the keys at half h (5.8 ms each): a chord every ~120 ms on part 1 or 2, a run of single notes between, the bass on 8ths,
 * the drums on 16ths. Returns the notes started */
static uint32_t play(uint32_t h)
{
    uint32_t i, n = 0, f = h * HALF_FRAMES;
    for (i = 0; i < nheld;)
        if (held[i].off <= h) {
            trk_note_off(&trk[held[i].part], held[i].note);
            held[i] = held[--nheld];
        } else {
            i++;
        }
    if (f % (FS / 8u) < HALF_FRAMES)                  /* 16ths at 120 BPM */
        drum_on(f % (FS / 2u) < HALF_FRAMES ? 36u : (f / (FS / 8u)) % 4u == 2u ? 38u : 42u, 100u);
    if (f % (FS / 4u) < HALF_FRAMES) {                 /* the bass on 8ths */
        trk_note_on(&trk[2], 36u + os_rnd(12), 100);
        n++;
    }
    if (h % 21u == 0u) {                               /* a chord, 3..5 keys, held 100..400 ms */
        uint32_t k = 3u + os_rnd(3), p = os_rnd(2), root = 48u + os_rnd(12);
        for (i = 0; i < k && nheld < 64u; i++) {
            uint32_t note = root + 3u * i + os_rnd(2);
            trk_note_on(&trk[p], note, 90u + os_rnd(30));
            held[nheld].note = (uint8_t)note, held[nheld].part = (uint8_t)p, held[nheld].off = h + 17u + os_rnd(52);
            nheld++, n++;
        }
    } else if (h % 7u == 3u && nheld < 64u) {          /* a single note between, held 50..150 ms */
        uint32_t note = 60u + os_rnd(24), p = os_rnd(2);
        trk_note_on(&trk[p], note, 100);
        held[nheld].note = (uint8_t)note, held[nheld].part = (uint8_t)p, held[nheld].off = h + 9u + os_rnd(17);
        nheld++, n++;
    }
    return n;
}

static double half_cost(void)                          /* one half rendered: instructions a sample */
{
    int32_t o[2 * CTL];
    uint32_t b;
    uint64_t i0 = instr_now();
    for (b = 0; b < HALF_FRAMES; b += CTL)
        mix_block(o, CTL);
    return (double)(instr_now() - i0) / HALF_FRAMES;
}

static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* the FM-1's own spikes, modelled (the host's render is flat, the hardware's is not: the user's log has 1,567 late halves at a 66 %
 * mean, a steady 12..17 a second, the longest half 133 %): a stand-in for what the host cannot show (other interrupts, flash
 * cache misses), to see what the guard does with them. Mode 1: 12 % of the halves at random (a fixed sequence) + 50 % of the
 * mean (clusters: two in a row now and then); mode 2: every 11th half + 55 % (isolated: never two in a row, 15.7 a second, as
 * the log's steady rate); mode 3: every 11th half starts 40 % of a half late (the interrupt held off: another interrupt, a
 * window with interrupts off) and renders as it would: late when its wait and its render pass the half; the ISR's own time
 * (all the guard measured before) shows nothing */
static uint32_t sp_seed, sp_n;
static int spike_now(int mode)
{
    sp_seed = sp_seed * 1664525u + 1013904223u;
    sp_n++;
    return mode == 1 ? (sp_seed >> 8) % 100u < 12u : sp_n % 11u == 0u;
}
#define SPIKE_ADD(mode) ((mode) == 1 ? 0.50 : (mode) == 2 ? 0.55 : 0.0)
#define SPIKE_WAIT(mode) ((mode) == 3 ? 40u : 0u)     /* % of a half the interrupt starts late */

/* the stress: guard 0 none (costs only), 1 the guard without its reactive path (as before), 2 with it; spikes: the model
 * above on. Late halves (over 100 %); the costs into c, the notes started into nstart; *top the highest level */
static uint32_t os_run(uint32_t nh, uint32_t khz, int guard, int spikes, double mean, double *c, uint32_t *nstart,
                       uint32_t *top, double *load)
{
    uint32_t h, late = 0, mx = 0;
    double sum = 0;
    setup();
    cpu_khz = guard ? khz : 0u;
    cg_react = (uint8_t)(guard == 2);
    sp_seed = 777u;
    sp_n = 0;
    for (h = 0; h < 200u; h++) {                       /* (settle: the first attacks, the reverb's tank) */
        play(h);
        half_cost();
    }
    for (h = 0; h < nh; h++) {
        uint32_t ns = play(200u + h), us;
        double ipc;
        if (guard)
            cg_pre();
        ipc = half_cost();
        uint32_t wait = 0;
        if (spikes && spike_now(spikes)) {
            ipc += mean * SPIKE_ADD(spikes);
            wait = CG_HALF_US * SPIKE_WAIT(spikes) / 100u;
        }
        us = khz ? (uint32_t)(ipc * CG_HALF_US / (khz * 1000.0 / FS)) : 0u;
        late += khz && us + wait >= CG_HALF_US;
        sum += us;
        if (guard) {
            cg.late = (uint8_t)(us + wait >= CG_HALF_US);
            cg_post(us);
            mx = cg.level > mx ? cg.level : mx;
        }
        if (c)
            c[h] = ipc;
        if (nstart)
            nstart[h] = ns;
    }
    if (top)
        *top = mx;
    if (load)
        *load = khz ? 100.0 * sum / nh / CG_HALF_US : 0;
    return late;
}

int main(int argc, char **argv)
{
    uint32_t secs = argc > 1 ? (uint32_t)atoi(argv[1]) : 20u, nh = secs * FS / HALF_FRAMES, h, k, khz, sp;
    uint32_t *ns = calloc(nh, sizeof *ns);
    double *c = calloc(nh, sizeof *c), *s = calloc(nh, sizeof *s), sum = 0, by[6] = {0}, byn[6] = {0};
    if (!c || !s || !ns || !instr_now()) {
        printf("overload_stress: no instruction counter on this host (proc_pid_rusage)\n");
        return 0;
    }
    os_run(nh, 0, 0, 0, 0, c, ns, 0, 0);
    for (h = 0; h < nh; h++) {
        sum += c[h];
        s[h] = c[h];
        k = ns[h] < 5u ? ns[h] : 5u;
        by[k] += c[h], byn[k]++;
    }
    qsort(s, nh, sizeof *s, cmpd);
    printf("overload_stress: %u halves (%u s), instructions a sample per half: mean %.0f, p50 %.0f, p90 %.0f, p99 %.0f, max %.0f "
           "(max / mean %.2f)\n", nh, secs, sum / nh, s[nh / 2u], s[nh * 9u / 10u], s[nh * 99u / 100u], s[nh - 1u],
           s[nh - 1u] / (sum / nh));
    for (k = 0; k < 6u; k++)
        if (byn[k])
            printf("  halves starting %u%s notes: %5.0f of them, mean %.0f (%.2f x the overall mean)\n", k, k == 5u ? "+" : "",
                   byn[k], by[k] / byn[k], by[k] / byn[k] / (sum / nh));
    khz = (uint32_t)(sum / nh * FS / 1000.0 * 100.0 / LOAD_PCT);   /* the clock that puts the mean at LOAD_PCT */
    printf("  the clock for a %u %% mean: %u kHz (%u target instructions a sample)\n", LOAD_PCT, khz, khz * 1000u / FS);
    for (sp = 0; sp < 4u; sp++) {
        static const char *const G[3] = {"no guard              ", "the guard as before   ", "the guard + reactive  "};
        uint32_t g;
        static const char *const M[4] = {"the host's costs alone", "spikes modelled: 12 % of the halves at random, + 50 % of the mean",
                                         "spikes modelled: every 11th half (isolated, 15.7 / s), + 55 % of the mean",
                                         "every 11th half's interrupt held off 40 % of a half (its render as usual)"};
        printf("  %s:\n", M[sp]);
        for (g = 0; g < 3u; g++) {
            uint32_t top = 0, late;
            double load = 0;
            late = os_run(nh, khz, (int)g, (int)sp, sum / nh, 0, 0, &top, &load);
            printf("    %s %5u late halves (%5.2f %%), mean load %4.1f %%", G[g], late, 100.0 * late / nh, load);
            if (g)
                printf(", %u steps (%u early), %u reactions, %u voices shed, top level %u, cap %u",
                       cg.steps, cg.early, cg.reacts, cg.sheds, top, cg.vcap);
            printf("\n");
        }
    }
    return 0;
}
