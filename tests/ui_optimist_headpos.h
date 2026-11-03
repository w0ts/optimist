/* The header while playing (2026-11-03: the clock icons replaced bar.beat, so nothing moves in it; before, op_draw.c head_pos_only; the user, 2026-10-31: the mirror over Bluetooth "DEAD SLOW"): while
 * playing, bar.beat moving draws its own text again, never the header's 240 x 25 (850 B a beat to the mirror, measured on
 * the FM-1: 2.4 KB/s of the 4.6 KB/s its Bluetooth link carried). The header then is pixel for pixel the one drawn whole,
 * and a message over it still draws it whole */
static uint16_t hp_shot[240 * 25];
/* the clock icon (the user, 2026-11-03: one icon, the clock's source, coloured by its state): none on the internal clock with
 * nothing heard; a MIDI clock heard amber, followed green; Link hidden without Wi-Fi or a session, red in a JAM mode not
 * connected, and that first */
static void hs_state_tests(void)
{
    uint32_t now = SYNC_NOW(), st = 0;
    head_t h;
    uint8_t src = sy.src;
    memset(sy_pr, 0, sizeof sy_pr);
    sy.src = 0;
#if !(WIFI_UI && FELUCCA_LINK)
    check(head_clock(&st) == HC_NONE, "clock icon: the internal clock, nothing heard: no icon");
#endif
    sy_pr[MSRC_USB].cnt = SY_STEADY, sy_pr[MSRC_USB].last = now;
    check(head_midi() == HS_THERE && hs_col(HS_THERE) == C_WARN, "clock icon: a USB clock heard, not followed: amber");
    sy.src = MSRC_TRS;
    check(head_midi() == HS_FOLLOW && hs_col(HS_FOLLOW) == C_OK, "clock icon: a TRS clock followed: green");
    check(head_clock(&st) == HC_MIDI && st == HS_FOLLOW,
          "clock icon: a MIDI clock followed: the DIN socket, green");
    check(hs_col(HS_BAD) == C_ERR && hs_col(HS_NONE) == C_DIM, "clock icon: red a JAM mode not connected, grey waiting");
#if WIFI_UI && FELUCCA_LINK
    {
        uint8_t m = wn_st.mode;
        wn_st.mode = WN_MODE_OFF;
        check(head_link() == HS_OFF, "clock icon: Link, Wi-Fi off: no Link icon");
#if WF_JAM                                              /* (JAM: the release's own Wi-Fi stack only) */
        wn_st.mode = WN_MODE_JAM_AUTO;
        wc.st = WC_SCAN;
        check(wf_jam_host_now() || (head_clock(&st) == HC_LINK && st == HS_BAD),
              "clock icon: JAM and not joined: the Link icon, red, before the MIDI clock followed");
#endif
        wn_st.mode = m;
    }
#else
    check(head_link() == HS_OFF, "clock icon: no Wi-Fi Link in this build: never the Link icon");
#endif
    sy.src = src;
    memset(sy_pr, 0, sizeof sy_pr);
    reset_ui();
    op_enter(SCR_HOME);
    head_make(&h);
    check(!h.pos[0] || h.room > 0, "header: HOME's title keeps its room with the clock icon's");
}
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
    snprintf(b, sizeof b, "header: playing 4 s, the clock icons in bar.beat's place: nothing drawn into it (%u px, %u whole-width blits)",
             (unsigned)hp_head_px, (unsigned)hp_head_full);
    check(hp_head_px == 0u && hp_head_full == 0u, b);
    (void)same;
    (void)hp_shot;
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
    hs_state_tests();
}
