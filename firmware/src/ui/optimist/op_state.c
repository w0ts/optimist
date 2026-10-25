/* SPDX-License-Identifier: GPL-3.0-only */
/* The Optimist UI's state, its messages and its one confirm idiom (docs/UI-OPTIMIST-DESIGN.md 2.2 Taps, holds, YES and NO; 2.5 Questions, toasts and what asks).
 * Messages stay as SLOOP's: the header, a fixed part and a variable part, coloured by the words of the fixed part;
 * ~2.5 s. The confirm: a destructive action arms, the header asks "CLEAR T2? YES", YES (SAVE tapped) does it, NO
 * (HOME tapped), another cursor row or 3 s let it go. Every confirm of the UI is this one; there is no "AGAIN". */
/* the cursor row's background: the line's colour at full strength (the user, 2026-10-14: the dark tint was not
 * recognisable); the row's text and bars are its ink, black or white, whichever reads better on it (gfx.c ink_on; the
 * user, 2026-10-18: white on a light colour was hard to read), the bars on a trough of the colour away from the ink:
 * 3/8 under white, 5/8 under black (docs/UI-OPTIMIST-DESIGN.md 3.1 Cards and lists) */
static uint16_t cur_bg(uint16_t c) { return c; }
static uint16_t cur_ink(uint16_t c) { return ink_on(cur_bg(c)); }
static uint16_t cur_trough(uint16_t c) { return col_shade(c, cur_ink(c) == C_WHITE ? 3u : 5u); }
static void str_app(char *d, const char *s, unsigned max)   /* s appended to d (a buffer of max), as str_cpy */
{
    unsigned n = str_len(d);
    str_cpy(d + n, s, max - n);
}
static void fmt_app(char *d, int32_t v) { fmt_int(d + str_len(d), v); }   /* v's digits appended to d */
static void pk_digits(char d[7], uint32_t v)        /* a pairing code: its 6 digits, leading zeros kept */
{
    int32_t k;
    for (k = 5; k >= 0; k--, v /= 10u)
        d[k] = (char)('0' + v % 10u);
    d[6] = 0;
}
/* where a list's window is: a line over len px from top, the window's part of it in grey (first, shown of n rows) */
static void scroll_bar(int32_t top, int32_t len, int32_t first, int32_t shown, int32_t n)
{
    cv_rect(238, top, 1, len, C_LINE);
    cv_rect(237, top + len * first / n, 3, len * shown / n, C_GRAY);
}
/* the UI's clamp: a call (dsp_common.h's is inlined for the sound; at 66 UI sites the copies cost more than calls) */
static __attribute__((noinline)) int32_t ui_clamp(int32_t v, int32_t lo, int32_t hi) { return clamp(v, lo, hi); }
static __attribute__((noinline)) int32_t step_in(int32_t v, int32_t s, int32_t n)   /* v moved by s, within 0..n-1 */
{
    return clamp(v + s, 0, n - 1);
}
/* text in the small face: the drawing files' most frequent calls, their arguments in registers (calls, never inlined:
 * cv_text's fifth argument and the face at every call cost more); the end x, the width, centred on the screen */
static __attribute__((noinline)) int32_t cv_texts(int32_t x, int32_t y, const char *s, uint16_t c)
{
    return cv_text(x, y, &FONT_S, s, c);
}
static __attribute__((noinline)) int32_t text_ws(const char *s) { return text_w(&FONT_S, s); }
static __attribute__((noinline)) void cv_text_mid(int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(120 - text_w(f, s) / 2, y, f, s, c);
}
enum { SCR_HOME, SCR_SOUND, SCR_FX, SCR_PROJECT, SCR_SYSTEM, SCR_STEP, SCR_SONG, SCR_TEMPO, SCR_SCOPE, SCR_GLOBAL, SCR_N };
static const char *const SCR_NAME[SCR_N] = {"MIX", "SOUND", "MASTER", "PROJECT", "SYSTEM", "STEPS", "SONG", "TEMPO",
                                            "SCOPE", "GLOBAL"};
#define OP_MSG_FRAMES 150u            /* ~2.5 s at the UI's 60 frames a second (main.c paces them at 15 ms) */
#define OP_ARM_MS 3000u               /* an armed action waits 3 s for its YES */
#define ARM_NONE 0xFFu
#define ARM_OP 0xFDu                  /* arm_scr: an action of no screen cell, arm_row its OA_*, arm_k its argument */
enum { OA_SCN_STORE, OA_SCN_CLR, OA_PAT_STORE, OA_PAT_CLR, OA_INIT, OA_EXTRAS, OA_DISCARD, OA_SREC,
       OA_SCN_LOAD, OA_PART_CUT, OA_DROP };

static struct {
    /* read by the core under SLOOP's names: project.c (menu: autosave waits), main.c (force, page: the crash
     * breadcrumb), editor.c and the stores (force), miss.c (msg, msg_t, layer, hold_kind) */
    uint8_t force;                    /* everything redraws */
    uint8_t menu;                     /* always 0: this UI has no modal menu */
    uint8_t page;                     /* the PAGES entry of the cursor row (TRACKS when the row is no page) */
    uint8_t layer;                    /* the performance layer shown (op_layers.c), LY_PLAY none */
    uint8_t hold_kind;                /* 0: no hold screen */
    uint8_t msg_t;                    /* frames the message stays */
    uint8_t msg_st;                   /* its status colour: 0 the palette, 1 ok, 2 notice, 3 error */
    char msg[30];                     /* (29 characters: the header's width) */
    /* the grammar */
    uint8_t scr;                      /* the screen shown (SCR_*) */
    uint8_t master;                   /* MASTER selected in a layer (op_layers.c): a UI state, song.sel stays T1; on SOUND
                                       * it is the FX screen (SCR_FX), which m_fam / m_row lead back from */
    uint8_t m_scr, m_fam, m_row;      /* the page, SOUND family and row ALGORITHM left for MASTER (SND_ALL: every row) */
    uint8_t lane;                     /* the drum track's selection is its lane lane_selected(), not DR itself: ALGORITHM's
                                       * walk past DR, the pick, a lane row (op_walk.c lane_picked; the mixer: its row) */
    uint8_t row[SCR_N];               /* each screen's cursor row */
    uint8_t hot;                      /* the hot cell of the cursor row: the one PRESETS and YES act on */
    uint8_t hot_lit;                  /* drawn white: a knob or PRESETS touched it since the row was picked */
    uint8_t shift;                    /* SHIFT: the LFO button held this frame (op_input.c op_knobs); a cell reads it
                                       * for its fine path (LEN: by one instead of 1 2 4 ... 64, op_cells.c) */
    uint8_t arm_scr, arm_row, arm_k;  /* the armed action: a cell (screen, row, cell) or ARM_OP */
    uint32_t arm_ms;
    char arm_q[26];                   /* its question, "CLEAR T2?" ("FORGET " + a 16-character name + "?") */
    char arm_verb[12], arm_arg[17];   /* the modal's two lines: "CLEAR?" and its target, "T2" (BLE_NAME_MAX + 1) */
    uint8_t arm_raw;                  /* the target is a name as it was given (a BLE device): never recased */
    uint8_t arm_danger;               /* it destroys something: a red frame (else amber) */
    uint8_t arm_ring;                 /* a delete asked while its key or button is held: the ring fills (the hold confirms, letting go cancels), not the modal */
    uint8_t toast_t;                  /* frames the toast stays: the result of an action just confirmed */
    uint16_t cards_t;                 /* STEP: frames the four cards stay over the grid (a page change, a value edited: ~1.5 s,
                                       * ~30 s after SHIFT2 + SELECT until STEP is left; a held step let go: ~1.5 s) */
    uint16_t toast_col;               /* its frame's colour (0: its words' status colour; a preset: its engine's) */
    uint8_t toast_next;               /* (a confirmed action runs: what it says is a toast, not the header) */
    uint8_t overlay;                  /* what covers the panel now: 0 nothing, 1 the modal, 2 the toast */
    /* the panel's buttons: SAVE and HOME act on release when nothing else was pressed meanwhile */
    uint8_t home_used, save_used;
    uint32_t enc_t[NE];               /* the knobs' last detents (acceleration) */
    uint32_t sig[5 + FELUCCA_PARAM_HELP];   /* what each band drew last: header, cards, panel, footer, overlay (the help line) */
    uint8_t snap_slot, user_slot;     /* PROJECT: the snapshot and user preset slots */
} ui = {.page = 0, .arm_scr = ARM_NONE, .m_scr = SCR_SOUND, .m_fam = 0xFFu};

/* SHIFT and SHIFT2 (the user, 2026-10-14): SHIFT is OCT- held, SHIFT2 is OCT+ held (HOME is only HOME / NO now). What HOME held
 * did (knob to default, drum lane pick, button clears, the layer lock, PRESETS the engine) is SHIFT's. Their taps change the
 * octave when let go, and only if nothing else was used while they were down (op_input.c op_tap). `held` = fm1_in.buttons */
static int op_shift(uint32_t held) { return (int)((held >> panel.btn[B_OCTDN]) & 1u); }
static int op_shift2(uint32_t held) { return (int)((held >> panel.btn[B_OCTUP]) & 1u); }
/* the buttons let go during this UI frame (op_input.c ui_input): still held for the frame's keys, so a chord whose key
 * press and button release land in one frame (a brisk SAVE + key on the device, its frames slower than the test's) is
 * the chord, not a tap (the user, 2026-10-23: SAVE + key in the pattern layer opened PROJECT) */
static uint32_t op_frame_up;
static int btn_held(uint32_t b) { return (int)(((fm1_in.buttons | op_frame_up) >> panel.btn[b]) & 1u); }   /* button b down now (or let go this frame) */
static __attribute__((noinline)) track_t *tsel(void) { return TSEL; }   /* the selected track (a call: TSEL inlined is a multiply each time) */

/* REC held: clear the selected track (the user, 2026-10-08: "I really like the long press of SLOOP to delete a
 * track... Just use it"; ui/sloop/ui_input.c holds_input, its timing copied). REC acts on its press as ever; held
 * RH_ARM_MS that press is undone and a ring fills (op_draw.c draw_ring); held RH_CLEAR_MS more, the track is cleared
 * (undoable); let go before, nothing. Only a plain arm / disarm press becomes a hold (a free take closed, SONG's
 * store, "stop the song first": no ring) */
#define RH_ARM_MS 700u
#define RH_CLEAR_MS 1300u
static struct {
    uint8_t on;                         /* REC down since a plain arm / disarm press */
    uint8_t ring;                       /* the ring filling (the press undone) */
    uint8_t trk;                        /* the track it clears */
    int8_t lane;                        /* a drum lane picked when the ring began: only that lane is cleared (-1: the track) */
    uint8_t prev_rec, prev_wait;        /* song.rec, rec_wait before the press */
    uint8_t knob;                       /* a knob turned while REC is down: the press undone, no ring until REC is up */
    uint8_t motion;                     /* SHIFT + REC: the ring clears the motion and locks, the notes stay */
    uint8_t kdone;                      /* the knobs whose parameter this hold cleared (bit k: once a knob a hold) */
    uint32_t t0, t1;                    /* the press, the ring's start */
} rh;

/* The cards (the user, 2026-10-12): the cursor row's four values are SLOOP 2.4's big values, 2 x 2 as the knobs sit
 * (op_draw.c draw_big), on every screen with cards but the mixer, which keeps its compact 1x4 cards (op_layers.c
 * cards_2x2). Under them the screen's picture alone, or its rows when it has none (op_draw.c draw_list). The SYSTEM >
 * SCREEN > CARDS choice (1x4 or 2x2) is gone; its settings word bit 23 is no longer read (storage/settings_word.c) */
static int cards_2x2(void);                             /* op_layers.c (it reads the layer shown) */
static int lane_picked(void);                               /* op_mixer.c: a drum lane selected, not DR itself */
static int algo_has_track(void);                        /* op_input.c: the pages ALGORITHM walks on */
static void algo_walk_trk(int32_t s);                    /* op_walk.c: the PATTERNS layer's walk, T1..DR */
static void algo_walk(int32_t s, int lay);              /* op_walk.c: ALGORITHM, on every page and layer */
static void lane_sound_step(int32_t s);                 /* op_walk.c: PRESETS on a lane, its own sound */
static int lane_presets(void);
#if FELUCCA_FAVORITES
static int op_fav_here(void);                           /* op_screens.c: FAVORITES, marked and browsed where presets are picked (PRESETS) */
static void op_fav_mark(int on);
static void op_fav_list(uint32_t on);
static void pre_toast_fav(const char *what);            /* op_preset.c */
#endif
static void lane_kind_step(int32_t s);
static const char *lane_sound_name(uint32_t l);

/* The drum track's ACCENT lane (core.h DRUM_ACC, lane 10) as this UI shows it: a lane as the others (a row in the grid,
 * the mixer and the walk, a key, a name), with no sound. lane_selected() is 0..DRUM_UILANES - 1; the per-lane SOUND
 * arrays (dl.*, dsend, the instruments' names) only know the DRUM_LANES sounds, so every site that indexes them asks
 * lane_snd() first. The lanes' keys are the black keys (drums.c lane_of_key, 2026-10-16: they select and play the
 * lane, the white keys are steps); the last upper key (25) is the ACCENT lane's where the build has it (DACC_KEY_ON) */
#define ACC_UI (FELUCCA_DRUM_ACCENT && DACC_KEY_ON)
static int lane_acc(void) { return ACC_UI && lane_selected() == DRUM_ACC; }   /* the accent lane is the selected one */
static int lane_snd(void) { return !lane_acc(); }                              /* the selected lane has a sound */
static int lane_key(uint32_t k)                         /* key k plays / picks a lane: a lane's black key and the accent key */
{
    return lane_of_key(k) < DRUM_LANES || (ACC_UI && k == DACC_KEY);
}
static uint32_t lane_of(uint32_t k) { return ACC_UI && k == DACC_KEY ? DRUM_ACC : lane_of_key(k); }   /* (lane_key keys only) */
static const char *lane_name(uint32_t l) { return l == DRUM_ACC ? DACC_NAME : LANE_NAME_OF(l % DRUM_LANES); }
static const char *lane_short(uint32_t l) { return l == DRUM_ACC ? DACC_SHORT : LANE_SHORT_OF(l % DRUM_LANES); }
static const char *lane_code(uint32_t l) { return l == DRUM_ACC ? DACC_CODE : LANE_CODE_OF(l % DRUM_LANES); }
static uint16_t op_lane_col(uint32_t l) { return l == DRUM_ACC ? C_HI : lane_col(l); }   /* (the accent: the bright tone, no kit kind) */
/* does step `step` carry automation (a p-lock or motion event of the sound; the step extras NUDGE / FILL / CHANCE / COND are not)? On a
 * drum track a lane's event marks that lane's cell only (lane 16: the ACCENT lane, the track's own events only; 0xFF: any lane, the drum row of the mixer), a track's event every cell of the step */
static uint32_t op_step_auto(const track_t *t, uint32_t step, uint32_t lane)
{
#if FELUCCA_AUTO
    const auto_list_t *l = AL(t);
    uint32_t i;
    for (i = 0; i < l->n; i++) {
        const auto_ev_t *e = &l->ev[i];
        if ((e->place & AUTO_STEP) != (step & AUTO_STEP) || AUTO_PSEUDO(e->param))
            continue;
        if (!AUTO_IS_LANE(e->param) || AUTO_LANE_L(e->param) == lane || lane == 0xFFu)
            return 1;
    }
#else
    (void)t, (void)step, (void)lane;
#endif
    return 0;
}

/* SYSTEM > CALIBRATE > HOLD (the user, 2026-10-08, on the FM-1: a click showed the layer's map one time out of two at
 * SLOOP's 140 ms): a button held this long is a hold (a layer's map, PLAY's TEMPO page); a page button let go within
 * HOLD + OP_TAP_GAP still counts as a tap (a slow click). The setting is main's shared one (core/hold.h hold_sel: 0 350 ms,
 * 1 250, 2 500; the settings word's bits 21..22), the same as the HOME menu's */
static const uint8_t OP_HOLD_POS[HOLD_N] = {1, 0, 2}, OP_HOLD_SEL[HOLD_N] = {1, 0, 2};   /* (the knob's order 250 350 500) */
#define OP_TAP_GAP 150u
static uint32_t op_hold_ms(void) { return HOLD_MS; }
static uint32_t op_t0[14];                              /* each button's press (fm1_ms), by panel id (op_input.c; op_combos.c: HOME's tap) */
static uint32_t lk_ev_ms;                               /* fm1_ms of the key event being handled (op_drain: its queued time, seq.c lk_t) */
/* the buttons' own times (the user, 2026-10-22: a slow frame turned taps into holds): the HAL stamps each press and release as
 * the scan makes it (hal/fm1_input.h fm1_btn_t, timer ticks); op_btn_ms gives it in fm1_ms. Read for this pass's edges only: an
 * edge is never before the last pass (op_pass.gap), so a stamp older than that (a button set by hand, a test) reads as then */
static struct { uint32_t ms, t, gap; } op_pass;         /* this pass's fm1_ms and timer, the time since the last (op_input.c ui_input) */
static __attribute__((noinline)) uint32_t op_btn_ms(uint32_t id, uint32_t up)   /* button id (panel id 0..13) went up (1) / down (0): fm1_ms */
{
    uint32_t d = op_pass.t - fm1_btn_t[up & 1u][id % 14u], ago = (int32_t)d < 0 ? 0u : d / (FM1_TICKS_PER_US * 1000u);
    return op_pass.ms - (ago < op_pass.gap ? ago : op_pass.gap);
}
static __attribute__((noinline)) uint32_t op_btn_span(uint32_t id, uint32_t t0)  /* button id let go this pass, pressed at t0: how long it was down (ms) */
{
    uint32_t d = op_btn_ms(id, 1) - t0;
    return (int32_t)d > 0 ? d : 0u;
}

static const page_t *cur_page(void) { return &PAGES[ui.page % NPAGES]; }   /* (miss.c: TOOLS > MISS) */

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam)
            return i;
    return 0;
}

/* a message's status colour by the words of its fixed part (as SLOOP's ui.c msg_status, tools/colors.json) */
static uint32_t msg_status(const char *a)
{
    static const char *const W[] = {"ERROR", "FULL", "STOP", "EMPTY", "MISSING", "NO ", "LOCKED", "RAM", "BUSY",
                                    "SAVED", "STORED", "LOADED", "SENT", "CLEARED"};
    static const uint8_t S[] = {3, 3, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1};
    uint32_t i, j, k;
    for (i = 0; i < sizeof S; i++)
        for (j = 0; a[j]; j++) {
            for (k = 0; W[i][k] && a[j + k] == W[i][k]; k++)
                ;
            if (!W[i][k])
                return S[i];
        }
    return 0;
}

/* a message: in the header (passive status: MISSING, RECORDING), or a toast in the middle when it is the result
 * of an action the user just confirmed (op_input.c op_yes sets toast_next around it) */
#define OP_TOAST_FRAMES 90u           /* ~1.5 s */
#define OP_CARDS_FRAMES 90u           /* STEP's card overlay: the same ~1.5 s, restarted by every page change or edit */
#define OP_CARDS_LONG 1800u           /* ~30 s instead, for the page's own cards (no step held) once SHIFT2 + SELECT was
                                       * turned on STEP, until STEP is left (the user, 2026-10-23: GRIDS' X / Y turned
                                       * while listening; "long if right shift + select turn", "not the plock") */
static uint8_t cards_snap_use;                          /* STEP: the cards show what was held (op_stepdraw.c step_frame) */
static uint8_t cards_long;                              /* STEP: the long time is on (op_enter clears it) */
static void step_cards_show(void);                      /* (op_step.c: which of the two times is the step state's) */
/* another track (ALGORITHM, the mixer's walk): every band drawn again over itself, in place. Not ui.force: that
 * clears the whole LCD to black first and the bands follow one blit at a time, a black flash on every detent (the
 * user, 2026-10-10: "while scrolling algo knob, the screen flash in a not nice way"). The layout is the screen's,
 * the same for every track, so the gaps between the bands stay as they are */
static void mx_redraw(void);
static void op_repaint(void)
{
    memset(ui.sig, 0, sizeof ui.sig);
    mx_redraw();
}
static uint8_t wipe_req;                               /* SHIFT2 + REC pressed: the wipe is wanted while both stay held (op_input.c wipe_frame) */
static uint8_t pop_kind;                                /* the toast is a popup list of the sounds (op_preset.c): 1 the presets, 2 the drum kits, 3 a card's list value; any message clears it */
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    pop_kind = 0;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_st = (uint8_t)msg_status(a);
    if (ui.toast_next) {
        ui.toast_t = OP_TOAST_FRAMES;
        ui.toast_col = 0;
        ui.msg_t = 0;
    } else {
        ui.msg_t = OP_MSG_FRAMES;
    }
}
static void ui_message(const char *s) { ui_say(s, ""); }
static void ui_say_st(uint32_t st, const char *a, const char *b)
{
    ui_say(a, b);
    ui.msg_st = (uint8_t)(st & 3u);
}

/* Sentence case at draw time (the user, 2026-10-08: "sentence case for UI words, capitals for short labels"): s
 * into b (b holds n), its first letter kept, the rest lowercased, except words that stay as printed legends: the
 * acronyms below, any word with a digit (T1, 2.4, U03) and a letter alone or a note (scene B, key C#). The tables stay in capitals (core/params.c, the core's
 * messages, what SLOOP's UI and the web editor read); msg_status reads the stored capitals. */
static int case_keep(const char *w, uint32_t len)
{
    static const char *const K[] = {"DR", "FX", "LFO", "ENV", "ENV2", "MIDI", "USB", "BPM", "CPU", "OCT", "GLO",
                                    "ARP", "SCL", "UI", "CC", "ACID", "GEN", "MHZ", "BLE", "BT"};
    uint32_t i, j;
    while (len && !((w[len - 1u] >= 'A' && w[len - 1u] <= 'Z') || (w[len - 1u] >= '0' && w[len - 1u] <= '9')))
        len--;                                          /* (the word without its "?", ":" or ",": "DR?" is DR) */
    for (j = 0; j < len; j++)
        if (w[j] >= '0' && w[j] <= '9')
            return 1;
    if (len == 1u || (len == 2u && w[1] == '#'))
        return 1;                                       /* a scene, a part, a note: "Scene B", "Key C#" */
    for (i = 0; i < sizeof K / sizeof K[0]; i++) {
        for (j = 0; j < len && K[i][j] == w[j]; j++)
            ;
        if (j == len && !K[i][j])
            return 1;
    }
    return 0;
}
static const char *op_case(char *b, const char *s, uint32_t n)
{
    uint32_t i = 0, w, len;
    while (s[i] && i + 1u < n) {
        for (len = 0; s[i + len] && s[i + len] != ' ' && s[i + len] != '/'; len++)
            ;
        for (w = 0; w < len && i + 1u < n; w++, i++)    /* a word: kept, or lowered past the sentence's first letter */
            b[i] = (char)(s[i] >= 'A' && s[i] <= 'Z' && i && !case_keep(s + i - w, len) ? s[i] + 32 : s[i]);
        if (s[i] && i + 1u < n) {                       /* the separator */
            b[i] = s[i];
            i++;
        }
    }
    b[i] = 0;
    return b;
}

/* "T1".."T3", "DR": a track's short name */
static const char *trk_tag(uint32_t i)
{
    static const char *const T[NTRK] = {"T1", "T2", "T3", "DR"};
    return T[i % NTRK];
}

#define MODAL_W 208                     /* the confirm's text room inside the box (224 px framed, 8 px margins) */
/* ---- the confirm */
static void op_disarm(void)
{
    ui.arm_scr = ARM_NONE;
    ui.arm_ring = 0;
}
/* arm what YES will do: the modal asks q + "?" in big type and names arg, its target ("CLEAR?", "T2"); the
 * question as one line is q, arg and "?" ("CLEAR T2?"; no arg: "NEW?"). danger: it destroys or replaces the work */
static void op_arm(uint32_t scr, uint32_t row, uint32_t k, const char *q, const char *arg, uint32_t danger)
{
    uint32_t n;
    str_cpy(ui.arm_verb, q, sizeof ui.arm_verb - 1u);
    str_app(ui.arm_verb, "?", sizeof ui.arm_verb);
    str_cpy(ui.arm_arg, arg, sizeof ui.arm_arg);
    ui.arm_danger = (uint8_t)(danger != 0u);
    ui.arm_ring = 0;
    ui.arm_raw = 0;
    ui.toast_t = 0;
    ui.arm_scr = (uint8_t)scr;
    ui.arm_row = (uint8_t)row;
    ui.arm_k = (uint8_t)k;
    ui.arm_ms = fm1_ms;
    str_cpy(ui.arm_q, q, sizeof ui.arm_q);
    n = str_len(ui.arm_q);
    if (arg[0] && n + 1u < sizeof ui.arm_q)
        ui.arm_q[n++] = ' ', ui.arm_q[n] = 0;
    str_cpy(ui.arm_q + n, arg, sizeof ui.arm_q - n);
    n = str_len(ui.arm_q);
    str_cpy(ui.arm_q + n, "?", sizeof ui.arm_q - n);
}
/* a delete just asked (op_arm) from a key or button still held: the ring (op_draw.c draw_ring) fills as the hold goes on and
 * confirms it, letting go cancels it (the user, 2026-10-21: "all the deletes use the same circle as the REC button's") */
static void op_arm_hold(void)
{
    ui.arm_ring = (uint8_t)(fm1_in.notes != 0u || fm1_in.buttons != 0u);
}
static int op_armed(void)
{
    if (ui.arm_scr != ARM_NONE && fm1_ms - ui.arm_ms > OP_ARM_MS)
        op_disarm();                                    /* 3 s: let go */
    return ui.arm_scr != ARM_NONE;
}

/* ---- knobs: acceleration by turn speed (knob_accel.h, X0X 61654ba; as SLOOP's ui_input.c accel) */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ticks(), dt = now - ui.enc_t[role];
    ui.enc_t[role] = now;
#if FELUCCA_KNOB_ACCEL
    return knob_accel(dt / (1000u * FM1_TICKS_PER_US), s, range);
#else
    (void)dt;
    (void)range;
    return s;
#endif
}
static int32_t accel_range(const param_desc_t *d)   /* none for lists: one entry a detent */
{
    return d->fmt == F_ENUM || d->fmt == F_ONOFF ? 0 : d->max - d->min;
}
static int32_t k_accel(uint32_t k, int32_t s, const param_desc_t *d) { return accel(EN_K1 + k, s, accel_range(d)); }
static void undo_level(track_t *t) { undo_mark(t, (undo_sess += 4u) | 3u); }   /* an undo level of the UI's */
