/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP 2.3 / X0X 0.10.1 backports (firmware/src/core/backports23.h), each with its switch on:
 *   mono     a key let go just after a VOICE change leaves no stuck note (FELUCCA_MONO_RELEASE)
 *   recmode  the REC screen's MODE (FREE / TEMPO) and START (NOTE / COUNT: one bar of clicks) (FELUCCA_REC_MODES;
 *            SLOOP 2.3's t_recmode)
 *   glide    a fast LEVEL turn (a step every UI frame), a PAN jump, MASTER halved: the gain each block starts from
 *            moves by at most ~7 % of a step a block and settles exactly (FELUCCA_GLIDE, X0X 0.10.1); printed for
 *            the build without the switch too (the whole step in one block)
 *   shed     overload: a releasing voice first, then the oldest held one that is not a bass or a lead, faded
 *            (FELUCCA_SHED_FADE; after SLOOP 2.3's t_shed)
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("bp23: %-74s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t clicks;                              /* the click's wood blocks (76, 77) started */
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k;
    mix_block(out, CTL);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && (drums.v[k].note == 76u || drums.v[k].note == 77u))
                clicks++;
}
static void run_ms(uint32_t ms)
{
    uint32_t k, n = ms * (FS / 1000u) / CTL + 1u;
    for (k = 0; k < n; k++)
        run_block();
}
static uint32_t gated(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate;
    return n;
}

/* MONO, a key held; VOICE -> POLY; the key let go; VOICE -> MONO; another key pressed and let go: nothing
 * may still sound (2.2: the first key stayed in the MONO stack and came back, held for ever) */
static void t_mono_release(void)
{
    track_t *t = &trk[0];
    host_tracks_init();
    t->p[P_VOICE] = V_MONO;
    trk_note_on(t, 60, 100);
    run_ms(20);
    t->p[P_VOICE] = V_POLY;
    trk_note_off(t, 60);
    run_ms(20);
    t->p[P_VOICE] = V_MONO;
    trk_note_on(t, 64, 100);
    run_ms(20);
    trk_note_off(t, 64);
    run_ms(20);
    check(gated(t) == 0u && t->nmono == 0u, "VOICE MONO -> POLY with a key held, let go, back to MONO: no stuck note");
}

#if FELUCCA_SHED_FADE
static void t_shed(void)
{
    track_t *t = &trk[0], *m = &trk[1];
    uint32_t i, k, low_ok = 1, lead_ok = 1, faded = 0;
    host_tracks_init();
    t->p[P_VOICE] = V_POLY;
    m->p[P_VOICE] = V_MONO;
    t->p[P_ATK] = 0;
    t->p[P_REL] = 100;                                /* a long release: the released voice still rings */
    trk_note_on(t, 48, 100);                          /* the bass, first and lowest */
    trk_note_on(t, 64, 100);
    trk_note_on(t, 67, 100);
    trk_note_on(t, 72, 100);
    trk_note_on(m, 40, 100);                          /* a MONO lead */
    trk_note_on(t, 76, 100);
    run_ms(15);
    trk_note_off(t, 76);                              /* releasing */
    run_block();
    shed_voice();
    for (k = 0, i = 0; i < NVOICE; i++)
        k += t->v[i].note == 76 && t->v[i].stage == 4u;
    check(k == 1u, "overload: the releasing voice goes first, faded (stage 4)");
    for (k = 0; k < 6u; k++)
        shed_voice();
    for (i = 0; i < NVOICE; i++) {
        if (t->v[i].note == 48 && t->v[i].active && t->v[i].stage == 4u)
            low_ok = 0;
        if (m->v[i].note == 40 && i == 0u && m->v[i].stage == 4u)
            lead_ok = 0;
        faded += t->v[i].active && t->v[i].stage == 4u && (t->v[i].note == 64 || t->v[i].note == 67 || t->v[i].note == 72);
    }
    for (k = 0, i = 0; i < NVOICE; i++)
        k += t->v[i].active && t->v[i].gate && t->v[i].stage != 4u &&
             (t->v[i].note == 64 || t->v[i].note == 67 || t->v[i].note == 72);
    check(low_ok && lead_ok && k == 0u && faded == 3u, "overload: the upper notes fade out, the bass and the MONO lead stay");
    run_ms(30);
    check(gated(t) == 1u && t->v[lowest_held(t)].note == 48 && gated(m) == 1u, "overload: ... and keep sounding");
}
#endif

#if FELUCCA_REC_MODES
static void rec_reset(uint32_t bpm)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    song.g[G_BPM] = (int16_t)bpm;
    song.g[G_CLOCK] = 0;
    song.rec = 0;
    song.playing = 0;
    song.sel = 0;
    rec_wait = 0;
    ft_on = 0;
    ci_on = 0;
    transport_req = 0;
}
static void t_recmode(void)
{
    uint32_t k, bpb = (uint32_t)((double)FS * 60.0 / 100.0 / CTL + 0.5), c0;
    rec_reset(100);
    rec_tempo = 0, rec_count = 0;
    rec_wait = 1;
    input_on(TSEL, 60, 100);
    run_block();
    check(ft_on && !song.playing, "REC MODE FREE, empty project: the first note starts a free take");
    transport_req = 2; run_block(); ft_bars = 0;
    trk_note_off(TSEL, 60);

    rec_reset(100);
    rec_tempo = 1, rec_count = 0;
    rec_wait = 1;
    input_on(TSEL, 60, 100);
    run_block();
    check(!ft_on && song.playing && song.rec == 1u && song.g[G_BPM] == 100,
          "REC MODE TEMPO, empty project: the first note starts the loop at 100 BPM, recording");
    transport_req = 2; run_block();
    trk_note_off(TSEL, 60);

    rec_reset(100);
    rec_tempo = 1, rec_count = 1;
    rec_wait = 1;
    input_on(TSEL, 62, 100);
    run_block();
    check(!song.playing && !ci_on && !ft_on && rec_wait, "REC START COUNT: a note only sounds, nothing starts");
    trk_note_off(TSEL, 62);
    c0 = clicks;
    transport_req = 1;                                /* PLAY: the count-in */
    run_block();
    for (k = 0; k + 2u < 4u * bpb; k++) run_block();
    check(ci_on && !song.playing, "REC START COUNT: PLAY -> one bar of clicks, not playing yet");
    check(clicks - c0 == 4u, "REC START COUNT: 4 clicks (one bar of 4/4)");
    for (k = 0; k < 4u; k++) run_block();
    check(!ci_on && song.playing && song.rec == 1u && !rec_wait, "REC START COUNT: after the bar the loop starts and records");
    transport_req = 2; run_block();

    rec_reset(100);
    rec_tempo = 1, rec_count = 1;
    rec_wait = 1;
    transport_req = 1; run_block();
    for (k = 0; k < bpb; k++) run_block();
    rec_wait = 0;                                     /* REC again: cancelled */
    for (k = 0; k < 4u * bpb; k++) run_block();
    check(!ci_on && !song.playing && !song.rec, "REC START COUNT: REC during the count-in cancels it");
    rec_wait = 1;
    transport_req = 1; run_block();
    transport_req = 1; run_block();                   /* PLAY again: back to armed */
    check(!ci_on && rec_wait && !song.playing, "REC START COUNT: PLAY again during the count-in: back to armed");
    rec_wait = 0;
    rec_tempo = 0, rec_count = 0;
}
/* SLOOP 2.4 (isod89/sloop-fm1 v2.4, 8d3823f, tests/seq2_test.c t_fixes24): a MIDI START from a DAW during the
 * count-in starts and records at once (the master counts), not after the bar of clicks. Ours needed no change:
 * clock_sync.c starts the transport at the first F8 and the count-in stops when it sees it playing */
static void ci_block(double *next_f8, double period)
{
    run_block();
    host_now += (uint32_t)(CTL * 24e6 / FS);
    while (*next_f8 > 0.0 && (double)host_now >= *next_f8) {
        usb_midi_rx_packet(0xF80Fu, (uint32_t)*next_f8);   /* the DAW's clock, 24 PPQN */
        *next_f8 += period;
    }
}
static void t_count_in_start(void)
{
    uint32_t k, started = 0, at = 0;
    double next_f8 = 0.0, period = 24e6 * 60.0 / 120.0 / 24.0;
    rec_reset(120);
    song.g[G_SYNC] = SYNC_AUTO;
    rec_tempo = 1, rec_count = 1;
    rec_wait = 1;
    transport_req = 1; ci_block(&next_f8, period);    /* PLAY: the count-in clicks (no clock yet) */
    for (k = 0; k < (uint32_t)(FS / 2u / CTL); k++) ci_block(&next_f8, period);   /* half a second: a beat */
    check(ci_on && !song.playing, "count-in: running before the DAW starts");
    usb_midi_rx_packet(0xFA0Fu, host_now);            /* the DAW starts: START, then its clock */
    next_f8 = (double)host_now + 24e6 * 0.001;
    for (k = 0; k < (uint32_t)(FS / 2u / CTL) && !started; k++) {   /* within half a second (the count-in: 1.5 s left) */
        ci_block(&next_f8, period);
        if (song.playing) {
            started = 1;
            at = k;
        }
    }
    ci_block(&next_f8, period);                       /* (the next block: the count-in sees it playing, records) */
    check(started && !ci_on && song.rec == 1u && !rec_wait,
          "count-in: a MIDI START during it starts and records at once (the master counts)");
    if (!started || at > 4u)
        printf("bp23:   started %u, %u blocks after START\n", started, at);
    transport_req = 2; ci_block(&next_f8, period);
    next_f8 = 0.0;
    for (k = 0; k < (uint32_t)(FS / CTL); k++) ci_block(&next_f8, period);   /* (the clock gone: AUTO lets it go) */
    song.g[G_SYNC] = SYNC_INT;
    rec_wait = 0;
    rec_tempo = 0, rec_count = 0;
}
#endif

/* the gain trajectories the mixer applies, block by block (where each block's ramp starts) */
static void t_glide(void)
{
    track_t *t = &trk[0];
    uint32_t k;
    int32_t prev = -1, maxd = 0, step = 0, cur = 0, lvl_end;
    host_tracks_init();
    t->p[P_LEVEL] = 120;
    trk_note_on(t, 48, 100);
    run_ms(30);
    for (k = 0; k < 440u; k++) {                      /* 20 UI frames of 22 blocks: LEVEL -4 each frame (a fast turn) */
        if (k % 22u == 0u && k < 440u)
            t->p[P_LEVEL] = (int16_t)(t->p[P_LEVEL] - 4);
#if FELUCCA_GLIDE
        cur = t->gl_v;
#else
        cur = t->lvl;
#endif
        if (prev >= 0 && (prev - cur > maxd || cur - prev > maxd))
            maxd = prev - cur > 0 ? prev - cur : cur - prev;
        prev = cur;
        run_block();
    }
    step = LEVEL_Q12[120] - LEVEL_Q12[116];          /* (the largest step of the turn: its first) */
    run_ms(200);
#if FELUCCA_GLIDE
    lvl_end = t->gl_v;
#else
    lvl_end = t->lvl;
#endif
    printf("bp23: glide %s: LEVEL -4 a UI frame: the largest move in one block %d (Q12; the largest -4 step, 120 -> 116: %d)\n",
           FELUCCA_GLIDE ? "on" : "off", maxd, step);
#if FELUCCA_GLIDE
    check(maxd * 100 <= step * 8 && lvl_end == LEVEL_Q12[t->p[P_LEVEL]], "glide: a LEVEL step moves <= 8 % of itself a block, and settles exactly");
    {   /* MASTER halved at once: ~10 ms to 63 %, settled exactly */
        uint32_t b63 = 0;
        song.master_q12 = 4096;
        run_ms(50);
        song.master_q12 = 2048;
        for (k = 0; k < 400u; k++) {
            run_block();
            if (!b63 && master_cur <= 4096 - (2048 * 63) / 100)
                b63 = k + 1u;
        }
        printf("bp23: glide: MASTER 4096 -> 2048: 63 %% after %u blocks (%.1f ms)\n", b63, b63 * CTL * 1000.0 / FS);
        check(b63 >= 12u && b63 <= 16u && master_cur == 2048, "glide: MASTER halved: 63 % in ~10 ms, then exactly there");
    }
    {   /* PAN hard right at once */
        int32_t gl0;
        t->p[P_PAN] = 0;
        run_ms(50);
        gl0 = t->gl_pl;
        t->p[P_PAN] = 64;
        run_block();
        check(t->gl_pl < gl0 && gl0 - t->gl_pl < 4096 / 10, "glide: PAN hard right: the left gain starts down by < 10 % a block");
        run_ms(200);
        check(t->gl_pl == 0 && t->gl_pr == 4096, "glide: ... and gets there exactly");
    }
    {   /* the drum track: GLO > DRUMS LEVEL down while a hit sounds */
        int32_t lv0;
        memset(&drums, 0, sizeof drums);
        drums.set = -2;
        song.g[G_DRLVL] = 100;
        drum_on(36u, 120u);
        run_ms(5);
        lv0 = dgl.lv;
        song.g[G_DRLVL] = 20;
        run_block();
        check(lv0 == 100 * 200 && dgl.lv < lv0 && lv0 - dgl.lv <= (80 * 200) / 12,
              "glide: DRUMS LEVEL 100 -> 20 under a hit: one block moves < 1/12 of the way");
    }
#endif
    trk_note_off(t, 48);
    run_ms(100);
}

int main(void)
{
    t_mono_release();
    t_glide();
#if FELUCCA_REC_MODES
    t_recmode();
    t_count_in_start();
#endif
#if FELUCCA_SHED_FADE
    t_shed();
#endif
    printf("%s\n", fails ? "BP23 TEST FAILED" : "bp23 test passed");
    return fails;
}
