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
 *   - engines: 2.4's 0..9 are our UIDs (ANALOG .. GRAIN, FM6 9): as they are. 2.5 (v2.5 fa9ce57, core.h ENGI_*) added
 *     PHYS 10 and NOISE 11 (FUN5 unchanged: a 2.5 project reads as 2.4's), and moved SLICE (never in a shipped SLOOP)
 *     past them: 10 -> our PHYS (the same EDIT: MODEL STRC BRIT DAMP POS ACC BOW EXC; 2.5's PHYS presets 0..8 ours,
 *     renamed), 11 NOISE and anything past it -> ANALOG at its EDIT defaults, their locks dropped (LOST: the engine);
 *     2.5's byte 73 (G_DRDLY, the drums' delay send, 0..127; 2.4's spare, 0): every drum lane's DLY send, the nearest
 *     of the lanes' 32 levels (sl25_drdly_in: the caller's drum record; as 2.4's G_DRREV becomes the lanes' REV);
 *     APPROXIMATE: 2.5 sends the drums' sum, each lane here sends its own, at 4v + v / 8 (77 -> level 19 = 78);
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
 *     an FM6 part's EDIT, or on TFLT STRUM VLEAD without their switches: dropped). Into x[NTRK] (stepx.h; 0: dropped).
 *
 * A SLOOP 2.6 project (v2.6 d30d655: "FUN6", 3816 B, its project.c project_t) is made a 2.5 one first, in place
 * (sl26_to_sl24, as 2.6's own proj_from_v5 backwards), then imported as above. FUN6 is FUN5 with 2.6's ten parameters
 * P_ITYPE .. P_LPOL (ids 53..62, P_E0 63) in a byte each after P_E7, and each step's nudge (6 bits) and fill (2 bits)
 * in one byte. Translated: the values, steps, nudges, fills and locks as FUN5's (a lock on P_E0.. back to 2.5's id);
 * the ANALOG filter TYPE (P_FTYPE: LP BP HP) of an ANALOG part -> our ANALOG 2's FTYP (LP: as 2.4, BP, HP); LFO 2's
 * SYNC (P_LSYNC n, 1 4BAR .. 10 1/32) -> the part's LFO RATE -n (params.c LRATE_OF_SLOOP; OFF: its free RATE, kept;
 * a lock on SYNC is dropped, below). 2.6's "LFO 2" page is its one LFO's SYNC TRIG POL: a part whose LFO is TRIG FREE or
 * POL UNI (P_LTRIG, P_LPOL; ours restarts on a note and is bipolar) moves to our LFO 2 when built (FELUCCA_LFO2), which
 * has both: its two slots with the LFO's RATE and WAVE (sl26_lfo2_in, what it loses there). LOST:
 *   - the INSERT (TYPE, A, B, C, MIX: P_ITYPE .. P_IMIX), ENV SYNC (P_ESYNC), LFO 2's TRIG and POL without LFO2 (or on
 *     the depths that stay on LFO): not ours; their values and their locks dropped;
 *   - FTYPE on a part that is not ANALOG (2.6 plays it on ANALOG only);
 *   - GLO > TEXTURE (SRC LEVEL TONE MOVE RATE WHEN DUCK, in the spare bytes and the slots of G_DRCH and G_INFO):
 *     those two globals take our defaults;
 *   - the SYN1..SYN4 kits (2.5's 42..45): our default kit (the drums' delay send, byte 73: as 2.5's, the lanes' DLY). */
#define SL24_MAGIC 0x46554E35u                         /* "FUN5", as our SLOOP plus format, told apart by size */
#define SL25_DRDLY (8u + 2u * PJ_NG + 1u)               /* 2.5's G_DRDLY, the byte after sel (its project.c drdly) */
#define SL24_SIZE 3840u
#define SL26_MAGIC 0x46554E36u                         /* "FUN6": SLOOP 2.6 (ours used it in test builds, other sizes) */
#define SL26_SIZE 3816u

/* n bytes at b are a SLOOP project of magic m and size z (project.c proj_import there: magic, size, FNV-1a sum) */
static int sl24_is_of(const void *b, int n, uint32_t m0, uint32_t z0)
{
    const uint8_t *c = (const uint8_t *)b;
    uint32_t m, z, s;
    if (n != (int)z0)
        return 0;
    memcpy(&m, c, 4), memcpy(&z, c + 4, 4), memcpy(&s, c + z0 - 4u, 4);
    return m == m0 && z == z0 && s == proj_hash(c, z0 - 4u);
}
/* a SLOOP 2.4 / 2.5 project (FUN5) */
static int sl24_is(const void *b, int n) { return sl24_is_of(b, n, SL24_MAGIC, SL24_SIZE); }
/* a SLOOP 2.6 project (FUN6) */
static int sl26_is(const void *b, int n) { return sl24_is_of(b, n, SL26_MAGIC, SL26_SIZE); }
/* either: one PROJECT shows as SLOOP's, and imports when asked */
static int sl24_any(const void *b, int n) { return sl24_is(b, n) || sl26_is(b, n); }
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

#define SL24_ENG_LOST 0xFFu                             /* (sl24_eng_in: no engine of ours) */
/* SLOOP's engine number (2.4: 0..9; 2.5 and 2.6: PHYS 10, NOISE 11) -> our UID, SL24_ENG_LOST: none (NOISE, past it) */
static uint32_t sl24_eng_in(uint32_t e)
{
#ifdef ENG_UID_PHYS
    if (e == 10u)
        return ENG_UID_PHYS;
#endif
    return e <= ENG_UID_FM6 ? e : SL24_ENG_LOST;
}
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
    if (id < SL24_E0 || id >= SL24_NP || (trk != TRK_DRUM && (eng == ENG_UID_FM6 || eng == SL24_ENG_LOST)))
        return -1;                                      /* (TFLT STRUM VLEAD without their switches; FM6's macros; the EDIT
                                                         * of an engine not ours) */
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
/* SLOOP 2.6's FUN6 track (its project.c proj_trk_t): p[53] (P_LEVEL .. P_VLEAD) and e[8] (P_E0 .. P_E7) as FUN5's
 * p[61], x[10] (P_ITYPE .. P_LPOL), engine, preset, the steps, mf[64] (nudge | fill << 6), lock[NLOCK] */
#define SL26_TRK 934u
#define SL26_NX 10u                                     /* its parameters in a byte (2.6's own ids 53..62) */
#define SL26_FTYPE 5u                                   /* x[5]: P_FTYPE, ANALOG's filter (LP BP HP) */
#define SL26_LSYNC 7u                                   /* x[7]: P_LSYNC, LFO 2's SYNC (0 OFF, 1 4BAR .. 10 1/32) */
#define SL26_LTRIG 8u                                   /* x[8]: P_LTRIG, LFO 2's TRIG (0 NOTE, 1 FREE) */
#define SL26_LPOL 9u                                    /* x[9]: P_LPOL, LFO 2's POL (0 BI, 1 UNI) */
#define SL26_STEPS (2u * SL24_NP + SL26_NX + 2u)        /* 134: where its steps start */
#define SL26_MF (SL26_STEPS + 640u)                     /* 774 */
#define SL24_MICRO (2u * SL24_NP + 2u + 640u)           /* FUN5: micro[64] (764), lock[NLOCK] (828), fill[16] (924) */
#define SL24_LOCKS (SL24_MICRO + 64u)
#define SL24_FILLS (SL24_LOCKS + 4u * NLOCK)
_Static_assert(SL26_MF + 64u + 4u * NLOCK == SL26_TRK && 12u + 2u * PJ_NG + NTRK * SL26_TRK + 4u == SL26_SIZE &&
               SL24_FILLS + 16u == SL24_TRK && sizeof(plock_t) == 4u && NLOCK == 24u,
               "SLOOP 2.6's FUN6 (tests/sl26_gen.c, with its own types)");
/* a SLOOP 2.6 project (sl26_is) at b, in place -> a 2.5 one (sl24_is; b holds SL24_SIZE bytes). Each track from the
 * last, each part of it from its end (FUN5's are 6 bytes longer: nothing is read after it is written over). -> the
 * parts' ANALOG filter TYPE, 2 bits each (1 BP, 2 HP; 0: LP or not an ANALOG part); with LFO2, bits 8 + 2 k and
 * 9 + 2 k: part k's LFO is TRIG FREE, POL UNI (sl26_lfo2_in) */
static uint32_t sl26_to_sl24(uint8_t *b)
{
    uint32_t i, k, ft = 0, w;
    for (i = NTRK; i-- > 0;) {
        const uint8_t *s = b + 12u + 2u * PJ_NG + i * SL26_TRK;
        uint8_t *d = b + 12u + 2u * PJ_NG + i * SL24_TRK, mf[NSTEP], eng = s[SL26_STEPS - 2u], pre = s[SL26_STEPS - 1u];
        int32_t lr = LRATE_OF_SLOOP(s[2u * SL24_NP + SL26_LSYNC], (int16_t)(s[2u * P_LRATE] | s[2u * P_LRATE + 1u] << 8));
        if (i < NPART && !eng && s[2u * SL24_NP + SL26_FTYPE] <= 2u)
            ft |= (uint32_t)s[2u * SL24_NP + SL26_FTYPE] << (2u * i);
#if FELUCCA_LFO2
        if (i < NPART)                                  /* (LFO 2's TRIG FREE, POL UNI: bits 8 + 2 i, 9 + 2 i; 2.6 clamps
                                                         * them to 0..1 at its load: any other value is 1) */
            ft |= (uint32_t)(s[2u * SL24_NP + SL26_LTRIG] != 0u) << (8u + 2u * i) |
                  (uint32_t)(s[2u * SL24_NP + SL26_LPOL] != 0u) << (9u + 2u * i);
#endif
        memcpy(mf, s + SL26_MF, sizeof mf);
        memmove(d + SL24_LOCKS, s + SL26_MF + 64u, 4u * NLOCK);
        for (k = 0; k < NLOCK; k++) {                   /* its ids: P_E0.. at 63 (2.5's 53), its ten own (53..62) LOST */
            uint8_t *l = d + SL24_LOCKS + 4u * k;
            if (l[0] == LOCK_FREE)
                continue;
            if (l[1] >= SL24_E0 + SL26_NX)
                l[1] = (uint8_t)(l[1] - SL26_NX);
            else if (l[1] >= SL24_E0)
                l[0] = LOCK_FREE, l[1] = l[2] = l[3] = 0;
        }
        memmove(d + 2u * SL24_NP + 2u, s + SL26_STEPS, 640u);
        d[2u * SL24_NP] = eng, d[2u * SL24_NP + 1u] = pre;
        memmove(d, s, 2u * SL24_NP);
        d[2u * P_LRATE] = (uint8_t)lr, d[2u * P_LRATE + 1u] = (uint8_t)((uint32_t)lr >> 8);   /* (SYNC n: our RATE -n) */
        memset(d + SL24_FILLS, 0, 16u);
        for (k = 0; k < NSTEP; k++) {
            d[SL24_MICRO + k] = (uint8_t)((mf[k] & 63u) >= 32u ? (mf[k] & 63u) + 192u : mf[k] & 63u);   /* (6 bits, signed) */
            if (mf[k] >> 6 != 3u)                       /* (3: none, as 2.6's proj_apply reads it) */
                d[SL24_FILLS + k / 4u] |= (uint8_t)((mf[k] >> 6) << (2u * (k % 4u)));
        }
    }
    if (b[8u + 2u * PJ_NG + 2u] & 0x80u) {              /* GLO > TEXTURE in G_DRCH's and G_INFO's slots and the spare bytes */
        b[8u + 2u * G_DRCH] = (uint8_t)GP[G_DRCH].def, b[9u + 2u * G_DRCH] = (uint8_t)((uint16_t)GP[G_DRCH].def >> 8);
        b[8u + 2u * G_INFO] = (uint8_t)GP[G_INFO].def, b[9u + 2u * G_INFO] = (uint8_t)((uint16_t)GP[G_INFO].def >> 8);
    }
    b[8u + 2u * PJ_NG + 2u] = b[8u + 2u * PJ_NG + 3u] = 0;
    w = SL24_MAGIC, memcpy(b, &w, 4);
    w = SL24_SIZE, memcpy(b + 4, &w, 4);
    w = proj_hash(b, SL24_SIZE - 4u), memcpy(b + SL24_SIZE - 4u, &w, 4);
    return ft;
}
#if FELUCCA_LFO2
/* 2.6's LFO with TRIG FREE or POL UNI (sl26_to_sl24's bits 8..13) on part k of q -> our LFO 2, which has both and two
 * slots as LFO's: the part's LFO slots (its depths' two of the most weight, params.c lfo1_load) go there whole, with
 * the LFO's RATE (its SYNC already in it) and WAVE, as 2.6 plays them; LFO's DEST to OFF. LOST: PHS on a TRIG NOTE
 * (LFO 2 restarts at 0), FADE (LFO 2 has none), a lock on a depth moved (it stays LFO's: no slot of LFO on it now, not
 * played). No depth: nothing (not heard) */
static void sl26_lfo2_in(project_t *q, uint32_t ft)
{
    static const int16_t NONE[4] = {0, 0, 0, 0};
    int16_t x[NPART][L2_NV], v[P_COUNT], s[4];
    uint32_t k;
    int32_t spill;
    (void)l2_unpack(q, x);
    for (k = 0; k < NPART; k++) {
        if (!((ft >> (8u + 2u * k)) & 3u))
            continue;
        (void)lfo1_load(&q->t[k].p[P_LEG_PIT], s, &spill);
        if (!(s[0] && s[1]) && !(s[2] && s[3]))
            continue;
        pj_to_p(v, q->t[k].p);
        x[k][0] = (int16_t)clamp(v[P_LRATE], TP[P_L2RATE].min, TP[P_L2RATE].max);
        x[k][1] = (int16_t)clamp(v[P_LWAVE], 0, TP[P_L2WAVE].max);
        x[k][2] = (int16_t)((ft >> (8u + 2u * k)) & 1u);
        x[k][3] = (int16_t)((ft >> (9u + 2u * k)) & 1u);
        memcpy(&x[k][4], s, sizeof s);
        lfo1_store(&q->t[k].p[P_LEG_PIT], NONE, 0);
    }
    l2_pack(q, x);
    for (k = 0; k < NPART; k++) {                     /* (LFO 2's AMT B: in LFO DEST's words) */
        (void)lfo1_load(&q->t[k].p[P_LEG_PIT], s, &spill);
        lfo1_store(&q->t[k].p[P_LEG_PIT], s, x[k][7]);
    }
}
#endif
/* the parts' ANALOG filter TYPE (sl26_to_sl24) -> q's ANALOG parts (ANALOG 2's FTYP: LP12 LP24 BP HP); their LFO's TRIG
 * FREE or POL UNI -> LFO 2 (sl26_lfo2_in, FELUCCA_LFO2) */
static void sl26_ftype_in(project_t *q, uint32_t ft)
{
#if FELUCCA_ANALOG2
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if ((ft >> (2u * k)) & 3u && !q->t[k].engine)
            q->t[k].p[P_A2FTYP] = (int16_t)(((ft >> (2u * k)) & 3u) == 1u ? 2 : 3);
#endif
#if FELUCCA_LFO2
    sl26_lfo2_in(q, ft);
#endif
#if FELUCCA_ANALOG2 || FELUCCA_LFO2
    q->sum = proj_sum(q);
#else
    (void)q, (void)ft;
#endif
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
    q->g[G_INFO] = 0;                                   /* (ours: REC QUANTIZE ON, project.c RECQ_PACK; 2.4's: unknown) */
    q->sel = c[8u + sizeof q->g];
    for (i = 0; i < NTRK; i++) {
        const uint8_t *s = c + 12u + sizeof q->g + i * SL24_TRK;
        int16_t e[SL24_NP];
        uint32_t eng = sl24_eng_in(s[2u * SL24_NP]);
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
        } else if (eng == SL24_ENG_LOST) {              /* (2.5's NOISE: ANALOG at its EDIT defaults) */
            for (k = 0; k < 8u; k++)
                v[P_E0 + k] = eng_built(0) ? ENGINES[eng_slot_built(0)]->edit[k].def : 0;
        }
        pj_from_p(q->t[i].p, v);
        q->t[i].engine = (uint8_t)(eng == SL24_ENG_LOST ? 0u : eng);
        q->t[i].preset = eng == SL24_ENG_LOST ? 0u : s[2u * SL24_NP + 1u];
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
/* 2.5's drums' delay send (byte 73: G_DRDLY 0..127, as a track's send; 0 in 2.4's) -> every lane's DLY in d, the
 * nearest level (drum_sends.c dsend_near); REV and CHO as they are (bit 15 too: dlrec_migrate still gives a TRK lane
 * 2.4's G_DRREV). -> 1 a lane was set */
static int sl25_drdly_in(const uint8_t *b, dlrec_t *d)
{
    uint32_t l, v = dsend_near(b[SL25_DRDLY] > 127u ? 127 : (int32_t)b[SL25_DRDLY]);
    if (!v)
        return 0;
    for (l = 0; l < DRUM_LANES; l++)
        d->snd[l] = (uint16_t)((d->snd[l] & ~(31u << 5)) | v << 5);
    return 1;
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
