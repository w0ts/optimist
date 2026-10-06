/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP 2.3 / X0X 0.10.1 backports (firmware/src/backports23.h), each with its switch on:
 *   mono     a key let go just after a VOICE change leaves no stuck note (FELUCCA_MONO_RELEASE)
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
static void run_block(void)
{
    int32_t out[CTL * 2];
    mix_block(out, CTL);
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

int main(void)
{
    t_mono_release();
#if FELUCCA_SHED_FADE
    t_shed();
#endif
    printf("%s\n", fails ? "BP23 TEST FAILED" : "bp23 test passed");
    return fails;
}
