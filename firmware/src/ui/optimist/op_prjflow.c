/* SPDX-License-Identifier: GPL-3.0-only */
/* PROJECT (docs/UI-OPTIMIST-DESIGN.md section 4.6, 2026-10-14 flow). A storage row (PROJECT, SNAPSHOT, USER) is four cards:
 * BANK (only a store of more than 16 slots), OPERATION (LOAD, the default; SAVE; DELETE), ITEM (the slot, a knob that walks
 * all of them, the bank following) and, on PROJECT, NEW (a blank project: it touches no slot). The 16 white keys are the
 * slots of the current bank: key n selects item n AND runs the operation on it, through the confirm; YES runs it on the
 * ITEM the knob is on. A free slot has nothing to load or clear: a toast, no question. The operation goes back to LOAD
 * after each action, when the row or the screen changes: a stray key never saves. Under the cards: the bank's 16 slots as
 * a strip, and the item's info (read from what is stored, nothing scanned). Then TOOLS, SLOOP and MEMORY, as they were. */

static void name_user(uint32_t k);                      /* op_name.c: NAME, then the save */
static void name_project(uint32_t k);
#if SEC_LOGGED
static void sec_name(uint32_t s, char *b);              /* storage/sections/sections.c: the project's name, "" none */
#endif
static uint32_t up_engine_slot(uint32_t k);             /* storage/upreset.c */
#if SEC_LOGGED
static int sec_scene_clear(uint32_t s);                 /* sections.c: a section's records gone */
#endif

enum { PR_PROJECT, PR_SNAP, PR_USER, PR_TOOLS, PR_A24, PR_MEM };
static const uint8_t PRJ[] = {PR_PROJECT,
#if FELUCCA_SNAPSHOTS
                              PR_SNAP,
#endif
                              PR_USER, PR_TOOLS,
#if SL24_AUTO
                              PR_A24,
#endif
#if SEC_LOGGED
                              PR_MEM,
#endif
};
#define NPRJ (sizeof PRJ / sizeof PRJ[0])
static const char *const PRJ_NAME[] = {"PROJECT", "SNAPSHOT", "USER", "TOOLS", "SLOOP", "MEMORY"};
enum { PO_LOAD, PO_SAVE, PO_DELETE };                    /* the operations, in the knob's order */
static const char *const PO_NAME[3] = {"LOAD", "SAVE", "DELETE"};
static const uint8_t PO_CODE[3] = {0, 2, 1};            /* up_ui, sn_ui: 0 load, 1 clear / erase, 2 save */
#define PRJ_BANK 16u                                    /* the slots one bank holds: the 16 keys */
#if SEC_LOGGED
static void sec_mem(uint32_t *pct, uint32_t *more);    /* sections.c: the section log's MEM gauge */
static void prj_mem(cell_t *c, uint32_t k)             /* MEMORY: USED (a bar) and FREE (sections that fit) */
{
    static uint32_t t, pct, more;
    if (!t || fm1_ms - t > 500u) {                      /* (a model of the log: counted twice a second at most) */
        t = fm1_ms | 1u;
        sec_mem(&pct, &more);
    }
    c->label = k ? "FREE" : "USED";                    /* (FREE: the sections that still fit; was MORE, the audit's #43) */
    c->kind = CK_RO;
    fmt_int(c->val, (int32_t)(k ? more : pct));
    c->unit = k ? "" : "%";
    c->col = !k && pct > 90u ? C_WARN : 0;
    cell_gauge(c, 0, 0, k ? 32 : 100, (int32_t)(k ? more : pct));
}
#endif

static const page_t *tools_page(void)                  /* TOOLS: CLRSQ INIT MISS NEW (params.c) */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == FAM_SAVE && PAGES[i].id[0] == G_CLRSEQ)
            return &PAGES[i];
    return &PAGES[0];
}
static uint32_t prj_kind(uint32_t r) { return PRJ[r % NPRJ]; }
static uint32_t prj_rows(void) { return NPRJ; }
static void prj_name(uint32_t r, char *b) { str_cpy(b, PRJ_NAME[prj_kind(r)], 12); }
static uint32_t prj_slot(void) { return (uint32_t)clamp(song.g[G_SLOT], 1, FELUCCA_SECTIONS) - 1u; }

/* ---- the stores: how many slots, which is the item, is it used */
static uint32_t prj_slot_n(uint32_t kind)
{
#if FELUCCA_SNAPSHOTS
    if (kind == PR_SNAP)
        return sn_ui_n();
#endif
    return kind == PR_PROJECT ? FELUCCA_SECTIONS : kind == PR_USER ? UP_SLOTS : 0u;
}
static uint32_t prj_slot_cur(uint32_t kind)
{
    return kind == PR_PROJECT ? prj_slot() : kind == PR_USER ? ui.user_slot : ui.snap_slot;
}
static void prj_slot_set(uint32_t kind, uint32_t s)
{
    if (kind == PR_PROJECT)
        val_turn(&GP[G_SLOT], &song.g[G_SLOT], 0, (int32_t)s + 1 - song.g[G_SLOT], 1);
    else if (kind == PR_USER)
        ui.user_slot = (uint8_t)s;
    else
        ui.snap_slot = (uint8_t)s;
}
static uint32_t prj_slot_used(uint32_t kind, uint32_t s)
{
#if FELUCCA_SNAPSHOTS
    char nm[16];
    uint32_t bytes;
    if (kind == PR_SNAP)
        return sn_ui_row(s, nm, &bytes) != 0u;
#endif
    return kind == PR_PROJECT ? (uint32_t)project_used(s) : (uint32_t)up_used(s);
}
static void prj_slot_label(uint32_t kind, uint32_t s, char *l)    /* "3", "U03", "B" (l holds 4) */
{
    l[0] = 0;
    if (kind == PR_PROJECT)
        fmt_int(l, (int32_t)s + 1);
    else if (kind == PR_USER)
        up_slot_label(l, s);
#if FELUCCA_SNAPSHOTS
    else
        sn_ui_label(l, s);
#endif
}
static uint32_t prj_slots_row(uint32_t kind) { return kind <= PR_USER; }     /* a row with slots */
static uint32_t prj_banks(uint32_t kind) { return (prj_slot_n(kind) + PRJ_BANK - 1u) >> 4; }   /* > 1: the BANK card */
static uint32_t prj_nops(uint32_t kind) { return kind == PR_PROJECT && !SEC_LOGGED ? 2u : 3u; }   /* (a RAM project slot has no DELETE; the section log's has: its tombstones) */

/* ---- the operation: LOAD at first, and again after an action, when the row changes or the screen is left (op_enter) */
static uint8_t prj_op_v, prj_op_row, prj_arm_op, prj_arm_slot;
static void prj_op_reset(void) { prj_op_v = PO_LOAD; }
static uint32_t prj_op(void)
{
    if (prj_op_row != ui.row[SCR_PROJECT]) {            /* (another row: LOAD) */
        prj_op_row = ui.row[SCR_PROJECT];
        prj_op_v = PO_LOAD;
    }
    return prj_op_v;
}
/* red only for what destroys or replaces work (style guide 7), one rule for the card's verb and the modal's frame: a
 * SAVE or DELETE of slot s that holds something; a LOAD of one while the work is not as stored; NEW always (it drops the
 * project in memory) */
static uint32_t prj_danger(uint32_t kind, uint32_t op, uint32_t s)
{
    uint32_t u = prj_slot_used(kind, s);
    if (op == PO_LOAD && kind == PR_USER)
        return u && !is_drum(TSEL) && snd_edited(song.sel);   /* (a user preset replaces the sound only: asked over edits, ed_ask's rule) */
    return op == PO_LOAD ? u && work_changed() : u;
}

static void prj_cell(uint32_t r, uint32_t k, cell_t *c)
{
    uint32_t kind = prj_kind(r), op, s;
    if (kind == PR_TOOLS) {
        page_cell(tools_page(), k, c);
        return;
    }
    cell_clear(c);
#if SEC_LOGGED
    if (kind == PR_MEM) {
        if (k < 2u)
            prj_mem(c, k);
        return;
    }
#endif
    if (kind == PR_A24) {
        if (!k)
            cell_act(c, "SLOOP", "IMPORT", 0, 1);       /* (it replaces the work: red, as its modal) */
        return;
    }
    op = prj_op();
    s = prj_slot_cur(kind);
    if (k == 0u && prj_banks(kind) > 1u) {              /* BANK: only a store of more than 16 slots */
        c->label = "BANK";
        c->kind = CK_VAL;
        fmt_int(c->val, (int32_t)(s >> 4) + 1);
        c->unit = prj_banks(kind) == 2u ? "/2" : prj_banks(kind) == 3u ? "/3" : "/4";
        cell_gauge(c, 1, 0, (int32_t)prj_banks(kind) - 1, (int32_t)(s >> 4));
    } else if (k == 1u) {                               /* OPERATION: its verb, red when it would destroy or replace */
        c->label = "ACTION";
        c->kind = CK_VAL;
        str_cpy(c->val, PO_NAME[op], sizeof c->val);
        c->col = prj_danger(kind, op, s) ? C_ERR : 0;
        cell_gauge(c, 1, 0, (int32_t)prj_nops(kind) - 1, (int32_t)op);
    } else if (k == 2u) {                               /* ITEM: the slot, lit when used, dim when free */
        c->label = "ITEM";
        c->kind = CK_VAL;
        prj_slot_label(kind, s, c->val);
        if (kind == PR_PROJECT) {
            c->col = project_used(s) ? 0 : C_DIM;
            cell_gauge(c, 1, 1, FELUCCA_SECTIONS, (int32_t)s + 1);
        } else if (kind == PR_USER) {
            c->col = up_used(s) ? 0 : C_DIM;
            cell_gauge(c, 1, 0, UP_SLOTS - 1, (int32_t)s);
        }
#if FELUCCA_SNAPSHOTS
        else {
            char nm[16];
            uint32_t bytes, st = sn_ui_row(s, nm, &bytes);
            c->col = st ? C_STATUS[st & 3u] : C_DIM;    /* (saved green, another build's amber, damaged red) */
            cell_gauge(c, 1, 0, (int32_t)sn_ui_n() - 1, (int32_t)s);
        }
#endif
    } else if (k == 3u && kind == PR_PROJECT) {
        cell_act(c, "PROJECT", "NEW", 0, 1);            /* (a blank project, no slot touched; red: it drops the work) */
    } else if (k == 3u && kind == PR_USER) {
        cell_act(c, "SOUND", "INIT", 0, 1);             /* (the selected track's sound back to its engine's defaults: INIT's home, the user 2026-10-14) */
    }
}

static void prj_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    uint32_t kind = prj_kind(r), cur;
    int32_t d = s > 0 ? 1 : -1;
    if (kind == PR_TOOLS) {
        page_turn(tools_page(), k, s, fine);
        return;
    }
    if (kind > PR_USER)
        return;                                         /* (SLOOP, MEMORY: nothing to turn) */
#if FELUCCA_FAVORITES
    if (kind == PR_USER && k == 2u && s != OP_RESET && op_shift2(fm1_in.buttons)) {   /* SHIFT2 + ITEM: the slot a favorite (right) or not (left) */
        uint32_t slot = prj_slot_cur(kind);
        if (up_used(slot) && fav_user_set(slot, s > 0)) {
            settings_later = 1;                         /* (written once the audio is quiet: op_fav_mark) */
            ui.force = 1;
            pre_toast_fav(s > 0 ? "FAV + USER PRESET" : "FAV - USER PRESET");
        }
        return;
    }
#endif
    if (k == 1u) {
        uint32_t op = prj_op();
        prj_op_v = (uint8_t)(s == OP_RESET ? PO_LOAD : clamp((int32_t)op + d, 0, (int32_t)prj_nops(kind) - 1));
        return;
    }
    if (s == OP_RESET || k == 3u)
        return;                                         /* (NEW is an action card: the turn only made it hot) */
    cur = prj_slot_cur(kind);
    if (k == 0u) {                                      /* BANK: the same place in the other bank */
        int32_t t = (int32_t)cur + d * (int32_t)PRJ_BANK;
        if (prj_banks(kind) > 1u && t >= 0 && t < (int32_t)prj_slot_n(kind))
            prj_slot_set(kind, (uint32_t)t);
        return;
    }
    if (k == 2u)                                        /* ITEM: across the banks, the bank follows */
        prj_slot_set(kind, (uint32_t)clamp((int32_t)cur + d, 0, (int32_t)prj_slot_n(kind) - 1));
}

/* ask to run operation op on slot s of the cursor row's store: the confirm (SAVE yes, HOME no), or, with nothing to load
 * or clear in a free slot, a toast and no question */
static void prj_ask(uint32_t r, uint32_t kind, uint32_t op, uint32_t s)
{
    static const char *const WHAT[3] = {"PROJECT ", "SNAPSHOT ", "USER "};
    char l[8], t[16], m[16];
    prj_slot_label(kind, s, l);
    if (op != PO_SAVE && !prj_slot_used(kind, s)) {
        str_cpy(m, l, sizeof m);
        str_app(m, " IS EMPTY", sizeof m);
        ui.toast_next = 1;
        ui_say_st(0u, "SLOT ", m);
        ui.toast_next = 0;
        return;
    }
    str_cpy(t, WHAT[kind], sizeof t);                   /* the target: "PROJECT 3", "SNAPSHOT 2", "USER U18" */
    str_app(t, l, sizeof t);
    prj_arm_op = (uint8_t)op;
    prj_arm_slot = (uint8_t)s;
    op_arm(SCR_PROJECT, r, 0, PO_NAME[op], t, prj_danger(kind, op, s));
}
static void prj_do(uint32_t kind, uint32_t op, uint32_t s)
{
    if (kind == PR_PROJECT) {
#if SEC_LOGGED && FELUCCA_UI == 1
        if (op == PO_DELETE) {
            (void)sec_scene_clear(s);                   /* (the section log's tombstones: its record, FX, extras and LFO records) */
            prj_op_reset();
            ui.force = 1;
            return;
        }
#endif
        if (op == PO_LOAD) {
            project_load(s);
            work_mark();                                /* (the work as loaded) */
        } else {
            name_project(s);                            /* (NAME first, with the section log: op_name.c) */
        }
    } else if (kind == PR_USER) {
        ui.user_slot = (uint8_t)s;
        if (op == PO_SAVE && !is_drum(TSEL))
            name_user(s);                               /* SAVE: NAME first (op_name.c) */
        else
            up_ui(PO_CODE[op], s);                      /* upreset.c */
        if (op == PO_LOAD)
            snd_mark(song.sel);                         /* (the user preset as loaded) */
    }
#if FELUCCA_SNAPSHOTS
    else {
        ui.snap_slot = (uint8_t)s;
        sn_ui(PO_CODE[op], s);                          /* snapshots.c */
    }
#endif
    prj_op_reset();                                     /* (after an action: LOAD again) */
    ui.force = 1;
}

static int prj_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    uint32_t kind = prj_kind(r);
    if (kind == PR_TOOLS)
        return page_yes(SCR_PROJECT, r, tools_page(), k, ok);
    if (kind == PR_MEM)
        return 0;
#if SL24_AUTO
    if (kind == PR_A24) {
        if (k)
            return 0;
        if (!ok)
            op_arm(SCR_PROJECT, r, k, "IMPORT", "SLOOP", 1);
        else
            sl24_auto_import();                         /* SLOOP 2.4 .. 2.6's autosave into the work (sl24_guard.c) */
        return 1;
    }
#endif
    if (kind == PR_PROJECT && k == 3u) {                /* NEW: the work blank, no slot touched; asks first */
        if (!ok)
            op_arm(SCR_PROJECT, r, k, "NEW", "PROJECT", 1);
        else
            op_global_go(G_NEWPRJ);
        ui.force = 1;
        return 1;
    }
    if (kind == PR_USER && k == 3u)
        return snd_init_yes(SCR_PROJECT, r, k, ok);
    if (kind > PR_USER)
        return 0;
    if (!ok)
        prj_ask(r, kind, prj_op(), prj_slot_cur(kind)); /* (any card: YES runs the operation on the ITEM) */
    else
        prj_do(kind, prj_arm_op, prj_arm_slot);
    return 1;
}

/* ---- OCT- / OCT+ on a row with more than one bank: a tap (on release) = the previous / next bank, the same place in it
 * (the user, 2026-10-15: "oct+/- on the save screen should bank up/down"); lit only where there is a bank that way */
static uint32_t prj_bank_avail(void)                    /* the OCT LED bits (B_OCTDN, B_OCTUP) that have a bank to go to */
{
    uint32_t kind, b;
    if (ui.scr != SCR_PROJECT)
        return 0u;
    kind = prj_kind(ui.row[SCR_PROJECT]);
    if (!prj_slots_row(kind) || prj_banks(kind) < 2u)
        return 0u;
    b = prj_slot_cur(kind) >> 4;
    return (b > 0u ? 1u << B_OCTDN : 0u) | (b + 1u < prj_banks(kind) ? 1u << B_OCTUP : 0u);
}
static uint32_t prj_bank_tap(uint32_t up)               /* 1: the tap was a bank step (or the page's, nothing to do) */
{
    uint32_t kind, cur, n, nb;
    char b[8];
    if (ui.scr != SCR_PROJECT || !prj_slots_row(prj_kind(ui.row[SCR_PROJECT])))
        return 0u;
    kind = prj_kind(ui.row[SCR_PROJECT]);
    if (!((prj_bank_avail() >> (up ? B_OCTUP : B_OCTDN)) & 1u))
        return 1u;
    cur = prj_slot_cur(kind);
    n = prj_slot_n(kind);
    nb = (cur >> 4) + (up ? 1u : (uint32_t)-1);
    cur = (nb << 4) + (cur & 15u);
    prj_slot_set(kind, cur < n ? cur : n - 1u);
    fmt_int(b, (int32_t)nb + 1);
    str_app(b, "/", sizeof b);
    fmt_int(b + str_len(b), (int32_t)prj_banks(kind));
    ui.force = 1;
    ui.toast_next = 1;
    ui_say("BANK ", b);
    ui.toast_next = 0;
    return 1u;
}

/* ---- the keys (the user, 2026-10-13: "while in save/load screen, the note keyboard should not play notes, but select the
 * slot"; 2026-10-14: and run the operation): on a row with slots the 16 white keys are the slots of the bank the ITEM is
 * in (USER: U01..U16 or U17..U32): key n selects item n and asks for the operation on it. Lit: a used slot full, a free
 * one dim, the selected one blinking (op_keys.c) */
/* key w (white key 0..15) -> the slot it picks on the cursor row, -1 none */
static int32_t prj_key_slot(uint32_t kind, int32_t w)
{
    uint32_t s = (prj_slots_row(kind) ? prj_slot_cur(kind) & ~15u : 0u) + (uint32_t)w;
    return prj_slots_row(kind) && w >= 0 && w < 16 && s < prj_slot_n(kind) ? (int32_t)s : -1;
}
static void prj_key(uint32_t k, uint32_t down)           /* op_input.c op_drain: a key of PROJECT */
{
    uint32_t kind = prj_kind(ui.row[SCR_PROJECT]);
    int32_t s = down && !op_armed() ? prj_key_slot(kind, punch_key(k)) : -1;
    if (s < 0)
        return;
    prj_slot_set(kind, (uint32_t)s);                    /* (the ITEM follows the key) */
    ui.hot = 2;
    ui.hot_lit = 1;
    ui.force = 1;
    prj_ask(ui.row[SCR_PROJECT], kind, prj_op(), (uint32_t)s);
}
static void prj_keys_lit(uint32_t *f, uint32_t *d, uint32_t blink)
{
    uint32_t kind = prj_kind(ui.row[SCR_PROJECT]), w;
    int32_t s;
    for (w = 0; w < 16u; w++) {
        s = prj_key_slot(kind, (int32_t)w);
        if (s < 0)
            continue;
        if (prj_slot_used(kind, (uint32_t)s))
            *f |= 1u << white_key(w);
        else
            *d |= 1u << white_key(w);
        if ((uint32_t)s == prj_slot_cur(kind) && !blink)
            *f &= ~(1u << white_key(w)), *d &= ~(1u << white_key(w));   /* (the selected one blinks) */
    }
}

/* ---- the panel under the cards: the bank's slots as a strip, then the item's info */
static struct {
    uint8_t kind, op, cur, danger;
    uint8_t first, n;                                   /* the bank's first slot, how many it has */
    uint16_t used, bar, col1, col2;                     /* a bit a slot used; the track colour; the detail lines' colours */
    char head[28], name[16], d1[28], d2[28];
} pdp;
static void cv_tall(uint32_t y, uint32_t h, uint16_t bg, void (*fn)(void));   /* op_draw.c */
static uint32_t hs(uint32_t h, const char *s);
static uint32_t hu(uint32_t h, uint32_t v);
static const felucca_font_t *font_big(void);            /* op_graph.c */

static void prj_info(uint32_t kind, uint32_t s)         /* the item's name and lines, from what is stored */
{
    char l[8];
    uint32_t u = prj_slot_used(kind, s);
    pdp.name[0] = pdp.d1[0] = pdp.d2[0] = 0;
    pdp.col1 = pdp.col2 = C_GRAY;
    prj_slot_label(kind, s, l);
    str_cpy(pdp.name, u ? "" : "EMPTY", sizeof pdp.name);
    str_cpy(pdp.d1, u ? "STORED" : "FREE SLOT", sizeof pdp.d1);
    if (!u)
        return;
    if (kind == PR_PROJECT) {
#if SEC_LOGGED
        sec_name(s, pdp.name);
#endif
        if (!pdp.name[0]) {
            str_cpy(pdp.name, "PROJECT ", sizeof pdp.name);
            str_cpy(pdp.name + 8, l, sizeof pdp.name - 8u);
        }
    } else if (kind == PR_USER) {
        uint32_t e = up_engine_slot(s);
        up_name(s, pdp.name);
#if FELUCCA_FAVORITES
        if (fav_user_has(s))
            str_app(pdp.name, " *", sizeof pdp.name);   /* (a favorite: the star after its name) */
#endif
        str_cpy(pdp.d1, "ENGINE", sizeof pdp.d1);
        str_cpy(pdp.d2, e < NENGINES ? ENGINES[e]->name : "NOT IN THIS BUILD", sizeof pdp.d2);
        pdp.col2 = e < NENGINES ? C_WHITE : C_WARN;
    }
#if FELUCCA_SNAPSHOTS
    else {
        uint32_t bytes, st = sn_ui_row(s, pdp.name, &bytes);
        str_cpy(pdp.d1, st == 3u ? "DAMAGED" : st == 2u ? "SAVED BY ANOTHER BUILD" : "SAVED", sizeof pdp.d1);
        pdp.col1 = C_STATUS[st & 3u];
        if (bytes) {
            fmt_int(pdp.d2, (int32_t)((bytes + 512u) >> 10));
            str_app(pdp.d2, " KB", sizeof pdp.d2);
        }
    }
#endif
}
static void prj_paint(void)                             /* the panel's band in its own coordinates (cv_tall) */
{
    char b[32];
    uint32_t i;
    for (i = 0; i < 16u; i++) {                         /* the strip: used full, free outlined, the item framed bright */
        int32_t x = (int32_t)i * 15;
        uint32_t on = (pdp.used >> i) & 1u, cur = i == pdp.cur;
        if (i >= pdp.n)
            continue;
        if (on)
            cv_rect(x, 0, 13, 12, cur ? cur_bg(pdp.bar) : C_GRAY);
        else
            cv_frame(x, 0, 13, 12, cur ? cur_bg(pdp.bar) : C_LINE);
        if (cur)
            cv_frame(x - 1, -1, 15, 14, C_WHITE);
    }
    cv_text(0, 20, &FONT_S, op_case(b, pdp.head, sizeof b), pdp.danger ? C_ERR : C_HI);   /* what a key does now */
    cv_text(0, 40, text_w(font_big(), op_case(b, pdp.name, sizeof b)) <= 236 ? font_big() : &FONT_S, b, pdp.used >> pdp.cur & 1u ? C_WHITE : C_DIM);
    if (pdp.d1[0])
        cv_text(0, 76, &FONT_S, op_case(b, pdp.d1, sizeof b), pdp.col1);
    if (pdp.d2[0])
        cv_text(0, 94, &FONT_S, op_case(b, pdp.d2, sizeof b), pdp.col2);
}
static int prj_listing(void) { return ui.scr == SCR_PROJECT && prj_slots_row(prj_kind(ui.row[SCR_PROJECT])); }
static void prj_draw(uint32_t y, uint32_t h)            /* op_draw.c, when prj_listing: lazy, redrawn when what it shows changes */
{
    uint32_t kind = prj_kind(ui.row[SCR_PROJECT]), s = prj_slot_cur(kind), op = prj_op(), i, sig, base = s & ~15u;
    char l[8];
    pdp.kind = (uint8_t)kind;
    pdp.op = (uint8_t)op;
    pdp.first = (uint8_t)base;
    pdp.n = (uint8_t)(prj_slot_n(kind) - base < 16u ? prj_slot_n(kind) - base : 16u);
    pdp.cur = (uint8_t)(s - base);
    pdp.used = 0;
    for (i = 0; i < pdp.n; i++)
        pdp.used = (uint16_t)(pdp.used | (prj_slot_used(kind, base + i) ? 1u << i : 0u));
    pdp.bar = trk_col(song.sel);
    pdp.danger = (uint8_t)prj_danger(kind, op, s);
    prj_slot_label(kind, s, l);
    str_cpy(pdp.head, PO_NAME[op], sizeof pdp.head);
    str_app(pdp.head, " ", sizeof pdp.head);
    str_app(pdp.head, kind == PR_USER ? "USER " : kind == PR_SNAP ? "SNAPSHOT " : "PROJECT ", sizeof pdp.head);
    str_app(pdp.head, l, sizeof pdp.head);
    prj_info(kind, s);
    sig = hs(hs(hs(hs(hu(hu(hu(hu(hu(0x93u, kind), op), s), pdp.used), pdp.bar + settings.palette * 65536u), pdp.head), pdp.name), pdp.d1), pdp.d2);
    sig = hu(sig, pdp.danger | h << 8) | 1u;
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(y, h, C_BLACK, prj_paint);
}
