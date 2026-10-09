/* SPDX-License-Identifier: GPL-3.0-only */
/* TEMPO, a page held open by PLAY (SLOOP rulings: "Tempo page = PLAY held: BPM, swing, sync, nudge"). The page's logic
 * (state, PLAY's tap / hold, the nudge) after the Optimist UI's ui/optimist/op_tempo.c; the drawing is SLOOP's own: the
 * TRACKS look (a header, four rows of 36 px, four dials), every value with its picture.
 *   PLAY tap     plays / stops, on its RELEASE (a press cannot know yet that it is a tap; the tap is handed on as the
 *                press the transport code has always been given, so it does what it did, a click later)
 *   PLAY held    HOLD_MS (core/hold.h, the HOLD setting): the page. Let go: the screen before comes back and the
 *                release does not play / stop. OCT- / OCT+ pressed while PLAY is down open it at once
 *   KNOB 1..4    BPM (beat lights), SWING (the timing of the off-beat 16th), SYNC (INT USB TRS AUTO, the clock followed
 *                lit), NUDGE (a read-out of OCT- / OCT+)
 *   SELECT       the BPM, as on every screen
 *   OCT- / OCT+  held: the clock 3.9 % slower / faster (seq.c clk_nudge; G_BPM never changes, it is back when let go;
 *                nothing while an external clock is followed)
 * Included by felucca.c after ui_studio.c (its dials, header and look). */
#define TEMPO_NUDGE 10                  /* the nudge, in 1/256 of the tempo: 3.9 % */
static struct {
    uint8_t on;                         /* the page is up */
    uint8_t pend;                       /* PLAY down, nothing decided yet: a tap when let go early */
    uint8_t eaten;                      /* the press was a free take's (seq.c ft_owns_press): neither a tap nor the page */
    uint8_t shown;                      /* drawn last frame (ui_draw.c clears the screen when it goes) */
    uint8_t ext_said;                   /* "external clock" said for this hold */
    uint32_t t0;                        /* PLAY's press (ms) */
} tp;
static int32_t accel_range(const param_desc_t *d);      /* ui_input.c */

static void tempo_open(void)
{
    tp.pend = 0;
    if (tp.on)
        return;
    tp.on = 1;
    tp.ext_said = 0;
    ui.hot_t = 0;
    ui.msg_t = 0;
    ui.force = 1;
}
static void tempo_close(void)
{
    clk_nudge = 0;
    tp.pend = 0;
    if (!tp.on)
        return;
    tp.on = 0;
    ui.hot_t = 0;
    ui.force = 1;
}
static int layer_button_down(void)                      /* a layer button held or a layer locked open */
{
    uint32_t l;
    if (ly_lock != LY_PLAY)
        return 1;
    for (l = LY_FX; l < LY_COUNT; l++)
        if (fm1_in.buttons & ly_bit[l])
            return 1;
    return 0;
}
/* once a frame, before the layers: PLAY's press is held back (taken out of *pressed) until it is known what it is.
 * A tap (let go before HOLD_MS) is put back as a press when it is let go; a hold opens the page and is never a press */
static void tempo_play(uint32_t *pressed)
{
    uint32_t pb = 1u << panel.btn[B_PLAY], down = (fm1_in.buttons & pb) != 0u, now = fm1_ms;
    uint32_t oct = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    if (*pressed & pb) {
        tp.pend = 1;
        tp.t0 = now;
        tp.eaten = (uint8_t)ft_owns_press();            /* (a free take closed or dropped by this press: it is seq.c's) */
        *pressed &= ~pb;
    }
    if (tp.pend && down && !tp.eaten && !layer_button_down() && !ui.hold_kind) {
        if (now - tp.t0 >= HOLD_MS)
            tempo_open();
        else if (*pressed & oct) {                      /* PLAY + OCT-: the nudge at once, no octave step */
            tempo_open();
            *pressed &= ~oct;
        }
    }
    if (!down && (tp.pend || tp.on)) {
        if (tp.on)
            tempo_close();                              /* the hold's release: the screen before; no play / stop */
        else if (!tp.eaten)
            *pressed |= pb;                             /* a tap: play / stop, as ever */
        tp.pend = 0;
    }
}

/* the BPM from SELECT or KNOB 1 (the page is a deliberate act: BPM LOCK does not apply here) */
static void tempo_bpm(uint32_t role, int32_t s)
{
    song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(role, s, 200), GP[G_BPM].min, GP[G_BPM].max);
}
static void tempo_hot(uint32_t k)
{
    ui.hot_col = (uint8_t)k;
    ui.hot_t = PH_HOT;
}
/* the page's knobs and the nudge, once a frame while it is up */
static void tempo_input(uint32_t pressed)
{
    int32_t s, n = 0;
    uint32_t held = fm1_in.buttons;
    (void)pressed;                                      /* (every button but PLAY waits; the keys play on) */
    if ((s = panel_enc(EN_K1)) != 0) {
        tempo_bpm(EN_K1, s);
        tempo_hot(0);
    }
    if ((s = panel_enc(EN_K2)) != 0) {
        song.g[G_SWING] = (int16_t)clamp(song.g[G_SWING] + accel(EN_K2, s, accel_range(&GP[G_SWING])), GP[G_SWING].min, GP[G_SWING].max);
        tempo_hot(1);
    }
    if ((s = panel_enc(EN_K3)) != 0) {
        if (FELUCCA_MIDI_CLOCK)
            song.g[G_SYNC] = (int16_t)clamp(song.g[G_SYNC] + (s > 0 ? 1 : -1), 0, SYNC_AUTO);
        tempo_hot(2);
    }
    if ((s = panel_enc(EN_K4)) != 0)
        tempo_hot(3);                                   /* (a read-out: OCT- / OCT+ are the nudge) */
    if ((s = panel_enc(EN_SELECT)) != 0) {
        tempo_bpm(EN_SELECT, s);
        ui.bpm_t = 40;
    }
    panel_enc(EN_ALGO);                                 /* (track and sound wait: no jump afterwards) */
    panel_enc(EN_PRESET);
    if ((held & dyn_bit[0]) && !(held & dyn_bit[1]))
        n = -TEMPO_NUDGE;
    else if ((held & dyn_bit[1]) && !(held & dyn_bit[0]))
        n = TEMPO_NUDGE;
    if (n && FELUCCA_MIDI_CLOCK && sy.src) {            /* (an external clock: its tempo, no nudge) */
        if (!tp.ext_said)
            ui_message("NUDGE: EXTERNAL CLOCK");
        tp.ext_said = 1;
        n = 0;
    }
    if (clk_nudge != n)
        clk_nudge = (int8_t)n;
}

/* ------------------------------------------------------------- drawing --- */
#define TP_X 88                                         /* the pictures: x 88 .. 234 */
#define TP_W 146
static int32_t tempo_swing_off(int32_t sw)              /* the off-beat 16th inside its 72 px pair: 50 % (36) .. 75 % (54) */
{
    return 36 + clamp(sw, 0, 100) * 18 / 100;
}
static void tempo_val(uint32_t i, char *b)
{
    switch (i) {
    case 0:
        fmt_int(b, song.g[G_BPM]);
        break;
    case 1:
        swing_str(b, song.g[G_SWING]);
        break;
    case 2:
        str_cpy(b, FELUCCA_MIDI_CLOCK ? N_SYNC[(uint32_t)song.g[G_SYNC] & 3u] : "-", 8);
        break;
    default:
        str_cpy(b, clk_nudge < 0 ? "-3.9%" : clk_nudge > 0 ? "+3.9%" : "0", 8);
        break;
    }
}
static void tempo_picture(uint32_t i)
{
    uint32_t k, beat = clk_beat % 4u, playing = song.playing;
    int32_t x;
    switch (i) {
    case 0:                                             /* the four beats of the bar: the one playing lit, the downbeat white */
        for (k = 0; k < 4u; k++)
            cv_rect(TP_X + 2 + (int32_t)k * 36, 7, 30, 22,
                    playing && beat == k ? (k ? TE_G4 : C_WHITE) : TE_G2);
        break;
    case 1: {                                           /* one beat, two pairs of 16ths: the straight 16th a ghost, the swung one where it falls */
        int32_t off = tempo_swing_off(song.g[G_SWING]);
        cv_rect(TP_X, 28, TP_W, 1, TE_G2);
        for (k = 0; k < 2u; k++) {
            x = TP_X + (int32_t)k * 72;
            cv_rect(x, 6, 2, 22, C_WHITE);              /* the 8th */
            cv_rect(x + 36, 16, 2, 12, TE_G2);          /* the straight 16th (50 %) */
            cv_rect(x + off, 10, 3, 18, TE_G4);         /* the swung one */
        }
        cv_rect(TP_X + 144, 6, 2, 22, C_WHITE);
        break;
    }
    case 2:                                             /* INT USB TRS AUTO; the clock followed, a bar under it */
        if (!FELUCCA_MIDI_CLOCK)
            break;
        for (k = 0; k < 4u; k++) {
            uint32_t sel = ((uint32_t)song.g[G_SYNC] & 3u) == k;
            const char *n = N_SYNC[k];
            x = TP_X + (int32_t)k * 37;
            cv_rect(x, 6, 35, 22, sel ? TE_G4 : TE_G1);
            cv_text(x + (35 - text_w(&FONT_S, n)) / 2, 8, &FONT_S, n, sel ? C_BLACK : TE_G3);
            if (sy.src == k && k)
                cv_rect(x, 30, 35, 3, C_OK);
        }
        break;
    default: {                                          /* the nudge: -3.9 .. +3.9 around the middle */
        int32_t mid = TP_X + TP_W / 2;
        if (FELUCCA_MIDI_CLOCK && sy.src) {
            te_text_c(mid, 10, "external clock", TE_G3);
            break;
        }
        cv_rect(TP_X, 11, TP_W, 8, TE_G1);
        if (clk_nudge)
            cv_rect(clk_nudge < 0 ? mid - 60 : mid + 1, 11, 60, 8, C_WARN);
        cv_rect(mid, 6, 2, 18, TE_G4);
        cv_text(TP_X, 20, &FONT_S, "OCT-", TE_G3);
        cv_text(TP_X + TP_W - text_w(&FONT_S, "OCT+"), 20, &FONT_S, "OCT+", TE_G3);
        break;
    }
    }
}
static void tempo_draw(void)
{
    static uint32_t head, rows[4], footer;
    static const char *const LAB[4] = {"BPM", "Swing", "Sync", "Nudge"};
    char v[4][8];
    const char *val[4] = {v[0], v[1], v[2], v[3]};
    int32_t ratio[4];
    uint32_t i, hot = ui.hot_t ? ui.hot_col : 9u;
    if (!tp.shown) {
        tp.shown = 1;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    te_header("Tempo", C_WHITE, &head);
    for (i = 0; i < 4u; i++) {
        uint32_t h = (hot == i) * 5003u + (uint32_t)(song.g[G_BPM] * 31 + song.g[G_SWING] * 7 + song.g[G_SYNC]) * 131u;
        tempo_val(i, v[i]);
        h = studio_hash(h * 7u + (uint32_t)clk_nudge * 3u + sy.src * 977u + i, v[i]);
        if (i == 0u && song.playing)
            h = h * 31u + clk_beat % 4u + 1u;
        if (i == 0u && !song.playing)
            h = h * 3u + 1u;
        if (!ui.force && h == rows[i])
            continue;
        rows[i] = h;
        cv_begin(240, 36, C_BLACK);
        cv_text(4, 1, &FONT_S, LAB[i], hot == i ? C_WHITE : TE_G3);
        cv_text(4, 17, &FONT_S, v[i], hot == i ? C_WHITE : TE_G4);
        tempo_picture(i);
        cv_blit(0, 40u + i * 36u);
    }
    ratio[0] = (song.g[G_BPM] - GP[G_BPM].min) * 5;     /* (40 .. 240: 200 steps of 5 over 1000) */
    ratio[1] = song.g[G_SWING] * 10;
    ratio[2] = FELUCCA_MIDI_CLOCK ? ((int32_t)song.g[G_SYNC] & 3) * 1000 / 3 : 0;
    ratio[3] = clk_nudge < 0 ? 0 : clk_nudge > 0 ? 1000 : 500;
    te_dials(184, LAB, val, ratio, 0x7E3u + sy.src, &footer, C_WHITE, 0xFu);
}
