/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.0.1: the keys of the notes
 * the sequencer and ARP play, #38, hugelton/Felucca 20c275e, ui_input.c play_leds)
 * Copyright (C) 2026 renebohne (github.com/renebohne/sloop-fm1 e2e5099: the sounding voices of a synth track light
 * their keys) */
/* Played notes on the keys (FELUCCA_KEYLIT, backports.h): on a synth track, outside a layer, the keys of the
 * notes the selected track sounds now light up: its sequencer's step notes and its ARP note (Felucca 1.0.1),
 * and its voices still held (renebohne: MIDI in, a latched chord). Included by ui_input.c (keys_lit).
 * A note lights the lowest key that plays it (kb_map: the octave, TRANSPOSE, the key layout; SNAP rounds
 * several keys onto one note: only the lowest); a note no key plays is not shown. A snapshot of what the
 * audio ISR keeps (seq_notes, arp_note, the voices): no state of its own. */
static uint32_t keylit_play(const track_t *t)
{
    uint8_t s[4 + 1 + NVOICE];
    uint32_t n = 0, i, j, k, note, used = 0, m = 0;
    for (i = 0; i < t->seq_n && i < 4u; i++)
        s[n++] = t->seq_notes[i];
    if (t->arp_note)
        s[n++] = t->arp_note;
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &t->v[i];
        if (!v->active || !v->gate || v->stage > 2u)
            continue;
        for (j = 0; j < n && s[j] != v->note; j++)
            ;
        if (j == n)
            s[n++] = v->note;
    }
    for (k = 0; n && k < 27u; k++) {
        note = kb_map(t, k);
        for (i = 0; i < n; i++)
            if (s[i] == note && !((used >> i) & 1u)) {
                used |= 1u << i;
                m |= 1u << k;
            }
    }
#if FELUCCA_LIGHTS
    if (t == TSEL)                                 /* + the notes just started, a few frames (ui_studio.c pads_tick) */
        for (k = 0; k < 27u; k++)
            if (key_lit[k])
                m |= 1u << k;
#endif
    return m;
}
