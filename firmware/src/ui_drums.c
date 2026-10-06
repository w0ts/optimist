/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum track's SOUND pages (drum_edit.c): EDIT tapped on the drum track opens them, tapped again it
 * turns the page. They edit one of the 16 sounds: the last one played (a key, MIDI, a roll: seq.c
 * pen_lane, the SEQ layer's sound too), or the one picked with EDIT held + its key (no sound, no erase).
 *   SOUND    TUNE  DECAY  SNAP   CLICK      (FELUCCA_DRUM_EDIT; a sampled sound: TUNE DECAY only)
 *   SOUND 2  BEND  CUT    DRIVE  LEVEL      (a sampled sound: CUT LEVEL only)
 *   SOUND 3  REV   DLY    CHO    -          (FELUCCA_DRUM_SENDS: the sound's sends, drum_sends.c)
 *   SOURCE   SRC   HIT    START  LEN        (FELUCCA_DRUM_USR / _KITS: the kit, USR1..3, another kit's
 *                                            sound; HIT START LEN on a user sample)
 *   KIT      SLOT  SAVE   ERASE  RESET      (FELUCCA_DRUM_KITS: the user kit bank, drum_kits.c; RESET:
 *                                            the sound back to the kit's)
 * They are ordinary pages (params.c page_desc -> dsnd_desc), so the one-page layout and VIEW ALL (a row
 * per page) both show them; the graph: the sound's name and source, its envelope and pitch drop.
 * Also here: the kit list with the user kits after the factory ones (PRESETS, the DRUMS kit page). */
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

static uint32_t dsnd_lane(void) { return pen_lane & 15u; }
static int on_dsnd_page(void) { return cur_page()->scope == SC_DSND; }

/* ---- the kit list: the factory kits, then the user kits (FELUCCA_DRUM_KITS) */
static void dl_reset_lanes(void)                        /* the lanes back to the kit (a user kit dropped) */
{
    fm1_irq_off();
    memset(&dl, 0, sizeof dl);
    memset(dsend, 0, sizeof dsend);                     /* (the kit's sends went with it) */
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

/* once a frame (ui_draw): another kit set elsewhere (the editor) drops a user kit's lanes; the start
 * states of user samples; the SOUND page tells seq.c that EDIT + a key picks a sound */
static void dsnd_tick(void)
{
    if (TDRUM->p[P_E0] != dl_e0) {
        if (dl.ukit && dl_e0 >= 0)
            dl_reset_lanes();
        dl_e0 = TDRUM->p[P_E0];
    }
    dl_tick();
    dl_ui_pick = (uint8_t)(is_drum(TSEL) && on_dsnd_page());
}

/* ---- the pages' values */
#define DS_SRC_NAMES DRUM_SRC_NAMES                     /* (drums.c: KIT, USR1..3, the kits) */
_Static_assert(DRUM_SRC_HEAD == 4u && sizeof DS_SRC_NAMES / sizeof DS_SRC_NAMES[0] == 4u + DRUM_KITS, "SRC: KIT, USR1..3, the kits");
static const char *dsnd_slot_names[16];
static const param_desc_t DSD[16] = {
    PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_BIPCT, -64, 63, 0), PD("SNAP", F_BIPCT, -64, 63, 0),
    PD("CLICK", F_BIPCT, -64, 63, 0), PD("BEND", F_SEMI, -24, 24, 0), PD("CUT", F_BIPCT, -64, 63, 0),
    PD("DRIVE", F_BIPCT, -64, 63, 0), {"LEVEL", F_INT, -24, 6, 0, 0, "dB"},
    {"SRC", F_ENUM, 0, (int16_t)(FELUCCA_DRUM_KITS ? 3u + DRUM_KITS : 3u), 0, DS_SRC_NAMES, 0},
    PD("HIT", F_INT, 1, 16, 1), PD("START", F_PCT, 0, 127, 0), PD("LEN", F_PCT, 0, 127, 127),
    {"SLOT", F_ENUM, 0, 15, 0, dsnd_slot_names, 0}, PE("SAVE", N_GO, 0), PE("ERASE", N_GO, 0), PE("RESET", N_GO, 0),
};
#if DRUM_X0X
static const param_desc_t XSD_TUNE = PD("TUNE", F_INT, -24, 24, 0);   /* an X0X sound's TUNE in steps */
#endif
static uint32_t dsnd_src_idx(uint32_t src)              /* src (DL_*) <-> its place in DS_SRC_NAMES */
{
    return src >= DL_KIT0 ? 4u + src - DL_KIT0 : src < DL_USR + SMP_USER_SLOTS ? src : 0u;
}
static uint32_t dsnd_idx_src(uint32_t i) { return i >= 4u ? DL_KIT0 + i - 4u : i; }
static int dsnd_sampled(uint32_t l) { return dl_usr_of(l) || dl_kit_of(l, drum_kit()) < DRUM_SAMPLED; }

/* page_desc of a SOUND page: the descriptor of value id (0 = none here), *vp its value */
static const param_desc_t *dsnd_desc(uint32_t id, int16_t **vp)
{
    uint32_t l = dsnd_lane(), usr = dl_usr_of(l);
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

static const param_desc_t *(*dsnd_desc_fn)(uint32_t id, int16_t **vp) = dsnd_desc;   /* (params.c page_desc) */

/* a knob turned on a SOUND page (edit_param): value id is now v (steps: the turn); next hit hears it */
static void dsnd_set(uint32_t id, int32_t v, int32_t steps)
{
    uint32_t l = dsnd_lane();
    uint8_t *r = dl.ref[l];
    if (id >= 16u) {                                    /* SOUND 3: the sends */
        dsend_set(l, id - 16u, v);
        return;
    }
    if (id < DE_N) {
        dl.ofs[l][id] = (int8_t)v;
        return;
    }
    switch (id) {
    case 8:                                             /* SRC (without USR: past USR1..3) */
        if (!FELUCCA_DRUM_USR && v >= 1 && v <= 3)
            v = steps > 0 ? 4 : 0;
        if (!FELUCCA_DRUM_KITS && v > 3)
            v = 3;
        dl.src[l] = (uint8_t)dsnd_idx_src((uint32_t)v);
        ui.force = 1;
        return;
    case 9:
        dl_set_ref(r, (uint32_t)v - 1u, dl_start(r), dl_len(r));
        return;
    case 10:
        dl_set_ref(r, dl_hit(r), (uint32_t)v << 3, dl_len(r));
        return;
    case 11:
        dl_set_ref(r, dl_hit(r), dl_start(r), ((uint32_t)v + 1u) << 3);
        return;
    case 12:
        dsnd_slot = (uint8_t)v;
        ui.arm = 0;
        return;
    default:
        break;
    }
    dsv[id] = 0;                                        /* GO: SAVE, ERASE, RESET; one detent arms */
    if (v <= 0)
        return;
    if (ui.arm != 0xD0u + id) {
        ui.arm = (uint8_t)(0xD0u + id);
        ui.arm_t = 90;
        ui_say("AGAIN: ", DSD[id].label);
        return;
    }
    ui.arm = 0;
    if (id == 15u) {                                    /* RESET: the sound as the kit has it */
        fm1_irq_off();
        memset(dl.ofs[l], 0, sizeof dl.ofs[l]);
        dl.src[l] = DL_KIT;
        memset(dl.ref[l], 0, sizeof dl.ref[l]);
        dsend[l] = 0;                                   /* (its sends: TRK, none) */
        fm1_irq_on();
        ui_say("RESET ", LANE_NAME[l]);
        ui.force = 1;
        return;
    }
    ukit_ui(id - 13u, dsnd_slot);
}

/* EDIT held + a key on a SOUND page (seq.c: no sound, no erase): that sound */
static void dsnd_pick(uint32_t k)
{
    pen_lane = (uint8_t)lane_of_key(k);
    ui.force = 1;
}

/* the graph's signature: the sound picked and all it is made of */
static uint32_t dsnd_sig(void)
{
    uint32_t l = dsnd_lane(), h = l * 7919u + drum_kit() * 31u + (uint32_t)usr_nz[0] * 3u + (uint32_t)usr_nz[1] * 5u +
                                  (uint32_t)usr_nz[2] * 7u + smp_user_gen * 131u, i;
    for (i = 0; i < DE_N; i++)
        h = (h ^ (uint8_t)dl.ofs[l][i]) * 16777619u;
    h = (h ^ dl.src[l]) * 16777619u;
    for (i = 0; i < 3u; i++)
        h = (h ^ dl.ref[l][i]) * 16777619u;
    return h;
}

/* the graph: name and source on top; under them the sound over its length (the time at the right): its
 * level in the track colour (a sample: its peaks, with DECAY; synthesised: the body or the noise, each as
 * it decays) and the pitch drop in white */
static void graph_time(int32_t bot, uint32_t ms)
{
    char b[12];
    fmt_int(b, (int32_t)ms);
    str_cpy(b + str_len(b), "ms", 4);
    cv_text(236 - text_w(&FONT_S, b), bot - 16, &FONT_S, b, C_DIM);
}
static void graph_dsnd(int32_t top, int32_t bot, uint16_t c)
{
    uint32_t l = dsnd_lane(), usr = dl_usr_of(l), kit = dl_kit_of(l, drum_kit()), x;
    const int8_t *o = dl_ofs(l);
    char b[24];
    int32_t y0 = top + 18, h = bot - 4 - y0;
    str_cpy(b, LANE_NAME[l], sizeof b);
    cv_text(4, top, &FONT_S, b, C_WHITE);
    if (usr) {
        str_cpy(b, DS_SRC_NAMES[usr], sizeof b);
        str_cpy(b + str_len(b), " HIT ", 8);
        fmt_int(b + str_len(b), (int32_t)dl_hit(dl.ref[l]) + 1);
    } else {
        str_cpy(b, dl.src[l] == DL_KIT ? drum_kit_name() : DRUM_KIT_NAMES[kit], sizeof b);
    }
    cv_text(236 - text_w(&FONT_S, b), top, &FONT_S, b, C_GRAY);
    cv_line(0, bot + 1, 239, bot + 1, C_LINE);
    if (h < 8)
        return;
#if DRUM_X0X
    if (!usr && x0x_show(kit, l)) {                     /* an X0X sound: its name, the decay as set (a sketch) */
        int32_t dk = (o ? o[DE_DECAY] : 0) + (int32_t)x0x_var_decay(kit, l), w = 6 + (64 + dk) * 3 / 4, y = 0;
        str_cpy(b, "X0X ", 8);
        str_cpy(b + 4, x0x_snd_name(kit, l), 8);
        str_cpy(b + str_len(b), " model", 8);
        cv_text(4, top + 16, &FONT_S, b, C_GRAY);
        for (x = 0; x < 232u; x++) {
            y = h * 1024 / (1024 + (int32_t)x * 1024 / (w > 1 ? w : 1));   /* (1 / (1 + t / w): no exp here) */
            cv_line((int32_t)x + 4, bot - 4, (int32_t)x + 4, bot - 4 - y * y / h, c);
        }
        return;
    }
    if (!usr && kit >= DRUM_UID_X909)
        kit = x0x_standin(kit);                         /* (a note the machine lacks: the stand-in's sound) */
#endif
    if (usr || kit < DRUM_SAMPLED || !FELUCCA_DRUM_SYNTH) {   /* a sample: its peaks over the part played, DECAY */
        const smp_zone_t *z = 0;
        uint32_t pos = 0, end = 0, k = o && o[DE_DECAY] < 0 ? DECAY_K[clamp(127 + 2 * o[DE_DECAY], 0, 127)] : 0u;
        uint32_t i, n, per, rate;
        int32_t e = 32767, pk[232], mx = 1;
        voice_t v;
        int32_t (*next)(const smp_zone_t *, voice_t *, int) = FAR(sample_next);
#if FELUCCA_DRUM_USR
        if (usr)
            z = dl_usr_zone(l, usr, &pos, &end);
#endif
        if (!usr && drum_set() >= 0) {
            const smp_set_t *set = &SMP_SETS[drum_set()];
            for (i = 0; i < set->nz; i++)
                if (LANE_NOTE[l] >= SMP_ZONES[set->z0 + i].lo && LANE_NOTE[l] <= SMP_ZONES[set->z0 + i].hi)
                    z = &SMP_ZONES[set->z0 + i];
            end = z ? z->n : 0u;
        }
        if (!z || end <= pos)
            return;
        memset(&v, 0, sizeof v);
        for (i = 0; i < pos; i++)                       /* (the start: decoded as dl_tick does) */
            next(z, &v, 0);
        n = end - pos;
        per = (n + 231u) / 232u;
        for (x = 0; x < 232u; x++) {
            int32_t m = 0;
            for (i = 0; i < per && pos < end; i++, pos++) {
                int32_t s = next(z, &v, 0);
                s = s < 0 ? -s : s;
                s = mulq15(s, e);
                m = s > m ? s : m;
                if (k)
                    e = (int32_t)(((uint32_t)e * k) >> 16);
            }
            pk[x] = m;
            mx = m > mx ? m : mx;
        }
        for (x = 0; x < 232u; x++)
            if (pk[x])
                cv_line((int32_t)x + 4, bot - 4, (int32_t)x + 4, bot - 4 - pk[x] * h / mx, c);
        rate = ((z->rate ? z->rate : 65536u) >> 4) * FS >> 12;   /* the length played: n samples at its rate, TUNE */
        graph_time(bot, n * 1000u / (rate ? rate : 1u) * 65536u / pow2_q16(dl_tune(l) * 16));
        return;
    }
    {                                                   /* synthesised: the edited sound, as drum_synth.c runs it */
        int32_t semi;
        const dsnd_t *d = &DS_KITS[kit - DRUM_SAMPLED].s[ds_lane(LANE_NOTE[l], &semi)];
#if FELUCCA_DRUM_EDIT
        static dsnd_t ed;
        if (o) {
            de_synth(&ed, d, o);
            d = &ed;
        }
#endif
        {
            uint32_t ka = ds_k32(d->decay), kn = ds_k32(d->ndec), kp = ds_k32(d->btime), j, blocks, per;
            int32_t a0 = d->wave ? (int32_t)d->tlev * 258 : 0, n0 = (d->src & 15u) ? (int32_t)d->nlev * 258 : 0;
            int32_t pk = a0 > n0 ? a0 : n0, a, n, pe = 32767, px = 4, py = bot - 4;
            uint32_t hold, nhold, bend = d->bend > 36u ? 36u : d->bend;
            pk = pk ? pk : 1;
            a = a0, n = n0, hold = ds_blocks(d->hold), nhold = ds_blocks(d->nhold);
            for (blocks = 0; blocks < FS * 4u / CTL && (hold || nhold || a > pk / 64 || n > pk / 64); blocks++) {
                if (hold) hold--; else a = (int32_t)(((uint32_t)a * ka) >> 16);
                if (nhold) nhold--; else n = (int32_t)(((uint32_t)n * kn) >> 16);
            }
            per = blocks / 232u + 1u;
            a = a0, n = n0, hold = ds_blocks(d->hold), nhold = ds_blocks(d->nhold);
            for (x = 0; x < 232u; x++) {
                int32_t v = a > n ? a : n, y = bot - 4 - v * h / pk;
                int32_t yp = bot - 4 - pe * h / 32767 * (int32_t)bend / 36;
                cv_line((int32_t)x + 4, bot - 4, (int32_t)x + 4, y, c);
                if (bend && x)
                    cv_line(px, py, (int32_t)x + 4, yp, C_WHITE);
                px = (int32_t)x + 4;
                py = yp;
                for (j = 0; j < per; j++) {
                    if (hold) hold--; else a = (int32_t)(((uint32_t)a * ka) >> 16);
                    if (nhold) nhold--; else n = (int32_t)(((uint32_t)n * kn) >> 16);
                    pe = (int32_t)(((uint32_t)pe * kp) >> 16);
                }
            }
            graph_time(bot, per * 232u * CTL * 1000u / FS);
        }
    }
}
#else
#define drum_kit_step(s) (TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + (s), 0, DRUM_KITS - 1))
#define drum_kit_name() DRUM_KIT_NAMES[drum_kit()]
#define drum_kit_style() DRUM_KIT_STYLES[drum_kit()]
#define drum_kit_pos() drum_kit()
#define drum_kit_total() DRUM_KITS
#define dsnd_tick() ((void)0)
#define on_dsnd_page() 0
#endif
