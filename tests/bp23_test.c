/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP 2.3 / X0X 0.10.1 backports (firmware/src/backports23.h), each with its switch on:
 *   mono     a key let go just after a VOICE change leaves no stuck note (FELUCCA_MONO_RELEASE)
 *   recmode  the REC screen's MODE (FREE / TEMPO) and START (NOTE / COUNT: one bar of clicks) (FELUCCA_REC_MODES;
 *            SLOOP 2.3's t_recmode)
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
#endif

int main(void)
{
    t_mono_release();
#if FELUCCA_REC_MODES
    t_recmode();
#endif
#if FELUCCA_SHED_FADE
    t_shed();
#endif
    printf("%s\n", fails ? "BP23 TEST FAILED" : "bp23 test passed");
    return fails;
}
