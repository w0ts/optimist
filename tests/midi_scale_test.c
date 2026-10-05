/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Melodee: keremimo/melodee 12ccb56, tests/midi_scale_test.c: the oracle)
 * Copyright (C) 2026 isod89 (SLOOP: SNAP, chords, the held-note table) */
/* MIDI in through the key layouts: the queue -> midi_map -> voices, as the keys play.
 * WHITE (note 60 = the root, every scale, root, transpose; the panel octave ignored), SNAP, OFF,
 * the bypasses (drums, GM KIT), one-key chords, releases after a layout change. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static void reset(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_held, 0, sizeof midi_held);
    memset(kb_kind, 0, sizeof kb_kind);
    memset(&um, 0, sizeof um);
    midi_nheld = 0;
    host_tracks_init();
    fm1_in.notes = kb_prev = 0;
    mi_w = mi_r = 0;
    panic_req = transport_req = 0;
    for (i = 0; i < NPART; i++) {
        host_preset(&trk[i], 0, 1);
        trk[i].p[P_VOICE] = V_POLY;
        trk[i].p[P_AMODE] = trk[i].p[P_AHOLD] = 0;
        trk[i].p[P_SUS] = 127;
        trk[i].p[P_TRANS] = 0;
    }
}

static void send(uint32_t status, uint32_t note, uint32_t vel)
{
    midi_in_q[mi_w++ % MQ] = (status >> 4) | status << 8 | note << 16 | vel << 24;
    events_block(CTL);
}

static int gated(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}

static uint32_t ngated(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate;
    return n;
}

/* walk degree scale steps from the root (an independent oracle: Melodee 12ccb56) */
static int walk(uint32_t mask, int degree)
{
    int offset = 0;
    while (degree) {
        int dir = degree > 0 ? 1 : -1;
        offset += dir;
        if (mask & (1u << ((offset % 12 + 12) % 12))) degree -= dir;
    }
    return offset;
}

static void mapping_test(void)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    uint32_t s, root, note;
    int trans;
    reset();
    trk[0].p[P_QUANT] = 2;                /* WHITE */
    song.octave = 3;                      /* MIDI ignores the panel octave */
    for (s = 0; s <= (uint32_t)TP[P_SCALE].max; s++)
        for (root = 0; root < 12; root++)
            for (trans = -24; trans <= 24; trans += 24) {
                trk[0].p[P_SCALE] = (int16_t)s;
                trk[0].p[P_ROOT] = (int16_t)root;
                trk[0].p[P_TRANS] = (int16_t)trans;
                for (note = 0; note < 128; note++) {
                    int degree = DEGREE[note % 12];
                    uint32_t got = midi_map(&trk[0], note);
                    if (degree < 0) { assert(got == KB_SILENT); continue; }
                    degree += ((int)note / 12 - 5) * 7;
                    assert(got == (uint32_t)clamp(60 + (int)root + trans + walk(SCALE_MASK[s], degree), 0, 127));
                }
            }
    /* SNAP: every note, rounded down into the scale, transposed */
    trk[0].p[P_QUANT] = 1;
    for (s = 0; s <= (uint32_t)TP[P_SCALE].max; s++)
        for (root = 0; root < 12; root++) {
            trk[0].p[P_SCALE] = (int16_t)s;
            trk[0].p[P_ROOT] = (int16_t)root;
            trk[0].p[P_TRANS] = 5;
            for (note = 0; note < 128; note++) {
                int want = (int)note + 5;
                while (!(SCALE_MASK[s] & (1u << ((want - (int)root + 120) % 12)))) want--;
                assert(midi_map(&trk[0], note) == (uint32_t)clamp(want, 0, 127));
            }
        }
    trk[0].p[P_QUANT] = 0;                /* OFF: as received (no transpose: as before) */
    for (note = 0; note < 128; note++) assert(midi_map(&trk[0], note) == note);
    trk[0].p[P_QUANT] = 2;
    trk[0].engine = trk[0].eng_req = 4;   /* SAMPLE on the GM KIT: raw notes */
    if (drum_set() >= 0) {
        trk[0].p[P_E0] = (int16_t)drum_set();
        for (note = 0; note < 128; note++) assert(midi_map(&trk[0], note) == note);
    }
    puts("MIDI scales: WHITE (16 scales, 12 roots, all 128 notes, transpose, octave ignored), SNAP, OFF, GM KIT ok");
}

static void all_test(void)
{
    uint32_t s, root, note;
    int trans;
    reset();
    trk[0].p[P_QUANT] = Q_ALL;
    song.octave = -3;                      /* the panel octave never shifts MIDI */
    for (s = 0; s <= (uint32_t)TP[P_SCALE].max; s++)
        for (root = 0; root < 12; root++)
            for (trans = -24; trans <= 24; trans += 12) {
                int previous = -1;
                trk[0].p[P_SCALE] = (int16_t)s;
                trk[0].p[P_ROOT] = (int16_t)root;
                trk[0].p[P_TRANS] = (int16_t)trans;
                for (note = 0; note < 128; note++) {
                    int want = 60 + (int)root + trans + walk(SCALE_MASK[s], (int)note - 60);
                    uint32_t actual = midi_map(&trk[0], note);
                    if (want < 0 || want > 127) {
                        assert(actual == KB_SILENT);
                    } else {
                        assert(actual == (uint32_t)want && want > previous);
                        previous = want;
                    }
                }
            }
    reset();
    trk[0].p[P_QUANT] = Q_ALL;
    trk[0].p[P_SCALE] = 2;                 /* C minor: C C# D -> C D Eb */
    send(0x90, 60, 100); send(0x90, 61, 100); send(0x90, 62, 100);
    assert(gated(&trk[0], 60) && gated(&trk[0], 62) && gated(&trk[0], 63) && ngated(&trk[0]) == 3);
    send(0x80, 60, 0); send(0x80, 61, 0); send(0x80, 62, 0);
    assert(!ngated(&trk[0]));
    puts("MIDI ALL: all 128 notes, 16 scales, 12 roots, transpose; unique pitches, silent ends; black notes play ok");
}

static void play_test(void)
{
    track_t *t = &trk[0];
    reset();
    t->p[P_QUANT] = 2;
    t->p[P_SCALE] = 2;                    /* minor */
    t->p[P_ROOT] = 2;                     /* D */
    send(0x90, 60, 100);                  /* C4 = the root: D4 */
    assert(gated(t, 62) && ngated(t) == 1);
    send(0x90, 62, 100);                  /* D4 = the second degree: E4 */
    assert(gated(t, 64));
    send(0x90, 61, 100);                  /* a black note: silent */
    assert(ngated(t) == 2 && midi_find(0, 61) < 0);
    t->p[P_ROOT] = 7;                     /* the key changes while held: the releases end what started */
    send(0x80, 60, 0);
    send(0x80, 62, 0);
    send(0x80, 61, 0);
    assert(!ngated(t) && !midi_nheld);
    /* chords: one note, the chord of its degree; released together */
    reset();
    t->p[P_CHORD] = 1;                    /* TRIAD */
    t->p[P_SCALE] = 1;                    /* major, C */
    send(0x90, 60, 100);                  /* I: C E G */
    assert(gated(t, 60) && gated(t, 64) && gated(t, 67) && ngated(t) == 3);
    send(0x90, 62, 100);                  /* ii: D F A */
    assert(gated(t, 62) && gated(t, 65) && gated(t, 69) && ngated(t) == 6);
    t->p[P_CHORD] = 2;                    /* changed while held */
    send(0x80, 60, 0);
    assert(!gated(t, 60) && !gated(t, 64) && !gated(t, 67) && ngated(t) == 3);
    send(0x80, 62, 0);
    assert(!ngated(t));
    /* the pedal holds a chord, pedal up releases it all */
    send(0xB0, 64, 127);
    send(0x90, 64, 100); send(0x80, 64, 0);
    assert(ngated(t) == 4);               /* 7TH on iii: E G B D */
    send(0xB0, 64, 0);
    assert(!ngated(t) && !midi_nheld);
    /* recorded as a chord: the step holds the notes */
    reset();
    t->p[P_CHORD] = 1;
    t->p[P_SCALE] = 1;
    song.rec = 1;
    seq_start();
    send(0x90, 67, 100);                  /* V: G B D */
    {
        uint32_t i, k, found = 0;
        for (i = 0; i < NSTEP; i++)
            if (t->step[i].n == 3u) {
                for (k = 0; k < 3u; k++)
                    found |= (t->step[i].note[k] == 67u) << 0 | (t->step[i].note[k] == 71u) << 1 |
                             (t->step[i].note[k] == 74u) << 2;
            }
        assert(found == 7u);
    }
    send(0x80, 67, 0);
    puts("MIDI scales: notes and black notes in WHITE, releases after a key change, chords, pedal, recording ok");
}

int main(void)
{
    mapping_test();
    all_test();
    play_test();
    puts("MIDI SCALE TESTS PASSED");
    return 0;
}
