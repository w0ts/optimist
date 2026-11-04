/* SPDX-License-Identifier: GPL-3.0-only */
/* SD card mode's lists on PROJECT (FELUCCA_SD_UI; docs/UI-OPTIMIST-DESIGN.md 4.14, docs/SD-CARD-DESIGN.md 9; the user,
 * 2026-10-26: projects saved and loaded by name, every list as banks of 16 on the white keys). Rows PROJECT (with a card
 * ready: the card's projects; without: the flash slot as before), PRESETS, KITS, SAMPLES. A list row is two big cards of
 * the four, the pair of the knob last turned (every knob works whichever shows: op_layers.c cards_2x1), and the page under them, two columns of eight names as the keys sit (the left octave, then the right):
 *   KNOB 1 BANK   the bank (a list with no banks: its pages of 16, A, B ..; OCT- / OCT+ tapped too)
 *   KNOB 2 ITEM   the slot, across the pages                   KNOB 3 ACTION   LOAD (the default), SAVE, DELETE, PROJECTS NEW; SAMPLES empty, USE
 *   KNOB 4 VIEW   PROJECTS: NAME, SAVES (the item's versions); PRESETS, KITS: FOLDER, NAME; SAMPLES: FOLDER, NAME, RECENT
 * The two first never change meaning (the user, 2026-11-05). On PRESETS and SAMPLES ALGORITHM selects the track as on
 * the normal pages (T1 .. DR, the lanes; the header shows it, a load goes to it), the PRESETS list following its engine;
 * on PROJECTS and KITS it does nothing, as before. On PRESETS the PRESETS knob filters by engine (sdc_presets_turn).
 * A white key picks its slot and asks the action on it, the slot's name on screen (a folder: into it, no question); the
 * same key again, or YES, runs it; another key asks about that one instead (the user, 2026-10-26: "one press shows what
 * it is and a second press actually loads"). HOME goes up a folder (SAVES: back to the projects), at the list's own folder back to the mixer. SAVE names first
 * (NAME, the slot's name or the work's; the card's rules: sd_layout.c sdl_name_ok); a project has versions, a preset or a
 * kit is written over only when asked over its own slot. USE: the selected synth part's sample, by its hash (the player
 * reads it: storage/sd/sd_mode.c sdm_sample_of). The card's side is storage/sd/sd_mode.c (included after the stores). */
enum { SDC_PROJ, SDC_PRE, SDC_KIT, SDC_SMP, SDC_N };   /* the card's lists */
enum { SDV_FOLDER, SDV_NAME, SDV_RECENT, SDV_VERSIONS };   /* the views (sd_browse.c's, and a project's versions) */
enum { SDO_LOAD, SDO_SAVE, SDO_DELETE, SDO_USE,       /* (ACTION) */
       SDO_SAVE_LOAD,                                   /* "SAVE <the work>?" before a LOAD (op_prjflow.c PO_SAVE_LOAD is 3: USE) */
       SDO_NEW, SDO_SAVE_NEW,                           /* PROJECTS: the work blank, no file touched (TOOLS' NEW); "SAVE <the work>?" first */
       SDO_N };
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
static const char *sdm_rel_of(uint32_t k);              /* (a project's place under Projects/: "A/03 - song") */
static void sdm_work_of(char *out, uint32_t cap);
static void sdm_save_at(uint32_t k);
#define SDM_NSAMPLERS 4u
static uint32_t sdm_samplers(uint8_t *e);               /* (a card WAV's engines: SAMPLE, SLICE, GRAIN, STRETCH) */
static int sdm_part_samples(uint32_t part);
static void sdm_use_with(uint32_t e);
static int32_t sdm_pre_tree(void);                      /* (the PRESETS list by engine) */
static int sdm_pre_show(uint32_t w, uint32_t e, uint32_t bk);
static uint32_t sdm_pre_mask(uint32_t w);
static void sdm_all_open(uint32_t w, const uint32_t *list, uint32_t n);
static int sdm_all_go(uint32_t v);
static void sdm_all_sync(void);
static uint32_t sdm_all_at(void);
static uint32_t sdm_all_total(void);
static int32_t sdm_bank_shown(void);                    /* (banks of 16: the letter shown, 0 = A; -1 none) */
static int sdm_bank_go(uint32_t bk);
static uint32_t sdm_shown_page(void);
static int sdm_reading(void);                           /* (a page asked and still coming: sd_mode.c sdm_read_pump) */
static uint32_t sdm_read_ms(void);
static void sdm_wait(void);
static void sdm_pv_play(uint32_t k);                    /* (the SAMPLES list's preview) */
static void sdm_pv_stop(void);
static int sdm_pv_busy(void);
static int sdm_into(uint32_t k);
static void sdm_load_later(uint32_t k);
static int sdm_up(void);
static int sdm_at_top(void);
static void sdm_folder(char *b, uint32_t cap);
static void sdm_do(uint32_t op, uint32_t k);
static int sdm_save(uint32_t list, const char *name, const char *was);
static void sdm_load_name(const char *name);
static void sdm_new(void);
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
static uint32_t sd_clock_khz(void);                     /* storage/sd/sdcard.c: the card's SPI clock (0: unknown) */
static int32_t sdm_index_pct(void);                     /* storage/sd/sd_mode.c: how far the index's pass is, -1 none */
static void name_card(uint32_t list, const char *prefill, const char *was);   /* op_name.c */
static void sec_name(uint32_t s, char *b);              /* storage/sections/sections.c */

static struct {
    uint8_t open, stale, ready, item, op, tried;        /* the list in sd_mode.c's page (SDC_N none); read again; the card
                                                         * ready when it was read; the cursor's slot; the action; a mount tried */
    uint8_t view[SDC_N];
    char vname[SDC_NAME];                      /* SAVES: whose versions */
    char unit[8];                                       /* BANK's "/5" (a list's pages) */
    uint8_t flt, eng;                                   /* PRESETS' filter: 0 the track's engine (eng: the one shown), 1 ALL,
                                                         * 2 + a slot that engine; until the row is left */
    uint32_t fmask;                                     /* the engines on the card (sdm_pre_mask), 0 not read */
    uint8_t loading;                                    /* the LOADING toast up (sdc_loading) */
    uint8_t pv, pv_ask, pv_k;                           /* SAMPLES: PREVIEW on (RAM only); K2 moved, the item to hear; the key heard */
    uint32_t ftree[2];                                  /* fmask by tree: Factory's, User's */
} sdc = {SDC_N, 1, 0, 0, 0, 0, {SDV_FOLDER, SDV_FOLDER, SDV_FOLDER, SDV_FOLDER}, "", "", 0, 0xFFu, 0};
static const char *const SDV_NAME_S[2][4] = {{"FOLDER", "NAME", "RECENT", "SAVES"}, {"NAME", "NAME", "NAME", "SAVES"}};
static const char *const SDO_NAME_S[SDO_N] = {[SDO_LOAD] = "LOAD", [SDO_SAVE] = "SAVE", [SDO_DELETE] = "DELETE", [SDO_USE] = "USE", [SDO_NEW] = "NEW"};
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
static uint32_t sdc_nops(uint32_t l) { return l == SDC_SMP ? 1u : l != SDC_PROJ ? 3u : sdc.view[l] == SDV_VERSIONS ? 2u : 4u; }
static uint32_t sdc_op_at(uint32_t l, uint32_t i)
{
    return l == SDC_SMP ? SDO_USE : sdc_nops(l) == 2u && i ? SDO_DELETE : i == 3u ? SDO_NEW : i;
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
    if (sdc.stale || sdc.open != l || sdc.ready != ready) {
        (void)sdm_open(l, sdc.view[l], sdc.vname);
        sdc.open = (uint8_t)l;
        sdc.stale = 0;
        sdc.ready = (uint8_t)ready;
        if (sdc.item >= SDC_KEYS)
            sdc.item = 0;
    }
    if (l == SDC_PRE && ready && !sdc.flt && sdc.view[l] == SDV_FOLDER && !is_drum(TSEL) && sdc.eng != TSEL->eng_req) {
        sdc.eng = TSEL->eng_req;                        /* the list follows the track's engine (a tree shown: Factory, User) */
        if (sdm_pre_tree() >= 0 && sdm_pre_show((uint32_t)sdm_pre_tree(), sdc.eng, 0))
            sdc.item = 0;
    }
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
/* slot k a folder to go into (a project's folder is the project; a bank's folder, 2, is gone into in every list) */
static uint32_t sdc_is_dir(uint32_t k)
{
    uint32_t dir = 0, cut;
    return sdm_slot(k, &dir, &cut) && dir && (dir == 2u || sdc.open != SDC_PROJ || sdc.view[SDC_PROJ] == SDV_VERSIONS);
}
static uint32_t sdc_used(uint32_t k)                    /* slot k holds something (a bank's free number does not) */
{
    uint32_t dir, cut;
    return k < sdm_count() && sdm_slot(k, &dir, &cut) != 0;
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
    if (k == 0u && l == SDC_PRE && sdc.flt == 1u) {     /* ALL: the engines' banks one after the other: "ACID B" */
        c->label = "BANK";
        c->kind = CK_VAL;
        sdm_folder(c->val, sizeof c->val);
        cell_gauge(c, 0, 0, (int32_t)(sdm_all_total() ? sdm_all_total() - 1u : 0u), (int32_t)sdm_all_at());
    } else if (k == 0u && sdm_bank_shown() >= 0) {      /* a bank of 16 (presets, projects, kits): its letter, A .. Z */
        c->label = "BANK";
        c->kind = CK_VAL;
        c->val[0] = (char)('A' + sdm_bank_shown());
        c->val[1] = 0;
        cell_gauge(c, 0, 0, 25, sdm_bank_shown());
    } else if (k == 0u) {                               /* no banks (a folder, a view): its pages of 16 read as banks */
        c->label = "BANK";
        c->kind = CK_VAL;
        if (sdm_page() < 26u)
            c->val[0] = (char)('A' + sdm_page()), c->val[1] = 0;
        else
            fmt_int(c->val, (int32_t)sdm_page() + 1);
        sdc.unit[0] = '/';
        fmt_int(sdc.unit + 1, (int32_t)sdm_pages());
        c->unit = sdc.unit;
        cell_gauge(c, 0, 0, (int32_t)sdm_pages() - 1, (int32_t)sdm_page());
    } else if (k == 2u) {
        if (l == SDC_SMP)
            return;                                     /* (K3 empty on SAMPLES: one action, USE, on the keys and YES) */
        c->label = "ACTION";
        c->kind = CK_VAL;
        cell_val(c, SDO_NAME_S[op]);
        c->col = op == SDO_DELETE || op == SDO_NEW || (op == SDO_SAVE && sdc_used(sdc.item)) ? C_ERR : 0;
        cell_gauge(c, 1, 0, (int32_t)sdc_nops(l) - 1, (int32_t)sdc.op);
    } else if (k == 1u) {
        c->label = "ITEM";
        c->kind = CK_VAL;
        if (sdc_used(sdc.item))
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
static void sdc_page_turn(int32_t d)                    /* (a bank shown: the next or the one before, A .. Z) */
{
    int32_t bk = sdm_bank_shown();
    if (sdc.open == SDC_PRE && sdc.flt == 1u) {         /* ALL: the next bank of all the engines' */
        if ((int32_t)sdm_all_at() + d >= 0 && sdm_all_at() + (uint32_t)(d > 0) < sdm_all_total())
            (void)sdm_all_go((uint32_t)((int32_t)sdm_all_at() + d));
        return;
    }
    if (bk >= 0) {
        if (bk + d >= 0 && bk + d < 26)
            (void)sdm_bank_go((uint32_t)(bk + d));
        return;
    }
    sdm_turn(d);                                        /* (the page comes when it is read; never ui.force, a black screen a
                                                         * detent: the bands see their own change) */
}
static void sdc_view_set(uint32_t l, uint32_t v)        /* VIEW: the list again (SAVES: the item's project, else the work's) */
{
    if (v == SDV_VERSIONS) {
        if (sdc_used(sdc.item))                         /* (the projects' list: the item is a project) */
            str_cpy(sdc.vname, sdm_rel_of(sdc.item), sizeof sdc.vname);
        else
            sdm_work_of(sdc.vname, sizeof sdc.vname);
    }
    sdc.view[l] = (uint8_t)v;
    sdc.op = 0;
    sdc.item = 0;
    (void)sdm_open(l, v, sdc.vname);
    sdc.open = (uint8_t)l;
    sdc.stale = 0;
    ui.redraw = 1;
}
static void sdc_turn(uint32_t kind, uint32_t k, int32_t s)
{
    uint32_t l = sdc_list_of(kind);
    int32_t d = s > 0 ? 1 : -1;
    if (!sdm_ready() || l >= SDC_N)
        return;
    sdc_sync(l);
    if (k == 2u) {
        sdc.op = (uint8_t)(s == OP_RESET ? 0 : step_in(sdc.op, d, (int32_t)sdc_nops(l)));
        return;
    }
    if (s == OP_RESET)
        return;
    if (k == 0u) {
        sdc_page_turn(d);
    } else if (k == 1u) {                               /* ITEM: across the pages, the page following */
        if (d > 0 && sdc.item + 1u >= SDC_KEYS && sdm_page() + 1u < sdm_pages()) {
            sdc_page_turn(1);
            sdc.item = 0;
        } else if (d < 0 && !sdc.item && sdm_page()) {
            sdc_page_turn(-1);
            sdc.item = SDC_KEYS - 1u;
        } else {
            sdc.item = (uint8_t)step_in(sdc.item, d, (int32_t)(sdm_count() ? sdm_count() : 1u));
        }
        sdc.pv_ask = l == SDC_SMP && sdc.pv;            /* (heard when the page is here: sdc_frame) */
    } else {
        sdc_view_set(l, sdc_view_at(l, (uint32_t)step_in((int32_t)sdc_view_ix(l), d, (int32_t)sdc_views(l))));
    }
}

/* ---- a sample's USE (the user, 2026-11-05): the part on a sampler: into it, no question; on another engine with more
 * than one sampler built: the engine popup (op_preset.c), the samplers alone, knobs SELECT, PRESETS or ITEM, YES or the same
 * key picks, HOME cancels (nothing changed); one sampler: that one. A sampler is what plays a card WAV (sd_mode.c
 * sdm_samplers: SAMPLE, SLICE, GRAIN, STRETCH); a drum track: its lane, as before */
static struct {
    uint8_t on, at, k, n;
    uint8_t e[SDM_NSAMPLERS];
} sdc_pk;
static void sdc_use_now(uint32_t k, uint32_t e)
{
    if (e < NENGINES)
        sdm_use_with(e);
    ui.toast_next = 1;
    sdm_do(SDO_USE, k);
    ui.toast_next = 0;
    ui.redraw = 1;
}
static void sdc_use(uint32_t k)
{
    sdc_pk.n = (uint8_t)sdm_samplers(sdc_pk.e);
    if (song.sel >= NPART || sdc_pk.n < 2u || sdm_part_samples(song.sel)) {
        sdc_use_now(k, NENGINES);
        return;
    }
    sdc_pk.on = 1;
    sdc_pk.at = 0;
    sdc_pk.k = (uint8_t)k;
}
static int sdc_pick_on(void) { return sdc_pk.on; }
static void sdc_pick_show(void)                         /* the popup held open */
{
    ui.toast_t = OP_TOAST_FRAMES;
    ui.toast_col = ENG_COL[sdc_pk.e[sdc_pk.at % SDM_NSAMPLERS]];
    pop_kind = 4;
}
static void sdc_pick_end(void)
{
    sdc_pk.on = 0;
    ui.toast_t = 0;
    pop_kind = 0;
    ui.redraw = 1;
}
static void sdc_pick_frame(uint32_t pressed)            /* one UI frame of the pick (op_input.c ui_input) */
{
    uint32_t k;
    int32_t s = panel_enc(EN_PRESET) + panel_enc(EN_SELECT) + panel_enc(EN_K2), go = 0;
    for (k = 0; k < 4u; k++)
        (void)panel_enc(EN_K1 + k);                     /* (no stray detents left) */
    (void)panel_enc(EN_ALGO);
    if (s)
        sdc_pk.at = (uint8_t)ui_clamp((int32_t)sdc_pk.at + (s > 0 ? 1 : -1), 0, (int32_t)sdc_pk.n - 1);
    while (lk_r != lk_w) {
        uint32_t e = lk_q[lk_r % LKQ];
        lk_r++;
        if (((e >> 7) & 1u) && (e >> 8) == LY_STEP && punch_key(e & 0x7Fu) == (int32_t)sdc_pk.k)
            go = 1;                                     /* (the sample's key again: as YES) */
    }
    if (pressed & (1u << panel.btn[B_HOME])) {
        sdc_pick_end();
        return;
    }
    if (go || (pressed & (1u << panel.btn[B_SAVE]))) {
        k = sdc_pk.e[sdc_pk.at % SDM_NSAMPLERS];
        sdc_pick_end();
        sdc_use_now(sdc_pk.k, k);
        return;
    }
    sdc_pick_show();
}
/* the engine popup's list here (op_preset.c pop_engines; an entry >= NENGINES: ALL) */
static void sdc_pop_list(uint32_t *list, uint32_t *n, uint32_t *at)
{
    uint32_t i, m = 0, a = 0, cur = sdc.flt >= 2u ? sdc.flt - 2u : TSEL->eng_req % NENGINES;
    if (sdc_pk.on) {
        for (*n = 0; *n < sdc_pk.n; (*n)++)
            list[*n] = sdc_pk.e[*n];
        *at = sdc_pk.at;
    } else if (sdc_here() == SDC_PRE) {                 /* the PRESETS filter: ALL, the engines on the card */
        for (i = 0; i < *n; i++)
            if (sdc.fmask >> list[i] & 1u) {
                list[m++] = list[i];
                if (list[m - 1u] == cur)
                    a = m;
            }
        for (i = m; i; i--)
            list[i] = list[i - 1u];
        list[0] = NENGINES;
        *n = m + 1u;
        *at = sdc.flt == 1u ? 0u : a;
    }
}
static uint32_t sdc_pop_sig(void) { return sdc_pk.at * 32u + sdc.flt * 256u; }
/* a card's PRESETS, the PRESETS knob: which engine's presets (the user, 2026-11-05): ALL or an engine that has a folder on
 * the card; the track's engine by default, a choice stays until the row is left (ALGO then moves the target only). ALL:
 * every engine's banks one after the other in the tree shown (sd_mode.c sdm_all_*). 1: taken */
static void pop_engines(uint32_t *list, uint32_t *n, uint32_t *at);
static int sdc_presets_turn(int32_t s)
{
    uint32_t l[34], n, at, w;
    if (sdc_here() != SDC_PRE || !sdm_ready() || op_armed())
        return 0;
    if (sdc.view[SDC_PRE] != SDV_FOLDER) {              /* (NAME: the filter is FOLDER's; the knob never the track's own presets here) */
        sdc.view[SDC_PRE] = SDV_FOLDER;
        sdc.stale = 1;
    }
    sdc_sync(SDC_PRE);
    if (!sdc.fmask) {
        sdc.ftree[0] = sdm_pre_mask(0);
        sdc.ftree[1] = sdm_pre_mask(1);
        sdc.fmask = sdc.ftree[0] | sdc.ftree[1];
    }
    pop_engines(l, &n, &at);
    at = (uint32_t)ui_clamp((int32_t)at + (s > 0 ? 1 : -1), 0, (int32_t)n - 1);
    w = sdm_pre_tree() == 0 ? 0u : 1u;                  /* (neither tree shown: User, as the list opens) */
    if (l[at] >= NENGINES ? !sdc.ftree[w] : !(sdc.ftree[w] >> l[at] & 1u))
        w = !w;                                         /* (what was picked is only in the other tree: a card whose User is
                                                         * empty showed Empty for every engine) */
    sdc.flt = (uint8_t)(l[at] >= NENGINES ? 1u : 2u + l[at]);
    if (sdc.flt == 1u)
        sdm_all_open(w, l + 1, n - 1u);
    else
        (void)sdm_pre_show(w, l[at], 0);
    sdc.item = 0;
    ui.toast_next = 1;
    ui_message(l[at] >= NENGINES ? "ALL" : ENGINES[l[at]]->name);
    ui.toast_next = 0;
    ui.toast_col = l[at] >= NENGINES ? C_HI : ENG_COL[l[at]];
    pop_kind = 4;
    ui.redraw = 1;
    return 1;
}
/* a card's PRESETS or SAMPLES: a page with the track (ALGORITHM, the header) */
static int sdc_track_page(void) { return (sdc_here() == SDC_PRE || sdc_here() == SDC_SMP) && sdm_ready(); }
/* a page not here SDC_LOADING_MS after it was asked: the toast says so until it is (the user, 2026-11-05); until then the
 * old page stays, nothing shows on a read that makes it */
#define SDC_LOADING_MS 200u
static void sdc_loading(void)
{
    if (sdm_read_ms() >= SDC_LOADING_MS && sdc_listing() && !op_armed() && !(ui.toast_t && pop_kind)) {
        if (!sdc.loading || !ui.toast_t) {
            ui.toast_next = 1;
            ui_message("LOADING");
            ui.toast_next = 0;
            sdc.loading = 1;
        }
        ui.toast_t = OP_TOAST_FRAMES;
    } else if (sdc.loading && !sdm_reading()) {
        sdc.loading = 0;
        if (!pop_kind)
            ui.toast_t = 0;
    }
}
static void sdc_pv_item(void)                           /* PREVIEW on, K2 moved: the item heard; a folder or an empty slot: silence */
{
    sdc.pv_ask = 0;
    if (sdc_used(sdc.item) && !sdc_is_dir(sdc.item)) {
        sdm_pv_play(sdc.item);
        sdc.pv_k = sdc.item;
    } else {
        sdm_pv_stop();
    }
}
static void sdc_frame(void)                             /* every UI frame: the filter ends with the row */
{
    sdc_loading();
    if (sdc_here() != SDC_SMP && sdm_pv_busy())
        sdm_pv_stop();                                  /* (the page left: silence; PREVIEW stays on for the session) */
    if (sdc.pv_ask && !sdm_reading() && sdc_here() == SDC_SMP)
        sdc_pv_item();
    if (!sdm_reading() && sdc.item >= sdm_count() && sdm_count())
        sdc.item = (uint8_t)(sdm_count() - 1u);         /* (a shorter last page, come) */
    if (sdc.flt && sdc_here() != SDC_PRE) {
        sdc.flt = 0;
        sdc.eng = 0xFFu;
    }
    if (sdc.fmask && sdc_here() != SDC_PRE)
        sdc.fmask = 0;
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
    if (sdc_used(k) && !sdc_is_dir(k))
        str_cpy(nm, l == SDC_PROJ ? sdm_base_of(k) : sdc_name(k), sizeof nm);
    else if (l == SDC_PROJ)
        sec_name(0, nm);
    sdm_save_at(k);                                     /* (a bank's free slot: its number for the save) */
    name_card(l, nm, sdc_used(k) ? nm : "");
}
static void sdc_ask(uint32_t r, uint32_t l, uint32_t op, uint32_t k)
{
    char a[16], cur[SDC_PNAME];
    if (op == SDO_NEW) {                                /* (any slot: it acts on none) */
        prj_arm_slot = (uint8_t)k;
        sec_name(0, cur);
        if (work_changed() && cur[0] && !song.playing && !transport_req) {
            prj_arm_op = SDO_SAVE_NEW;                  /* "SAVE <the work's name>?" first, as a LOAD's */
            op_arm(SCR_PROJECT, r, 0, "SAVE", cur, 1);
        } else {
            prj_arm_op = SDO_NEW;
            op_arm(SCR_PROJECT, r, 0, "NEW", "PROJECT", 1);
        }
        return;
    }
    if (op == SDO_SAVE && !sdc_used(k)) {
        sdc_name_for(l, k);
        return;
    }
    if (!sdc_used(k)) {
        ui.toast_next = 1;
        ui_message("EMPTY");
        ui.toast_next = 0;
        return;
    }
    if (sdc_is_dir(k)) {
        if (sdm_into(k))                                /* a folder: into it, no question */
            sdc.item = 0, ui.redraw = 1;
        return;
    }
    if (op == SDO_USE) {
        sdc_use(k);                                     /* (a sample: no question) */
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
    if (ui.arm_scr != SCR_PROJECT || (prj_arm_op != SDO_SAVE_LOAD && prj_arm_op != SDO_SAVE_NEW) || sdc_here() != SDC_PROJ)
        return 0;
    if (prj_arm_op == SDO_SAVE_NEW) {
        prj_arm_op = SDO_NEW;
        op_arm(SCR_PROJECT, ui.arm_row, 0, "NEW", "PROJECT", 1);
        return 1;
    }
    prj_arm_op = SDO_LOAD;
    op_arm(SCR_PROJECT, ui.arm_row, 0, "LOAD", sdc_name(prj_arm_slot), 1);
    return 1;
}
static void sdc_new(void)                               /* YES to NEW: TOOLS' NEW; the card's project no longer the open one */
{
    (void)op_global_go(G_NEWPRJ);
    sdm_new();
}
static int sdc_yes(uint32_t r, uint32_t kind, uint32_t ok)
{
    uint32_t l = sdc_list_of(kind), op, k;
    char cur[SDC_PNAME];
    if (!sdm_ready() || sdc.loading)
        return 1;
    sdc_sync(l);
    sdm_wait();
    if (!ok) {
        sdc_ask(r, l, sdc_op_at(l, sdc.op), sdc.item);
        return 1;
    }
    op = prj_arm_op, k = prj_arm_slot;
    if (op == SDO_SAVE_LOAD) {                          /* YES to "SAVE <the work>?": saved by its name, then loaded */
        char want[SDC_NAME];
        str_cpy(want, sdm_rel_of(k), sizeof want);      /* (its place: "A/03 - song") */
        sec_name(0, cur);
        sdm_save_at(0xFFu);                             /* (no slot asked: the work's own place) */
        if (!sdm_save(SDC_PROJ, cur, cur))
            sdm_load_name(want);                        /* (the card's copy whole first: never the flash copy's only) */
    } else if (op == SDO_SAVE_NEW) {                    /* YES to "SAVE <the work>?": saved by its name, then blank */
        sec_name(0, cur);
        sdm_save_at(0xFFu);
        if (!sdm_save(SDC_PROJ, cur, cur))
            sdc_new();
    } else if (op == SDO_NEW) {
        sdc_new();
    } else if (op == SDO_SAVE) {
        sdc_name_for(l, k);
    } else if (op == SDO_LOAD && l == SDC_PROJ && (song.playing || transport_req)) {
        sdm_load_later(k);                              /* (stopped, loaded, played again: op_edits.c pj_tick) */
    } else {
        sdm_do(op, k);
    }
    sdc.op = 0;
    sdc.stale = 1;                                      /* (what it changed: read again) */
    ui.redraw = 1;
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
    if (sdc.loading)
        return;                                         /* (the page it would pick is not here yet) */
    sdc_sync(l);
    sdm_wait();
    if (l == SDC_SMP && sdc.pv && w >= 0 && (uint32_t)w < sdm_count() && sdc_used((uint32_t)w) && !sdc_is_dir((uint32_t)w)) {
        sdc.item = (uint8_t)w;                          /* PREVIEW on: a key selects and plays, again stops; USE is YES's */
        ui.hot = 1;
        ui.hot_lit = 1;
        if (sdc.pv_k == (uint8_t)w && sdm_pv_busy()) {
            sdm_pv_stop();
        } else {
            sdm_pv_play((uint32_t)w);
            sdc.pv_k = (uint8_t)w;
        }
        return;
    }
    sdc.item = (uint8_t)w;
    ui.hot = 1;                                         /* (K2: ITEM) */
    ui.hot_lit = 1;
    ui.redraw = 1;
    sdc_ask(ui.row[SCR_PROJECT], l, sdc_op_at(l, sdc.op), (uint32_t)w);
}
static uint32_t sdc_bank_avail(void)
{
    int32_t bk = sdm_bank_shown();
    if (!sdc_listing() || !sdm_ready())
        return 0u;
    if (bk >= 0)                                        /* a bank: OCT the next one or the one before (A .. Z) */
        return (bk ? 1u << B_OCTDN : 0u) | (bk < 25 ? 1u << B_OCTUP : 0u);
    return (sdm_page() ? 1u << B_OCTDN : 0u) | (sdm_page() + 1u < sdm_pages() ? 1u << B_OCTUP : 0u);
}
static void sdc_bank_say(void)                          /* "BANK C" */
{
    char b[2] = {(char)('A' + sdm_bank_shown()), 0};
    ui.toast_next = 1;
    ui_say("BANK ", b);
    ui.toast_next = 0;
}
static void sdc_page_say(void)                          /* "BANK C/6": a list with no banks, its pages as letters */
{
    char b[8];
    b[0] = (char)('A' + sdm_page() % 26u);
    b[1] = 0;
    str_app(b, "/", sizeof b);
    fmt_app(b, (int32_t)sdm_pages());
    ui.toast_next = 1;
    ui_say("BANK ", b);
    ui.toast_next = 0;
}
/* SAMPLES' black keys (the user, 2026-11-05): the first ten its pages A .. J in every view (K1 and OCT reach the rest),
 * the top F# PREVIEW on or off, lit while on (sd_preview.h: the voice) */
#define SDC_PV_KEY 10u
static void sdc_smp_key(uint32_t b)
{
    if (b == SDC_PV_KEY) {
        sdc.pv = !sdc.pv;
        sdc.pv_ask = 0;
        if (!sdc.pv)
            sdm_pv_stop();
        ui.toast_next = 1;
        ui_message(sdc.pv ? "PREVIEW ON" : "PREVIEW OFF");
        ui.toast_next = 0;
    } else if (b < sdm_pages() && b != sdm_page()) {
        sdm_turn((int32_t)b - (int32_t)sdm_page());
        sdc.item = 0;
        sdc.pv_k = 0xFFu;
        sdc_page_say();
    }
}
/* a black key on a list of banks (the user, 2026-11-04: one black key a bank): the 11 black keys of the 27 (F# G# A#
 * C# D# twice, the top F#) are banks A .. K, in key order; OCT goes on to L .. Z (sdc_bank_tap). Black key b of the
 * keyboard (0 .. 10) */
static void sdc_bank_key(uint32_t kind, uint32_t b)
{
    uint32_t l = sdc_list_of(kind);
    if (!sdm_ready() || l >= SDC_N || b > 10u || op_armed())
        return;
    sdc_sync(l);
    if (l == SDC_SMP) {
        sdc_smp_key(b);
        return;
    }
    if (sdm_bank_go(b)) {
        if (sdc.flt == 1u)
            sdm_all_sync();                             /* (ALL: this engine's bank, its place among them all) */
        sdc.item = 0;
        sdc_bank_say();
    }
}
static uint32_t sdc_bank_lit(void)                      /* the black key of the bank shown (A .. K), 0 none */
{
    static const uint8_t B[5] = {1, 3, 5, 8, 10};       /* F# G# A# C# D# from F (punch_key's white keys between) */
    int32_t bk = sdc.open == SDC_SMP ? (sdm_page() < SDC_PV_KEY ? (int32_t)sdm_page() : -1) : sdm_bank_shown();
    uint32_t m = bk >= 0 && bk < 11 ? 1u << ((uint32_t)bk / 5u * 12u + B[bk % 5]) : 0u;
    return sdc.open == SDC_SMP && sdc.pv ? m | 1u << (SDC_PV_KEY / 5u * 12u + B[0]) : m;
}
static void sdc_bank_tap(uint32_t up)
{
    if (!((sdc_bank_avail() >> (up ? B_OCTUP : B_OCTDN)) & 1u))
        return;
    sdc_page_turn(up ? 1 : -1);
    if (sdm_bank_shown() >= 0)
        sdc_bank_say();
    else
        sdc_page_say();
}
static int sdc_home(void)                               /* HOME: up a folder, SAVES back to the projects; 1 done */
{
    uint32_t l = sdc_here();
    if (l >= SDC_N || !sdm_ready())
        return 0;
    sdm_wait();
    if (sdc.view[l] == SDV_VERSIONS) {
        sdc_view_set(l, SDV_FOLDER);
        return 1;
    }
    if (sdm_at_top() || !sdm_up())
        return 0;
    sdc.item = 0;
    ui.redraw = 1;
    return 1;
}
static void sdc_keys_lit(uint32_t *f, uint32_t *d, uint32_t blink)   /* a file full, a folder dim, the ITEM blinking */
{
    uint32_t w, u = 0, fr = 0;
    if (!sdm_ready())
        return;
    for (w = 0; w < sdm_count() && w < SDC_KEYS; w++) {
        if ((w == sdc.item && !blink) || !sdc_used(w))
            continue;                                   /* (a bank's free number: dark) */
        if (sdc_is_dir(w))
            fr |= 1u << w;
        else
            u |= 1u << w;
    }
    *f |= keys_of(u) | sdc_bank_lit();                  /* (the bank shown: its black key) */
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
    for (k = 0; k < sdm_count() && k < SDC_KEYS && !sdc_used(k); k++)
        ;
    if (k == sdm_count() || k == SDC_KEYS) {            /* (nothing, or a bank with every number free) */
        if (!sdm_count() && sdm_reading())
            return;                                     /* (another list on its way: not "Empty") */
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
    sig = hs(hu(hu(sig, sdm_shown_page()), sdm_count() | h << 8 | (uint32_t)(!sdm_count() && sdm_reading()) << 16), fold);
    for (k = 0; k < sdm_count() && k < SDC_KEYS; k++)
        sig = hs(sig, sdc_name(k));
    sig |= 1u;
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(y, h, C_BLACK, sdc_paint);
}
