/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the projects themselves (command 110; FELUCCA_PATTERNS, docs/PROJECTS-DESIGN.md; web/EDITOR_PROTOCOL.md
 * "Projects"). With the backup's PJ01..PJ16 streams (ed_backup.c) a client moves one project from a device to another:
 * the simulator's "Get a project" / "Send the project" (web/sim/sim.js; the user, 2026-10-24: "load a project from the
 * fm1, and be able to have the current project of the emulator in project of the fm1 (entering the name also)").
 * Included after ed_backup.c by editor.c and ed_lite.c (the UI's prj_load_go / prj_save_go are in the build by then).
 *   110 PJ_OP  0                 -> 0, 0, rc, open project, used (3 x 7 bit: bit p = project p holds something),
 *                                   then 16 names (ASCII, 0-terminated, 12 at most; "" none)
 *              1, p              -> 1, p, rc: the work saved as project p's state (the PROJECT page's SAVE: another
 *                                   project than the open one is the open one from then on)
 *              2, p              -> 2, p, rc: project p opened, its state into the work (none: a blank work)
 *              3, p              -> 3, p, rc: every record of project p cleared (DELETE; before a stream is written)
 *              4, p, name        -> 4, p, rc: project p's name (up to 12 of " A-Z 0-9 -._/#+", the NAME page's)
 * rc: 0 done, 1 arguments, 3 the transport plays (nothing done), 4 no flash, 7 not written (MEM FULL, flash), 9 a
 * restore holds the log (between BK_BEGIN and BK_COMMIT) */
enum { ED_PJ_OP = 110 };
static int sec_name_set(uint32_t s, const char *nm);   /* storage/sections/sections.c */
static void sec_name(uint32_t s, char *b);

static int edpj_name_ok(uint32_t c)                    /* one of the NAME page's characters (op_name.c NM_SET) */
{
    return c == ' ' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
           c == '/' || c == '#' || c == '+';
}

static uint32_t edpj_op(uint32_t op, uint32_t p, const uint8_t *nm, uint32_t nn)
{
    char b[SEC_NAME_LEN + 1u];
    uint32_t i;
    if (!flash_ok)
        return 4;
    if (song.playing || transport_req)
        return 3;
    if (bk.obj >= 0)
        return 9;
    switch (op) {
    case 1:
        return prj_save_go(p) ? 7u : 0u;               /* (op_edits.c: 1 MEM FULL, -1 flash) */
    case 2:
        prj_load_go(p);
        return 0;
    case 3:
        return project_clear(p) ? 7u : 0u;
    default:                                           /* 4: the name */
        for (i = 0; i < nn && i < SEC_NAME_LEN && nm[i]; i++) {
            if (!edpj_name_ok(nm[i]))
                return 1;
            b[i] = (char)nm[i];
        }
        while (i && b[i - 1u] == ' ')
            i--;                                       /* (the NAME page keeps no trailing space either) */
        b[i] = 0;
        return sec_name_set(p, b) ? 7u : 0u;
    }
}

/* a project command; 0 = not one of them: no reply */
static int ed_proj(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t p, used = 0;
    char b[SEC_NAME_LEN + 1u];
    if (cmd != ED_PJ_OP || na < 1u || a[0] > 4u)
        return 0;
    if (a[0] == 0u) {
        for (p = 0; p < 16u; p++)
            used |= (uint32_t)(project_slot_used(p) != 0) << p;
        ed_b4(0, 0, flash_ok ? 0u : 4u, project_cur());
        ed_b32(used, 3);
        for (p = 0; p < 16u; p++) {
            sec_name(p, b);
            ed_str(b, SEC_NAME_LEN);
        }
        return 1;
    }
    if (na < 2u || a[1] >= 16u)
        return 0;
    ed_b3(a[0], a[1], edpj_op(a[0], a[1], a + 2, na - 2u));
    return 1;
}
#define ED_PROJ_FN ed_proj,                    /* (editor.c, ed_lite.c: their tables of commands) */
