/* SPDX-License-Identifier: GPL-3.0-only */
/* LANE FX, a page held open by a drum lane's black key (FELUCCA_LANE_FX; the user, 2026-10-26: "while in drum mode, hold
 * an black key to select a drum lane, then a 4 line screen shows up with midi fx like fill, rolls etc for that lane").
 * The drum track selected, the keys playing (no layer, no NAME, no shift held, a page with a track), one lane's black
 * key held alone past the hold time (SYSTEM HOLD) opens it; the key let go, the screen before comes back. The key's
 * press still plays and selects the lane as a tap does (op_input.c drum_hit_pick).
 *   row FX     ROLL (OFF x2 x3 x4 x6 x8) · CHANCE (0..100 %, 5 % a detent; past 100 EVERY: 1:2 .. 4:4, the loops the lane
 *              plays, op_step.c chc_turn) · FILL (ALWAYS / FILL / NO FILL) · NUDGE (-32..31, 1/64 of a step)
 *   row FX 2   VRAND (velocity at random, 0..64) · FLAM (OFF, 5..40 ms) · RAMP (-100..100 %: a ROLL's hits fade in / out) · -
 * KNOB 1-4, SHIFT + a knob its default, SELECT the row. The pattern's events (seq/auto.c lfx_get / lfx_set), heard from
 * the next step (seq/lane_fx.c) */
static int keys_none(void);                             /* op_input.c */
static int name_on(void);                               /* op_name.c */
static struct {
    uint8_t on;                         /* the page is up */
    uint8_t prev;                       /* the screen under it */
    int8_t key;                         /* the key held (-1: none), its lane */
    uint8_t lane;
    uint32_t t0;                        /* its press */
    uint32_t sess, turn_ms;             /* the undo level the knobs' gesture writes (0: none open), its last turn */
} lx = {0, 0, -1, 0, 0, 0, 0};
/* one undo level a gesture (the user, 2026-10-26: "cleverly count done events, one per time the move, the last value
 * after a timeout"): the knobs turned with no pause of LX_UNDO_MS are one level, closed once they rest (or the page goes) */
#define LX_UNDO_MS 1000u

/* A step held on any page of the drum track (the white keys are steps on every page there: seq.c) shows its cards, as on
 * STEP (the user, 2026-10-26: "plocks pages (hold on steps keys) should work on all pages with step seq on 11+16 keys"):
 * past the hold time STEP's held-step view comes up over the page (no step_reset: the step stays held), every step let go,
 * the page before comes back. Not where the keys are something else (a layer, NAME, a page with no track, TEMPO, SONG) */
static struct {
    uint8_t on, prev;                   /* STEP shown for a held step, the screen under it */
} speek;
static void sp_frame(uint32_t held)
{
    if (speek.on) {
        if (st.held || st.latch)
            return;
        speek.on = 0;
        if (ui.scr == SCR_STEP)
            ui.scr = speek.prev;
        ui.force = 1;
        return;
    }
    if (!st.held || !is_drum(TSEL) || ui.scr == SCR_STEP || ui.scr == SCR_TEMPO || ui.scr == SCR_SONG || ui.scr == SCR_LFX ||
        master_sel() || name_on() || lay_now() != LY_PLAY || keys_none() || (held & 0x3FFFu) || fm1_ms - st.t_down < op_hold_ms())
        return;
    speek.on = 1;
    speek.prev = ui.scr;
    ui.scr = SCR_STEP;
    ui.force = 1;
}

#if FELUCCA_LANE_FX
enum { LXR_FX, LXR_FX2, LXR_N };                        /* the page's rows (SELECT) */
static const char *const LX_ROLL[LFX_ROLL_N + 1u] = {"OFF", "x2", "x3", "x4", "x6", "x8"};
static const char *const LX_FILL[3] = {"ALWAYS", "FILL", "NO FILL"};
static const uint8_t LX_CARD[LXR_N][4] = {{LFX_I_ROLL, LFX_I_CHANCE, LFX_I_FILL, LFX_I_NUDGE},   /* (KNOB 1-4: the user's order) */
                                          {LFX_I_VRND, LFX_I_FLAM, LFX_I_RAMP, 0xFF}};   /* (EVERY: CHANCE past 100) */
/* the lane's CHANCE knob (op_step.c chc_turn): its CHANCE, past 100 its EVERY */
static int32_t lx_chc(const track_t *t) { int32_t e = lfx_get(t, lx.lane, LFX_I_EVERY); return e ? 100 + e : lfx_get(t, lx.lane, LFX_I_CHANCE); }

static int lx_here(uint32_t held)                       /* where a held lane key opens the page */
{
    return is_drum(TSEL) && !master_sel() && !name_on() && lay_now() == LY_PLAY && !keys_none() &&
           !(held & 0x3FFFu) && ui.scr != SCR_TEMPO && ui.scr != SCR_SONG;   /* (STEP too: its keys stay steps, lx_step_key) */
}
static void lx_open(void)
{
    if (lx.on)
        return;
    lx.on = 1;
    lx.prev = ui.scr;
    ui.scr = SCR_LFX;
    ui.row[SCR_LFX] = 0;
    ui.hot = 0;
    ui.hot_lit = 0;
    ui.force = 1;
}
static void lx_undo_end(void)
{
    if (lx.sess)
        undo_end(lx.sess);
    lx.sess = 0;
}
static void lx_close(void)
{
    lx_undo_end();
    if (!lx.on)
        return;
    lx.on = 0;
    if (ui.scr == SCR_LFX)
        ui.scr = lx.prev != SCR_LFX ? lx.prev : (uint8_t)SCR_HOME;
    ui.hot = 0;
    ui.hot_lit = 0;
    ui.force = 1;
}
/* the page opened from STEP keeps STEP's keys steps (op_input.c: ly_lock LY_STEP); a step key pressed (on the drum track the
 * white keys are steps on every page: seq.c), or the lane key let go, closes it first, so the gestures go on as before (a lane key held + a step: that lane's level; the user,
 * 2026-10-26: "should work everywhere we have the drum step seq on the keys") */
static int lx_on_step(void) { return lx.on && lx.prev == SCR_STEP; }
static void lx_step_key(uint32_t k, uint32_t down)
{
    if (lx.on && (down ? (int8_t)k != lx.key : (int8_t)k == lx.key)) {   /* (any page: the white keys are steps there too) */
        lx.key = -1;
        lx_close();
    }
}
/* once a frame (op_input.c): one lane key held alone, past the hold time: the page; let go: closed */
static void lx_frame(uint32_t held)
{
    uint32_t n = fm1_in.notes, k;
    if (lx.sess && fm1_ms - lx.turn_ms >= LX_UNDO_MS)
        lx_undo_end();                                  /* (the knobs at rest: the gesture is one undo level) */
    if (lx.on && !is_drum(TSEL)) {                      /* (ALGORITHM walked off the drum track: no lane to edit) */
        lx.key = -1;
        lx_close();
        return;
    }
    if (lx.key >= 0 && !((n >> lx.key) & 1u)) {
        lx.key = -1;
        lx_close();
    }
    if (lx.on || !n || (n & (n - 1u)))                  /* (open, nothing held, or a chord: no new hold) */
        return;
    for (k = 0; !((n >> k) & 1u); k++)
        ;
    if (!lane_key(k) || lane_of(k) >= LFX_LANES || !lx_here(held)) {
        lx.key = -1;
        return;
    }
    if (lx.key != (int8_t)k) {
        lx.key = (int8_t)k;
        lx.lane = (uint8_t)lane_of(k);
        lx.t0 = fm1_ms;
        return;
    }
    if (fm1_ms - lx.t0 >= op_hold_ms())
        lx_open();
}

static uint32_t lx_rows(void) { return LXR_N; }
static void lx_name(uint32_t r, char *b) { str_cpy(b, r ? "LANE FX 2" : "LANE FX", 12); }   /* (the badge: the lane) */
static void lx_cell(uint32_t r, uint32_t k, cell_t *c)
{
    static const char *const LBL[LFX_N] = {"ROLL", "NUDGE", "FILL", "CHANCE", "VRAND", "FLAM", "RAMP", ""};
    uint32_t i;
    int32_t v;
    cell_clear(c);
    if (r >= LXR_N || k >= 4u)
        return;
    i = LX_CARD[r][k];
    if (i >= LFX_N)
        return;
    if (i == LFX_I_CHANCE) {
        chc_cell(c, lx_chc(tsel()));
        return;
    }
    v = ui_clamp(lfx_get(tsel(), lx.lane, i), LFX_R[i / 4u][i % 4u][0], LFX_R[i / 4u][i % 4u][1]);
    c->kind = CK_VAL;
    if (i == LFX_I_ROLL)
        cell_list(c, LBL[i], LX_ROLL[v], (int32_t)LFX_ROLL_N, v);
    else if (i == LFX_I_FILL)
        cell_list(c, LBL[i], LX_FILL[v], 2, v);
    else if (i == LFX_I_FLAM && !v)
        cell_list(c, LBL[i], "OFF", 8, 0);
    else {
        c->label = LBL[i];
        c->unit = i == LFX_I_RAMP ? "%" : i == LFX_I_FLAM ? "ms" : "";
        fmt_int(c->val, i == LFX_I_FLAM ? v * LFX_FLAM_MS : v);
        cell_gauge(c, 0, LFX_R[i / 4u][i % 4u][0], LFX_R[i / 4u][i % 4u][1], v);
    }
}
static void lx_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    track_t *t = tsel();
    uint32_t i;
    int32_t v, d;
    int ok;
    if (r >= LXR_N || k >= 4u)
        return;
    i = LX_CARD[r][k];
    if (i >= LFX_N)
        return;
    d = i == LFX_I_RAMP && !fine ? 5 : i == LFX_I_VRND && !fine ? 2 : 1;   /* (a detent: 5 %, 2 levels) */
    v = i == LFX_I_CHANCE ? chc_turn(lx_chc(t), s, fine)
      : s == OP_RESET ? LFX_R[i / 4u][i % 4u][2]
                      : ui_clamp(lfx_get(t, lx.lane, i) + s * d, LFX_R[i / 4u][i % 4u][0], LFX_R[i / 4u][i % 4u][1]);
    if (!lx.sess)
        lx.sess = (undo_sess += 4u) | 3u;
    undo_mark(t, lx.sess);                              /* (the gesture's first turn: the pattern as it was) */
    lx.turn_ms = fm1_ms;
    fm1_irq_off();
    if (i == LFX_I_CHANCE)                              /* (past 100: EVERY, the chance 100 %) */
        ok = lfx_set(t, lx.lane, LFX_I_CHANCE, v > 100 ? 100 : v) && lfx_set(t, lx.lane, LFX_I_EVERY, v > 100 ? v - 100 : 0);
    else
        ok = lfx_set(t, lx.lane, i, v);
    fm1_irq_on();
    if (!ok)
        ui_message("AUTOMATION FULL");
}
static int lx_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    (void)r, (void)k, (void)ok;
    return 0;
}
#else
static int lx_on_step(void) { return 0; }
static void lx_step_key(uint32_t k, uint32_t down) { (void)k, (void)down; }
static void lx_undo_end(void) {}
static void lx_frame(uint32_t held) { (void)held; }
static void lx_close(void) {}
static uint32_t lx_rows(void) { return 1u; }           /* (never opened in this build: one empty row, as every screen has) */
static void lx_name(uint32_t r, char *b) { (void)r, str_cpy(b, "LANE FX", 12); }
static void lx_cell(uint32_t r, uint32_t k, cell_t *c) { (void)r, (void)k, cell_clear(c); }
static void lx_turn(uint32_t r, uint32_t k, int32_t s, int fine) { (void)r, (void)k, (void)s, (void)fine; }
static int lx_yes(uint32_t r, uint32_t k, uint32_t ok) { (void)r, (void)k, (void)ok; return 0; }
#endif
