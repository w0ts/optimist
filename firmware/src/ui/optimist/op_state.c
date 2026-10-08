/* SPDX-License-Identifier: GPL-3.0-only */
/* The Optimist UI's state, its messages and its one confirm idiom (docs/UI-OPTIMIST-DESIGN.md sections 2, 2.1).
 * Messages stay as SLOOP's: the header, a fixed part and a variable part, coloured by the words of the fixed part;
 * ~2.5 s. The confirm: a destructive action arms, the header asks "CLEAR T2? YES", YES (SAVE tapped) does it, NO
 * (HOME tapped), another cursor row or 3 s let it go. Every confirm of the UI is this one; there is no "AGAIN". */
enum { SCR_HOME, SCR_SOUND, SCR_FX, SCR_PROJECT, SCR_SYSTEM, SCR_N };
static const char *const SCR_NAME[SCR_N] = {"MIX", "SOUND", "FX", "PROJECT", "SYSTEM"};
#define OP_MSG_FRAMES 150u            /* ~2.5 s at the UI's 60 frames a second (main.c paces them at 15 ms) */
#define OP_ARM_MS 3000u               /* an armed action waits 3 s for its YES */
#define ARM_NONE 0xFFu
#define ARM_TRACK 0xFEu               /* arm_scr: HOME + REC, clear track arm_k (no screen cell) */

static struct {
    /* read by the core under SLOOP's names: project.c (menu: autosave waits), main.c (force, page: the crash
     * breadcrumb), editor.c and the stores (force), miss.c (msg, msg_t, layer, hold_kind) */
    uint8_t force;                    /* everything redraws */
    uint8_t menu;                     /* always 0: this UI has no modal menu */
    uint8_t page;                     /* the PAGES entry of the cursor row (TRACKS when the row is no page) */
    uint8_t layer;                    /* LY_PLAY: no held layer in phase 1 */
    uint8_t hold_kind;                /* 0: no hold screen */
    uint8_t msg_t;                    /* frames the message stays */
    uint8_t msg_st;                   /* its status colour: 0 the palette, 1 ok, 2 notice, 3 error */
    char msg[30];                     /* (29 characters: the header's width) */
    /* the grammar */
    uint8_t scr;                      /* the screen shown (SCR_*) */
    uint8_t row[SCR_N];               /* each screen's cursor row */
    uint8_t hot;                      /* the hot cell of the cursor row: the one PRESETS and YES act on */
    uint8_t hot_lit;                  /* drawn white: a knob or PRESETS touched it since the row was picked */
    uint8_t arm_scr, arm_row, arm_k;  /* the armed action: a cell (screen, row, cell) or ARM_TRACK */
    uint32_t arm_ms;
    char arm_q[22];                   /* its question, "CLEAR T2?" */
    /* the panel's buttons: SAVE and HOME act on release when nothing else was pressed meanwhile */
    uint8_t home_used, save_used;
    uint32_t enc_t[NE];               /* the knobs' last detents (acceleration) */
    uint32_t sig[4];                  /* what each band drew last: header, cards, panel, footer */
    uint8_t snap_slot, user_slot;     /* PROJECT: the snapshot and user preset slots */
    uint8_t meter[NTRK];              /* the mixer's meters as drawn */
    uint8_t step_drawn[NTRK];         /* the playheads as drawn: the mixer's strips */
    uint8_t foot_step;                /* and the footer's (the selected track) */
} ui = {.page = 0, .arm_scr = ARM_NONE};

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

static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = OP_MSG_FRAMES;
    ui.msg_st = (uint8_t)msg_status(a);
}
static void ui_message(const char *s) { ui_say(s, ""); }
static void ui_say_st(uint32_t st, const char *a, const char *b)
{
    ui_say(a, b);
    ui.msg_st = (uint8_t)(st & 3u);
}

/* "T1".."T3", "DR": a track's short name */
static const char *trk_tag(uint32_t i)
{
    static const char *const T[NTRK] = {"T1", "T2", "T3", "DR"};
    return T[i % NTRK];
}

/* ---- the confirm */
static void op_disarm(void)
{
    if (ui.arm_scr != ARM_NONE)
        ui.sig[0] = 0;                                  /* (the header asked: it goes) */
    ui.arm_scr = ARM_NONE;
}
/* arm what YES will do: the question is q, arg and "?" ("CLEAR", "T2" -> "CLEAR T2?"; no arg: "NEW?") */
static void op_arm(uint32_t scr, uint32_t row, uint32_t k, const char *q, const char *arg)
{
    uint32_t n;
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
    ui.msg_t = 0;                                       /* (the question takes the header) */
    ui.sig[0] = 0;
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
