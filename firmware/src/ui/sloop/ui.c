/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca user interface. Four columns map to KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
#define ACC C_HI                   /* amber everywhere; white is the only accent */
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? C_WHITE : C_HI)   /* a value: white while its knob turns (no colour per knob) */
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout: four 60 px columns, 4 px inset */
/* Terminus 8x16 (S) and 16x32 (L) */
#define Y_HEAD 0
#define H_HEAD 20
#define Y_LABEL 26
#define Y_VALUE 44
#define Y_GAUGE 64
#define Y_SEP_END 70
#define Y_GRAPH 74
#define H_GRAPH 124
#define G_OY 24                       /* graphs draw in the lower 100 px; the focus readout sits on top */
#define Y_FOOT 202
#define H_FOOT 38

static struct {
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* SEQ: 16-step bank (follows the cursor) */
    uint8_t cursor;              /* SEQ: step being edited (STEP page KNOB 1 moves it) */
    uint8_t entry_open;          /* SEQ: keys held since the first press of this entry */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about (HOME held) */
    uint8_t menu_sel;
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t msg_st;              /* its status colour: 0 the palette, 1 ok (green), 2 notice (amber), 3 error (red) */
    uint8_t bpm_t;               /* frames the BPM stays highlighted after a SELECT turn */
    uint8_t arm, arm_t;          /* destructive action armed: param id, frames left to confirm */
    uint32_t rec_t0;             /* REC press time (btn_hold) */
    uint8_t confirm;             /* 1 = "clear the sequence?" (REC held on SEQ / ARP), 2 = "clear track n?" (TRACKS) */
    uint8_t confirm_trk;         /* the track the dialog clears */
    uint8_t uslot;               /* SAVE > USER: the selected user preset slot */
    uint8_t sslot;               /* SAVE > SNAPSHOT: the selected row (the last: BEFORE LOAD) */
    /* layers (ui_layers.c): a function button held, the keys and knobs do something else */
    uint8_t layer;               /* the layer drawn (LY_*), LY_PLAY = none */
    uint8_t layer_btn;           /* its button (B_*), NB = none */
    uint8_t layer_used;          /* a key / knob / OCT was used while it was held: no tap on release */
    uint32_t layer_t0;           /* its press time (ms) */
    uint8_t step_page;           /* SEQ layer: the 16 steps shown (page x 16) */
    uint8_t step_follow;         /* DRUM STEP: the page follows the playhead (on while stopped; a page key turns it off) */
    uint16_t step_held;          /* SEQ layer: the step keys held (white key index) */
    uint32_t step_sess;          /* SEQ layer: the undo session of this hold */
    uint8_t hold_kind;           /* a hold to confirm: 1 = clear the track (REC), 2 = save (SAVE) */
    uint32_t hold_t0;            /* (ms) */
    uint8_t hold_trk;
    uint32_t tap_ms[4];          /* tap tempo: the last taps */
    uint8_t tap_n;
    char msg[30];                /* (29 characters: the top bar's width) */
    uint32_t enc_t[NE];
    /* drawn-state cache */
    char col[4][32];
    char focus_l[8], focus_v[8], focus_u[8];   /* the touched column, shown large */
#if FELUCCA_BIGVALS
    char big_l[4][8], big_v[4][10], big_u[4][8];   /* the four columns, for the big values (SLOOP 2.4: pages without a graph) */
    uint16_t big_c[4];
#endif
    uint32_t graph_sig, head_sig, foot_sig, frame;
    uint8_t graph_top;           /* the graph strip's top G_OY rows hold something */
    uint8_t midi_view;           /* GLO > SYSTEM MIDI column: USB (0) / TRS (1, FELUCCA_UART builds); both stay on */
#if FELUCCA_PARAM_HELP
    uint16_t help;               /* the knob's help line: its offset in PH_BLOB, 0 = none (param_help.c; in what
                                  * was the struct's padding after midi_view: no RAM) */
#endif
} ui;

static const page_t *cur_page(void) { return &PAGES[ui.page]; }
#include "param_help.c"                                         /* the knob's help line (FELUCCA_PARAM_HELP) */
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* ui_input.c */
static void layer_screen_draw(void);                            /* ui_layers.c */
static void hold_screen_draw(void);
static uint8_t layer_shown;

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam)
            return i;
    return 0;
}

/* a message's status colour by the words of its fixed part a (tools/colors.json "status"): 3 an error (red),
 * 2 a notice (amber: stop first, empty, missing, again to confirm, RAM only), 1 done (green: saved, loaded), 0 none */
static uint32_t msg_status(const char *a)
{
    static const char *const W[] = {"ERROR", "FULL", "STOP", "EMPTY", "MISSING", "AGAIN", "NO ", "LOCKED", "RAM",
                                    "BUSY", "SAVED", "STORED", "LOADED", "SENT"};
    static const uint8_t S[] = {3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1};
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

/* transient message in the top bar: a + b, in the status colour its words say */
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
    ui.msg_st = (uint8_t)msg_status(a);
}

static void ui_message(const char *s) { ui_say(s, ""); }
/* the same in a status colour (st: 1 ok green, 2 notice amber, 3 error red; tools/colors.json "status") */
static void ui_say_st(uint32_t st, const char *a, const char *b)
{
    ui_say(a, b);
    ui.msg_st = (uint8_t)(st & 3u);
}

/* SELECT turned: the tempo. FELUCCA_BPM_LOCK (after Felucca 1.0.2 #58, hugelton/Felucca db70550, by Leo Kuroshita,
 * GPL-3.0-only; here the build switch is the choice, no menu item): only with GLO held (ui_layers.c layer_knobs);
 * elsewhere SELECT says BPM LOCKED (once a turn burst) and leaves it. GLO > GLOBAL's BPM knob still sets it */
static void tempo_keep(void);                                   /* ui_tempo.c: the TEMPO page, when up, stays (SELECT never opens it) */
static void tempo_knob(int32_t s)
{
    PH_CLEAR();                                     /* (the help line is the parameter knobs' only) */
#if FELUCCA_BPM_LOCK
    static uint32_t said;
    if (fm1_ms - said > 1000u)
        ui_message("BPM LOCKED");
    said = fm1_ms;
    (void)s;
#else
    song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
    ui.bpm_t = 40;                                  /* the header's BPM lights up; no message over the header */
    tempo_keep();
#endif
}

static void page_entered(void)
{
    const page_t *pg = cur_page();
    song.seq_mode = pg->fam == FAM_SEQ;
    ui.entry_open = 0;
    ui.hot_t = 0;                                /* the white value / focus box was the old page's */
    ui.force = 1;
}

static int step_on(const step_t *st) { return st->time == ST_NOTE && st->n; }
/* step i of track t has something to play (synth: notes, drums: a lane) */
static int trk_step_on(const track_t *t, uint32_t i)
{
    return is_drum(t) ? dstep_mask(&t->dstep[i % NSTEP]) != 0u : step_on(&t->step[i % NSTEP]);
}

static void step_clear(step_t *st)
{
    memset(st, 0, sizeof *st);
    st->time = ST_REST;
}

/* undo / redo (EDIT + OCT- / OCT+), seq.c undo.c: one level back / forward; 0 = nothing there */
static int undo_swap(int redo)
{
    if (!undo_apply(redo))
        return 0;
    sync_reload = 1;
    ui.force = 1;
    return 1;
}
/* its message: "UNDO" / "REDO"; the history: "UNDO 3/5 TRACK 2" (levels applied of all, its track) */
static void undo_say(int redo)
{
    char b[32];
    if (!undo_swap(redo)) {
        ui_message(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO");
        return;
    }
    str_cpy(b, redo ? "REDO" : "UNDO", sizeof b);
#if FELUCCA_UNDO_HISTORY
    {
        uint32_t n, m, tk, k;
        undo_status(&n, &m, &tk);
        k = str_len(b);
        b[k++] = ' ';
        fmt_int(b + k, (int32_t)n);
        k = str_len(b);
        b[k++] = '/';
        fmt_int(b + k, (int32_t)m);
        k = str_len(b);
        str_cpy(b + k, tk == TRK_DRUM ? " DRUMS" : " TRACK ", sizeof b - k);
        if (tk != TRK_DRUM) {
            k = str_len(b);
            b[k++] = (char)('1' + tk);
            b[k] = 0;
        }
    }
#endif
    ui_message(b);
}

/* SEQ cursor: wraps inside the pattern length, the bank follows, a step entry ends */
static void cursor_set(int32_t c)
{
    int32_t len = TSEL->p[P_SLEN] > 0 ? TSEL->p[P_SLEN] : 1;
    ui.cursor = (uint8_t)((c % len + len) % len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
}

static void cursor_fix(void)                           /* LEN got shorter: onto the last step */
{
    if (ui.cursor >= (uint32_t)TSEL->p[P_SLEN])
        cursor_set(TSEL->p[P_SLEN] - 1);
}

static void note_name(char *b, uint32_t n)
{
    str_cpy(b, N_NOTE[n % 12u], 4);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
}

#if FELUCCA_CHORD_NAMES
/* a step's notes as a chord: "C", "Am", "F#maj7", "Bm7b5", "G5"... in any inversion, named by its root (the
 * roots tried from the bass up). From isod89/sloop-fm1 PR #45 (8d9623f) by Erick Buendia Barrientos (Erbubar23),
 * GPL-3.0-only; here: 1 = named into b (8 B), 0 = no chord it knows (b untouched: the STEP page keeps "C4 +2") */
static int chord_name(char *b, const uint8_t *note, uint32_t n)
{
    static const struct { uint16_t mask; char suf[5]; } CH[] = {
        {0x091u, ""}, {0x089u, "m"}, {0x049u, "dim"}, {0x111u, "aug"}, {0x085u, "sus2"}, {0x0A1u, "sus4"},
        {0x081u, "5"}, {0x491u, "7"}, {0x891u, "maj7"}, {0x489u, "m7"}, {0x449u, "m7b5"},
    };
    uint32_t i, k, set = 0, bass = 255, r;
    if (n < 2u)
        return 0;
    for (i = 0; i < n; i++) {
        set |= 1u << (note[i] % 12u);
        if (note[i] < bass)
            bass = note[i];
    }
    for (i = 0; i < 12u; i++) {
        r = (bass + i) % 12u;                        /* roots from the bass up */
        if ((set >> r) & 1u) {
            uint32_t rot = ((set >> r) | (set << (12u - r))) & 0xFFFu;
            for (k = 0; k < sizeof CH / sizeof CH[0]; k++)
                if (rot == CH[k].mask) {
                    str_cpy(b, N_NOTE[r], 4);
                    str_cpy(b + str_len(b), CH[k].suf, 5);
                    return 1;
                }
        }
    }
    return 0;
}
#endif

static void open_family(uint32_t fam)
{
    if (cur_page()->fam == fam) {          /* same button again: next page */
        uint32_t i = ui.page + 1u;
        if (i >= NPAGES || PAGES[i].fam != fam)
            i = page_first(fam);
#if PAGE_SHOWN_FN
        while (!page_shown(&PAGES[i]))                 /* (ANALOG 2's pages: not on this track; the drum track's) */
            i = i + 1u < NPAGES && PAGES[i + 1u].fam == fam ? i + 1u : page_first(fam);
#endif
        ui.page = (uint8_t)i;
    } else {
        ui.page = ui.fam_last[fam] && PAGES[ui.fam_last[fam]].fam == fam ? ui.fam_last[fam]
                                                                          : (uint8_t)page_first(fam);
    }                                                  /* (ANALOG 2: a page not shown on this track: ui_draw turns to EDIT 1) */
    ui.fam_last[fam] = ui.page;
    page_entered();
}

#if FELUCCA_SEL_PAGES
/* SELECT on a page: the previous / next page of its family, as tapping the family button again but both ways,
 * stopping at the ends (SLOOP 2.4 ui.c page_walk, isod89/sloop-fm1 8d3823f, GPL-3.0-only). Not the screens of their
 * own (SONG, DRUMS, the FM6 editor), pages this track does not show, nor a family of one page (TRACKS): 0 then,
 * and SELECT is the tempo there */
static int page_walk(int32_t s)
{
    const page_t *pg = cur_page();
    uint32_t i, fam = pg->fam, n = 0;
    int32_t cur = -1, to;
    uint8_t idx[12];
    if (pg->scope == SC_SONG || pg->scope == SC_DRUM || pg->scope == SC_FM6K)
        return 0;
    for (i = 0; i < NPAGES && n < sizeof idx; i++) {
        if (PAGES[i].fam != fam || PAGES[i].scope == SC_SONG || PAGES[i].scope == SC_DRUM || PAGES[i].scope == SC_FM6K ||
            !page_shown(&PAGES[i]))
            continue;
        if (i == ui.page)
            cur = (int32_t)n;
        idx[n++] = (uint8_t)i;
    }
    if (n < 2u || cur < 0)
        return 0;
    to = clamp(cur + (s > 0 ? 1 : -1), 0, (int32_t)n - 1);
    if (to != cur) {
        ui.page = idx[to];
        ui.fam_last[fam] = ui.page;
        page_entered();
    }
    return 1;
}
#endif

static void go_home(void)       /* TRACKS (SLOOP's HOME screen of four knobs and a scope is gone: it was unreachable) */
{
    ui.page = (uint8_t)page_first(FAM_TRK);
    ui.entry_open = 0;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

/* switch engine (its defaults + first preset); the footer names it, no notice */
static void select_engine(uint32_t e)
{
    if (is_drum(TSEL))
        return;
    set_engine(e);
    ui.force = 1;
}

static void preset_go(uint32_t n)                    /* load list index n into the selected track */
{
    uint32_t k, e = preset_at(n, &k);
    PH_CLEAR();
    if (is_drum(TSEL))
        return;                                      /* one GM kit: nothing to browse */
    if (e == NENGINES) {
        up_load(k);
        return;
    }
#if FELUCCA_NATIVE_BANKS
    if (e == NB_LIST) {
        nb_load(k);
        return;
    }
#endif
    if (e != TSEL->eng_req)
        select_engine(e);
    apply_preset(k);
    ui.force = 1;
}

/* select track i (KNOB 1 on TRACKS, the editor): its sound, pages and pattern from now on */
static void track_select(uint32_t i)
{
    PH_CLEAR();
    if (i >= NTRK || i == song.sel)
        return;
    song.sel = (uint8_t)i;
    rec_follow(i);                                   /* LIVE: recording follows the selected track */
    ui.entry_open = 0;
    ui.cursor = 0;
    ui.bank = 0;
    sync_reload = 1;
    ui.force = 1;
}
