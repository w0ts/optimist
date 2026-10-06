/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP 2.3 / X0X 0.10.1 backports' UI side (firmware/src/backports23.h), included by ui_pages_test.c after
 * backports_ui.c: each block only with its switch on (tests/run_tests.sh: ui_pages_bp23_test).
 *   panel    a calibration table read back with two labels on one button / knob: the default (FELUCCA_ST_STRICT) */
/* the learned panel table, as read back from flash: ranges were checked, now also that it is a permutation */
static void bp23_panel(void)
{
    panel = PANEL_DEFAULT;
    panel.btn[B_HOME] = panel.btn[B_PLAY];           /* HOME on PLAY's button: HOME could never be reached */
    panel_init();
    check(!memcmp(&panel, &PANEL_DEFAULT, sizeof panel), "panel: two labels on one button -> the default table");
    panel = PANEL_DEFAULT;
    panel.enc[EN_K1] = panel.enc[EN_K2];
    panel_init();
    check(!memcmp(&panel, &PANEL_DEFAULT, sizeof panel), "panel: two roles on one knob -> the default table");
    panel = PANEL_DEFAULT;
    panel.btn[B_FX] = PANEL_DEFAULT.btn[B_SCL], panel.btn[B_SCL] = PANEL_DEFAULT.btn[B_FX];   /* a real swap */
    panel_init();
    check(panel.btn[B_FX] == PANEL_DEFAULT.btn[B_SCL], "panel: a learned permutation is kept");
    panel = PANEL_DEFAULT;
}

static void bp23_ui_tests(void)
{
#if FELUCCA_ST_STRICT
    bp23_panel();
#endif
}
