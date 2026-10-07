/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca user interface. Four columns map to KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "OPTIMIST 0.1"  /* the FM-1 firmware Optimist (based on SLOOP and Felucca) */
#endif
static void project_save(uint32_t slot);
static void arrangement_save(void);
static void panel_setup(void);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
#if FELUCCA_SNAPSHOTS                        /* SAVE > SNAPSHOT: snapshots.c */
static uint32_t sn_ui_n(void);
static void sn_ui_label(char *b, uint32_t i);
static uint32_t sn_ui_row(uint32_t i, char *name, uint32_t *bytes);
static uint32_t sn_ui_free(void);
static int sn_ui_fits(void);
static uint32_t sn_ui_sig(void);
static void sn_ui(uint32_t op, uint32_t i);
#endif
static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, UP_SLOTS = none */
{
    return t->user && up_used(t->user - 1u) ? t->user - 1u : UP_SLOTS;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

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
    uint32_t graph_sig, head_sig, foot_sig, frame;
    uint8_t graph_top;           /* the graph strip's top G_OY rows hold something */
    uint8_t midi_view;           /* GLO > SYSTEM MIDI column: USB (0) / TRS (1, FELUCCA_UART builds); both stay on */
} ui;

static const page_t *cur_page(void) { return &PAGES[ui.page]; }
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
static void tempo_knob(int32_t s)
{
#if FELUCCA_BPM_LOCK
    static uint32_t said;
    if (fm1_ms - said > 1000u)
        ui_message("BPM LOCKED");
    said = fm1_ms;
    (void)s;
#else
    song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
    ui.bpm_t = 40;                                  /* the header's BPM lights up; no message over the header */
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

static void go_home(void)       /* TRACKS (SLOOP's HOME screen of four knobs and a scope is gone: it was unreachable) */
{
    ui.page = (uint8_t)page_first(FAM_TRK);
    ui.entry_open = 0;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

/* ------------------------------------------------------- track setup --- */
/* LIVE: no factory sequence patterns. Loading a sound (factory or user preset) never
 * writes the sequencer: every pattern is the one the player records or enters. */

/* the parts' sounds at power-on (engine, preset): bass, pad, lead */
static const uint8_t TRK_DEF[NPART][2] = {{0, 0}, {1, 0}, {4, 5}};   /* ANALOG 808 BOOM, DIGITAL RHODES, SAMPLE LOFI FLUTE */
/* part i's default engine (slot; TRK_DEF holds UIDs: a fallback when not built) and preset */
static uint32_t trk_def_engine(uint32_t i)
{
    return i >= NPART ? 0u : ENG_ALL ? TRK_DEF[i][0] : eng_slot(TRK_DEF[i][0]);
}
static uint32_t trk_def_preset(uint32_t i) { return ENG_ALL || eng_built(TRK_DEF[i][0]) ? TRK_DEF[i][1] : 0u; }   /* (i < NPART) */

static int seq_is_empty(const track_t *t) { return track_empty(t); }

static void track_defaults_steps(track_t *t) { steps_clear(t); }

/* what loading a sound (factory or user preset) leaves alone: the mix (LEVEL, PAN, MUTE, the FX bypass:
 * the TRACKS faders, GLO + key), the pattern parameters (LEN, DIV, SWING, GATE) and the key the part plays
 * in (ROOT, SCALE, QNT, CHORD: the song's; SCL + key sets the root of every part). The SLICER is
 * part of the sound: a factory preset turns it OFF (its defaults), a user preset brings its own */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || i == P_FXOFF || (i >= P_SLEN && i <= P_SGATE) ||
           (i >= P_ROOT && i <= P_QUANT) || i == P_CHORD;
}

/* INIT: an engine with no factory preset this build can play (none in its table, or none whose sample set is
 * built) has this one entry on the PRESETS list, and in the web editor's list: the engine's defaults, as a fresh
 * track on it. No data: made from the engine's and the track's defaults */
#define PRESET_INIT 0xFFu
static uint32_t eng_first_playable(const engine_t *e)    /* its first playable preset, else PRESET_INIT */
{
    uint32_t k;
    for (k = 0; k < e->npresets; k++)
        if (preset_playable(e, k))
            return k;
    return PRESET_INIT;
}

/* preset pi of the engine the track asked for: the whole sound (not the pattern parameters); PRESET_INIT, or any pi
 * on an engine without a playable preset: INIT */
static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i;
    if (is_drum(t))
        return;
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0;
    if (t == TSEL)
        sync_reload = 1;
    if (pi == PRESET_INIT || eng_first_playable(e) == PRESET_INIT) {
        t->preset = 0;
        for (i = 0; i < P_E0; i++)                    /* INIT: the track's sound and the engine's values to their */
            if (!param_kept(i))                       /* defaults */
                t->p[i] = TP[i].def;
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = e->edit[i].def;
        return;
    }
    pi %= e->npresets;
    t->preset = (uint8_t)pi;
    if (ENG_IS(e, FM6))
        fm6_cur[trk_index(t) % NPART] = 0;           /* its VOICE afresh: edits of the buffer go */
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i))
            t->p[i] = TP[i].def;                     /* sounds the same after any edit (not the pattern, not the mix) */
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)e->presets[pi].e[i];
    t->p[P_ATK] = e->presets[pi].env[0];
    t->p[P_DEC] = e->presets[pi].env[1];
    t->p[P_SUS] = e->presets[pi].env[2];
    t->p[P_REL] = e->presets[pi].env[3];
    t->p[P_ED_FLT] = e->presets[pi].fenv;
    t->p[P_ED_FX] = preset_trim(eng_uid(t->eng_req % NENGINES), pi);  /* level-matched (tools/level_presets.py) */
    t->p[P_VOICE] = e->presets[pi].mono ? V_LEGATO : V_POLY;   /* mono presets keep the legato feel */
    {   /* the rest of the patch: sends, arpeggiator (never a pattern: LIVE) */
        static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
        const preset_t *pr = &e->presets[pi];
        for (i = 0; i < 4u; i++) {
            t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
            t->p[P_AMODE + i] = (int16_t)(pr->arp[i] ? pr->arp[i] - 1 : TP[P_AMODE + i].def);
        }
        preset_extras(t->p, pr);                     /* glide, pitch / LFO modulation, voice mode */
        analog2_extras(t->p, e, pi);                 /* ANALOG 2's own values */
    }
}

/* the engine's defaults and its first preset. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    if (is_drum(t))
        return;
    fm1_irq_off();
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_to(t, eng_first_playable(e));      /* (its first preset; none: INIT) */
    fm1_irq_on();
}

static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
}

/* switch engine (its defaults + first preset); the footer names it, no notice */
static void select_engine(uint32_t e)
{
    if (is_drum(TSEL))
        return;
    set_engine(e);
    ui.force = 1;
}

/* the factory presets as one list by kind (basses, keys, organs, pads, leads, plucks and bells, stabs,
 * the rest), then the used user presets: the PRESETS knob and the PRESETS page browse it. By name: an
 * engine's preset table may change order; tests/ui_pages_test.c checks every preset is here once */
enum { BK_BASS, BK_KEYS, BK_ORGAN, BK_PAD, BK_LEAD, BK_PLUCK, BK_STAB, BK_FX };
static const char *const BANK_KIND[] = {"BASS", "KEYS", "ORGN", "PAD", "LEAD", "PLCK", "STAB", "FX"};
static const struct { uint8_t kind, e; const char *name; } BANK[] = {
    {BK_BASS, 0, "808 BOOM"}, {BK_BASS, 0, "808 DIRTY"}, {BK_BASS, 0, "808 SLIDE"}, {BK_BASS, 0, "SUB BASS"},
    {BK_BASS, 0, "PLUGG BASS"}, {BK_BASS, 0, "REESE"}, {BK_BASS, 0, "WOBBLE"}, {BK_BASS, 0, "ACID 303"},
    {BK_BASS, 1, "FM BASS"}, {BK_BASS, 2, "CZ BASS"}, {BK_BASS, 6, "FAT BASS"}, {BK_BASS, 0, "FUNK BASS"},
    {BK_BASS, 5, "WOW BASS"}, {BK_BASS, 3, "GB BASS"}, {BK_BASS, 4, "UP BASS"}, {BK_BASS, 4, "DEEP BASS"},
#if FELUCCA_ANALOG2
    {BK_BASS, 0, "LP24 BASS"},
#endif
    {BK_BASS, ENG_UID_FM6, "SOLID BASS"}, {BK_BASS, ENG_UID_FM6, "SAW BASS"},
    {BK_KEYS, 1, "RHODES"}, {BK_KEYS, 1, "DX RHODES"}, {BK_KEYS, 1, "WURLI"}, {BK_KEYS, 1, "M1 PIANO"},
    {BK_KEYS, 1, "AFRO KEYS"}, {BK_KEYS, 4, "GRAND PNO"}, {BK_KEYS, 4, "DUSTY PNO"}, {BK_KEYS, 4, "LOFI KEYS"}, {BK_KEYS, 2, "SOFT KEYS"},
    {BK_KEYS, 1, "CLAV"},
    {BK_KEYS, ENG_UID_FM6, "TINE EP"}, {BK_KEYS, ENG_UID_FM6, "CLAVINET"},
    {BK_ORGAN, 7, "SOUL ORGAN"}, {BK_ORGAN, 7, "GOSPEL"}, {BK_ORGAN, 7, "JAZZ ORGAN"}, {BK_ORGAN, 7, "DIRTY B3"},
    {BK_ORGAN, 7, "HOUSE ORGN"}, {BK_ORGAN, ENG_UID_FM6, "DRAWBARS"},
    {BK_PAD, 0, "WARM PAD"}, {BK_PAD, 6, "SAW PAD"}, {BK_PAD, 1, "GLASS PAD"}, {BK_PAD, 0, "DARK STR"},
    {BK_PAD, 2, "CZ STRING"}, {BK_PAD, 0, "ATMOS PAD"}, {BK_PAD, 8, "LOFI CLOUD"}, {BK_PAD, 8, "VIBE HAZE"},
    {BK_PAD, 5, "CHOIR AAH"}, {BK_PAD, 5, "SOUL OOH"}, {BK_PAD, ENG_UID_SUPER, "SUPER PAD"},
    {BK_PAD, ENG_UID_FM6, "STRINGS"}, {BK_PAD, ENG_UID_FM6, "FM GLASS"},
    {BK_LEAD, 0, "SUPERSAW"}, {BK_LEAD, ENG_UID_SUPER, "SUPER LEAD"}, {BK_LEAD, 0, "G-FUNK LD"}, {BK_LEAD, 6, "SYNC LEAD"},
    {BK_LEAD, 6, "HOOVER"}, {BK_LEAD, ENG_UID_SUPER, "HOOVER SAW"},
#if FELUCCA_ANALOG2
    {BK_LEAD, 0, "SYNC SWEEP"}, {BK_LEAD, 0, "FIFTH LEAD"},
#endif
    {BK_LEAD, 5, "TALKBOX"}, {BK_LEAD, 3, "GAME LEAD"}, {BK_LEAD, 4, "LOFI FLUTE"}, {BK_LEAD, 8, "FLUTE DUST"},
    {BK_LEAD, ENG_UID_FM6, "FM SYNC LD"}, {BK_LEAD, ENG_UID_FM6, "FLUTE"},
    {BK_PLUCK, 0, "TRAP PLUCK"}, {BK_PLUCK, ENG_UID_SUPER, "SUPER PLCK"}, {BK_PLUCK, 2, "RESO PLUCK"}, {BK_PLUCK, 1, "PLUGG BELL"}, {BK_PLUCK, 1, "TRAP BELL"},
    {BK_PLUCK, 1, "MUSIC BOX"}, {BK_PLUCK, 1, "KALIMBA"}, {BK_PLUCK, 1, "MARIMBA"}, {BK_PLUCK, 4, "VIBES"},
    {BK_PLUCK, 3, "8BIT ARP"},
    {BK_PLUCK, ENG_UID_FM6, "BELLS"}, {BK_PLUCK, ENG_UID_FM6, "FM MARIMBA"}, {BK_PLUCK, ENG_UID_FM6, "FM KALIMBA"},
    {BK_PLUCK, ENG_UID_FM6, "STEEL DRUM"}, {BK_PLUCK, ENG_UID_FM6, "TUBULAR"}, {BK_PLUCK, ENG_UID_FM6, "HARP"},
    {BK_STAB, 6, "MIN STAB"}, {BK_STAB, 6, "MIN7 STAB"}, {BK_STAB, 6, "RAVE STAB"}, {BK_STAB, 6, "DUB CHORD"}, {BK_STAB, ENG_UID_SUPER, "SUPER CHRD"},
    {BK_STAB, 0, "SYN BRASS"}, {BK_STAB, 2, "CZ BRASS"}, {BK_STAB, 4, "HORN STAB"}, {BK_STAB, 4, "STRING STB"},
    {BK_STAB, ENG_UID_FM6, "BRASS SECT"},
    {BK_FX, 4, "SCRATCH"}, {BK_FX, 4, "GM KIT"},
#if FELUCCA_ENG_PHYS
    {BK_PLUCK, ENG_UID_PHYS, "BELL TREE"}, {BK_PLUCK, ENG_UID_PHYS, "WOOD MRMBA"}, {BK_PLUCK, ENG_UID_PHYS, "PLUCK"},
    {BK_PLUCK, ENG_UID_PHYS, "THUMB PNO"}, {BK_PLUCK, ENG_UID_PHYS, "SYMP HARP"}, {BK_PAD, ENG_UID_PHYS, "BOWED METAL"},
    {BK_PAD, ENG_UID_PHYS, "DRONE STRING"}, {BK_FX, ENG_UID_PHYS, "HAND DRUM"}, {BK_FX, ENG_UID_PHYS, "TOMS"},
#endif
#if FELUCCA_ENG_ACID
    {BK_BASS, ENG_UID_ACID, "ACID LINE"}, {BK_BASS, ENG_UID_ACID, "ACID SQR"}, {BK_BASS, ENG_UID_ACID, "ACID RAGE"},
    {BK_BASS, ENG_UID_ACID, "ACID DUB"},
#endif
#if FELUCCA_ENG_SLICE
    {BK_FX, ENG_UID_SLICE, "BREAK 16"}, {BK_FX, ENG_UID_SLICE, "CHOP 8"}, {BK_FX, ENG_UID_SLICE, "REVERSE"},
    {BK_FX, ENG_UID_SLICE, "USR SLICE"},
#endif
#if FELUCCA_ENG_CZ
    {BK_BASS, ENG_UID_CZ, "PD BASS"}, {BK_LEAD, ENG_UID_CZ, "RESO SWEEP"}, {BK_PLUCK, ENG_UID_CZ, "GLASS BELL"},
    {BK_STAB, ENG_UID_CZ, "WIRE BRASS"}, {BK_PAD, ENG_UID_CZ, "SOFT PAD"}, {BK_FX, ENG_UID_CZ, "NOISE BREATH"},
    {BK_KEYS, ENG_UID_CZ, "PULSE KEYS"}, {BK_LEAD, ENG_UID_CZ, "CZ INIT"},
#endif
#if GR_FALLBACK                                     /* (GRAIN without its presets' sets: eng_grain.c) */
    {BK_PAD, 8, "GRAIN PAD"},
#endif
};
#define NBANK_ALL (sizeof BANK / sizeof BANK[0])
/* the list as this build has it: the entries whose engine is built and whose preset exists (a reduced build:
 * registry.h; a sample set left out takes its presets along). bank_ix: list position -> BANK entry */
static uint8_t bank_pi[NBANK_ALL];                   /* the preset index of each entry in its engine */
static uint8_t bank_ix[NBANK_ALL], bank_n;
static uint8_t bank_init[NENGINES], bank_ni;          /* after them: INIT of each engine with no entry (slots) */
static uint8_t bank_ready;
static void bank_resolve(void)
{
    uint32_t i, k;
    bank_n = bank_ni = 0;
    for (i = 0; i < NBANK_ALL; i++) {
        const engine_t *e;
        bank_pi[i] = 0xFF;
        if (!eng_built(BANK[i].e))
            continue;
        e = ENGINES[eng_slot_built(BANK[i].e)];
        for (k = 0; k < e->npresets; k++)
            if (str_eq(e->presets[k].name, BANK[i].name))
                bank_pi[i] = (uint8_t)k;
        if (bank_pi[i] != 0xFF && preset_playable(e, bank_pi[i]))
            bank_ix[bank_n++] = (uint8_t)i;
    }
    for (i = 0; i < NENGINES; i++) {
        for (k = 0; k < bank_n && eng_slot_built(BANK[bank_ix[k]].e) != i; k++)
            ;
        if (k == bank_n)
            bank_init[bank_ni++] = (uint8_t)i;
    }
    bank_ready = 1;
}
#define NBANK ((uint32_t)bank_n)                     /* the factory entries */
#define NLIST (NBANK + bank_ni)                      /* and the INIT ones: then the user presets */
static uint32_t preset_pos(uint32_t *total)          /* list index of the selected track's preset */
{
    uint32_t i, cur = 0;
    if (!bank_ready)
        bank_resolve();
    for (i = 0; i < NBANK; i++)
        if (eng_slot_built(BANK[bank_ix[i]].e) == TSEL->eng_req && bank_pi[bank_ix[i]] == TSEL->preset)
            cur = i;
    for (i = 0; i < bank_ni; i++)
        if (bank_init[i] == TSEL->eng_req)
            cur = NBANK + i;
    if (user_of(TSEL) < UP_SLOTS)
        cur = NLIST + up_rank(user_of(TSEL));
    *total = NLIST + up_count();
    return cur;
}

/* list index n (< total) -> engine slot, *k its preset (PRESET_INIT: INIT); NENGINES = user preset, *k its slot */
static uint32_t preset_at(uint32_t n, uint32_t *k)
{
    if (!bank_ready)
        bank_resolve();
    if (n >= NLIST) {
        *k = up_nth(n - NLIST);
        return NENGINES;
    }
    if (n >= NBANK) {
        *k = PRESET_INIT;
        return bank_init[n - NBANK];
    }
    *k = bank_pi[bank_ix[n]];
    return eng_slot_built(BANK[bank_ix[n]].e);
}
static const char *preset_kind(uint32_t n) { return n < NBANK ? BANK_KIND[BANK[bank_ix[n]].kind] : n < NLIST ? "INIT" : "USER"; }
/* the name of preset k of engine slot e as the lists show it */
static const char *preset_name(uint32_t e, uint32_t k)
{
    return k == PRESET_INIT || !ENGINES[e]->npresets ? "INIT" : ENGINES[e]->presets[k % ENGINES[e]->npresets].name;
}

static void preset_go(uint32_t n)                    /* load list index n into the selected track */
{
    uint32_t k, e = preset_at(n, &k);
    if (is_drum(TSEL))
        return;                                      /* one GM kit: nothing to browse */
    if (e == NENGINES) {
        up_load(k);
        return;
    }
    if (e != TSEL->eng_req)
        select_engine(e);
    apply_preset(k);
    ui.force = 1;
}

/* select track i (KNOB 1 on TRACKS, the editor): its sound, pages and pattern from now on */
static void track_select(uint32_t i)
{
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
