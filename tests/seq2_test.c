/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.0 sequencer: the transport clock and what plays on it.
 *   timing   the kick and the click stay together for 64 bars at any tempo (no drift); a tempo or DIV
 *            change mid-step plays one step, not a burst; LEN changes keep the track in phase; an odd
 *            LEN with SWING stays in phase with a 16-step track
 *   ratchet  x2 / x3 / x4 hits evenly in their step (drums and synth)
 *   roll     ARP + key: hits on the grid at G_ROLL; recorded as ratchets (1/32 -> x2 on 1/16 steps)
 *   erase    EDIT + key: the lane goes as the playhead passes (playing), all of it (stopped); undo / redo
 *   levels   OCT- / OCT+ held on the drum track: ghost / hard hits, recorded as such
 *   chords   P_CHORD: one key plays the chord of the scale (white keys walk the degrees from C4)
 *   mute     P_MUTE / solo: no new notes, the output fades
 *   fx       P_FXOFF: the track as dry as with no DIST, SLICER, sends; the values kept
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static uint64_t blk;
typedef struct { uint64_t blk; uint8_t note, vel; } hit_t;
static hit_t hits[200000];
static uint32_t nhits;
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k;
    mix_block(out, CTL);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && nhits < 200000u) {
                hits[nhits].blk = blk;
                hits[nhits].note = drums.v[k].note;
                hits[nhits].vel = drums.v[k].vel;
                nhits++;
            }
    blk++;
}
static void reset(uint32_t bpm)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    nhits = 0;
    blk = 0;
    song.g[G_BPM] = (int16_t)bpm;
    song.rec = 0;
    rec_wait = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    kb_prev = 0;
    ly_bit[LY_ROLL] = 1u << 8;                       /* (the buttons: any bits) */
    ly_bit[LY_ERASE] = 1u << 4;
    dyn_bit[0] = 1u << 0;
    dyn_bit[1] = 1u << 1;
}
static int fails;
static void check(int ok, const char *what)
{
    printf("seq2: %-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t count_note(uint32_t note, uint64_t b0, uint64_t b1)
{
    uint32_t i, n = 0;
    for (i = 0; i < nhits; i++)
        n += hits[i].note == note && hits[i].blk >= b0 && hits[i].blk < b1;
    return n;
}

static void t_drift(void)
{
    static const uint32_t BPM[5] = {87, 90, 120, 128, 174};
    uint32_t bi, worst = 0;
    for (bi = 0; bi < 5u; bi++) {
        uint32_t i, nk = 0, nc = 0, bad = 0;
        uint64_t kb[80], cb[80];
        reset(BPM[bi]);
        song.g[G_CLOCK] = 2;                         /* the click on: its downbeat (77) */
        dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);  /* a kick on the 1 */
        transport_req = 1;
        while (blk < (uint64_t)(FS * 60.0 / BPM[bi] * 4 * 66 / CTL))
            run_block();
        for (i = 0; i < nhits; i++) {
            if (hits[i].note == 36 && nk < 80u)
                kb[nk++] = hits[i].blk;
            if (hits[i].note == 77 && nc < 80u)
                cb[nc++] = hits[i].blk;
        }
        for (i = 0; i < nk && i < nc && i < 64u; i++) {
            uint32_t d = kb[i] > cb[i] ? (uint32_t)(kb[i] - cb[i]) : (uint32_t)(cb[i] - kb[i]);
            bad += d != 0u;
            worst = d > worst ? d : worst;
        }
        if (bad || nk < 64u || nc < 64u)
            printf("seq2:   %u BPM: %u kicks, %u clicks, %u apart\n", BPM[bi], nk, nc, bad);
        worst += (nk < 64u || nc < 64u) * 1000u;
    }
    check(worst == 0u, "64 bars at 87..174 BPM: every kick in the block of its downbeat click");
}

static void t_burst(void)
{
    uint32_t i, n0;
    int ok1, ok2;
    reset(90);
    TDRUM->p[P_SDIV] = 0;                            /* 1/4 */
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    while (clk_beat < 2u || clk_pos < BEAT_U * 85u / 100u)
        run_block();
    TDRUM->p[P_SDIV] = 3;                            /* DIV 1/4 -> 1/32 at 85 % of a step */
    n0 = nhits;
    run_block();
    ok1 = nhits - n0 <= 1u;
    reset(40);
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    while (clk_beat < 1u || clk_pos < BEAT_U / 4u * 3u / 4u)
        run_block();
    song.g[G_BPM] = 240;                             /* 40 -> 240 in the middle of a step */
    n0 = nhits;
    run_block();
    ok2 = nhits - n0 <= 1u;
    check(ok1 && ok2, "DIV 1/4 -> 1/32 and 40 -> 240 BPM mid-step: one step a block, no burst");
}

static void t_swing_odd(void)
{
    uint32_t i, ok = 1, starts0 = 0, startsd = 0;
    uint64_t at0[64], atd[64];
    reset(120);
    song.g[G_SWING] = 50;
    trk[0].p[P_SLEN] = 3;
    TDRUM->p[P_SLEN] = 16;
    transport_req = 1;
    for (i = 0; i < 20u * FS / CTL; i++) {
        uint32_t b0 = trk[0].seq_idx, bd = TDRUM->seq_idx;
        uint32_t f0 = trk[0].seq_abs, fd = TDRUM->seq_abs;
        run_block();
        if (trk[0].seq_abs != f0 && trk[0].seq_idx == 0u && starts0 < 64u)
            at0[starts0++] = blk - 1u;
        if (TDRUM->seq_abs != fd && TDRUM->seq_idx == 0u && startsd < 64u)
            atd[startsd++] = blk - 1u;
        (void)b0;
        (void)bd;
    }
    /* step 48 = cycle 16 of 3 = bar 3 of 16; step 144 = cycle 48 = bar 9 */
    ok = starts0 > 48u && startsd > 9u && at0[16] == atd[3] && at0[48] == atd[9];
    if (!ok)
        printf("seq2:   %u %u starts; step 48: %llu %llu, step 144: %llu %llu\n", starts0, startsd,
               (unsigned long long)at0[16], (unsigned long long)atd[3], (unsigned long long)at0[48], (unsigned long long)atd[9]);
    check(ok, "SWING 75 %, LEN 3 against LEN 16: in phase after 15 bars (no drift)");
}

static void t_len_phase(void)
{
    reset(120);
    TDRUM->p[P_SLEN] = 32;
    trk[0].p[P_SLEN] = 16;
    transport_req = 1;
    while (TDRUM->seq_idx != 20u)
        run_block();
    TDRUM->p[P_SLEN] = 16;                           /* LEN 32 -> 16 at step 20 */
    while (TDRUM->seq_idx == 20u)
        run_block();
    check(TDRUM->seq_idx == trk[0].seq_idx, "LEN 32 -> 16 while playing: in phase with a 16-step track");
}

static void t_ratchet(void)
{
    uint32_t i, n = 0, gaps_ok = 1;
    uint64_t b[8];
    reset(120);
    dstep_set(&TDRUM->dstep[0], 4, LV_NORM, 3);      /* a hat x4 on step 0 */
    dstep_set(&TDRUM->dstep[1], 0, LV_NORM, 0);      /* a kick on step 1 */
    transport_req = 1;
    while (blk < div_samples(2) * 2u / CTL + 2u)
        run_block();
    for (i = 0; i < nhits; i++)
        if (hits[i].note == 42 && n < 8u)
            b[n++] = hits[i].blk;
    for (i = 1; i < n; i++) {
        int32_t g = (int32_t)(b[i] - b[i - 1]), want = (int32_t)(div_samples(2) / 4u / CTL);
        gaps_ok &= g >= want - 1 && g <= want + 1;
    }
    check(n == 4u && gaps_ok && count_note(36, 0, blk) == 1u, "drums: a hat x4 plays 4 even hits in its step, the next step plays");
    reset(120);
    host_preset(&trk[0], 0, 7);                      /* TRAP PLUCK (POLY) */
    put_step(&trk[0], 0, 1, (const uint8_t[]){60}, ST_NOTE, 0);
    trk[0].step[0].rat = 2;                          /* x3 */
    transport_req = 1;
    {
        uint32_t starts = 0, last = vage;
        while (blk < div_samples(2) / CTL) {
            run_block();
            if (vage != last)
                starts += vage - last;
            last = vage;
        }
        check(starts == 3u, "synth: a note x3 starts 3 times in its step");
    }
}

static void t_roll(void)
{
    uint32_t i, k = 6, n;                           /* key 6 = B3 -> lane 3 (clap) */
    reset(120);
    song.g[G_ROLL] = 2;                              /* 1/32 */
    song.sel = TRK_DRUM;
    song.rec = 1u << TRK_DRUM;
    transport_req = 1;
    run_block();
    fm1_in.buttons = ly_bit[LY_ROLL];                /* ARP held */
    fm1_in.notes = 1u << k;
    for (i = 0; i < div_samples(2) * 8u / CTL; i++)  /* 8 steps */
        run_block();
    fm1_in.notes = 0;
    fm1_in.buttons = 0;
    run_block();
    n = count_note(LANE_NOTE[3], 0, blk);
    {
        uint32_t s, rat_ok = 1, on = 0;
        for (s = 1; s < 7u; s++) {
            on += dstep_has(&TDRUM->dstep[s], 3);
            rat_ok &= !dstep_has(&TDRUM->dstep[s], 3) || dstep_rat(&TDRUM->dstep[s], 3) == 1u;
        }
        if (n < 15u || n > 17u)
            printf("seq2:   roll: %u hits\n", n);
        check(n >= 15u && n <= 17u, "roll 1/32 held 8 steps: ~16 hits");
        check(on >= 6u && rat_ok, "roll 1/32 recorded: one hit a step, ratchet x2");
    }
}

static void t_erase_undo(void)
{
    uint32_t i, left = 0, kept = 0;
    reset(120);
    for (i = 0; i < 16u; i++) {
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);  /* hats everywhere */
        if (i % 4u == 0u)
            dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
    }
    song.sel = TRK_DRUM;
    transport_req = 1;
    while (TDRUM->seq_idx != 4u)
        run_block();
    fm1_in.buttons = ly_bit[LY_ERASE];               /* EDIT held + the hat key (C4 = key 7) */
    fm1_in.notes = 1u << 7;
    while (TDRUM->seq_idx != 7u)
        run_block();
    for (i = 0; i < 20u; i++)                        /* (into step 7, not 8) */
        run_block();
    fm1_in.notes = 0;
    fm1_in.buttons = 0;
    run_block();
    for (i = 0; i < 16u; i++) {
        left += dstep_has(&TDRUM->dstep[i], 4);
        kept += dstep_has(&TDRUM->dstep[i], 0);
    }
    if (left != 12u)
        printf("seq2:   erase: %u hats left, %u kicks; %d %d\n", left, kept, dstep_has(&TDRUM->dstep[5], 4), dstep_has(&TDRUM->dstep[9], 4));
    check(left == 12u && kept == 4u && !dstep_has(&TDRUM->dstep[5], 4) && dstep_has(&TDRUM->dstep[9], 4),
          "EDIT + hat while playing steps 4..7: those hats gone, the rest and the kicks kept");
    {   /* undo (undo.c, as ui.c undo_swap): the hats back */
        int ok = undo_apply(0);
        left = 0;
        for (i = 0; i < 16u; i++)
            left += dstep_has(&TDRUM->dstep[i], 4);
        check(ok && left == 16u, "undo: the erased hats are back");
    }
    transport_req = 2;
    run_block();
    fm1_in.buttons = ly_bit[LY_ERASE];               /* stopped: every hat of the pattern */
    fm1_in.notes = 1u << 7;
    run_block();
    fm1_in.notes = fm1_in.buttons = 0;
    run_block();
    left = 0;
    for (i = 0; i < 16u; i++)
        left += dstep_has(&TDRUM->dstep[i], 4);
    check(left == 0u, "EDIT + hat while stopped: every hat gone");
}

static void t_levels(void)
{
    uint32_t i, gh = 0, hd = 0;
    reset(120);
    song.sel = TRK_DRUM;
    song.rec = 1u << TRK_DRUM;
    transport_req = 1;
    run_block();
    fm1_in.buttons = dyn_bit[0];                     /* OCT- held: a ghost snare (A3 = key 4) */
    fm1_in.notes = 1u << 4;
    run_block();
    fm1_in.notes = 0;
    run_block();
    while (TDRUM->seq_idx != 3u)
        run_block();
    fm1_in.buttons = dyn_bit[1];                     /* OCT+ held: a hard kick */
    fm1_in.notes = 1u << 0;
    run_block();
    fm1_in.notes = fm1_in.buttons = 0;
    run_block();
    for (i = 0; i < 16u; i++) {
        if (dstep_has(&TDRUM->dstep[i], 2))
            gh = dstep_lvl(&TDRUM->dstep[i], 2);
        if (dstep_has(&TDRUM->dstep[i], 0))
            hd = dstep_lvl(&TDRUM->dstep[i], 0);
    }
    check(gh == LV_GHOST && hd == LV_HARD, "OCT- / OCT+ held on the drums: a ghost snare and a hard kick recorded");
    {
        uint32_t vg = 0, vh = 0;
        for (i = 0; i < nhits; i++) {
            if (hits[i].note == 38)
                vg = hits[i].vel;
            if (hits[i].note == 36)
                vh = hits[i].vel;
        }
        check(vg == 42u && vh == 127u, "... and played at 42 / 127");
    }
}

static void t_chords(void)
{
    uint8_t c[4];
    uint32_t n;
    reset(120);
    trk[0].p[P_ROOT] = 0;
    trk[0].p[P_SCALE] = 2;                           /* C minor */
    trk[0].p[P_CHORD] = 3;                           /* 9TH: 1 3 7 9 */
    n = chord_notes(&trk[0], kb_map(&trk[0], 7), c); /* C4: i */
    check(n == 4u && c[0] == 60 && c[1] == 63 && c[2] == 70 && c[3] == 74, "chord 9TH on C4 in C minor: C Eb Bb D");
    trk[0].p[P_CHORD] = 1;
    n = chord_notes(&trk[0], kb_map(&trk[0], 9), c); /* D4: ii (dim) */
    check(n == 3u && c[0] == 62 && c[1] == 65 && c[2] == 68, "chord TRIAD on D4 in C minor: D F Ab");
    check(kb_map(&trk[0], 8) == KB_SILENT, "chord mode: the black keys are silent");
}

static void t_mute(void)
{
    uint32_t i, n0;
    reset(120);
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
    transport_req = 1;
    while (blk < 200u)
        run_block();
    TDRUM->p[P_MUTE] = 1;
    n0 = nhits;
    while (blk < 400u)
        run_block();
    check(nhits == n0, "drum track muted: no hits");
    TDRUM->p[P_MUTE] = 0;
    song.solo = 1u;                                  /* track 1 soloed: the drums silent */
    n0 = nhits;
    while (blk < 600u)
        run_block();
    check(nhits == n0 && TDRUM->att == 32767, "track 1 soloed: the drums silent (faded out)");
    song.solo = 0;
    while (blk < 800u)
        run_block();
    check(nhits > n0 && TDRUM->att == 0, "solo off: the drums back");
}

/* FX bypass (P_FXOFF): the track sounds as with no DIST, SLICER, sends; its values stay. eng < 0: the
 * track's power-on sound, else that engine's first preset (FM6: its own render and DC filter, the same bypass) */
static uint64_t fx_render_here(int dist, int rev, int slcr, int off, int eng)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i;
    reset(120);
    if (eng >= 0)
        host_preset(&trk[0], (uint32_t)eng, 0);
    trk[0].p[P_DIST] = (int16_t)dist;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = (int16_t)rev;
    trk[0].p[P_SLCR] = (int16_t)slcr;
    trk[0].p[P_FXOFF] = (int16_t)off;
    TDRUM->p[P_FXOFF] = (int16_t)off;
    host_drum_rev(rev ? 60 : 0);
    transport_req = 1;
    fm1_in.notes = 1u << 7;
    for (b = 0; b < 600u; b++) {
        int32_t out[CTL * 2];
        if (b == 300u)
            fm1_in.notes = 0;
        mix_block(out, CTL);
        for (i = 0; i < CTL * 2u; i++)
            h = (h ^ (uint32_t)out[i]) * 1099511628211ull;
    }
    fm1_in.notes = 0;
    return h;
}
/* each render in a child of the same parent: the same state before it (the engines keep some across reset) */
static uint64_t fx_render(int dist, int rev, int slcr, int off, int eng)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (pid == 0) {
        h = fx_render_here(dist, rev, slcr, off, eng);
        if (write(fd[1], &h, sizeof h) != (ssize_t)sizeof h)
            _exit(1);
        _exit(0);
    }
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h)
        h = 0;
    waitpid(pid, 0, 0);
    close(fd[0]);
    close(fd[1]);
    return h;
}
static void t_fxbypass(void)
{
    uint64_t dry = fx_render(0, 0, 0, 0, -1), dry2 = fx_render(0, 0, 0, 0, -1), byp = fx_render(90, 100, 1, 1, -1),
             wet = fx_render(90, 100, 1, 0, -1);
    uint64_t xdry = fx_render(0, 0, 0, 0, (int)ENG_IX_FM6), xbyp = fx_render(90, 100, 1, 1, (int)ENG_IX_FM6),
             xwet = fx_render(90, 100, 1, 0, (int)ENG_IX_FM6);
    check(dry == dry2 && dry, "FX renders: repeatable");
    check(byp == dry, "FX bypass: as dry as no DIST / SLICER / sends (bit for bit)");
    check(wet != dry, "FX on: the effects heard");
    check(str_eq(ENGINES[ENG_IX_FM6]->name, "FM6") && xdry && xbyp == xdry && xwet != xdry,
          "FX bypass on an FM6 track: as dry (bit for bit), FX on heard");
    fx_render_here(90, 100, 1, 1, -1);
    check(trk[0].p[P_DIST] == 90 && trk[0].p[P_REV] == 100 && trk[0].p[P_SLCR] == 1, "FX bypass: the values kept");
}

/* SLOOP 2.4 fixes (isod89/sloop-fm1 v2.4, 8d3823f, tests/seq2_test.c t_fixes24): swing is off on the triplet
 * grids; a DIV (or arp RATE) change in the first beat after PLAY does not lose the next step */
static void t_swing_triplets(void)
{
    uint32_t k, i, n = 0, ok = 1;
    uint64_t d[24];
    reset(120);
    song.g[G_SWING] = 80;
    TDRUM->p[P_SDIV] = 4;                             /* 8T: 3 a beat */
    TDRUM->p[P_SLEN] = 12;
    for (i = 0; i < 12u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    for (k = 0; k < 8u * (uint32_t)(FS / CTL) / 2u; k++) run_block();   /* 4 s = 8 beats */
    for (i = 0; i < nhits && n < 24u; i++)
        d[n++] = hits[i].blk;
    for (i = 2; i < n; i++)                           /* evenly spaced: gaps within a block of each other */
        if (d[i] - d[i - 1] > d[i - 1] - d[i - 2] + 1u || d[i - 1] - d[i - 2] > d[i] - d[i - 1] + 1u)
            ok = 0;
    check(n >= 20u && ok, "SWING on the 8T grid does nothing (triplets stay even)");
    reset(120);
    song.g[G_SWING] = 80;
    TDRUM->p[P_SDIV] = 5;                             /* 16T: 6 a beat */
    TDRUM->p[P_SLEN] = 12;
    for (i = 0; i < 12u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    for (k = 0; k < 4u * (uint32_t)(FS / CTL) / 2u; k++) run_block();   /* 4 beats */
    n = 0, ok = 1;
    for (i = 0; i < nhits && n < 24u; i++)
        d[n++] = hits[i].blk;
    for (i = 2; i < n; i++)
        if (d[i] - d[i - 1] > d[i - 1] - d[i - 2] + 1u || d[i - 1] - d[i - 2] > d[i] - d[i - 1] + 1u)
            ok = 0;
    check(n >= 20u && ok, "SWING on the 16T grid does nothing");
    song.g[G_SWING] = 0;
    transport_req = 2; run_block();
}
static void t_div_after_play(void)
{
    uint32_t i, k, n0, a0;
    track_t *t = &trk[0];
    reset(120);
    TDRUM->p[P_SDIV] = 1;                             /* 1/8 */
    TDRUM->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    run_block();
    while (clk_beat < 1u && clk_pos < BEAT_U * 6u / 10u) run_block();   /* 60 % into beat 1: step 1 of 1/8 */
    TDRUM->p[P_SDIV] = 0;                             /* 1/4 */
    n0 = nhits;
    while (clk_beat < 1u) run_block();                /* to the start of beat 2 */
    for (k = 0; k < 3u; k++) run_block();
    check(nhits > n0, "DIV 1/8 -> 1/4 in the first beat: the step on beat 2 plays");
    transport_req = 2; run_block();

    reset(120);
    t->p[P_AMODE] = 1;
    t->p[P_ARATE] = 1;                                /* 1/8 */
    t->p[P_APROB] = 127;
    input_on(t, 60, 100);
    transport_req = 1;
    run_block();
    while (clk_beat < 1u && clk_pos < BEAT_U * 6u / 10u) run_block();
    t->p[P_ARATE] = 0;                                /* 1/4 */
    a0 = t->arp_idx;
    while (clk_beat < 1u) run_block();
    for (k = 0; k < 3u; k++) run_block();
    check(t->arp_idx != a0, "arp RATE 1/8 -> 1/4 in the first beat: the arp step on beat 2 plays");
    trk_note_off(t, 60);
    t->p[P_AMODE] = 0;
    transport_req = 2; run_block();
}

int main(void)
{
    t_drift();
    t_burst();
    t_swing_odd();
    t_len_phase();
    t_ratchet();
    t_roll();
    t_erase_undo();
    t_levels();
    t_chords();
    t_mute();
    t_fxbypass();
    t_swing_triplets();
    t_div_after_play();
    printf("seq2: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
