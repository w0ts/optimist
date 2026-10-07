/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 isod89 (SLOOP 2.4, github.com/isod89/sloop-fm1 v2.4 8d3823f, seq.c: seq_out_on / seq_out_off /
 * seq_out_track_off / seq_out_all_off, the note set per track; "IN = CLOCK" in events_block) */
/* The MIDI side of the sequencer (included by seq.c, after trk_index):
 *
 *   trk_midi_ch(i)       the MIDI channel 0..15 of track i, MCH_OFF when it has none. FELUCCA_MIDI_CH: each track's own
 *                        (BPS_CH0..CH2, the drum track's G_DRCH; 1..16 or OFF, per project). Without: part i -> channel
 *                        i + 1, the drums G_DRCH (0 = channel 10), as before.
 *   midi_track_idx(ch)   the track a note on channel ch plays, -1 when none does. FELUCCA_MIDI_CH: the track that has
 *                        the channel (the first one when two have it); a channel no track has plays the selected
 *                        track (as before), unless its channel is OFF. Without: part ch, the drums on G_DRCH,
 *                        else the selected track.
 *   midi_key_out(...)    a key of the panel to MIDI OUT (always, as before; nothing on a channel that is OFF)
 *
 *   MIDI OUT = SEQ (FELUCCA_MIDI_OUT, HOME menu; SLOOP 2.4): what the sequencer, the arp and the rolls play goes
 *   out too, on the track's channel (the drums on theirs). Rules (as 2.4):
 *     - every note is ended: a set of the notes sent per track, a note on again while it is on is ended first;
 *       a step's, a gate's, an arp note's, a roll's end sends its note-off; the drum hits end with the next step;
 *     - STOP ends what is still on, and so does MIDI OUT back to KEYS, a track's channel changed or set to OFF
 *       (the off goes on the channel the note went out on);
 *     - notes that came in from MIDI (or the keys: they have their own path) are never sent back: only what the
 *       sequencer, the arp and the rolls play. The arp plays what is held, so a held MIDI note arpeggiated goes out
 *       as the arp's notes: that is the arp's output, not an echo of the note.
 *   Called from the audio interrupt (events_block): no waits, midi_out_event only queues. */
#define MCH_OFF 0xFFu

static uint32_t trk_midi_ch(uint32_t i)
{
#if FELUCCA_MIDI_CH
    uint32_t c = i < NPART ? (uint32_t)bp_set[BPS_CH0 + i] : (uint32_t)song.g[G_DRCH];
    return c >= 1u && c <= 16u ? c - 1u : MCH_OFF;
#else
    if (i < NPART)
        return i;
    return song.g[G_DRCH] ? (uint32_t)song.g[G_DRCH] - 1u : 9u;
#endif
}

static int midi_track_idx(uint32_t ch)
{
#if FELUCCA_MIDI_CH
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        if (trk_midi_ch(i) == ch)
            return (int)i;
    i = song.sel % NTRK;
    return trk_midi_ch(i) == MCH_OFF ? -1 : (int)i;     /* (a channel nobody has: the selected track, as before) */
#else
    if (song.g[G_DRCH] && ch + 1u == (uint32_t)song.g[G_DRCH])
        return TRK_DRUM;
    return ch < NPART ? (int)ch : (int)(song.sel % NTRK);
#endif
}

/* a key of the panel: the note on / off on channel mc (0..15) to MIDI OUT; vel = 0 (a note off) */
static void midi_key_out(uint32_t mc, uint32_t note, uint32_t vel)
{
    if (mc == MCH_OFF)
        return;
    if (vel)
        midi_out_event(0x09u | (0x90u | mc) << 8 | note << 16 | vel << 24);
    else
        midi_out_event(0x08u | (0x80u | mc) << 8 | note << 16);
}

#if FELUCCA_MIDI_OUT
static uint32_t mo_set[NTRK][4];           /* per track: the notes sent on and not yet off */
static uint8_t mo_ch[NTRK];                /* the channel they went out on */
static uint8_t mo_any;                     /* something was sent on since the last time all ended (events_block) */
static void seq_out_off(const track_t *t, uint32_t note)
{
    uint32_t i = trk_index(t) % NTRK;
    if (note > 127u || !(mo_set[i][note >> 5] & (1u << (note & 31u))))
        return;
    mo_set[i][note >> 5] &= ~(1u << (note & 31u));
    midi_key_out(mo_ch[i], note, 0u);
}
static void seq_out_track_off(const track_t *t)    /* every note of the track still on */
{
    uint32_t i = trk_index(t) % NTRK, w, b;
    for (w = 0; w < 4u; w++)
        for (b = 0; mo_set[i][w]; b++)
            if (mo_set[i][w] & (1u << b)) {
                mo_set[i][w] &= ~(1u << b);
                midi_key_out(mo_ch[i], w * 32u + b, 0u);
            }
}
static int seq_out_held(const track_t *t, uint32_t note)    /* its note is on (a slide into it: one MIDI note) */
{
    return note < 128u && (mo_set[trk_index(t) % NTRK][note >> 5] & (1u << (note & 31u))) != 0u;
}
static void seq_out_on(const track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t i = trk_index(t) % NTRK, ch = trk_midi_ch(i), w;
    if (!bp_set[BPS_MOUT] || note > 127u || ch == MCH_OFF)
        return;
    for (w = 0; w < 4u && !mo_set[i][w]; w++)
        ;
    if (w < 4u && mo_ch[i] != ch)
        seq_out_track_off(t);                  /* its channel changed under the notes: they end where they began */
    seq_out_off(t, note);                      /* played again while on: off first */
    mo_ch[i] = (uint8_t)ch;
    mo_set[i][note >> 5] |= 1u << (note & 31u);
    mo_any = 1;
    midi_key_out(ch, note, vel ? vel & 127u : 1u);
}
static void seq_out_all_off(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        seq_out_track_off(&trk[i]);
    mo_any = 0;
}
/* once a block: MIDI OUT back to KEYS, or a track's channel changed / OFF: what was sent ends */
static void seq_out_check(void)
{
    uint32_t i, w, busy = 0;
    if (!mo_any)
        return;
    for (i = 0; i < NTRK; i++) {
        for (w = 0; w < 4u && !mo_set[i][w]; w++)
            ;
        if (w == 4u)
            continue;
        if (!bp_set[BPS_MOUT] || trk_midi_ch(i) != mo_ch[i])
            seq_out_track_off(&trk[i]);
        else
            busy = 1;
    }
    mo_any = (uint8_t)busy;
}
#else
#define seq_out_on(t, n, v) ((void)0)
#define seq_out_off(t, n) ((void)0)
#define seq_out_held(t, n) 0
#define seq_out_track_off(t) ((void)0)
#define seq_out_all_off() ((void)0)
#define seq_out_check() ((void)0)
#endif

/* MIDI in with IN = CLOCK: a note on is dropped (the note offs still end what was held) */
static int midi_in_dropped(uint32_t st, uint32_t d2)
{
#if FELUCCA_MIDI_INCLK
    return bp_set[BPS_MIN] && st == 0x90u && d2;
#else
    (void)st, (void)d2;
    return 0;
#endif
}
