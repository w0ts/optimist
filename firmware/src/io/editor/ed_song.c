/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v13: the song chain (seq/arranger.h; the Optimist UI's SONG screen, ui/optimist/op_song.c, MODE and PART rows;
 * included by editor.c; web/EDITOR_PROTOCOL.md "v13: the song chain"; builds with the arranger: FELUCCA_ARRANGER). The scenes and the
 * patterns are SCENE / PAT_* (81, 79..84); this is the order they play in: PART 1..64 (a scene, a number of bars), LOOP or SONG mode.
 * The rules are the device's: edited stopped only ("STOP FIRST"), INSERT copies the part after it, DELETE leaves one, SAVE puts the chain
 * into flash (arrangement_save; the editor asks for it, the device does it when its YES says so).
 *   97 SONGC  op [args] -> 0, then the chain
 *       op 0  get        -> 0, rc, mode (1 SONG, 0 LOOP), playing, scenes (ARR_SCENES), steps (ARR_STEPS), count, count x (scene, bars)
 *       op 1  part, scene, bars   the part set (scene 0.., bars 1..64)
 *       op 2  part       INSERT: a copy after it (the chain full: rc 2)
 *       op 3  part       DELETE (one part left: rc 3)
 *       op 4  mode       LOOP (0) or SONG (1)
 *       op 5  -          SAVE: the chain into flash
 *       every op answers op 0's reply; rc 0 done, 1 stop first (the transport plays), 2 full, 3 one part left, 4 bad argument
 * INFO tag 0x5A: version 1. A bad op: no reply. */
#include "ed_reply.h"         /* (ed_b2..ed_b4, ed_b32) */
enum { ED_SONGC = 97 };
#if FELUCCA_ARRANGER && FELUCCA_ED_SONG
static void ed_songc_out(uint32_t rc)
{
    uint32_t i;
    ed_b4(0, rc, arrangement_enabled != 0u, song.playing != 0u); ed_b3(ARR_SCENES, ARR_STEPS, arrangement.count);
    for (i = 0; i < arrangement.count && i < ARR_STEPS; i++) {
        ed_b2(arrangement.entry[i].scene, arrangement.entry[i].bars);
    }
}
static uint32_t ed_songc_part(uint32_t a, uint32_t ins)
{
    uint32_t i, n = arrangement.count;
    if (ins) {
        if (n >= ARR_STEPS)
            return 2u;
        for (i = n; i > a + 1u; i--)
            arrangement.entry[i] = arrangement.entry[i - 1u];
        arrangement.entry[a + 1u] = arrangement.entry[a];
        arrangement.count = (uint8_t)(n + 1u);
        return 0u;
    }
    if (n <= 1u)
        return 3u;
    for (i = a; i + 1u < n; i++)
        arrangement.entry[i] = arrangement.entry[i + 1u];
    arrangement.count = (uint8_t)(n - 1u);
    return 0u;
}
static int ed_songc(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, rc = 0;
    if (cmd != ED_SONGC || op > 5u)
        return 0;
    if (op == 1u || op == 2u || op == 3u) {
        if (na < (op == 1u ? 4u : 2u))
            return 0;
        if (song.playing)
            rc = 1u;
        else if (a[1] >= arrangement.count)
            rc = 4u;
        else if (op == 1u) {
            if (a[2] >= ARR_SCENES || a[3] < 1u || a[3] > 64u)
                rc = 4u;
            else
                arrangement.entry[a[1]].scene = a[2], arrangement.entry[a[1]].bars = a[3];
        } else
            rc = ed_songc_part(a[1], op == 2u);
    } else if (op == 4u) {
        if (na < 2u || a[1] > 1u)
            return 0;
        if (srec)
            rc = 1u;                                        /* (SONG REC is on: the device says "SONG REC IS ON") */
        else
            arrangement_enabled = (uint8_t)a[1];
    } else if (op == 5u) {
        arrangement_save();
    }
    ui.force = 1;
    ed_songc_out(rc);
    return 1;
}
#define ED_SONGC_FN ed_songc,               /* (editor.c: its table of commands) */
static void ed_songc_info(void)
{
    ed_b(0x5A);
    ed_b2(1, 1);
}
#else
static int ed_songc(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
#define ED_SONGC_FN
static void ed_songc_info(void) {}
#endif
