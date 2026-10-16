/* SPDX-License-Identifier: GPL-3.0-only */
/* editor.c: the commands 86..107 (v10..v14) whose op only reads. A write asks for ui.force (the screen cleared to black and drawn
 * again); a read changes nothing, and the editor reads often (UNDO, SOLO, the FX popups, the Device screens): with ui.force for
 * them the panel flashed while the editor was open. The numbers are the protocol's (web/EDITOR_PROTOCOL.md); bit n of a mask:
 * op n reads. Included by editor.c and tests/editor_v14.h. */
static int ed_reads(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, m;
    switch (cmd) {
    case 86u: case 97u: case 98u: case 102u: case 104u: case 107u: m = 1u; break;   /* FX SONG TOOLS UNDO SOLO REC: op 0, the state */
    case 94u: case 95u: m = 0xDu; break;                   /* FXP DLAN: ops 0, 2, 3 (the list, a DESC, the values) */
    case 96u: case 99u: case 100u: m = 3u; break;          /* SCRN SCRS FAV: ops 0, 1 (the screen / list, a line / row / engine's marks) */
    case 105u: m = 7u; break;                              /* STEPLANE: ops 0..2 (LIST, DESC, STEP) */
    case 101u: return 1;                                   /* SCOPE: the trace only */
    default: return 0;                                     /* (103 NOTE: every op plays or stops a note) */
    }
    return op < 8u && ((m >> op) & 1u);
}
