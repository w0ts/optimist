/* SPDX-License-Identifier: GPL-3.0-only */
/* Ours -> SLOOP 2.4 (FELUCCA_SL24_EXPORT: sl24_export.c proj_to_sl24, sl24_persist, sl24_word), and back through the
 * importer (sl24_import.c proj_from_sl24). Run by tests/run_tests.sh (XSTEP 0 and 1).
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
    uint32_t k, i, ok, lost;
    lost = proj_from_sl24(&q, gold, sizeof gold, x, 0) ? proj_to_sl24(&q, xp, out, 0) : 0xFFFFu;
    check("golden FUN5 -> ours -> 2.4: a SLOOP 2.4 project (magic, size, FNV sum)", sl24_is(out, sizeof out));
    check("... it reports the FM6 part (its voice: the closest factory patch) and nothing else",
          lost == SX24_FM6);
    ok = !memcmp(out, gold, 8u) && !memcmp(out + 8u, gold + 8u, 2u * G_MIDI) && r16(out, 8u + 2u * G_MIDI) == 0 &&
         r16(out, 8u + 2u * G_SYNC) == 0 && r16(out, 8u + 2u * G_VIEW) == 0 &&
         !memcmp(out + 8u + 2u * (G_VIEW + 1u), gold + 8u + 2u * (G_VIEW + 1u), 2u * (PJ_NG - G_VIEW - 1u) + 4u);
    check("globals as 2.4 wrote them but MIDI (13 -> 0), SYNC (14, past TRS -> INT) and ROUTE (1 -> 0): its devices'", ok);
    for (k = 0, ok = 1; k < NTRK; k++) {
        const uint8_t *a = T24(out, k), *g = T24(gold, k);
        for (i = 0; i < SL24_COMMON; i++)                                          /* values 0..49 */
            ok &= SL24_TP_ON && k == TRK_DRUM && (i == P_GLMODE || i == P_PRIO || i == P_ALLOC || i == P_DETUNE)
                      ? p24(out, k, i) == TP[i].def     /* (the import packs TFLT STRUM there: px_pack; the drum's own lost) */
                      : p24(out, k, i) == p24(gold, k, i);
        ok &= p24(out, k, 50) == (SL24_TP_ON ? p24(gold, k, 50) : 0) && p24(out, k, 52) == (SL24_TP_ON && k < NPART ? 1 : 0);
        ok &= k == 1 || !memcmp(a + 2u * SL24_E0, g + 2u * SL24_E0, 16u);           /* E0..E7 (FM6: below) */
        ok &= a[122] == g[122] && (k == 1 || a[123] == g[123]);                    /* engine, preset */
        ok &= !memcmp(a + 124, g + 124, 640u);                                     /* the steps */
        ok &= !memcmp(a + SL24_TAIL, g + SL24_TAIL, 64u) && !memcmp(a + SL24_TAIL + 160u, g + SL24_TAIL + 160u, 16u);
    }
    check("tracks: values 0..49, E0..E7, engine, preset, steps, nudges, fills byte for byte; TFLT / VLEAD lost "
          "(0) without SL24_TP", ok);
    ok = p24(out, 1, SL24_E0 + 7u) == 2 && T24(out, 1)[123] == 2;
    for (i = 0; i < 7u; i++)
        ok &= p24(out, 1, SL24_E0 + i) == (i == 2u ? 10 : 0);
    check("FM6 part: PTCH F3 (its macros in the voice: not F3 now) -> R03 -> F3; MLVL 10 back; ALG FB MRAT MEG VMOD DTUN 0", ok);
    for (k = 0, ok = 1; k < NTRK; k++) {
        ok &= has24(out, k, 2, P_PAN, (int16_t)(-30 + (int)k)) && has24(out, k, 6, P_LEVEL, 100);
        ok &= k == 1 ? !has24(out, k, 5, SL24_E0 + 1u, 7) : has24(out, k, 5, SL24_E0 + 1u, 7);
        ok &= has24(out, k, 4, SL24_TFLT, 50) == SL24_TP_ON && n24(out, k) == 3u - (k == 1) + SL24_TP_ON + 2u * (k == 3);
    }
    ok &= has24(out, 3, 8, SL24_E0, 12) && has24(out, 3, 9, SL24_E0, 5);
    check("locks: PAN, LEVEL, EDIT 2 back with 2.4's ids; lost: TFLT's (without SL24_TP), FM6's EDIT, kit USR2 -> 808", ok);
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
         r16(out, 8u + 2u * G_SYNC) == 0 && r16(out, 8u + 2u * G_DRREV) == 16 && out[8u + 2u * PJ_NG] == 1;
    check("globals: BPM; our MIDI channels and VIEW -> 0; SYNC AUTO -> INT; the lanes' REV -> 16; the selection", ok);
    for (k = 0, ok = 1; k < NTRK; k++) {
        for (i = 0; i < SL24_COMMON; i++)
            ok &= p24(out, k, i) == (int16_t)(k * 11u + i * 3u) - 20;
        ok &= p24(out, k, 50) == 0 && p24(out, k, 51) == 0 && p24(out, k, 52) == 0;
        ok &= !memcmp(T24(out, k) + 124, q.t[k].step, 640u) && (int8_t)T24(out, k)[SL24_TAIL + 5u] == -7 - (int)k;
        ok &= stepx_fill((const stepx_t *)(const void *)(T24(out, k) + SL24_TAIL), 6) == FC_FILL;
    }
    check("values 0..49 at 2.4's offsets, its 50..52 at 0 (TFLT STRUM VLEAD: off); steps, nudges, fills", ok);
    for (i = 0, ok = T24(out, 0)[122] == 0 && T24(out, 0)[123] == 4; i < 8u; i++)
        ok &= p24(out, 0, SL24_E0 + i) == (int16_t)(20 + i);
    check("ANALOG part: engine 0, preset, E0..E7 at 2.4's 53..60", ok);
    for (i = 0, ok = T24(out, 1)[122] == 9 && p24(out, 1, SL24_E0 + 7u) == 5 && T24(out, 1)[123] == 5; i < 7u; i++)
        ok &= p24(out, 1, SL24_E0 + i) == (i == 2u ? 2 : 0);
    check("FM6 part (no voice of its own): engine 9, VOICE R05 (FM MARIMBA) -> PTCH F6 (WOOD BARS), MOD 3 -> MLVL 2, "
          "the rest 0, preset F6", ok);
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
    ok = proj_from_sl24(&r, out, sizeof out, y, 0) && proj_ok(&r);
    check("... and back in (the importer): a valid project of ours", ok);
    for (k = 0; k < NTRK; k++) {
        pj_to_p(v, q.t[k].p), pj_to_p(w, r.t[k].p);
        for (i = 0; i < SL24_COMMON; i++)
            ok &= v[i] == w[i];
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
    ok = r.t[1].engine == ENG_UID_FM6 && w[P_E0] == 4 && w[P_E1] == 3 && w[P_E4] == 1 && ((r.fm6_has >> 1) & 1u) &&
         !memcmp(r.fm6[1], SL24_FM6_F[5], 128);
    check("FM6: back to VOICE R05 (F6's image), MOD 3; the voice 2.4's WOOD BARS (R05 itself: lost)", ok);
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
    const uint8_t *fv[NPART];
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
    for (k = 0; k < NPART; k++) {
        int pt = p24(out, k, SL24_E0 + 7u);
        fv[k] = pt >= 8 ? bank + 16 + 128 * (pt - 8) : 0;
    }
    ok = proj_from_sl24(&r, out, sizeof out, y, fv) && r.fm6_has == 7;
    for (k = 0; k < NPART; k++)
        ok &= !memcmp(r.fm6[k], q.fm6[k], 128);
    check("... and back (the bank's patches given, as the editor does): every voice as it was", ok);
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
    printf("sl24 export test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
