/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum track's SOUND pages' values and the kit list, the part of ui/sloop/ui_drums.c that is model, not
 * screen: the descriptors of a lane's values (dsnd_desc_lane, DSD, the SRC names), the factory and user kit list the
 * PRESETS knob steps through. Shared by both UIs (FELUCCA_UI) and the web editor (ed_dsrc.c); moved verbatim
 * from ui_drums.c (2026-10-08, docs/UI-FEASIBILITY.md section 4.1 step 2). felucca.c includes it before the UI */

/* the selected drum lane: one value for the whole device, as song.sel is the selected track (UI-OPTIMIST-DESIGN.md
 * section 7). The Optimist UI's pick (HOME held + a drum key) sets it; its mixer, SOUND rows (and STEP, later)
 * show and edit that lane. SLOOP's UI keeps its own: the lane played last (seq.c pen_lane) */
static uint8_t lane_sel;
static uint32_t lane_selected(void) { return lane_sel & 15u; }
static void lane_select(uint32_t l) { lane_sel = (uint8_t)(l & 15u); }

#define DL_UI DL_ANY
#if DL_UI
static const char *const UK_NAME[16];                   /* "KIT 1".."KIT 16" (drum_kits.c) */
#if FELUCCA_DRUM_KITS
static uint32_t ukit_count(void);                       /* drum_kits.c: the user kit bank */
static uint32_t ukit_nth(uint32_t n);
static uint32_t ukit_rank(uint32_t u);
static int ukit_used(uint32_t u);
static void ukit_name(uint32_t u, char *b);
static int ukit_load(uint32_t u);
static void ukit_ui(uint32_t op, uint32_t u);           /* 0 save, 1 erase */
#else
#define ukit_count() 0u
#define ukit_nth(n) 0u
#define ukit_rank(u) 0u
#define ukit_used(u) 0
#define ukit_name(u, b) ((void)0)
#define ukit_load(u) 0
#define ukit_ui(op, u) ((void)0)
#endif

static int16_t dsv[16];                                 /* the pages' values for page_desc (the sound picked) */
static uint8_t dsnd_slot;                               /* KIT: the user kit slot */

/* ---- the kit list: the factory kits, then the user kits (FELUCCA_DRUM_KITS) */
static void dl_reset_lanes(void)                        /* the lanes back to the kit (a user kit dropped) */
{
    fm1_irq_off();
    memset(&dl, 0, sizeof dl);
    memset(dsend, 0, sizeof dsend);                     /* (the kit's sends went with it: each lane as it is) */
    fm1_irq_on();
}
static void dl_kit_set(uint32_t k)                      /* a factory kit: a user kit's lanes go with it */
{
    if (dl.ukit)
        dl_reset_lanes();
    TDRUM->p[P_E0] = (int16_t)k;
    dl_e0 = (int16_t)k;
}
static uint32_t drum_kit_pos(void)
{
    if (FELUCCA_DRUM_KITS && dl.ukit && ukit_used(dl.ukit - 1u))
        return DRUM_KITS + ukit_rank(dl.ukit - 1u);
    return drum_kit();
}
static uint32_t drum_kit_total(void) { return DRUM_KITS + (FELUCCA_DRUM_KITS ? ukit_count() : 0u); }
static void drum_kit_step(int32_t s)                    /* PRESETS / KNOB 1 on the kit list */
{
    uint32_t cur = drum_kit_pos(), p = cur, tot = drum_kit_total();
    int32_t d = s < 0 ? -1 : 1, n = s < 0 ? -s : s;
    while (n > 0) {                                     /* (the kits of this build only) */
        uint32_t q = p;
        do
            q = (uint32_t)((int32_t)q + d);
        while (q < DRUM_KITS && !drum_kit_built(q));
        if (q >= tot)
            break;
        p = q;
        n--;
    }
    if (p == cur)
        return;
    if (p < DRUM_KITS)
        dl_kit_set(p);
    else
        ukit_load(ukit_nth(p - DRUM_KITS));
}
static const char *drum_kit_name(void)
{
#if FELUCCA_DRUM_KITS
    if (dl.ukit && dl.ukit <= 16u)
        return UK_NAME[dl.ukit - 1u];                   /* a user kit: KIT n, its slot */
#endif
    return DRUM_KIT_NAMES[drum_kit()];
}
static const char *drum_kit_style(void) { return dl.ukit ? "USER KIT" : DRUM_KIT_STYLES[drum_kit()]; }

/* ---- the pages' values */
#define DS_SRC_NAMES DRUM_SRC_NAMES                     /* (drums.c: KIT, USR1..3, the kits) */
#define DS_SRC_XN (DRUM_SRC_X909N + DRUM_SRC_X808N)       /* Optimist: the X0X voices after the kits (drums.c) */
_Static_assert(DRUM_SRC_HEAD == 4u && sizeof DS_SRC_NAMES / sizeof DS_SRC_NAMES[0] == 4u + DRUM_KITS + DS_SRC_XN,
               "SRC: KIT, USR1..3, the kits, the X0X voices");
static const char *dsnd_slot_names[16];
static const param_desc_t DSD[16] = {
    PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_BIPCT, -64, 63, 0), PD("SNAP", F_BIPCT, -64, 63, 0),
    PD("CLICK", F_BIPCT, -64, 63, 0), PD("BEND", F_SEMI, -24, 24, 0), PD("CUT", F_BIPCT, -64, 63, 0),
    PD("DRIVE", F_BIPCT, -64, 63, 0), {"LEVEL", F_INT, -24, 6, 0, 0, "dB"},
    {"SRC", F_ENUM, 0, (int16_t)(FELUCCA_DRUM_KITS ? 3u + DRUM_KITS + DS_SRC_XN : 3u), 0, DS_SRC_NAMES, 0},
    PD("HIT", F_INT, 1, 16, 1), PD("START", F_PCT, 0, 127, 0), PD("LEN", F_PCT, 0, 127, 127),
    {"SLOT", F_ENUM, 0, 15, 0, dsnd_slot_names, 0}, PE("SAVE", N_GO, 0), PE("ERASE", N_GO, 0), PE("RESET", N_GO, 0),
};
#if DRUM_X0X
static const param_desc_t XSD_TUNE = PD("TUNE", F_INT, -24, 24, 0);   /* an X0X sound's TUNE in steps */
#endif
static uint32_t dsnd_src_idx(uint32_t src)              /* src (DL_*) <-> its place in DS_SRC_NAMES */
{
    const uint32_t x = 4u + DRUM_KITS;                  /* (the X0X voices: those of this build, after the kits) */
    if (src >= DL_X909 && src < DL_X909 + DRUM_SRC_X909N)
        return x + src - DL_X909;
    if (src >= DL_X808 && src < DL_X808 + DRUM_SRC_X808N)
        return x + DRUM_SRC_X909N + src - DL_X808;
    return src >= DL_KIT0 && src < DL_KIT0 + DRUM_KITS ? 4u + src - DL_KIT0 : src < DL_USR + SMP_USER_SLOTS ? src : 0u;
}
static uint32_t dsnd_idx_src(uint32_t i)
{
    const uint32_t x = 4u + DRUM_KITS;
    if (i >= x + DRUM_SRC_X909N)
        return DL_X808 + i - x - DRUM_SRC_X909N;
    if (i >= x)
        return DL_X909 + i - x;
    return i >= 4u ? DL_KIT0 + i - 4u : i;
}
static int dsnd_sampled(uint32_t l) { return dl_usr_of(l) || dl_kit_of(l, drum_kit()) < DRUM_SAMPLED; }

/* page_desc of a SOUND page for lane l: the descriptor of value id (0 = none here), *vp its value (also the
 * editor's DRUM_SHOW, ed_dsrc.c: what applies to any lane) */
static const param_desc_t *dsnd_desc_lane(uint32_t l, uint32_t id, int16_t **vp)
{
    uint32_t usr = dl_usr_of(l);
    if ((id &= 31u) >= 16u)                             /* SOUND 3: the sends */
        return dsend_desc(l, id - 16u, vp);
    *vp = &dsv[id];
    if (id < DE_N) {
        if (!FELUCCA_DRUM_EDIT || (dsnd_sampled(l) && (id == DE_SNAP || id == DE_CLICK || id == DE_BEND || id == DE_DRIVE)))
            return 0;
        dsv[id] = dl.ofs[l][id];
#if DRUM_X0X
        {   /* an X0X sound: the values its model has (drum_x0x.c X9_SHOW / X8_SHOW) */
            uint32_t xs = usr ? 0u : x0x_show(dl_kit_of(l, drum_kit()), l);
            if (xs && !((xs >> id) & 1u))
                return 0;
            if (xs && id == DE_TUNE && !(xs & XS_SEMI))
                return &XSD_TUNE;                       /* (its TUNE in steps, not semitones) */
        }
#endif
    } else if (id == 8u) {
        if (!FELUCCA_DRUM_USR && !FELUCCA_DRUM_KITS)
            return 0;
        dsv[id] = (int16_t)dsnd_src_idx(dl.src[l]);
    } else if (id < 12u) {
        if (!usr)
            return 0;
        dsv[id] = (int16_t)(id == 9u ? dl_hit(dl.ref[l]) + 1u : id == 10u ? dl_start(dl.ref[l]) >> 3 : (dl_len(dl.ref[l]) >> 3) - 1u);
    } else if (id < 15u) {
        uint32_t k;
        if (!FELUCCA_DRUM_KITS)
            return 0;
        for (k = 0; k < 16u; k++)                       /* SLOT: KIT n, "--" when empty (drum_kits.c) */
            dsnd_slot_names[k] = ukit_used(k) ? UK_NAME[k] : "--";
        dsv[id] = id == 12u ? dsnd_slot : 0;
    } else {
        if (!FELUCCA_DRUM_EDIT && !FELUCCA_DRUM_USR && !FELUCCA_DRUM_KITS)
            return 0;
        dsv[id] = 0;
    }
    return &DSD[id];
}
#else
#define drum_kit_step(s) (TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + (s), 0, DRUM_KITS - 1))
#define drum_kit_name() DRUM_KIT_NAMES[drum_kit()]
#define drum_kit_style() DRUM_KIT_STYLES[drum_kit()]
#define drum_kit_pos() drum_kit()
#define drum_kit_total() DRUM_KITS
#endif
