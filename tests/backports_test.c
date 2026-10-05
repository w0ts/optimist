/* SPDX-License-Identifier: GPL-3.0-only */
/* The backported features (firmware/src/backports.h, tools/backports.json), each section built only with its
 * switch on (tests/run_tests.sh builds this with every switch on; the goldens in regress.c check that a switch
 * at 0 leaves the sound alone, and that a switch at 1 changes nothing until the feature is used).
 *   chance   per-step chance: the 5 % grid, 0 % never, 100 % always (no random number), about half at 50 %,
 *            ratchets of a dropped step silent, TIE steps unaffected
 *   qnt seq  SCL > QNT SEQ: the keys as SNAP, sequenced notes snap at play (steps kept), ROOT follows, a chord's
 *            notes snapping together start once, ratchets of a snapped note, no hanging note
 *   spring   REVERB > TYPE SPRING: level near the ROOM's, bounded, rings out to exactly 0 (idle), a model change
 *            clears the lines (renders: build/host/reverb-room.wav, reverb-spring.wav)
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1                                  /* project.c's capture / apply (as tests/project_test.c) */
static uint32_t trk_def_engine(uint32_t i)
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/project.c"

static uint64_t blk;
static int fails;
static void check(int ok, const char *what)
{
    printf("backports: %-78s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void run_block(void)
{
    int32_t out[CTL * 2];
    mix_block(out, CTL);
    blk++;
}
static void reset(uint32_t bpm)
{
    uint32_t i;
    seq_stop();                                      /* (nothing held over from the test before) */
    transport_req = 0;
    for (i = 0; i < NTRK; i++) {
        memset(trk[i].v, 0, sizeof trk[i].v);
        trk[i].seq_n = 0;
        trk[i].seq_hold = 0;
    }
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    blk = 0;
    song.g[G_BPM] = (int16_t)bpm;
    song.rec = 0;
    fm1_in.notes = fm1_in.buttons = 0;
}
/* note starts on the synth parts while n blocks render */
static uint32_t starts_over(uint64_t n)
{
    uint32_t starts = 0, last = vage;
    uint64_t end = blk + n;
    while (blk < end) {
        run_block();
        starts += vage - last;
        last = vage;
    }
    return starts;
}

#if FELUCCA_CHANCE
static void t_chance(void)
{
    step_t s;
    uint32_t i, ok = 1, n, r0;
    memset(&s, 0, sizeof s);
    check(step_chance(&s) == 100u, "chance: a step written before (flags bits clear) is 100 %");
    for (i = 0; i <= 100u; i += 5u) {
        step_set_chance(&s, i);
        ok &= step_chance(&s) == i && (s.flags & (SF_ACCENT | SF_SLIDE)) == 0;
    }
    s.flags |= SF_ACCENT | SF_SLIDE;
    step_set_chance(&s, 35);
    ok &= step_chance(&s) == 35u && (s.flags & (SF_ACCENT | SF_SLIDE)) == (SF_ACCENT | SF_SLIDE);
    step_set_chance(&s, 100);
    ok &= s.flags == (SF_ACCENT | SF_SLIDE);
    check(ok, "chance: 0..100 % in 5 % steps round trip; ACCENT / SLIDE kept; 100 % clears the bits");

    /* 16 NOTE steps of 1/16 at 120 BPM, 32 passes */
    reset(120);
    host_preset(&trk[0], 0, 7);                      /* TRAP PLUCK (POLY) */
    for (i = 0; i < 16u; i++)
        put_step(&trk[0], i, 1, (const uint8_t[]){60}, ST_NOTE, 0);
    trk[0].p[P_SLEN] = 16;
    transport_req = 1;
    n = starts_over((uint64_t)div_samples(2) * 16u * 4u / CTL);
    check(n >= 63u && n <= 65u, "chance: 100 % everywhere: every step plays");
    r0 = rng_state;
    i = (uint32_t)chance_drop(&trk[0].step[0]);
    check(!i && rng_state == r0, "chance: a step at 100 % draws no random number (renders stay as before)");

    reset(120);
    host_preset(&trk[0], 0, 7);
    for (i = 0; i < 16u; i++) {
        put_step(&trk[0], i, 1, (const uint8_t[]){60}, ST_NOTE, 0);
        step_set_chance(&trk[0].step[i], 50);
    }
    trk[0].p[P_SLEN] = 16;
    transport_req = 1;
    n = starts_over((uint64_t)div_samples(2) * 16u * 32u / CTL);
    printf("backports: chance 50 %%: %u of 512 steps played\n", n);
    check(n > 200u && n < 312u, "chance: 50 % on 512 steps: 200..312 play");

    reset(120);
    host_preset(&trk[0], 0, 7);
    for (i = 0; i < 16u; i++) {
        put_step(&trk[0], i, 1, (const uint8_t[]){60}, ST_NOTE, 0);
        step_set_chance(&trk[0].step[i], 0);
    }
    trk[0].step[3].rat = 2;                          /* x3: its hits silent too */
    trk[0].p[P_SLEN] = 16;
    transport_req = 1;
    n = starts_over((uint64_t)div_samples(2) * 16u * 4u / CTL);
    check(n == 0u, "chance: 0 %: nothing plays, a x3 ratchet on a dropped step neither");

    reset(120);                                      /* a NOTE (always) then TIEs at 0 %: the tie holds */
    host_preset(&trk[0], 0, 7);
    put_step(&trk[0], 0, 1, (const uint8_t[]){60}, ST_NOTE, 0);
    for (i = 1; i < 4u; i++) {
        put_step(&trk[0], i, 0, (const uint8_t[]){0}, ST_TIE, 0);
        step_set_chance(&trk[0].step[i], 0);
    }
    trk[0].p[P_SLEN] = 4;
    transport_req = 1;
    starts_over((uint64_t)div_samples(2) * 3u / CTL);
    {
        uint32_t held = 0;
        for (i = 0; i < NVOICE; i++)
            held |= trk[0].v[i].active && trk[0].v[i].gate && trk[0].v[i].note == 60;
        check(held, "chance: a TIE is not rolled (it holds the note before it)");
    }
}
#endif

#if FELUCCA_QNT_SEQ
/* the notes started on part 1 while n blocks render (the newest voice at each start) */
static uint32_t notes_over(uint64_t n, uint8_t *out, uint32_t max)
{
    uint32_t got = 0, last = vage, i;
    uint64_t end = blk + n;
    while (blk < end) {
        run_block();
        if (vage != last) {
            for (i = 0; i < NVOICE; i++)
                if (trk[0].v[i].age > last && got < max)
                    out[got++] = trk[0].v[i].note;
            last = vage;
        }
    }
    return got;
}
static void qseq_setup(uint32_t quant, uint32_t root)
{
    reset(120);
    host_preset(&trk[0], 0, 7);                      /* TRAP PLUCK (POLY) */
    trk[0].p[P_SCALE] = 1;                           /* MAJ */
    trk[0].p[P_ROOT] = (int16_t)root;
    trk[0].p[P_QUANT] = (int16_t)quant;
    trk[0].p[P_SLEN] = 4;
    trk[0].p[P_TRANS] = 0;
    song.octave = 0;
}
static void t_qnt_seq(void)
{
    uint8_t got[64];
    uint32_t n;
    qseq_setup(Q_SEQ, 0);
    check(kb_map(&trk[0], 8) == 60u && kb_map(&trk[0], 7) == 60u, "QNT SEQ: the keys as SNAP (C#4 key -> C4 in C major)");
    put_step(&trk[0], 0, 1, (const uint8_t[]){61}, ST_NOTE, 0);
    put_step(&trk[0], 1, 1, (const uint8_t[]){66}, ST_NOTE, 0);
    transport_req = 1;
    n = notes_over((uint64_t)div_samples(2) * 2u / CTL, got, 64);
    check(n == 2u && got[0] == 60 && got[1] == 65, "QNT SEQ: steps C#4, F#4 play C4, F4 in C major");
    check(trk[0].step[0].note[0] == 61 && trk[0].step[1].note[0] == 66, "QNT SEQ: the steps keep their notes");

    qseq_setup(Q_SNAP, 0);
    put_step(&trk[0], 0, 1, (const uint8_t[]){61}, ST_NOTE, 0);
    transport_req = 1;
    n = notes_over((uint64_t)div_samples(2) / CTL, got, 64);
    check(n == 1u && got[0] == 61, "QNT SNAP: a sequenced C#4 plays as written");

    qseq_setup(Q_SEQ, 2);                            /* D major: C# is in it */
    put_step(&trk[0], 0, 1, (const uint8_t[]){61}, ST_NOTE, 0);
    put_step(&trk[0], 1, 1, (const uint8_t[]){60}, ST_NOTE, 0);
    transport_req = 1;
    n = notes_over((uint64_t)div_samples(2) * 2u / CTL, got, 64);
    check(n == 2u && got[0] == 61 && got[1] == 59, "QNT SEQ: ROOT D: C#4 stays, C4 snaps down to B3");

    qseq_setup(Q_SEQ, 0);                            /* a chord C4 + C#4: both C4, once */
    put_step(&trk[0], 0, 2, (const uint8_t[]){60, 61}, ST_NOTE, 0);
    transport_req = 1;
    n = notes_over((uint64_t)div_samples(2) / CTL, got, 64);
    check(n == 1u && got[0] == 60, "QNT SEQ: two chord notes snapping together start once");

    qseq_setup(Q_SEQ, 0);                            /* x3 ratchet on C#4: three C4 */
    put_step(&trk[0], 0, 1, (const uint8_t[]){61}, ST_NOTE, 0);
    trk[0].step[0].rat = 2;
    transport_req = 1;
    n = notes_over((uint64_t)div_samples(2) / CTL, got, 64);
    check(n == 3u && got[0] == 60 && got[1] == 60 && got[2] == 60, "QNT SEQ: a x3 ratchet plays the snapped note 3 times");
    {
        uint32_t i, hang = 0;
        notes_over((uint64_t)div_samples(2) * 3u / CTL, got, 64);
        for (i = 0; i < NVOICE; i++)
            hang |= trk[0].v[i].active && trk[0].v[i].gate;
        check(!hang, "QNT SEQ: no note left hanging after the snapped ratchets");
    }
}
#endif

#if FELUCCA_SPRING
/* the reverb bus alone: a 50 ms noise burst into its send at time 0, then silence; the wet output's RMS and
 * peak over secs, and the block it went idle (exactly 0 out from then on), -1 = never */
static double rev_render(uint32_t type, double secs, uint32_t burst, int32_t *peak, int64_t *idle_blk, const char *wav)
{
    int32_t cin[CTL] = {0}, din[CTL] = {0}, rin[CTL], wl[CTL], wr[CTL];
    uint32_t b, i, nb = (uint32_t)(secs * FS / CTL), seed = 12345;
    double acc = 0;
    FILE *f = wav ? fopen(wav, "wb") : 0;
    if (f)
        wav_hdr(f, nb * CTL);
    bp_set[BPS_RTYPE] = (int16_t)type;
    song.g[G_RSIZE] = 90;
    song.g[G_RDAMP] = 60;
    *peak = 0;
    *idle_blk = -1;
    for (b = 0; b < nb; b++) {
        int32_t any = 0;
        for (i = 0; i < CTL; i++) {
            seed = seed * 1664525u + 1013904223u;
            rin[i] = burst && b < 2205u / CTL ? ((int32_t)(seed >> 16) - 32768) / 2 : 0;
        }
        fx_buses(cin, din, rin, wl, wr, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t a = wl[i] < 0 ? -wl[i] : wl[i];
            acc += (double)wl[i] * wl[i];
            if (a > *peak)
                *peak = a;
            any |= wl[i] | wr[i];
            if (f) {
                int16_t s2[2] = {(int16_t)clamp(wl[i], -32768, 32767), (int16_t)clamp(wr[i], -32768, 32767)};
                fwrite(s2, 2, 2, f);
            }
        }
        if (any)
            *idle_blk = -1;
        else if (*idle_blk < 0 && b > 2205u / CTL)
            *idle_blk = b;
    }
    if (f)
        fclose(f);
    return sqrt(acc / ((double)nb * CTL));
}
static void t_spring(void)
{
    int32_t pk_r, pk_s;
    int64_t idle_r, idle_s;
    double rms_r, rms_s, db;
    reset(120);
    rev_render(0, 3.0, 0, &pk_r, &idle_r, 0);        /* (ROOM, silent: the lines clear) */
    rms_r = rev_render(0, 3.0, 1, &pk_r, &idle_r, "build/host/reverb-room.wav");
    rev_render(1, 3.0, 0, &pk_s, &idle_s, 0);        /* (the change to SPRING, then silence) */
    rms_s = rev_render(1, 3.0, 1, &pk_s, &idle_s, "build/host/reverb-spring.wav");
    if (idle_s < 0) {                                /* still ringing after 3 s: its tail, on */
        int32_t pk;
        rev_render(1, 30.0, 0, &pk, &idle_s, 0);
        if (idle_s >= 0)
            idle_s += (int64_t)(3.0 * FS / CTL);
    }
    db = 20.0 * log10(rms_s / rms_r);
    printf("backports: spring: wet RMS %.1f (ROOM %.1f, %+.1f dB), peak %d (ROOM %d), idle after %.2f s (ROOM %.2f s)\n",
           rms_s, rms_r, db, pk_s, pk_r, idle_s < 0 ? -1.0 : idle_s * CTL / (double)FS,
           idle_r < 0 ? -1.0 : idle_r * CTL / (double)FS);
    check(rms_s > 0 && db > -3.0 && db < 3.0, "spring: its wet level within 3 dB of the ROOM's on the same send");
    check(pk_s < 32768 * 3, "spring: bounded");
    check(idle_s > 0, "spring: the tail rings out to exactly 0 and the bus goes idle");
    {
        int32_t pk;
        int64_t idle;
        double r = rev_render(0, 0.02, 0, &pk, &idle, 0);  /* back to ROOM: the change fades, then silence */
        uint32_t i, clear = 1;
        for (i = 0; i < sizeof rev_line / 2u; i++)
            clear &= rev_line[i] == 0;
        (void)r;
        check(clear && sp.type == 0 && fx.rev_q == FX_Q_MAX, "spring: a model change clears the lines, the bus idle");
    }
    {
        static project_t pj;
        bp_set[BPS_RTYPE] = 1;
        proj_capture(&pj);
        bp_set[BPS_RTYPE] = 0;
        proj_apply(&pj, 1);
        check(pj.rsv[0] == 1u && bp_set[BPS_RTYPE] == 1, "spring: TYPE saved in the project (rsv[0]) and loaded back");
        pj.rsv[0] = 0;
        proj_apply(&pj, 1);
        check(bp_set[BPS_RTYPE] == 0, "spring: a project without it (rsv[0] 0) loads ROOM");
        bp_set[BPS_RTYPE] = 0;
    }
}
#endif

int main(void)
{
    host_tracks_init();
#if FELUCCA_CHANCE
    t_chance();
#endif
#if FELUCCA_QNT_SEQ
    t_qnt_seq();
#endif
#if FELUCCA_SPRING
    t_spring();
#endif
    printf("backports: %d failed\n", fails);
    return fails;
}
