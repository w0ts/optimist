/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum track's SOUND pages (drum_edit.c): EDIT tapped on the drum track opens them, tapped again it
 * turns the page. They edit one of the 16 sounds: the last one played (a key, MIDI, a roll: seq.c
 * pen_lane, the SEQ layer's sound too), or the one picked with EDIT held + its key (no sound, no erase).
 *   SOUND    TUNE  DECAY  SNAP   CLICK      (FELUCCA_DRUM_EDIT; a sampled sound: TUNE DECAY only)
 *   SOUND 2  BEND  CUT    DRIVE  LEVEL      (a sampled sound: CUT LEVEL only)
 *   SOUND 3  REV   DLY    CHO    -          (the sound's sends, drum_sends.c: the drums' only sends)
 *   SOURCE   SRC   HIT    START  LEN        (FELUCCA_DRUM_USR / _KITS: the kit, USR1..3, another kit's
 *                                            sound; HIT START LEN on a user sample)
 *   KIT      SLOT  SAVE   ERASE  RESET      (FELUCCA_DRUM_KITS: the user kit bank, drum_kits.c; RESET:
 *                                            the sound back to the kit's)
 * They are ordinary pages (params.c page_desc -> dsnd_desc), so the one-page layout and VIEW ALL (a row
 * per page) both show them; the graph: the sound's name and source, its envelope and pitch drop.
 * The values' descriptors and the kit list are model, shared with the other UI and the editor: drums/dsnd_desc.c. */
#if DL_UI
static uint32_t dsnd_lane(void) { return pen_lane & 15u; }
static int on_dsnd_page(void) { return cur_page()->scope == SC_DSND; }

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

static const param_desc_t *dsnd_desc(uint32_t id, int16_t **vp) { return dsnd_desc_lane(dsnd_lane(), id, vp); }
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
        dsend[l] = 0;                                   /* (its sends as it is: REV 4, no DLY / CHO) */
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
        str_cpy(b, dl.src[l] == DL_KIT ? drum_kit_name() : dl.src[l] >= DL_X909 && dsnd_src_idx(dl.src[l])
                   ? DS_SRC_NAMES[dsnd_src_idx(dl.src[l])] : DRUM_KIT_NAMES[kit], sizeof b);   /* (an X0X voice: its name) */
    }
    {   /* the source, its kind before it (the editor's tags: SMP sampled, SYN synthesised, X0X a model) */
        const char *tg = usr ? "" : kit < DRUM_SAMPLED ? "SMP " : kit < DRUM_SYNTH_END ? "SYN " : "X0X ";
        int32_t w = text_w(&FONT_S, b);
        cv_text(236 - w, top, &FONT_S, b, C_GRAY);
        cv_text(236 - w - text_w(&FONT_S, tg), top, &FONT_S, tg, C_DIM);
    }
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
#define dsnd_tick() ((void)0)
#define on_dsnd_page() 0
#endif
