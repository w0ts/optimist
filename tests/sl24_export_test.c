/* SPDX-License-Identifier: GPL-3.0-only */
/* Ours -> SLOOP 2.4 (FELUCCA_SL24_EXPORT: sl24_export.c proj_to_sl24, sl24_persist, sl24_word), and back through the
 * importer (sl24_import.c proj_from_sl24). Run by tests/run_tests.sh (XSTEP, TRK_FILT, CHORDPLUS: as
 * sl24_import_test: both switches, each alone).
 *   1. tests/sl24_fun5.bin (golden, written with SLOOP 2.4's own types: tests/sl24_fun5_gen.c) -> import -> export:
 *      byte for byte 2.4's file again, but for exactly what the import loses (listed in the checks);
 *   2. a project of ours using what 2.4 has not (FX OFF, ANALOG 2, a CZ part, an FM6 voice and its MOD, an X0X kit,
 *      the drum lanes, MIDI channels, the master) -> export: each field read at 2.4's offsets, the losses reported
 *      -> import: what comes back, and exactly what does not;
 *   3. the settings for 2.4's backup file (its persist_t, 88 B) and its lights word.
 * With an argument (a directory) it writes sl24_export.bin and sl24_persist.bin there, for
 * tests/sl24_export_check24.c (2.4's own types read them; not run here, it needs 2.4's tree). */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_SL24_EXPORT 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
/* LFO DEST (2026-10): a part's four depths of before as they come back after a load (lfo_store.h: the two of the most
 * weight kept, as depths) */
static int16_t leg_canon(const int16_t *d, uint32_t j)
{
    int16_t s[4], o[4];
    (void)lfo_from_depths(d, s);
    (void)lfo_depths(s, o);
    return o[j];
}
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"
/* (the rest of the firmware project.c needs: as tests/sl24_import_test.c) */
static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"
static struct { int force; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static void ui_message(const char *m) { (void)m; }
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/storage/sections/sections.c"
_Static_assert(sizeof proj_tmp >= SL24_SIZE, "ed_sl24.c makes the export in proj_tmp");

#if defined(SL24_TP) && SL24_TP
#define SL24_TP_ON 1
#else
#define SL24_TP_ON 0
#endif
/* what an import of 2.4's keeps (sl24_import.c, OPTIMIST.md "Bringing a 2.4 project over", sl24_import_test's "FILT with
 * TRK_FILT, the parts' STRUM and VLEAD with CHORDPLUS, else 0"): its FILT (value and locks) only in a TRK_FILT build, its
 * STRUM and VLEAD only in a CHORDPLUS build. A build with one switch has the three ids (SL24_TP) but drops the other's */
#define FILT_IN TFLT_ON
#define CHORD_IN FELUCCA_CHORDPLUS

static int bad;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define T24(b, k) ((b) + SL24_HDR + (k) * SL24_TRK)     /* 2.4's track k */
static int16_t r16(const uint8_t *b, uint32_t at) { return (int16_t)(b[at] | b[at + 1] << 8); }
static int16_t p24(const uint8_t *b, uint32_t k, uint32_t i) { return r16(T24(b, k), 2u * i); }
static const plock_t *l24(const uint8_t *b, uint32_t k) { return (const plock_t *)(const void *)(T24(b, k) + SL24_TAIL + 64u); }
static int has24(const uint8_t *b, uint32_t k, uint32_t step, uint32_t param, int16_t val)   /* 2.4's lock there */
{
    const plock_t *l = l24(b, k);
    uint32_t i;
    for (i = 0; i < NLOCK; i++)
        if (l[i].step == step && l[i].param == param && l[i].val == val)
            return 1;
    return 0;
}
static uint32_t n24(const uint8_t *b, uint32_t k)       /* its locks used */
{
    uint32_t i, n = 0;
    for (i = 0; i < NLOCK; i++)
        n += l24(b, k)[i].step < SX_NSTEP;
    return n;
}

static uint8_t gold[SL24_SIZE], out[SL24_SIZE], again[SL24_SIZE];

/* 1. the golden 2.4 project, in and out again */
static void golden(void)
{
    static project_t q;
    static stepx_t x[NTRK];
    const stepx_t *xp[NTRK] = {&x[0], &x[1], &x[2], &x[3]};
    static dlrec_t d0;
    uint32_t k, i, ok, lost, gone = 0xFFu;                 /* gone: the amount (P_*) of the type FILT took the slot of */
    lost = 0xFFFFu;
    if (proj_from_sl24(&q, gold, sizeof gold, x)) {
        /* the export is of the working project (ed_sl24.c) and reads its FX slots (sl24_fx_out): the import applied as
         * LOAD twice does (proj_apply; no FX record in 2.4's: fx_rec.c ins_map, each part S1 DIST S2 FILTER), then
         * exported */
        proj_apply(&q, &d0, 1);
        lost = proj_to_sl24(&q, xp, out, 0);
        host_tracks_init(), fxs_set(FXS_DEF);            /* (the state the other parts start from) */
    }
    check("golden FUN5 -> ours -> 2.4: a SLOOP 2.4 project (magic, size, FNV sum)", sl24_is(out, sizeof out));
    /* TRK_FILT: its FILT (-20, every track) and all four of DST CHO DLY REV: since the track insert slots (2026-10-12) the
     * FILTER is each part's S2, no send displaced: the round trip loses nothing for them (the old FX-SLOTS-INSERTS-DESIGN
     * P7-4 loss is gone) */
    check("... it reports the FM6 part (its voice: the closest factory patch) and nothing else (FILT and the four FX kept)",
          lost == SX24_FM6);
    (void)gone;
    ok = !memcmp(out, gold, 8u) && !memcmp(out + 8u, gold + 8u, 2u * G_MIDI) && r16(out, 8u + 2u * G_MIDI) == 0 &&
         r16(out, 8u + 2u * G_SYNC) == 0 && r16(out, 8u + 2u * G_VIEW) == 0 && r16(out, 8u + 2u * G_INFO) == 0 &&
         !memcmp(out + 8u + 2u * (G_INFO + 1u), gold + 8u + 2u * (G_INFO + 1u), 2u * (PJ_NG - G_INFO - 1u) + 4u);
    check("globals as 2.4 wrote them but MIDI (13 -> 0), SYNC (14, past TRS -> INT), ROUTE (1 -> 0), slot 15 (-> 0)", ok);
    for (k = 0, ok = 1; k < NTRK; k++) {
        const uint8_t *a = T24(out, k), *g = T24(gold, k);
        for (i = 0; i < SL24_COMMON; i++)                                          /* values 0..49 */
            ok &= k < NPART && i >= P_LEG_PIT && i <= P_LEG_AMP     /* (a part's LFO depths: two kept) */
                      ? p24(out, k, i) == leg_canon((const int16_t *)(const void *)(g + 2u * P_LEG_PIT), i - P_LEG_PIT)
                  : SL24_TP_ON && k == TRK_DRUM && (i == P_GLMODE || i == P_PRIO || i == P_ALLOC || i == P_DETUNE)
                      ? p24(out, k, i) == TP[i].def     /* (the import packs TFLT STRUM there: px_pack; the drum's own lost) */
                      : i == gone ? p24(out, k, i) == 0 && p24(gold, k, i) != 0   /* (P7-4: not heard here) */
                      : p24(out, k, i) == p24(gold, k, i);
        ok &= p24(out, k, 50) == (FILT_IN ? p24(gold, k, 50) : 0);                 /* FILT (heard: in a slot) */
        ok &= p24(out, k, 51) == (CHORD_IN && k < NPART ? p24(gold, k, 51) : 0);   /* STRUM, VLEAD: the parts' */
        ok &= p24(out, k, 52) == (CHORD_IN && k < NPART ? p24(gold, k, 52) : 0) && p24(gold, k, 52) == 1;
        ok &= k == 1 || !memcmp(a + 2u * SL24_E0, g + 2u * SL24_E0, 16u);           /* E0..E7 (FM6: below) */
        ok &= a[122] == g[122] && (k == 1 || a[123] == g[123]);                    /* engine, preset */
        ok &= !memcmp(a + 124, g + 124, 640u);                                     /* the steps */
        ok &= !memcmp(a + SL24_TAIL, g + SL24_TAIL, 64u) && !memcmp(a + SL24_TAIL + 160u, g + SL24_TAIL + 160u, 16u);
    }
    check(FILT_IN ? "tracks: values 0..49 (but the type FILT displaced: 0), E0..E7, engine, preset, steps, nudges, fills "
                    "byte for byte; FILT kept; STRUM / VLEAD kept with CHORDPLUS, else lost (0)"
                  : "tracks: values 0..49, E0..E7, engine, preset, steps, nudges, fills byte for byte; FILT lost (0) "
                    "without TRK_FILT; STRUM / VLEAD kept with CHORDPLUS, else lost (0)", ok);
    ok = p24(out, 1, SL24_E0 + 7u) == 0 && T24(out, 1)[123] == 0;
    for (i = 0; i < 7u; i++)
        ok &= p24(out, 1, SL24_E0 + i) == 0;
    check("FM6 part: its macros are in the voice (not F3 now) -> PTCH F1 (lost); the macros ALG FB MLVL MRAT MEG VMOD DTUN 0", ok);
    for (k = 0, ok = 1; k < NTRK; k++) {
        ok &= has24(out, k, 2, P_PAN, (int16_t)(-30 + (int)k)) && has24(out, k, 6, P_LEVEL, 100);
        ok &= k == 1 ? !has24(out, k, 5, SL24_E0 + 1u, 7) : has24(out, k, 5, SL24_E0 + 1u, 7);
        ok &= has24(out, k, 4, SL24_TFLT, 50) == FILT_IN && n24(out, k) == 3u - (k == 1) + FILT_IN + 2u * (k == 3);
    }
    ok &= has24(out, 3, 8, SL24_E0, 12) && has24(out, 3, 9, SL24_E0, 5);
    check("locks: PAN, LEVEL, EDIT 2 back with 2.4's ids; lost: FILT's (without TRK_FILT), FM6's EDIT, kit USR2 -> 808", ok);
}

/* 2. ours, with what 2.4 has not */
static void ours(project_t *q, stepx_t *x)
{
    int16_t v[P_COUNT];
    uint32_t k, i;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);
    q->magic = PROJ_MAGIC, q->size = sizeof *q;
    for (i = 0; i < PJ_NG; i++)
        q->g[i] = GP[i].def;
    q->g[G_BPM] = 123, q->g[G_MIDI] = 0x0421, q->g[G_VIEW] = 2, q->g[G_SYNC] = SYNC_AUTO, q->g[G_DRREV] = DRREV_MOVED;
    q->g[G_INFO] = 1;                                   /* (REC QUANTIZE OFF, project.c RECQ_PACK: never to 2.4) */
    q->sel = 1, q->rsv[0] = 2;                          /* (the reverb's algorithm) */
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_COUNT; i++)
            v[i] = TP[i].def;
        for (i = 0; i < SL24_COMMON; i++)
            v[i] = (int16_t)(k * 11u + i * 3u) - 20;
        for (i = 0; i < 8u; i++)
            v[P_E0 + i] = (int16_t)(20 + i + k);
        if (k == 0)
            v[P_FXOFF] = 1, v[P_A2SEMI] = 7;            /* FX OFF, ANALOG 2's OSC 2 */
        if (k == 1)
            v[P_E0] = 4, v[P_E1] = 3, v[P_E4] = 1;      /* FM6: R05 (FM MARIMBA), MOD 3, MARK I */
        if (k == 3)
            v[P_E0] = 37;                               /* an X0X kit */
        pj_from_p(q->t[k].p, v);
        for (i = 0; i < 640u; i++)
            ((uint8_t *)q->t[k].step)[i] = (uint8_t)(i * 7u + k);
        stepx_clear(&x[k]);
        x[k].micro[5] = (int8_t)(-7 - (int)k);
        stepx_fill_set(&x[k], 6, FC_FILL);
    }
    q->t[0].engine = 0, q->t[0].preset = 4;
    q->t[1].engine = ENG_UID_FM6, q->t[1].preset = 4;
    q->t[2].engine = ENG_UID_CZ, q->t[2].preset = 3;
    q->dl_hash = 0x1234u;                               /* (the drum lanes' record) */
    stepx_lock_set(&x[0], 2, P_PAN, -10);
    stepx_lock_set(&x[0], 3, P_FXOFF, 1);               /* (2.4 has no FX OFF) */
    stepx_lock_set(&x[0], 4, P_E0 + 2u, 9);
    stepx_lock_set(&x[1], 1, P_E1, 5);                  /* (FM6's MOD) */
    stepx_lock_set(&x[1], 1, P_LEVEL, 90);
    stepx_lock_set(&x[2], 0, P_E0, 3);                  /* (CZ's EDIT) */
    stepx_lock_set(&x[3], 8, P_E0, 12);
    stepx_lock_set(&x[3], 9, P_E0, 40);                 /* (a user kit) */
    stepx_lock_set(&x[3], 10, P_E1, 2);
    q->sum = proj_sum(q);
}
static void native(const char *dir)
{
    static project_t q, r;
    static stepx_t x[NTRK], y[NTRK];
    const stepx_t *xp[NTRK] = {&x[0], &x[1], &x[2], &x[3]};
    int16_t v[P_COUNT], w[P_COUNT];
    uint32_t k, i, ok, lost;
    ours(&q, x);
    lost = proj_to_sl24(&q, xp, out, 0);
    check("ours -> 2.4: a SLOOP 2.4 project", sl24_is(out, sizeof out));
    check("... lost, reported: ENGINE (CZ), FM6, FX OFF, ANALOG 2, KIT, LOCKS, the drum LANES, the MASTER (rsv)",
          lost == (SX24_ENGINE | SX24_FM6 | SX24_FXOFF | SX24_A2 | SX24_KIT | SX24_LOCK | SX24_LANES | SX24_MASTER));
    ok = r16(out, 8u) == 123 && r16(out, 8u + 2u * G_MIDI) == 0 && r16(out, 8u + 2u * G_VIEW) == 0 &&
         r16(out, 8u + 2u * G_SYNC) == 0 && r16(out, 8u + 2u * G_DRREV) == 16 && out[8u + 2u * PJ_NG] == 1 &&
         r16(out, 8u + 2u * G_INFO) == 0;
    check("globals: BPM; MIDI channels, VIEW, QUANT (G_INFO) -> 0; SYNC AUTO -> INT; the lanes' REV -> 16; selection", ok);
    for (k = 0, ok = 1; k < NTRK; k++) {
        int16_t d[4];
        for (i = 0; i < 4u; i++)
            d[i] = (int16_t)((int16_t)(k * 11u + (P_LEG_PIT + i) * 3u) - 20);
        for (i = 0; i < SL24_COMMON; i++)
            ok &= p24(out, k, i) == (k < NPART && i >= P_LEG_PIT && i <= P_LEG_AMP ? leg_canon(d, i - P_LEG_PIT)
                                                                                  : (int16_t)(k * 11u + i * 3u) - 20);
        ok &= p24(out, k, 50) == 0 && p24(out, k, 51) == 0 && p24(out, k, 52) == 0;
        ok &= !memcmp(T24(out, k) + 124, q.t[k].step, 640u) && (int8_t)T24(out, k)[SL24_TAIL + 5u] == -7 - (int)k;
        ok &= stepx_fill((const stepx_t *)(const void *)(T24(out, k) + SL24_TAIL), 6) == FC_FILL;
    }
    check("values 0..49 at 2.4's offsets, its 50..52 at 0 (TFLT STRUM VLEAD: off); steps, nudges, fills", ok);
    for (i = 0, ok = T24(out, 0)[122] == 0 && T24(out, 0)[123] == 4; i < 8u; i++)
        ok &= p24(out, 0, SL24_E0 + i) == (int16_t)(20 + i);
    check("ANALOG part: engine 0, preset, E0..E7 at 2.4's 53..60", ok);
    for (i = 0, ok = T24(out, 1)[122] == 9 && p24(out, 1, SL24_E0 + 7u) == 0 && T24(out, 1)[123] == 0; i < 7u; i++)
        ok &= p24(out, 1, SL24_E0 + i) == 0;
    check("FM6 part (no voice of its own): engine 9, PTCH F1 (the closest-patch mapping of R voices is gone), macros 0, preset F1", ok);
    for (i = 0, ok = T24(out, 2)[122] == 2 && T24(out, 2)[123] == 0; i < 8u; i++)
        ok &= p24(out, 2, SL24_E0 + i) == ENG_PHASE.edit[i].def;
    check("CZ part: 2.4 has no CZ -> its fallback PHASE (engine 2) at PHASE's EDIT defaults", ok);
    ok = p24(out, 3, SL24_E0) == 5 && p24(out, 3, SL24_E0 + 1u) == 24;
    check("drum track: the X0X kit (37) -> 808 (5), E1 as it is", ok);
    ok = has24(out, 0, 2, P_PAN, -10) && has24(out, 0, 4, SL24_E0 + 2u, 9) && n24(out, 0) == 2u &&
         has24(out, 1, 1, P_LEVEL, 90) && n24(out, 1) == 1u && n24(out, 2) == 0u &&
         has24(out, 3, 8, SL24_E0, 12) && has24(out, 3, 9, SL24_E0, 5) && has24(out, 3, 10, SL24_E0 + 1u, 2) &&
         n24(out, 3) == 3u && l24(out, 0)[2].step == LOCK_FREE;
    check("locks: 2.4's ids, packed; dropped: FX OFF's, FM6's MOD, CZ's EDIT; the user kit lock -> 808", ok);
    /* back in */
    ok = proj_from_sl24(&r, out, sizeof out, y) && proj_ok(&r);
    check("... and back in (the importer): a valid project of ours", ok);
    for (k = 0; k < NTRK; k++) {
        pj_to_p(v, q.t[k].p), pj_to_p(w, r.t[k].p);
        for (i = 0; i < SL24_COMMON; i++)
            ok &= k < NPART && i >= P_LEG_PIT && i <= P_LEG_AMP ? w[i] == leg_canon(&v[P_LEG_PIT], i - P_LEG_PIT) : v[i] == w[i];
        ok &= !memcmp(q.t[k].step, r.t[k].step, sizeof q.t[k].step) && !memcmp(y[k].micro, x[k].micro, SX_NSTEP) &&
              !memcmp(y[k].fill, x[k].fill, sizeof x[k].fill);
        ok &= w[P_FXOFF] == TP[P_FXOFF].def;
#if FELUCCA_ANALOG2
        ok &= w[P_A2SEMI] == TP[P_A2SEMI].def;
#endif
    }
    pj_to_p(w, r.t[0].p);
    for (i = 0; i < 8u; i++)
        ok &= w[P_E0 + i] == (int16_t)(20 + i);
    check("kept: values 0..49, steps, nudges, fills, ANALOG's EDIT; lost: FX OFF, ANALOG 2 (their defaults)", ok);
    pj_to_p(w, r.t[1].p);
    ok = r.t[1].engine == ENG_UID_FM6 && w[P_E0] == 0 && w[P_E1] == 0 && w[P_E4] == 0 && ((r.fm6_has >> 1) & 1u) &&
         !memcmp(r.fm6[1], SL24_FM6_F[0], 128);
    check("FM6: back as VOICE INIT VOICE with 2.4's F1 as its voice (the part's own voice was lost)", ok);
    ok = r.t[2].engine == 2 && q.t[2].engine == ENG_UID_CZ;
    pj_to_p(w, r.t[3].p);
    ok &= w[P_E0] == 5 && w[P_E1] == 24;
    ok &= r.g[G_BPM] == 123 && r.g[G_MIDI] == 0 && r.g[G_DRREV] == 16 && r.dl_hash == 0 && !r.rsv[0] && r.sel == 1;
    check("lost: CZ (PHASE now), the X0X kit (808), MIDI channels, the lanes' record, the reverb type", ok);
    ok = stepx_lock_find(&y[0], 2, P_PAN) >= 0 && stepx_lock_find(&y[0], 4, P_E0 + 2u) >= 0 &&
         stepx_lock_find(&y[0], 3, P_FXOFF) < 0 && stepx_lock_find(&y[1], 1, P_LEVEL) >= 0 &&
         stepx_lock_find(&y[1], 1, P_E1) < 0 && stepx_lock_find(&y[2], 0, P_E0) < 0 &&
         stepx_lock_find(&y[3], 10, P_E1) >= 0 && y[3].lock[stepx_lock_find(&y[3], 9, P_E0)].val == 5;
    check("locks back: PAN, ANALOG's E2, LEVEL, the kit's; lost: FX OFF's, FM6 MOD's, CZ's EDIT; user kit -> 808", ok);
    /* export (import (export)) = export: the losses happen once */
    {
        const stepx_t *yp[NTRK] = {&y[0], &y[1], &y[2], &y[3]};
        lost = proj_to_sl24(&r, yp, again, 0);
        check("2.4 -> ours -> 2.4 again: the same bytes; the FM6 voice is 2.4's F6 now: no loss there", !memcmp(again, out, sizeof out) &&
              lost == 0);
    }
    /* no extras: tails empty */
    proj_to_sl24(&q, 0, again, 0);
    for (k = 0, ok = 1; k < NTRK; k++)
        ok &= n24(again, k) == 0 && !T24(again, k)[SL24_TAIL + 5u] && !T24(again, k)[SL24_TAIL + 161u];
    check("without step extras: 2.4's tails empty (no nudge, every lock slot free, every step NORM)", ok);
    if (dir) {
        char p[512];
        FILE *f;
        snprintf(p, sizeof p, "%s/sl24_export.bin", dir);
        if ((f = fopen(p, "wb")))
            fwrite(out, 1, sizeof out, f), fclose(f);
    }
}

/* 2b. FM6 voices both ways: 2.4's factory patches by PTCH, the others through 2.4's bank (the editor's choice) */
static void fm6_voices(void)
{
    static project_t q, r;
    static stepx_t x[NTRK], y[NTRK];
    static uint8_t bank[SL24_BANK_LEN], other[128], mine[128];
    const stepx_t *xp[NTRK] = {&x[0], &x[1], &x[2], &x[3]};
    int16_t v[P_COUNT];
    uint32_t k, lost, used, ok;
    ours(&q, x);
    memcpy(mine, SL24_FM6_F[6], 128), memcpy(mine + 118, "MY ORGAN  ", 10);
    memcpy(other, SL24_FM6_F[0], 128), memcpy(other + 118, "2.4 OWN   ", 10);
    for (k = 0; k < NPART; k++) {                       /* parts 0, 1 my voice, part 2 2.4's F2 exactly */
        pj_to_p(v, q.t[k].p);
        v[P_E0] = 0, v[P_E1] = 0, v[P_E2] = 0, v[P_E3] = 0, v[P_E4] = 1;
        pj_from_p(q.t[k].p, v);
        q.t[k].engine = ENG_UID_FM6;
        memcpy(q.fm6[k], k == 2 ? SL24_FM6_F[1] : mine, 128);
        q.fm6_on[k] = 0x3F;
    }
    q.fm6_has = 7;
    q.sum = proj_sum(&q);
    sl24_bank_empty(bank);
    used = 1, memcpy(bank + 8, &used, 4), memcpy(bank + 16, other, 128);   /* (2.4's bank kept: B1 its own) */
    lost = proj_to_sl24(&q, xp, out, bank);
    memcpy(&used, bank + 8, 4);
    ok = p24(out, 0, SL24_E0 + 7u) == 9 && p24(out, 1, SL24_E0 + 7u) == 9 && p24(out, 2, SL24_E0 + 7u) == 1 &&
         T24(out, 2)[123] == 1 && T24(out, 0)[123] == 0 && used == 3u && !memcmp(bank + 16, other, 128) &&
         !memcmp(bank + 16 + 128, mine, 128);
    check("bank: F2 exactly -> PTCH F2; my voice -> B2 (B1 is 2.4's own, kept), once for both parts", ok);
    check("... reported: the bank goes with it; no FM6 loss", (lost & SX24_BANK) && !(lost & SX24_FM6));
    ok = proj_from_sl24(&r, out, sizeof out, y) && r.fm6_has == 7;
    ok &= !memcmp(r.fm6[2], q.fm6[2], 128);             /* (F2 exactly: back as it was) */
    for (k = 0; k < 2u; k++)
        ok &= !memcmp(r.fm6[k], SL24_FM6_INIT, 128);    /* (a bank patch is not imported: INIT, 2.4's empty slot) */
    check("... and back: F2 as it was; a bank patch is not imported (the part starts from INIT)", ok);
    lost = proj_to_sl24(&q, xp, out, 0);
    ok = p24(out, 0, SL24_E0 + 7u) == 0 && p24(out, 2, SL24_E0 + 7u) == 1 && (lost & SX24_FM6) && !(lost & SX24_BANK);
    check("without the bank: my voice -> the closest factory patch (lost), F2 still F2", ok);
    used = (1u << SL24_BANK_N) - 1u, memcpy(bank + 8, &used, 4);
    memset(bank + 16 + 128, 0, 128);                    /* (full, none of them mine) */
    lost = proj_to_sl24(&q, xp, out, bank);
    check("a full bank: the closest factory patch (lost)", p24(out, 0, SL24_E0 + 7u) == 0 && (lost & SX24_FM6));
    q.fm6_on[1] = 0x1F;                                 /* (OP6 off: 2.4 has no switches) */
    sl24_bank_empty(bank);
    lost = proj_to_sl24(&q, xp, out, bank);
    check("an operator switched off: not 2.4's (lost), the others still go to the bank",
          p24(out, 1, SL24_E0 + 7u) == 0 && p24(out, 0, SL24_E0 + 7u) == 8 && (lost & SX24_FM6) && (lost & SX24_BANK));
}

/* 3. the settings for 2.4 */
static void settings24(const char *dir)
{
    static uint8_t o[SL24_PERSIST + 4];
    uint8_t pan[32];
    uint32_t i, m, ok;
    for (i = 0; i < 32u; i++)
        pan[i] = (uint8_t)(i + 1);
    memset(o, 0xEE, sizeof o);
    sl24_persist(o, 3, 1, 7, pan, 0x12345u);
    memcpy(&m, o, 4);
    ok = m == 0x50455233u && o[4] == 3 && o[8] == 1 && o[12] == 1 && !memcmp(o + 16, pan, 32) && o[48] == 4 &&
         o[49] == 0 && o[52] == 0 && o[53] == 4 && o[52 + 2 * 5] == 1 && o[52 + 2 * 15] == 3 && o[84] == 0x45 &&
         o[86] == 0x01 && o[SL24_PERSIST] == 0xEE;
    check("2.4's persist_t: PER3, palette, low cut, zoom (0 / 1), the panel table, song A B C D x 4 bars, the word; 88 B", ok);
    sl24_persist(o, 9, 0, 0, pan, 0);
    check("... a palette 2.4 has not (past its 5): GREEN", o[4] == 0);
    ok = sl24_word(7u << 21 | 0x100u, 0) == 0u && sl24_word(3u << 24 | 0x100u, 0) == 0u &&
         sl24_word(1u << 25 | 0x100u, 0) == 0u && sl24_word(1u << 24 | 0x100u, 0) == 0u;   /* HOLD 21..22, CARDS 23, BLUETOOTH 24, KNOB COLORS 25 */
    check("the lights word: our HOLD (21..22), CARDS (23), BLUETOOTH (24), KNOB COLORS (25) and bits 24..31 never reach a 2.4 file", ok);
    ok = sl24_word(0x1C6FFu, 1) == 0x1D7FFu && sl24_word(0x100u, 3) == 0u && sl24_word(0u, 2) == 0x2100u &&
         sl24_word(0x1800u, 0) == 0x100u && sl24_word(0xFFFFFFFFu, 0) == 0x1C6FFu;
    check("the lights word: 2.3's bits, MIDI OUT / IN, USB SERIAL as they are; NOTES inverted; SYNC (AUTO -> INT) at 12", ok);
    if (dir) {
        char p[512];
        FILE *f;
        for (i = 0; i < 32u; i++)                       /* (a real panel table for 2.4's checks: PAN5, the identity) */
            pan[i] = 0;
        m = 0x50414E35u, memcpy(pan, &m, 4);
        for (i = 0; i < 14u; i++)
            pan[4 + i] = (uint8_t)i;
        for (i = 0; i < 7u; i++)
            pan[18 + i] = (uint8_t)i, pan[25 + i] = 1;
        sl24_persist(o, 2, 1, 0, pan, sl24_word(0x4012u, 1));
        snprintf(p, sizeof p, "%s/sl24_persist.bin", dir);
        if ((f = fopen(p, "wb")))
            fwrite(o, 1, SL24_PERSIST, f), fclose(f);
    }
}

/* the FX slots (fx_slots.c, the working project's: sl24_fx_out): a type in no slot written 0 (2.4 would play it), and what
 * 2.4 has not, heard here: COMP (a part, the drum bus, a drum sound), a drum sound's DIST, the drum bus's sends */
static void fx_slots24(void)
{
    static project_t q;
    static stepx_t x[NTRK];
    static const uint8_t NOREV[FX_NSLOT] = {FXT_NONE, FXT_CHO, FXT_DLY, FXT_NONE}, NOINS[2] = {FXT_NONE, FXT_NONE};
    uint32_t k, ok, lost, base;
    ours(&q, x);
    ins_defaults();
    base = proj_to_sl24(&q, 0, out, 0);
    check("FX slots: the default layout and track slots, every insert amount 0: nothing more lost than before",
          !(base & (SX24_SLOTS | SX24_COMP | SX24_DINS | SX24_DBUS | SX24_INSORD)));
    fxs_set(NOREV);
    lost = proj_to_sl24(&q, 0, out, 0);
    for (k = 0, ok = 1; k < NTRK; k++)
        ok &= p24(out, k, P_REV) == 0 && p24(out, k, P_DLY) == (int16_t)(k * 11u + P_DLY * 3u) - 20 && q.t[k].p[P_REV] != 0;
    check("REV in no master slot: its amounts written 0 (2.4 would play them), the others as they are; SLOTS lost", ok && lost == (base | SX24_SLOTS));
    fxs_set(FXS_DEF);
    ins_put(1, NOINS);
    lost = proj_to_sl24(&q, 0, out, 0);
    check("T2's slots empty: its DST (set) written 0, the other parts' as they are; SLOTS lost",
          p24(out, 1, P_DIST) == 0 && (q.t[1].p[P_DIST] == 0 || lost == (base | SX24_SLOTS)) &&
          p24(out, 2, P_DIST) == q.t[2].p[P_DIST]);
    ins_defaults();
#if FELUCCA_MASTER_COMP
    ins_load(1, 1, FXT_COMP);
    trk[1].p[P_TCOMP] = 40;
    check("COMP in T2's S2 and its amount: COMP lost", proj_to_sl24(&q, 0, out, 0) == (base | SX24_COMP));
    ins_defaults();
    check("... COMP in no slot of T2 (its amount not heard): not lost", proj_to_sl24(&q, 0, out, 0) == base);
    trk[1].p[P_TCOMP] = 0, dins_amt[1][7] = 3;
    check("a drum sound's COMP (always heard, D7): COMP lost", proj_to_sl24(&q, 0, out, 0) == (base | SX24_COMP));
    dins_amt[1][7] = 0;
#endif
    dins_amt[0][2] = 9, TDRUM->p[P_DLY] = 20;
    check("a drum sound's DIST, the drum bus's DLY: DINS, DBUS lost", proj_to_sl24(&q, 0, out, 0) == (base | SX24_DINS | SX24_DBUS));
    dins_amt[0][2] = 0, TDRUM->p[P_DLY] = 0;
    dsend_kit[1] = 64;
    check("the drum track's DLY level at 50 %: DBUS lost (2.4 has none)", proj_to_sl24(&q, 0, out, 0) == (base | SX24_DBUS));
    dsend_kit[1] = DKS_DEF;
#if FELUCCA_TRK_FILT
    {
        static const uint8_t FD[2] = {FXT_FILT, FXT_DIST};
        int16_t f = trk[0].p[P_TFLT], d = trk[0].p[P_DIST];
        trk[0].p[P_TFLT] = -20, trk[0].p[P_DIST] = 50;
        ins_put(0, FD);
        check("T1's FILTER before its DIST, both on: INSORD lost (2.4 runs DIST, then FILT)",
              (proj_to_sl24(&q, 0, out, 0) & SX24_INSORD) != 0u);
        ins_defaults();
        check("... S1 DIST S2 FILTER (2.4's order): not lost", (proj_to_sl24(&q, 0, out, 0) & SX24_INSORD) == 0u);
        trk[0].p[P_TFLT] = f, trk[0].p[P_DIST] = d;
    }
#endif
#if INS_FX
    {   /* the further inserts (insfx.c): 2.4 has none */
        uint32_t t, k;
        for (t = FXT_NEW0; t < FXT_NEWN; t++) {
            if (FXT_AMT[t] == 0xFFu)
                continue;
            for (k = 0; k < NTRK; k++) {
                uint8_t s[2] = {(uint8_t)t, FXT_NONE};
                uint32_t l0, l1;
                ins_put(k, s);
                l0 = proj_to_sl24(&q, 0, out, 0);
                trk[k].p[FXT_AMT[t]] = 90;
                l1 = proj_to_sl24(&q, 0, out, 0);
                s[0] = FXT_NONE;
                ins_put(k, s);                          /* (in no slot: not heard, nothing lost for it) */
                if ((l0 & SX24_INSFX) || !(l1 & SX24_INSFX) || (proj_to_sl24(&q, 0, out, 0) & SX24_INSFX))
                    break;
                trk[k].p[FXT_AMT[t]] = 0;
            }
            check("a further insert heard with an amount: INSFX lost; at 0 or in no slot: not", k == NTRK);
        }
        ins_defaults();
        check("INSFX: the second word's bit 7, its own", (SX24_INSFX >> 14) == 128u && !(SX24_INSFX & (SX24_INSORD | SX24_NGATE)));
    }
#endif
    /* the FX returns (2026-10-12, core.h G_CRET G_DRET G_RRET): 2.4 has none, its wet comes back at unity */
    song.g[G_CRET] = song.g[G_DRET] = song.g[G_RRET] = 127;
    base = proj_to_sl24(&q, 0, out, 0);
    check("FX returns at unity: nothing lost for them", !(base & SX24_FXRET));
    check("the second word's bits are each their own: CHANCE 0, LSYNC 1, LAUTO 2, DLFO 3, FXRET 4, INSORD 6",
          (SX24_CHANCE >> 14) == 1u && (SX24_LSYNC >> 14) == 2u && (SX24_LAUTO >> 14) == 4u && (SX24_DLFO >> 14) == 8u &&
          (SX24_FXRET >> 14) == 16u && (SX24_INSORD >> 14) == 64u && SX24_INSORD == 1048576u);
    song.g[G_DRET] = 64;
    check("DLY RET at one half: FXRET lost (2.4 plays it at unity)", proj_to_sl24(&q, 0, out, 0) == (base | SX24_FXRET));
    fxs_set(NOREV);
    song.g[G_DRET] = 127, song.g[G_RRET] = 0;
    check("REV RET at 0 with REV in no slot: not heard here either, nothing more lost for it",
          (proj_to_sl24(&q, 0, out, 0) & SX24_FXRET) == 0u);
    fxs_set(FXS_DEF);
    check("... REV back in its slot: FXRET lost", (proj_to_sl24(&q, 0, out, 0) & SX24_FXRET) != 0u);
    song.g[G_RRET] = 127;
    /* the delay filter (2026-10-18): 2.4 has COLOR's low-pass alone */
    song.g[G_DFTYPE] = song.g[G_DFSLOPE] = song.g[G_DFRES] = 0;
    check("the delay filter at LP 12 dB RES 0: nothing lost for it", !(proj_to_sl24(&q, 0, out, 0) & SX24_DLYF));
    song.g[G_DFTYPE] = DLYF_HP;
    check("HP: DLYF lost, the second word's bit 13", (proj_to_sl24(&q, 0, out, 0) & SX24_DLYF) && (SX24_DLYF >> 14) == 1u << 13);
    song.g[G_DFTYPE] = 0, song.g[G_DFRES] = 40;
    check("RES 40: DLYF lost", (proj_to_sl24(&q, 0, out, 0) & SX24_DLYF) != 0u);
    song.g[G_DFRES] = 0;
}

/* the LFO's RATE synced (< 0: a note value, SLOOP 2.6's SYNC; 2.4 has none) -> the free rate nearest that note value's
 * at the project's tempo (log scale), its locks too; lost: LSYNC. A free RATE as it is */
static int32_t near_rate(int32_t bpm, double beats)
{
    double hz = bpm / 60.0 / beats, best = 1e9;
    int32_t i, r = 0;
    for (i = 0; i < 128; i++) {
        double e = fabs(log(LFO_HZ_X100[i] / 100.0 / hz));
        if (e < best)
            best = e, r = i;
    }
    return r;
}
static void lsync24(void)
{
    static project_t q, r;
    static stepx_t x[NTRK], y[NTRK];
    const stepx_t *xp[NTRK] = {&x[0], &x[1], &x[2], &x[3]};
    int16_t v[P_COUNT];
    uint32_t lost, base, ok;
    ours(&q, x);
    base = proj_to_sl24(&q, xp, out, 0);
    check("LFO sync: a free RATE everywhere: LSYNC not lost", !(base & SX24_LSYNC));
    pj_to_p(v, q.t[0].p), v[P_LRATE] = -5, pj_from_p(q.t[0].p, v);      /* 1/4 at 123 BPM: 2.05 Hz */
    pj_to_p(v, q.t[2].p), v[P_LRATE] = -1, pj_from_p(q.t[2].p, v);      /* 4BAR: 0.128 Hz */
    stepx_lock_set(&x[1], 7, P_LRATE, -10);             /* 1/32: 16.4 Hz */
    stepx_lock_set(&x[1], 8, P_LRATE, 33);
    q.sum = proj_sum(&q);
    lost = proj_to_sl24(&q, xp, out, 0);
    ok = lost == (base | SX24_LSYNC) && p24(out, 0, P_LRATE) == near_rate(123, 1.0) &&
         p24(out, 2, P_LRATE) == near_rate(123, 16.0) && p24(out, 1, P_LRATE) == (int16_t)(11u + P_LRATE * 3u) - 20 &&
         has24(out, 1, 7, P_LRATE, (int16_t)near_rate(123, 0.125)) && has24(out, 1, 8, P_LRATE, 33);
    check("LFO sync -> 2.4: 1/4, 4BAR, a 1/32 lock -> the nearest free rates at 123 BPM; free ones as they are; "
          "lost LSYNC", ok);
    ok = proj_from_sl24(&r, out, sizeof out, y) && proj_ok(&r);
    pj_to_p(v, r.t[0].p);
    check("... and back in: the free rate (2.4 has no sync)", ok && v[P_LRATE] == near_rate(123, 1.0));
}

int main(int argc, char **argv)
{
    FILE *f = fopen("tests/sl24_fun5.bin", "rb");
    if (!f || fread(gold, 1, sizeof gold, f) != sizeof gold) {
        printf("tests/sl24_fun5.bin missing (run from the repo root)\n");
        return 1;
    }
    fclose(f);
    host_tracks_init();
    golden();
    native(argc > 1 ? argv[1] : 0);
    fm6_voices();
    settings24(argc > 1 ? argv[1] : 0);
    fx_slots24();
    lsync24();
    printf("sl24 export test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
