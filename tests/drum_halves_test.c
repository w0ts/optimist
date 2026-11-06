/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum track's voices split in two halves by lane (drums/drum_halves.c: a render job each, one a core) render the
 * same samples as the drum track as one job (slicer_drums, the order the voices always ran in), whatever the lanes'
 * split and whichever half runs first:
 *   material  a bar on all ten lanes with ratchets (voices overlapping, stolen: the declick tail), lanes panned, sends
 *             (REV DLY CHO) on some lanes, both insert slots on others, a lane muted while it sounds, the SLICER on the
 *             drums for a while (the one job again) and off (back to the halves); the sampled kit and two of its
 *             treatments (the filter, the envelope), a synthesised kit, the X0X 909 when built
 *   splits    the halves' own (dh_split, with and without its bias), every lane in half B, every lane in half A, even /
 *             odd lanes, a random split every block; half B rendered first on every other split
 *   compared  each block's output (mix_finish: the master after the buses and the drum bus), the drum stem (USB audio),
 *             the lanes' meters and the declick tail at the end: equal, word for word, to the one job's
 * Each render runs in a child from the same state. Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "../firmware/src/drums/drum_halves.c"

#define NBLK (3u * FS / CTL)                    /* 3 s */
#define MUTE_AT 700u                            /* lane 3 muted (its voices sounding: they stop, declicked) */
#define SLCR_ON 1200u                           /* the SLICER on the drums (one job), then off again */
#define SLCR_OFF 1700u
enum { S_ONE, S_SPLIT, S_BIAS, S_ALL_B, S_ALL_A, S_EVEN, S_RANDOM, S_N };
static const char *const S_NAME[S_N] = {"one job", "dh_split", "dh_split, bias 0.8", "every lane in half B",
                                        "every lane in half A", "even lanes A, odd B", "a random split each block"};
static int bad;
static uint32_t n_two;                          /* (the child's: blocks rendered as two halves) */
static void check(const char *what, int ok)
{
    printf("drum halves: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* one block as fx.c mix_block (single core), the drums by strategy s */
static void blk(uint32_t s, uint32_t b, uint32_t *rs, int32_t *out)
{
    uint32_t i, n = CTL;
#if FELUCCA_USB_AUDIO
    capture_clear(n);
#endif
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    {
        uint32_t sq = lfo_seq_any() ? lfo_seq_in() : 0u;
        events_block(n);
        if (sq)
            lfo_seq_out();
    }
#if FELUCCA_MACROS
    mac_pre();
#endif
    if (FELUCCA_FX_DUCK)
        duck_block(n * (uint32_t)song.g[G_BPM]);
    drums.a0 = TDRUM->att;
    drums.a1 = 32767 - gain_next(TDRUM);
    if (s == S_ONE) {
        slicer_drums(mix_l, mix_r, send_r, n);
        dbus_run(n);
    } else {
        uint32_t h = dh_pre(n, s == S_BIAS ? 205u : 0u);
        if (h == DH_TWO) {
            n_two++;
            *rs = *rs * 1664525u + 1013904223u;
            if (s == S_ALL_B)
                dh_lanes[1] = ~0u;
            else if (s == S_ALL_A)
                dh_lanes[1] = 0u;
            else if (s == S_EVEN)
                dh_lanes[1] = 0x2AAAAAAAu;
            else if (s == S_RANDOM)
                dh_lanes[1] = *rs >> 7;
            dh_lanes[0] = ~dh_lanes[1];
            if (s & 1u) {                       /* (half B first) */
                dh_job(1u, n);
                dh_job(0u, n);
            } else {
                dh_job(0u, n);
                dh_job(1u, n);
            }
        }
        if (h == DH_ONE) {
            slicer_drums(mix_l, mix_r, send_r, n);
            dbus_run(n);
        } else {
            dh_join(n);
            dbus_run(n);
        }
    }
    (void)b;
    for (i = 0; i < NPART; i++)
        mix_part(&trk[i], n);
#if FELUCCA_USB_AUDIO
    ut_mix(n);
#endif
    mix_finish(out, n);
#if FELUCCA_MACROS
    mac_post();
#endif
}

typedef struct {
    uint64_t h, hcap;                           /* every block's output; the drum stem */
    uint64_t energy;
    int32_t pk[DRUM_LANES], tail;
    uint32_t two;
} rs_t;

static rs_t render(uint32_t kit, uint32_t s)
{
    int fd[2];
    rs_t res;
    memset(&res, 0, sizeof res);
    if (pipe(fd))
        return res;
    if (!fork()) {
        int32_t o[CTL * 2];
        uint32_t b, i, l, rs = 12345u;
        close(fd[0]);
        host_tracks_init();
        TDRUM->p[P_SLEN] = 16;
        TDRUM->p[P_E0] = (int16_t)kit;
        TDRUM->p[P_PAN] = -10;
        song.g[G_DRLVL] = 110;
        for (i = 0; i < 16u; i++) {
            dstep_t *st = &TDRUM->dstep[i];
            memset(st, 0, sizeof *st);
            for (l = 0; l < DRUM_LANES; l++)
                if ((i + l) % (2u + l % 3u) == 0u)
                    dstep_set(st, l, (i + l) & 3u, (i * 3u + l) % 4u == 0u ? 2u : 0u);   /* (ratchets: overlaps) */
        }
        for (l = 0; l < DRUM_LANES; l++) {
            dsend[l] = dsend_word(l % 3u == 0u ? 12u : 0u, l % 4u == 1u ? 20u : 0u, l % 5u == 2u ? 16u : 0u);
            dlm_set_pan(l, (int32_t)l * 13 - 60);
            dins_amt[0][l] = dins_amt[1][l] = 0;
        }
        dins_amt[0][1] = 90, dins_amt[1][1] = 70;   /* (lane 1: DIST and DYN; lanes 4, 7: one each) */
        dins_amt[0][4] = 60;
        dins_amt[1][7] = 100;
        dlm_update();
        transport_req = 1;
        for (b = 0; b < NBLK; b++) {
            if (b == MUTE_AT) {
                dlm_set_mute(3, 1);
                dlm_update();
            }
#if FELUCCA_FX_SLICER
            if (b == SLCR_ON)
                TDRUM->p[P_SLCR] = SL_GATE;
            if (b == SLCR_OFF)
                TDRUM->p[P_SLCR] = SL_OFF;
#endif
            blk(s, b, &rs, o);
            for (i = 0; i < 2u * CTL; i++) {
                res.h = (res.h ^ (uint32_t)o[i]) * 1099511628211ull;
                res.energy += (uint64_t)(o[i] < 0 ? -o[i] : o[i]);
            }
#if FELUCCA_USB_AUDIO
            for (i = 0; i < CTL; i++)
                res.hcap = (res.hcap ^ (uint32_t)track_capture[i * NTRK + TRK_DRUM]) * 1099511628211ull;
#endif
        }
        memcpy(res.pk, dlm_pk, sizeof res.pk);
        res.tail = drums.tail;
        res.two = n_two;
        if (write(fd[1], &res, sizeof res) != sizeof res)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &res, sizeof res) != sizeof res)
        res.h = 2;
    close(fd[0]);
    wait(0);
    return res;
}

static void t_kit(uint32_t kit, const char *name)
{
    char w[160];
    rs_t ref = render(kit, S_ONE), r;
    uint32_t s;
    snprintf(w, sizeof w, "%s: the one job sounds", name);
    check(w, ref.energy > 0u && ref.h != 2u);
    for (s = S_SPLIT; s < S_N; s++) {
        r = render(kit, s);
        snprintf(w, sizeof w, "%s, %s: the one job's samples (%u blocks in two halves)", name, S_NAME[s], r.two);
        check(w, r.h == ref.h && r.hcap == ref.hcap && r.two > NBLK / 2u);
        snprintf(w, sizeof w, "%s, %s: the lanes' meters, the declick tail", name, S_NAME[s]);
        check(w, !memcmp(r.pk, ref.pk, sizeof r.pk) && r.tail == ref.tail);
    }
}

int main(void)
{
    t_kit(0u, "sampled kit");
    t_kit(1u, "sampled kit, its filter treatment");
    t_kit(2u, "sampled kit, its envelope treatment");
    if (drum_kit_built(DRUM_SAMPLED))
        t_kit(DRUM_SAMPLED, "synthesised kit");
#if DRUM_X0X
    if (drum_kit_built(DRUM_UID_X909))
        t_kit(DRUM_UID_X909, "X0X 909");
    if (drum_kit_built(DRUM_UID_X808))
        t_kit(DRUM_UID_X808, "X0X 808");
#endif
    printf(bad ? "drum halves: %d FAILED\n" : "drum halves: all ok\n", bad);
    return bad != 0;
}
