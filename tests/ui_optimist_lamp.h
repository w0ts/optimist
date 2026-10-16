/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the transport LEDs against the transport (the user, 2026-10-16: "there IS a green led below the
 * play button ... green in play mode and red in record mode"). Green = PLAY's second LED (a matrix site with no key, row PA5
 * column 8: OP_LAMP_COL); red = REC's own LED. Every transport and record state shows the right one:
 * stopped both dark; playing green steady, REC half lit; REC armed (stopped) green dark, REC blinking; count-in green on the
 * beat; recording (live, free take) REC full, green out. Included after led_lvl (the LED test). */
static uint32_t lamp_g(void) { return (fm1_led[OP_LAMP_COL] >> OP_LAMP_ROW) & 1u; }
static uint32_t lamp_dim_g(void) { return (fm1_led_dim[OP_LAMP_COL] >> OP_LAMP_ROW) & 1u; }
static void lamp_idle(void)
{
    reset_ui();
    song.playing = 0;
    transport_req = 0;
    rec_wait = 0;
    song.rec = 0;
    ft_on = 0;
#if FELUCCA_REC_MODES
    ci_on = 0;
#endif
    song.sel = 0;
    frames(2);
}
static void lamp_tests(void)
{
    uint32_t i, on = 0, off = 0;
    lamp_idle();
    check(FM1_KEYMAP[OP_LAMP_ROW][OP_LAMP_COL] == -1, "lamp: PLAY's green is a matrix LED with no key (no button's LED is taken)");
    check(lamp_g() == 0 && !lamp_dim_g() && led_lvl(B_REC) != 2, "lamp: stopped, green dark, REC not full");
    song.playing = 1;
    frames(2);
    check(lamp_g() == 1 && led_lvl(B_REC) != 2, "lamp: playing, green");
    for (i = 0; i < 40u; i++) {
        frame();
        on += lamp_g();
        off += !lamp_g();
    }
    check(on == 40u && off == 0, "lamp: playing, green on every frame (steady, no blink)");
    song.playing = 0;
    frames(2);
    check(lamp_g() == 0, "lamp: stopped again, dark");
    rec_wait = 1;
    for (i = 0, on = 0; i < 40u; i++) {
        frame();
        on += lamp_g();
    }
    check(on == 0, "lamp: REC armed, stopped: green dark (REC blinks)");
    rec_wait = 0;
#if FELUCCA_REC_MODES
    rec_wait = 1;
    ci_on = 1;
    ci_u = 0;
    frames(1);
    check(lamp_g() == 1, "lamp: count-in, the beat: green");
    ci_u = BEAT_U / 2u;
    frames(1);
    check(lamp_g() == 0, "lamp: count-in, between the beats: dark");
    ci_on = 0;
    rec_wait = 0;
#endif
    song.playing = 1;
    song.rec = 1;
    frames(2);
    check(lamp_g() == 0 && led_lvl(B_REC) == 2, "lamp: recording (playing): green out, REC (red) full");
    song.rec = 0;
    song.playing = 0;
    frames(2);
    ft_on = 1;
    frames(2);
    check(lamp_g() == 0 && led_lvl(B_REC) == 2, "lamp: free take: green out, REC full");
    ft_on = 0;
    lamp_idle();
    tap(B_PLAY);
    frames(4);
    check(song.playing && lamp_g() == 1, "lamp: PLAY tapped: green");
    tap(B_REC);
    frames(2);
    check(song.rec != 0u && lamp_g() == 0 && led_lvl(B_REC) == 2, "lamp: REC tapped while playing: red, green out");
    tap(B_REC);
    frames(2);
    check(song.rec == 0u && song.playing && lamp_g() == 1, "lamp: REC off, still playing: green again");
    lamp_idle();
}
