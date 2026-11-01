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
    {   /* every page the user plays on (2026-11-01: "still slow" over Bluetooth with PATTERNS / SECTIONS on): 4 s playing on
         * each, no whole-width header blit after the first second */
        static const uint8_t PG[] = {SCR_HOME, SCR_SOUND, SCR_FX, SCR_STEP, SCR_SONG, SCR_GLOBAL};
        uint32_t k, full = 0, px = 0;
        char who[64] = "";
        for (k = 0; k < sizeof PG; k++) {
            reset_ui();
            op_enter(PG[k]);
            frame();
            tap(B_PLAY);
            frames(60);
            hp_head_px = hp_head_full = 0;
#ifdef MIRROR_CHECK
            {   /* the mirror's bytes a second while playing (what a link must carry to keep up) */
                uint32_t b0 = mc_bytes, f0 = mc_frames;
                frames(240);
                printf("  mirror, playing on page %u: %u B/s, %u frames/s\n", (unsigned)PG[k], (unsigned)(mc_bytes - b0) / 4u,
                       (unsigned)(mc_frames - f0) / 4u);
            }
#else
            frames(240);
#endif
            if (hp_head_full && strlen(who) < 50u)
                snprintf(who + strlen(who), sizeof who - strlen(who), " %u:%u", (unsigned)PG[k], (unsigned)hp_head_full);
            full += hp_head_full, px += hp_head_px;
            tap(B_PLAY);
            frames(5);
        }
        snprintf(b, sizeof b, "header: playing 4 s on HOME SOUND FX STEP SONG GLOBAL: %u px into the header, %u whole-width blits%s",
                 (unsigned)px, (unsigned)full, who);
        check(full == 0u, b);
    }
}
