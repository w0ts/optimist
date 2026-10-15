/* SPDX-License-Identifier: GPL-3.0-only */
/* Live effective values on the cards (the user's decisions, 2026-10-12: optimist-work/effval/DECISIONS.md): every card
 * has one big number, the value as entered (the knob's own), and two bars: the entered value, as the gauge draws it, and
 * the actual value now, about 30 times a second (op_draw.c draw_cards). The second bar shows only while the actual
 * value differs from the entered one.
 *   a drum lane value   (SOUND, SOUND 2, ENV, STEP's LANE page, the mixer's lane rows) what a hit now plays: the patch,
 *                       the motion and the locks in force, the lane's LFO now (drums/lane_play.c dl_play_ofs: the hit's
 *                       own formula, the same code); on a synthesised kit DECAY CLICK BEND DRIVE as the absolute value
 *                       the sound gets (drum_edit.c de_synth on the kit's own sound: kit + offset, 0..127, BEND 0..96),
 *                       always shown; the other values (and the sampled, user and X0X sounds) as offsets
 *   a synth value       after the LFO, the envelope and the macros (core/voice.c track_render's terms, core/macro.c
 *                       mac_effective), for the last note played still sounding (the newest active voice: POLY too):
 *                       the engine's cutoff and shape cards (EFFV_DEST), any value a macro moves
 *   a +/- amount        (LFO DEST, ENV DEST, the lane LFO's AMT) the destination's actual value on its own scale: CUT
 *                       and SHP the engine's card (none: what the source adds, on -64..63), PIT the pitch now in 1/16
 *                       semitones (+-12), AMP the voice's level now (0..127); shown while the amount acts
 * Read from RAM the audio side already keeps (t->lfo_val, lfo_fade, the voices' env, penv, env_out, vel; the lanes'
 * dla, dlfo): nothing is published, nothing added to the audio path or to HOT (mac_effective is the precedent). Reads
 * take no IRQ off: aligned words, a frame may mix two blocks. A held step's cards describe the step: no live bar. */

/* ---- drums */
#if FELUCCA_DRUM_EDIT && FELUCCA_DRUM_SYNTH
/* lane l's synthesised sound (the kit's dsnd_t, as drum_on takes it), 0 a sampled, user or X0X sound */
static const dsnd_t *effv_dsnd(uint32_t l)
{
    uint32_t kit = dl_kit_of(l, drum_kit());
    int32_t semi;
    if (dl_usr_of(l) || kit < DRUM_SAMPLED || kit >= DRUM_SYNTH_END)
        return 0;
    return DS_SND(&DS_KITS[kit - DRUM_SAMPLED], ds_lane(LANE_NOTE[l % DRUM_LANES], &semi));
}
#endif
/* lane l's offsets a hit now plays into o (the patch when nothing acts on it) */
static void effv_lane_ofs(uint32_t l, int8_t *o)
{
    union {
        int8_t o[DE_N];
        uint32_t w[DE_N / 4u];
    } x;
    memcpy(x.o, dl.ofs[l % DRUM_LANES], DE_N);
#if DLA_ON || DLFO_ON
    (void)dl_play_ofs(l % DRUM_LANES, x.w);             /* (lane_play.c: the hit's formula) */
#endif
    memcpy(o, x.o, DE_N);
}
/* lane l's value v as a hit now plays it into *val, its range; 2 absolute (the kit's value + the offset), 1 an offset */
static uint32_t effv_lane(uint32_t l, uint32_t v, int32_t *val, int32_t *mn, int32_t *mx)
{
    int8_t o[DE_N];
    v %= DE_N;
    effv_lane_ofs(l, o);
#if FELUCCA_DRUM_EDIT && FELUCCA_DRUM_SYNTH
    if (v == DE_DECAY || v == DE_CLICK || v == DE_BEND || v == DE_DRIVE) {
        const dsnd_t *d = effv_dsnd(l);
        dsnd_t s;
        if (d) {
            de_synth(&s, d, o);
            *val = v == DE_DECAY ? s.decay : v == DE_CLICK ? s.click : v == DE_BEND ? s.bend : s.drive;
            *mn = 0;
            *mx = v == DE_BEND ? 96 : 127;
            return 2;
        }
    }
#endif
    *val = o[v];
    *mn = DE_MIN[v];
    *mx = DE_MAX[v];
    return 1;
}
/* the lane the cards' lane values are (the mixer: the cursor row's; elsewhere the selected lane) */
static uint32_t effv_lane_l(void)
{
    uint32_t r = ui.row[SCR_HOME] % MXR_N;
    return ui.scr == SCR_HOME && r >= MXR_LANE0 ? r - MXR_LANE0 : lane_selected();
}

/* ---- synths */
typedef struct {
    int32_t cut, shp, pit;                              /* voice.c's m.cutoff, m.shape - (64 << 8) (Q8 steps), the
                                                         * pitch's LFO + ENV (1/4096 semitone) */
} effv_mod_t;
/* the last note played still sounding: the newest active voice (age: voice.c voice_start), 0 none */
static const voice_t *effv_voice(const track_t *t)
{
    const voice_t *b = 0;
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && (!b || t->v[i].age > b->age))
            b = &t->v[i];
    return b;
}
/* part k's value id as it plays (the macros: as mac_pre writes it for the block) */
static int32_t effv_p(uint32_t k, uint32_t id)
{
#if FELUCCA_MACROS
    return mac_effective(k, id, trk[k % NTRK].p[id]);
#else
    return trk[k % NTRK].p[id];
#endif
}
/* part k's modulation for voice v (0: none sounding: the envelope's terms 0) and its LFO lfo (Q15, its fade in): the
 * terms of voice.c track_render, the same integer arithmetic (tests/ui_optimist_effval.h checks them against it) */
static void effv_mods(uint32_t k, const voice_t *v, int32_t lfo, effv_mod_t *m)
{
    const track_t *t = &trk[k % NTRK];
    int32_t env = v ? v->env >> 9 : 0, penv = v ? v->penv : 0, dep[4] = {0, 0, 0, 0}, j;
    lfo_mod_t lm = {{0, 0, 0}, -1, -1};
    for (j = 0; j < 2; j++) {                           /* LFO DEST's slots on the voices (voice.c lfo1_voices; the macros: */
        int32_t d = t->p[P_L1DA + 2 * j];               /* as mac_pre writes them) */
        if (LT_CLASSIC(d))
            dep[d - LT_PIT] += effv_p(k, P_L1AA + 2u * (uint32_t)j);
    }
    lfo_apply(dep, lfo, (lfo + 32768) >> 1, &lm, &lm.g1);
#if FELUCCA_LFO2
    if (k < NPART && LFO2_ON(t)) {                      /* LFO 2's block (voice.c lfo2_tick: its phase and S&H value as the
                                                         * audio side left them) */
        int32_t w = lfo_shape((uint32_t)t->p[P_L2WAVE], lfo2[k].ph, lfo2[k].rnd), u = (w + 32768) >> 1, d2[4] = {0, 0, 0, 0};
        lfo_dep(&t->p[P_L2DA], d2);
        lfo_apply(d2, t->p[P_L2POL] ? u : w, u, &lm, &lm.g2);
    }
#endif
    m->cut = lm.m[1] + ((env * effv_p(k, P_ED_FLT)) >> 7);
    if (v && v->vel > 110)                              /* (an accent opens the filter with the envelope) */
        m->cut += (env * 24) >> 7;
    m->shp = lm.m[2] + ((env * effv_p(k, P_ED_SHP)) >> 7);
    m->pit = lm.m[0] + ((penv * effv_p(k, P_ED_PIT) * 3) >> 7);
}
/* each engine's (by UID) EDIT value its render adds the cutoff (low 4 bits) and the shape (high 4 bits) to, as (p[P_E0 +
 * n] << 8) + m->cutoff (the engines' render, grep m->cutoff / m->shape); 0xF none (DIGITAL and CZ fold both into one
 * index, WHEEL and ACID take neither, ANALOG's shape is its pulse width, no card) */
#define EFFV_NONE 0xFu
#if FELUCCA_ANALOG2
static const uint8_t EFFV_DEST[ENG_UID_N] = {0xF4, 0xFF, 0xF2, 0xF7, 0xF4, 0x4F, 0x75,   /* ANALOG CUT, -, PHASE DPTH,
                                                                                         * LOFI TONE, SAMPLE CUT, VOICE
                                                                                         * BUZZ, TRIO CUT / DUTY */
                                             0xFF, 0x17, 0x1F, 0xF7, 0xF2, 0xFF, 0xFF,   /* -, GRAIN TONE / POS, FM6 MOD,
                                                                                         * SLICE TONE, PHYS BRIT, -, -, */
                                             0x14,                                       /* JUNO CUT / PW (UID 14, as
                                                                                         * feat/engine-juno) */
                                             0x12};                                      /* NOISE FREQ / COLR */
#else
static const uint8_t EFFV_DEST[ENG_UID_N] = {0xF4, 0xFF, 0xF2, 0xF7, 0xF4, 0x4F, 0x75, 0xFF, 0x17, 0xF5, 0x1F, 0xF7};
#endif                                                  /* (SUPER CUT at 9) */
/* part t's cutoff (shape: 1) card, P_E0 + n; -1 none (the engine asked for, as the cards: track_desc) */
static int32_t effv_dest(const track_t *t, uint32_t shape)
{
    uint32_t n = (EFFV_DEST[eng_uid(t->eng_req % NENGINES) % ENG_UID_N] >> (shape ? 4u : 0u)) & 15u;
    return n == EFFV_NONE ? -1 : (int32_t)(P_E0 + n);
}
/* the destination card dest (-1 none) moved by mod (Q8 steps): its value now on its own scale; no card: mod alone */
static void effv_at(uint32_t k, int32_t dest, int32_t mod, int32_t *val, int32_t *mn, int32_t *mx)
{
    const param_desc_t *d = dest >= 0 ? track_desc(&trk[k % NTRK], (uint32_t)dest) : 0;
    if (!d) {
        *mn = -64, *mx = 63;
        *val = clamp(mod >> 8, -64, 63);
        return;
    }
    *mn = d->min, *mx = d->max;
    *val = clamp(effv_p(k, (uint32_t)dest) + (mod >> 8), d->min, d->max);
}
#if FELUCCA_LFO2
#define EFFV_NLFO (k < NPART ? 2u : 1u)
#define EFFV_FIRST(j) ((j) ? P_L2DA : P_L1DA)
#define EFFV_LV(j) ((j) ? lfo2[k].v : mulq15(t->lfo_val, t->lfo_fade))
#else
#define EFFV_NLFO 1u
#define EFFV_FIRST(j) ((void)(j), P_L1DA)
#define EFFV_LV(j) ((void)(j), mulq15(t->lfo_val, t->lfo_fade))
#endif
/* part k's value LT_RES .. LT_INS2 (DEST d; INS1 INS2: the amount of the insert in that slot) as the render has it now (voice.c lfo_over_in: its slots on it in turn, from
 * the LFOs' values of the block before) into *val, its range; 0 for another destination (XRATE XAMT) */
static uint32_t effv_over(uint32_t k, int32_t d, int32_t *val, int32_t *mn, int32_t *mx)
{
    const track_t *t = &trk[k % NTRK];
    const param_desc_t *ds;
    uint32_t q, j, s, f;
    int32_t v;
    ds = lo_dest(t, d, &q);
    if (!ds || ds->max <= ds->min)
        return 0;
    v = effv_p(k, q);
    for (j = 0; j < EFFV_NLFO; j++)
        for (s = 0; s < 2u; s++) {
            f = EFFV_FIRST(j);
            if (t->p[f + 2u * s] == d)
                v = lo_move(v, ds, effv_p(k, f + 2u * s + 1u), EFFV_LV(j));
        }
    *mn = ds->min, *mx = ds->max;
    *val = v;
    return 2;
}
/* part k's value id now into *val (its scale: *mn..*mx); 0 nothing moves it, 1 a value, 2 an amount (shown while it acts) */
static uint32_t effv_synth(uint32_t k, uint32_t id, int32_t *val, int32_t *mn, int32_t *mx)
{
    const track_t *t = &trk[k % NTRK];
    const voice_t *v = effv_voice(t);
    int32_t dc = effv_dest(t, 0), ds = effv_dest(t, 1);
    effv_mod_t m;
    effv_mods(k, v, mulq15(t->lfo_val, t->lfo_fade), &m);
    if ((int32_t)id == dc || (int32_t)id == ds) {
        effv_at(k, (int32_t)id, (int32_t)id == dc ? m.cut : m.shp, val, mn, mx);
        return 1;
    }
    if (id == P_L1AA || id == P_L1AB
#if FELUCCA_LFO2
        || id == P_L2AA || id == P_L2AB
#endif
    ) {                                                 /* a slot's AMT: its destination now, on that card's scale */
        int32_t d = t->p[id - 1u];
        if (!d || !t->p[id])
            return 0;
        if (!LT_CLASSIC(d))
            return effv_over(k, d, val, mn, mx);        /* (a value moved for the block: voice.c lfo_over) */
        if (d == LT_PIT) {
            *mn = -192, *mx = 192;                      /* (1/16 semitone: +-12) */
            *val = clamp(m.pit >> 8, -192, 192);
        } else if (d == LT_AMP) {
            *mn = 0, *mx = 127;
            *val = v ? v->env_out >> 8 : 0;             /* (the voice's level now: envelope, velocity, the LFO's dip) */
        } else {
            effv_at(k, d == LT_FLT ? dc : ds, d == LT_FLT ? m.cut : m.shp, val, mn, mx);
        }
        return 2;
    }
    switch (id) {
    case P_ED_FLT:
    case P_ED_SHP:
        if (!effv_p(k, id))
            return 0;
        if (id == P_ED_FLT)
            effv_at(k, dc, m.cut, val, mn, mx);
        else
            effv_at(k, ds, m.shp, val, mn, mx);
        return 2;
    case P_ED_PIT:
        if (!effv_p(k, id))
            return 0;
        *mn = -192, *mx = 192;                          /* (1/16 semitone: +-12) */
        *val = clamp(m.pit >> 8, -192, 192);
        return 2;
    default:
        break;
    }
#if FELUCCA_MACROS
    {
        const param_desc_t *d = track_desc(t, id);
        if (!d || d->max <= d->min)
            return 0;
        *mn = d->min, *mx = d->max;
        *val = mac_effective(k, id, t->p[id]);
        return 1;
    }
#else
    return 0;
#endif
}

/* ---- the cell: its actual bar (c->ak, amin..amax at av); 0 none, 1 shown (it differs, or always: an absolute drum
 * value, an acting amount), 2 computed and equal to the entered value (draw_cards keeps a bar that just left for a moment,
 * so an LFO crossing it does not blink) */
static uint32_t effv_cell(cell_t *c)
{
    int32_t val, mn, mx, e;
    uint32_t r = 1;
    c->ak = GK_NONE;
    if (c->kind != CK_VAL || !c->vp || !c->d || (ui.scr == SCR_STEP && st.held))
        return 0;
    e = *c->vp;
    if (c->vp >= dsv && c->vp < dsv + DE_N) {           /* a drum lane value */
        if (effv_lane(effv_lane_l(), (uint32_t)(c->vp - dsv), &val, &mn, &mx) == 1u && val == e)
            r = 2;
#if LANE_LFO_UI
    } else if (c->vp == &dlfv[DLF_AMT1] || c->vp == &dlfv[DLF_AMT2]) {   /* a lane LFO's AMT: its destination now */
        const lfo_slot_t *s = &dlfo_get(lane_selected())->s[c->vp == &dlfv[DLF_AMT2]];
        if (!lfo_slot_live(s))
            return 0;
        (void)effv_lane(lane_selected(), s->dest - 1u, &val, &mn, &mx);
#endif
    } else if (!is_drum(TSEL) && c->vp >= TSEL->p && c->vp < TSEL->p + P_COUNT) {   /* a synth part's value */
        uint32_t k = effv_synth(song.sel, (uint32_t)(c->vp - TSEL->p), &val, &mn, &mx);
        if (!k)
            return 0;
        if (k == 1u && val == e)
            r = 2;
    } else {
        return 0;
    }
    if (mx <= mn)
        return 0;
    c->amin = (int16_t)mn;
    c->amax = (int16_t)mx;
    c->av = (int16_t)clamp(val, mn, mx);
    c->ak = mn < 0 && mx > 0 ? GK_BIP : GK_BAR;
    return r;
}
