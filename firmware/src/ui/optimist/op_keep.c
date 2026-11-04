/* SPDX-License-Identifier: GPL-3.0-only */
/* The page kept by what it is, not by its place (the user, 2026-10-29: "when I change some effects, the page count changes
 * (expected) but then the current page changes to another one, super confusing ... Possible to fix so that we don't change
 * page?"). A screen's rows come from the settings (an insert type's pages, the returns' effects, the master's DYN, the engine's
 * pages, a lane's user sample), so a change before the cursor moved every row after it while ui.row stayed a number: the
 * cursor landed on another page. Now every list the cursor walks keeps a key of the row it is on (pgk_keep): a row's key is what the
 * row is (a page: its page; GLOBAL / SYSTEM: its line; the mixer's knob sets: the set's first value; any other screen: its
 * name), checked against the row at that place each time the rows are fixed (op_input.c op_rows_fix: after every input and
 * before every frame drawn; the mirror's 109 NAV reads the same cursor).
 *   the page still there      the cursor follows it to its new place; the hot card stays
 *   the page gone             the page that owns it (the slots it is an effect of: SOUND's INSERTS, MASTER's INSERTS for the
 *                             master's effects, SLOTS for the returns' effects), else the nearest page before it that is still
 *                             there (up to PGK_BACK back), else the place clamped as before; the hot card goes back to the first
 *   the same count, a row renamed at the cursor (a scene's "*"): it stays where it is
 * Only a change under the cursor moves it: another screen, track, lane, family or mixer row (the context), or the cursor moved
 * by a gesture (another place than the one recorded), is recorded as it is, the screens' own rules first (SOUND's page memory,
 * the walk, the lists' memory) */
#define PGK_NONE 0xFFFFu
#define PGK_BACK 6u                                     /* (the most pages that go at once before a page left: ANALOG's four) */
typedef uint32_t (*pgk_key_fn)(uint32_t i);             /* item i's key (16 bits), PGK_NONE none */
typedef struct {
    uint32_t ctx;                                       /* what the list belongs to (pgk_ctx, the mixer's row) */
    uint16_t key, own;                                  /* the cursor's key, its owner's (PGK_NONE: none) */
    uint16_t back[PGK_BACK];                            /* the keys of the items before it, the nearest first */
    uint8_t at, n, ok;                                  /* the place and the count recorded; ok: recorded once */
} pgkeep_t;
static pgkeep_t pgk_rows, pgk_sets;                    /* the screen's rows; the mixer row's knob sets */

static uint32_t pgk_fold(uint32_t h)                    /* a 32-bit signature to a key: never PGK_NONE */
{
    h = (h ^ h >> 16) & 0xFFFFu;
    return h == PGK_NONE ? 0xFFFEu : h;
}
static int32_t pgk_find(uint32_t n, pgk_key_fn f, uint32_t key, uint32_t near)   /* the item of that key nearest to near, -1 none */
{
    uint32_t i, d, bd = 0;
    int32_t best = -1;
    if (key == PGK_NONE)
        return -1;
    for (i = 0; i < n; i++)
        if (f(i) == key) {
            d = i > near ? i - near : near - i;
            if (best < 0 || d < bd)
                best = (int32_t)i, bd = d;
        }
    return best;
}
/* list p's cursor at item at of n (in context ctx): its place again when the item it was on moved (*gone: it went, the place
 * is its owner's or a neighbour's); own(i) is item i's owner's key */
static uint32_t pgk_keep(pgkeep_t *p, uint32_t ctx, uint32_t at, uint32_t n, pgk_key_fn key, pgk_key_fn own, int *gone)
{
    uint32_t cur = at < n ? key(at) : PGK_NONE, j;
    int32_t i = -1;
    *gone = 0;
    if (p->ok && p->ctx == ctx && p->at == at) {
        if (n == p->n && cur == p->key)
            return at;                                  /* (nothing changed: the usual frame) */
        if (cur != p->key && p->key != PGK_NONE) {
            i = pgk_find(n, key, p->key, at);
            if (i < 0 && n != p->n) {                   /* (its page went with the change: its owner, else one before it) */
                *gone = 1;
                i = pgk_find(n, key, p->own, at);
                for (j = 0; i < 0 && j < PGK_BACK; j++)
                    i = pgk_find(n, key, p->back[j], at);
            }
            if (i >= 0)
                at = (uint32_t)i;
        }
    }
    p->ok = 1;
    p->ctx = ctx;
    p->at = (uint8_t)at;
    p->n = (uint8_t)(n < 0xFFu ? n : 0xFFu);
    p->key = (uint16_t)(at < n ? key(at) : PGK_NONE);
    p->own = (uint16_t)(at < n && own ? own(at) : PGK_NONE);
    for (j = 0; j < PGK_BACK; j++)
        p->back[j] = (uint16_t)(at < n && at > j ? key(at - 1u - j) : PGK_NONE);
    return at;
}

/* ---- the rows' keys, by screen */
static uint32_t pgk_name_key(uint32_t r)                /* any screen: the row's name */
{
    char b[24];
    b[0] = 0;
    SCR->name(r, b);
    return pgk_fold(hs(2166136261u, b));
}
static uint32_t pgk_snd_key(uint32_t r)                 /* SOUND: its page (the SOUND row, MAIN, FM6's rows: their place) */
{
    if (snd_fam == SND_FM6)
        return 0x300u | (r & 0xFFu);
    if (r < snd_first())
        return 0x200u | r;
    return 0x100u | snd_ix[(r - snd_first()) % OP_MAXROWS];
}
static uint32_t pgk_fx_key(uint32_t r) { return 0x100u | fx_ix[r % OP_MAXROWS]; }   /* MASTER: its page (FX_RET_IX: RETURNS) */
static uint32_t pgk_gl_key(uint32_t r) { return 0x400u | gl_ent(ui.scr == SCR_SYSTEM, r); }   /* GLOBAL, SYSTEM: the line itself */
static uint32_t pgk_row_key(uint32_t r)
{
    if (ui.scr == SCR_SOUND)
        return pgk_snd_key(r);
    if (ui.scr == SCR_FX)
        return pgk_fx_key(r);
    if (glo_cycle())
        return pgk_gl_key(r);
    if (ui.scr == SCR_HOME)
        return r;                                       /* (the mixer's rows are fixed: its sets are pgk_sets) */
    if (ui.scr == SCR_PROJECT)
        return r;                                       /* (PROJECT's rows are fixed; a card list's name is the folder
                                                         * shown, op_sdcard.c sdc_title: up from Presets/User, titled
                                                         * USER, the cursor went to the USER row) */
    return pgk_name_key(r);
}
/* the row whose page is titled t (its key), PGK_NONE none */
static uint32_t pgk_titled(const char *t, const page_t *(*pg_of)(uint32_t r))
{
    uint32_t r, n = SCR->rows();
    for (r = 0; r < n; r++)
        if (pg_of(r) && str_eq(pg_of(r)->title, t))
            return pgk_row_key(r);
    return PGK_NONE;
}
/* an effect's page: shown while a slot holds its type, owned by the slots' page. SOUND: every FX page but INSERTS, SENDS and
 * SLICER (a picked lane's INSERTS and SENDS too) is an insert's (DYN, GATER, JUNO CHO, EQ3, XDLY, FILTER): INSERTS */
static uint32_t pgk_snd_own(uint32_t r)
{
    const page_t *pg = snd_fam == SND_FM6 ? 0 : snd_page(r);
    if (!pg || pg->fam != FAM_FX || str_eq(pg->title, "INSERTS") || str_eq(pg->title, "SENDS") || pg->graph == GR_SLCR)
        return PGK_NONE;
    return pgk_titled("INSERTS", snd_page);
}
/* MASTER: the master inserts' effects (INS DYN 1 2, JUNO, EQ3, XDLY) are its INSERTS' (PAGES' MST INS), the returns' (RET REV,
 * RET TYPE, RET DELAY, RET DLY F, RET AMT, RET EQ3) SLOTS' */
static uint32_t pgk_fx_own(uint32_t r)
{
    static const char *const MST[] = {"COMP", "LIMIT", "MST JUNO", "MST EQ3", "MST EQ3 F", "MST XDLY"};
    static const char *const RET[] = {"REV/CHO", "REVERB", "DLY", "DLY F", "RET AMT", "RET EQ3", "RET EQ3 F"};
    const char *t = fx_pg(r)->title;
    uint32_t i;
    for (i = 0; i < sizeof MST / sizeof MST[0]; i++)
        if (str_eq(t, MST[i]))
            return pgk_titled("MST INS", fx_pg);
    for (i = 0; i < sizeof RET / sizeof RET[0]; i++)
        if (str_eq(t, RET[i]))
            return pgk_titled("SLOTS", fx_pg);
    return PGK_NONE;
}
static uint32_t pgk_row_own(uint32_t r) { return ui.scr == SCR_SOUND ? pgk_snd_own(r) : ui.scr == SCR_FX ? pgk_fx_own(r) : PGK_NONE; }
static uint32_t pgk_ctx(void)                           /* what the rows belong to: another one, other rows */
{
    return ui.scr | (song.sel % NTRK) << 4 | lane_selected() << 7 | (uint32_t)lane_picked() << 12 | (uint32_t)snd_fam << 13 |
           (uint32_t)(ui.master != 0) << 21 | (uint32_t)(ui.scr == SCR_SYSTEM && GL_DEV) << 22 | (uint32_t)(glo_cycle() != 0) << 23;
}

/* ---- the mixer's knob sets: a set by its first value (its group, kind and id) */
static uint32_t pgk_mx_r;                               /* the mixer row whose sets these are */
static uint32_t pgk_item_key(const mitem_t *m) { return pgk_fold(hu(hu(hu(2166136261u, m->grp), m->kind), m->id)); }
static uint32_t pgk_set_key(uint32_t s)
{
    mitem_t it[MX_MAXI];
    uint32_t n = mx_items(pgk_mx_r, it);
    return s * 4u < n ? pgk_item_key(&it[s * 4u]) : PGK_NONE;
}
/* a slot's effect's set (the USB input's EQ3, GATER, JUNO; MASTER's DYN) is its slots' set's, a return's sends the returns' */
static int pgk_slots_set(const mitem_t *m, uint32_t want) { return m->grp == want && !(want == MG_INS && m->kind == MI_GLOB); }
static uint32_t pgk_set_own(uint32_t s)
{
    mitem_t it[MX_MAXI];
    uint32_t n = mx_items(pgk_mx_r, it), g, want, i;
    if (s * 4u >= n)
        return PGK_NONE;
    g = it[s * 4u].grp;
    if ((g >= MG_UEQ && g <= MG_UJC) || (g == MG_INS && it[s * 4u].kind == MI_GLOB))
        want = MG_INS;
    else if (g >= MG_RSA && g <= MG_RSC)
        want = MG_SEND;
    else
        return PGK_NONE;
    for (i = 0; i * 4u < n; i++)
        if (pgk_slots_set(&it[i * 4u], want))
            return pgk_item_key(&it[i * 4u]);
    return PGK_NONE;
}

/* the cursor kept on its row (n: the screen's rows now), the mixer's set on its set; before op_rows_fix's clamp */
static void op_keep(uint32_t n)
{
    int gone;
    uint32_t was = ui.row[ui.scr], r = pgk_keep(&pgk_rows, pgk_ctx(), was, n, pgk_row_key, pgk_row_own, &gone);
    if (r != was) {
        if (ui.arm_scr == ui.scr && ui.arm_row == was)
            ui.arm_row = (uint8_t)r;                    /* (a question asked on that row: its row still) */
        ui.row[ui.scr] = (uint8_t)r;
        if (gone)
            ui.hot = 0, ui.hot_lit = 0;                 /* (another page: its first card, as a pick) */
    }
    if (ui.scr == SCR_HOME && !mx.across) {
        pgk_mx_r = mx_row();
        was = mx.set;
        r = pgk_keep(&pgk_sets, pgk_mx_r, was, mx_sets(pgk_mx_r), pgk_set_key, pgk_set_own, &gone);
        if (r != was) {
            mx.set = (uint8_t)r;
            if (gone)
                ui.hot = 0, ui.hot_lit = 0;
        }
    }
}
