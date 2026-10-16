/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v14: notes played from the editor (included by editor.c; web/EDITOR_PROTOCOL.md "v14: playing notes"). INFO tag 0x60 = here.
 * The requests run in the main loop; the sound is made in the audio interrupt from a ring of its own (seq/ednote.c: one writer, one reader, no
 * interrupt masked). This file keeps the policy: at most 8 notes held, each with its own end (dur), and the rule that no note hangs: the host quiet
 * for 3 s, USB unconfigured or reset, a track's note-off lost to a full ring (retried) all end the notes.
 *   103 NOTE  op [track, note, vel [, dur]] -> op, rc, track, note, held
 *       op 0 ON   track, note, vel 1..127 [, dur 1..127: ends by itself after dur x 100 ms]   (the drum track: note = the lane 0..15, 16 the ACCENT lane)
 *       op 1 OFF  track, note
 *       op 2 ALL  track (127: every track)
 *       rc 0 done, 1 refused: the ring was full (nothing happened). A bad op, track, note or vel: no reply. INFO tag 0x60: version 1, quiet seconds (3). */
enum { ED_NOTE = 103, EDNOTE_QUIET_MS = 3000 };
#if EDN_ON
typedef struct {
    uint8_t trk, note, used, pend;                      /* pend: its off is still to be queued (the ring was full) */
    uint32_t end_ms;                                    /* 0: held until an OFF or the quiet rule */
    uint32_t age;                                       /* the order the notes were played in */
} ed_hn_t;
static uint32_t ed_hn_age;
static ed_hn_t ed_hn[EDN_HELD];
static uint32_t ed_hn_resets;
static uint32_t ed_hn_n(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < EDN_HELD; i++)
        n += ed_hn[i].used && !ed_hn[i].pend;
    return n;
}
static int ed_hn_off(ed_hn_t *h)                        /* queue its off; 1 = queued (the slot is free) */
{
    if (!h->used)
        return 1;
    if (!ednote_push(EDN_OP_OFF, h->trk, h->note, 0u)) {
        h->pend = 1;
        return 0;
    }
    h->used = h->pend = 0;
    return 1;
}
/* every service pass (also while the editor is not watching): the notes due end, the quiet host ends them all */
static void ed_note_tick(void)
{
    uint32_t i, now = fm1_ms, any = 0, quiet = !usb.config || usb.resets != ed_hn_resets || now - ed_w.last_ms > EDNOTE_QUIET_MS;
    for (i = 0; i < EDN_HELD; i++) {
        ed_hn_t *h = &ed_hn[i];
        if (!h->used)
            continue;
        any = 1;
        if (h->pend || quiet || (h->end_ms && (int32_t)(now - h->end_ms) >= 0))
            (void)ed_hn_off(h);
    }
    (void)any;
}
static int ed_note(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, track, note = 0, rc = 0, i, free_i = EDN_HELD, old = EDN_HELD;
    if (cmd != ED_NOTE || op > 2u || na < 2u)
        return 0;
    track = a[1];
    if (op == 2u) {
        if (track != 127u && track >= NTRK)
            return 0;
        for (i = 0; i < EDN_HELD; i++)
            if (ed_hn[i].used && (track == 127u || ed_hn[i].trk == track))
                rc |= !ed_hn_off(&ed_hn[i]);
        note = 127u;
    } else {
        if (na < (op == 0u ? 4u : 3u) || track >= NTRK)
            return 0;
        note = a[2];
        if (op == 0u && (a[3] == 0u || (is_drum(&trk[track]) && !(note < DRUM_LANES || (FELUCCA_DRUM_ACCENT && note == DRUM_ACC)))))
            return 0;
        if (op == 1u) {
            for (i = 0; i < EDN_HELD; i++)
                if (ed_hn[i].used && ed_hn[i].trk == track && ed_hn[i].note == note)
                    rc |= !ed_hn_off(&ed_hn[i]);
        } else if (is_drum(&trk[track])) {              /* a one-shot: nothing is held */
            rc = !ednote_push(EDN_OP_ON, track, note, a[3]);
        } else {
            for (i = 0; i < EDN_HELD; i++) {
                if (!ed_hn[i].used)
                    free_i = i;
                else if (ed_hn[i].trk == track && ed_hn[i].note == note)
                    old = i;
            }
            if (old == EDN_HELD && free_i == EDN_HELD) {   /* eight held: the first one goes (the oldest queued) */
                uint32_t best = 0;
                for (i = 1; i < EDN_HELD; i++)
                    if ((int32_t)(ed_hn[i].age - ed_hn[best].age) < 0)
                        best = i;
                if (!ed_hn_off(&ed_hn[best]))
                    rc = 1;
                else
                    free_i = best;
            }
            i = old != EDN_HELD ? old : free_i;
            if (!rc && i < EDN_HELD && ednote_push(EDN_OP_ON, track, note, a[3])) {
                ed_hn[i].used = 1, ed_hn[i].pend = 0, ed_hn[i].age = ++ed_hn_age;
                ed_hn[i].trk = (uint8_t)track, ed_hn[i].note = (uint8_t)note;
                ed_hn[i].end_ms = na >= 5u && a[4] ? (fm1_ms + (uint32_t)a[4] * 100u) | 1u : 0u;
                ed_hn_resets = usb.resets;
            } else {
                rc = 1;
            }
        }
    }
    ed_b(op), ed_b(rc != 0), ed_b(track), ed_b(note), ed_b(ed_hn_n());
    return 1;
}
static void ed_note_info(void)
{
    ed_b(0x60);
    ed_b(2);
    ed_b(1);
    ed_b(EDNOTE_QUIET_MS / 1000u);
}
#else
static void ed_note_tick(void) {}
static int ed_note(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
static void ed_note_info(void) {}
#endif
