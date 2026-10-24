/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: a synth track's clip read and written note by note (command 111; builder item ED_MIRROR, live editing or
 * not; included by editor.c and ed_lite.c after ed_steps.c; web/EDITOR_PROTOCOL.md "The clip"). The app's piano roll (the
 * user, 2026-10-24: "editing of the clip, by touch. Add note, remove note, move note. Stretch note ... by pulling the data
 * from the clip"; "I would do it for synths only"). The steps go as STEP_GET / TRACK_STEP give them, all 15 bytes (ed_steps.c):
 * a note longer than its step is a TIE or a carry step after it (seq.c note_span), the client writes them as the recorder does.
 *   111 CLIP  0                        -> 0, 0, selected track, NTRK, per track: LEN, kind (0 a synth, 1 the drums)
 *             1, track, first, count   -> 1, rc, track, first, count, count steps (15 bytes each; count 16 at most)
 *             2, track, index, step    -> 2, rc, track, index, the step as written (15 bytes)
 * rc: 0 done, 1 arguments (the drum track, an index past NSTEP, a step of fewer than 15 bytes). A write is an undo level of the
 * device's own history, as a step key's; writes to one track within 800 ms are one (a drag is one undo). Not echoed: the
 * client reads again when it wants the device's own edits. INFO tag 0x67 lists opti_clip_1 (0x66 is full). */
enum { ED_CLIP = 111, CLIP_PAGE = 16 };
_Static_assert(6u + CLIP_PAGE * 15u <= ED_PAYLOAD_N, "CLIP 1 (a page of steps) fits one reply");

static void clip_mark(const track_t *t)               /* an undo level, as ed_undo.c ed_undo_edit (before the IRQ goes off) */
{
    static uint32_t last_ms, sess;
    static uint8_t last_trk = 0xFF;
    uint32_t k = trk_index(t) % NTRK;
    if (last_trk != k || fm1_ms - last_ms > 800u || !sess)
        sess = (undo_sess += 4u) | 3u;
    last_trk = (uint8_t)k;
    last_ms = fm1_ms;
    undo_mark(t, sess);
}

static int ed_clip(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, i, ok;
    if (cmd != ED_CLIP || op > 2u)
        return 0;
    if (op == 0u) {
        ed_b4(0, 0, song.sel, NTRK);
        for (i = 0; i < NTRK; i++)
            ed_b2(trk_len(&trk[i]), is_drum(&trk[i]));
        return 1;
    }
    ok = na >= 4u && a[1] < NTRK && !is_drum(&trk[a[1]]) && a[2] < NSTEP;
    if (op == 1u) {
        uint32_t n = ok ? a[3] : 0u;
        n = n > CLIP_PAGE ? CLIP_PAGE : n;
        n = a[2] + n > NSTEP ? NSTEP - a[2] : n;
        ed_b4(1, !ok, na > 1u ? a[1] : 0u, na > 2u ? a[2] : 0u);
        ed_b(n);
        for (i = 0; i < n; i++)
            ed_step_out(&trk[a[1]], a[2] + i, 1);
        return 1;
    }
    ok = ok && na >= 3u + 15u;
    if (ok) {
        track_t *t = &trk[a[1]];
        clip_mark(t);
        fm1_irq_off();
        ed_step_in(t, a[2], a + 3, 15);
        fm1_irq_on();
        ui.force = 1;
    }
    ed_b4(2, !ok, na > 1u ? a[1] : 0u, na > 2u ? a[2] : 0u);
    if (ok)
        ed_step_out(&trk[a[1]], a[2], 1);
    return 1;
}
#define ED_CLIP_FN ed_clip,                   /* (editor.c, ed_lite.c: their tables of commands) */
