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
 *   - engines, as SLOOP 2.5 and 2.6 number them (v2.5 fa9ce57 core.h ENGI_*; FUN5 did not change, 2.6 reads it): our
 *     UIDs 0..9 (ANALOG .. GRAIN, FM6) are theirs, our PHYS (11) is their 10 with its EDIT values (the same eight);
 *     SLICE, ACID, CZ (10, 12, 13) -> their fallback (registry.h: SAMPLE, ANALOG, PHASE) with that engine's EDIT
 *     defaults (LOST: the engine). SLOOP 2.4 has no 10: it plays a PHYS part as ANALOG (its proj_apply: engine %
 *     NENGINES), PHYS's EDIT values on ANALOG's knobs (web/EDITOR_PROTOCOL.md: the export is for 2.5 and later);
 *   - FM6 parts (sl24_fm6.c): a voice that is one of 2.4's F1..F8 exactly (an import of one with its macros at 0)
 *     -> that PTCH; else, when the caller gives 2.4's bank (the editor's "FM6 voices into 2.4's bank": 2.4's bank
 *     kept in flash, or an empty one), the voice into it (the slot holding it, else the first free one: B1..B27;
 *     SX24_BANK: the bank goes into the file as 2.4's object 8); else the closest factory patch to VOICE (SL24_PTCH;
 *     the importer's SL24_FM6V images map back to themselves), U01..U32 -> F1 (LOST: the voice). MOD -> MLVL;
 *     ALG FB MRAT MEG VMOD DTUN 0 (in the voice already); preset = PTCH (F1..F8, else 0). LOST: M.TIM, C.TIM, an
 *     ENGINE other than MARK I (2.4's), operators switched off;
 *   - the LFO's RATE synced to the tempo (< 0, params.c N_LSYNC: SLOOP 2.6's, not 2.4's) -> the free rate nearest
 *     that note value's at the project's tempo (sl24_rate_out), its locks too (LOST: LSYNC, the sync);
 *   - a part's LFO 2 (project.c l2_pack; the drum track's LFO slots that hold it: their defaults): 2.4's
 *     LFO when the part's own does nothing (sl24_l2_out); else, and its TRIG FREE, POL UNI, AMP < 0: LOST (SX24_LFO2);
 *   - LFO DEST's two slots: 2.4's depths PIT FLT SHP AMP (an odd AMT on PIT FLT SHP: APPROXIMATE, half rounded toward
 *     0); another destination, AMP < 0: LOST (SX24_LFODEST);
 *   - the drum track: kits 0..36 as they are; ours past 36 (X0X, user kits) -> 808 (LOST); its lanes' record (sends,
 *     per-lane edits, dl_hash) LOST but the lanes' DLY: SLOOP 2.5's drums' delay send (G_DRDLY, byte 73; 2.4 ignores
 *     it) is the DLY level most lanes have (a tie: the lower), as a send (dsend_lvl), 0 while DLY is in no FX slot.
 *     APPROXIMATE: one send of the drums' sum for all lanes, so lanes of another level take it too (sl25_drdly_out);
 *   - globals as they are but g[12] (our MIDI channels; 2.4's MIDI OUT flag) and g[14] (our G_VIEW; 2.4's MIDI IN
 *     route), 0 = 2.4's defaults (a 2.4 load never reads them: settings of the FM-1), and G_SYNC AUTO -> INT;
 *     the reserved bytes (reverb type, master COMP / LIMIT) LOST;
 *   - the step extras (stepx.h, 2.4's own layout) into each track's tail, from the automation store (auto.h
 *     auto_to_stepx, ed_sl24.c): nudges and fills as they are; the first 24 step-only events of a sound value as locks
 *     with 2.4's ids, the ones 2.4 has no parameter for dropped (LOST: FX OFF, ANALOG 2, an FM6 or fallback part's
 *     EDIT; more than 24: LOCK). Hold events (motion) and chance are not 2.4's (LOST: MOTION, which CHANCE shares;
 *     the editor says "motion not in 2.4");
 *   - the FX slots (fx_slots.c, the working project's: sl24_fx_out): 2.4 plays DST CHO DLY REV, and FILT with
 *     TRK_FILT, on every track: a type not heard on a track here (an insert in none of its two slots, a send in no
 *     master slot) is written 0 so that 2.4 plays what we do (LOST: SX24_SLOTS, its amounts, when one was not 0);
 *     heard here and not on 2.4 (LOST): COMP (a part's, the drum bus's, a drum sound's: SX24_COMP), a drum sound's DIST
 *     (SX24_DINS), the drum bus's DST CHO DLY REV and the drum track's send levels (SX24_DBUS), an FX return below
 *     unity (SX24_FXRET: 2.4 has none, its wet comes back at unity), a part's FILTER before its DIST, both on
 *     (SX24_INSORD). The import (sl24_import.c, proj_apply without an FX record: fx_rec.c ins_map) puts each part's
 *     S1 DIST, S2 FILTER (2.4's order) and the drum track's FILTER in a slot: all four of DST CHO DLY REV and FILT
 *     round-trip.
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
               STEP_SN * NSTEP == 640u && sizeof(stepx_t) == 176u && P_CHORD == SL24_COMMON - 1u &&
               DRUM_SAMPLED == 5u && ENG_UID_FM6 == 9u && ENG_UID_PHYS == 11u,
               "SLOOP 2.4's FUN5 (tests/sl24_fun5_gen.c), its kit 5 = 808, its engines 0..9 our UIDs");
#define SL25_PHYS 10u                                   /* SLOOP 2.5's PHYS (core.h ENGI_PHYS) */
#define SL25_DRDLY (8u + 2u * PJ_NG + 1u)               /* (as sl24_import.c) 2.5's G_DRDLY, the byte after sel */
#define SL24_PERSIST 88u                                /* 2.4's persist_t (its settings, BACKUP object 1) */

enum {                                                  /* what an export lost (the wire: 2 words of 2 x 7 bits) */
    SX24_ENGINE = 1, SX24_FM6 = 2, SX24_FXOFF = 4, SX24_A2 = 8, SX24_KIT = 16, SX24_LOCK = 32, SX24_LANES = 64,
    SX24_MASTER = 128,
    SX24_BANK = 256,                                    /* (not a loss: FM6 voices went into the bank, which goes with it) */
    SX24_SLOTS = 512, SX24_COMP = 1024, SX24_DINS = 2048, SX24_DBUS = 4096,  /* (the FX slots: sl24_fx_out) */
    SX24_MOTION = 8192,                                 /* (the automation store: hold events, sl24_auto_out; the last bit
                                                         * of the first word) */
    SX24_CHANCE = 16384,                                /* (the steps' chance: the first bit of the second word, which an
                                                         * editor asks for with SL24_GET's flag; ed_sl24.c) */
    SX24_LSYNC = 32768,                                 /* (the LFO's RATE synced: the second word's bit 1) */
    SX24_LAUTO = 65536,                                 /* (the drum lanes' motion and locks, seq/auto.h AUTO_LANE: the
                                                         * second word's bit 2; a v11 editor gets it as MOTION) */
    SX24_DLFO = 131072,                                 /* (a drum lane's LFO acting, drums/lane_play.c: 2.4's drums have
                                                         * none; the second word's bit 3; a v11 editor: MOTION) */
    SX24_FXRET = 262144,                                /* (an FX return below unity, CHO DLY REV RET: sl24_fx_out; the
                                                         * second word's bit 4: 2.4 plays each return at full) */
    SX24_NGATE = 524288,                                /* (a POLY note's own end, seq/ngate.h: 2.4 has one GATE a step, the
                                                         * step's goes; the second word's bit 5: ed_sl24.c) */
    SX24_INSORD = 1048576,                              /* (a track with its FILTER before its DIST, both on: 2.4 plays
                                                         * DIST then FILT; the second word's bit 6; sl24_fx_out) */
    SX24_INSFX = 2097152,                               /* (a further insert heard, FOLD .. ECHO GATER: insfx.c;
                                                         * 2.4 has none; the second word's bit 7; sl24_fx_out) */
    SX24_COND = 4194304,                                /* (a step condition 1:2 .. 4:4, seq/auto.h AUTO_COND, Felucca 1.5.6:
                                                         * 2.4 has none; the second word's bit 8; a v11 editor: MOTION) */
    SX24_LFO2 = 8388608,                                /* (a part's LFO 2 not written as 2.4's LFO: sl24_l2_out; the second
                                                         * word's bit 9) */
                                                        /* (bit 10: another branch's) */
    SX24_LFODEST = 33554432,                            /* (a part's LFO DEST slot not one of 2.4's depths: another
                                                         * destination, AMP < 0; the second word's bit 11) */
    SX24_DACC = 67108864                                /* (the drum track's ACCENT lane, seq/ngate.h: 2.4 and 2.6 have no
                                                         * drum accent; the second word's bit 12; ed_sl24.c) */
};
/* our factory VOICE R01..R16 (eng_fm6.c FM6_PRESETS) -> 2.4's PTCH F1..F8 (TINE EP, GLASS BELL, ROUND BASS, BRASS
 * SECT, SOFT PAD, WOOD BARS, DRAWBARS, NYLON PICK): TINE EP, BRASS SECT, SOLID BASS, BELLS, FM MARIMBA, CLAVINET,
 * DRAWBARS, STRINGS, FM GLASS, FM SYNC LD, HARP, FM KALIMBA, FLUTE, STEEL DRUM, SAW BASS, TUBULAR */
static const uint8_t SL24_PTCH[16] = {0, 3, 2, 1, 5, 7, 6, 4, 4, 3, 7, 5, 4, 5, 2, 1};
#if defined(SL24_TP) && SL24_TP
static int px_unpack(const project_t *p, int16_t (*x)[3]);   /* project.c: our TFLT STRUM VLEAD */
#endif

/* 2.4's FM6 bank (sl24_fm6.c SL24_BANK_LEN, as 2.4 stores it) at b: the slot holding the packed voice pk, else the
 * first free one, given pk; -1 full */
static int sl24_bank_put(uint8_t *b, const uint8_t *pk)
{
    uint32_t used, k;
    memcpy(&used, b + 8, 4);
    for (k = 0; k < SL24_BANK_N; k++)
        if (((used >> k) & 1u) && !memcmp(b + 16u + 128u * k, pk, 128u))
            return (int)k;
    for (k = 0; k < SL24_BANK_N; k++)
        if (!((used >> k) & 1u)) {
            memcpy(b + 16u + 128u * k, pk, 128u);
            used |= 1u << k;
            memcpy(b + 8, &used, 4);
            return (int)k;
        }
    return -1;
}
/* an empty 2.4 FM6 bank at b (SL24_BANK_LEN bytes) */
static void sl24_bank_empty(uint8_t *b)
{
    uint32_t w[4] = {SL24_BANK_MAGIC, 1u | SL24_BANK_N << 16, 0, 0};
    memset(b, 0, SL24_BANK_LEN);
    memcpy(b, w, sizeof w);
}
/* FM6 part k's PTCH for 2.4: its voice (all six operators on) when it is one of 2.4's F1..F8; else, given a bank b,
 * the slot it is put in (B1..B27, *lost |= SX24_BANK: the bank goes with the export); else the closest factory patch
 * to its VOICE (SL24_PTCH; *lost |= SX24_FM6) */
static uint32_t sl24_fm6_ptch(const project_t *q, uint32_t k, int32_t voice, uint8_t *b, uint32_t *lost)
{
    if ((q->fm6_has >> k) & 1u) {
        int f = (q->fm6_on[k] & 0x3Fu) == 0x3Fu ? sl24_fm6_factory(q->fm6[k]) : -1, s;
        if (f >= 0)
            return (uint32_t)f;
        if (b && (q->fm6_on[k] & 0x3Fu) == 0x3Fu && (s = sl24_bank_put(b, q->fm6[k])) >= 0) {
            *lost |= SX24_BANK;
            return SL24_NFAC + (uint32_t)s;
        }
    }
    *lost |= SX24_FM6;
    return voice >= 0 && voice < 16 ? SL24_PTCH[voice] : 0u;
}

/* our LFO RATE r -> 2.4's: a synced one (< 0: a cycle of LSYNC_P 24ths of a beat) the free rate (LFO_HZ_X100, Hz x 100)
 * nearest it at bpm, on a log scale (above the geometric mean of two neighbours: the upper one); a free one as it is */
static int32_t sl24_rate_out(int32_t r, int32_t bpm)
{
    static const uint16_t LSYNC_P[LSYNC_N] = {384, 192, 96, 48, 24, 12, 8, 6, 4, 3};   /* (voice.c LSYNC_KT's) */
    uint64_t p, t;
    int32_t i;
    if (r >= 0)
        return r;
    p = LSYNC_P[(uint32_t)(-r - 1) < LSYNC_N ? (uint32_t)(-r - 1) : 0u];
    t = (uint64_t)(bpm > 0 ? bpm : 1) * 40u;           /* the note value's Hz x 100, times p */
    for (i = 1; i < 128 && LFO_HZ_X100[i] * p < t; i++)
        ;
    if (i == 128)
        return 127;
    return t * t >= (uint64_t)LFO_HZ_X100[i - 1] * LFO_HZ_X100[i] * p * p ? i : i - 1;
}

static int16_t sl24_kit_out(int32_t k) { return (int16_t)(k >= 0 && k < (int32_t)SL24_KITS ? k : (int32_t)DRUM_SAMPLED); }
/* the engine number SLOOP 2.5 / 2.6 play for our UID: its own (0..9; PHYS 10), else the first fallback that is one */
static uint32_t sl24_eng_out(uint32_t uid)
{
    uint32_t n;
    for (n = 0; n < 4u && uid > ENG_UID_FM6 && uid != ENG_UID_PHYS && uid < ENG_UID_N; n++)
        uid = ENG_FALLBACK[uid];
    return uid == ENG_UID_PHYS ? SL25_PHYS : uid <= ENG_UID_FM6 ? uid : 0u;
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
/* slots s[4] over 2.4's LFO DEST depths at o (its p[]): as the depths they are (params.c lfo_depths: a destination
 * other than PIT FLT SHP AMP, AMP < 0, not heard there: -> 1) */
static uint32_t sl24_slots_out(const int16_t *s, uint8_t *o)
{
    int16_t d[4];
    uint32_t i, r = lfo_depths(s, d) & LFO_NOSND;
    for (i = 0; i < 4u; i++)
        sl24_w16(o + 2u * (P_LEG_PIT + i), d[i]);
    return r;
}
/* part k's LFO 2 (project.c l2_unpack) over 2.4's values at o (its p[]), v ours: when it acts and the part's LFO does
 * not (its slots OFF or at 0), it becomes 2.4's LFO: RATE (synced: the nearest free one, SX24_LSYNC), WAVE, PHS and FADE
 * 0, its slots as depths (sl24_slots_out); 2.4 has no TRIG FREE, no POL UNI, nothing but PIT FLT SHP AMP: those, and a
 * LFO 2 beside a LFO that acts, are lost (SX24_LFO2) */
static uint32_t sl24_l2_out(const int16_t *l, const int16_t *l1, int32_t bpm, uint8_t *o)
{
    uint32_t lost = 0;
    if (!(l[4] && l[5]) && !(l[6] && l[7]))            /* (OFF, or AMT 0: nothing heard) */
        return 0;
    if ((l1[0] && l1[1]) || (l1[2] && l1[3]))
        return SX24_LFO2;
    sl24_w16(o + 2u * P_LRATE, sl24_rate_out(l[0], bpm));
    sl24_w16(o + 2u * P_LWAVE, l[1]);
    sl24_w16(o + 2u * P_LPHASE, 0);
    sl24_w16(o + 2u * P_LFADE, 0);
    lost |= sl24_slots_out(&l[4], o) ? SX24_LFO2 : 0u;
    lost |= l[0] < 0 ? SX24_LSYNC : 0u;
    lost |= l[2] || l[3] ? SX24_LFO2 : 0u;
    return lost;
}
/* global i of q as 2.4 keeps it: G_MIDI (our MIDI channels), G_VIEW (2.4's G_ROUTE) and G_INFO (our REC QUANTIZE) 0,
 * SYNC AUTO -> INT, the drum reverb our lanes took (DRREV_MOVED) -> 2.4's default 16 */
static int32_t sl24_g_out(const project_t *q, uint32_t i)
{
    int32_t v = q->g[i];
    if (i == G_MIDI || i == G_VIEW || i == G_INFO || (i == G_SYNC && (v < 0 || v > 2)))
        return 0;
    return i == G_DRREV && v < 0 ? 16 : v;
}

/* track k's stored values -> 2.4's p[61], engine, preset at o; -> SX24_* lost */
static uint32_t sl24_trk_out(const project_t *q, uint32_t k, const int16_t *px, uint8_t *o, int *e_ok, uint8_t *bank)
{
    int16_t v[P_COUNT];
    uint32_t i, lost = 0, uid = q->t[k].engine, eng = k == TRK_DRUM ? 0u : sl24_eng_out(uid);
    pj_to_p(v, q->t[k].p);
    if (k == TRK_DRUM && l2_tagged(q))                  /* (its LFO slots held the parts' LFO 2: project.c l2_pack) */
        l2_slots_def(v);
    for (i = 0; i < SL24_COMMON; i++)
        sl24_w16(o + 2u * i, v[i]);
    sl24_w16(o + 2u * P_LRATE, sl24_rate_out(v[P_LRATE], q->g[G_BPM]));
    lost |= v[P_LRATE] < 0 ? SX24_LSYNC : 0u;
    if (k < NPART) {                                    /* LFO DEST's slots as 2.4's depths (SX24_LFODEST: not all); the
                                                         * slots: the project's modulation record, else its words */
        int16_t s[4], mv[P_COUNT];
        int32_t spill;
        const uint8_t *mb = mrec_parts_of(q);
        int16_t x[NPART][L2_NV];
        (void)l2_unpack(q, x);
        if (mb) {
            memcpy(mv, v, sizeof mv);
            mrec_part_vals(mb, k, mv);
            memcpy(s, &mv[P_L1DA], sizeof s);
            memcpy(x[k], &mv[P_L2RATE], sizeof x[k]);
            (void)env_store(mv);                        /* (ENV DEST's slots as 2.4's FLT SHP depths) */
            sl24_w16(o + 2u * P_ED_FLT, mv[P_ED_FLT]);
            sl24_w16(o + 2u * P_ED_SHP, mv[P_ED_SHP]);
        } else {
            (void)lfo1_load(&q->t[k].p[P_LEG_PIT], s, &spill);
        }
        lost |= sl24_slots_out(s, o) ? SX24_LFODEST : 0u;
        lost |= sl24_l2_out(x[k], s, q->g[G_BPM], o);
    }
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
    if (uid == ENG_UID_FM6) {                           /* PTCH: the voice's own (sl24_fm6_ptch), MOD -> MLVL, the rest 0 */
        uint32_t pt = sl24_fm6_ptch(q, k, v[P_E0], bank, &lost);
        for (i = 0; i < 8u; i++)
            sl24_w16(o + 2u * (SL24_E0 + i), i == 7u ? (int32_t)pt : i == 2u ? sl24_mod_out(v[P_E1]) : 0);
        o[2u * SL24_NP + 1u] = (uint8_t)(pt < SL24_NFAC ? pt : 0u);   /* (preset: F1..F8's, as 2.4 numbers them) */
        *e_ok = 0;
        return lost | (v[P_E2] || v[P_E3] || v[P_E4] != 1 ? SX24_FM6 : 0u);   /* (M.TIM, C.TIM, ENGINE: not 2.4's) */
    }
    if (eng != (uid == ENG_UID_PHYS ? SL25_PHYS : uid)) {   /* (SLOOP has not this engine: its fallback, at its defaults) */
        for (i = 0; i < 8u; i++)
            sl24_w16(o + 2u * (SL24_E0 + i), eng_built(eng) ? ENGINES[eng_slot_built(eng)]->edit[i].def : 0);
        o[2u * SL24_NP + 1u] = 0;
        *e_ok = 0;
        lost |= SX24_ENGINE;
    }
    return lost;
}
/* track k's extras x (0: none) -> 2.4's tail at o, locks converted; -> SX24_* lost */
static uint32_t sl24_tail_out(const stepx_t *x, uint32_t k, int e_ok, int32_t bpm, uint8_t *o)
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
            if (l.param == P_LRATE && l.val < 0) {
                lost |= SX24_LSYNC;
                l.val = (int16_t)sl24_rate_out(l.val, bpm);
            }
            l.param = (uint8_t)p;
            d.lock[m++] = l;
        }
    }
    memcpy(o, &d, sizeof d);                            /* (2.4's layout: stepx.h) */
    return lost;
}

/* the FX slots at the export (fx_slots.c: RAM, the working project's: the only one exported, ed_sl24.c) over 2.4's
 * project at o, "as heard": an amount of a type not heard on its track (an insert in none of the track's two slots, a
 * send in no master slot) written 0 (2.4 would play it; SX24_SLOTS when it was not 0), and what is heard here that 2.4
 * has not: COMP (the parts', the drum bus's, the drum sounds'), the drum sounds' DIST, the drum bus's DST CHO DLY REV
 * and its send levels, a part's FILTER before its DIST with both on (SX24_INSORD: 2.4 runs DIST, then FILT), a further
 * insert heard (SX24_INSFX: FOLD .. WIDE, CHRS ECHO, GATER; insfx.c); the
 * SLICER's place (first here, after DIST on 2.4) is not reported (the gate's 2.9 ms ramps); -> SX24_* lost */
static uint32_t sl24_fx_out(uint8_t *o)
{
    static const uint8_t T[] = {FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV, FXT_FILT};
    uint32_t k, i, l, lost = 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < sizeof T; i++) {
            uint8_t *a = o + SL24_HDR + k * SL24_TRK + 2u * (T[i] == FXT_FILT ? SL24_TFLT : (uint32_t)FXT_AMT[T[i]]);
            uint32_t heard = FXT_INSERT >> T[i] & 1u ? INS_ON(k, T[i]) : FXS_HAS(T[i]);   /* (a track's slot, a send's) */
            if (heard || (T[i] == FXT_FILT && !TFLT_ON) || !(a[0] | a[1]))
                continue;                               /* (heard; 2.4's FILT carried through a build without ours) */
            sl24_w16(a, 0);
            lost |= SX24_SLOTS;
        }
    for (k = 0; k < NPART; k++)                         /* (2.4: DIST, then FILT: a FILTER first, both on) */
        lost |= ins_has(k, FXT_FILT) == 1u && ins_has(k, FXT_DIST) && trk[k].p[P_DIST] &&
                FXT_AMT[FXT_FILT] != 0xFFu && trk[k].p[FXT_AMT[FXT_FILT]] ? SX24_INSORD : 0u;
    for (l = 0; l < DRUM_LANES; l++)                    /* (a drum sound's DST: always heard, D7) */
        lost |= FELUCCA_FX_DIST && dins_amt[0][l] ? SX24_DINS : 0u;
    lost |= INS_ON(TRK_DRUM, FXT_DIST) && TDRUM->p[P_DIST] ? SX24_DBUS : 0u;   /* (the drum bus: project_t's drum slots
                                                                               * are stored 0, P4-2) */
    for (i = 1; i < 4u; i++) {
        lost |= FXS_HAS(T[i]) && TDRUM->p[FXT_AMT[T[i]]] ? SX24_DBUS : 0u;
        lost |= FXS_HAS(T[i]) && song.g[G_CRET + (T[i] - FXT_CHO)] < 127 ? SX24_FXRET : 0u;   /* (2.4: its wet at unity) */
        lost |= FXS_HAS(T[i]) && dsend_kit[T[i] == FXT_REV ? 0u : T[i] == FXT_DLY ? 1u : 2u] < DKS_DEF ? SX24_DBUS : 0u;
    }                                                   /* (the drum track's send levels: 2.4 has none) */
#if INS_FX
    for (k = 0; k < NTRK; k++)                          /* (each its amount, P_TAIL: not in 2.4's file, nothing to clear) */
        for (i = FXT_NEW0; i < FXT_NEWN; i++)
            lost |= INS_ON(k, i) && trk[k].p[FXT_AMT[i]] ? SX24_INSFX : 0u;
#endif
#if FELUCCA_MASTER_COMP
    for (k = 0; k < NTRK; k++)
        lost |= INS_ON(k, FXT_COMP) && trk[k].p[P_TCOMP] ? SX24_COMP : 0u;
    for (l = 0; l < DRUM_LANES; l++)
        lost |= dins_amt[1][l] ? SX24_COMP : 0u;
#endif
    return lost;
}

/* SLOOP 2.5's drums' delay send (byte 73, G_DRDLY 0..127: a send of the drums' sum) from the working lanes' DLY
 * (drum_sends.c dsend[]): the level most lanes have, a tie the lower one, as a track's send (dsend_lvl: 31 -> 127);
 * 0 while DLY is in no FX slot (the lanes send nothing here: fx_slots.c fxs_lm) */
static uint8_t sl25_drdly_out(void)
{
    uint8_t n[DSEND_MAX + 1u];
    uint32_t l, v, best = 0;
    if (!FXS_HAS(FXT_DLY))
        return 0;
    memset(n, 0, sizeof n);
    for (l = 0; l < DRUM_LANES; l++)
        n[dsend_dly(dsend[l])]++;
    for (v = 1; v <= DSEND_MAX; v++)
        if (n[v] > n[best])
            best = v;
    return (uint8_t)(dsend_lvl(best) * clamp(dsend_kit[1], 0, DKS_DEF) / DKS_DEF);   /* (x the drum track's DLY level) */
}

#if FELUCCA_AUTO
/* track k's automation (seq/auto.h) as 2.4's extras x: its nudges, fills and first NLOCK step-only events of a value;
 * -> SX24_* of what 2.4 has no place for: MOTION (hold events), CHANCE, COND, LOCK (more than NLOCK), LAUTO (a drum lane's
 * value, either kind: left out) */
static uint32_t sl24_auto_out(stepx_t *x, const auto_list_t *l)
{
    uint32_t r = auto_to_stepx(x, l);
    return (r & AUTO_X_HOLD ? SX24_MOTION : 0u) | (r & AUTO_X_CHANCE ? SX24_CHANCE : 0u) | (r & AUTO_X_LOCKS ? SX24_LOCK : 0u) |
           (r & AUTO_X_LANE ? SX24_LAUTO : 0u) | (r & AUTO_X_COND ? SX24_COND : 0u);
}
#endif
/* q (ours, valid) and its extras x[k] (0: none) -> a SLOOP 2.4 project at o (SL24_SIZE bytes, sl24_is says yes);
 * returns what was lost (SX24_*) */
static uint32_t proj_to_sl24(const project_t *q, const stepx_t *const *x, uint8_t *o, uint8_t *bank)
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
    o[SL25_DRDLY] = sl25_drdly_out();                   /* (2.5's drums' delay send; 2.4's spare, which it ignores) */
    lost |= q->rsv[0] | q->rsv[1] | q->rsv[2] ? SX24_MASTER : 0u;
#if FELUCCA_ANALOG2
    lost |= q->dl_hash ? SX24_LANES : 0u;
#endif
    for (k = 0; k < NTRK; k++) {
        uint8_t *t = o + SL24_HDR + k * SL24_TRK;
        int e_ok;
        lost |= sl24_trk_out(q, k, px[k], t, &e_ok, bank);
        memcpy(t + 2u * SL24_NP + 2u, q->t[k].step, 640u);
        lost |= sl24_tail_out(x ? x[k] : 0, k, e_ok, q->g[G_BPM], t + SL24_TAIL);
    }
    lost |= sl24_fx_out(o);
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
