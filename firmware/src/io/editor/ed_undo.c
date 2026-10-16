/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v14: undo and redo (included by editor.c before ed_steps.c; web/EDITOR_PROTOCOL.md "v14: undo"). INFO tag 0x5F = this command is here.
 * The device's own history (seq/undo.c: the steps, the pattern's LEN and DIV, the automation store's events of a track) undone and redone by the
 * function the panel calls (undo_apply, main loop only: it takes the audio interrupt off itself, or is told it is inside it by undo_isr; the editor's
 * requests run in ed_service, never in an interrupt). The editor's own step writes are levels of that history too (ed_undo_edit marks the track first).
 *   102 UNDO  op -> op, rc, done lo, done hi, all lo, all hi, track, can_undo, can_redo
 *       op 0 the state, 1 undo, 2 redo; rc 0 done, 1 nothing to undo / redo (nothing moved); done / all: the levels applied / held, 2 x 7 bits;
 *       track: the last undo or redo's track (0..3). A bad op: no reply. INFO tag 0x5F: version 1, flags (bit 0: a history of many levels). */
enum { ED_UNDO = 102 };
#if FELUCCA_ED_UNDO
/* a write of the editor to track t's pattern is about to happen: mark it as a panel step key does. Writes to the same track within 800 ms are one
 * level (a drag is one undo). Call it BEFORE the interrupt is taken off (undo_mark takes it off itself). */
static void ed_undo_edit(const track_t *t)
{
    static uint32_t last_ms, sess;
    static uint8_t last_trk = 0xFF;
    uint32_t k = trk_index(t) % NTRK, now = fm1_ms;
    if (last_trk != k || now - last_ms > 800u || !sess)
        sess = (undo_sess += 4u) | 3u;
    last_trk = (uint8_t)k;
    last_ms = now;
    undo_mark(t, sess);
}
static int ed_undo(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, rc = 0, n, m, tk;
    if (cmd != ED_UNDO || op > 2u)
        return 0;
    if (op) {
        if (undo_apply(op == 2u)) {
            sync_reload = 1;                                /* (as the panel's undo: the editor re-reads) */
            ui.force = 1;
        } else {
            rc = 1;
        }
    }
    undo_status(&n, &m, &tk);
    ed_b(op), ed_b(rc);
    ed_b(n & 127u), ed_b((n >> 7) & 127u);
    ed_b(m & 127u), ed_b((m >> 7) & 127u);
    ed_b(tk & 3u);
    ed_b(undo_can(0) != 0), ed_b(undo_can(1) != 0);
    return 1;
}
static void ed_undo_info(void)
{
    ed_b(0x5F);
    ed_b(2);
    ed_b(1);
    ed_b(FELUCCA_UNDO_HISTORY ? 1u : 0u);
}
#else
static void ed_undo_edit(const track_t *t) { (void)t; }
static int ed_undo(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
static void ed_undo_info(void) {}
#endif
