/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared helper for the host UI tests (ui_pages_test.c family): open the SYSTEM menu the way the SLOOP UI does now,
 * HOME tapped twice (a second press within 300 ms of the first release; ui_input.c home_gesture). It replaces the
 * 0.7 s HOME hold the tests used before. Needs press / release / tap / frames of the including test. The first tap acts
 * as ever (go home; on TRACKS the scope opens), so the screen under the menu is reset here. After it: ui.menu == 1
 * and HOME is up. Tests that only need the menu's state may still set ui.menu directly. */
#ifndef TEST_MENU_OPEN_H
#define TEST_MENU_OPEN_H
static void test_open_menu(void)
{
    tap(B_HOME);                                      /* the first tap: acts */
    press(B_HOME);                                    /* the second press, 32 ms after the release: the menu */
    release(B_HOME);
    frames(2);
#if FELUCCA_SCOPE
    scope_on = 0;                                     /* (the first tap's scope: not wanted under the menu) */
#endif
#if FELUCCA_VIS
    vis_on = 0;
#endif
}
#endif
