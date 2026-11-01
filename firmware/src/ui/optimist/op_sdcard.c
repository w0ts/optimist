/* SPDX-License-Identifier: GPL-3.0-only */
/* SD card mode's lists on PROJECT (FELUCCA_SD_UI; docs/UI-OPTIMIST-DESIGN.md 4.14, docs/SD-CARD-DESIGN.md 9; the user,
 * 2026-10-26: projects saved and loaded by name, every list as banks of 16 on the white keys). Rows PROJECT (with a card
 * ready: the card's projects; without: the flash slot as before), PRESETS, KITS, SAMPLES. A list row is four small cards
 * and the page under them, two columns of eight names as the keys sit (the left octave, then the right):
 *   KNOB 1 PAGE   the page (OCT- / OCT+ tapped too)            KNOB 2 ACTION   LOAD (the default), SAVE, DELETE; USE
 *   KNOB 3 ITEM   the slot, across the pages                   KNOB 4 VIEW     PROJECTS: NAME, SAVES (the item's versions);
 *                                                                              PRESETS, KITS: FOLDER, NAME; SAMPLES: FOLDER,
 *                                                                              NAME, RECENT
 * A white key picks its slot and asks the action on it, the slot's name on screen (a folder: into it, no question); the
 * same key again, or YES, runs it; another key asks about that one instead (the user, 2026-10-26: "one press shows what
 * it is and a second press actually loads"). HOME goes up a folder (SAVES: back to the projects), at the list's own folder back to the mixer. SAVE names first
 * (NAME, the slot's name or the work's; the card's rules: sd_layout.c sdl_name_ok); a project has versions, a preset or a
 * kit is written over only when asked over its own slot. USE: the selected synth part's sample, by its hash (the player
 * reads it: storage/sd/sd_mode.c sdm_sample_of). The card's side is storage/sd/sd_mode.c (included after the stores). */
enum { SDC_PROJ, SDC_PRE, SDC_KIT, SDC_SMP, SDC_N };   /* the card's lists */
enum { SDV_FOLDER, SDV_NAME, SDV_RECENT, SDV_VERSIONS };   /* the views (sd_browse.c's, and a project's versions) */
enum { SDO_LOAD, SDO_SAVE, SDO_DELETE, SDO_USE,       /* (ACTION) */
       SDO_SAVE_LOAD };                                 /* "SAVE <the work>?" before a LOAD (op_prjflow.c PO_SAVE_LOAD is 3: USE) */
#define SDC_KEYS 16u                                    /* a page's slots (sd_browse.c SDC_KEYS) */
#define SDC_NAME 64u                                    /* a card name's room (sd_layout.c SDL_NAME_MAX + 1) */
#define SDC_PNAME 13u                                   /* the flash project's name's (sections.c SEC_NAME_LEN + 1) */
static int sdm_ready(void);                             /* storage/sd/sd_mode.c */
static void sdm_mount_try(void);
static int sdm_open(uint32_t list, uint32_t view, const char *vname);
static void sdm_turn(int32_t step);
static uint32_t sdm_page(void);
static uint32_t sdm_pages(void);
static uint32_t sdm_count(void);
static const char *sdm_slot(uint32_t k, uint32_t *dir, uint32_t *cut);
static const char *sdm_base_of(uint32_t k);
static int sdm_into(uint32_t k);
static void sdm_load_later(uint32_t k);
static int sdm_up(void);
static int sdm_at_top(void);
static void sdm_folder(char *b, uint32_t cap);
static void sdm_do(uint32_t op, uint32_t k);
static int sdm_save(uint32_t list, const char *name, const char *was);
static void sdm_load_name(const char *name);
static uint32_t sdm_keep(void);                         /* (SYSTEM > SD CARD: op_project.c) */
static void sdm_backup(void);                           /* (SYSTEM > SETTINGS: to the card and back) */
static void sdm_restore(void);
static uint32_t sdm_scale(void);
static void sdm_set(uint32_t what, int32_t s);
static void sdm_leave(void);
static void sdm_again(void);
static uint32_t sdm_state(void);
static uint32_t sdm_pre_ms(void);                       /* (SD_STREAM: the pre-buffer, its KB) */
static uint32_t sdm_pre_kb(void);
static uint32_t sd_card_mb(void);                       /* storage/sd/sdcard.c */
static void name_card(uint32_t list, const char *prefill, const char *was);   /* op_name.c */
static void sec_name(uint32_t s, char *b);              /* storage/sections/sections.c */

static struct {
    uint8_t open, stale, ready, item, op, tried;        /* the list in sd_mode.c's page (SDC_N none); read again; the card
                                                         * ready when it was read; the cursor's slot; the action; a mount tried */
    uint8_t view[SDC_N];
    char vname[SDC_NAME];                      /* SAVES: whose versions */
    char unit[8];                                       /* PAGE's "/5" */
} sdc = {SDC_N, 1, 0, 0, 0, 0, {SDV_FOLDER, SDV_FOLDER, SDV_FOLDER, SDV_FOLDER}, "", ""};
static const char *const SDV_NAME_S[2][4] = {{"FOLDER", "NAME", "RECENT", "SAVES"}, {"NAME", "NAME", "NAME", "SAVES"}};
static const char *const SDO_NAME_S[4] = {"LOAD", "SAVE", "DELETE", "USE"};
static const char *const SDC_WHAT[SDC_N] = {"PROJECT", "PRESET", "KIT", "SAMPLE"};

/* the row's list: PROJECT only with a card ready, the card's rows always (they say NO SD CARD); SDC_N none */
static uint32_t sdc_list_of(uint32_t kind)
{
    return kind == PR_PROJECT ? (sdm_ready() ? SDC_PROJ : SDC_N) : kind == PR_CPRE ? SDC_PRE : kind == PR_CKIT ? SDC_KIT
           : kind == PR_CSMP ? SDC_SMP : SDC_N;
}
static uint32_t sdc_here(void) { return ui.scr == SCR_PROJECT ? sdc_list_of(prj_kind(ui.row[SCR_PROJECT])) : SDC_N; }
static int sdc_listing(void) { return sdc_here() < SDC_N; }
static uint32_t sdc_views(uint32_t l) { return l == SDC_SMP ? 3u : 2u; }   /* (PROJECTS: FOLDER = by name, then SAVES) */
static uint32_t sdc_view_at(uint32_t l, uint32_t i) { return l == SDC_PROJ && i ? SDV_VERSIONS : i; }
static uint32_t sdc_view_ix(uint32_t l) { return sdc.view[l] == SDV_VERSIONS ? 1u : sdc.view[l]; }
static uint32_t sdc_nops(uint32_t l) { return l == SDC_SMP ? 1u : l == SDC_PROJ && sdc.view[l] == SDV_VERSIONS ? 2u : 3u; }
static uint32_t sdc_op_at(uint32_t l, uint32_t i)
{
    return l == SDC_SMP ? SDO_USE : sdc_nops(l) == 2u && i ? SDO_DELETE : i;
}
static void sdc_op_reset(void)                          /* (prj_op_reset: a screen entered, an action) */
{
    sdc.op = 0, sdc.stale = 1, sdc.tried = 0;
    if (ui.scr == SCR_PROJECT && !sdm_ready())
        sdm_mount_try();                                /* (PROJECT entered: a card put in since; none: a few CMD0s) */
}

/* the list read again when stale, another row's, or the card came or went (never every frame: a folder pass a time) */
static void sdc_sync(uint32_t l)
{
    uint32_t ready;
    if (!sdm_ready() && !sdc.tried) {
        sdc.tried = 1;
        sdm_mount_try();                                /* (no card: a few CMD0s; poll prepares one found) */
    }
    ready = (uint32_t)sdm_ready();
    if (!sdc.stale && sdc.open == l && sdc.ready == ready)
        return;
    (void)sdm_open(l, sdc.view[l], sdc.vname);
    sdc.open = (uint8_t)l;
    sdc.stale = 0;
    sdc.ready = (uint8_t)ready;
    if (sdc.item >= SDC_KEYS)
        sdc.item = 0;
}
static void sdc_title(uint32_t kind, char *b)           /* PROJECT's row name (12): a sub-folder's, or whose SAVES are shown */
{
    uint32_t l = sdc_list_of(kind);
    if (l < SDC_N && sdc.open == l && sdm_ready() && (sdc.view[l] == SDV_VERSIONS || !sdm_at_top()))
        sdm_folder(b, 12);
}
static const char *sdc_name(uint32_t k)                 /* slot k as shown, "" none */
{
    uint32_t dir, cut;
    const char *s = sdm_slot(k, &dir, &cut);
    return s ? s : "";
}
static uint32_t sdc_is_dir(uint32_t k)                  /* slot k a folder to go into (a project's folder is the project) */
{
    uint32_t dir = 0, cut;
    return sdm_slot(k, &dir, &cut) && dir && (sdc.open != SDC_PROJ || sdc.view[SDC_PROJ] == SDV_VERSIONS);
}

/* ---- the cards */
static void sdc_cell(uint32_t kind, uint32_t k, cell_t *c)
{
    uint32_t l = sdc_list_of(kind), op;
    cell_clear(c);
    if (sdc_here() == l)
        sdc_sync(l);                                    /* (the cursor's row only: another row's cards, asked by the
                                                         * editor's screens, never read the card) */
    if (!sdm_ready() || (sdc.open != l && k != 3u)) {
        if (!k && !sdm_ready()) {
            c->label = "CARD";
            c->kind = CK_RO;
            cell_val(c, "NONE");
            c->col = C_WARN;
        }
        return;
    }
    op = sdc_op_at(l, sdc.op);
    if (k == 0u) {
        c->label = "PAGE";
        c->kind = CK_VAL;
        fmt_int(c->val, (int32_t)sdm_page() + 1);
        sdc.unit[0] = '/';
        fmt_int(sdc.unit + 1, (int32_t)sdm_pages());
        c->unit = sdc.unit;
        cell_gauge(c, 0, 0, (int32_t)sdm_pages() - 1, (int32_t)sdm_page());
    } else if (k == 1u) {
        c->label = "ACTION";
        c->kind = CK_VAL;
        cell_val(c, SDO_NAME_S[op]);
        c->col = op == SDO_DELETE || (op == SDO_SAVE && sdc.item < sdm_count()) ? C_ERR : 0;
        cell_gauge(c, 1, 0, (int32_t)sdc_nops(l) - 1, (int32_t)sdc.op);
    } else if (k == 2u) {
        c->label = "ITEM";
        c->kind = CK_VAL;
        if (sdc.item < sdm_count())
            fmt_int(c->val, (int32_t)sdc.item + 1);
        else
            cell_val(c, "-");
        cell_gauge(c, 0, 1, SDC_KEYS, (int32_t)sdc.item + 1);
    } else {
        c->label = "VIEW";
        c->kind = CK_VAL;
        cell_val(c, SDV_NAME_S[l == SDC_PROJ][sdc.view[l] & 3u]);
        cell_gauge(c, 1, 0, (int32_t)sdc_views(l) - 1, (int32_t)sdc_view_ix(l));
    }
}
static void sdc_page_turn(int32_t d)
{
    sdm_turn(d);
    if (sdc.item >= sdm_count() && sdm_count())
        sdc.item = (uint8_t)(sdm_count() - 1u);
    ui.force = 1;
}
static void sdc_view_set(uint32_t l, uint32_t v)        /* VIEW: the list again (SAVES: the item's project, else the work's) */
{
    if (v == SDV_VERSIONS) {
        if (sdc.item < sdm_count())                     /* (the projects' list: the item is a project) */
            str_cpy(sdc.vname, sdm_base_of(sdc.item), sizeof sdc.vname);
        else
            sec_name(0, sdc.vname);
    }
    sdc.view[l] = (uint8_t)v;
    sdc.op = 0;
    sdc.item = 0;
    (void)sdm_open(l, v, sdc.vname);
    sdc.open = (uint8_t)l;
    sdc.stale = 0;
    ui.force = 1;
}
static void sdc_turn(uint32_t kind, uint32_t k, int32_t s)
{
    uint32_t l = sdc_list_of(kind);
    int32_t d = s > 0 ? 1 : -1;
    if (!sdm_ready() || l >= SDC_N)
        return;
    sdc_sync(l);
    if (k == 1u) {
        sdc.op = (uint8_t)(s == OP_RESET ? 0 : step_in(sdc.op, d, (int32_t)sdc_nops(l)));
        return;
    }
    if (s == OP_RESET)
        return;
    if (k == 0u) {
        sdc_page_turn(d);
    } else if (k == 2u) {                               /* ITEM: across the pages, the page following */
        if (d > 0 && sdc.item + 1u >= SDC_KEYS && sdm_page() + 1u < sdm_pages()) {
            sdc_page_turn(1);
            sdc.item = 0;
        } else if (d < 0 && !sdc.item && sdm_page()) {
            sdc_page_turn(-1);
            sdc.item = SDC_KEYS - 1u;
        } else {
            sdc.item = (uint8_t)step_in(sdc.item, d, (int32_t)(sdm_count() ? sdm_count() : 1u));
        }
    } else {
        sdc_view_set(l, sdc_view_at(l, (uint32_t)step_in((int32_t)sdc_view_ix(l), d, (int32_t)sdc_views(l))));
    }
}

/* ---- the confirm: op on slot k of the list (SAVE and a slot past the list's end: NAME at once) */
static void sdc_name_for(uint32_t l, uint32_t k)        /* NAME, prefilled: the slot's name, else the work's (a project) */
{
    char nm[SDC_NAME];
    nm[0] = 0;
    if (l == SDC_PRE && is_drum(TSEL)) {
        ui_message("DRUM TRACK: NO SOUND");             /* (a preset is a synth part's sound: said before NAME) */
        return;
    }
    if (k < sdm_count() && !sdc_is_dir(k))
        str_cpy(nm, l == SDC_PROJ ? sdm_base_of(k) : sdc_name(k), sizeof nm);
    else if (l == SDC_PROJ)
        sec_name(0, nm);
    name_card(l, nm, k < sdm_count() ? nm : "");
}
static void sdc_ask(uint32_t r, uint32_t l, uint32_t op, uint32_t k)
{
    char a[16], cur[SDC_PNAME];
    if (op == SDO_SAVE && k >= sdm_count()) {
        sdc_name_for(l, k);
        return;
    }
    if (k >= sdm_count()) {
        ui.toast_next = 1;
        ui_message("EMPTY");
        ui.toast_next = 0;
        return;
    }
    if (sdc_is_dir(k)) {
        if (sdm_into(k))                                /* a folder: into it, no question */
            sdc.item = 0, ui.force = 1;
        return;
    }
    str_cpy(a, sdc_name(k), sizeof a);
    prj_arm_op = (uint8_t)op;
    prj_arm_slot = (uint8_t)k;
    sec_name(0, cur);
    if (l == SDC_PROJ && op == SDO_LOAD && work_changed() && cur[0] && !song.playing && !transport_req) {
        prj_arm_op = SDO_SAVE_LOAD;                     /* "SAVE <the work's name>?" first (as the flash slots, op_prjflow.c) */
        op_arm(SCR_PROJECT, r, 0, "SAVE", cur, 1);
        return;
    }
    op_arm(SCR_PROJECT, r, 0, SDO_NAME_S[op], a,
           op == SDO_USE ? 0 : op != SDO_LOAD || (l == SDC_PROJ ? work_changed() : l == SDC_PRE ? snd_edited(song.sel)
                                                                                                : snd_edited(TRK_DRUM)));
    ui.arm_stop = (uint8_t)(l == SDC_PROJ && op == SDO_LOAD && (song.playing || transport_req));
}
static int sdc_no(void)                                 /* HOME on "SAVE <the work>?": the LOAD asked next (prj_no's) */
{
    if (ui.arm_scr != SCR_PROJECT || prj_arm_op != SDO_SAVE_LOAD || sdc_here() != SDC_PROJ)
        return 0;
    prj_arm_op = SDO_LOAD;
    op_arm(SCR_PROJECT, ui.arm_row, 0, "LOAD", sdc_name(prj_arm_slot), 1);
    return 1;
}
static int sdc_yes(uint32_t r, uint32_t kind, uint32_t ok)
{
    uint32_t l = sdc_list_of(kind), op, k;
    char cur[SDC_PNAME];
    if (!sdm_ready())
        return 1;
    sdc_sync(l);
    if (!ok) {
        sdc_ask(r, l, sdc_op_at(l, sdc.op), sdc.item);
        return 1;
    }
    op = prj_arm_op, k = prj_arm_slot;
    if (op == SDO_SAVE_LOAD) {                          /* YES to "SAVE <the work>?": saved by its name, then loaded */
        char want[SDC_NAME];
        str_cpy(want, sdm_base_of(k), sizeof want);
        sec_name(0, cur);
        if (!sdm_save(SDC_PROJ, cur, cur))
            sdm_load_name(want);                        /* (the card's copy whole first: never the flash copy's only) */
    } else if (op == SDO_SAVE) {
        sdc_name_for(l, k);
    } else if (op == SDO_LOAD && l == SDC_PROJ && (song.playing || transport_req)) {
        sdm_load_later(k);                              /* (stopped, loaded, played again: op_edits.c pj_tick) */
    } else {
        sdm_do(op, k);
    }
    sdc.op = 0;
    sdc.stale = 1;                                      /* (what it changed: read again) */
    ui.force = 1;
    return 1;
}

/* ---- the keys, OCT, HOME */
static void op_yes(void);                               /* (op_input.c: SAVE's YES) */
static void sdc_key(uint32_t kind, int32_t w)
{
    uint32_t l = sdc_list_of(kind);
    if (w < 0 || w >= (int32_t)SDC_KEYS || !sdm_ready())
        return;
    if (op_armed()) {                                   /* this list's question up: its key again YES, another key asks again */
        if (ui.arm_scr != SCR_PROJECT || ui.arm_row != ui.row[SCR_PROJECT])
            return;
        if ((uint32_t)w == prj_arm_slot) {
            op_yes();
            return;
        }
        op_disarm();
    }
    sdc_sync(l);
    sdc.item = (uint8_t)w;
    ui.hot = 2;
    ui.hot_lit = 1;
    ui.force = 1;
    sdc_ask(ui.row[SCR_PROJECT], l, sdc_op_at(l, sdc.op), (uint32_t)w);
}
static uint32_t sdc_bank_avail(void)
{
    if (!sdc_listing() || !sdm_ready())
        return 0u;
    return (sdm_page() ? 1u << B_OCTDN : 0u) | (sdm_page() + 1u < sdm_pages() ? 1u << B_OCTUP : 0u);
}
static void sdc_bank_tap(uint32_t up)
{
    char b[8];
    if (!((sdc_bank_avail() >> (up ? B_OCTUP : B_OCTDN)) & 1u))
        return;
    sdc_page_turn(up ? 1 : -1);
    fmt_int(b, (int32_t)sdm_page() + 1);
    str_app(b, "/", sizeof b);
    fmt_app(b, (int32_t)sdm_pages());
    ui.toast_next = 1;
    ui_say("PAGE ", b);
    ui.toast_next = 0;
}
static int sdc_home(void)                               /* HOME: up a folder, SAVES back to the projects; 1 done */
{
    uint32_t l = sdc_here();
    if (l >= SDC_N || !sdm_ready())
        return 0;
    if (sdc.view[l] == SDV_VERSIONS) {
        sdc_view_set(l, SDV_FOLDER);
        return 1;
    }
    if (sdm_at_top() || !sdm_up())
        return 0;
    sdc.item = 0;
    ui.force = 1;
    return 1;
}
static void sdc_keys_lit(uint32_t *f, uint32_t *d, uint32_t blink)   /* a file full, a folder dim, the ITEM blinking */
{
    uint32_t w, u = 0, fr = 0;
    if (!sdm_ready())
        return;
    for (w = 0; w < sdm_count() && w < SDC_KEYS; w++) {
        if (w == sdc.item && !blink)
            continue;
        if (sdc_is_dir(w))
            fr |= 1u << w;
        else
            u |= 1u << w;
    }
    *f |= keys_of(u);
    *d |= keys_of(fr);
}

/* ---- the page: two columns of eight, the slots as the keys sit; a folder's name grey with a "/", a cut name with "~" */
#define SDC_ROW 18
static void sdc_paint(void)
{
    char b[20], fold[24];
    uint32_t k, dir, cut, n;
    const char *s;
    if (!sdm_ready()) {
        cv_texts(80, 40, "No SD card", C_WARN);
        return;
    }
    if (!sdm_count()) {
        sdm_folder(fold, sizeof fold);
        cv_texts(4, 40, op_case(b, fold, sizeof b), C_GRAY);
        cv_texts(4, 60, "Empty", C_DIM);
        return;
    }
    for (k = 0; k < SDC_KEYS; k++) {
        int32_t x = (int32_t)(k >> 3) * 120, y = (int32_t)(k & 7u) * SDC_ROW;
        if (!(s = sdm_slot(k, &dir, &cut)))
            continue;
        dir = sdc_is_dir(k);
        str_cpy(b, s, 15u - dir);                       /* (14 characters: 112 px of the column's 120) */
        n = str_len(b);
        if (cut || str_len(s) > n)
            b[n ? n - 1u : 0u] = '~';
        if (dir)
            str_app(b, "/", sizeof b);
        cv_texts(x + 4, y + 1, b, k == sdc.item ? C_WHITE : dir ? C_GRAY : C_HI);
        if (k == sdc.item)
            cv_frame(x, y, 120, SDC_ROW, C_WHITE);
    }
}
static void sdc_draw(uint32_t y, uint32_t h)
{
    uint32_t k, sig = hu(hu(hu(0x5Du, sdc_here()), sdm_ready()), sdc.item);
    char fold[24];
    sdm_folder(fold, sizeof fold);
    sig = hs(hu(hu(sig, sdm_page()), sdm_count() | h << 8), fold);
    for (k = 0; k < sdm_count() && k < SDC_KEYS; k++)
        sig = hs(sig, sdc_name(k));
    sig |= 1u;
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(y, h, C_BLACK, sdc_paint);
}
