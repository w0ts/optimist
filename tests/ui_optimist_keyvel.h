/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the keys' velocity (issue #134): SHIFT2 + SELECT sets it, a key plays and records it. Included after the helpers. */

static uint32_t kv_rec_vel(track_t *t)                  /* the velocity of the first note in the track, 0: none */
{
    uint32_t i;
    for (i = 0; i < trk_len(t); i++)
        if (t->step[i].time == ST_NOTE && t->step[i].n)
            return t->step[i].vel;
    return 0;
}
static void keyvel_tests(void)
{
    track_t *t = &trk[0];
    uint32_t i;
    key_vel = 100;
    reset_ui();
    if (song.playing)
        tap(B_PLAY), frames(3);
    song.sel = 0;
    song.octave = 0;
    ui.scr = SCR_HOME;
    frame();
    check(key_vel == 100, "keys' velocity: 100 at power on (what the keys always gave)");
    press(B_OCTUP);
    turn(EN_SELECT, -3);
    check(key_vel == 97 && !strcmp(ui.msg, "VELOCITY: 97"), "SHIFT2 + SELECT turned left 3: 97, said");
    turn(EN_SELECT, 40);
    check(key_vel == 127, "... stops at 127");
    turn(EN_SELECT, -200);
    check(key_vel == 1, "... and at 1");
    turn(EN_SELECT, 96);
    release(B_OCTUP);
    frames(2);
    check(key_vel == 97 && song.octave == 0, "... back to 97; OCT+ let go: no octave change");
    turn(EN_SELECT, 1);
    check(key_vel == 97, "SELECT alone: the velocity is not touched");
    for (i = 0; i < NSTEP; i++)
        t->step[i].n = 0, t->step[i].time = ST_REST;
    song.rec = 1u;
    lane_transport(1);
    key(0);
    frames(4);
    check(kv_rec_vel(t) == 97, "a key recorded: the step holds velocity 97");
    lane_transport(0);
    song.rec = 0;
    key_vel = 100;
}
