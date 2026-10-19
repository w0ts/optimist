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
 *     NENGINES), PHYS's EDIT values on ANALOG's knobs (web/EDITOR_PROTOCOL.md "SLOOP files");
 *   - FM6 parts (sl24_fm6.c): a voice that is one of 2.4's F1..F8 exactly (an import of one with its macros at 0)
 *     -> that PTCH; else, when the caller gives 2.4's bank (the editor's "FM6 voices into 2.4's bank": 2.4's bank
 *     kept in flash, or an empty one), the voice into it (the slot holding it, else the first free one: B1..B27;
 *     SX24_BANK: the bank goes into the file as 2.4's object 8); else F1 (LOST: the voice; our R01..R16, which it once
 *     picked the closest patch by, are gone). MLVL ALG FB MRAT MEG VMOD DTUN 0 (in the voice already); preset = PTCH (F1..F8,
 *     else 0). LOST: operators switched off;
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

enum {                                                  /* what an export lost (the wire: 2 words of 2 x 7 bits) */
    SX24_ENGINE = 1, SX24_FM6 = 2, SX24_FXOFF = 4, SX24_A2 = 8, SX24_KIT = 16, SX24_LOCK = 32, SX24_LANES = 64,
    SX24_MASTER = 128,
                                                        /* (256: SX24_BANK, the 2.4 backup's FM6 bank, gone) */
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
    SX24_DACC = 67108864,                               /* (the drum track's ACCENT lane, seq/ngate.h: 2.4 and 2.6 have no
                                                         * drum accent; the second word's bit 12; ed_sl24.c) */
    SX24_DLYF = 134217728                               /* (the delay filter not LP 12 dB RES 0, core.h G_DFTYPE: 2.4 has
                                                         * COLOR alone, FREQ goes as it; the second word's bit 13, its last) */
};
#if defined(SL24_TP) && SL24_TP
static int px_unpack(const project_t *p, int16_t (*x)[3]);   /* project.c: our TFLT STRUM VLEAD */
#endif

/* FM6 part k's PTCH for SLOOP: its voice (all six operators on) when it is one of SLOOP's F1..F8; else F1 (*lost |=
 * SX24_FM6: a song file carries no FM6 bank) */
static uint32_t sl24_fm6_ptch(const project_t *q, uint32_t k, uint32_t *lost)
{
    if ((q->fm6_has >> k) & 1u) {
        int f = (q->fm6_on[k] & 0x3Fu) == 0x3Fu ? sl24_fm6_factory(q->fm6[k]) : -1;
        if (f >= 0)
            return (uint32_t)f;
    }
    *lost |= SX24_FM6;
    return 0u;
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
    if (i == G_DCOLOR)
        return dly_color_out(v);                        /* (our FREQ: 2.4's COLOR, project.c) */
    return i == G_DRREV && v < 0 ? 16 : v;
}

/* track k's stored values -> 2.4's p[61], engine, preset at o; -> SX24_* lost */
static uint32_t sl24_trk_out(const project_t *q, uint32_t k, const int16_t *px, uint8_t *o, int *e_ok)
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
        uint32_t mtag = 0;
        const uint8_t *mb = mrec_parts_of(q, &mtag);
        int16_t x[NPART][L2_NV];
        (void)l2_unpack(q, x);
        if (mb) {
            memcpy(mv, v, sizeof mv);
            (void)mrec_part_vals(mb, k, mv, mtag);
            memcpy(s, &mv[P_L1DA], sizeof s);
            memcpy(x[k], &mv[P_L2RATE], sizeof x[k]);
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
    if (uid == ENG_UID_FM6) {                           /* PTCH: the voice's own (sl24_fm6_ptch), the rest 0 */
        uint32_t pt = sl24_fm6_ptch(q, k, &lost);
        for (i = 0; i < 8u; i++)
            sl24_w16(o + 2u * (SL24_E0 + i), i == 7u ? (int32_t)pt : 0);
        o[2u * SL24_NP + 1u] = (uint8_t)(pt < SL24_NFAC ? pt : 0u);   /* (preset: F1..F8's, as 2.4 numbers them) */
        *e_ok = 0;
        return lost;
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
        lost |= FXS_HAS(T[i]) && (song.g[G_CRET + (T[i] - FXT_CHO)] < 127 || (fx_rmute >> (T[i] - FXT_CHO) & 1u)) ? SX24_FXRET : 0u;   /* (2.4: its wet at unity) */
        lost |= T[i] == FXT_DLY && (song.g[G_DFTYPE] || song.g[G_DFSLOPE] || song.g[G_DFRES]) ? SX24_DLYF : 0u;   /* (2.4: COLOR's low-pass) */
        lost |= FXS_HAS(T[i]) && dsend_kit[T[i] == FXT_REV ? 0u : T[i] == FXT_DLY ? 1u : 2u] < DKS_DEF ? SX24_DBUS : 0u;
    }                                                   /* (the drum track's send levels: 2.4 has none) */
#if INS_FX
    for (k = 0; k < NTRK; k++)                          /* (each its amount, P_TAIL: not in 2.4's file, nothing to clear) */
        for (i = 0; i < IFX_NFUR; i++)                  /* (the shapers too) */
            lost |= INS_ON(k, IFX_FTY(i)) && FXT_AMT[IFX_FTY(i)] != 0xFFu && trk[k].p[FXT_AMT[IFX_FTY(i)]] ? SX24_INSFX : 0u;
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

/* a synth part's 64 steps at o (as stored, 10 B each) -> SLOOP's flags: accent, slide; SF_CARRY (the notes already
 * sounding go on) -> 2.7's held notes (bits 3..6, its core.h SF_CONT_ALL: each note goes on from the step before); our
 * chance bits (2..6, seq/chance.c) are 2.7's SF_CONT: never written (SX24_CHANCE when one was set) */
static uint32_t sl27_flags_out(uint8_t *o)
{
    uint32_t i, lost = 0;
    for (i = 0; i < NSTEP; i++) {
        uint8_t *f = o + STEP_SN * i + 6u;              /* (pstep_t.flags) */
        lost |= *f & 0x7Cu ? SX24_CHANCE : 0u;
        *f = (uint8_t)((*f & (SF_ACCENT | SF_SLIDE)) | (*f & SF_CARRY ? 0x78u : 0u));
    }
    return lost;
}
/* the working drum lanes -> SLOOP's drum track at o (its 16 fixed sounds, our INST 0..15): each lane's hits on the
 * sound of its instrument (two lanes on one: both, the higher level and ratchet), MID TOM and CLAVE (not SLOOP's) on
 * LOW TOM and RIM (SX24_LANES); and 2.7's per-sound TUNE (-8 .. +7) and DECAY (-4 .. +3, the nearest of its DECAY_K
 * steps, sl27_dec_in) from the lane's TUNE / DECAY, packed as 2.7's drum_tw_put does (the drum track's E1..E7, g[G_SAVE]
 * its mark and check: an older SLOOP plays every sound as the kit has it); a value past those ranges, another lane
 * edit (SNAP .. LEVEL), two lanes on one sound tuned apart, a lane on a user sample or another kit, lanes sending
 * apart: SX24_LANES */
static uint32_t sl27_drums_out(const project_t *q, uint8_t *o)
{
    static const int8_t DK[8] = {-26, -17, -10, -4, 0, 6, 13, 21};
    const dstep_sn_t *st = q->t[TRK_DRUM].dstep;
    uint8_t *t = o + SL24_HDR + TRK_DRUM * SL24_TRK, *s = t + 2u * SL24_NP + 2u;
    uint32_t snd[DRUM_LANES], tw[16] = {0}, set = 0, lost = 0, i, l, k, c = 0x5Au;
    uint16_t w[7] = {0};
    for (l = 0; l < DRUM_LANES; l++) {
        uint32_t in = lane_inst(l), v, b = 0;
        const int8_t *x = dl.ofs[l];
        int32_t tu = x[DE_TUNE], dc = x[DE_DECAY];
        snd[l] = in == INST_MT ? INST_LT : in == INST_CLAVE ? INST_RIM : in;
        lost |= snd[l] != in || dl.src[l] != DL_KIT || dsend[l] != dsend[0] ? SX24_LANES : 0u;   /* (SLOOP: every sound from
                                                     * the kit, one send of each bus for the drums: 2.5's G_DRDLY, G_DRREV) */
        for (k = 1; k < 8u; k++)                        /* (the nearest DECAY step, b = 2.7's DECAY + 4; a tie: the shorter) */
            b = (DK[k] > dc ? DK[k] - dc : dc - DK[k]) < (DK[b] > dc ? DK[b] - dc : dc - DK[b]) ? k : b;
        v = ((uint32_t)clamp(tu, -8, 7) & 15u) | ((b - 4u) & 7u) << 4;
        lost |= tu < -8 || tu > 7 || DK[b] != dc || x[DE_SNAP] || x[DE_CLICK] || x[DE_BEND] || x[DE_CUT] || x[DE_DRIVE] ||
                x[DE_LEVEL] ? SX24_LANES : 0u;
        lost |= (set >> snd[l] & 1u) && tw[snd[l]] != v ? SX24_LANES : 0u;
        tw[snd[l]] = v, set |= 1u << snd[l];
    }
    memset(s, 0, 640u);
    for (i = 0; i < NSTEP; i++) {
        dstep_sn_t d;
        memset(&d, 0, sizeof d);
        for (l = 0; l < DRUM_LANES; l++) {
            uint32_t n = snd[l], lv, rt, ol, orr;
            if (!((st[i].on[l >> 3] >> (l & 7u)) & 1u))
                continue;
            lv = (st[i].lvl[l >> 2] >> ((l & 3u) * 2u)) & 3u, rt = (st[i].rat[l >> 2] >> ((l & 3u) * 2u)) & 3u;
            ol = (d.lvl[n >> 2] >> ((n & 3u) * 2u)) & 3u, orr = (d.rat[n >> 2] >> ((n & 3u) * 2u)) & 3u;
            d.on[n >> 3] |= (uint8_t)(1u << (n & 7u));
            d.lvl[n >> 2] = (uint8_t)((d.lvl[n >> 2] & ~(3u << ((n & 3u) * 2u))) | (lv > ol ? lv : ol) << ((n & 3u) * 2u));
            d.rat[n >> 2] = (uint8_t)((d.rat[n >> 2] & ~(3u << ((n & 3u) * 2u))) | (rt > orr ? rt : orr) << ((n & 3u) * 2u));
        }
        memcpy(s + STEP_SN * i, &d, sizeof d);
    }
    for (l = 0; l < 16u; l++)
        for (k = 0; k < 7u; k++)
            if ((tw[l] >> k) & 1u)
                w[(l * 7u + k) / 16u] |= (uint16_t)(1u << ((l * 7u + k) % 16u));
    for (k = 0; k < 7u; k++) {
        sl24_w16(t + 2u * (SL24_E0 + 1u + k), (int16_t)w[k]);
        c += (w[k] & 0xFFu) * (k * 2u + 3u) + (uint32_t)(w[k] >> 8) * (k * 2u + 4u);
    }
    sl24_w16(o + 8u + 2u * G_SAVE, (int32_t)(0x5400u | (c & 0xFFu)));
    return lost;
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
    o[SL25_DRDLY] = sl25_drdly_out();                   /* (2.5's drums' delay send; 2.4's spare, which it ignores) */
    lost |= q->rsv[0] | q->rsv[1] | q->rsv[2] ? SX24_MASTER : 0u;
    for (k = 0; k < NTRK; k++) {
        uint8_t *t = o + SL24_HDR + k * SL24_TRK;
        int e_ok;
        lost |= sl24_trk_out(q, k, px[k], t, &e_ok);
        memcpy(t + 2u * SL24_NP + 2u, q->t[k].step, 640u);
        lost |= k == TRK_DRUM ? 0u : sl27_flags_out(t + 2u * SL24_NP + 2u);
        lost |= sl24_tail_out(x ? x[k] : 0, k, e_ok, q->g[G_BPM], t + SL24_TAIL);
    }
    lost |= sl24_fx_out(o);
    lost |= sl27_drums_out(q, o);                       /* (after the drum track's values: its E1..E7 hold 2.7's TUNE / DECAY) */
    s = proj_hash(o, SL24_SIZE - 4u);
    memcpy(o + SL24_SIZE - 4u, &s, 4);
    return lost;
}
