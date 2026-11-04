/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The output's correction by the headphone jack (fx/outcorr.c, system/hpjack.h): SPK LOWCUT (OFF / ON / BASS+) only while
 * nothing is in the jack, HPHN BASS (OFF / +3 / +6 dB) only while headphones are in, and only on the FM-1's own codec
 * output. Through the real mixer, each case from the same start (a child process each, the same notes): the codec's
 * output and what Bluetooth takes, beside the plain output (both settings OFF).
 *   - each setting only on its side of the jack, OFF never;
 *   - Bluetooth: the plain output bit for bit while it streams, silence for a block corrected before it started;
 *   - USB: MASTER (ua_mst) and the stems bit for bit whatever the settings and the jack;
 *   - the jack's debounce: a bouncing contact changes the state once, after HPJ_MS of a steady pin;
 *   - loud (into the limiter): the levels printed.
 * The sequence of a block is core/audio.c audio_block's (fm1-base's opt_core_render: the same calls). */
#include <assert.h>
#include <math.h>
#include <sys/mman.h>
#define FELUCCA_USB_AUDIO 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "../firmware/src/system/hpjack.h"

static int fails;
static void check(int ok, const char *what)
{
    printf("  %s %s\n", ok ? "ok" : "FAIL", what);
    fails += !ok;
}

#define NBLK 400u                                        /* ~0.3 s a case */
#define NS (2u * CTL * NBLK)
typedef struct {
    int32_t codec[NS], bt[NS], mst[NS], stem[CTL * NTRK * NBLK];
    int32_t bt_n;
} run_t;
typedef struct {
    uint8_t spk, hp, jack, bt;                           /* SPK LOWCUT, HPHN BASS, 1 headphones in, Bluetooth streams */
    int32_t vel;                                         /* the notes' velocity (loud: into the limiter) */
    uint32_t flip;                                       /* the block at which the jack changes (0: never) */
} case_t;

static void setup(int32_t vel)
{
    host_tracks_init();
    for (uint32_t k = 0; k < NPART; k++) host_preset(&trk[k], 0, 5);
    memset(drums.v, 0, sizeof drums.v);
    drums.tail = 0;
    dgl.on = 0;
    for (uint32_t k = 0; k < NPART; k++) trk_note_on(&trk[k], 33 + 12 * k, (uint32_t)vel);   /* (A1 55 Hz: a bass) */
    drum_on(36, (uint32_t)vel);
}

/* one block as core/audio.c audio_block runs it: the choice, the mix, USB (ua_route reads no output), Bluetooth, the
 * recorder, the codec's copy */
static uint8_t bt_streamed;
static void block(run_t *r, uint32_t b, const case_t *c)
{
    int32_t *out = r->codec + 2u * CTL * b;
    const int32_t *f;
    uint32_t i, muted = 0;
    oc_begin(bt_streamed, 0, CTL);
    mix_block(out, CTL);
    f = ua_route(out, CTL);
    memcpy(r->mst + 2u * CTL * b, ua_mst, sizeof(int32_t) * 2u * CTL);
    memcpy(r->stem + CTL * NTRK * b, track_capture, sizeof(int32_t) * CTL * NTRK);
    (void)f;
    if (c->bt) {                                         /* (bt_audio_feed: it streams, the own output muted) */
        const int32_t *p = oc_bt_frames(out);
        for (i = 0; i < 2u * CTL; i++)
            r->bt[2u * CTL * b + i] = p ? p[i] : 0;
        r->bt_n++;
        muted = 1;
    }
    bt_streamed = (uint8_t)muted;
    if (muted)
        for (i = 0; i < 2u * CTL; i++)
            out[i] = 0;
}

static run_t *run_case(const case_t *c)                  /* the case in a child, from the same start: its outputs */
{
    run_t *r = mmap(0, sizeof *r, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    pid_t pid;
    assert(r != MAP_FAILED);
    memset(r, 0, sizeof *r);
    fflush(stdout);                                      /* (the parent's buffer not copied into the child) */
    pid = fork();
    if (pid == 0) {
        setup(c->vel);
        track_capture_on = 1;
        ua_dst[UAS_MAIN] = UAD_12;                       /* (MASTER on 1 2: ua_mst made every block) */
        ua_route_apply();
        (void)oc_set_word(oc_word(1u, c->hp));  spk_lc = c->spk;   /* (the mechanism keeps OFF and BASS+: the settings only ever set ON) */
        hp_in = c->jack;
        for (uint32_t b = 0; b < NBLK; b++) {
            if (c->flip && b == c->flip)
                hp_in ^= 1u;                             /* (the jack changes during the run) */
            block(r, b, c);
        }
        fflush(stdout);
        _exit(0);
    }
    waitpid(pid, 0, 0);
    return r;
}
static int same(const int32_t *a, const int32_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && a[i] == b[i]; i++)
        ;
    if (i < n && getenv("OC_DIFF"))
        printf("    (first difference at sample %u, block %u: %d against %d)\n", i, i / (2u * CTL), b[i]);
    return i == n;
}
static double rms(const int32_t *a, uint32_t n)
{
    double s = 0;
    for (uint32_t i = 0; i < n; i++)
        s += (double)a[i] * a[i];
    return sqrt(s / n);
}
static int32_t peak(const int32_t *a, uint32_t n)
{
    int32_t p = 0;
    for (uint32_t i = 0; i < n; i++)
        p = abs(a[i]) > p ? abs(a[i]) : p;
    return p;
}

static void t_sides(void)
{
    static const case_t OFF = {0, 0, 0, 0, 90};
    run_t *ref = run_case(&OFF), *r;
    uint32_t j, s, h;
    for (j = 0; j < 2u; j++) {                           /* OFF never, either side */
        case_t c = {0, 0, (uint8_t)j, 0, 90};
        r = run_case(&c);
        check(same(r->codec, ref->codec, NS), j ? "both OFF, headphones in: the codec's output is the plain one"
                                                : "both OFF, nothing plugged: the codec's output is the plain one");
    }
    for (s = 1; s <= 2u; s++) {                          /* SPK LOWCUT: the speaker only */
        case_t c = {(uint8_t)s, 0, 0, 0, 90}, d = {(uint8_t)s, 0, 1, 0, 90};
        r = run_case(&c);
        check(!same(r->codec, ref->codec, NS), s == 1u ? "SPK LOWCUT ON, nothing plugged: the codec's output corrected"
                                                       : "SPK LOWCUT BASS+, nothing plugged: the codec's output corrected");
        check(same(r->mst, ref->mst, NS) && same(r->stem, ref->stem, CTL * NTRK * NBLK),
              "SPK LOWCUT on the speaker: USB's MASTER and stems bit for bit the plain ones");
        r = run_case(&d);
        check(same(r->codec, ref->codec, NS), s == 1u ? "SPK LOWCUT ON, headphones in (HPHN BASS OFF): the plain output"
                                                      : "SPK LOWCUT BASS+, headphones in (HPHN BASS OFF): the plain output");
    }
    for (h = 1; h <= 2u; h++) {                          /* HPHN BASS: the headphones only */
        case_t c = {0, (uint8_t)h, 1, 0, 40}, d = {0, (uint8_t)h, 0, 0, 40}, o = {0, 0, 1, 0, 40};
        run_t *q = run_case(&o);
        r = run_case(&c);
        printf("  HPHN BASS %s: RMS %.0f against %.0f plain (x %.2f)\n", h == 1u ? "+3 dB" : "+6 dB", rms(r->codec, NS),
               rms(q->codec, NS), rms(r->codec, NS) / rms(q->codec, NS));
        check(rms(r->codec, NS) > (h == 1u ? 1.04 : 1.08) * rms(q->codec, NS), h == 1u ? "HPHN BASS +3 dB, headphones in: the bass up"
                                                                     : "HPHN BASS +6 dB, headphones in: the bass up");
        check(same(r->mst, q->mst, NS) && same(r->stem, q->stem, CTL * NTRK * NBLK),
              "HPHN BASS on headphones: USB's MASTER and stems bit for bit the plain ones");
        r = run_case(&d);
        check(same(r->codec, q->codec, NS), "HPHN BASS on, nothing plugged (SPK LOWCUT OFF): the plain output");
    }
}

static void t_bluetooth(void)
{
    static const case_t OFF = {0, 0, 0, 0, 40}, SPK = {1, 0, 0, 1, 40}, HP = {0, 2, 1, 1, 40}, HPO = {0, 0, 1, 0, 40},
                        BOFF = {0, 0, 0, 1, 40};
    run_t *ref = run_case(&OFF), *r = run_case(&SPK), *hr = run_case(&HPO), *h = run_case(&HP), *b0 = run_case(&BOFF);
    const uint32_t sk = 1u + (OC_LA + CTL - 1u) / CTL, ns = 2u * CTL * sk;   /* (the corrected block, then LIMIT's delay) */
    uint32_t i, z = 1;
    for (i = 0; i < ns; i++)
        z &= r->bt[i] == 0 && h->bt[i] == 0;
    printf("  a stream started during a corrected block: %u block(s) of silence (the block and LIMIT's lookahead, %u frames)\n",
           sk, OC_LA);
    check(z, "Bluetooth starting during a corrected block: it and the lookahead's blocks after it go as silence");
    check(same(b0->bt, ref->codec, NS), "Bluetooth streaming, both OFF: the plain output, bit for bit (the harness)");
    check(r->bt_n == (int32_t)NBLK && same(r->bt + ns, ref->codec + ns, NS - ns),
          "Bluetooth streaming, SPK LOWCUT ON, nothing plugged: every block after them the plain output, bit for bit");
    check(same(h->bt + ns, hr->codec + ns, NS - ns),
          "Bluetooth streaming, HPHN BASS +6 dB, headphones in: the plain output, bit for bit");
    check(same(r->mst, ref->mst, NS) && same(r->stem, ref->stem, CTL * NTRK * NBLK), "Bluetooth streaming: USB bit for bit");
}

static void t_debounce(void)
{
    hpj_t j = {0};
    uint32_t t, flips = 0, st, last;
    static const uint16_t BOUNCE[] = {0, 12, 30, 41, 66, 80, 104, 131, 150};   /* (a plug: the pin's flips, ms) */
    check(hpj_step(&j, 0, 1000) == 0u, "the first reading counts at once (nothing plugged)");
    last = 0;
    for (t = 1000; t < 1600; t++) {                      /* a plug at 1100: the contact bounces 150 ms, then holds 1 */
        uint32_t raw = 0, k;
        if (t >= 1100u) {
            raw = 1;
            for (k = 0; k < sizeof BOUNCE / sizeof BOUNCE[0]; k++)
                raw ^= t - 1100u >= BOUNCE[k];
            raw = t - 1100u >= 150u ? 1u : raw;
        }
        st = hpj_step(&j, raw, t);
        flips += st != last;
        last = st;
        if (t == 1240u)
            check(st == 0u, "while it bounces: no change yet");
    }
    check(flips == 1u && last == 1u, "a bouncing plug: the state changes once, to headphones in");
    for (t = 1600; t < 1650; t++)                        /* a 50 ms drop: not an unplug */
        st = hpj_step(&j, 0, t);
    st = hpj_step(&j, 1, 1650);
    check(st == 1u, "a 50 ms drop of the pin: still in");
    for (t = 1700; t <= 1800; t++)
        st = hpj_step(&j, 0, t);
    check(st == 0u, "unplugged and steady 100 ms: out");
    hpj_host_in = 1;                                     /* hp_poll: the simulator's / host's pin into hp_in */
    fm1_ms = 5000;
    hp_poll();
    fm1_ms = 5200;
    hp_poll();
    check(hp_in == 1u, "hp_poll: the pin (sim_jack) in after HPJ_MS");
    hpj_host_in = 0;
    hp_poll();
    fm1_ms = 5400;
    hp_poll();
    check(hp_in == 0u, "hp_poll: out again");
}

static void t_settings(void)
{
    check(oc_set_word(0) == oc_word(1, 0) && spk_lc == 1u && hp_bass == 0u, "a stored SPK LOWCUT OFF: ignored, ON (the row is gone), HPHN BASS OFF");
    check(oc_set_word(1) == 1u && spk_lc == 1u && hp_bass == 0u, "an old record's LOWCUT: SPK LOWCUT ON");
    check(oc_set_word(2) == 1u && spk_lc == 1u && hp_bass == 0u, "a stored BASS+: ignored, SPK LOWCUT ON");
    check(oc_set_word(oc_word(1, 2)) == 0x21u && spk_lc == 1u && hp_bass == 2u, "both kept in the record's word");
    check(oc_set_word(0x33u) == oc_word(1, 0), "out of range: the defaults (SPK LOWCUT ON, HPHN BASS OFF)");
}

int main(void)
{
    t_settings();
    t_debounce();
    t_sides();
    t_bluetooth();
    if (fails) {
        printf("outcorr: %d FAILED\n", fails);
        return 1;
    }
    puts("outcorr: SPK LOWCUT / HPHN BASS by the jack, the codec's output only (Bluetooth, USB): OK");
    return 0;
}
