/* SPDX-License-Identifier: GPL-3.0-only */
/* The arp plays the sequencer's notes as it plays the keys' (seq.c arp_seq_on / arp_seq_off / seq_note_end): a step's
 * notes are the arp's held notes while they would sound, the arp plays them in real time on its own grid (RATE, MODE,
 * OCT, GATE, PROB), and their end lets them go. The user, 2026-10-17: "it should work whether the notes are played or
 * sequenced".
 *   chord       C E G on step 1, tied over the first quarter, ARP UP at 1/32: the arp walks the chord, those notes only
 *   end         the rests after it: the arp stops, nothing held, no note left on
 *   ARP off     the same pattern: the step sounds as ever, the arp plays nothing
 *   REC         the steps' own arp is not recorded back into the pattern (the rests stay rests)
 *   keys        a key held over the sequenced chord joins it; the step's end leaves the key's note, the key-up the rest
 * Exit status: the failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("arp seq: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void block(void)
{
    int32_t out[CTL * 2];
    mix_block(out, CTL);
}
static void reset(uint32_t amode)
{
    track_t *t = &trk[0];
    uint32_t i;
    seq_stop();
    transport_req = 0;
    host_tracks_init();
    host_preset(t, 0, 0);
    song.g[G_BPM] = 120;
    t->p[P_VOICE] = V_POLY;
    t->p[P_AMODE] = (int16_t)amode;                      /* 1 UP */
    t->p[P_ARATE] = 3;                                   /* 1/32: two arp notes a step */
    t->p[P_AOCT] = 1;
    t->p[P_AORDER] = 0;
    t->p[P_AHOLD] = 0;
    t->p[P_APROB] = 127;
    t->p[P_SLEN] = 16;
    t->nheld = 0;
    t->arp_phys = 0;
    t->arp_note = 0;
    song.sel = 0;
    song.rec = 0;
    rec_wait = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    put_step(t, 0, 3, (const uint8_t[]){60, 64, 67}, ST_NOTE, 0);
    for (i = 1; i < 4u; i++)
        put_step(t, i, 0, 0, ST_TIE, 0);                 /* the chord a quarter long */
    for (i = 4; i < 16u; i++)
        put_step(t, i, 0, 0, ST_REST, 0);
    block();
}
/* blocks until the track's step reaches stop (or a bound): the arp's notes as they start, in *seq (at most 32), -> count */
static uint32_t run_to(uint32_t stop, uint8_t *seq)
{
    track_t *t = &trk[0];
    uint32_t n = 0, k, last = 0;
    for (k = 0; k < 20000u && !(song.playing && t->seq_idx >= stop && t->seq_idx < 15u); k++) {
        block();
        if (t->arp_note && t->arp_note != last && n < 32u)
            seq[n++] = t->arp_note;
        last = t->arp_note;
    }
    return n;
}

int main(void)
{
    track_t *t = &trk[0];
    uint8_t seq[32];
    uint32_t n, i, only = 1, saw60 = 0, saw64 = 0, saw67 = 0;

    reset(1);
    transport_req = 1;
    n = run_to(3, seq);
    for (i = 0; i < n; i++) {
        only &= seq[i] == 60 || seq[i] == 64 || seq[i] == 67;
        saw60 |= seq[i] == 60, saw64 |= seq[i] == 64, saw67 |= seq[i] == 67;
    }
    printf("arp seq: the first quarter: %u arp notes:", n);
    for (i = 0; i < n; i++)
        printf(" %u", seq[i]);
    printf("\n");
    check(n >= 4u && only && saw60 && saw64 && saw67, "C E G on step 1 tied a quarter, ARP UP 1/32: the arp walks the chord (its notes only)");
    check(n >= 2u && seq[0] == 60 && seq[1] == 64, "... in order, from the lowest (UP)");
    run_to(6, seq);
    check(t->nheld == 0u && !t->arp_note, "the rests after it: nothing held, the arp's note ended");

    reset(0);
    transport_req = 1;
    n = run_to(3, seq);
    check(n == 0u && t->nheld == 0u, "ARP off: the step sounds as ever, the arp plays nothing");

    reset(1);
    song.rec = 1;                                        /* track 1 recording */
    transport_req = 1;
    run_to(3, seq);
    run_to(8, seq);
    for (i = 4, only = 1; i < 8u; i++)
        only &= t->step[i].time == ST_REST && t->step[i].n == 0u;
    check(only, "REC on: the steps' own arp is not written back into them (the rests stay rests)");
    song.rec = 0;

    reset(1);
    transport_req = 1;
    run_to(1, seq);
    arp_add(t, 72);                                      /* a key (MIDI in) held over the chord */
    check(t->nheld == 4u, "a key held over the sequenced chord: it joins (4 notes held)");
    run_to(6, seq);
    check(t->nheld == 1u && t->held[0] == 72, "the step's end: its notes go, the key's stays");
    arp_remove(t, 72);
    check(t->nheld == 0u, "the key let go: nothing held");

    /* REC with the arp on: what is played is recorded (a chord as a chord), not what the arp makes of it */
    reset(1);
    for (i = 0; i < 16u; i++)
        put_step(t, i, 0, 0, ST_REST, 0);
    song.rec = 1;
    transport_req = 1;
    run_to(8, seq);
    input_on(t, 60, 100);
    input_on(t, 64, 100);
    for (i = 0; i < 400u; i++)
        block();
    input_off(t, 60);
    input_off(t, 64);
    for (i = 0; i < 100u; i++)
        block();
    {
        uint32_t k, first = 99, notes = 0, only2 = 1;
        for (k = 0; k < 16u; k++)
            if (t->step[k].time == ST_NOTE && t->step[k].n) {
                if (first == 99u)
                    first = k;
                notes += t->step[k].n;
                for (i = 0; i < t->step[k].n; i++)
                    only2 &= t->step[k].note[i] == 60 || t->step[k].note[i] == 64;
            }
        check(first != 99u && t->step[first].n == 2u && only2, "REC, ARP UP: a chord played is a chord in its step (60 and 64), not the arp's walk through it");
        check(notes <= 4u, "... and no note per arp tick (the arp's output is not written back)");
    }
    song.rec = 0;

    /* the same on a MONO part: with the arp on the chord is still a chord (not one note a step), the arp walks it */
    reset(1);
    t->p[P_VOICE] = V_MONO;
    for (i = 0; i < 16u; i++)
        put_step(t, i, 0, 0, ST_REST, 0);
    song.rec = 1;
    transport_req = 1;
    run_to(8, seq);
    input_on(t, 60, 100);
    input_on(t, 64, 100);
    for (i = 0; i < 400u; i++)
        block();
    input_off(t, 60);
    input_off(t, 64);
    for (i = 0; i < 100u; i++)
        block();
    {
        uint32_t k, first = 99;
        for (k = 0; k < 16u; k++)
            if (t->step[k].time == ST_NOTE && t->step[k].n) {
                first = k;
                break;
            }
        check(first != 99u && t->step[first].n == 2u, "REC, ARP UP, MONO part: the chord is two notes in its step");
    }
    /* DBG: overdub in the middle of a long note (a TIE step) */
    reset(1);
    t->p[P_VOICE] = V_MONO;
    for (i = 0; i < 16u; i++)
        put_step(t, i, 0, 0, ST_REST, 0);
    put_step(t, 4, 1, (const uint8_t[]){76}, ST_NOTE, 0);
    for (i = 5; i < 9u; i++)
        put_step(t, i, 0, 0, ST_TIE, 0);
    song.rec = 1;
    transport_req = 1;
    run_to(6, seq);
    input_on(t, 60, 100);
    for (i = 0; i < 150u; i++)
        block();
    input_off(t, 60);
    for (i = 0; i < 100u; i++)
        block();
    check(t->step[4].time == ST_NOTE && t->step[4].note[0] == 76 && t->step[5].time == ST_TIE, "overdub in a long note: its start and the tie before are untouched");
    check(t->step[6].time == ST_NOTE && t->step[6].n == 2u && t->step[6].note[0] == 76 && t->step[6].note[1] == 60 && (t->step[6].flags & SF_CARRY),
          "... the step played into: the long note goes on through it (carried, not struck again), the new note joins it");
    check(t->step[6].ngate[0] == 0u && t->step[6].ngate[1] != 0u, "... the new note ends with its step, the long one has no end of its own");
    check(t->step[7].time == ST_TIE && t->step[8].time == ST_TIE && t->step[9].time == ST_REST, "... the ties after it stay: the long note plays to its end");
    {   /* played back (the arp off): 76 sounds through steps 5..8, 60 only in step 6 */
        uint32_t has76 = 0, has60 = 0, in7_76 = 0, in7_60 = 0, k;
        song.rec = 0;
        t->p[P_AMODE] = 0;
        seq_stop();
        transport_req = 1;
        for (k = 0; k < 20000u && !(song.playing && t->seq_idx >= 6u); k++)
            block();
        for (k = 0; k < 40u; k++) {
            block();
            if (t->seq_idx == 6u)
                has60 |= t->v[0].note == 60 && t->v[0].gate, has76 |= t->v[0].note == 76;
        }
        for (k = 0; k < 3000u && t->seq_idx < 7u; k++)
            block();
        for (k = 0; k < 40u && t->seq_idx == 7u; k++) {
            uint32_t v;
            block();
            for (v = 0; v < NVOICE; v++) {
                in7_76 |= t->v[v].note == 76 && t->v[v].gate;
                in7_60 |= t->v[v].note == 60 && t->v[v].gate;
            }
        }
        printf("arp seq: playback: step 6: 60 sounds %u / 76 seen %u; step 7: 76 still held %u, 60 held %u\n", has60, has76, in7_76, in7_60);
        check(in7_76 && !in7_60, "played back: the long note goes on into step 7, the overdubbed note ended with its step");
    }
    song.rec = 0;
    /* overdub on a MONO part with the arp on: the notes there stay, the new one joins them */
    reset(1);
    t->p[P_VOICE] = V_MONO;
    for (i = 0; i < 16u; i++)
        put_step(t, i, 1, (const uint8_t[]){72}, ST_NOTE, 0);
    song.rec = 0;
    reset(1);
    t->p[P_VOICE] = V_MONO;
    for (i = 0; i < 16u; i++)
        put_step(t, i, 1, (const uint8_t[]){72}, ST_NOTE, 0);
    song.rec = 1;
    transport_req = 1;
    run_to(4, seq);
    input_on(t, 60, 100);
    for (i = 0; i < 300u; i++)
        block();
    input_off(t, 60);
    for (i = 0; i < 100u; i++)
        block();
    {
        uint32_t k, kept = 0, joined = 0, bad = 0;
        for (k = 0; k < 16u; k++) {
            uint32_t j, has72 = 0, has60 = 0;
            for (j = 0; j < t->step[k].n; j++)
                has72 |= t->step[k].note[j] == 72, has60 |= t->step[k].note[j] == 60;
            kept += has72;
            joined += has60;
            bad += !has72;
        }
        printf("arp seq: overdub: 72 kept in %u steps, 60 joined %u, steps that lost 72: %u\n", kept, joined, bad);
        check(bad == 0u && joined >= 1u, "REC overdub, ARP UP, MONO: the notes there stay (all 16 steps keep 72), the played one joins");
    }
    song.rec = 0;
    t->p[P_AMODE] = 0;
    for (i = 0; i < 16u; i++)
        put_step(t, i, 0, 0, ST_REST, 0);
    song.rec = 1;
    transport_req = 1;
    run_to(8, seq);
    input_on(t, 60, 100);
    input_on(t, 64, 100);
    for (i = 0; i < 100u; i++)
        block();
    input_off(t, 60);
    input_off(t, 64);
    {
        uint32_t k, first = 99;
        for (k = 0; k < 16u; k++)
            if (t->step[k].time == ST_NOTE && t->step[k].n) {
                first = k;
                break;
            }
        check(first != 99u && t->step[first].n == 1u, "... ARP off, MONO: one note a step, as ever");
    }
    song.rec = 0;

    printf("arp seq: %d failed\n", fails);
    return fails;
}
