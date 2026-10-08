/* SPDX-License-Identifier: GPL-3.0-only */
/* Ours -> a SLOOP 2.4 project (isod89/sloop-fm1 v2.4, 8d3823f: "FUN5", 3840 B), FELUCCA_SL24_EXPORT (backports24.h):
 * the reverse of sl24_import.c, lossy where Optimist has more than 2.4. Included by project.c after sl24_import.c.
 * Writes nothing to flash: the editor reads the result (ed_sl24.c, command 78) and saves it as a SLOOP 2.4 backup
 * file that 2.4's own editor restores (BACKUP > RESTORE: its object 0, the working project, and 1, the settings).
 *
 * Translated (sl24_import.c, backwards):
 *   - values 0..49 (P_LEVEL .. P_CHORD) as they are; our P_E0..P_E7 -> its 53..60; its 50 TFLT, 51 STRUM, 52 VLEAD
 *     from our P_TFLT P_STRUM P_VLEAD when built (SL24_TP: px_unpack), else 0 (their default: off). LOST: P_FXOFF,
 *     ANALOG 2's pages and ENV2's extras (2.4 has none);
 *   - engines: our UIDs 0..10 are 2.4's numbers; PHYS, ACID, CZ (11..13) -> their fallback (registry.h: ANALOG,
 *     ANALOG, PHASE) with that engine's EDIT defaults (LOST: the engine);
 *   - FM6 parts: VOICE R01..R16 -> the closest of 2.4's factory patches F1..F8 (SL24_PTCH; the importer's
 *     SL24_FM6V images map back to themselves), U01..U32 -> F1; 2.4's macros ALG FB MLVL MRAT MEG VMOD DTUN at 0
 *     (the patch as it is); preset = PTCH. LOST: the voice itself (and its edits), MOD M.TIM C.TIM ENGINE;
 *   - the drum track: kits 0..36 as they are; ours past 36 (X0X, user kits) -> 808 (LOST); its lanes' record (sends,
 *     per-lane edits, dl_hash) LOST;
 *   - globals as they are but g[12] (our MIDI channels; 2.4's MIDI OUT flag) and g[14] (our G_VIEW; 2.4's MIDI IN
 *     route), 0 = 2.4's defaults (a 2.4 load never reads them: settings of the FM-1), and G_SYNC AUTO -> INT;
 *     the reserved bytes (reverb type, master COMP / LIMIT) LOST;
 *   - the step extras (stepx.h, 2.4's own layout) into each track's tail, from the automation store (auto.h
 *     auto_to_stepx, ed_sl24.c): nudges and fills as they are; the first 24 step-only events of a sound value as locks
 *     with 2.4's ids, the ones 2.4 has no parameter for dropped (LOST: FX OFF, ANALOG 2, an FM6 or fallback part's
 *     EDIT; more than 24: LOCK). Hold events (motion) and chance are not 2.4's (LOST: MOTION, CHANCE; the editor
 *     says "motion not in 2.4").
 * proj_to_sl24 says what was lost (SX24_*, the editor tells the user). */
#define SL24_MAGIC 0x46554E35u                         /* (as sl24_import.c: identical definitions) */
#define SL24_SIZE 3840u
#define SL24_NP 61u
#define SL24_E0 53u
#define SL24_COMMON 50u
#define SL24_KITS 37u
#define SL24_TRK 940u
#define SL24_TAIL (SL24_NP * 2u + 2u + 640u)
#define SL24_TFLT 50u
#define SL24_HDR (12u + 2u * PJ_NG)                     /* where its tracks start (magic, size, g[32], sel, 3 spare) */
_Static_assert(SL24_TAIL == 764u && SL24_TAIL + 176u == SL24_TRK && SL24_HDR + NTRK * SL24_TRK + 4u == SL24_SIZE &&
               sizeof(step_t) * NSTEP == 640u && sizeof(stepx_t) == 176u && P_CHORD == SL24_COMMON - 1u &&
               DRUM_SAMPLED == 5u && ENG_UID_FM6 == 9u && ENG_UID_SLICE == 10u,
               "SLOOP 2.4's FUN5 (tests/sl24_fun5_gen.c), its kit 5 = 808, its engines 0..10 our UIDs");
#define SL24_PERSIST 88u                                /* 2.4's persist_t (its settings, BACKUP object 1) */

enum {                                                  /* what an export lost (2 x 7 bits on the wire) */
    SX24_ENGINE = 1, SX24_FM6 = 2, SX24_FXOFF = 4, SX24_A2 = 8, SX24_KIT = 16, SX24_LOCK = 32, SX24_LANES = 64,
    SX24_MASTER = 128, SX24_MOTION = 256, SX24_CHANCE = 512
};
/* our factory VOICE R01..R16 (eng_fm6.c FM6_PRESETS) -> 2.4's PTCH F1..F8 (TINE EP, GLASS BELL, ROUND BASS, BRASS
 * SECT, SOFT PAD, WOOD BARS, DRAWBARS, NYLON PICK): TINE EP, BRASS SECT, SOLID BASS, BELLS, FM MARIMBA, CLAVINET,
 * DRAWBARS, STRINGS, FM GLASS, FM SYNC LD, HARP, FM KALIMBA, FLUTE, STEEL DRUM, SAW BASS, TUBULAR */
static const uint8_t SL24_PTCH[16] = {0, 3, 2, 1, 5, 7, 6, 4, 4, 3, 7, 5, 4, 5, 2, 1};
#if defined(SL24_TP) && SL24_TP
static int px_unpack(const project_t *p, int16_t (*x)[3]);   /* project.c: our TFLT STRUM VLEAD */
#endif

static int16_t sl24_kit_out(int32_t k) { return (int16_t)(k >= 0 && k < (int32_t)SL24_KITS ? k : (int32_t)DRUM_SAMPLED); }
/* the UID 2.4 plays for ours: its own, or the first fallback within 2.4's 0..10 */
static uint32_t sl24_eng_out(uint32_t uid)
{
    uint32_t n;
    for (n = 0; n < 4u && uid >= ENG_UID_SLICE + 1u && uid < ENG_UID_N; n++)
        uid = ENG_FALLBACK[uid];
    return uid <= ENG_UID_SLICE ? uid : 0u;
}
/* our parameter id on track trk -> 2.4's, -1 none. e: the track's E0..E7 mean what 2.4's do (not FM6, no fallback) */
static int32_t sl24_param_out(uint32_t id, uint32_t trk, int e)
{
    if (id < SL24_COMMON)
        return (int32_t)id;
    if (id >= P_E0 && id <= P_E7)
        return trk == TRK_DRUM || e ? (int32_t)(SL24_E0 + id - P_E0) : -1;
#if defined(SL24_TP) && SL24_TP
    if (id == P_TFLT || ((id == P_STRUM || id == P_VLEAD) && trk != TRK_DRUM))
        return (int32_t)(SL24_TFLT + id - P_TFLT);
#endif
    return -1;
}
static void sl24_w16(uint8_t *o, int32_t v) { o[0] = (uint8_t)v, o[1] = (uint8_t)((uint32_t)v >> 8); }
/* global i of q as 2.4 keeps it: G_MIDI (our MIDI channels) and G_VIEW (2.4's G_ROUTE) 0, SYNC AUTO -> INT, the drum
 * reverb our lanes took (DRREV_MOVED) -> 2.4's default 16 */
static int32_t sl24_g_out(const project_t *q, uint32_t i)
{
    int32_t v = q->g[i];
    if (i == G_MIDI || i == G_VIEW || (i == G_SYNC && (v < 0 || v > 2)))
        return 0;
    return i == G_DRREV && v < 0 ? 16 : v;
}

/* track k's stored values -> 2.4's p[61], engine, preset at o; -> SX24_* lost */
static uint32_t sl24_trk_out(const project_t *q, uint32_t k, const int16_t *px, uint8_t *o, int *e_ok)
{
    int16_t v[P_COUNT];
    uint32_t i, lost = 0, uid = q->t[k].engine, eng = k == TRK_DRUM ? 0u : sl24_eng_out(uid);
    pj_to_p(v, q->t[k].p);
    for (i = 0; i < SL24_COMMON; i++)
        sl24_w16(o + 2u * i, v[i]);
    for (i = 0; i < 3u; i++)
        sl24_w16(o + 2u * (SL24_TFLT + i), px[i]);
    for (i = 0; i < 8u; i++)
        sl24_w16(o + 2u * (SL24_E0 + i), v[P_E0 + i]);
    o[2u * SL24_NP] = (uint8_t)eng;
    o[2u * SL24_NP + 1u] = q->t[k].preset;
    lost |= v[P_FXOFF] ? SX24_FXOFF : 0u;
    *e_ok = 1;
    if (k == TRK_DRUM) {
        sl24_w16(o + 2u * SL24_E0, sl24_kit_out(v[P_E0]));
        lost |= v[P_E0] != sl24_kit_out(v[P_E0]) ? SX24_KIT : 0u;
#if defined(SL24_TP) && SL24_TP
        if (px[3]) {                                    /* (its GLMODE PRIO ALLOC DTUNE slots held TFLT STRUM: px_pack) */
            static const uint8_t S[4] = {P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE};
            for (i = 0; i < 4u; i++)
                sl24_w16(o + 2u * S[i], TP[S[i]].def);
        }
#endif
        return lost;
    }
#if FELUCCA_ANALOG2
    for (i = P_A2WAVE; i < PJ_E0; i++)                  /* (stored: P_A2WAVE .. P_A2SDTN; the rest in pj_x) */
        lost |= v[i] != TP[i].def ? SX24_A2 : 0u;
    for (i = 0; i < PROJ_XW; i++)
        lost |= q->t[TRK_DRUM].p[P_A2WAVE + PROJ_XW * k + i] ? SX24_A2 : 0u;
#endif
    if (uid == ENG_UID_FM6) {                           /* PTCH from VOICE, the macros at 0, preset = PTCH */
        uint32_t pt = v[P_E0] >= 0 && v[P_E0] < 16 ? SL24_PTCH[v[P_E0]] : 0u;
        for (i = 0; i < 8u; i++)
            sl24_w16(o + 2u * (SL24_E0 + i), i == 7u ? (int32_t)pt : 0);
        o[2u * SL24_NP + 1u] = (uint8_t)pt;
        *e_ok = 0;
        return lost | SX24_FM6;
    }
    if (eng != uid) {                                   /* (2.4 has not this engine: its fallback, at its defaults) */
        for (i = 0; i < 8u; i++)
            sl24_w16(o + 2u * (SL24_E0 + i), eng_built(eng) ? ENGINES[eng_slot_built(eng)]->edit[i].def : 0);
        o[2u * SL24_NP + 1u] = 0;
        *e_ok = 0;
        lost |= SX24_ENGINE;
    }
    return lost;
}
/* track k's extras x (0: none) -> 2.4's tail at o, locks converted; -> SX24_* lost */
static uint32_t sl24_tail_out(const stepx_t *x, uint32_t k, int e_ok, uint8_t *o)
{
    stepx_t d;
    uint32_t i, m = 0, lost = 0;
    stepx_clear(&d);
    if (x) {
        memcpy(d.micro, x->micro, sizeof d.micro);
        memcpy(d.fill, x->fill, sizeof d.fill);
        for (i = 0; i < NLOCK; i++) {
            plock_t l = x->lock[i];
            int32_t p = stepx_lock_used(&l) ? sl24_param_out(l.param, k, e_ok) : -2;
            if (p < 0) {
                lost |= p == -1 ? SX24_LOCK : 0u;
                continue;
            }
            if (k == TRK_DRUM && l.param == P_E0) {
                lost |= l.val != sl24_kit_out(l.val) ? SX24_KIT : 0u;
                l.val = sl24_kit_out(l.val);
            }
            l.param = (uint8_t)p;
            d.lock[m++] = l;
        }
    }
    memcpy(o, &d, sizeof d);                            /* (2.4's layout: stepx.h) */
    return lost;
}

#if FELUCCA_AUTO
/* track k's automation (seq/auto.h) as 2.4's extras x: its nudges, fills and first NLOCK step-only events of a value;
 * -> SX24_* of what 2.4 has no place for: MOTION (hold events), CHANCE, LOCK (more than NLOCK) */
static uint32_t sl24_auto_out(stepx_t *x, const auto_list_t *l)
{
    uint32_t r = auto_to_stepx(x, l);
    return (r & AUTO_X_HOLD ? SX24_MOTION : 0u) | (r & AUTO_X_CHANCE ? SX24_CHANCE : 0u) | (r & AUTO_X_LOCKS ? SX24_LOCK : 0u);
}
#endif
/* q (ours, valid) and its extras x[k] (0: none) -> a SLOOP 2.4 project at o (SL24_SIZE bytes, sl24_is says yes);
 * returns what was lost (SX24_*) */
static uint32_t proj_to_sl24(const project_t *q, const stepx_t *const *x, uint8_t *o)
{
    int16_t px[NTRK][4];
    uint32_t i, k, lost = 0, s;
    memset(o, 0, SL24_SIZE);
    memset(px, 0, sizeof px);
#if defined(SL24_TP) && SL24_TP
    {
        int16_t t[NTRK][3];
        int has = px_unpack(q, t);
        for (k = 0; k < NTRK; k++)
            px[k][0] = t[k][0], px[k][1] = t[k][1], px[k][2] = t[k][2], px[k][3] = (int16_t)has;
    }
#endif
    s = SL24_MAGIC, memcpy(o, &s, 4);
    s = SL24_SIZE, memcpy(o + 4, &s, 4);
    for (i = 0; i < PJ_NG; i++)
        sl24_w16(o + 8u + 2u * i, sl24_g_out(q, i));
    o[8u + 2u * PJ_NG] = q->sel < NTRK ? q->sel : 0u;
    lost |= q->rsv[0] | q->rsv[1] | q->rsv[2] ? SX24_MASTER : 0u;
#if FELUCCA_ANALOG2
    lost |= q->dl_hash ? SX24_LANES : 0u;
#endif
    for (k = 0; k < NTRK; k++) {
        uint8_t *t = o + SL24_HDR + k * SL24_TRK;
        int e_ok;
        lost |= sl24_trk_out(q, k, px[k], t, &e_ok);
        memcpy(t + 2u * SL24_NP + 2u, q->t[k].step, 640u);
        lost |= sl24_tail_out(x ? x[k] : 0, k, e_ok, t + SL24_TAIL);
    }
    s = proj_hash(o, SL24_SIZE - 4u);
    memcpy(o + SL24_SIZE - 4u, &s, 4);
    return lost;
}

/* 2.4's settings (its persist_t, "PER3", 88 B: magic, palette, low cut, zoom, the panel table, its song order, its
 * lights word) at o, from ours: the panel table as it is (the same PAN5 panel_t, 32 B), its song order 2.4's
 * default (A B C D, 4 bars each), its word from ours (sl24_word) */
static void sl24_persist(uint8_t *o, uint32_t palette, uint32_t lowcut, uint32_t zoom, const void *panel, uint32_t word)
{
    uint32_t w[4] = {0x50455233u, palette < 5u ? palette : 0u, lowcut != 0u, zoom != 0u}, i;
    memset(o, 0, SL24_PERSIST);
    memcpy(o, w, sizeof w);
    memcpy(o + 16, panel, 32u);
    o[48] = 4;                                          /* count 4, loop 0, 2 spare, then 16 x {scene, bars} */
    for (i = 0; i < 16u; i++)
        o[52u + 2u * i] = (uint8_t)(i % 4u), o[53u + 2u * i] = 4;
    memcpy(o + 84, &word, 4);
}
/* our settings word (project.c bp23_word: 2.3's bits) and SYNC -> 2.4's lights word (its panel.c lights_word): bits
 * 0..7 LIGHTS KEYS, 9 10 the REC screen's, 14 MIDI OUT SEQ, 15 IN CLOCK, 16 USB SERIAL as they are; 8 the inverse
 * (ours NOTES OFF, 2.4's notes lit); 12..13 SYNC (INT USB TRS; AUTO -> INT); USB AUDIO (11), the visualiser 0 */
static uint32_t sl24_word(uint32_t w, int32_t sync)
{
    return (w & 0x1C6FFu) | (~w & 0x100u) | (uint32_t)(sync >= 0 && sync <= 2 ? sync : 0) << 12;
}
