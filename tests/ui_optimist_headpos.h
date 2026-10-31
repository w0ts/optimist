/* The loop position alone (op_draw.c head_pos_only; the user, 2026-10-31: the mirror over Bluetooth "DEAD SLOW"): while
 * playing, bar.beat moving draws its own text again, never the header's 240 x 25 (850 B a beat to the mirror, measured on
 * the FM-1: 2.4 KB/s of the 4.6 KB/s its Bluetooth link carried). The header then is pixel for pixel the one drawn whole,
 * and a message over it still draws it whole */
static uint16_t hp_shot[240 * 25];
static void headpos_tests(void)
{
    uint32_t i, same = 1;
    char b[128];
    reset_ui();
    op_enter(SCR_HOME);
    frame();
    tap(B_PLAY);
    frames(20);
    hp_head_px = hp_head_full = 0;
    for (i = 0; i < 240u; i++) {                        /* (about 4 s: beats at the default tempo) */
        frame();
    }
    snprintf(b, sizeof b, "header: playing 4 s, bar.beat drawn alone (%u px into the header, %u whole-width blits)",
             (unsigned)hp_head_px, (unsigned)hp_head_full);
    check(hp_head_px > 0u && hp_head_full == 0u && hp_head_px < 240u * 25u, b);
    memcpy(hp_shot, screen, sizeof hp_shot);
    ui.force = 1;
    frame();
    for (i = 2 * 240u; i < 23u * 240u; i++)             /* (rows 2..22: the memory line and the rule aside) */
        same &= hp_shot[i] == screen[i];
    check(same, "header: after the position alone, the same pixels as the header drawn whole");
    hp_head_full = 0;
    ui_message("HEADPOS");
    frames(3);
    check(hp_head_full >= 1u, "header: a message draws the header whole");
    tap(B_PLAY);
    frames(5);
}
