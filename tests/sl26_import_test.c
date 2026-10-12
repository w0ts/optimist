/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.5 and 2.6 projects into Optimist (FELUCCA_SL24_IMPORT: sl24_import.c, sl24_guard.c), from golden files
 * written with their own types (tests/sl25_gen.c): tests/sl25_fun5.bin (2.5: FUN5, its engine numbers PHYS 10,
 * NOISE 11, a SYN kit) and tests/sl26_fun6.bin (2.6: FUN6, 3816 B). Checks:
 *   - 2.5: a PHYS part imports as our PHYS with its EDIT values and locks; NOISE (not ours) as ANALOG at its EDIT
 *     defaults, its EDIT locks dropped; the SYN kit as our default kit;
 *   - 2.6: told apart (not ours, not 2.4's FUN5), shown as SLOOP's under an empty slot, LOAD twice imports it: values,
 *     engines (PHYS), steps, nudges, fills, locks (P_E0.. moved back, 2.6's own dropped), the ANALOG filter TYPE, the
 *     TEXTURE's globals at our defaults; its original untouched;
 *   - 2.5 / 2.6's drums' delay send (G_DRDLY, byte 73; 77 in both goldens): every drum lane's DLY at the nearest level
 *     (19: a send of 78), REV and CHO as before; 0 (2.4's spare): the lanes as they were;
 *   - the export (SL24_EXPORT, tests/sl24_export_test.c has the rest): our PHYS as 2.5's 10, SLICE as its fallback; the
 *     drum lanes' DLY as 2.5's drums' delay send (the level most lanes have, a tie the lower; 0 with DLY in no slot).
 * With -DSL25_OLD only the checks the firmware before had the functions for are built (the RED run).
 * Run by tests/run_tests.sh (XSTEP / TRK_FILT / CHORDPLUS 0 and 1). */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#ifndef FELUCCA_SL24_EXPORT
#define FELUCCA_SL24_EXPORT 1
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

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
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/storage/sections/sections.c"   /* (and sl24_guard.c) */

static int bad;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint8_t f25[3840], f26[3816];
static int load(const char *path, uint8_t *b, uint32_t n)
{
    FILE *f = fopen(path, "rb");
    int ok = f && fread(b, 1, n, f) == n && fgetc(f) == EOF;
    if (f)
        fclose(f);
    return ok;
}
#define T26(k) (f26 + 76u + (k) * 934u)                 /* FUN6's track k */

static void sl25_project(void)
{
    static project_t q;
    static stepx_t x[NTRK];
    int16_t v[P_COUNT];
    uint32_t i, ok;
    int e1;
    check("2.5's FUN5 reads as 2.4's (the same format); ours does not read it as a project",
          sl24_is(f25, sizeof f25) && !proj_import(&q, f25, sizeof f25));
    ok = proj_from_sl24(&q, f25, sizeof f25, x, 0) && proj_ok(&q);
    check("2.5: imported, a valid project of ours", ok);
    pj_to_p(v, q.t[0].p);
    ok = q.t[0].engine == ENG_UID_PHYS && q.t[0].preset == 4 && v[P_E0] == 2;
    for (i = 1; i < 8u; i++)
        ok &= v[P_E0 + i] == (int16_t)(10u + i);
    e1 = stepx_lock_find(&x[0], 5, P_E1);
    check("2.5's PHYS (10) -> our PHYS: preset THUMB PNO (4), MODEL SYMP, its EDIT values", ok);
    check("... its EDIT lock (E1) kept", e1 >= 0 && x[0].lock[e1].val == 7);
    pj_to_p(v, q.t[1].p);
    ok = q.t[1].engine == 0 && q.t[1].preset == 0 && stepx_lock_find(&x[1], 5, P_E1) < 0 &&
         stepx_lock_find(&x[1], 2, P_PAN) >= 0;
    for (i = 0; i < 8u; i++)
        ok &= v[P_E0 + i] == ENGINES[eng_slot_built(0)]->edit[i].def;
    check("2.5's NOISE (11, not ours) -> ANALOG at its EDIT defaults, preset 0; its EDIT lock dropped, PAN kept", ok);
    pj_to_p(v, q.t[3].p);
    check("2.5's SYN2 kit (43) -> our default kit", v[P_E0] == (int16_t)DRUM_DEFAULT_KIT && q.t[2].engine == 6);
}

#ifndef SL25_OLD
static void sl26_convert(void)
{
    static uint8_t b[3840];
    static project_t q;
    static stepx_t x[NTRK];
    int16_t v[P_COUNT];
    uint32_t k, i, ok, ft;
    memcpy(b, f26, sizeof f26);
    ft = sl26_to_sl24(b);
    check("2.6 -> 2.5 in place: a FUN5 (sl24_is); the parts' filter TYPE: track 1 ANALOG HP (TRIO's BP not heard)",
          sl24_is(b, 3840) && ft == 2u);
    for (k = 0, ok = 1; k < NTRK; k++) {
        const uint8_t *s = T26(k), *d = b + 76u + k * 940u;
        ok &= !memcmp(d, s, 122) && d[122] == s[132] && d[123] == s[133] && !memcmp(d + 124, s + 134, 640);
    }
    ok &= !memcmp(b + 8, f26 + 8, 2u * 15u) && b[72] == f26[72] && b[73] == 77 && b[74] == 0 && b[75] == 0;
    check("... values, E0..E7, engine, preset, steps at FUN5's places; sel, drdly; the spare bytes 0", ok);
    ok = (int16_t)(b[8 + 2 * G_DRCH] | b[9 + 2 * G_DRCH] << 8) == GP[G_DRCH].def &&
         (int16_t)(b[8 + 2 * G_INFO] | b[9 + 2 * G_INFO] << 8) == GP[G_INFO].def;
    check("... GLO > TEXTURE's two globals (G_DRCH, G_INFO slots): our defaults", ok);
    ok = proj_from_sl24(&q, b, 3840, x, 0) && proj_ok(&q);
    check("2.6: imported (through 2.5's), a valid project of ours", ok);
    for (k = 0; k < NTRK; k++) {
        pj_to_p(v, q.t[k].p);
        for (i = 0; i < 50u; i++)
            ok &= (SL24_TP && k == TRK_DRUM && (i == P_GLMODE || i == P_PRIO || i == P_ALLOC || i == P_DETUNE)) ||
                  v[i] == (int16_t)(k * 7u + i % 40u);
        if (k < NPART)
            for (i = 0; i < 8u; i++)
                ok &= v[P_E0 + i] == (int16_t)(10u + i + k);
    }
    check("... values 0..49 and E0..E7 as stored", ok);
    pj_to_p(v, q.t[3].p);
    ok = q.t[0].engine == 0 && q.t[1].engine == ENG_UID_PHYS && q.t[2].engine == 6 && v[P_E0] == 7;
    check("... engines: ANALOG, PHYS (10), TRIO; the drum kit 7 (606)", ok);
    for (k = 0, ok = 1; k < NTRK; k++) {
        int pan = stepx_lock_find(&x[k], 2, P_PAN), lvl = stepx_lock_find(&x[k], 6, P_LEVEL);
        int e1 = stepx_lock_find(&x[k], 5, P_E1);
        ok &= x[k].micro[3] == (int8_t)(-5 - (int)k) && x[k].micro[10] == MICRO_MAX && x[k].micro[12] == 2 &&
              stepx_fill(&x[k], 1) == FC_FILL && stepx_fill(&x[k], 2) == FC_NOFILL && stepx_fill(&x[k], 12) == FC_NORM;
        ok &= pan >= 0 && x[k].lock[pan].val == -30 + (int)k && lvl >= 0 && x[k].lock[lvl].val == 100;
        ok &= e1 >= 0 && x[k].lock[e1].val == 7;
        for (i = 0; i < NLOCK; i++)                     /* (2.6's INSERT and LFO 2 SYNC locks, steps 7 and 8: gone) */
            ok &= !stepx_lock_used(&x[k].lock[i]) || (x[k].lock[i].step != 7u && x[k].lock[i].step != 8u);
    }
    check("... nudges (6 bits, signed), fills (3: none), locks (P_E0.. back to its id; INSERT, LFO 2: dropped)", ok);
}
#endif

/* every working drum lane's DLY at level v, its CHO 0 (drum_sends.c dsend[]) */
static int lanes_dly(uint32_t v)
{
    uint32_t l;
    int ok = 1;
    for (l = 0; l < DRUM_LANES; l++)
        ok &= dsend_dly(dsend[l]) == v && dsend_cho(dsend[l]) == 0u;
    return ok;
}

static void sl26_flow(void)
{
    static uint8_t keep[4096], b[3840];
    uint32_t ok;
    memcpy(b, f26, sizeof f26);
    check("2.6's FUN6: ours does not read it as a project (our test-build FUN6s have other sizes)",
          !proj_import(&proj_tmp.cur, b, sizeof f26));
    memset(nor, 0xFF, sizeof nor);
    (void)st_save(OBJ_PROJECT0 + 1, f26, sizeof f26);   /* (2.6's storage writes the same object) */
    memcpy(keep, nor + st_sector(OBJ_PROJECT0 + 1, 0), sizeof keep);
    sec_pend_clear();
#if FELUCCA_SL24_XSTEP
    auto_init();
#endif
    sl24_boot_scan();
    sec_boot();
    host_tracks_init();
    song.playing = 0, transport_req = 0;
    project_load(1);
    ok = project_state(1) == PJ_SL24 && !strcmp(last_msg, "SLOOP: LOAD = IMPORT");
    check("flash: a 2.6 slot shows as SLOOP's (never EMPTY); LOAD offers", ok);
    fm1_ms += 500;
    project_load(1);
    ok = !strncmp(last_msg, "2.4 IMPORTED", 12) && proj_tmp.cur.t[1].engine == ENG_UID_PHYS && proj_tmp.cur.t[2].engine == 6 &&
         trk[2].eng_req == eng_slot(6);           /* (the project's UID: PHYS is not in every build, its slot then ANALOG's) */
#if FELUCCA_ANALOG2
    ok &= trk[0].p[P_A2FTYP] == 3 && trk[2].p[P_A2FTYP] == TP[P_A2FTYP].def;
#endif
    check("... LOAD again imports it: PHYS, TRIO; ANALOG's filter TYPE HP -> ANALOG 2's HP", ok);
    check("... 2.6's original untouched", !memcmp(keep, nor + st_sector(OBJ_PROJECT0 + 1, 0), sizeof keep));
    check("... its drums' delay send (77): every lane's DLY level 19 (78), no chorus", lanes_dly(19));
    memcpy(b, f25, sizeof f25);
    ok = sl24_import_buf(b, sizeof f25, 0) && lanes_dly(19);
    check("2.5's drums' delay send (77) imported: every lane's DLY level 19", ok);
    memcpy(b, f25, sizeof f25);
    b[73] = 0;                                    /* (2.4's: its spare byte, 0) */
    {
        uint32_t s = proj_hash(b, 3836u);
        memcpy(b + 3836, &s, 4);
    }
    ok = sl24_import_buf(b, sizeof f25, 0) && lanes_dly(0);
    check("... a drums' delay send of 0 (2.4's spare byte): the lanes' DLY 0, as before", ok);
}

#if FELUCCA_SL24_EXPORT
static void export_engines(void)
{
    static project_t q, r;
    static uint8_t out[3840];
    uint32_t lost;
    memcpy(out, f25, sizeof out);
    (void)proj_from_sl24(&q, out, sizeof out, 0, 0);
    q.t[0].engine = ENG_UID_PHYS;
    q.t[1].engine = ENG_UID_SLICE;
    q.sum = proj_sum(&q);
    lost = proj_to_sl24(&q, 0, out, 0);
    check("export: our PHYS -> SLOOP 2.5's 10 with its EDIT values (2.5 and 2.6 play it)",
          out[76 + 122] == 10 && (int16_t)(out[76 + 2 * 54] | out[76 + 2 * 54 + 1] << 8) == 11);
    check("... SLICE (never in a shipped SLOOP) -> its fallback SAMPLE (4) at SAMPLE's defaults, said lost",
          out[76 + 940 + 122] == 4 && (lost & SX24_ENGINE) &&
          (int16_t)(out[76 + 940 + 2 * 53] | out[76 + 940 + 2 * 53 + 1] << 8) == ENGINES[eng_slot_built(4)]->edit[0].def);
    check("... and back: 10 imports as our PHYS", proj_from_sl24(&r, out, sizeof out, 0, 0) && r.t[0].engine == ENG_UID_PHYS);
    {   /* the drum lanes' DLY -> 2.5's drums' delay send (byte 73) */
        static uint16_t keep[DRUM_LANES];
        uint8_t live = fxs_live;
        uint32_t l, ok;
        memcpy(keep, dsend, sizeof keep);
        fxs_live |= (uint8_t)(1u << FXT_DLY);
        for (l = 0; l < DRUM_LANES; l++)
            dsend[l] = dsend_word(l % 3u + 3u, 19, l % 2u);
        (void)proj_to_sl24(&q, 0, out, 0);
        ok = out[73] == 78 && sl24_is(out, sizeof out);
        for (l = 0; l < DRUM_LANES; l++)
            dsend[l] = dsend_word(4, l < 10u ? 0u : 31u, 0);
        (void)proj_to_sl24(&q, 0, out, 0);
        ok &= out[73] == 0;
        for (l = 0; l < DRUM_LANES; l++)
            dsend[l] = dsend_word(4, l < 8u ? 7u : 5u, 0);
        (void)proj_to_sl24(&q, 0, out, 0);
        ok &= out[73] == 20;
        check("export: 2.5's drums' delay send = the lanes' DLY most have (19 -> 78; 10 x 0 + 6 x 31 -> 0; 8 + 8: the lower)", ok);
        for (l = 0; l < DRUM_LANES; l++)
            dsend[l] = dsend_word(4, 19, 0);
        fxs_live &= (uint8_t)~(1u << FXT_DLY);
        (void)proj_to_sl24(&q, 0, out, 0);
        check("... 0 while DLY is in no FX slot (the lanes send nothing here)", out[73] == 0);
        fxs_live = live;
        memcpy(dsend, keep, sizeof keep);
    }
}
#endif

int main(void)
{
    if (!load("tests/sl25_fun5.bin", f25, sizeof f25) || !load("tests/sl26_fun6.bin", f26, sizeof f26)) {
        printf("tests/sl25_fun5.bin, sl26_fun6.bin missing (run from the repo root)\n");
        return 1;
    }
    sl25_project();
#ifndef SL25_OLD
    check("2.6's FUN6 told apart: SLOOP's (sl26_is), not 2.4's FUN5", sl26_is(f26, sizeof f26) && !sl24_is(f26, sizeof f26));
    sl26_convert();
#endif
    sl26_flow();
#if FELUCCA_SL24_EXPORT
    export_engines();
#endif
    printf("%s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
