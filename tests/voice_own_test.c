/* SPDX-License-Identifier: GPL-3.0-only */
/* VOICE_OWN (core/voice.c voice_alloc; JIANT 0.9.9 cpu3 da61bc62): the shared budget (NVOICE) full, a part with a released
 * voice of its own restarts that one; the other parts' tails ring on.
 *   own       T2 has 7 old released tails, T1 one newer released voice: T1's next note takes T1's own voice, none of T2's
 *             faded (without the item: T2's oldest tail faded, the global victim)
 *   none      T1 with no released voice of its own: one of T2's taken, as before
 *   held      T2's held notes: never faded for it either way
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("voice_own: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void reset(void)
{
    uint32_t i;
    seq_stop();
    transport_req = 0;
    for (i = 0; i < NTRK; i++)
        memset(trk[i].v, 0, sizeof trk[i].v);
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    for (i = 0; i < 2u; i++) {                         /* T1 T2: ANALOG, POLY, a long release (tails that last) */
        host_preset(&trk[i], ENG_SLOT_ANALOG, 0);
        trk[i].p[P_VOICE] = V_POLY;
        trk[i].p[P_REL] = 127;
    }
}
static void blocks(uint32_t nb)
{
    int32_t out[CTL * 2];
    while (nb--)
        mix_block(out, CTL);
}
static uint32_t count(const track_t *t, int fading, int held)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && (fading ? t->v[i].stage == 4u : held ? t->v[i].gate : (!t->v[i].gate && t->v[i].stage != 4u));
    return n;
}

int main(void)
{
    uint32_t i;
    /* own: T2's 7 tails (older), T1's 1 (newer); the budget full; T1 plays a new note */
    reset();
    for (i = 0; i < NVOICE - 1u; i++)
        trk_note_on(&trk[1], 40 + i, 100);
    blocks(4);
    for (i = 0; i < NVOICE - 1u; i++)
        trk_note_off(&trk[1], 40 + i);
    blocks(4);
    trk_note_on(&trk[0], 60, 100);
    blocks(4);
    trk_note_off(&trk[0], 60);
    blocks(4);
    check(count(&trk[1], 0, 0) == NVOICE - 1u && count(&trk[0], 0, 0) == 1u, "set-up: T2 7 tails, T1 1 tail: the budget full");
    trk_note_on(&trk[0], 64, 100);
    printf("voice_own: after T1's new note: T2 tails %u fading %u; T1 held %u tails %u\n", count(&trk[1], 0, 0),
           count(&trk[1], 1, 0), count(&trk[0], 0, 1), count(&trk[0], 0, 0));
#if FELUCCA_VOICE_OWN
    check(count(&trk[1], 1, 0) == 0u && count(&trk[1], 0, 0) == NVOICE - 1u, "own: none of T2's tails faded");
    check(count(&trk[0], 0, 1) == 1u && count(&trk[0], 0, 0) == 0u, "own: T1's own released voice took the note");
#else
    check(count(&trk[1], 1, 0) == 1u, "VOICE_OWN off: T2's oldest tail faded (the global victim, as before)");
#endif
    blocks(40);

    /* none: T1 has no released voice: a voice from T2 as before */
    reset();
    for (i = 0; i < NVOICE; i++)
        trk_note_on(&trk[1], 40 + i, 100);
    blocks(4);
    for (i = 0; i < NVOICE; i++)
        trk_note_off(&trk[1], 40 + i);
    blocks(4);
    trk_note_on(&trk[0], 64, 100);
    check(count(&trk[1], 1, 0) == 1u && count(&trk[0], 0, 1) == 1u, "none: T1 with no tail of its own takes one of T2's");
    blocks(40);

    /* held: T2 holds 6, has 1 tail; T1 one tail; T1's new note: T2's held notes untouched */
    reset();
    for (i = 0; i < NVOICE - 1u; i++)
        trk_note_on(&trk[1], 40 + i, 100);
    blocks(4);
    trk_note_off(&trk[1], 40);
    trk_note_on(&trk[0], 60, 100);
    blocks(4);
    trk_note_off(&trk[0], 60);
    blocks(4);
    trk_note_on(&trk[0], 64, 100);
    check(count(&trk[1], 0, 1) == NVOICE - 2u, "held: T2's held notes never faded for T1's");
    printf("voice_own: %d failed\n", fails);
    return fails != 0;
}
