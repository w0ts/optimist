/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v14: the REC key and its state (included by editor.c; web/EDITOR_PROTOCOL.md "v14: REC"). INFO tag 0x63 = here.
 * The device has one global REC, no per-track arm: what it records is the selected track. The state machine is seq.c's (rec_wait, ci_on,
 * ft_on, song.rec); TOGGLE is the plain press of the Optimist UI's op_rec / SLOOP's rec_toggle, from the main loop as they run.
 *   107 REC  op -> op, rc, state, track
 *       op 0 GET, 1 TOGGLE; state bit 0 recording, 1 waiting for the start (also while counting in), 2 count-in, 3 free take;
 *       track: the track that records / will record / takes the take, 127 when state is 0;
 *       rc 0 done, 1 refused (a song plays), 2 nothing done (a free take ended within 0.3 s). A bad op: no reply.
 *       INFO tag 0x63: version 1, flags bit 0 = the count-in exists (FELUCCA_REC_MODES). */
#include "ed_reply.h"         /* (ed_b2..ed_b4, ed_b32) */
enum { ED_REC = 107 };
#if FELUCCA_ED_REC
static uint32_t ed_rec_bits(void)
{
    uint32_t s = (song.rec != 0u) | (uint32_t)(rec_wait != 0u) << 1 | (uint32_t)(ft_on != 0u) << 3;
#if FELUCCA_REC_MODES
    s |= (uint32_t)(ci_on != 0u && rec_wait != 0u) << 2;   /* (REC cancelled it: seq.c ends it at the next block) */
#endif
    return s;
}
static uint32_t ed_rec_track(uint32_t st)
{
    uint32_t i;
    if (!st)
        return 127u;
    if (st & 8u)
        return ft_trk % NTRK;
    if (song.rec)
        for (i = 0; i < NTRK; i++)
            if (song.rec >> i & 1u)
                return i;
    return song.sel % NTRK;
}
static uint32_t ed_rec_toggle(void)                    /* the REC key's plain press: rc */
{
    if (ft_owns_press()) {
        if (!ft_on)
            return 2u;                                  /* (a take just ended: the key is ignored as well) */
        ft_req = 1;                                     /* (closed by the audio at its next block) */
        return 0u;
    }
    if (song.playing && arrangement_enabled)
        return 1u;
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;
        return 0u;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();
    else
        rec_wait = 1;
    return 0u;
}
static int ed_rec(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t rc = 0, st;
    if (cmd != ED_REC || !na || a[0] > 1u)
        return 0;
    if (a[0]) {
        rc = ed_rec_toggle();
        ui.force = 1;
    }
    st = ed_rec_bits();
    ed_b4(a[0], rc, st, ed_rec_track(st));
    return 1;
}
#define ED_REC_FN ed_rec,                   /* (editor.c: its table of commands) */
static void ed_rec_info(void)
{
    ed_b4(0x63, 2, 1, FELUCCA_REC_MODES ? 1u : 0u);
}
#else
static int ed_rec(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
#define ED_REC_FN
static void ed_rec_info(void) {}
#endif
