/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* User presets (editor protocol v2, cmds 16-21; the SAVE > USER page): 32
 * slots in two storage.c objects (OBJ_UPRESET0/1, 0xDC000..0xDFFFF), 16
 * records each, mirrored in RAM so browsing never reads flash. A record:
 * engine, name, the instrument parameters, a 16-step pattern.
 *
 * A bank whose magic, record size or slot count differ reads as empty; so
 * does a record with another UP_VER. A record keeps np = the P_COUNT it was
 * stored with; when that differs it is mapped by count: its last 8 values
 * are P_E0..P_E7, the first np - 8 are P_LEVEL.. in order, missing ones take
 * their defaults. So common parameters may only be added just before P_E0
 * (else bump UP_VER).
 *
 * UP_VER 2 (FELUCCA_ANALOG2, written since the ENV2 destinations): ANALOG 2's
 * ENV2 extras (core.h P_A2ESUS .. P_A2ESDT: SUS2 REL2 and four amounts, the
 * last values before P_E0) are packed a byte each in A2X_W words (a2x_pack),
 * so np = P_COUNT values take np - A2X_N + A2X_W of p[]. UP_VER 1 records
 * still load: one of np 72 (P_A2ESUS REL2 DST2 a word each) gets DST2 with
 * AMT2 as that destination's amount (a2x_from_dst): the same sound.
 *
 * With -DUP_HOST (host test) only the part above #ifndef UP_HOST is built;
 * it needs nothing but core.h. */
#define UP_PER_BANK 16u
#define UP_PMAX 84u                              /* room for P_COUNT to grow (UP_VER 3: P_ENG_END values, the slots) */
#define UP_PMAX2 72u                             /* the records of UPB1 / UPB2 banks (UP_VER 1, 2) */
#define UP_USED 0xA5u
#define UP_VER 4u                                /* (2026-10-16) every value in its own word, in P_* order, then
                                                  * UP_NSLOT more (P_E1PIT ..: ENV 1 MOD's PIT and moves, ENV2's
                                                  * slots): nothing packed */
#define UP_VER3 3u                               /* (2026-10-14 .. 16, read) the same, P_E1PIT .. P_E1Z ENV DEST's
                                                  * slots (folded: env_store.h env1_fold), ENV's FLT SHP words 0 */
#define UP_VER_OK(v) ((v) == UP_VER || (v) == UP_VER3)
#define UP_NSLOT 8u
#if FELUCCA_ANALOG2
#define UP_VER2 2u                               /* ENV2's extras packed (UPB2, read only) */
#define UP_NP_V1A2 72u                           /* UP_VER 1's P_COUNT with SUS2 REL2 DST2 (P_E0 64) */
#define UP_NS(np) ((np) - A2X_N + A2X_W)         /* p[] used by a record of UP_VER 2 */
#define UP_XN A2X_N
#else
#define UP_NS(np) (np)
#define UP_XN 0u
#endif
#if FELUCCA_SL24_SAFE
#define UP_NP_SL24 61u                           /* SLOOP 2.4's P_COUNT (UPB1, UP_VER 1: P_TFLT P_STRUM P_VLEAD at 50..52) */
#define UP_NP_SL26 71u                           /* SLOOP 2.6's (v2.6 d30d655: + P_ITYPE .. P_LPOL at 53..62, P_E0 63). No
                                                  * SLOOP plus stored it (its P_COUNTs: 53 57 58 59, ANALOG 2's 69 72) */
#define UP_SL26_LSYNC 60u                        /* 2.6's P_LSYNC: LFO 2's SYNC (0 OFF, 1..10), our RATE -n */
#define UP_SL26_FTYPE 58u                        /* 2.6's P_FTYPE: ANALOG's filter LP / BP / HP */
#define UP_SL24_COMMON 50u                       /* its values 0..49: ours (P_LEVEL .. P_CHORD) */
#define UP_IS_SL(r) ((r)->ver == 1u && ((r)->np == UP_NP_SL24 || (r)->np == UP_NP_SL26))   /* SLOOP 2.4 .. 2.6's record */
#endif
#define UP_BANK_MAGIC 0x33425055u                /* "UPB3" (2026-10-14): records of UP_VER 3 and 4, UP_PMAX words */
#if FELUCCA_ANALOG2
#define UP_BANK_MAGIC_V2 0x32425055u             /* "UPB2": today's engine numbers (FM6 9, SLICE 10); read, written UPB3 */
#define UP_BANK_MAGIC_V1 0x31425055u             /* "UPB1": SLOOP plus's (SUPER 9, DX7 / FM6 10, SLICE 11) */
#else
#define UP_BANK_MAGIC_V2 0x31425055u             /* "UPB1" */
#endif
typedef struct {
    uint8_t used, ver, engine, np;               /* UP_USED, UP_VER, engine, P_COUNT when stored */
    char name[12];                               /* ASCII 32..126, 0-padded (no 0 when 12 long) */
    int16_t p[UP_PMAX];
    uint8_t note[16], flags[16];                 /* note 0 = rest; flags 1 accent, 2 slide, 4 tie */
} up_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_rec_t r[UP_PER_BANK];
} up_bank_t;
typedef struct {                                 /* a record of UPB1 / UPB2 (UP_VER 1, 2), in flash */
    uint8_t used, ver, engine, np;
    char name[12];
    int16_t p[UP_PMAX2];
    uint8_t note[16], flags[16];
} up_rec2_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_rec2_t r[UP_PER_BANK];
} up_bank2_t;
_Static_assert(sizeof(up_rec_t) == 216 && sizeof(up_rec2_t) == 192 && sizeof(up_bank_t) <= 3840u,
               "user preset record layouts (UPB3, UPB1 / UPB2)");
/* a record of before -> today's struct (the same fields, p[] longer: the rest 0) and back (UP_VER 1, 2 only) */
static void up_rec_from2(up_rec_t *r, const up_rec2_t *o)
{
    memset(r, 0, sizeof *r);
    r->used = o->used, r->ver = o->ver, r->engine = o->engine, r->np = o->np;
    memcpy(r->name, o->name, sizeof r->name);
    memcpy(r->p, o->p, sizeof o->p);
    memcpy(r->note, o->note, sizeof r->note);
    memcpy(r->flags, o->flags, sizeof r->flags);
}
static void up_rec_to2(up_rec2_t *o, const up_rec_t *r)
{
    o->used = r->used, o->ver = r->ver, o->engine = r->engine, o->np = r->np;
    memcpy(o->name, r->name, sizeof o->name);
    memcpy(o->p, r->p, sizeof o->p);
    memcpy(o->note, r->note, sizeof o->note);
    memcpy(o->flags, r->flags, sizeof o->flags);
}
_Static_assert(UP_NS(P_ENG_END) <= UP_PMAX && P_COUNT < 128, "user preset record: P_COUNT");   /* (SLOOP 2.4's track
                                                 * values after P_E7, core.h P_TFLT ..: not a sound's, not kept here) */
static up_bank_t up_bank[UP_SLOTS / UP_PER_BANK] IN_POOL;

static up_rec_t *up_rec(uint32_t k) { return &up_bank[k / UP_PER_BANK].r[k % UP_PER_BANK]; }

static int up_valid(const up_rec_t *r)
{
    if (r->used != UP_USED || r->engine >= ENG_UID_N || !r->name[0] || r->np < 8u)
        return 0;
    if (UP_VER_OK(r->ver))
        return r->np + UP_NSLOT <= UP_PMAX;
#if FELUCCA_ANALOG2
    if (r->ver == UP_VER2)
        return r->np >= 8u + UP_XN && UP_NS(r->np) <= UP_PMAX2;
#endif
    return r->ver == 1u && r->np <= UP_PMAX2;
}

static int up_used(uint32_t k) { return k < UP_SLOTS && up_valid(up_rec(k)); }
#if FELUCCA_UI == 1
/* the engine slot of user preset k (the Optimist UI names it beside the preset), NENGINES: empty or not built */
static uint32_t up_engine_slot(uint32_t k)
{
    return up_used(k) && eng_built(up_rec(k)->engine) ? eng_slot_built(up_rec(k)->engine) : NENGINES;
}
#endif

#include "../core/lfo_store.h"                     /* (LFO DEST: lfo1_store, lfo1_load) */
#include "../core/env_store.h"                     /* (ENV 1 MOD, ENV2 DEST: env1_fold, env_store, env_load) */
/* values v (today's P_* order, P_COUNT) -> record r's p[] and np (UP_VER 4): P_LEVEL .. P_E7 each in its word, the
 * envelopes' slots after them. Values in the words of the depths of before (LFO DEST's slots are in their own ids:
 * kept; ENV DEST FLT SHP, ENV2's amounts, which today's values hold at 0) that are not 0 come from outside (the editor,
 * a file of before): read as those depths (env_load), then 0 */
static void up_vals_put(up_rec_t *r, const int16_t *v0)
{
    int16_t v[P_COUNT];
    uint32_t i;
    memcpy(v, v0, sizeof v);
#if FELUCCA_ANALOG2
    if (v[P_A2FENV] || v[P_A2EPIT] || v[P_A2ESHP] || v[P_A2EOS2] || v[P_A2ESDT])
        (void)env_load(v);
#endif
    r->ver = UP_VER;
    r->np = P_ENG_END;
    memset(r->p, 0, sizeof r->p);
    for (i = 0; i < P_ENG_END; i++)
        r->p[i] = v[i];
    for (i = 0; i <= P_NLAST - P_E1PIT; i++)
        r->p[P_ENG_END + i] = v[P_E1PIT + i];
}

#if FELUCCA_ANALOG2 && (!defined(UP_HOST) || defined(UP_WITH_ENGINES))
/* a bank of SLOOP plus (UPB1, its engine numbers) -> today's, in RAM (flash keeps UPB1, every time: up_put_v1 saves a
 * slot into it as it is there; without FELUCCA_SL24_SAFE the bank goes out as UPB2): SUPER (9) -> ANALOG on the swarm, as projects do (params.c
 * analog2_from_super: SUPER LEAD's ANALOG version, the record has no preset number), DX7 / FM6 (10) -> FM6 (9),
 * SLICE (11) -> 10. A SUPER record is rewritten with today's P_COUNT values. (Test builds of feat/analog2 also
 * wrote UPB1 with DX7 at 9: such a record now reads as SUPER; none of them shipped) */
static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def);
static void up_params_st(const up_rec_t *r, int16_t *out, const int16_t *def);
static void up_bank_from_v1(up_bank_t *bk)
{
    uint32_t i, k;
    for (i = 0; i < UP_PER_BANK; i++) {
        up_rec_t *r = &bk->r[i];
        if (r->used != UP_USED || r->ver != 1u || r->np < 8u || r->np > UP_PMAX)
            continue;                                   /* (UPB1: UP_VER 1 records only) */
#if FELUCCA_SL24_SAFE
        if (UP_IS_SL(r)) {                              /* (SLOOP 2.4 .. 2.6's: 0..9 our engine numbers, FM6 9; 2.5's */
            if (r->engine == 10u)                       /* PHYS 10 -> ours, its NOISE 11 -> ANALOG at its EDIT */
                r->engine = ENG_UID_PHYS;               /* defaults: sl24_import.c sl24_eng_in; 2.6's filter TYPE */
            else if (r->engine > ENG_UID_FM6) {         /* was not its: LP) */
                r->engine = 0;
                for (k = 0; k < 8u; k++)
                    r->p[r->np - 8u + k] = eng_built(0) ? ENGINES[eng_slot_built(0)]->edit[k].def : 0;
                if (r->np == UP_NP_SL26)
                    r->p[UP_SL26_FTYPE] = 0;
            }
            continue;
        }
#endif
        if (r->engine == 10u || r->engine == 11u)
            r->engine--;
        else if (r->engine == 9u) {
            int16_t v[P_COUNT], def[P_COUNT];
            for (k = 0; k < P_COUNT; k++)
                def[k] = TP[k].def;
            up_params(r, v, def);
            analog2_from_super(v, 0);
            up_vals_put(r, v);
            r->engine = 0;
        }
    }
    bk->magic = UP_BANK_MAGIC;
}
#define UP_FROM_V1 1
#else
#define UP_FROM_V1 0
#endif

/* bank b from len bytes at src (st_buf; -1 = none): UPB3 as it is; UPB2 / UPB1 (records of 192 bytes) into today's
 * struct, a record each (UPB1: SLOOP plus's engine numbers then, up_bank_from_v1); any other shape: empty */
static void up_bank_check(uint32_t b, const void *src, int len)
{
    up_bank_t *bk = &up_bank[b];
    const up_bank2_t *o = (const up_bank2_t *)src;
    uint32_t i;
    if (len == (int)sizeof *bk && ((const up_bank_t *)src)->magic == UP_BANK_MAGIC &&
        ((const up_bank_t *)src)->rsize == sizeof(up_rec_t) && ((const up_bank_t *)src)->nslot == UP_PER_BANK) {
        if ((const void *)bk != src)
            memcpy(bk, src, sizeof *bk);
        return;
    }
    if (len == (int)sizeof *o && o->rsize == sizeof(up_rec2_t) && o->nslot == UP_PER_BANK &&
        (o->magic == UP_BANK_MAGIC_V2
#if UP_FROM_V1
         || o->magic == UP_BANK_MAGIC_V1
#endif
         )) {
        uint32_t m = o->magic;                          /* (src: st_buf, never the bank itself) */
        for (i = 0; i < UP_PER_BANK; i++)
            up_rec_from2(&bk->r[i], &o->r[i]);
        bk->magic = UP_BANK_MAGIC, bk->rsize = sizeof(up_rec_t), bk->nslot = UP_PER_BANK;
#if UP_FROM_V1
        if (m == UP_BANK_MAGIC_V1)
            up_bank_from_v1(bk);
#endif
        (void)m;
        return;
    }
    memset(bk, 0, sizeof *bk);
}

/* the record's values in today's P_* order (mapped by count, see above); def = the defaults. LFO DEST's four words
 * as stored (the depths of before, or the slots' tagged layout): up_params turns them into the slots */
static void up_params_st(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    uint32_t i, nc = r->np - 8u;
#if P_TAIL
    for (i = P_ENG_END; i < P_COUNT; i++)        /* (SLOOP 2.4's track values, the COMP insert's: none in a record) */
        out[i] = def[i];
#endif
#if FELUCCA_SL24_SAFE
    if (UP_IS_SL(r)) {                          /* SLOOP 2.4 / 2.5 (UPB1, P_COUNT 61): 0..49 ours, then its TFLT STRUM */
        for (i = 0; i < P_E0; i++)              /* VLEAD (not a sound's here: their defaults), P_E0.. at 53; 2.6 (71): */
            out[i] = i < UP_SL24_COMMON ? r->p[i] : def[i];   /* its ten more (INSERT, FTYPE, ENV SYNC, LFO 2 TRIG POL: LOST */
        for (i = 0; i < 8u; i++)                /* but an ANALOG's filter TYPE, as a 2.6 project's: sl26_ftype_in), */
            out[P_E0 + i] = r->p[r->np - 8u + i];   /* P_E0.. at 63 */
        if (r->np == UP_NP_SL26)                /* (its LFO 2 SYNC n: our RATE -n; OFF: its free RATE, 0..49's) */
            out[P_LRATE] = (int16_t)LRATE_OF_SLOOP(r->p[UP_SL26_LSYNC], r->p[P_LRATE]);
#if FELUCCA_ANALOG2
        if (r->np == UP_NP_SL26 && !r->engine && (r->p[UP_SL26_FTYPE] == 1 || r->p[UP_SL26_FTYPE] == 2))
            out[P_A2FTYP] = (int16_t)(r->p[UP_SL26_FTYPE] + 1);   /* (BP 2, HP 3; LP: ours as it is) */
#endif
        return;
    }
#endif
    if (UP_VER_OK(r->ver)) {                     /* UP_VER 3, 4: the values by count (the last 8 P_E0 ..), the slots */
        nc = r->np - 8u;
        for (i = 0; i < P_E0; i++)
            out[i] = i < nc ? r->p[i] : def[i];
        for (i = 0; i < 8u; i++)
            out[P_E0 + i] = r->p[nc + i];
        for (i = 0; i <= P_NLAST - P_E1PIT; i++)
            out[P_E1PIT + i] = r->p[r->np + i];
        return;
    }
#if FELUCCA_ANALOG2
    if (r->ver != 1u) {                          /* UP_VER 2: the values, ENV2's extras packed, P_E0 .. */
        nc = r->np - 8u - A2X_N;
        for (i = 0; i < P_A2ESUS; i++)
            out[i] = i < nc ? r->p[i] : def[i];
        a2x_unpack(&out[P_A2ESUS], &r->p[nc]);
        for (i = 0; i < 8u; i++)
            out[P_E0 + i] = r->p[nc + A2X_W + i];
        return;
    }
    if (r->np == UP_NP_V1A2) {                   /* UP_VER 1 with SUS2 REL2 DST2: DST2's AMT2 -> its amount (clamped */
                                                 /* after, up_values: as AMT2 was) */
        for (i = 0; i < P_A2ESUS; i++)
            out[i] = r->p[i];
        out[P_A2ESUS] = r->p[P_A2ESUS];
        out[P_A2EREL] = r->p[P_A2EREL];
        a2x_from_dst(&out[P_A2ESUS], &out[P_A2FENV], r->p[P_A2ESUS + 2u]);
        for (i = 0; i < 8u; i++)
            out[P_E0 + i] = r->p[UP_NP_V1A2 - 8u + i];
        return;
    }
#endif
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? r->p[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = r->p[nc + i];
}
/* the record's values in today's P_* order, LFO DEST's slots from their stored form (params.c lfo1_load: the depths of
 * a record of before, SLOOP's too, keep the two of the most weight) */
static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    int16_t w[4];
    int32_t spill;
    uint32_t k;
    up_params_st(r, out, def);
    if (UP_VER_OK(r->ver)) {                           /* (UP_VER 3, 4: the slots as they are, inside their ranges) */
        for (k = 0; k < 2u; k++)
            if ((uint32_t)out[P_L1DA + 2u * k] >= LT_NB)
                out[P_L1DA + 2u * k] = LT_OFF, out[P_L1DA + 2u * k + 1u] = 0;
        if (r->ver == UP_VER3)                         /* (ENV DEST's slots of 2026-10-14 .. 16 onto ENV 1 MOD) */
            (void)env1_fold(out);
#if FELUCCA_ANALOG2
        for (k = 0; k <= P_NLAST - P_N2DA; k += 2u)
            if ((uint32_t)out[P_N2DA + k] >= LT_NB)
                out[P_N2DA + k] = LT_OFF, out[P_N2DA + k + 1u] = 0;
#endif
        return;
    }
    memcpy(w, &out[P_LEG_PIT], sizeof w);              /* (a record of before: the depths, SLOOP's too) */
    (void)lfo1_load(w, &out[P_L1DA], &spill);
    (void)env_load(out);                               /* ENV2 DEST's slots (env_store.h); ENV 1's depths as they are */
}

static int up_name_ok(const uint8_t *s, uint32_t n)   /* 1..12 printable ASCII */
{
    uint32_t i;
    if (!n || n > 12u)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] < 32u || s[i] > 126u)
            return 0;
    return 1;
}

static void up_name(uint32_t k, char *b)       /* upper case, 0-terminated: b holds 13 */
{
    const up_rec_t *r = up_rec(k);
    uint32_t i;
    for (i = 0; i < 12u && r->name[i]; i++)
        b[i] = r->name[i] >= 'a' && r->name[i] <= 'z' ? (char)(r->name[i] - 32) : r->name[i];
    b[i] = 0;
}

static void up_pat_norm(uint8_t *note, uint8_t *flags)   /* tie: no note; rest: no flags */
{
    *note &= 127u;
    if (*flags & 4u) {
        *note = 0;
        *flags = 4;
    } else {
        *flags = *note ? (uint8_t)(*flags & (SF_ACCENT | SF_SLIDE)) : 0u;
    }
}

static void up_pat_from(up_rec_t *r, const step_t *st)   /* the first 16 steps -> the pattern */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        r->note[i] = st[i].time == ST_NOTE && st[i].n ? st[i].note[0] : 0u;
        r->flags[i] = st[i].time == ST_TIE ? 4u : st[i].flags;
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
}

static int up_pat_empty(const up_rec_t *r)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (r->note[i])
            return 0;
    return 1;
}

/* UP_PUT arguments: slot, engine, name, P_COUNT x v14, 16 x (note, flags) -> *r (values not yet
 * clamped); 0 ok, 1 bad arguments. *slot gets the slot byte when there is one. */
static int up_parse(const uint8_t *a, uint32_t na, up_rec_t *r, uint32_t *slot)
{
    uint32_t i, n, k;
    int16_t v[P_COUNT];
    if (na < 3u)
        return 1;
    *slot = a[0];
    for (n = 0; 2u + n < na && a[2 + n]; n++)
        ;
    k = 3u + n;                                  /* after the name's 0 */
    if (a[0] >= UP_SLOTS || a[1] >= NENGINES || 2u + n >= na || !up_name_ok(a + 2, n) ||
        na < k + 2u * P_COUNT + 32u)
        return 1;
    memset(r, 0, sizeof *r);
    r->used = UP_USED;
    r->engine = (uint8_t)eng_uid(a[1]);                 /* (the wire: a slot; the record: the UID) */
    for (i = 0; i < n; i++)
        r->name[i] = (char)a[2 + i];
    for (i = 0; i < P_COUNT; i++, k += 2u)
        v[i] = (int16_t)((int32_t)((a[k] & 127u) | (a[k + 1] & 127u) << 7) - 8192);
    up_vals_put(r, v);                                  /* (values not yet clamped: editor.c up_values) */
    for (i = 0; i < 16u; i++, k += 2u) {
        r->note[i] = a[k];
        r->flags[i] = a[k + 1];
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
    return 0;
}

#ifndef UP_HOST
#include "../core/favorites.c"                    /* (fav_user_drop: an erased slot loses its mark; once only) */
static const param_desc_t *up_desc(uint32_t uid, uint32_t i)   /* (by the record's engine UID) */
{
#if P_TAIL
    if (i > P_E7)
        return &TP[i];                                  /* (SLOOP 2.4's track values, the COMP insert's) */
#endif
    return i >= P_E0 ? &ENGINES[eng_slot(uid)]->edit[i - P_E0] : &TP[i];
}

static void up_values(const up_rec_t *r, int16_t *v)   /* mapped and clamped for its engine */
{
    int16_t def[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        def[i] = up_desc(r->engine, i)->def;
    up_params(r, v, def);
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int16_t)clamp(v[i], up_desc(r->engine, i)->min, up_desc(r->engine, i)->max);
}

#if FELUCCA_UP_FM6 && FELUCCA_ENG_FM6 && FELUCCA_FLASH
/* FELUCCA_UP_FM6: an FM6 user preset keeps its voice. The preset record holds only the VOICE number, and loading
 * it reset the part's buffer to that voice: operator edits not stored into the bank were lost. Here a store of an
 * FM6 track also keeps its buffer (the packed DX7 voice, 128 bytes of 7 bits in 112) in OBJ_UPFM6 (storage.c,
 * 0x95000 / 0x96000; at 0xE7000 / 0xE8000 before fix/upfm6-off-vm: moved once at start, st_upf_move), and a load
 * puts it back. Idea from Felucca 1.0.3 (up_fm6.c, hugelton/Felucca b22a24b, by Leo
 * Kuroshita, GPL-3.0-only), written for Melodee's FM6 (eng_fm6.c fm6_ed / fm6_pack, fm6_store.c). The object is
 * built and read in storage.c's st_buf (no RAM of its own); a record written by the editor (UP_PUT) or erased
 * drops the slot's voice (it no longer matches); a preset without one loads its VOICE as before. */
#define UPF_MAGIC 0x36465055u                    /* "UPF6" */
#define UPF_VB 112u                              /* a packed voice, 7 bits a byte */
typedef struct {
    uint32_t magic, used;                        /* bit k: slot k has its voice */
    uint8_t v[UP_SLOTS][UPF_VB];
} upf_t;
_Static_assert(sizeof(upf_t) <= ST_PAYLOAD_MAX && UP_SLOTS <= 32u, "OBJ_UPFM6 fits one storage object");
#define UPF ((upf_t *)(void *)st_buf)
static uint32_t upf_used;                        /* RAM copy of the mask (up_boot, every write) */
static uint8_t upf_hold;                         /* up_store of an FM6 sound: up_put leaves the voice to it */
static int upf_read(void)                        /* the object -> st_buf: valid 1 */
{
    return flash_ok && st_load(OBJ_UPFM6, st_buf, sizeof(upf_t)) == (int)sizeof(upf_t) && UPF->magic == UPF_MAGIC;
}
static void upf_set(uint32_t k, const int16_t *ed)   /* slot k's voice = ed (0: none), then to flash */
{
    uint8_t b[128];
    uint32_t i, j, acc = 0, n = 0;
    if (!flash_ok || k >= UP_SLOTS || (!ed && (upf_hold || !((upf_used >> k) & 1u))))
        return;
    if (!upf_read()) {
        memset(st_buf, 0, sizeof(upf_t));
        UPF->magic = UPF_MAGIC;
    }
    if (ed) {
        fm6_pack(b, ed);
        for (i = j = 0; i < 128u; i++) {         /* 8 x 7 bits -> 7 bytes */
            acc |= (uint32_t)(b[i] & 127u) << n;
            for (n += 7u; n >= 8u; n -= 8u, acc >>= 8)
                UPF->v[k][j++] = (uint8_t)acc;
        }
        UPF->used |= 1u << k;
    } else {
        UPF->used &= ~(1u << k);
    }
    upf_used = st_save(OBJ_UPFM6, st_buf, sizeof(upf_t)) ? upf_used & ~(1u << k) : UPF->used;
}
static int upf_get(uint32_t k, int16_t *ed)      /* slot k's voice -> ed: 1, or none: 0 */
{
    uint8_t b[128];
    uint32_t i, j, acc = 0, n = 0;
    if (k >= UP_SLOTS || !((upf_used >> k) & 1u) || !upf_read() || !((UPF->used >> k) & 1u))
        return 0;
    for (i = j = 0; i < 128u; i++) {
        while (n < 7u) {
            acc |= (uint32_t)UPF->v[k][j++] << n;
            n += 8u;
        }
        b[i] = (uint8_t)(acc & 127u);
        acc >>= 7;
        n -= 7u;
    }
    fm6_unpack(ed, b);
    return 1;
}
#define UPF_BOOT() (flash_ok ? (void)st_upf_move(UPF_MAGIC) : (void)0, upf_used = upf_read() ? UPF->used : 0u)
#else
#define upf_set(k, ed) ((void)0)
#define UPF_BOOT() ((void)0)
#endif

static void up_boot(void)                      /* persist_boot: the banks from flash */
{
#if FELUCCA_FLASH
    uint32_t b;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        up_bank_check(b, st_buf, flash_ok ? st_load(OBJ_UPRESET0 + b, st_buf, sizeof up_bank[b]) : -1);
    UPF_BOOT();
#endif
#if FELUCCA_FAVORITES
    {   /* a mark of a slot that is empty (erased by another firmware, or a record that does not read) goes */
        uint32_t k;
        for (k = 0; k < UP_SLOTS; k++)
            if (!up_used(k))
                fav_user_drop(k);
    }
#endif
}

#if UP_FROM_V1 && FELUCCA_SL24_SAFE && FELUCCA_FLASH
/* bank k / 16 in flash is "UPB1" (SLOOP 2.4 .. 2.6's, or SLOOP plus's): record k = *r (0: erased) into it as it is in
 * flash (in st_buf), the bank kept UPB1 and every other record byte for byte, so SLOOP still reads all of its own. Ours
 * is an UP_VER 2 record (up_vals_put): SLOOP skips it (its up_valid: UP_VER 1 only; to it an empty slot, which it may
 * save over), up_bank_from_v1 leaves it as it is. Before, the bank went out as UPB2 (RAM's, converted) and SLOOP
 * then read every slot of it as empty. -> 1 done (*rc: 0 ok, 2 flash error), 0 not such a bank (written as ours) */
static void up_vals_put2(up_rec2_t *o, const int16_t *v);
static int up_put_v1(uint32_t k, const up_rec_t *r, int *rc)
{
    up_bank2_t *f = (up_bank2_t *)(void *)st_buf;
    uint32_t obj = OBJ_UPRESET0 + k / UP_PER_BANK;
    if (st_load(obj, st_buf, sizeof *f) != (int)sizeof *f || f->magic != UP_BANK_MAGIC_V1 || f->rsize != sizeof(up_rec2_t) ||
        f->nslot != UP_PER_BANK)
        return 0;
    if (!r) {
        memset(&f->r[k % UP_PER_BANK], 0, sizeof(up_rec2_t));
    } else if (!UP_VER_OK(r->ver)) {
        up_rec_to2(&f->r[k % UP_PER_BANK], r);          /* (a record of before: as it was) */
    } else {                                            /* ours in that bank's layout, UP_VER 2: its values, the slots as */
        int16_t v[P_COUNT], def[P_COUNT];               /* the depths they are (as far as they go) */
        uint32_t i;
        for (i = 0; i < P_COUNT; i++)
            def[i] = TP[i].def;
        up_params(r, v, def);
        up_rec_to2(&f->r[k % UP_PER_BANK], r);
        up_vals_put2(&f->r[k % UP_PER_BANK], v);
    }
    *rc = st_save(obj, st_buf, sizeof *f) ? 2 : 0;     /* (storage.c: a source in st_buf, ST_SRC_BUF) */
    return 1;
}
/* values v -> o's p[] and np as UP_VER 2 lays them out (ENV2's extras packed; LFO DEST's, ENV DEST's slots as the depths
 * of before they are: lfo_depths, env_store) */
static void up_vals_put2(up_rec2_t *o, const int16_t *v0)
{
    int16_t v[P_COUNT], d[4];
    uint32_t i;
    memcpy(v, v0, sizeof v);
    (void)env_store(v);
    (void)lfo_depths(&v[P_L1DA], d);
    memcpy(&v[P_LEG_PIT], d, sizeof d);
    o->ver = UP_VER2;
    o->np = P_ENG_END;
    memset(o->p, 0, sizeof o->p);
    for (i = 0; i < P_A2ESUS; i++)
        o->p[i] = v[i];
    a2x_pack(&o->p[P_A2ESUS], &v[P_A2ESUS]);
    for (i = 0; i < 8u; i++)
        o->p[P_A2ESUS + A2X_W + i] = v[P_E0 + i];
}
#define UP_KEEP_V1 1
#else
#define UP_KEEP_V1 0
#endif

/* record k = *r (0: erase), then the bank to flash: 0 ok, 2 flash error, 3 no flash (kept in RAM) */
static int up_put(uint32_t k, const up_rec_t *r)
{
    up_bank_t *bk = &up_bank[k / UP_PER_BANK];
    bk->magic = UP_BANK_MAGIC;
    bk->rsize = sizeof(up_rec_t);
    bk->nslot = UP_PER_BANK;
    if (r)
        *up_rec(k) = *r;
    else
        memset(up_rec(k), 0, sizeof(up_rec_t));
    if (!r) {
        uint32_t i;
        for (i = 0; i < NTRK; i++)
            if (trk[i].user == k + 1u)
                trk[i].user = 0;
#if FELUCCA_FAVORITES
        fav_user_drop(k);                               /* (an erased preset keeps no mark) */
#endif
    }
    up_gen++;
    upf_set(k, 0);                                      /* (UP_FM6: a voice kept for the old record goes) */
#if FELUCCA_FLASH
    if (flash_ok) {
#if UP_KEEP_V1
        int rc;
        if (up_put_v1(k, r, &rc))                       /* (SLOOP's bank: kept UPB1, its records untouched) */
            return rc;
#endif
        return st_save(OBJ_UPRESET0 + k / UP_PER_BANK, bk, sizeof *bk) ? 2 : 0;
    }
#endif
    return 3;
}

static void up_slot_label(char *b, uint32_t k)  /* "U07" */
{
    b[0] = 'U';
    b[1] = (char)('0' + (k + 1u) / 10u);
    b[2] = (char)('0' + (k + 1u) % 10u);
    b[3] = 0;
}

/* the selected part's sound -> slot k; name 0 or "": engine name + slot number ("ANALOG 07").
 * 1 = the drum track is selected (it has no sound to store) */
static int up_store(uint32_t k, const char *name)
{
    up_rec_t r;
    uint32_t i;
    if (is_drum(TSEL))
        return 1;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.engine = (uint8_t)eng_uid(TSEL->eng_req % NENGINES);   /* (a UID) */
    if (name && name[0]) {
        for (i = 0; i < 12u && name[i]; i++)
            r.name[i] = name[i];
    } else {
        char b[16], l[4];
        str_cpy(b, ENGINES[TSEL->eng_req]->name, 9);
        up_slot_label(l, k);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), l + 1, 3);
        for (i = 0; i < 12u && b[i]; i++)
            r.name[i] = b[i];
    }
    up_vals_put(&r, TSEL->p);
    up_pat_from(&r, TSEL->step);
#if FELUCCA_UP_FM6 && FELUCCA_ENG_FM6 && FELUCCA_FLASH
    {
        int rc;
        upf_hold = r.engine == ENG_UID_FM6;
        rc = up_put(k, &r);
        upf_hold = 0;
        if (r.engine == ENG_UID_FM6)                    /* the part's voice as it is, edits and all */
            upf_set(k, rc ? (const int16_t *)0 : fm6_ed[song.sel % NPART]);
        return rc;
    }
#else
    return up_put(k, &r);
#endif
}

/* slot k -> the selected part's sound: engine and every parameter except its mix (LEVEL,
 * PAN, MUTE: the TRACKS faders) and its pattern parameters (param_kept). LIVE: the pattern
 * stored in the record is not loaded: changing the sound never changes the sequence.
 * 0 ok, 1 empty (or the drum track is selected), or its engine is left out of this build (kept, not loaded:
 * "MISSING: PHYS") */
static int up_load(uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    track_t *t = TSEL;
    if (!up_used(k) || is_drum(t))
        return 1;
    if (!eng_built(up_rec(k)->engine)) {
        ui_say("MISSING: ", ENG_UID_NAME[up_rec(k)->engine % ENG_UID_N]);
        return 1;
    }
    r = up_rec(k);
    up_values(r, v);
    for (i = 0; i < P_COUNT; i++)                       /* (LEN etc. of a kept pattern changed too, and */
        if (param_kept(i))                              /* a preset pattern then counted as edited) */
            v[i] = t->p[i];
    sound_change(t);                                    /* (SLOOP 2.6: what sounds fades out with the old sound, no click) */
    panic_req |= (uint8_t)(1u << song.sel);
    fm1_irq_off();                                      /* the audio ISR must not see half a sound */
    t->eng_req = (uint8_t)eng_slot_built(r->engine);
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = v[i];
    t->preset = 0;
#if FELUCCA_UP_FM6 && FELUCCA_ENG_FM6 && FELUCCA_FLASH
    if (r->engine == ENG_UID_FM6 && upf_get(k, fm6_ed[song.sel % NPART]))   /* its own voice (UP_FM6): kept as the */
        fm6_cur[song.sel % NPART] = (int16_t)(clamp(t->p[P_E0], 0, FM6_NVOICE - 1) + 1);   /* VOICE's, no reload */
    else
#endif
    fm6_cur[song.sel % NPART] = 0;                      /* FM6: the VOICE it names, afresh */
    fm1_irq_on();
    t->user = (uint8_t)(k + 1u);
    sync_reload = 1;
    ui.force = 1;
    MISS_BUMP();                                        /* (its set, its sends: miss.c) */
    return 0;
}

static uint32_t up_count(void)                 /* used slots */
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

static uint32_t up_nth(uint32_t n)             /* slot of the n-th used one (n < up_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && !n--)
            return k;
    return 0;
}

static uint32_t up_rank(uint32_t slot)         /* used slots before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

/* SAVE > USER page actions, with the message in the top bar */
static void up_ui(uint32_t op, uint32_t k)     /* 0 load, 1 erase, 2 save */
{
    char l[4];
    int rc;
    up_slot_label(l, k);
    if (op != 1u && is_drum(TSEL)) {
        ui_message("DRUM TRACK: NO SOUND");
        return;
    }
    if (op < 2u && !up_used(k)) {
        ui_message("EMPTY SLOT");
        return;
    }
    if (op == 0u) {
        if (!up_load(k))                               /* (its engine left out: up_load says MISSING) */
            ui_say("LOADED ", l);
        return;
    }
    if (song.playing || transport_req) {               /* a flash erase stops the audio ~50 ms */
        ui_message("STOP BEFORE SAVE");
        return;
    }
    rc = op == 1u ? up_put(k, 0) : up_store(k, 0);
    if (rc == 3)
        ui_message(op == 1u ? "ERASED (RAM)" : "SAVED (RAM)");
    else if (rc)
        ui_message(op == 1u ? "ERASE ERROR" : "SAVE ERROR");
    else
        ui_say(op == 1u ? "ERASED " : "SAVED ", l);
    ui.force = 1;
}
#endif
