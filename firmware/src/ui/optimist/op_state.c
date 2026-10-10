/* SPDX-License-Identifier: GPL-3.0-only */
/* The Optimist UI's state, its messages and its one confirm idiom (docs/UI-OPTIMIST-DESIGN.md sections 2, 2.1).
 * Messages stay as SLOOP's: the header, a fixed part and a variable part, coloured by the words of the fixed part;
 * ~2.5 s. The confirm: a destructive action arms, the header asks "CLEAR T2? YES", YES (SAVE tapped) does it, NO
 * (HOME tapped), another cursor row or 3 s let it go. Every confirm of the UI is this one; there is no "AGAIN". */
enum { SCR_HOME, SCR_SOUND, SCR_FX, SCR_PROJECT, SCR_SYSTEM, SCR_STEP, SCR_SONG, SCR_TEMPO, SCR_SCOPE, SCR_N };
static const char *const SCR_NAME[SCR_N] = {"MIX", "SOUND", "FX", "PROJECT", "SYSTEM", "STEPS", "SONG", "TEMPO",
                                            "SCOPE"};
#define OP_MSG_FRAMES 150u            /* ~2.5 s at the UI's 60 frames a second (main.c paces them at 15 ms) */
#define OP_ARM_MS 3000u               /* an armed action waits 3 s for its YES */
#define ARM_NONE 0xFFu
#define ARM_TRACK 0xFEu               /* arm_scr: HOME + REC, clear track arm_k (no screen cell) */
#define ARM_OP 0xFDu                  /* arm_scr: an action of no screen cell, arm_row its OA_*, arm_k its argument */
enum { OA_SCN_STORE, OA_SCN_CLR, OA_PAT_STORE, OA_PAT_CLR, OA_PAT_COPY, OA_INIT, OA_EXTRAS };

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
    uint8_t row[SCR_N];               /* each screen's cursor row */
    uint8_t hot;                      /* the hot cell of the cursor row: the one PRESETS and YES act on */
    uint8_t hot_lit;                  /* drawn white: a knob or PRESETS touched it since the row was picked */
    uint8_t shift;                    /* SHIFT: the LFO button held this frame (op_input.c op_knobs); a cell reads it
                                       * for its fine path (LEN: by one instead of 1 2 4 ... 64, op_cells.c) */
    uint8_t arm_scr, arm_row, arm_k;  /* the armed action: a cell (screen, row, cell) or ARM_TRACK */
    uint32_t arm_ms;
    char arm_q[26];                   /* its question, "CLEAR T2?" ("FORGET " + a 16-character name + "?") */
    char arm_verb[12], arm_arg[17];   /* the modal's two lines: "CLEAR?" and its target, "T2" (BLE_NAME_MAX + 1) */
    uint8_t arm_raw;                  /* the target is a name as it was given (a BLE device): never recased */
    uint8_t arm_danger;               /* it destroys something: a red frame (else amber) */
    uint8_t toast_t;                  /* frames the toast stays: the result of an action just confirmed */
    uint16_t toast_col;               /* its frame's colour (0: its words' status colour; a preset: its engine's) */
    uint8_t toast_next;               /* (a confirmed action runs: what it says is a toast, not the header) */
    uint8_t overlay;                  /* what covers the panel now: 0 nothing, 1 the modal, 2 the toast */
    /* the panel's buttons: SAVE and HOME act on release when nothing else was pressed meanwhile */
    uint8_t home_used, save_used;
    uint32_t enc_t[NE];               /* the knobs' last detents (acceleration) */
    uint32_t sig[5];                  /* what each band drew last: header, cards, panel, footer, overlay */
    uint8_t snap_slot, user_slot;     /* PROJECT: the snapshot and user preset slots */
} ui = {.page = 0, .arm_scr = ARM_NONE};

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
    uint8_t prev_rec, prev_wait;        /* song.rec, rec_wait before the press */
    uint32_t t0, t1;                    /* the press, the ring's start */
} rh;

/* SYSTEM > SCREEN > CARDS (the user, 2026-10-08): how the cursor row's four values are shown, 0 the four cards in a
 * line, 1 SLOOP 2.4's big values 2 x 2 as the knobs sit (op_draw.c draw_big); the panel under them shrinks. Kept
 * in the settings word, bit 23 (storage/settings_word.c) */
enum { CARDS_LINE, CARDS_2X2, CARDS_N };
static uint8_t op_cards = CARDS_LINE;
static int cards_2x2(void) { return op_cards == CARDS_2X2 && ui.scr != SCR_HOME; }   /* (the mixer: 1x4 always, its rows want the height) */

/* SYSTEM > CALIBRATE > HOLD (the user, 2026-10-08, on the FM-1: a click showed the layer's map one time out of two at
 * SLOOP's 140 ms): a button held this long is a hold (a layer's map, PLAY's TEMPO page); a page button let go within
 * HOLD + OP_TAP_GAP still counts as a tap (a slow click). The setting is main's shared one (core/hold.h hold_sel: 0 350 ms,
 * 1 250, 2 500; the settings word's bits 21..22), the same as the HOME menu's */
static const uint8_t OP_HOLD_POS[HOLD_N] = {1, 0, 2}, OP_HOLD_SEL[HOLD_N] = {1, 0, 2};   /* (the knob's order 250 350 500) */
#define OP_TAP_GAP 150u
static uint32_t op_hold_ms(void) { return HOLD_MS; }

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
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
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
                                    "ARP", "SCL", "UI", "CC", "ACID", "GEN", "MHZ", "BLE"};
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
/* a card's label: 5 characters or fewer stay as a panel legend (ATK, BPM, FILT); longer ones in sentence case */
static const char *op_label(char *b, const char *s, uint32_t n) { return str_len(s) <= 5u ? s : op_case(b, s, n); }

/* "T1".."T3", "DR": a track's short name */
static const char *trk_tag(uint32_t i)
{
    static const char *const T[NTRK] = {"T1", "T2", "T3", "DR"};
    return T[i % NTRK];
}

/* ---- the confirm */
static void op_disarm(void)
{
    ui.arm_scr = ARM_NONE;
}
/* arm what YES will do: the modal asks q + "?" in big type and names arg, its target ("CLEAR?", "T2"); the
 * question as one line is q, arg and "?" ("CLEAR T2?"; no arg: "NEW?"). danger: it destroys or replaces the work */
static void op_arm(uint32_t scr, uint32_t row, uint32_t k, const char *q, const char *arg, uint32_t danger)
{
    uint32_t n;
    str_cpy(ui.arm_verb, q, sizeof ui.arm_verb - 1u);
    str_cpy(ui.arm_verb + str_len(ui.arm_verb), "?", 2);
    str_cpy(ui.arm_arg, arg, sizeof ui.arm_arg);
    ui.arm_danger = (uint8_t)(danger != 0u);
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
