/* SPDX-License-Identifier: GPL-3.0-only */
/* Notes played from the web editor (editor protocol v14, command 103 NOTE; web/EDITOR_PROTOCOL.md "v14: playing notes"). seq.c includes this
 * after its note input (input_on / input_off, midi_map) and before midi_control.c.
 *
 * The editor's requests run in the main loop (io/editor/ed_note.c); the sound is made in the audio interrupt (events_block). Between them a ring of
 * its own: ONE writer (the main loop, ednote_push) and ONE reader (events_block, ednote_block), so no interrupt is masked and nothing the USB side
 * or the other note sources use is shared. A slot is stored before the index moves (RING_PUBLISH).
 *   slot  op (bits 0..1: 0 on, 1 off, 2 forget the track) | track << 2 | note << 4 | vel << 11
 * A note goes where a MIDI note goes (midi_note_on): the track's layout (midi_map: SCL > KEYS snap, CHORD, TRANSPOSE), input_on (the arp, the free take,
 * REC, the voice budget in trk_note_on) and MIDI OUT KEYS (midi_key_out), as the panel's keys do. On the drum track the note is the lane. The notes
 * a press started are kept in a table of this file's own (the interrupt's), so the off ends what the on started even after the layout changed. */
#ifdef EDN_HOST                                     /* (the host stress test has no OTA plumbing: it drives the ring itself) */
#define EDN_ON FELUCCA_ED_NOTE
#else
#define EDN_ON (FELUCCA_ED_NOTE && FELUCCA_OTA && FELUCCA_EDITOR)
#endif
#if EDN_ON
#define EDN_Q 16u
#define EDN_HELD 8u                                 /* the editor holds at most this many notes (the voices) */
enum { EDN_OP_ON, EDN_OP_OFF, EDN_OP_FORGET };
static uint32_t edn_q[EDN_Q];
static volatile uint32_t edn_w, edn_r;
typedef struct {
    uint8_t trk, src, n;
    uint8_t nt[4];
} edn_held_t;
static edn_held_t edn_h[EDN_HELD];                  /* the interrupt's table: the press, the notes it started */
static uint32_t edn_nh;

/* main loop: one slot into the ring; 0 = full (nothing queued) */
static int ednote_push(uint32_t op, uint32_t track, uint32_t note, uint32_t vel)
{
    if (edn_w - edn_r >= EDN_Q)
        return 0;
    edn_q[edn_w % EDN_Q] = op | (track & 3u) << 2 | (note & 127u) << 4 | (vel & 127u) << 11;
    RING_PUBLISH();
    edn_w++;
    return 1;
}
static int edn_find(uint32_t track, uint32_t src)
{
    uint32_t i;
    for (i = 0; i < edn_nh; i++)
        if (edn_h[i].trk == track && edn_h[i].src == src)
            return (int)i;
    return -1;
}
static void edn_release_at(uint32_t i)
{
    edn_held_t h = edn_h[i];
    track_t *t = &trk[h.trk % NTRK];
    uint32_t k, mc = trk_midi_ch(h.trk % NTRK);
    for (edn_nh--; i < edn_nh; i++)
        edn_h[i] = edn_h[i + 1u];
    for (k = 0; k < h.n; k++) {
        input_off(t, h.nt[k]);
        midi_key_out(mc, h.nt[k], 0u);
    }
}
/* the track changed its sound (the engine panic): its presses are forgotten without a note off, as midi_forget_track does */
static void edn_forget(uint32_t track)
{
    uint32_t i, k = 0;
    for (i = 0; i < edn_nh; i++)
        if (edn_h[i].trk != track)
            edn_h[k++] = edn_h[i];
    edn_nh = k;
}
static void edn_on(uint32_t track, uint32_t note, uint32_t vel)
{
    track_t *t = &trk[track % NTRK];
    edn_held_t *h;
    uint32_t k, mapped, mc;
    int i = edn_find(track, note);
    if (is_drum(t)) {                               /* a one-shot: the lane's hit, as the lane's key plays it */
        uint32_t lane = note, lvl = vel_lvl(vel);
        if (lane < DRUM_LANES || lane == DRUM_ACC) {
            drum_input(lane, lvl, 0, 1);
            if (lane != DRUM_ACC)
                midi_key_out(trk_midi_ch(TRK_DRUM), LANE_NOTE_OF(lane), lvl_vel(lvl, 100));
        }
        return;
    }
    if (i >= 0)
        edn_release_at((uint32_t)i);                /* a repeated note replaces its press */
    mapped = midi_map(t, note);
    if (mapped == KB_SILENT)
        return;
    if (edn_nh == EDN_HELD)
        edn_release_at(0);
    h = &edn_h[edn_nh++];
    h->trk = (uint8_t)track;
    h->src = (uint8_t)note;
    if (t->p[P_CHORD])
        h->n = (uint8_t)chord_notes(t, mapped, h->nt);
    else {
        h->n = 1;
        h->nt[0] = (uint8_t)mapped;
    }
    mc = trk_midi_ch(track % NTRK);
    for (k = 0; k < h->n; k++) {
        input_on(t, h->nt[k], vel);
        midi_key_out(mc, h->nt[k], vel);
    }
}
/* events_block (the audio interrupt): the editor's notes due */
static void ednote_block(void)
{
    while (edn_r != edn_w) {
        uint32_t e = edn_q[edn_r % EDN_Q], op = e & 3u, track = (e >> 2) & 3u, note = (e >> 4) & 127u, vel = (e >> 11) & 127u;
        edn_r++;
        if (op == EDN_OP_ON)
            edn_on(track, note, vel);
        else if (op == EDN_OP_OFF) {
            int i = edn_find(track, note);
            if (i >= 0)
                edn_release_at((uint32_t)i);
        } else
            edn_forget(track);
    }
}
#else
static void edn_forget(uint32_t track) { (void)track; }
static void ednote_block(void) {}
#endif
