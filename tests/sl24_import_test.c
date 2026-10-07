/* SPDX-License-Identifier: GPL-3.0-only */
/* A SLOOP 2.4 project into Optimist (FELUCCA_SL24_IMPORT: sl24_import.c, sl24_guard.c sl24_load), from
 * tests/sl24_fun5.bin, a golden FUN5 written with SLOOP 2.4's own types (tests/sl24_fun5_gen.c). Checks each
 * translation (values, engines, FM6 PTCH, drum kits, globals, the step extras and their locks), then the flow on a
 * simulated NOR: started on 2.4's flash, LOAD once offers, LOAD again imports, the original untouched, SAVE keeps
 * the import (with FELUCCA_SL24_XSTEP: its extras too). Run by tests/run_tests.sh (with XSTEP 0 and 1). */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/project.c"

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
#include "../firmware/src/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/drum_store.c"

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
#include "../firmware/src/sections.c"   /* (and sl24_guard.c) */

static int bad;
static void check(const char *what, int ok)
{
    printf("%-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint8_t fun5[3840];

int main(void)
{
    static project_t q;
    static stepx_t x[NTRK];
    int16_t v[P_COUNT];
    uint32_t k, i, ok;
    FILE *f = fopen("tests/sl24_fun5.bin", "rb");
    if (!f || fread(fun5, 1, sizeof fun5, f) != sizeof fun5) {
        printf("tests/sl24_fun5.bin missing (run from the repo root)\n");
        return 1;
    }
    fclose(f);
    check("tests/sl24_fun5.bin is a SLOOP 2.4 project; ours does not read it as a project (import only when asked)",
          sl24_is(fun5, sizeof fun5) && !proj_import(&q, fun5, sizeof fun5));
    ok = proj_from_sl24(&q, fun5, sizeof fun5, x) && proj_ok(&q);
    check("imported: a valid project of ours", ok);
    /* values (the generator: track k, value i = k * 7 + i % 40; E i = 10 + i + k) */
    for (k = 0, ok = 1; k < NTRK; k++) {
        pj_to_p(v, q.t[k].p);
        for (i = 0; i < 50u; i++)
            ok &= v[i] == (int16_t)(k * 7u + i % 40u);
        ok &= v[P_FXOFF] == TP[P_FXOFF].def && v[P_A2WAVE] == TP[P_A2WAVE].def;
        if (k == 0 || k == 2)
            for (i = 0; i < 8u; i++)
                ok &= v[P_E0 + i] == (int16_t)(10u + i + k);
    }
    check("values 0..49 as stored; TFLT / STRUM / VLEAD not read into ours (FX OFF, ANALOG 2 at defaults); E0..E7", ok);
    ok = q.t[0].engine == 0 && q.t[0].preset == 3 && q.t[1].engine == ENG_UID_FM6 && q.t[2].engine == 6 &&
         q.t[3].engine == 0;
    check("engines: 2.4's numbers are our UIDs (ANALOG, FM6 9, TRIO)", ok);
    pj_to_p(v, q.t[1].p);
    check("FM6 part: PTCH F3 (ROUND BASS) -> VOICE R03 (SOLID BASS), MOD.. 0, ENGINE MARK I; its macros dropped",
          v[P_E0] == 2 && v[P_E1] == 0 && v[P_E2] == 0 && v[P_E3] == 0 && v[P_E4] == 1 && !q.fm6_has);
    pj_to_p(v, q.t[3].p);
    check("drum track: kit 7 (606) stays 7", v[P_E0] == 7);
    ok = q.g[G_VIEW] == GP[G_VIEW].def && q.g[0] == 1 && q.g[PJ_NG - 1] == (int16_t)PJ_NG && q.sel == 2;
    check("globals as stored but their G_ROUTE (our G_VIEW): the default; the selected track", ok);
    ok = !memcmp(q.t[2].step, fun5 + 12 + 64 + 2 * 940 + 124, sizeof q.t[2].step);
    check("steps: byte for byte (2.4's 10-byte steps are ours)", ok);
    for (k = 0, ok = 1; k < NTRK; k++)
        ok &= x[k].micro[3] == (int8_t)(-5 - (int)k) && x[k].micro[10] == MICRO_MAX && stepx_fill(&x[k], 1) == FC_FILL &&
              stepx_fill(&x[k], 2) == FC_NOFILL && stepx_fill(&x[k], 0) == FC_NORM;
    check("step extras: nudges and fills as stored", ok);
    ok = 1;
    for (k = 0; k < NTRK; k++) {
        int pan = stepx_lock_find(&x[k], 2, P_PAN), lvl = stepx_lock_find(&x[k], 6, P_LEVEL);
        int e1 = stepx_lock_find(&x[k], 5, P_E0 + 1);
        ok &= pan >= 0 && x[k].lock[pan].val == -30 + (int)k && lvl >= 0 && x[k].lock[lvl].val == 100;
        ok &= k == 1 ? e1 < 0 : e1 >= 0 && x[k].lock[e1].val == 7;    /* (FM6's EDIT locks: dropped) */
        for (i = 0; i < NLOCK; i++)
            ok &= !stepx_lock_used(&x[k].lock[i]) || x[k].lock[i].param < P_COUNT;
        ok &= stepx_lock_find(&x[k], 4, 50) < 0;                     /* (TFLT: dropped, not our P_FXOFF) */
    }
    {
        int a = stepx_lock_find(&x[3], 8, P_E0), b = stepx_lock_find(&x[3], 9, P_E0);
        ok &= a >= 0 && x[3].lock[a].val == 12 && b >= 0 && x[3].lock[b].val == (int16_t)DRUM_DEFAULT_KIT;
    }
    check("locks: our ids (E1 -> P_E1), TFLT and FM6's macros dropped, the drum kit's via the kit table (USR2 -> default)", ok);
    /* the flow on flash: started on 2.4's (slot B), LOAD twice */
    {
        static uint8_t keep[4096];
        static step_t want2[NSTEP];
        static dlrec_t d0;
        host_tracks_init();
        proj_apply(&q, &d0, 1);                         /* (what applying the import plays: steps as proj_apply keeps them) */
        memcpy(want2, trk[2].step, sizeof want2);
        st_hdr_t h;
        memset(nor, 0xFF, sizeof nor);
        (void)st_save(OBJ_PROJECT0 + 1, fun5, sizeof fun5);   /* (2.4's storage writes the same object) */
        st_current(OBJ_PROJECT0 + 1, &h);
        memcpy(keep, nor + st_sector(OBJ_PROJECT0 + 1, 0), sizeof keep);
        sec_pend_clear();
#if FELUCCA_SL24_XSTEP
        sx_init();
#endif
        sl24_boot_scan();
        sec_boot();
        host_tracks_init();
        song.playing = 0, transport_req = 0;
        project_load(1);
        ok = project_state(1) == PJ_SL24 && !strcmp(last_msg, "SLOOP 2.4: LOAD = IMPORT");
        fm1_ms += 500;
        project_load(1);
        ok &= !strncmp(last_msg, "2.4 IMPORTED", 12) && !memcmp(trk[2].step, want2, sizeof want2) &&
              trk[1].p[P_E0] == 2;
        check("flash: B shows SLOOP 2.4; LOAD offers, LOAD again (4 s) imports it as the working project", ok);
        check("... 2.4's original untouched", !memcmp(keep, nor + st_sector(OBJ_PROJECT0 + 1, 0), sizeof keep));
#if FELUCCA_SL24_XSTEP
        for (k = 0, ok = !strcmp(last_msg, "2.4 IMPORTED: SAVE IT"); k < NTRK; k++)
            ok &= !memcmp(STEPX(k), &x[k], sizeof x[k]);
        check("... its step extras are the working ones (XSTEP)", ok);
        project_save(5);
        for (k = 0; k < NTRK; k++)
            stepx_clear(STEPX(k));
        project_load(5);
        for (k = 0, ok = !strcmp(last_msg, "LOADED"); k < NTRK; k++)
            ok &= !memcmp(STEPX(k), &x[k], sizeof x[k]);
        check("... SAVE into F, LOAD F: the import and its extras", ok);
#else
        check("... without XSTEP its extras are dropped, and it says so", !strcmp(last_msg, "2.4 IMPORTED, NO LOCKS"));
#endif
        fm1_ms += 5000;
        project_load(1);
        check("LOAD after 4 s: offered again, not imported", !strcmp(last_msg, "SLOOP 2.4: LOAD = IMPORT"));
    }
    printf("sl24 import test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
