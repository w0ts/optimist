/* MIRROR_CHECK: an in-place repaint (op_state.c op_repaint) draws every row of the screen, on every screen and with every METERS
 * mode, the help line's band too (a build with PARAM_HELP): a mirror client that missed a page's clear (a frame lost over
 * Bluetooth, dropped by the lag rule) gets every row again from a catch-up in place or a repair, and the self-check (SUMS,
 * io/editor/ed_mirror_sum.h) checks every band: one not drawn whole is never compared, a wrong pixel there stays for good.
 * 2026-11-01, the user over Bluetooth: SYSTEM > INFO's last line ("GUARD OFF", in the help band's rows: INFO's first page has
 * none) stayed at the foot of HOME on the mirror; HOME's repaint drew neither the help band (no line shown) nor the load
 * lines' strip, so bands 27..29 were never checked */
#ifdef MIRROR_CHECK
#include <stdarg.h>
static void mr_check(int ok, const char *f, ...)       /* (check with the numbers in its line) */
{
    char b[160];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    check(ok, b);
}
static uint32_t mr_undrawn(uint32_t *first)            /* rows the repaint left as they were (a sentinel over the panel first) */
{
    uint32_t i, y, x, n = 0;
    for (i = 0; i < 240u * 240u; i++)
        screen[i] = 0xDEADu;
    op_repaint();
    frame();
    *first = 240;
    for (y = 0; y < 240u; y++)
        for (x = 0; x < 240u; x++)
            if (screen[y * 240u + x] == 0xDEADu) {
                *first = *first < y ? *first : y;
                n++;
                break;
            }
    ui.force = 1;                                       /* (the panel whole again for what follows) */
    frames(2);
    return n;
}
static uint32_t mr_valid(void)                          /* the bands a self-check on this screen can compare */
{
    uint32_t i, valid = 0;
    for (i = 0; i < 2u; i++) {                          /* (the first proves nothing: the last check's rectangles) */
        msum_begin();
        op_repaint();
        frame();
        msum_end(&valid);
    }
    return valid;
}
static void mirror_rows_tests(void)
{
    static const uint8_t SC[] = {SCR_HOME, SCR_SOUND, SCR_FX, SCR_PROJECT, SCR_SYSTEM, SCR_STEP, SCR_SONG, SCR_TEMPO, SCR_GLOBAL};
    uint32_t m, s, n, first, bad = 0, all = (1u << MSUM_BANDS) - 1u, keep = meters_mode;
    int bt = mc_bt;
    mc_bt = 1;                                          /* (Bluetooth: no meters in the mixer, as the user's client) */
    for (m = 0; m < MTR_N; m++) {
        meters_mode = (uint8_t)m;
        for (s = 0; s < sizeof SC; s++) {
            if (SC[s] == SCR_SYSTEM)
                line_named(SCR_SYSTEM, "INFO");         /* (INFO's first page: its list runs to the foot, over the help band) */
            else
                op_enter(SC[s]);
            ui.force = 1;
            frames(4);
            n = mr_undrawn(&first);
            bad += n != 0u;
            mr_check(!n, "the mirror: METERS %u, %s's repaint draws every row (%u not, the first %u)", m, SCR_NAME[SC[s]], n, first);
            if (SC[s] == SCR_HOME) {
                uint32_t v = mr_valid();
                mr_check(v == all, "the mirror: METERS %u, HOME: every band checkable by SUMS (%08x)", m, v);
            }
        }
        line_named(SCR_SYSTEM, "INFO");                 /* SYSTEM > INFO, then HOME: the band INFO drew over is HOME's again */
        ui.force = 1;
        frames(4);
        tap(B_HOME);
        frames(4);
        if (ui.scr != SCR_HOME)
            go_home(), frames(4);
        n = mr_undrawn(&first);
        mr_check(!n && ui.scr == SCR_HOME, "the mirror: METERS %u, HOME after SYSTEM > INFO: its repaint draws every row (%u not, the first %u)",
              m, n, first);
    }
    meters_mode = (uint8_t)keep;
    mc_bt = bt;
    go_home();
    ui.force = 1;
    frames(3);
    printf("  mirror: every screen's repaint draws every row (%u screens x %u METERS modes, %u not)\n", (unsigned)sizeof SC, MTR_N, bad);
}
#endif
