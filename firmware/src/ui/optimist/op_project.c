/* SPDX-License-Identifier: GPL-3.0-only */
/* PROJECT and SYSTEM (docs/UI-OPTIMIST-DESIGN.md sections 4.6, 4.7). An action cell does nothing when turned: its
 * knob makes it the hot cell, YES does it, and what destroys asks first (op_state.c op_arm). Then the screen table. */

static void name_user(uint32_t k);                      /* op_name.c: NAME, then the save */
static void name_project(uint32_t k);

/* ---- PROJECT */
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
static const char *const PRJ_NAME[] = {"PROJECT", "SNAPSHOT", "USER", "TOOLS", "SLOOP 2.4", "MEMORY"};
#if SEC_LOGGED
static void sec_mem(uint32_t *pct, uint32_t *more);    /* sections.c: the section log's MEM gauge */
static void prj_mem(cell_t *c, uint32_t k)             /* MEMORY: USED (a bar) and MORE (sections that fit) */
{
    static uint32_t t, pct, more;
    if (!t || fm1_ms - t > 500u) {                      /* (a model of the log: counted twice a second at most) */
        t = fm1_ms | 1u;
        sec_mem(&pct, &more);
    }
    c->label = k ? "MORE" : "USED";
    c->kind = CK_RO;
    fmt_int(c->val, (int32_t)(k ? more : pct));
    c->unit = k ? "" : "%";
    c->col = !k && pct > 90u ? C_WARN : 0;
    cell_gauge(c, 0, 0, k ? 32 : 100, (int32_t)(k ? more : pct));
}
#endif
static const char *const PRJ_GO[3][3] = {{"LOAD", "SAVE", "NEW"}, {"LOAD", "CLEAR", "SAVE"}, {"LOAD", "ERASE", "SAVE"}};

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

static void prj_cell(uint32_t r, uint32_t k, cell_t *c)
{
    uint32_t kind = prj_kind(r);
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
        if (!k) {
            c->label = "IMPORT";
            c->kind = CK_ACT;
        }
        return;
    }
    if (k) {                                            /* the GO buttons */
        c->label = PRJ_GO[kind][k - 1u];
        c->kind = CK_ACT;
        return;
    }
    c->label = "SLOT";
    c->kind = CK_VAL;
    if (kind == PR_PROJECT) {                           /* lit when used, dim when empty */
        fmt_int(c->val, (int32_t)prj_slot() + 1);
        c->col = project_used(prj_slot()) ? 0 : C_DIM;
        cell_gauge(c, 1, 1, FELUCCA_SECTIONS, (int32_t)prj_slot() + 1);
    } else if (kind == PR_USER) {
        up_slot_label(c->val, ui.user_slot);
        c->col = up_used(ui.user_slot) ? 0 : C_DIM;
        cell_gauge(c, 1, 0, UP_SLOTS - 1, ui.user_slot);
    }
#if FELUCCA_SNAPSHOTS
    else {
        char nm[16];
        uint32_t bytes, st = sn_ui_row(ui.snap_slot, nm, &bytes);
        sn_ui_label(c->val, ui.snap_slot);
        c->col = st ? C_STATUS[st & 3u] : C_DIM;        /* (saved green, another build's amber, damaged red) */
        cell_gauge(c, 1, 0, (int32_t)sn_ui_n() - 1, ui.snap_slot);
    }
#endif
}

static void prj_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    uint32_t kind = prj_kind(r);
    if (kind == PR_TOOLS) {
        page_turn(tools_page(), k, s, fine);
        return;
    }
    if (k || s == OP_RESET || kind == PR_A24 || kind == PR_MEM)
        return;                                         /* (an action cell: the turn only made it hot) */
    if (kind == PR_PROJECT)
        val_turn(&GP[G_SLOT], &song.g[G_SLOT], k, s > 0 ? 1 : -1, 1);
    else if (kind == PR_USER)
        ui.user_slot = (uint8_t)clamp((int32_t)ui.user_slot + (s > 0 ? 1 : -1), 0, UP_SLOTS - 1);
#if FELUCCA_SNAPSHOTS
    else
        ui.snap_slot = (uint8_t)clamp((int32_t)ui.snap_slot + (s > 0 ? 1 : -1), 0, (int32_t)sn_ui_n() - 1);
#endif
}

static int prj_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    uint32_t kind = prj_kind(r), used = 0;
    char l[8];
    if (kind == PR_TOOLS)
        return page_yes(SCR_PROJECT, r, tools_page(), k, ok);
    if (kind == PR_MEM)
        return 0;
#if SL24_AUTO
    if (kind == PR_A24) {
        if (k)
            return 0;
        if (!ok)
            op_arm(SCR_PROJECT, r, k, "IMPORT", "SLOOP 2.4", 1);
        else
            sl24_auto_import();                         /* SLOOP 2.4's autosave into the work (sl24_guard.c) */
        return 1;
    }
#endif
    if (!k)
        return 0;
    if (kind == PR_PROJECT) {
        fmt_int(l, (int32_t)prj_slot() + 1);
        used = (uint32_t)project_used(prj_slot());
    } else if (kind == PR_USER) {
        up_slot_label(l, ui.user_slot);
        used = (uint32_t)up_used(ui.user_slot);
    }
#if FELUCCA_SNAPSHOTS
    else {
        char nm[16];
        uint32_t bytes;
        sn_ui_label(l, ui.snap_slot);
        used = sn_ui_row(ui.snap_slot, nm, &bytes) != 0u;
    }
#endif
    /* LOAD replaces the work, CLEAR / ERASE / NEW destroy, a SAVE over a used slot too: they ask first (the SAVE
     * cell: 2 on PROJECT, 3 on SNAPSHOT and USER) */
    if (!ok && (k != (kind == PR_PROJECT ? 2u : 3u) || used)) {
        static const char *const WHAT[3] = {"PROJECT ", "SNAPSHOT ", "PRESET "};
        char t[14];                                     /* the target: "PROJECT 3", "SNAPSHOT 2", "PRESET U03" */
        str_cpy(t, WHAT[kind], sizeof t);
        if (!(kind == PR_PROJECT && k == 3u))           /* (NEW: the project, no slot) */
            str_cpy(t + str_len(t), l, sizeof t - str_len(t));
        else
            t[7] = 0;
        /* red: it destroys or replaces the work; amber: a snapshot saved into an empty slot */
        op_arm(SCR_PROJECT, r, k, PRJ_GO[kind][k - 1u], t, !(kind == PR_SNAP && k == 3u && !used));
        return 1;
    }
    if (kind == PR_PROJECT) {
        if (k == 1u)
            project_load(prj_slot());
        else if (k == 2u)
            name_project(prj_slot());                   /* (NAME first, with the section log: op_name.c) */
        else
            op_global_go(G_NEWPRJ);
    } else if (kind == PR_USER) {
        if (k == 3u && !is_drum(TSEL))
            name_user(ui.user_slot);                    /* SAVE: NAME first (op_name.c) */
        else
            up_ui(k - 1u, ui.user_slot);                /* 0 load, 1 erase, 2 save (upreset.c) */
    }
#if FELUCCA_SNAPSHOTS
    else {
        sn_ui(k - 1u, ui.snap_slot);                    /* 0 load, 1 clear, 2 save (snapshots.c) */
    }
#endif
    ui.force = 1;
    return 1;
}

/* ---- SYSTEM: SLOOP's HOME-held menu as rows (ui/sloop/ui_menu.c) */
enum { SI_NONE, SI_COLOR, SI_BRIGHT, SI_LIGHTS, SI_KEYS, SI_LOWCUT, SI_OUT, SI_IN, SI_SYNC, SI_CLOCK, SI_CH1, SI_CH2,
       SI_CH3, SI_CHD, SI_USB, SI_CPU, SI_MHZ, SI_CALIB, SI_ABOUT, SI_DMIX, SI_CARDS, SI_HOLD };
#define SI_IF(c, i) ((c) ? (uint8_t)(i) : (uint8_t)SI_NONE)
static const struct { const char *name; uint8_t it[4]; } SYS[] = {
    {"SCREEN", {SI_COLOR, SI_IF(FELUCCA_BRIGHT, SI_BRIGHT), SI_DMIX, SI_CARDS}},   /* (SI_DMIX: the drum mixer's strips; SI_CARDS: 1x4 or 2x2) */
#if FELUCCA_LIGHTS
    {"LIGHTS", {SI_LIGHTS, SI_KEYS, SI_NONE, SI_NONE}},
#endif
    {"AUDIO", {SI_LOWCUT, SI_NONE, SI_NONE, SI_NONE}},
    {"MIDI", {SI_IF(FELUCCA_MIDI_OUT, SI_OUT), SI_IF(FELUCCA_MIDI_INCLK, SI_IN), SI_IF(FELUCCA_MIDI_CLOCK, SI_SYNC),
              SI_IF(FELUCCA_MIDI_CLOCK, SI_CLOCK)}},
    {"CHANNELS", {SI_CH1, SI_CH2, SI_CH3, SI_CHD}},
#if FELUCCA_CDC
    {"USB", {SI_USB, SI_NONE, SI_NONE, SI_NONE}},
#endif
    {"CPU", {SI_CPU, SI_MHZ, SI_NONE, SI_NONE}},
    {"CALIBRATE", {SI_CALIB, SI_HOLD, SI_NONE, SI_NONE}},   /* (SI_HOLD: a button's hold, 250 / 350 / 500 ms) */
    {"ABOUT", {SI_ABOUT, SI_NONE, SI_NONE, SI_NONE}},
};
#define NSYS (sizeof SYS / sizeof SYS[0])
static uint8_t sys_dirty;                               /* a setting changed: saved when SYSTEM is left */
static uint32_t sys_rows(void) { return NSYS; }
static void sys_name(uint32_t r, char *b) { str_cpy(b, SYS[r % NSYS].name, 12); }
static uint32_t sys_item(uint32_t r, uint32_t k) { return SYS[r % NSYS].it[k & 3u]; }

/* the settings with a descriptor (params.c GP, bp_set.c): the MIDI rows */
static const param_desc_t *sys_desc(uint32_t it, int16_t **vp)
{
    static const uint8_t BPS[] = {BPS_MOUT, BPS_MIN, 0, 0, BPS_CH0, BPS_CH1, BPS_CH2, BPS_CHD};
    *vp = 0;
    if (it == SI_SYNC || it == SI_CLOCK) {
        *vp = &song.g[it == SI_SYNC ? G_SYNC : G_MIDI];
        return &GP[it == SI_SYNC ? G_SYNC : G_MIDI];
    }
    if (it >= SI_OUT && it <= SI_CHD)
        return bps_desc(BPS[it - SI_OUT], vp);
    return 0;
}
static int32_t sys_step(int32_t v, int32_t s, int32_t max)   /* a knob stops at the ends; YES (s 0) goes round */
{
    return s ? clamp(v + (s > 0 ? 1 : -1), 0, max) : (v >= max ? 0 : v + 1);
}
static void sys_cell(uint32_t r, uint32_t k, cell_t *c)
{
    static const char *const LV[4] = {"OFF", "LOW", "MID", "HIGH"};
    static const char *const KY[4] = {"OFF", "C", "WHITE", "ALL"};
    static const char *const LC[3] = {"OFF", "LOWCUT", "BASS+"};
    uint32_t it = sys_item(r, k);
    int16_t *vp;
    const param_desc_t *d = sys_desc(it, &vp);
    if (d) {
        cell_param(c, d, vp);
        if (it == SI_CLOCK)
            c->kind = CK_RO;                            /* (the clock followed now: a read-out) */
        return;
    }
    cell_clear(c);
    c->kind = CK_VAL;
    switch (it) {
    case SI_COLOR:
        c->label = "COLOR";
        str_cpy(c->val, PALETTES[settings.palette % NPALETTES].name, sizeof c->val);
        cell_gauge(c, 1, 0, NPALETTES - 1, (int32_t)(settings.palette % NPALETTES));
        break;
#if FELUCCA_BRIGHT
    case SI_BRIGHT:
        c->label = "BRIGHT";
        fmt_int(c->val, (int32_t)bright_level());
        cell_gauge(c, 0, 1, 8, (int32_t)bright_level());
        break;
#endif
#if FELUCCA_LIGHTS
    case SI_LIGHTS:
        c->label = "LIGHTS";
        str_cpy(c->val, LV[lights_lvl & 3u], sizeof c->val);
        cell_gauge(c, 1, 0, LIGHTS_N - 1, lights_lvl);
        break;
    case SI_KEYS:
        c->label = "KEYS";
        str_cpy(c->val, KY[lights_keys & 3u], sizeof c->val);
        cell_gauge(c, 1, 0, KEYS_N - 1, lights_keys);
        break;
#endif
    case SI_LOWCUT:
        c->label = "LOWCUT";
        str_cpy(c->val, LC[FELUCCA_BASSPLUS ? settings.lowcut % 3u : settings.lowcut ? 1u : 0u], sizeof c->val);
        cell_gauge(c, 1, 0, FELUCCA_BASSPLUS ? 2 : 1, FELUCCA_BASSPLUS ? (int32_t)(settings.lowcut % 3u) : settings.lowcut != 0u);
        break;
#if FELUCCA_CDC
    case SI_USB:
        c->label = "SERIAL";
        str_cpy(c->val, usb_serial ? "ON" : "OFF", sizeof c->val);
        c->col = usb_serial == usb_cdc_on ? 0 : C_AMB;  /* (amber: a restart applies it) */
        cell_gauge(c, 1, 0, 1, usb_serial != 0u);
        break;
#endif
    case SI_CPU:
        c->label = "LOAD";
        c->kind = CK_RO;
        fmt_int(c->val, (int32_t)(song.cpu_q8 * 100u / 256u));
        c->unit = "%";
        cell_gauge(c, 0, 0, 100, (int32_t)(song.cpu_q8 * 100u / 256u));
        break;
    case SI_MHZ:
        c->label = "CLOCK";
        c->kind = CK_RO;
        fmt_int(c->val, (int32_t)((cpu_khz + 500u) / 1000u));
        c->unit = "MHz";
        cell_gauge(c, 0, 0, 240, (int32_t)((cpu_khz + 500u) / 1000u));
        break;
    case SI_DMIX:                                       /* the drum mixer: 4 or 8 strips (op_dmix.c) */
        c->label = "DR MIX";
        fmt_int(c->val, (int32_t)dm_strips());
        cell_gauge(c, 1, 0, DMV_N - 1, dm_view % DMV_N);
        break;
    case SI_HOLD:                                       /* a button held this long is a hold (op_state.c op_hold_ms) */
        c->label = "HOLD";
        fmt_int(c->val, (int32_t)op_hold_ms());
        c->unit = "ms";
        cell_gauge(c, 1, 0, HOLD_N - 1, op_hold % HOLD_N);
        break;
    case SI_CARDS:                                      /* the cursor row's values: 1x4 cards or 2x2 big (op_draw.c) */
        c->label = "CARDS";
        str_cpy(c->val, op_cards == CARDS_2X2 ? "2x2" : "1x4", sizeof c->val);
        cell_gauge(c, 1, 0, CARDS_N - 1, op_cards % CARDS_N);
        break;
    case SI_CALIB:
        c->label = "PANEL";
        c->kind = CK_ACT;
        break;
    case SI_ABOUT:
        c->label = "VERSION";
        c->kind = CK_ACT;
        break;
    default:
        c->kind = CK_NONE;
        break;
    }
    (void)LV, (void)KY;
}
/* setting it: a knob turned s (> 0 right), or 0: YES (steps round, toggles) */
static void sys_set(uint32_t it, int32_t s)
{
    switch (it) {
    case SI_COLOR:
        settings.palette = (settings.palette + (s < 0 ? NPALETTES - 1u : 1u)) % NPALETTES;
        palette_set(settings.palette);
        ui.force = 1;
        break;
#if FELUCCA_BRIGHT
    case SI_BRIGHT:
        bright_set(s ? (uint32_t)clamp((int32_t)bright_level() + (s > 0 ? 1 : -1), 1, 8) : bright_level() % 8u + 1u);
        break;
#endif
#if FELUCCA_LIGHTS
    case SI_LIGHTS:
    case SI_KEYS: {
        uint8_t *v = it == SI_LIGHTS ? &lights_lvl : &lights_keys;
        *v = (uint8_t)sys_step(*v, s, (int32_t)(it == SI_LIGHTS ? LIGHTS_N : KEYS_N) - 1);
        if (it == SI_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                    /* keys lit need a level: the lowest */
        break;
    }
#endif
    case SI_DMIX:                                       /* (kept in the settings word: settings_word.c) */
        dm_view = (uint8_t)sys_step(dm_view % DMV_N, s, DMV_N - 1);
        break;
    case SI_HOLD:                                       /* (kept in the settings word: settings_word.c) */
        op_hold = (uint8_t)sys_step(op_hold % HOLD_N, s, HOLD_N - 1);
        break;
    case SI_CARDS:                                      /* (kept in the settings word: settings_word.c) */
        op_cards = (uint8_t)sys_step(op_cards % CARDS_N, s, CARDS_N - 1);
        ui.force = 1;                                   /* (every band moves: the whole screen again) */
        break;
    case SI_LOWCUT:
#if FELUCCA_BASSPLUS
        settings.lowcut = (uint32_t)sys_step((int32_t)(settings.lowcut % 3u), s, 2);   /* OFF / LOWCUT / BASS+ */
        fx_lowcut = (uint8_t)settings.lowcut;
#else
        settings.lowcut = s > 0 ? 1u : s < 0 ? 0u : !settings.lowcut;
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
#endif
        break;
#if FELUCCA_CDC
    case SI_USB:
        usb_serial = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_serial);
        break;
#endif
    default:
        return;
    }
    sys_dirty = 1;
}
static void sys_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    uint32_t it = sys_item(r, k);
    int16_t *vp;
    const param_desc_t *d = sys_desc(it, &vp);
    if (s == OP_RESET || it == SI_CLOCK)
        return;
    if (d) {
        val_turn(d, vp, k, s, fine);
        sys_dirty = 1;
        return;
    }
    sys_set(it, s);
}
static int sys_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    uint32_t it = sys_item(r, k);
    int16_t *vp;
    const param_desc_t *d = sys_desc(it, &vp);
    (void)ok;
    if (it == SI_CALIB) {
        panel_setup();                                  /* (op_input.c: teach each button and knob) */
        return 1;
    }
    if (it == SI_ABOUT) {
        ui_say(FELUCCA_VERSION, " OPTIMIST UI");
        return 1;
    }
    if (d) {
        if (it == SI_CLOCK || !val_toggle(d, vp))
            return 0;
        sys_dirty = 1;
        return 1;
    }
    if (it == SI_NONE || it == SI_CPU || it == SI_MHZ)
        return 0;
    sys_set(it, 0);
    return 1;
}
static void sys_leave(void)                             /* SYSTEM left: the settings into flash (as SLOOP's menu_close) */
{
    if (!sys_dirty)
        return;
    sys_dirty = 0;
    if (song.playing || transport_req)
        settings_later = 1;                             /* (a flash write stops the audio: once stopped) */
    else
        settings_save();
}

static const screen_t SCREENS[SCR_N] = {
    {mix_rows, mix_name, mix_cell, mix_turn, mix_yes},
    {snd_rows, snd_name_row, snd_cell, snd_turn, snd_yes},
    {fxs_rows, fxs_name, fxs_cell, fxs_turn, fxs_yes},
    {prj_rows, prj_name, prj_cell, prj_turn, prj_yes},
    {sys_rows, sys_name, sys_cell, sys_turn, sys_yes},
    {step_rows, step_name, step_cell, step_turn, step_yes},
    {song_rows, song_name, song_cell, song_turn, song_yes},
    {tp_rows, tp_name, tp_cell, tp_turn, tp_yes},
    {dm_rows, dm_name, dm_cell, dm_turn, dm_yes},
    {scope_rows, scope_name, scope_cell, scope_turn, scope_yes},
};
#define SCR (&SCREENS[ui.scr % SCR_N])
