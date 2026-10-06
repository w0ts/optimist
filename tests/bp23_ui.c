/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP 2.3 / X0X 0.10.1 backports' UI side (firmware/src/backports23.h), included by ui_pages_test.c after
 * backports_ui.c: each block only with its switch on (tests/run_tests.sh: ui_pages_bp23_test).
 *   panel    a calibration table read back with two labels on one button / knob: the default (FELUCCA_ST_STRICT)
 *   rec      the REC screen's dials (FELUCCA_REC_MODES): MODE, LENGTH, START, the count-in 4-3-2-1 on screen and
 *            on the PLAY light; screens rec-*.ppm
 *   lights   MENU > LIGHTS / KEYS / NOTES (FELUCCA_LIGHTS): the backlight layer on every button (and the C / white
 *            keys) at the level's pulse, what is lit stays lit, NOTES OFF hides the played notes (KEYLIT) */
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

#if FELUCCA_REC_MODES
static void bp23_rec(void)
{
    uint32_t i, shown = 0, lit = 0, k;
    song.sel = 0; song.playing = 0; song.rec = 0; rec_wait = 0; ft_on = 0; transport_req = 0; ci_on = 0; go_home(); frames(2);
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    rec_tempo = 0; rec_count = 0;
    trk[0].p[P_SLEN] = 16;
    tap(B_REC); frames(2);
    check(rec_wait && rec_shown, "rec: REC stopped, an empty project: armed, the REC screen");
    ui.force = 1; frame(); ppm("rec-ready-free");
    encs[panel.enc[EN_K1]] = 1; frames(2);
    check(rec_tempo == 1u && settings_later, "rec: KNOB 1 right: MODE tempo (a setting, saved once stopped)");
    encs[panel.enc[EN_K2]] = 1; frames(2);
    check(trk[0].p[P_SLEN] == 32, "rec: KNOB 2 right: LENGTH 1 -> 2 bars");
    encs[panel.enc[EN_K2]] = 5; frames(2);
    check(trk[0].p[P_SLEN] == 64, "rec: KNOB 2 right again: 4 bars, the top");
    encs[panel.enc[EN_K2]] = -2; frames(2);
    check(trk[0].p[P_SLEN] == 32, "rec: KNOB 2 left: 2 bars");
    ui.force = 1; frame(); ppm("rec-ready-tempo");
    encs[panel.enc[EN_K3]] = 1; frames(2);
    check(rec_count == 1u, "rec: KNOB 3 right: START count");
    ui.force = 1; frame(); ppm("rec-ready-count");
    key(14); frames(2);
    check(!song.playing && rec_wait && !ci_on, "rec: START count, a key: it only sounds");
    tap(B_PLAY); frames(1);
    check(ci_on && !song.playing, "rec: PLAY: the count-in");
    ui.force = 1; frame(); ppm("rec-count-4");
    for (k = 0; k < 400u && ci_on; k++) {             /* (a frame is ~16 ms: 4 beats at 120 BPM = 2 s) */
        frame();
        lit |= (fm1_led[0] | fm1_led[1] | fm1_led[2] | fm1_led[3] | fm1_led[4] | fm1_led[5] | fm1_led[6] |
                fm1_led[7] | fm1_led[8] | fm1_led[9] | fm1_led[10]) != 0;
        if (ci_beat == 2u && !(shown & 4u)) { shown |= 4u; ui.force = 1; frame(); ppm("rec-count-2"); }
        if (ci_beat == 3u && !(shown & 8u)) { shown |= 8u; ui.force = 1; frame(); ppm("rec-count-1"); }
    }
    check(shown == 12u, "rec: the count-in shows its beats (3, 2, 1 seen)");
    check(!ci_on && song.playing && song.rec, "rec: after the bar: playing and recording");
    tap(B_PLAY); frames(4);
    transport_req = 2; frames(2);
    rec_tempo = 0; rec_count = 0; settings_later = 0;
    go_home(); frames(2);
}
#endif

#if FELUCCA_LIGHTS
static uint32_t bg_count(void) { uint32_t c, n = 0; for (c = 0; c < FM1_NCOL; c++) n += (uint32_t)__builtin_popcount(fm1_led_bg[c]); return n; }
static void bp23_lights(void)
{
    uint32_t k, ok, nb;
    song.sel = 0; song.playing = 0; go_home(); frames(2);
    lights_lvl = 0; lights_keys = 0; lights_notes_off = 0;
    ui.menu = 1; ui.menu_sel = MI_LIGHTS; ui.force = 1; frame();
    ppm("menu-lights-off");
    check(bg_count() == 0u && fm1_led_bg_ns == 0u, "lights: OFF: no backlight");
    encs[panel.enc[EN_K1]] = 1; frames(2);
    encs[panel.enc[EN_K1]] = 1; frames(2);
    check(lights_lvl == LIGHTS_MID && fm1_led_bg_ns == 1000u, "lights: KNOB 1 +2: MID, a 1 us pulse a frame");
    nb = bg_count();
    check(nb > 0u, "lights: the buttons glow (the backlight layer is set)");
    ok = 1;
    for (k = 0; k < FM1_NCOL; k++)
        ok &= !(fm1_led_bg[k] & fm1_led[k]);
    check(ok, "lights: a lit LED is not in the backlight layer (full light)");
    encs[panel.enc[EN_K1]] = 1; frames(2);
    encs[panel.enc[EN_K1]] = 1; frames(2);
    check(lights_lvl == LIGHTS_HIGH && fm1_led_bg_ns == 2000u, "lights: KNOB 1 on: HIGH (stops at the top)");
    ui.force = 1; frame(); ppm("menu-lights-high");
    ui.menu_sel = MI_KEYS; encs[panel.enc[EN_K1]] = 1; frames(2);
    check(lights_keys == KEYS_C, "lights: KEYS C KEYS");
    check(lights_keys_mask() == (1u << 7 | 1u << 19), "lights: the C keys (C4, C5: keys 7 and 19) glow");
    encs[panel.enc[EN_K1]] = 1; frames(2);
    ok = __builtin_popcount(lights_keys_mask()) == 16 && !(lights_keys_mask() & (1u << 1 | 1u << 3 | 1u << 5));
    check(lights_keys == KEYS_WHITE && ok, "lights: KEYS WHITE KEYS: the 16 white keys glow, no black one (F#3 G#3 A#3)");
    ui.force = 1; frame(); ppm("menu-keys-white");
    lights_lvl = 0; frames(2);
    ui.menu_sel = MI_KEYS; encs[panel.enc[EN_K1]] = -1; frames(2);
    check(lights_lvl == LIGHTS_LOW, "lights: KEYS changed with LIGHTS OFF: LIGHTS goes to LOW");
#if FELUCCA_KEYLIT
    ui.menu = 0; ui.force = 1; frame();
    trk[0].seq_n = 1; trk[0].seq_notes[0] = 60;      /* C4 = key 7 (keylit.c) */
    check(keys_lit() == 1u << 7, "lights: NOTES ON: a sequenced C4 lights its key (KEYLIT)");
    lights_notes_off = 1;
    check(keys_lit() == 0u, "lights: NOTES OFF: it does not");
    trk[0].seq_n = 0;
    lights_notes_off = 0;
    ui.menu = 1; ui.menu_sel = MI_NOTES; ui.force = 1; frame();
    encs[panel.enc[EN_K1]] = -1; frames(2);
    check(lights_notes_off == 1u, "lights: MENU NOTES KNOB 1 left: OFF");
    ui.force = 1; frame(); ppm("menu-notes-off");
    encs[panel.enc[EN_K1]] = 1; frames(2);
#endif
    lights_lvl = 0; lights_keys = 0; lights_notes_off = 0;
    ui.menu = 0; ui.force = 1; frames(2);
    go_home(); frames(2);
}
#endif

static void bp23_ui_tests(void)
{
#if FELUCCA_LIGHTS
    bp23_lights();
#endif
#if FELUCCA_REC_MODES
    bp23_rec();
#endif
#if FELUCCA_ST_STRICT
    bp23_panel();
#endif
}
