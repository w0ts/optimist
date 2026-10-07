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
 *   bass+    the response 40 Hz .. 8 kHz of OFF / LOWCUT / BASS+ (BASS+ leaves 440 Hz: Felucca 1.0.2 #42); the master
 *            on a 55 Hz sine: LOWCUT cuts it, BASS+ gives it back as harmonics; exactly 0 after
 *   motion   events on steps, the patch back each pass and at STOP, recording with REC armed, projects
 *            hold the patch, their store the motion; written beside the project in its flash sector, read
 *            back only for the same project
 *   phys     engine 11, every preset audible, bounded, free after the release; 10 kept free
 *   acid     engine 12, TB-3PO lines (seeded, in the scale, no slide into a rest), the line plays bounded
 *   delay    a delay longer than the line halves (1/4 at 40 BPM -> 1/8), one that fits is unchanged
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
#if FELUCCA_MOTION
/* the project sectors on a RAM image (0x97000..0x9FFFF), through storage.c: motion_flash.c beside them */
static uint8_t mo_nor[0x9000];
static int st_read(uint32_t off, void *dst, uint32_t n)
{ if (off < 0x97000u || off + n > 0xA0000u) return -1; memcpy(dst, mo_nor + off - 0x97000u, n); return 0; }
static int st_erase(uint32_t off) { if (off < 0x97000u || off >= 0xA0000u) return -1; memset(mo_nor + off - 0x97000u, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{ uint32_t i; if (off < 0x97000u || off + n > 0xA0000u) return -1; for (i = 0; i < n; i++) mo_nor[off - 0x97000u + i] &= ((const uint8_t *)src)[i]; return 0; }
#include "../firmware/src/storage.c"
#include "../firmware/src/motion_flash.c"
static project_t proj_tmp_m;
#endif

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
/* the reverb bus alone, algorithm type (rev_type.c RT_*: TYPE set to it): a 50 ms noise burst into its send at time
 * 0, then silence; the wet output's RMS and peak over secs, and the block it went idle (exactly 0 out from then on),
 * -1 = never */
static double rev_render(uint32_t type, double secs, uint32_t burst, int32_t *peak, int64_t *idle_blk, const char *wav)
{
    int32_t cin[CTL] = {0}, din[CTL] = {0}, rin[CTL], wl[CTL], wr[CTL];
    uint32_t b, i, nb = (uint32_t)(secs * FS / CTL), seed = 12345;
    double acc = 0;
    FILE *f = wav ? fopen(wav, "wb") : 0;
    if (f)
        wav_hdr(f, nb * CTL);
    bp_set[BPS_RTYPE] = (int16_t)rev_index(type);
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
    const uint32_t T = REV_ALGO[0];                  /* (the tank beside it: ROOM, PLATE or FDN8, as built) */
    rev_render(T, 3.0, 0, &pk_r, &idle_r, 0);        /* (the tank, silent: the lines clear) */
    rms_r = rev_render(T, 3.0, 1, &pk_r, &idle_r, "build/host/reverb-room.wav");
    rev_render(RT_SPRING, 3.0, 0, &pk_s, &idle_s, 0);   /* (the change to SPRING, then silence) */
    rms_s = rev_render(RT_SPRING, 3.0, 1, &pk_s, &idle_s, "build/host/reverb-spring.wav");
    if (idle_s < 0) {                                /* still ringing after 3 s: its tail, on */
        int32_t pk;
        rev_render(RT_SPRING, 30.0, 0, &pk, &idle_s, 0);
        if (idle_s >= 0)
            idle_s += (int64_t)(3.0 * FS / CTL);
    }
    db = 20.0 * log10(rms_s / rms_r);
    printf("backports: spring: wet RMS %.1f (ROOM %.1f, %+.1f dB), peak %d (ROOM %d), idle after %.2f s (ROOM %.2f s)\n",
           rms_s, rms_r, db, pk_s, pk_r, idle_s < 0 ? -1.0 : idle_s * CTL / (double)FS,
           idle_r < 0 ? -1.0 : idle_r * CTL / (double)FS);
#if REV_TANK_HALF   /* (a tank at 22.05 kHz: REV_HALF, PLATE, FDN8 keeps nothing above 11 kHz: half this white burst's energy, -2.5 dB) */
    check(rms_s > 0 && db > -4.0 && db < 4.0, "spring: its wet level within 4 dB of the half-rate ROOM's on the same send");
#else
    check(rms_s > 0 && db > -3.0 && db < 3.0, "spring: its wet level within 3 dB of the ROOM's on the same send");
#endif
    check(pk_s < 32768 * 3, "spring: bounded");
    check(idle_s > 0, "spring: the tail rings out to exactly 0 and the bus goes idle");
    {
        int32_t pk;
        int64_t idle;
        double r = rev_render(T, 0.02, 0, &pk, &idle, 0);  /* back to the tank: the change fades, then silence */
        uint32_t i, clear = 1;
        for (i = 0; i < sizeof rev_line / 2u; i++)
            clear &= rev_line[i] == 0;
        (void)r;
        check(clear && rsel.type == T && fx.rev_q == FX_Q_MAX, "spring: a model change clears the lines, the bus idle");
    }
    {
        static project_t pj;
        static dlrec_t bp_dl;                          /* (its drum record: format 10) */
        bp_set[BPS_RTYPE] = (int16_t)rev_index(RT_SPRING);
        proj_capture(&pj, &bp_dl);
        bp_set[BPS_RTYPE] = 0;
        proj_apply(&pj, &bp_dl, 1);
        check(pj.rsv[0] == 1u && bp_set[BPS_RTYPE] == rev_index(RT_SPRING), "spring: TYPE saved in the project (rsv[0]) and loaded back");
        pj.rsv[0] = 0;
        proj_apply(&pj, &bp_dl, 1);
        check(bp_set[BPS_RTYPE] == 0 && (rev_orph == 0xFF) == (T == RT_ROOM),
              "spring: a project without it (rsv[0] 0) loads ROOM (without ROOM: the first tank, ROOM kept as asked)");
        bp_set[BPS_RTYPE] = 0;
    }
}
#endif

#if FELUCCA_BASSPLUS
/* the master stage alone on a sine (freq Hz, amplitude a) for secs, then silence for secs: the RMS of each half
 * and whether the output is exactly 0 at the end */
static double master_render(uint32_t lowcut, uint32_t freq, int32_t a, double secs, double *rms_tail, int *zero_end)
{
    uint32_t i, n = (uint32_t)(secs * FS);
    double acc = 0, acc2 = 0;
    int32_t last = 1;
    fx_lowcut = (uint8_t)lowcut;
    for (i = 0; i < 2u * n; i++) {
        int32_t x = i < n ? (int32_t)(a * sin(2.0 * M_PI * freq * i / FS)) : 0, l = x, r = x;
        master_out(&l, &r);
        if (i < n)
            acc += (double)l * l;
        else
            acc2 += (double)l * l;
        last = l | r;
    }
    *rms_tail = sqrt(acc2 / n);
    *zero_end = last == 0;
    fx_lowcut = 0;
    return sqrt(acc / n);
}
/* the gain of a sine (6000, ~-15 dBFS) through master_out in one mode, in dB, from a cleared state (the response
 * check of Felucca 1.0.2 tests/speaker_test.c tone_db, #42, by Leo Kuroshita, GPL-3.0-only) */
static double tone_db(uint32_t mode, double f)
{
    static int32_t y[FS / 2u];
    uint32_t i, n = FS / 2u, skip = FS / 8u;
    double in = 0, out = 0;
    fx_lowcut = (uint8_t)mode;
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = dc_l = dc_r = dce_l = dce_r = 0;
    memset(lce, 0, sizeof lce);
    spk_bass_reset();
    lim_env = LIM_T;
    for (i = 0; i < n; i++) {
        int32_t l = (int32_t)(6000.0 * sin(2.0 * M_PI * f * i / FS)), r = l;
        if (i >= skip)
            in += (double)l * l;
        master_out(&l, &r);
        y[i] = l;
    }
    for (i = skip; i < n; i++)
        out += (double)y[i] * y[i];
    fx_lowcut = 0;
    return 10.0 * log10(out / in);
}
static void t_bassplus_response(void)
{
    static const double F[8] = {40, 80, 110, 220, 440, 1000, 4000, 8000};
    static const char *const NAME[3] = {"OFF", "LOWCUT", "BASS+"};
    double g[3][8];
    uint32_t m, k;
    int ok = 1;
    for (m = 0; m < 3u; m++) {
        printf("backports: bass+ response %-6s", NAME[m]);
        for (k = 0; k < 8u; k++) {
            g[m][k] = tone_db(m, F[k]);
            printf(" %4.0f Hz %5.1f", F[k], g[m][k]);
        }
        printf(" dB\n");
    }
    for (k = 0; k < 8u; k++)
        ok &= fabs(g[0][k]) <= 0.3;
    check(ok, "bass+: OFF is flat 40 Hz .. 8 kHz (only the DC blocker)");
    check(g[1][0] <= -9.0 && g[1][2] <= -4.0 && g[1][2] >= -8.0 && fabs(g[1][5]) <= 0.5 && fabs(g[1][7]) <= 0.5,
          "bass+: LOWCUT ~-6 dB at 110 Hz, 40 Hz well down, 1 kHz and up untouched");
    check(g[2][3] <= -4.0 && fabs(g[2][6]) <= 0.5 && fabs(g[2][7]) <= 0.5,
          "bass+: BASS+ cuts at ~220 Hz, 4 kHz and up untouched");
    check(g[2][4] >= -3.0 && fabs(g[2][5]) <= 1.0,
          "bass+: BASS+ leaves 440 Hz and 1 kHz (the bass path does not cancel the mix's midrange, Felucca #42)");
}
static void t_bassplus(void)
{
    double t0, t1, t2, r0, r1, r2;
    int z0, z1, z2;
    t_bassplus_response();
    reset(120);
    r0 = master_render(0, 55, 8000, 1.0, &t0, &z0);
    r1 = master_render(1, 55, 8000, 1.0, &t1, &z1);
    r2 = master_render(2, 55, 8000, 1.0, &t2, &z2);
    printf("backports: bass+: 55 Hz sine RMS out: OFF %.0f, LOWCUT %.0f, BASS+ %.0f; tails %.2f %.2f %.2f\n", r0, r1, r2, t0, t1, t2);
    check(r1 < r0 && r2 > 1.5 * r1, "bass+: a 55 Hz bass: LOWCUT takes it down, BASS+ gives it back as harmonics");
    check(z2, "bass+: silence after: exactly 0 out");
}
#endif
#if FELUCCA_DLY_HALVE
static void t_dly_halve(void)
{
    reset(120);
    song.g[G_DTIME] = 0;                             /* 1/4 */
    song.g[G_BPM] = 40;                              /* 66150 samples: longer than the line */
    printf("backports: delay 1/4 at 40 BPM: line %u, delay %u (1/8: %u)\n", DLY_LEN, delay_samples(), div_samples(1));
    check(div_samples(0) < DLY_LEN || delay_samples() == div_samples(1) || delay_samples() == div_samples(0) / 4u,
          "delay halve: a 1/4 longer than the line plays 1/8 (on the beat), not cut");
    song.g[G_BPM] = 120;
    check(delay_samples() == div_samples(0), "delay halve: a 1/4 that fits plays as before");
}
#endif

#if FELUCCA_MOTION
static void t_motion(void)
{
    track_t *t = &trk[0];
    static project_t pj;
    uint32_t i, ok = 1;
    int16_t base;
    reset(120);
    host_preset(t, 0, 7);
    memset(&motion, 0, sizeof motion);
    for (i = 0; i < 4u; i++)
        put_step(t, i, 1, (const uint8_t[]){60}, ST_NOTE, 0);
    t->p[P_SLEN] = 4;
    base = t->p[P_CHOR];
    check(motion_set_event(t, 2, P_CHOR, base == 77 ? 78 : 77) == 0 && motion_count(t) == 1u && (motion.on & 1u), "motion: an event on step 3, PLAY on");
    check(motion_set_event(t, 2, P_SLEN, 5) == 1 && motion_set_event(TDRUM, 1, P_E0, 3) == 1,
          "motion: not on LEN, not on the drum track's kit");
    transport_req = 1;
    {
        uint32_t idx[4], v[4], k;
        for (k = 0; k < 4u; k++) {                                 /* the middle of steps 1..4 */
            starts_over(k ? (uint64_t)div_samples(2) / CTL : (uint64_t)div_samples(2) / CTL / 2u);
            idx[k] = t->seq_idx;
            v[k] = (uint32_t)t->p[P_CHOR];
        }
        starts_over((uint64_t)div_samples(2) / CTL);              /* step 1 of the next pass */
        printf("backports: motion: CHORUS %d, an event 77 on step 3: steps %u..%u: %u %u %u %u, next pass %d\n", base,
               idx[0], idx[3], v[0], v[1], v[2], v[3], t->p[P_CHOR]);
        ok = v[0] == (uint32_t)base && v[1] == (uint32_t)base && v[2] == 77u && v[3] == 77u && t->p[P_CHOR] == base;
    }
    check(ok, "motion: the value on its step, the patch back when the loop starts again");
    proj_capture(&proj_slot[0], &proj_dl[0]);
    check(proj_slot[0].t[0].p[P_CHOR] == base && motion_slot[0].psum == proj_slot[0].sum && motion_slot[0].count == 1u,
          "motion: a project saved while it plays holds the patch, its store the motion");
    starts_over((uint64_t)div_samples(2) * 2u / CTL);              /* step 3 again */
    seq_stop();
    check(t->p[P_CHOR] == base, "motion: STOP puts the patch back");
    {   /* recording: a knob on step 2 (REC armed, playing) */
        uint32_t into, slen;
        reset(120);
        host_preset(t, 0, 7);
        t->p[P_SLEN] = 4;
        memset(&motion, 0, sizeof motion);
        transport_req = 1;
        starts_over((uint64_t)div_samples(2) / CTL + div_samples(2) / CTL / 4u);   /* a quarter into step 2 */
        song.rec = 1;
        trk_grid(t, &into, &slen);
        motion_knob(t, P_CHOR, 77);
        check(motion.count == 1u && motion.ev[0].place == 1u && motion.ev[0].param == P_CHOR && motion.ev[0].value == 77,
              "motion: REC + a knob a quarter into step 2: an event on step 2");
        song.rec = 0;
        t->p[P_CHOR] = 12;                                         /* (as edit_param: the value, then the hook) */
        motion_knob(t, P_CHOR, 12);
        check(motion.count == 1u && motion_base[0][P_CHOR] == 12, "motion: not recording, a knob sets the patch");
        seq_stop();
        check(t->p[P_CHOR] == 12, "motion: STOP: the knob's value, not the recorded one");
    }
    {   /* a project round trip, in RAM and in flash */
        motion_store_t keep = motion;
        proj_capture(&proj_slot[1], &proj_dl[1]);
        memset(&motion, 0, sizeof motion);
        proj_apply(&proj_slot[1], &proj_dl[1], 1);
        check(motion.count == keep.count && motion.on == keep.on, "motion: a slot loads its motion back");
        memset(mo_nor, 0xFF, sizeof mo_nor);
        check(st_save(OBJ_PROJECT0 + 1, &proj_slot[1], sizeof proj_slot[1]) == 0, "motion: the slot saved (host flash)");
        motion_flash_write(OBJ_PROJECT0 + 1, &proj_slot[1]);
        memset(&motion_slot[1], 0, sizeof motion_slot[1]);
        motion_flash_read(OBJ_PROJECT0 + 1, &proj_slot[1]);
        check(motion_slot[1].count == keep.count && motion_slot[1].psum == proj_slot[1].sum,
              "motion: written beside the project in its sector, read back");
        proj_slot[1].t[0].p[P_LEVEL] ^= 1;                         /* another project in RAM: its sum differs */
        proj_slot[1].sum = proj_sum(&proj_slot[1]);
        motion_flash_read(OBJ_PROJECT0 + 1, &proj_slot[1]);
        check(motion_slot[1].count == 0u, "motion: never attached to another project");
        check(st_load(OBJ_PROJECT0 + 1, &proj_tmp_m, sizeof proj_tmp_m) == (int)sizeof(project_t),
              "motion: the project itself loads as before (its payload untouched)");
        memset(&motion, 0, sizeof motion);
    }
}
#endif

#if FELUCCA_ENG_PHYS
/* each PHYS preset: a note held 1 s, then 3 s: audible, bounded, finite; its voices free after the release;
 * the free number 10 (SLICE's) plays its stand-in */
static void t_phys(void)
{
    uint32_t pi, ok = 1, n = ENGINES[ENG_IX_PHYS]->npresets;
    check(str_eq(ENGINES[ENG_IX_PHYS]->name, "PHYS") && eng_uid(ENG_IX_PHYS) == 11u, "phys: engine UID 11 is PHYS");
    check(FELUCCA_SLICE || ENGINES[eng_slot(10)] == &ENG_SAMPLE,
          "phys: UID 10 (SLICE's) plays its fallback SAMPLE without SLICE (registry.h)");
    for (pi = 0; pi < n; pi++) {
        int32_t out[CTL * 2];
        double acc = 0;
        int32_t pk = 0;
        uint32_t b, i, busy = 0;
        reset(120);
        host_preset(&trk[0], ENG_IX_PHYS, pi);
        trk_note_on(&trk[0], 48 + 7 * pi % 24, 110);
        for (b = 0; b < 4u * FS / CTL; b++) {
            if (b == FS / CTL)
                trk_note_off(&trk[0], 48 + 7 * pi % 24);
            mix_block(out, CTL);
            for (i = 0; i < 2u * CTL; i++) {
                int32_t a = out[i] < 0 ? -out[i] : out[i];
                acc += (double)out[i] * out[i];
                if (a > pk)
                    pk = a;
            }
        }
        for (i = 0; i < NVOICE; i++)
            busy |= trk[0].v[i].active;
        printf("backports: phys %-12s rms %6.0f peak %5d %s\n", ENGINES[ENG_IX_PHYS]->presets[pi].name,
               sqrt(acc / (4.0 * FS * 2)), pk, busy ? "STILL SOUNDING" : "");
        ok &= acc > 0 && pk <= 32767 && !busy;
    }
    check(ok, "phys: every preset audible, bounded, its voices free 3 s after the release");
}
#endif

#if FELUCCA_ENG_ACID
/* ACID: engine 12; a line of 16th notes with an accent and a slide: audible, bounded, the slide no new attack;
 * TB-3PO: the same seed the same line, notes in the scale from ROOT, no slide into a rest */
static void t_acid(void)
{
    track_t *t = &trk[0];
    uint32_t i, ok = 1, n_notes = 0, n_acc = 0, n_sld = 0;
    int32_t out[CTL * 2], pk = 0;
    double acc = 0;
    static step_t keep[NSTEP];
    check(str_eq(ENGINES[ENG_IX_ACID]->name, "ACID") && eng_uid(ENG_IX_ACID) == 12u, "acid: engine UID 12 is ACID");
    check(FELUCCA_ENG_PHYS || ENGINES[eng_slot(11)] == &ENG_ANALOG,
          "acid: UID 11 (PHYS's) plays its fallback ANALOG without PHYS (registry.h)");
    reset(120);
    host_preset(t, ENG_IX_ACID, 0);
    t->p[P_SLEN] = 16;
    t->p[P_ROOT] = 9;                                /* A */
    acid_generate(t, 70, 40, 25, 12345);
    memcpy(keep, t->step, sizeof keep);
    acid_generate(t, 70, 40, 25, 12345);
    check(!memcmp(keep, t->step, sizeof keep), "acid: TB-3PO: the same seed, the same line");
    for (i = 0; i < 16u; i++) {
        const step_t *st = &t->step[i];
        if (st->time != ST_NOTE)
            continue;
        n_notes++;
        n_acc += (st->flags & SF_ACCENT) != 0;
        n_sld += (st->flags & SF_SLIDE) != 0;
        ok &= ((1u << ((st->note[0] - 9u) % 12u)) & 0x5ADu) != 0;   /* A minor: 0 2 3 5 7 8 10 */
        ok &= !(st->flags & SF_SLIDE) || t->step[(i + 1u) % 16u].time == ST_NOTE;
    }
    printf("backports: acid: TB-3PO line: %u notes, %u accents, %u slides of 16\n", n_notes, n_acc, n_sld);
    check(ok && n_notes >= 6u, "acid: TB-3PO: notes in A minor, no slide into a rest");
    transport_req = 1;
    for (i = 0; i < 4u * div_samples(2) * 16u / CTL; i++) {
        uint32_t k;
        mix_block(out, CTL);
        for (k = 0; k < 2u * CTL; k++) {
            int32_t a = out[k] < 0 ? -out[k] : out[k];
            acc += (double)out[k] * out[k];
            if (a > pk)
                pk = a;
        }
    }
    printf("backports: acid: 4 bars of the line: rms %.0f peak %d\n", sqrt(acc / (4.0 * div_samples(2) * 16 * 2)), pk);
    check(acc > 0 && pk < 32768, "acid: the line plays, bounded");
    seq_stop();
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
#if FELUCCA_BASSPLUS
    t_bassplus();
#endif
#if FELUCCA_DLY_HALVE
    t_dly_halve();
#endif
#if FELUCCA_MOTION
    t_motion();
#endif
#if FELUCCA_ENG_PHYS
    t_phys();
#endif
#if FELUCCA_ENG_ACID
    t_acid();
#endif
    printf("backports: %d failed\n", fails);
    return fails;
}
