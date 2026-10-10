/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* NAME: a user preset or a project named on the device before it is written (docs/UI-OPTIMIST-DESIGN.md sections
 * 0 "Names", 4.3, 4.6; section 11.4). Ported from Felucca 1.0.1's ui_name.c (Leo Kuroshita, GPL-3.0-only): its
 * gesture, its key map and its multi-tap typing; the grammar is this UI's:
 *   SAVE          done: the name is written with what it names (stopped: STOP BEFORE SAVE keeps the screen)
 *   HOME tapped   the character before the cursor deleted; with the cursor at the start: cancel, nothing written
 *   HOME + a knob the whole name cleared
 *   white keys    ABC: AB CD EF GH IJK LM NO PQ RS TU VW XYZ 123 456 789 0-. (phone style: the same key within
 *                 0.8 s the group's next character); 123: 1 2 3 4 5 6 7 8 9 0 - . _ / # + one tap each
 *   black keys    by name, in both octaves: F# left, G# space, A# right, C# delete (held: they repeat), D# ABC / 123
 *   KNOB 1        the cursor; KNOB 2 (and PRESETS) the character at the cursor (at the end: a new one)
 * Where: SAVE + a sound button and SOUND's SAVE AS (a user preset), PROJECT's USER and PROJECT rows' SAVE (after the
 * modal over a used slot). Kits keep numbers (the user's ruling). The keys type and never sound: the keys go to the
 * UI as STEP's do (seq.c ly_lock LY_STEP). Names are kept as typed, upper case, at most 12 characters, and drawn in
 * sentence case. A user preset's name is its record's (upreset.c up_rec_t.name); a project's is the section log's
 * names record (storage/sections/sections.c sec_name_set, FELUCCA_UI == 1): only builds with the log (SECTIONS 8 or
 * 16) name projects, the four RAM slots save as before. */
enum { NK_NONE, NK_USER, NK_PROJ };
#define NM_LEN 12u
#define NM_TAP_MS 800u                                 /* multi-tap: the next letter within this */
#define NM_REP_MS 450u                                 /* a held arrow / DELETE repeats after this, */
#define NM_RATE_MS 90u                                 /* .. every this */
enum { NMK_LEFT, NMK_SPACE, NMK_RIGHT, NMK_DEL, NMK_MODE, NMK_NONE };
static const char *const NM_ABC[16] = {"AB", "CD", "EF", "GH", "IJK", "LM", "NO", "PQ", "RS", "TU", "VW", "XYZ",
                                       "123", "456", "789", "0-."};
static const char NM_NUM[17] = "1234567890-._/#+";
static const char NM_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/#+";   /* KNOB 2 */
static struct {
    uint8_t kind;                                      /* NK_*: NK_NONE = closed */
    uint8_t slot;
    uint8_t len, cur;                                  /* the name's length; the cursor 0..len */
    uint8_t num;                                       /* the white keys: 0 ABC, 1 123 */
    uint8_t key;                                       /* the white key (place + 1) of the letter being cycled, 0 none */
    uint8_t tap;                                       /* .. that letter's index in its group */
    uint8_t rep;                                       /* the key (+ 1) of a held arrow / DELETE, 0 none */
    uint8_t home, home_used;                           /* HOME down; a knob turned meanwhile (no delete) */
    uint32_t t, rep_t;                                 /* fm1_ms of the last tap; of the next repeat */
    char s[NM_LEN + 1u];
    char ph[NM_LEN + 1u];                              /* the automatic name ("ANALOG 07", "PROJECT 3") */
} nm;
#if SEC_LOGGED
static void sec_name(uint32_t s, char *b);             /* storage/sections/sections.c (FELUCCA_UI == 1) */
static int sec_name_set(uint32_t s, const char *nm);
#endif
static int up_store(uint32_t k, const char *name);     /* storage/upreset.c */
static void op_play(void);                              /* op_input.c */

static int name_on(void) { return nm.kind != NK_NONE; }
static void name_close(void)
{
    nm.kind = NK_NONE;
    ui.force = 1;
}

/* open NAME for user preset slot k (the sound's own name, else the automatic one) or project slot k (its stored
 * name, else the automatic one); the keys stop playing (op_input.c ui_input: ly_lock LY_STEP) */
static void name_open(uint32_t kind, uint32_t k)
{
    char b[16];
    uint32_t l;
    b[0] = 0;
    if (kind == NK_USER) {
        str_cpy(nm.ph, ENGINES[TSEL->eng_req % NENGINES]->name, 9);   /* as upreset.c up_store: "ANALOG 07" */
        l = str_len(nm.ph);
        nm.ph[l] = ' ';
        nm.ph[l + 1u] = (char)('0' + (k + 1u) / 10u);
        nm.ph[l + 2u] = (char)('0' + (k + 1u) % 10u);
        nm.ph[l + 3u] = 0;
        if (user_of(TSEL) < UP_SLOTS)
            up_name(user_of(TSEL), b);
    } else {
        str_cpy(nm.ph, "PROJECT ", sizeof nm.ph);
        fmt_int(nm.ph + 8, (int32_t)k + 1);
#if SEC_LOGGED
        sec_name(k, b);
#endif
    }
    str_cpy(nm.s, b[0] ? b : nm.ph, sizeof nm.s);
    nm.kind = (uint8_t)kind;
    nm.slot = (uint8_t)k;
    nm.len = nm.cur = (uint8_t)str_len(nm.s);
    nm.num = nm.key = nm.rep = nm.home = 0;
    for (l = LY_FX; l < LY_COUNT; l++)                 /* (no layer takes the keys: they type) */
        if (l != LY_STEP)
            ly_bit[l] = 0;
    op_disarm();
    ui.toast_t = 0;
    ui.force = 1;
}
static void name_user(uint32_t k) { name_open(NK_USER, k); }
static void name_user_free(void)                        /* SAVE AS, SAVE + a sound button: the first free slot */
{
    uint32_t i;
    for (i = 0; i < UP_SLOTS && up_used(i); i++)
        ;
    if (i == UP_SLOTS)
        ui_message("USER PRESETS FULL");
    else
        name_user(i);
}
/* a project saved through NAME (op_project.c): the log's builds name it, the others save it as before */
static void name_project(uint32_t k)
{
#if SEC_LOGGED
    name_open(NK_PROJ, k);
#else
    project_save(k);
#endif
}

static const char *nm_group(uint32_t p)                /* white key place p's characters */
{
    static char one[2];
    if (!nm.num)
        return NM_ABC[p & 15u];
    one[0] = NM_NUM[p & 15u];
    return one;
}
static uint32_t nm_black(uint32_t k)                   /* black key k's function: by its name (F# .. D#) */
{
    switch ((k + 5u) % 12u) {
    case 6: return NMK_LEFT;
    case 8: return NMK_SPACE;
    case 10: return NMK_RIGHT;
    case 1: return NMK_DEL;
    case 3: return NMK_MODE;
    default: return NMK_NONE;
    }
}
static void nm_commit(void)                            /* the letter being cycled is kept: the cursor past it */
{
    if (nm.key) {
        nm.key = 0;
        nm.cur++;
    }
}
static int nm_insert(char c)                           /* at the cursor (it stays on it); 0 = full */
{
    uint32_t i;
    if (nm.len >= NM_LEN) {
        ui_message("NAME FULL");
        return 0;
    }
    for (i = nm.len; i > nm.cur; i--)
        nm.s[i] = nm.s[i - 1u];
    nm.s[nm.cur] = c;
    nm.s[++nm.len] = 0;
    return 1;
}
static void nm_white(uint32_t p)
{
    const char *g = nm_group(p);
    uint32_t n = str_len(g);
    if (nm.key == p + 1u && fm1_ms - nm.t < NM_TAP_MS) {   /* the same key again: its next character */
        nm.tap = (uint8_t)((nm.tap + 1u) % n);
        nm.s[nm.cur] = g[nm.tap];
        nm.t = fm1_ms;
        return;
    }
    nm_commit();
    if (!nm_insert(g[0]))
        return;
    if (n > 1u) {
        nm.key = (uint8_t)(p + 1u);
        nm.tap = 0;
        nm.t = fm1_ms;
    } else {
        nm.cur++;
    }
}
static void nm_del(void)                               /* the character before the cursor */
{
    uint32_t i;
    if (!nm.cur)
        return;
    for (i = --nm.cur; i < nm.len; i++)
        nm.s[i] = nm.s[i + 1u];
    nm.len--;
}
static void nm_do(uint32_t f)                          /* a black key's function */
{
    nm_commit();
    switch (f) {
    case NMK_LEFT:
        if (nm.cur)
            nm.cur--;
        break;
    case NMK_RIGHT:
        if (nm.cur < nm.len)
            nm.cur++;
        break;
    case NMK_SPACE:
        if (nm_insert(' '))
            nm.cur++;
        break;
    case NMK_DEL:
        nm_del();
        break;
    case NMK_MODE:
        nm.num ^= 1u;
        break;
    default:
        break;
    }
}
static void nm_knob(uint32_t k, int32_t s)             /* KNOB 1 the cursor, KNOB 2 the character there */
{
    int32_t i, n = (int32_t)sizeof NM_SET - 1;
    nm_commit();
    if (k == 0u) {
        nm.cur = (uint8_t)clamp((int32_t)nm.cur + s, 0, nm.len);
        return;
    }
    if (nm.cur == nm.len && !nm_insert(' '))           /* at the end: a new character (from SPACE) */
        return;
    for (i = 0; i < n && NM_SET[i] != nm.s[nm.cur]; i++)
        ;
    i = i < n ? i : 0;
    nm.s[nm.cur] = NM_SET[((i + s) % n + n) % n];
}

/* SAVE: the name (spaces at its ends dropped) is written with what it names; the screen stays while that is
 * refused (playing). An empty name: a user preset the automatic one, a project none */
static void name_ok(void)
{
    char b[NM_LEN + 1u], l[4];
    uint32_t a = 0, z, kind;
    int rc;
    nm_commit();
    for (z = nm.len; z && nm.s[z - 1u] == ' '; z--)
        ;
    while (a < z && nm.s[a] == ' ')
        a++;
    for (z -= a, b[z] = 0; z--;)
        b[z] = nm.s[a + z];
    if (song.playing || transport_req) {               /* (a flash erase stops the audio) */
        ui_message("STOP BEFORE SAVE");
        return;
    }
    kind = nm.kind;
    name_close();
    ui.toast_next = 1;                                  /* (what it says: a toast, as a confirmed action's) */
    if (kind == NK_USER) {
        up_slot_label(l, nm.slot);
        rc = up_store(nm.slot, b);
        if (rc == 3)
            ui_message("SAVED (RAM)");
        else if (rc)
            ui_message("SAVE ERROR");
        else
            ui_say("SAVED ", l);
    } else {
#if SEC_LOGGED
        project_save(nm.slot);
        if (project_used(nm.slot) && sec_name_set(nm.slot, b))
            ui_message("NAME NOT SAVED");
#endif
    }
    ui.toast_next = 0;
}

/* one UI frame of NAME (op_input.c ui_input): the keys (seq.c lk_q, as STEP's), the knobs, SAVE, HOME, PLAY */
static void name_frame(uint32_t pressed, uint32_t held)
{
    uint32_t k, hb = 1u << panel.btn[B_HOME];
    int32_t s;
    if (nm.key && fm1_ms - nm.t >= NM_TAP_MS)          /* 0.8 s without a tap: the letter is kept */
        nm_commit();
    while (lk_r != lk_w) {
        uint32_t e = lk_q[lk_r % LKQ], kk = e & 0x7Fu;
        lk_r++;
        if (!((e >> 7) & 1u) || (e >> 8) != LY_STEP)
            continue;                                   /* (a key let go, or another layer's) */
        if (punch_key(kk) >= 0) {
            nm_white((uint32_t)punch_key(kk) & 15u);
            continue;
        }
        nm_do(nm_black(kk));
        if (nm_black(kk) == NMK_LEFT || nm_black(kk) == NMK_RIGHT || nm_black(kk) == NMK_DEL) {
            nm.rep = (uint8_t)(kk + 1u);
            nm.rep_t = fm1_ms + NM_REP_MS;
        }
    }
    if (nm.rep && !((fm1_in.notes >> (nm.rep - 1u)) & 1u))
        nm.rep = 0;
    else if (nm.rep && (int32_t)(fm1_ms - nm.rep_t) >= 0) {
        nm_do(nm_black(nm.rep - 1u));
        nm.rep_t = fm1_ms + NM_RATE_MS;
    }
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0) {
            if (held & hb) {                            /* HOME + a knob: the whole name cleared */
                nm.len = nm.cur = nm.key = 0;
                nm.s[0] = 0;
                nm.home_used = 1;
            } else if (k < 2u) {
                nm_knob(k, s);
            }
        }
    (void)panel_enc(EN_PRESET);                         /* (PRESETS does nothing here: no stray detents left) */
    (void)panel_enc(EN_SELECT);
    (void)panel_enc(EN_ALGO);
    if (pressed & hb) {
        nm.home = 1;
        nm.home_used = 0;
    }
    if (nm.home && !(held & hb)) {                      /* HOME let go: delete, or at the start cancel */
        nm.home = 0;
        nm_commit();
        if (!nm.home_used && nm.cur)
            nm_del();
        else if (!nm.home_used)
            name_close();
    }
    if (pressed & (1u << panel.btn[B_PLAY]))
        op_play();                                      /* (stop to save) */
    if (pressed & (1u << panel.btn[B_SAVE]))
        name_ok();
}

/* the key LEDs: every key that does something lit, the key whose letters are cycling blinks */
static uint32_t name_leds(void)
{
    uint32_t k, m = 0, blink = ((fm1_ms / 250u) & 1u) == 0u;
    for (k = 0; k < 27u; k++) {
        int32_t w = punch_key(k);
        uint32_t on = w < 0 ? nm_black(k) != NMK_NONE : nm.key != (uint32_t)(w & 15) + 1u || blink;
        m |= on << k;
    }
    return m;
}

/* ---- drawing (no footer: the user's ruling, 2026-10-08): the header names it, the cards' band is the name field
 * with the hints (SAVE done, HOME delete / cancel), the panel the keyboard. The field: 12 cells, the cursor a white
 * frame, the letter being cycled a cell in the track's colour, an empty name its automatic one, dim. The panel: the
 * cycling key's letters (the one typed lit), the white keys as two rows of 8 (the left octave, then the right), the
 * black keys' functions under their names */
static void name_title(char *t, uint32_t n)
{
    char l[4];
    str_cpy(t, "NAME ", n);
    if (nm.kind == NK_USER) {
        up_slot_label(l, nm.slot);
        str_cpy(t + 5, l, n - 5u);
    } else {
        str_cpy(t + 5, nm.ph, n - 5u);                  /* "Name project 3" */
    }
}
static uint32_t name_sig(void)
{
    uint32_t h = 2166136261u, i;
    const uint8_t st[8] = {nm.kind, nm.slot, nm.len, nm.cur, nm.num, nm.key, nm.tap, (uint8_t)settings.palette};
    for (i = 0; i < sizeof st; i++)
        h = (h ^ st[i]) * 16777619u;
    for (i = 0; i < NM_LEN; i++)
        h = (h ^ (uint8_t)nm.s[i]) * 16777619u;
    return h;
}
#define NM_CX(i) (12 + 18 * (int32_t)(i))              /* cell i: 16 x 22 at y 19 of the band */
static void name_draw_field(void)
{
    char b[NM_LEN + 2u], c[2] = {0, 0};
    uint32_t i, empty = nm.len == 0u;
    uint16_t tc = trk_col(song.sel);
    const char *src = empty ? nm.ph : nm.s;
    op_case(b, src, sizeof b);                          /* (sentence case as drawn: "Super saw") */
    cv_begin(240, OH_CARD, C_BLACK);
    cv_rect(3, 0, 234, OH_CARD - 1, OP_SURF);
    cv_text(9, 1, &FONT_S, nm.cur ? "Save done  Home delete" : "Save done  Home cancel", C_GRAY);
    {
        char n[8];
        fmt_int(n, nm.len);
        str_cpy(n + str_len(n), "/12", 4);
        cv_text(231 - text_w(&FONT_S, n), 1, &FONT_S, n, nm.len >= NM_LEN ? C_WARN : C_DIM);
    }
    for (i = 0; i < NM_LEN; i++) {
        int32_t x = NM_CX(i);
        uint16_t fg = empty ? C_DIM : C_WHITE;
        c[0] = i < str_len(b) ? b[i] : 0;
        if (i == nm.cur && nm.key) {                   /* the letter being cycled */
            cv_rect(x, 19, 16, 22, tc);
            fg = C_BLACK;
        } else if (i == nm.cur) {                      /* the cursor: a frame */
            cv_rect(x, 19, 16, 22, C_WHITE);
            cv_rect(x + 1, 20, 14, 20, OP_SURF);
        }
        if (!c[0] || (empty && i >= str_len(nm.ph)))
            cv_rect(x + 3, 38, 10, 1, C_LINE);          /* an empty cell */
        else if (c[0] == ' ')
            cv_rect(x + 7, 29, 2, 2, fg == C_BLACK ? C_BLACK : C_DIM);   /* a space */
        else
            cv_text(x + 4, 22, &FONT_S, c, fg);
    }
    if (nm.cur >= NM_LEN)                              /* full, the cursor past the end: a bar */
        cv_rect(NM_CX(NM_LEN) - 1, 19, 2, 22, C_WHITE);
    cv_blit(0, OY_CARD);
}
static void name_draw_keys(void)
{
    static const char *const FN[5] = {"F#", "G#", "A#", "C#", "D#"};
    static const char *const DO[5] = {"Left", "Space", "Right", "Del", ""};
    uint16_t tc = trk_col(song.sel);
    uint32_t i;
    char c[2] = {0, 0};
    cv_begin(240, OH_PANEL, C_BLACK);
    if (nm.key) {                                      /* the cycling key's characters: the one typed lit */
        const char *g = nm_group(nm.key - 1u);
        uint32_t n = str_len(g);
        for (i = 0; i < n; i++) {
            int32_t x = 6 + 22 * (int32_t)i;
            c[0] = g[i];
            cv_rect(x, 1, 20, 20, i == nm.tap ? tc : OP_SURF);
            cv_text(x + 6, 3, &FONT_S, c, i == nm.tap ? C_BLACK : C_HI);
        }
        cv_text(12 + 22 * (int32_t)n, 3, &FONT_S, "Tap again: next", C_GRAY);
    } else {
        cv_text(6, 3, &FONT_S, nm.num ? "One tap, one character" : "Tap again: next letter", C_GRAY);
    }
    cv_text(234 - text_w(&FONT_S, nm.num ? "123" : "ABC"), 3, &FONT_S, nm.num ? "123" : "ABC", tc);
    for (i = 0; i < 16u; i++) {                        /* the white keys: the left octave, then the right one */
        int32_t x = 6 + 29 * (int32_t)(i % 8u), y = i < 8u ? 25 : 48;
        uint32_t act = nm.key == i + 1u;
        const char *g = nm_group(i);
        cv_rect(x, y, 27, 20, act ? tc : OP_SURF);
        cv_text(x + 14 - text_w(&FONT_S, g) / 2, y + 2, &FONT_S, g, act ? C_BLACK : C_HI);
    }
    for (i = 0; i < 5u; i++) {                         /* the black keys, by name */
        int32_t x = 6 + 46 * (int32_t)i, y = 74;
        const char *d = i == NMK_MODE ? (nm.num ? "ABC" : "123") : DO[i];
        cv_rect(x, y, 44, 44, C_LINE);
        cv_text(x + 22 - text_w(&FONT_S, FN[i]) / 2, y + 3, &FONT_S, FN[i], C_GRAY);
        cv_text(x + 22 - text_w(&FONT_S, d) / 2, y + 23, &FONT_S, d, C_WHITE);
    }
    cv_blit(0, OY_PANEL);
}
static void name_draw(void)                             /* the cards' band and the panel (op_draw.c op_frame_draw) */
{
    uint32_t sig = name_sig();
    if (sig == ui.sig[1] && sig == ui.sig[2])
        return;
    ui.sig[1] = ui.sig[2] = sig;
    name_draw_field();
    name_draw_keys();
}
