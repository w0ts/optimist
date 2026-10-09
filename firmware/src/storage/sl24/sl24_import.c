/* SPDX-License-Identifier: GPL-3.0-only */
/* A SLOOP 2.4 project (isod89/sloop-fm1 v2.4, 8d3823f: "FUN5", 3840 B, project.c project_t) -> ours, FELUCCA_SL24_IMPORT
 * (backports24.h). Included by project.c after the other formats' readers. Never called by proj_import: a 2.4 project
 * is imported only when asked (sl24_guard.c sl24_load: LOAD twice on a SLOOP 2.4 slot); its original stays in flash.
 *
 * 2.4's track: p[61] (P_COUNT 61), engine, preset, 64 10-byte steps (ours, byte for byte), then its step extras
 * (stepx.h, 176 B). Translated:
 *   - values 0..49 (P_LEVEL .. P_CHORD): ours, as they are; 50 P_TFLT, 51 P_STRUM, 52 P_VLEAD -> our P_TFLT, P_STRUM,
 *     P_VLEAD (FELUCCA_TRK_FILT: FILT; FELUCCA_CHORDPLUS: STRUM and VLEAD, the parts'; packed by px_pack, locks too),
 *     without those switches LOST (their defaults); 53..60 P_E0..P_E7 -> our P_E0..P_E7; ours past 49 (P_FXOFF, ANALOG 2, ENV2's extras): defaults;
 *   - engines: 2.4's 0..10 are our UIDs (ANALOG .. GRAIN, FM6 9, SLICE 10): as they are;
 *   - FM6 parts: 2.4's EDIT is ALG FB MLVL MRAT MEG VMOD DTUN PTCH, macros over the patch PTCH picks (F1..F8 its
 *     factory patches, B1..B27 its patch bank); ours is VOICE MOD M.TIM C.TIM ENGINE with the voice in the project.
 *     The patch itself becomes the part's voice (sl24_fm6.c: F1..F8 2.4's own, a bank slot's record from fv[] (the
 *     caller: 2.4's bank in flash, or the editor's file), none: 2.4's INIT), ALG FB MRAT MEG VMOD put into it as
 *     2.4 plays them, MLVL -> MOD; VOICE the closest of our factory voices (SL24_FM6V; a bank patch: R01), only a
 *     name now. LOST: DTUN (the carriers' spread);
 *   - the drum track's kit (P_E0, and its locks): 2.4's 0..36 (5 sampled, 32 synthesised: the same generator,
 *     tools/gen_drumkits.py) are ours; its USR1..USR4 and USR3+4 kits (37..41): our default kit; USR1..USR3 as the
 *     kit (not a lock) -> each drum lane plays its sound from that user sample slot, as 2.4 does (sl24_usr_lanes:
 *     the caller's drum record, FELUCCA_DRUM_USR); USR4 (not one of our slots) and a lock on a USR kit: LOST;
 *   - globals: as they are but g[14], 2.4's G_ROUTE (MIDI IN = CLOCK; our G_VIEW): our default; DTIME 1/8D 1/16D and
 *     DIV 1/2..2BAR (appended values) are clamped by proj_apply until ours has them;
 *   - the step extras: nudges and fills as they are; locks with their param converted (as the values above; one on
 *     an FM6 part's EDIT, or on TFLT STRUM VLEAD without their switches: dropped). Into x[NTRK] (stepx.h; 0: dropped). */
#define SL24_MAGIC 0x46554E35u                         /* "FUN5", as our SLOOP plus format, told apart by size */
#define SL24_SIZE 3840u

/* n bytes at b are a SLOOP 2.4 project (project.c proj_import there: magic, size, FNV-1a sum) */
static int sl24_is(const void *b, int n)
{
    const uint8_t *c = (const uint8_t *)b;
    uint32_t m, z, s;
    if (n != (int)SL24_SIZE)
        return 0;
    memcpy(&m, c, 4), memcpy(&z, c + 4, 4), memcpy(&s, c + SL24_SIZE - 4u, 4);
    return m == SL24_MAGIC && z == SL24_SIZE && s == proj_hash(c, SL24_SIZE - 4u);
}
#if FELUCCA_SL24_IMPORT
#define SL24_NP 61u                                     /* 2.4's P_COUNT */
#define SL24_TFLT 50u                                   /* its P_TFLT, P_STRUM, P_VLEAD (ours: P_TFLT + 0, 1, 2) */
#define SL24_STRUM 51u
#define SL24_VLEAD 52u
#define SL24_E0 53u                                     /* its P_E0 */
#define SL24_COMMON 50u                                 /* its values 0..49 are ours */
#define SL24_KITS 37u                                   /* its kits 0..36 are ours (DRUM_SAMPLED + DS_NKITS) */
#define SL24_TRK 940u                                   /* its proj_trk_t */
#define SL24_TAIL (SL24_NP * 2u + 2u + 640u)            /* where the track's extras start (764) */
_Static_assert(SL24_TAIL == 764u && SL24_TAIL + 176u == SL24_TRK && 12u + 2u * PJ_NG + NTRK * SL24_TRK + 4u == 3840u,
               "SLOOP 2.4's FUN5 (measured with its own struct: tests/sl24_fun5_gen.c)");
_Static_assert(P_CHORD == SL24_COMMON - 1u && NTRK == 4u && PJ_NG == 32u, "2.4's ids 0..49 and globals are ours");
/* 2.4's FM6 factory patches F1..F8 (tools/gen_fm6_patches.py: TINE EP, GLASS BELL, ROUND BASS, BRASS SECT, SOFT PAD,
 * WOOD BARS, DRAWBARS, NYLON PICK) -> our factory VOICE: TINE EP, BELLS, SOLID BASS, BRASS SECT, FM GLASS (the glass
 * pad), FM MARIMBA, DRAWBARS, HARP */
static const uint8_t SL24_FM6V[8] = {0, 3, 2, 1, 8, 4, 6, 10};

static int16_t sl24_kit(int32_t k) { return (int16_t)(k >= 0 && k < (int32_t)SL24_KITS ? k : (int32_t)DRUM_DEFAULT_KIT); }
/* 2.4's parameter id on track trk (engine eng) -> ours, -1 none */
static int32_t sl24_param(uint32_t id, uint32_t trk, uint32_t eng)
{
    if (id < SL24_COMMON)
        return (int32_t)id;
#if FELUCCA_TRK_FILT
    if (id == SL24_TFLT)
        return (int32_t)P_TFLT;
#endif
#if FELUCCA_CHORDPLUS
    if (id == SL24_STRUM || id == SL24_VLEAD)
        return trk != TRK_DRUM ? (int32_t)(P_TFLT + id - SL24_TFLT) : -1;   /* (the parts' only) */
#endif
    if (id < SL24_E0 || id >= SL24_NP || (trk != TRK_DRUM && eng == ENG_UID_FM6))
        return -1;                                      /* (TFLT STRUM VLEAD without their switches; FM6's macros) */
    return (int32_t)(P_E0 + id - SL24_E0);
}

/* a SLOOP 2.4 project (sl24_is) at b holds step extras: a nudge, a lock or a fill */
static int sl24_has_extras(const uint8_t *b)
{
    uint32_t i, k;
    for (i = 0; i < NTRK; i++) {
        const uint8_t *t = b + 12u + 2u * PJ_NG + i * SL24_TRK + SL24_TAIL;
        for (k = 0; k < SX_NSTEP; k++)
            if (t[k] || (k < SX_NSTEP / 4u && t[160u + k]) || (k < NLOCK && t[64u + 4u * k] < SX_NSTEP))
                return 1;
    }
    return 0;
}
/* track k of a SLOOP 2.4 project (sl24_is) at b: its PTCH when it is an FM6 part, else -1 */
static int sl24_ptch(const uint8_t *b, uint32_t k)
{
    const uint8_t *s = b + 12u + 2u * PJ_NG + k * SL24_TRK;
    return k < NPART && s[2u * SL24_NP] == ENG_UID_FM6 ? (int)(int16_t)(s[2u * (SL24_E0 + 7u)] | s[2u * (SL24_E0 + 7u) + 1u] << 8)
                                                         : -1;
}
/* its drum kit (the drum track's P_E0) */
static int32_t sl24_kit_of(const uint8_t *b)
{
    const uint8_t *s = b + 12u + 2u * PJ_NG + TRK_DRUM * SL24_TRK + 2u * SL24_E0;
    return (int16_t)(s[0] | s[1] << 8);
}
/* n bytes at b, a SLOOP 2.4 project (sl24_is) -> q (FUNB) and its extras x[NTRK] (0: not wanted); fv[k] (fv or an
 * entry 0: none) the packed record of FM6 part k's bank patch (PTCH B1..B27); 0 = not one */
static int proj_from_sl24(project_t *q, const void *b, int n, stepx_t *x, const uint8_t *const *fv)
{
    const uint8_t *c = (const uint8_t *)b;
    uint32_t i, k;
    int16_t v[P_COUNT];
#if SL24_TP
    int16_t px[NTRK][3] = {{0}};                        /* TFLT STRUM VLEAD (px_pack) */
#endif
    if (!sl24_is(b, n))
        return 0;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);          /* FM6 functions: the defaults */
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, c + 8, sizeof q->g);
    q->g[G_VIEW] = GP[G_VIEW].def;                      /* (their G_ROUTE) */
    q->sel = c[8u + sizeof q->g];
    for (i = 0; i < NTRK; i++) {
        const uint8_t *s = c + 12u + sizeof q->g + i * SL24_TRK;
        int16_t e[SL24_NP];
        uint32_t eng = s[2u * SL24_NP];
        memcpy(e, s, sizeof e);
        for (k = 0; k < P_COUNT; k++)
            v[k] = k < SL24_COMMON ? e[k] : k >= P_E0 && k < P_ENG_END ? e[SL24_E0 + k - P_E0] : TP[k].def;
#if SL24_TP
#if FELUCCA_TRK_FILT
        px[i][0] = (int16_t)clamp(e[SL24_TFLT], TP[P_TFLT].min, TP[P_TFLT].max);
#endif
#if FELUCCA_CHORDPLUS
        if (i != TRK_DRUM) {                            /* (the parts' only) */
            px[i][1] = (int16_t)clamp(e[SL24_STRUM], TP[P_STRUM].min, TP[P_STRUM].max);
            px[i][2] = (int16_t)clamp(e[SL24_VLEAD], TP[P_VLEAD].min, TP[P_VLEAD].max);
        }
#endif
#endif
        if (i == TRK_DRUM) {
            v[P_E0] = sl24_kit(e[SL24_E0]);
            eng = 0;
        } else if (eng == ENG_UID_FM6) {                /* the patch as the voice, MOD from MLVL, ENGINE MARK I */
            int32_t pt = e[SL24_E0 + 7u];
            const uint8_t *src = pt >= 0 && pt < (int32_t)SL24_NFAC ? SL24_FM6_F[pt] : fv && fv[i] ? fv[i] : SL24_FM6_INIT;
            memcpy(q->fm6[i], src, 128u);
            sl24_fm6_bake(q->fm6[i], &e[SL24_E0]);
            q->fm6_on[i] = 0x3Fu;                       /* (2.4 has no operator switches: all six on) */
            q->fm6_has |= (uint8_t)(1u << i);
            for (k = 0; k < 8u; k++)
                v[P_E0 + k] = 0;
            v[P_E0] = pt >= 0 && pt < 8 ? SL24_FM6V[pt] : 0;
            v[P_E0 + 1u] = sl24_mod_in(e[SL24_E0 + 2u]);
            v[P_E0 + 4u] = 1;
        }
        pj_from_p(q->t[i].p, v);
        q->t[i].engine = (uint8_t)eng;
        q->t[i].preset = s[2u * SL24_NP + 1u];
        memcpy(q->t[i].step, s + 2u * SL24_NP + 2u, sizeof q->t[i].step);
        if (x) {
            stepx_t *d = &x[i];
            uint32_t m = 0;
            memcpy(d, s + SL24_TAIL, sizeof *d);        /* (2.4's layout: stepx.h) */
            for (k = 0; k < NLOCK; k++) {               /* the locks: our ids, packed; a step past 63: free */
                plock_t l = d->lock[k];
                int32_t p = l.step < SX_NSTEP ? sl24_param(l.param, i, eng) : -1;
                if (p < 0)
                    continue;
                if (i == TRK_DRUM && p == (int32_t)P_E0)
                    l.val = sl24_kit(l.val);
                l.param = (uint8_t)p;
                d->lock[m++] = l;
            }
            for (; m < NLOCK; m++)
                d->lock[m].step = LOCK_FREE, d->lock[m].param = 0, d->lock[m].val = 0;
            for (k = 0; k < SX_NSTEP; k++)              /* a nudge out of range: none (their load clamps it too) */
                if (d->micro[k] < MICRO_MIN || d->micro[k] > MICRO_MAX)
                    d->micro[k] = 0;
        }
    }
#if SL24_TP
    px_pack(q, px);                                     /* (after the drum track's values: in its free slots) */
#endif
    pj_x_reset(q);                                      /* (ENV2's extras: none; the sum) */
    return 1;
}

/* the drum record for a SLOOP 2.4 project (sl24_is) at b -> d (cleared first): its USR1..USR3 kit (USR3+4: USR3's
 * part) as 2.4 plays one, each lane the zone of that user sample slot whose notes hold the lane's GM note
 * (drums.c LANE_NOTE, 2.4's too; FELUCCA_DRUM_USR: a lane on a user sample, drum_edit.c). A lane the slot has no
 * zone for plays our default kit's sound (2.4: silent). -> 1 a lane was set */
static int sl24_usr_lanes(const uint8_t *b, dlrec_t *d)
{
    int r = 0;
    memset(d, 0, sizeof *d);
#if FELUCCA_DRUM_USR && FELUCCA_ANALOG2
    {
        int32_t kit = sl24_kit_of(b);
        uint32_t k = kit == (int32_t)SL24_KITS + 4 ? 2u : (uint32_t)(kit - (int32_t)SL24_KITS), l, j;
        if (kit < (int32_t)SL24_KITS || k >= SMP_USER_SLOTS)
            return 0;                                   /* (not a user kit; USR4: not one of our slots) */
        for (l = 0; l < DRUM_LANES; l++)
            for (j = 0; j < usr_nz[k] && j < 16u; j++)
                if (LANE_NOTE[l] >= usr_zone[k][j].lo && LANE_NOTE[l] <= usr_zone[k][j].hi) {
                    d->l.src[l] = (uint8_t)(DL_USR + k);
                    dl_set_ref(d->l.ref[l], j, 0, 0);
                    r = 1;
                    break;
                }
    }
#else
    (void)b;
#endif
    return r;
}
#if FELUCCA_AUTO
/* 2.4's extras x[NTRK] (proj_from_sl24) -> store m (emptied first: m is the imported project's), as step-only events;
 * -> AUTO_CLAMP when a lock's value was past a signed byte (said in the import's message) */
static uint32_t sl24_auto_in(auto_store_t *m, const stepx_t *x)
{
    uint32_t k, r = 0;
    for (k = 0; k < NTRK; k++)
        m->l[k].n = 0;
    m->on = 0;
    for (k = 0; k < NTRK; k++)
        r |= auto_from_stepx(&m->l[k], &x[k]);
    return r & AUTO_CLAMP;
}
#endif
#endif
