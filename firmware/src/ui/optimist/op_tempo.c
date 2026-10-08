/* SPDX-License-Identifier: GPL-3.0-only */
/* TEMPO, a page held open by PLAY (docs/UI-OPTIMIST-DESIGN.md section 4.8, phase 4): PLAY held past TEMPO_HOLD_MS
 * shows it, let go the screen before comes back; a tap of PLAY stays start / stop (on its release: a press cannot
 * know yet that it is a tap). A key while PLAY is down opens it at once.
 *   rows       TEMPO: BPM · NUDGE · SWING · SYNC     REC: MODE · LENGTH · START · CLICK (the REC MODES dials)
 *   OCT- / +   held: the nudge, the clock a few percent slower / faster while held (seq.c clk_nudge, the BPM value
 *              never changes; nothing while an external clock is followed)
 *   white key  tap tempo (two taps or more)
 * The panel: the tempo big, the beat, the nudge, the clock followed (A:USB ...) and its RX light. */
#define TEMPO_HOLD_MS 400u              /* PLAY held this long: the page (shorter: a tap, start / stop) */
#define TEMPO_NUDGE 10                  /* the nudge, in 1/256 of the tempo: 3.9 % */
static void tap_tempo(void);                           /* op_layers.c */
static struct {
    uint8_t on;                         /* the page is up */
    uint8_t prev;                       /* the screen under it */
    uint8_t pend;                       /* PLAY down, nothing done yet: a tap (start / stop) when let go early */
    uint8_t ext_said;                   /* "external clock" said for this hold */
    uint32_t t0;                        /* PLAY's press */
} tp;
static const uint8_t REC_LENS[3] = {16u, 32u, 64u};    /* LENGTH: 1, 2, 4 bars of 1/16 */

static void tempo_open(void)
{
    tp.pend = 0;
    if (tp.on)
        return;
    tp.on = 1;
    tp.prev = ui.scr;
    tp.ext_said = 0;
    ui.scr = SCR_TEMPO;
    ui.hot = 0;
    ui.hot_lit = 0;
    ui.force = 1;
}
static void tempo_close(void)
{
    clk_nudge = 0;
    if (!tp.on)
        return;
    tp.on = 0;
    if (ui.scr == SCR_TEMPO)
        ui.scr = tp.prev != SCR_TEMPO ? tp.prev : (uint8_t)SCR_HOME;
    ui.hot = 0;
    ui.hot_lit = 0;
    ui.force = 1;
}
static void tempo_key(int32_t w)                        /* a key while PLAY is held: the page, a white key taps */
{
    tempo_open();
    if (w >= 0)
        tap_tempo();
}
/* once a frame (op_input.c): the page after the hold, the nudge while OCT- / OCT+ are held */
static void tempo_frame(uint32_t held)
{
    int8_t n = 0;
    if (tp.pend && (held & (1u << panel.btn[B_PLAY])) && fm1_ms - tp.t0 >= TEMPO_HOLD_MS)
        tempo_open();
    if (tp.on) {
        n = held & dyn_bit[0] ? -TEMPO_NUDGE : held & dyn_bit[1] ? TEMPO_NUDGE : 0;
        if (n && sy.src) {                              /* (an external clock: its tempo, no nudge) */
            if (!tp.ext_said)
                ui_message("NUDGE: EXTERNAL CLOCK");
            tp.ext_said = 1;
            n = 0;
        }
    }
    if (clk_nudge != n)
        clk_nudge = n;
}

/* ---- the page's rows */
static uint32_t rec_len_ix(void)                        /* the selected track's LEN as 1, 2, 4 bars: 0..2 */
{
    int32_t len = TSEL->p[P_SLEN];
    return len > 32 ? 2u : len > 16 ? 1u : 0u;
}
static uint32_t tp_rows(void) { return 2u; }
static void tp_name(uint32_t r, char *b) { str_cpy(b, r ? "REC" : "TEMPO", 12); }
static void tp_cell(uint32_t r, uint32_t k, cell_t *c)
{
    static const char *const LEN[3] = {"1 BAR", "2 BARS", "4 BARS"};
    if (!r) {
        if (k == 0u)
            cell_param(c, &GP[G_BPM], &song.g[G_BPM]);
        else if (k == 2u)
            cell_param(c, &GP[G_SWING], &song.g[G_SWING]);
        else if (k == 3u && FELUCCA_MIDI_CLOCK)
            cell_param(c, &GP[G_SYNC], &song.g[G_SYNC]);
        else if (k == 3u)
            cell_clear(c);
        else {                                          /* NUDGE: what OCT- / OCT+ do now, a read-out */
            cell_clear(c);
            c->label = "NUDGE";
            c->kind = CK_RO;
            str_cpy(c->val, clk_nudge < 0 ? "-3.9" : clk_nudge > 0 ? "+3.9" : "0", sizeof c->val);
            c->unit = "%";
            cell_gauge(c, 0, -1, 1, clk_nudge < 0 ? -1 : clk_nudge > 0 ? 1 : 0);
        }
        return;
    }
    if (k == 3u) {
        cell_param(c, &GP[G_CLOCK], &song.g[G_CLOCK]);
        return;
    }
    cell_clear(c);
    c->kind = CK_VAL;
    if (k == 1u) {
        c->label = "LENGTH";
        str_cpy(c->val, LEN[rec_len_ix()], sizeof c->val);
        cell_gauge(c, 1, 0, 2, (int32_t)rec_len_ix());
        return;
    }
#if FELUCCA_REC_MODES
    c->label = k ? "START" : "MODE";
    str_cpy(c->val, k ? (rec_count ? "COUNT" : "NOTE") : (rec_tempo ? "TEMPO" : "FREE"), sizeof c->val);
    cell_gauge(c, 1, 0, 1, k ? rec_count : rec_tempo);
#else
    c->kind = CK_NONE;
    c->label = "";
    str_cpy(c->val, "-", sizeof c->val);                /* (REC MODES not in this build) */
#endif
}
static void tp_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    if (!r) {
        if (k == 0u)
            val_turn(&GP[G_BPM], &song.g[G_BPM], k, s, fine);
        else if (k == 2u)
            val_turn(&GP[G_SWING], &song.g[G_SWING], k, s, fine);
        else if (k == 3u && FELUCCA_MIDI_CLOCK)
            val_turn(&GP[G_SYNC], &song.g[G_SYNC], k, s, fine);
        return;
    }
    if (k == 3u) {
        val_turn(&GP[G_CLOCK], &song.g[G_CLOCK], k, s, fine);
        return;
    }
    if (s == OP_RESET)
        return;
    if (k == 1u) {                                      /* LENGTH: the selected track's, 1 / 2 / 4 bars */
        TSEL->p[P_SLEN] = (int16_t)REC_LENS[clamp((int32_t)rec_len_ix() + (s > 0 ? 1 : -1), 0, 2)];
        sync_reload = 1;
        return;
    }
#if FELUCCA_REC_MODES
    if (k == 0u)
        rec_tempo = (uint8_t)(s > 0);
    else
        rec_count = (uint8_t)(s > 0);
    settings_later = 1;                                 /* (settings of the FM-1: saved once stopped) */
#endif
}
static int tp_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    cell_t c;
    (void)ok;
    tp_cell(r, k, &c);
    if (c.kind != CK_VAL || c.gk != GK_PILL)
        return 0;
    tp_turn(r, k, c.gv ? -1 : 1, 1);                    /* an on / off value: toggled */
    return 1;
}
