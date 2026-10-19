/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the transport LEDs against the transport. PLAY has two LEDs: its own, red, and a green one at a matrix
 * site with no key (row PA5 column 8: OP_LAMP_COL; the user, 2026-10-16: "there IS a green led below the play button"). As the
 * user asked, 2026-10-19 ("green while paused (dimmed), fully green while playing, flashing red, and red while recording,
 * flashing green"): stopped green dim, red off; playing green full, red on each beat; recording (live, free take) red full,
 * green on each beat; the count-in the green on its beats. REC's own LED as before (full recording, blinking armed). Included
 * after led_lvl (the LED test). */
static uint32_t lamp_g(void) { return (fm1_led[OP_LAMP_COL] >> OP_LAMP_ROW) & 1u; }
static uint32_t lamp_dim_g(void) { return (fm1_led_dim[OP_LAMP_COL] >> OP_LAMP_ROW) & 1u; }
static void lamp_idle(void)
{
    reset_ui();
    song.playing = 0;
    transport_req = 0;
    arrangement_enabled = 0;                            /* (LOOP: an earlier test's SAVE on SONG's MODE row may leave SONG on) */
    rec_wait = 0;
    song.rec = 0;
    ft_on = 0;
#if FELUCCA_REC_MODES
    ci_on = 0;
#endif
    song.sel = 0;
    frames(2);
}
static void lamp_count(uint32_t frames_n, uint32_t *g_on, uint32_t *g_off, uint32_t *r_on, uint32_t *r_off)
{                                                       /* over frames_n frames: the green full / not, PLAY's red full / not */
    uint32_t i;
    *g_on = *g_off = *r_on = *r_off = 0;
    for (i = 0; i < frames_n; i++) {
        frame();
        *g_on += lamp_g(), *g_off += !lamp_g();
        *r_on += led_lvl(B_PLAY) == 2u, *r_off += led_lvl(B_PLAY) != 2u;
    }
}
static void lamp_tests(void)
{
    uint32_t go, gf, ro, rf;
    lamp_idle();
    check(FM1_KEYMAP[OP_LAMP_ROW][OP_LAMP_COL] == -1, "lamp: PLAY's green is a matrix LED with no key (no button's LED is taken)");
    check(lamp_g() == 0 && lamp_dim_g() && led_lvl(B_PLAY) == 0 && led_lvl(B_REC) != 2, "lamp: stopped, green dim, PLAY's red off, REC not full");
    song.playing = 1;
    lamp_count(60, &go, &gf, &ro, &rf);                 /* (60 frames, ~1 s: two beats at 120 BPM) */
    check(go == 60u && !lamp_dim_g() && ro && rf && led_lvl(B_REC) != 2, "lamp: playing, green full on every frame, PLAY's red on each beat");
    song.playing = 0;
    frames(2);
    check(lamp_g() == 0 && lamp_dim_g() && led_lvl(B_PLAY) == 0, "lamp: stopped again, green dim, red off");
    rec_wait = 1;
    lamp_count(40, &go, &gf, &ro, &rf);
    check(go == 0 && lamp_dim_g() && ro == 0, "lamp: REC armed, stopped: green dim, red off (REC blinks)");
    rec_wait = 0;
#if FELUCCA_REC_MODES
    rec_wait = 1;
    ci_on = 1;
    ci_u = 0;
    frames(1);
    check(lamp_g() == 1, "lamp: count-in, the beat: green");
    ci_u = BEAT_U / 2u;
    frames(1);
    check(lamp_g() == 0 && !lamp_dim_g(), "lamp: count-in, between the beats: dark");
    ci_on = 0;
    rec_wait = 0;
#endif
    song.playing = 1;
    song.rec = 1;
    lamp_count(60, &go, &gf, &ro, &rf);
    check(ro == 60u && go && gf && led_lvl(B_REC) == 2, "lamp: recording (playing): PLAY's red full, the green on each beat, REC full");
    song.rec = 0;
    song.playing = 0;
    frames(2);
    ft_on = 1;
    frames(2);
    check(led_lvl(B_PLAY) == 2 && !lamp_dim_g() && led_lvl(B_REC) == 2, "lamp: free take: PLAY's red full, REC full");
    ft_on = 0;
    lamp_idle();
    tap(B_PLAY);
    frames(4);
    check(song.playing && lamp_g() == 1, "lamp: PLAY tapped: green full");
    tap(B_REC);
    frames(2);
    check(song.rec != 0u && led_lvl(B_PLAY) == 2 && led_lvl(B_REC) == 2, "lamp: REC tapped while playing: red full");
    tap(B_REC);
    frames(2);
    check(song.rec == 0u && song.playing && lamp_g() == 1, "lamp: REC off, still playing: green full again");
    lamp_idle();
}
