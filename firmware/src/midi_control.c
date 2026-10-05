/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChanceTheMaker, Kerem Kilic (Melodee: keremimo/melodee 670193c, midi_control.c)
 * Copyright (C) 2026 isod89 (SLOOP: the held-note table, our note input) */
/* MIDI channel messages from USB and TRS: notes, sustain, pitch bend, mod wheel, panic.
 * seq.c includes this after its note input (input_on / input_off, midi_track) and before events_block.
 *
 * API
 *   midi_event(st, ch, d1, d2)  one channel message from midi_in_q (status high nibble, channel, data)
 *   midi_forget_track(i)        track i changed its sound (preset / engine panic): its MIDI notes are
 *                               dropped without a note-off, so a later note-off or pedal-up cannot end
 *                               a note of the new sound
 *   midi_pitch_tick(t, n)       voice.c, once a block and part: the live bend + vibrato, Q8 semitones
 *   midi_ch[ch]                 per channel: bend, wheel (CC1), breath (CC2), foot (CC4), press (channel
 *                               aftertouch), porta (CC65), pedal (CC64), bend range (RPN 0)
 *   MIDI_EXPR_HOOK(t, c)        optional, #define before seq.c: an engine's own use of a channel's
 *                               controllers, called with each part they reach (FM6: wheel / foot /
 *                               breath / aftertouch routing)
 *   MIDI_CC_HOOK(ch, cc, v)     optional: the CCs this file does not use (FM6: CC5 portamento time)
 *
 * USB and TRS share the channel state. A synth part has one bend / wheel state: channels that reach
 * the same part share it (the last controller wins). Drum hits ignore bend, wheel and sustain.
 * A channel's controllers reach the track it plays (midi_track) and the tracks still holding its
 * notes. Bend and wheel are live state: never saved in a preset or a project, never recorded.
 *
 * Notes go through the track's key layout (midi_map: SCL › KEYS, CHORD, TRANSPOSE) as the keys do.
 * Held notes: a table of MIDI_HELD presses in press order (channel, received note, the track and the
 * notes it started), not a 16 x 128 map: a note-off ends what its note-on started even after a track,
 * scale or octave change. Full, the oldest press is released (more than the 8 voices sound anyway). */
#ifndef MIDI_EXPR_HOOK
#define MIDI_EXPR_HOOK(t, c) ((void)0)
#endif
#ifndef MIDI_CC_HOOK
#define MIDI_CC_HOOK(ch, cc, v) ((void)0)
#endif

typedef struct {
    int16_t bend;                       /* -8192..8191, 0 = centre */
    uint8_t wheel, breath, foot, press, porta, pedal;
    uint8_t ready, semis, cents;        /* RPN 0, the bend range: +-semis + cents / 100 */
    uint8_t rpn_msb, rpn_lsb;           /* the selected RPN, 127 = none */
} midi_chan_t;
static midi_chan_t midi_ch[16];

#define MIDI_HELD 32u
#define MH_PEDAL 0x80u                  /* (ch) released while the pedal was down: held by it */
typedef struct {
    uint8_t ch;                         /* channel | MH_PEDAL */
    uint8_t src;                        /* the note received */
    uint8_t trk;                        /* the track it plays */
    uint8_t n;                          /* the notes it started (a chord: up to 4) */
    uint8_t nt[4];
} midi_held_t;
static midi_held_t midi_held[MIDI_HELD];
static uint32_t midi_nheld;

static midi_chan_t *midi_chan(uint32_t ch)
{
    midi_chan_t *c = &midi_ch[ch & 15u];
    if (!c->ready) {
        c->semis = 2;
        c->rpn_msb = c->rpn_lsb = 127;
        c->ready = 1;
    }
    return c;
}

static int midi_find(uint32_t ch, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < midi_nheld; i++)
        if ((midi_held[i].ch & 15u) == ch && midi_held[i].src == note)
            return (int)i;
    return -1;
}

/* the tracks a channel's controllers reach: the one it plays, and those holding its notes */
static uint32_t midi_targets(uint32_t ch)
{
    uint32_t m = 1u << trk_index(midi_track(ch)), i;
    for (i = 0; i < midi_nheld; i++)
        if ((midi_held[i].ch & 15u) == ch)
            m |= 1u << midi_held[i].trk;
    return m;
}

static void midi_expr(track_t *t, const midi_chan_t *c)
{
    int32_t range = ((int32_t)c->semis * 100 + c->cents) * 256 / 100;   /* Q8 semitones */
    if (is_drum(t))
        return;
    t->bend_target = (int16_t)((int32_t)c->bend * range / (c->bend < 0 ? 8192 : 8191));
    t->wheel_target = (int16_t)(c->wheel * 256);
    MIDI_EXPR_HOOK(t, c);
}

static void midi_expr_channel(uint32_t ch)
{
    uint32_t i, mask = midi_targets(ch);
    for (i = 0; i < NPART; i++)
        if (mask & (1u << i))
            midi_expr(&trk[i], midi_chan(ch));
}

/* press i ends: out of the table, then its notes off (input_off: recording, arp, free take) */
static void midi_release_at(uint32_t i)
{
    midi_held_t h = midi_held[i];
    uint32_t k;
    for (midi_nheld--; i < midi_nheld; i++)
        midi_held[i] = midi_held[i + 1u];
    for (k = 0; k < h.n; k++)
        input_off(&trk[h.trk % NTRK], h.nt[k]);
}

static void midi_note_on(uint32_t ch, uint32_t note, uint32_t vel)
{
    track_t *t = midi_track(ch);
    midi_held_t *h;
    uint32_t k, mapped;
    int i = midi_find(ch, note);
    if (i >= 0)
        midi_release_at((uint32_t)i);             /* a repeated note replaces its press (pedal-held too) */
    if (is_drum(t)) {
        input_on(t, note, vel);                   /* a one-shot: nothing to release */
        return;
    }
    mapped = midi_map(t, note);                   /* the track's layout: WHITE, SNAP, chords */
    if (mapped == KB_SILENT)
        return;
    if (midi_nheld == MIDI_HELD)
        midi_release_at(0);                       /* full: the oldest press goes */
    midi_expr(t, midi_chan(ch));
    h = &midi_held[midi_nheld++];
    h->ch = (uint8_t)ch;
    h->src = (uint8_t)note;
    h->trk = (uint8_t)trk_index(t);
    if (t->p[P_CHORD])                            /* one key, a chord (SCL › CHORD) */
        h->n = (uint8_t)chord_notes(t, mapped, h->nt);
    else {
        h->n = 1;
        h->nt[0] = (uint8_t)mapped;
    }
    for (k = 0; k < h->n; k++)
        input_on(t, h->nt[k], vel);
}

static void midi_note_off(uint32_t ch, uint32_t note)
{
    int i = midi_find(ch, note);
    if (i < 0)
        return;
    if (midi_chan(ch)->pedal)
        midi_held[i].ch |= MH_PEDAL;              /* the pedal holds it */
    else
        midi_release_at((uint32_t)i);
}

static void midi_pedal_up(uint32_t ch)
{
    uint32_t i = 0;
    midi_chan(ch)->pedal = 0;
    while (i < midi_nheld)
        if (midi_held[i].ch == (ch | MH_PEDAL))
            midi_release_at(i);
        else
            i++;
}

static void __attribute__((noinline)) midi_forget_track(uint32_t track)
{
    uint32_t i = 0, k = 0;
    for (; i < midi_nheld; i++)
        if (midi_held[i].trk != track)
            midi_held[k++] = midi_held[i];
    midi_nheld = k;
}

/* a key or a MIDI note still holds a note on the track */
static int midi_track_held(uint32_t track)
{
    uint32_t k;
    for (k = 0; k < midi_nheld; k++)
        if (midi_held[k].trk == track)
            return 1;
    for (k = 0; k < 27u; k++)
        if (kb_kind[k] == KS_NOTE && kb_trk[k] == track)
            return 1;
    return 0;
}

static void midi_arp_clear(track_t *t)
{
    t->nheld = t->arp_phys = t->arp_note = 0;
}

/* CC120 All Sound Off: the track silent now, whatever the pedal and RELEASE */
static void midi_silence_track(uint32_t track)
{
    track_t *t = &trk[track];
    uint32_t i = 0;
    while (i < midi_nheld)                         /* its MIDI notes end (recording, free take) */
        if (midi_held[i].trk == track)
            midi_release_at(i);
        else
            i++;
    trk_all_off(t);
    midi_arp_clear(t);
    t->seq_n = t->seq_hold = t->slide_glide = 0;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            voice_kill(&t->v[i]);                  /* a fade over KILL_BLOCKS, no click */
    if (is_drum(t))
        drums_off();
    sl[track].rec = sl[track].loop = 0;           /* the SLICER does not replay what it caught */
}

static void midi_control(uint32_t ch, uint32_t cc, uint32_t v)
{
    midi_chan_t *c = midi_chan(ch);
    uint32_t i, mask;
    switch (cc) {
    case 1:  c->wheel = (uint8_t)v;  break;
    case 2:  c->breath = (uint8_t)v; break;
    case 4:  c->foot = (uint8_t)v;   break;
    case 65: c->porta = v >= 64u;    break;
    case 64:
        if (v >= 64u)
            c->pedal = 1;
        else
            midi_pedal_up(ch);
        return;
    case 120:                                      /* All Sound Off: the pedal does not hold */
        mask = midi_targets(ch);
        for (i = 0; i < NTRK; i++)
            if (mask & (1u << i))
                midi_silence_track(i);
        return;
    case 123:                                      /* All Notes Off: releases, the pedal holds */
        mask = midi_targets(ch);
        i = 0;
        while (i < midi_nheld)
            if ((midi_held[i].ch & 15u) != ch)
                i++;
            else if (c->pedal)
                midi_held[i++].ch |= MH_PEDAL;     /* as a note-off: the pedal holds it */
            else
                midi_release_at(i);
        for (i = 0; i < NTRK; i++)
            if ((mask & (1u << i)) && !midi_track_held(i)) {
                trk_all_off(&trk[i]);              /* (a latched arp chord too) */
                midi_arp_clear(&trk[i]);
            }
        return;
    case 121:                                      /* Reset All Controllers (the bend range stays) */
        c->bend = 0;
        c->wheel = c->press = c->porta = 0;
        c->rpn_msb = c->rpn_lsb = 127;
        midi_pedal_up(ch);
        break;
    case 101: c->rpn_msb = (uint8_t)v; return;
    case 100: c->rpn_lsb = (uint8_t)v; return;
    case 99: case 98:                              /* an NRPN: data entry is not for RPN 0 */
        c->rpn_msb = c->rpn_lsb = 127;
        return;
    case 6: case 38:
        if (c->rpn_msb || c->rpn_lsb)
            return;
        if (cc == 6u)                              /* RPN 0: +-0..24 semitones, 0..99 cents */
            c->semis = (uint8_t)(v > 24u ? 24u : v);
        else
            c->cents = (uint8_t)(v > 99u ? 99u : v);
        break;
    default:
        MIDI_CC_HOOK(ch, cc, v);
        return;
    }
    midi_expr_channel(ch);
}

/* the occasional controller / panic: out of the render path's inlining */
static void __attribute__((noinline)) midi_event(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    if (st == 0x90u && d2)
        midi_note_on(ch, d1, d2);
    else if (st == 0x80u || st == 0x90u)
        midi_note_off(ch, d1);
    else if (st == 0xB0u)
        midi_control(ch, d1, d2);
    else if (st == 0xE0u || st == 0xD0u) {
        if (st == 0xE0u)
            midi_chan(ch)->bend = (int16_t)((int32_t)(d1 | d2 << 7) - 8192);
        else
            midi_chan(ch)->press = (uint8_t)d1;    /* channel aftertouch */
        midi_expr_channel(ch);
    }
}
