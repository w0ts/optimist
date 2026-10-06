/* SPDX-License-Identifier: GPL-3.0-only */
/* The audio path's skips (FELUCCA_SKIP, core.h): work left out because it cannot change a sample. run_tests.sh
 * builds this twice, with FELUCCA_SKIP=1 (the firmware) and FELUCCA_SKIP=0 (everything computed, as before), and
 * compares what both print: a hash of every scenario's output samples. Each scenario starts a sound with a
 * setting at its neutral value (where the skip applies), flips it to a working value in the middle of the sound
 * and back, lets the tails ring out to silence and plays again: a skip that left a state behind (a filter, a
 * delay line, a phase, a gain ramp) shows as a different hash after the flip.
 *   skip_test [VERBOSE]   prints "name hash" per scenario */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint64_t H;
static int32_t last_out[2u * CTL];
#define ES(u) eng_slot_built(u)
static void blocks(uint32_t nb)                  /* nb blocks of the mix into the hash (FNV-1a) */
{
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        mix_block(last_out, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            H ^= (uint32_t)last_out[i];
            H *= 0x100000001b3ull;
        }
    }
}
#define MS(x) ((uint32_t)(x) * (FS / CTL) / 1000u)   /* blocks in x ms */

static void start(void)
{
    H = 0xcbf29ce484222325ull;
    host_tracks_init();
}

/* the FX buses: sends at 0 (every bus idle, the wet skipped), then on mid-note, off, the tails out, again */
static void sc_buses(void)
{
    track_t *t = &trk[0];
    uint32_t k;
    host_preset(t, (uint32_t)ES(0), 13);
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    TDRUM->p[P_REV] = 0;
    song.g[G_DRREV] = 0;
    blocks(MS(50));                                /* silence: all idle */
    trk_note_on(t, 60, 100);
    trk_note_on(t, 67, 90);
    blocks(MS(200));                               /* sound, no send */
    for (k = 0; k < 3u; k++) {                     /* one bus at a time, then all */
        t->p[P_CHOR + k] = 90;
        blocks(MS(120));
        t->p[P_CHOR + k] = 0;
        blocks(MS(80));
    }
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 100;
    blocks(MS(150));
    trk_note_off(t, 60);
    trk_note_off(t, 67);
    blocks(MS(100));
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;  /* the tails ring out to exact 0, the buses go idle */
    blocks(MS(6000));
    trk_note_on(t, 64, 100);
    t->p[P_DLY] = 70;
    blocks(MS(300));
    trk_note_off(t, 64);
    blocks(MS(800));
}

/* the drums with the track's reverb send off, then on, the FX bypass, mute */
static void sc_drums(void)
{
    uint32_t k;
    song.g[G_DRREV] = 0;
    for (k = 0; k < 24u; k++) {
        drum_on(k % 4u == 0u ? 36u : k % 4u == 2u ? 38u : 42u, 100u);
        if (k == 8u)
            song.g[G_DRREV] = 80;
        if (k == 14u)
            TDRUM->p[P_FXOFF] = 1;
        if (k == 16u)
            TDRUM->p[P_FXOFF] = 0, TDRUM->p[P_MUTE] = 1;
        if (k == 18u)
            TDRUM->p[P_MUTE] = 0;
        blocks(MS(125));
    }
    blocks(MS(4000));
}

static const struct {
    const char *name;
    void (*run)(void);
} SC[] = {
    {"buses", sc_buses},
    {"drums", sc_drums},
};

int main(int argc, char **argv)
{
    uint32_t i;
    (void)argv;
    for (i = 0; i < sizeof SC / sizeof SC[0]; i++) {
        pid_t pid;
        fflush(stdout);
        if (!(pid = fork())) {                     /* each from the boot state (as regress.c) */
            start();
            SC[i].run();
            printf("%-10s %016llx\n", SC[i].name, (unsigned long long)H);
            fflush(stdout);
            _exit(0);
        }
        waitpid(pid, 0, 0);
    }
    (void)argc;
    return 0;
}
