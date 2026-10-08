/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4's quick chain (FELUCCA_QCHAIN, seq.c live_block chain_*, arranger_scene.c section_bars), with the four
 * project slots: sections A (1 bar), B (2 bars: LEN 32), C (DIV 1BAR, LEN 1) chained A B C: each plays its bars, in
 * order, looped; STOP ends the chain. Exit status: the number of failed checks. */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? i : 0; }
#include "../firmware/src/storage/project.c"
static struct { uint8_t force; } ui;
static uint8_t sync_reload;
#include "../firmware/src/seq/arranger_scene.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("chain: %-74s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void capture(uint32_t slot)
{
    project_t *p = &proj_slot[slot];
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC; p->size = sizeof *p;
    memcpy(p->g, song.g, sizeof song.g);
    for (i = 0; i < NTRK; i++) {
        memcpy(p->t[i].p, trk[i].p, sizeof trk[i].p);
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
    }
    p->sum = proj_sum(p);
}
static void section(uint32_t slot, uint32_t lane, uint32_t div, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        steps_clear(&trk[i]);
        trk[i].p[P_SLEN] = 16;
        trk[i].p[P_SDIV] = 2;
    }
    TDRUM->p[P_SDIV] = (int16_t)div;
    TDRUM->p[P_SLEN] = (int16_t)len;
    dstep_set(&TDRUM->dstep[0], lane, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[16], lane, LV_NORM, 0);          /* (B's second bar: the same) */
    capture(slot);
}

int main(void)
{
    static const uint8_t WANT[9] = {0, 1, 1, 2, 0, 1, 1, 2, 0};   /* the section of bars 0..8 */
    uint32_t bar, ok = 1, seen[9];
    int32_t out[CTL * 2];
    host_tracks_init();
    song.g[G_BPM] = 120;
    section(0, 0, 2, 16);                                    /* A: a kick, 1 bar */
    section(1, 2, 2, 32);                                    /* B: a snare, 2 bars */
    section(2, 4, 7, 1);                                     /* C: a hat, one step of a bar */
    check(section_bars(0) == 1 && section_bars(1) == 2 && section_bars(2) == 1 && section_bars(3) == 1,
          "the bars of each section: its longest pattern (A 1, B 2, C 1BAR x 1: 1; empty: 1)");
    proj_apply(&proj_slot[0], &proj_dl[0], 1);
    live_sec = 0;
    transport_req = 1;
    chain_sec[0] = 0, chain_sec[1] = 1, chain_sec[2] = 2;
    chain_bar[0] = (uint8_t)section_bars(0), chain_bar[1] = (uint8_t)section_bars(1), chain_bar[2] = (uint8_t)section_bars(2);
    chain_i = 0;
    chain_n = 3;
    for (bar = 0; bar < 9u; bar++)
        seen[bar] = 99;
    {
        uint32_t blk, bar_blk = 4u * BEAT_U / 120u / CTL;   /* (120 BPM: a bar is 88200 samples) */
        for (blk = 0; blk < 9u * bar_blk; blk++) {
            uint32_t a = drums.age, k;
            mix_block(out, CTL);
            if (drums.age != a)
                for (k = 0; k < NDRUM; k++)
                    if (drums.v[k].age > a && seen[blk / bar_blk] == 99u)
                        seen[blk / bar_blk] = drums.v[k].note == 36 ? 0 : drums.v[k].note == 38 ? 1 : drums.v[k].note == 42 ? 2 : 98;
        }
    }
    for (bar = 0; bar < 9u; bar++)
        ok &= seen[bar] == WANT[bar];
    if (!ok)
        printf("chain:   bars: %u %u %u %u %u %u %u %u %u\n", seen[0], seen[1], seen[2], seen[3], seen[4], seen[5], seen[6], seen[7], seen[8]);
    check(ok, "A B C chained: A, B twice, C, then A again (looped)");
    transport_req = 2;
    mix_block(out, CTL);
    check(chain_n == 0, "STOP ends the chain");
    printf("chain: %d failed\n", fails);
    return fails;
}
