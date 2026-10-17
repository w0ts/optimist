/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v13: the Optimist UI's GLOBAL and SYSTEM screens as lists of lines (ui/optimist/op_global.c, op_project.c;
 * included by editor.c; web/EDITOR_PROTOCOL.md "v13: GLOBAL and SYSTEM"; builds with the Optimist UI only: FELUCCA_UI == 1).
 * The settings of the FM-1 (BPM, SWING, the REC modes, MIDI, the channels, screen, lights, audio, USB, Bluetooth, USB AUDIO's
 * four channels, calibrate, about, the info read-outs) are not parameters with a descriptor each: the device builds each line from a
 * cell (op_cells.c cell_t) and turns it with its own rules. This command hands those cells out as the device draws them
 * (label, value text, unit, kind, its pages and sections as the screen groups them) and turns / does them through the
 * screen's own functions, so an editor shows the same lines, in the same order, with the same words, and cannot drift.
 *   96 SCRN  op screen line [...] -> op screen line [...]
 *       screen 0 GLOBAL, 1 SYSTEM
 *       op 0  screen                     -> 0, screen, lines n, pages np
 *       op 1  screen, line               -> 1, screen, line, then the LINE below
 *       op 2  screen, line, mode, v14    turn: mode 0 steps (accelerated as a knob), 1 fine steps (a unit each), 2 the default
 *                                        -> 2, screen, line, LINE
 *       op 3  screen, line, ok           YES: ok 0 may ask (the LINE's `ask` is 1: a question, the device shows it), ok 1 does it
 *                                        -> 3, screen, line, rc (1 done / asked, 0 nothing), LINE
 *       op 4  -                          the settings into flash (what SYSTEM does when it is left; once stopped) -> 4
 *       LINE: page, page title, section, kind (CK_*: 0 none, 1 value, 2 read-out, 3 action, 4 enter), label, value text, unit,
 *             state (0 plain, 1 warn, 2 error, 3 ok, 4 other colour), danger, ask, gauge form (GK_*), gmin v14, gmax v14, gv v14,
 *             by-descriptor 0/1 and then fmt, min v14, max v14, def v14, then the action's object text
 * A bad screen or line, or the BLE devices list open on the device: no reply. */
#include "ed_reply.h"         /* (ed_b2..ed_b4, ed_b32) */
#include "ed_desc.h"          /* (DESC's body) */
enum { ED_SCRN = 96 };
#if FELUCCA_UI == 1 && FELUCCA_ED_SCRN
#include "ed_cell.h"          /* (a card's bytes, SCRS's too) */
static int scrn_ok(uint32_t s) { return s < 2u && !(s == 1u && GL_DEV); }
static void scrn_line(uint32_t s, uint32_t i)
{
    cell_t c;
    char t[24], sec[14];
    gl_cell(s, i, &c);
    t[0] = sec[0] = 0;
    gl_ptitle(s, i, t, sizeof t);
    gl_sect(s, i, sec);
    ed_b(gl_pg(s, i));
    ed_str(t, 20);
    ed_str(sec, 12);
    ed_cell_text(&c);                                  /* (ed_cell.h) */
    ed_b4(ed_cell_col(&c), c.danger, op_armed() && ui.arm_scr == (s ? SCR_SYSTEM : SCR_GLOBAL) && ui.arm_row == i, c.gk);
    ed_cell_bar(&c);
    ed_str(c.obj, 14);
}
/* the screen's own state a remote turn must not leave behind: a question the remote asked is the device's to show, it stays */
static int ed_scrn(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, s, i;
    if (cmd != ED_SCRN || op > 4u)
        return 0;
    if (op == 4u) {
        sys_leave();
        ed_b(4);
        return 1;
    }
    if (na < 2u || !scrn_ok(s = a[1]))
        return 0;
    if (op == 0u) {
        ed_b4(0, s, gl_n(s), gl_npg(s));
        return 1;
    }
    if (na < 3u || (i = a[2]) >= gl_n(s))
        return 0;
    if (op == 1u) {
        ed_b3(1, s, i);
        scrn_line(s, i);
        return 1;
    }
    if (op == 2u) {
        int32_t st;
        uint32_t mode;
        if (na < 6u)
            return 0;
        mode = a[3];
        st = mode == 2u ? OP_RESET : ed_rv(a + 4);
        gl_turn(s, i, st, mode != 0u);
        ui.force = 1;
        ed_b3(2, s, i);
        scrn_line(s, i);
        return 1;
    }
    {                                                   /* 3 YES */
        int r;
        if (na < 4u)
            return 0;
        r = gl_yes(s, i, a[3] != 0u);
        ui.force = 1;
        ed_b4(3, s, i, r != 0);
        scrn_line(s, i);
        return 1;
    }
}
#define ED_SCRN_FN ed_scrn,                 /* (editor.c: its table of commands) */
static void ed_scrn_info(void)
{
    ed_b(0x59);
    ed_b3(2, 1, 2);
}
#else
static int ed_scrn(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
#define ED_SCRN_FN
static void ed_scrn_info(void) {}
#endif
