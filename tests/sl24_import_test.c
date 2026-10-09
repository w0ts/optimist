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
#if FELUCCA_SL24_XSTEP
static const stepx_t *sx_work(uint32_t k)              /* the working track k's step-only events in 2.4's form */
{
    static stepx_t x;
    (void)auto_to_stepx(&x, AUTO_L(k));
    return &x;
}
#endif
static void check(const char *what, int ok)
{
    printf("%-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint8_t fun5[3840];

/* got = base (a packed DX7 voice) with 2.4's macros as its eng_fm6.c fm6_sync puts them, checked by the DX7 chart:
 * algorithm alg (0-based; 4 = DX7's 5: carriers OP1, OP3, OP5), feedback + fb, and on the modulators coarse + mrat
 * (clamped 0..31), rates - meg (0..99), velocity sensitivity + vmod (0..7); carriers and everything else as base */
static int fm6_baked(const uint8_t *got, const uint8_t *base, int alg, int fb, int mrat, int meg, int vmod)
{
    uint8_t w[128];
    int n, i;
    memcpy(w, base, sizeof w);
    w[110] = (uint8_t)alg;
    w[111] = (uint8_t)((w[111] & 8) | ((w[111] & 7) + fb > 7 ? 7 : (w[111] & 7) + fb));
    for (n = 1; n <= 6; n++) {
        uint8_t *s = w + 17 * (6 - n);
        int c, r;
        if (alg == 4 && (n == 1 || n == 3 || n == 5))
            continue;
        c = ((s[15] >> 1) & 31) + mrat;
        if (!(s[15] & 1))
            s[15] = (uint8_t)((s[15] & 1) | (c < 0 ? 0 : c > 31 ? 31 : c) << 1);
        for (i = 0; i < 4; i++)
            r = s[i] - meg, s[i] = (uint8_t)(r < 0 ? 0 : r > 99 ? 99 : r);
        c = ((s[13] >> 2) & 7) + vmod;
        s[13] = (uint8_t)((s[13] & 3) | (c < 0 ? 0 : c > 7 ? 7 : c) << 2);
    }
    return !memcmp(got, w, sizeof w);
}
/* 2.4's FM6 bank (object 8, "FM6B") as 2.4's storage writes it at 0xE5000 + copy: slot k holds rec (0: none) */
static void put_bank24(uint32_t copy, uint32_t seq, uint32_t k, const uint8_t *rec)
{
    static uint8_t b[SL24_BANK_LEN];
    uint32_t w[4] = {SL24_BANK_MAGIC, 1u | SL24_BANK_N << 16, rec ? 1u << k : 0u, 0}, off = 0xE5000u + copy * 4096u;
    st_hdr_t h;
    memset(b, 0, sizeof b);
    memcpy(b, w, sizeof w);
    if (rec)
        memcpy(b + 16 + 128 * k, rec, 128);
    memset(nor + off, 0xFF, 4096);
    memcpy(nor + off + 256, b, sizeof b);
    memset(&h, 0, sizeof h);
    h.magic = ST_MAGIC, h.type = 8, h.slot = (uint16_t)copy, h.seq = seq, h.len = sizeof b, h.crc = st_crc32(b, sizeof b);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(nor + off, &h, sizeof h);
}
/* fun5 with track k an FM6 part on PTCH pt (E0..E6: e), the drum kit kit -> b, its sum made again */
static void fun5_with(uint8_t *b, uint32_t k, int pt, const int16_t *e, int kit)
{
    uint8_t *t = b + 12 + 64 + k * 940;
    uint32_t i, s;
    memcpy(b, fun5, 3840);
    t[122] = 9;
    for (i = 0; i < 7; i++)
        t[2 * (53 + i)] = (uint8_t)e[i], t[2 * (53 + i) + 1] = (uint8_t)((uint16_t)e[i] >> 8);
    t[2 * 60] = (uint8_t)pt, t[2 * 60 + 1] = 0;
    t = b + 12 + 64 + 3 * 940;
    t[2 * 53] = (uint8_t)kit, t[2 * 53 + 1] = 0;
    s = proj_hash(b, 3836);
    memcpy(b + 3836, &s, 4);
}

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
    ok = proj_from_sl24(&q, fun5, sizeof fun5, x, 0) && proj_ok(&q);
    check("imported: a valid project of ours", ok);
    /* values (the generator: track k, value i = k * 7 + i % 40; E i = 10 + i + k) */
    for (k = 0, ok = 1; k < NTRK; k++) {
        pj_to_p(v, q.t[k].p);
        for (i = 0; i < 50u; i++)
            ok &= (SL24_TP && k == TRK_DRUM && (i == P_GLMODE || i == P_PRIO || i == P_ALLOC || i == P_DETUNE)) ||
                  v[i] == (int16_t)(k * 7u + i % 40u);   /* (the drum track's four hold TFLT .. : px_pack) */
        ok &= v[P_FXOFF] == TP[P_FXOFF].def && v[P_A2WAVE] == TP[P_A2WAVE].def;
        if (k == 0 || k == 2)
            for (i = 0; i < 8u; i++)
                ok &= v[P_E0 + i] == (int16_t)(10u + i + k);
    }
    check("values 0..49 as stored; (FX OFF, ANALOG 2 at defaults); E0..E7", ok);
#if SL24_TP
    {   /* the generator: every track's TFLT -20, STRUM 15, VLEAD 1; the drum track has no STRUM / VLEAD */
        int16_t px[NTRK][3];
        px_unpack(&q, px);
        for (k = 0, ok = 1; k < NTRK; k++) {
            ok &= px[k][0] == (FELUCCA_TRK_FILT ? -20 : 0);
            ok &= px[k][1] == (FELUCCA_CHORDPLUS && k != TRK_DRUM ? 15 : 0);
            ok &= px[k][2] == (FELUCCA_CHORDPLUS && k != TRK_DRUM ? 1 : 0);
        }
        check("TFLT / STRUM / VLEAD: FILT with TRK_FILT, the parts' STRUM and VLEAD with CHORDPLUS, else 0", ok);
    }
#else
    check("TFLT / STRUM / VLEAD: not ours in this build (dropped)", 1);
#endif
    ok = q.t[0].engine == 0 && q.t[0].preset == 3 && q.t[1].engine == ENG_UID_FM6 && q.t[2].engine == 6 &&
         q.t[3].engine == 0;
    check("engines: 2.4's numbers are our UIDs (ANALOG, FM6 9, TRIO)", ok);
    pj_to_p(v, q.t[1].p);
    check("FM6 part: PTCH F3 (ROUND BASS) -> VOICE R03 (SOLID BASS, a name), MLVL 10 -> MOD 15, M.TIM C.TIM 0, MARK I",
          v[P_E0] == 2 && v[P_E1] == 15 && v[P_E2] == 0 && v[P_E3] == 0 && v[P_E4] == 1);
    check("... its voice: 2.4's ROUND BASS itself (the part's own, all six operators on); no other part has one",
          q.fm6_has == 2u && q.fm6_on[1] == 0x3Fu && !memcmp(q.fm6[1] + 118, "ROUND BASS", 10));
    check("... with 2.4's macros in it as 2.4 plays them (ALG 5, FB +2, MRAT -3 MEG +4 VMOD +1 on the modulators)",
          fm6_baked(q.fm6[1], SL24_FM6_F[2], 4, 2, -3, 2, 1));
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
#if FELUCCA_TRK_FILT
        {   /* (TFLT lock, val 50, step 4: ours P_TFLT, not our P_FXOFF) */
            int tl = stepx_lock_find(&x[k], 4, P_TFLT);
            ok &= tl >= 0 && x[k].lock[tl].val == 50 && stepx_lock_find(&x[k], 4, P_FXOFF) < 0;
        }
#else
        ok &= stepx_lock_find(&x[k], 4, 50) < 0;                     /* (TFLT: dropped, not our P_FXOFF) */
#endif
    }
    {
        int a = stepx_lock_find(&x[3], 8, P_E0), b = stepx_lock_find(&x[3], 9, P_E0);
        ok &= a >= 0 && x[3].lock[a].val == 12 && b >= 0 && x[3].lock[b].val == (int16_t)DRUM_DEFAULT_KIT;
    }
    check("locks: our ids (E1 -> P_E1), TFLT (TRK_FILT) and FM6's macros, the drum kit's via the kit table (USR2 -> default)", ok);
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
        auto_init();
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
#if FELUCCA_TRK_FILT
        check("... its FILT in use (-20): the FILTER takes an FX slot, heard (fx_slots.c fxs_auto: no FX record in 2.4's)",
              FXS_ON(FXT_FILT) && trk[0].p[P_TFLT] == -20 && fxs_live == (FXS_LIVE_DEF & ~(uint32_t)FXT_BIT(fxs_slot[0] == FXT_FILT ? FXT_DIST :
              fxs_slot[1] == FXT_FILT ? FXT_CHO : fxs_slot[2] == FXT_FILT ? FXT_DLY : FXT_REV) | FXT_BIT(FXT_FILT)));
#else
        check("... without TRK_FILT: the default FX slots", !memcmp(fxs_slot, FXS_DEF, FX_NSLOT));
#endif
        check("... 2.4's original untouched", !memcmp(keep, nor + st_sector(OBJ_PROJECT0 + 1, 0), sizeof keep));
#if FELUCCA_SL24_XSTEP
        for (k = 0, ok = !strcmp(last_msg, "2.4 IMPORTED: SAVE IT"); k < NTRK; k++)
            ok &= !memcmp(sx_work(k), &x[k], sizeof x[k]);
        check("... its step extras are the working ones (XSTEP)", ok);
        project_save(5);
        for (k = 0; k < NTRK; k++)
            AUTO_L(k)->n = 0;
        project_load(5);
        for (k = 0, ok = !strcmp(last_msg, "LOADED"); k < NTRK; k++)
            ok &= !memcmp(sx_work(k), &x[k], sizeof x[k]);
        check("... SAVE into F, LOAD F: the import and its extras", ok);
#else
        check("... without XSTEP its extras are dropped, and it says so", !strcmp(last_msg, "2.4 IMPORTED, NO LOCKS"));
#endif
        fm1_ms += 5000;
        project_load(1);
        check("LOAD after 4 s: offered again, not imported", !strcmp(last_msg, "SLOOP 2.4: LOAD = IMPORT"));
    }
    /* SLOOP 2.4's AUTOSAVE (PROJECT > A24, twice): found at start, kept after Optimist's own autosaves, imported */
    {
        static uint8_t keep[4096];
        static step_t want2[NSTEP];
        static dlrec_t d0;
        int16_t *vp = 0;
        const page_t *pg = 0;
        st_hdr_t ah;
        host_tracks_init();
        proj_apply(&q, &d0, 1);
        memcpy(want2, trk[2].step, sizeof want2);
        memset(nor, 0xFF, sizeof nor);
        (void)st_save(OBJ_AUTOSAVE, fun5, sizeof fun5);
        memcpy(keep, nor + st_sector(OBJ_AUTOSAVE, 0), sizeof keep);
        sec_pend_clear();
        sl24_boot_scan();
        check("autosave: a SLOOP 2.4 one is found at start (and shown as 2.4's)", sl24_find(OBJ_AUTOSAVE, &ah) >= 0 && pj_alien[4] == PJ_SL24);
        (void)st_save(OBJ_AUTOSAVE, &q, sizeof q);              /* (Optimist's autosave: the other copy) */
        (void)st_save(OBJ_AUTOSAVE, &q, sizeof q);              /* (and again: in place, never over 2.4's) */
        (void)st_save(OBJ_AUTOSAVE, &q, sizeof q);
        check("... Optimist's autosaves leave 2.4's copy as it was", !memcmp(keep, nor + st_sector(OBJ_AUTOSAVE, 0), sizeof keep));
        sl24_boot_scan();                                       /* (the next start: ours is the current copy) */
        check("... next start: still offered, ours is the working one", sl24_find(OBJ_AUTOSAVE, &ah) >= 0 && pj_alien[4] == 0);
        (void)st_save(OBJ_AUTOSAVE, &q, sizeof q);
        check("... and still kept", !memcmp(keep, nor + st_sector(OBJ_AUTOSAVE, 0), sizeof keep));
        for (i = 0; i < NPAGES && !(PAGES[i].scope == SC_GLOBAL && PAGES[i].id[1] == G_A24); i++)
            ;
        pg = i < NPAGES ? &PAGES[i] : 0;
        ok = pg && !strcmp(pg->title, "PROJECT") && page_desc(pg, 1, &vp) && !strcmp(page_desc(pg, 1, &vp)->label, "A24") && vp;
        check("... PROJECT page has the A24 GO button (no stored value)", ok);
        host_tracks_init();
        song.playing = 0, transport_req = 0;
        sl24_auto_import();
        ok = !strncmp(last_msg, "2.4 IMPORTED", 12) && !memcmp(trk[2].step, want2, sizeof want2) && trk[1].p[P_E0] == 2;
        check("... A24 imports it as the working project", ok);
#if FELUCCA_SL24_XSTEP
        for (k = 0, ok = 1; k < NTRK; k++)
            ok &= !memcmp(sx_work(k), &x[k], sizeof x[k]);
        check("... its step extras too (XSTEP)", ok);
#endif
        song.playing = 1;
        sl24_auto_import();
        song.playing = 0;
        check("... not while playing", !strcmp(last_msg, "STOP BEFORE LOAD"));
        memset(nor, 0xFF, sizeof nor);
        sl24_boot_scan();
        sl24_auto_import();
        check("no 2.4 autosave: A24 says EMPTY SLOT", sl24_find(OBJ_AUTOSAVE, &ah) < 0 && !strcmp(last_msg, "EMPTY SLOT"));
    }
    /* FM6 bank patches (PTCH B1..B27): from 2.4's bank where 2.4 left it (0xE5000 / 0xE6000), or the editor's file */
    {
        static uint8_t b24[3840], rec[128], keep[8192];
        static const int16_t E0[7] = {0, 0, 0, 0, 0, 0, 0}, E1[7] = {0, 9, -6, 2, -64, -9, 30};
        memcpy(rec, SL24_FM6_F[6], 128);                         /* (a patch of its own: DRAWBARS renamed) */
        memcpy(rec + 118, "MY ORGAN  ", 10);
        rec[110] = 4;                                            /* (ALG 5) */
        memset(nor, 0xFF, sizeof nor);
        put_bank24(0, 3, 4, SL24_FM6_F[1]);                      /* (an older copy: B5 GLASS BELL) */
        put_bank24(1, 4, 4, rec);                                /* (the current: B5 its own) */
        memcpy(keep, nor + 0xE5000, sizeof keep);
        fun5_with(b24, 0, 8 + 4, E0, 7);
        host_tracks_init();
        song.playing = 0, transport_req = 0;
        check("bank: the current copy of 2.4's bank found (the newer of A / B, CRC and layout checked)",
              sl24_bank_at() == 0xE6000u + 256u);
        ok = sl24_import_buf(b24, sizeof b24, 0) && ((proj_tmp.cur.fm6_has & 1u) != 0) && !memcmp(proj_tmp.cur.fm6[0], rec, 128);
        check("... PTCH B5: the part's voice is B5's patch as the bank has it (macros 0: as it is)", ok);
        check("... read only: 2.4's bank as it was", !memcmp(keep, nor + 0xE5000, sizeof keep));
        fun5_with(b24, 0, 8 + 4, E1, 7);
        ok = sl24_import_buf(b24, sizeof b24, 0) && fm6_baked(proj_tmp.cur.fm6[0], rec, 4, 9, 2, -40, -9);
        pj_to_p(v, proj_tmp.cur.t[0].p);
        check("... with its macros put in (FB clamped at 7, MEG -64 = 40 rate steps faster), MLVL -6 -> MOD -9", ok && v[P_E1] == -9);
        fun5_with(b24, 0, 8 + 5, E0, 7);
        ok = sl24_import_buf(b24, sizeof b24, 0) && !memcmp(proj_tmp.cur.fm6[0], SL24_FM6_INIT, 128);
        check("... an empty slot (B6): 2.4's INIT, as 2.4 plays it", ok);
        {
            const uint8_t *fv[NPART] = {SL24_FM6_F[3], 0, 0};
            fun5_with(b24, 0, 8 + 4, E0, 7);
            ok = sl24_import_buf(b24, sizeof b24, fv) && !memcmp(proj_tmp.cur.fm6[0], SL24_FM6_F[3], 128);
            check("... the editor's file gives the patch: that one, not the flash's", ok);
        }
        memset(nor + 0xE6000, 0xFF, 4096);                       /* (copy B gone: A, the older, is current) */
        memset(nor + 0xE5000 + 800, 0, 4);                       /* (and A damaged: its CRC) */
        fun5_with(b24, 0, 8 + 4, E0, 7);
        ok = sl24_bank_at() == 0 && sl24_import_buf(b24, sizeof b24, 0) && !memcmp(proj_tmp.cur.fm6[0], SL24_FM6_INIT, 128);
        check("... no valid bank: INIT", ok);
#if FELUCCA_DRUM_USR && FELUCCA_ANALOG2
        /* 2.4's USR kits: each lane the zone of that user slot holding its GM note */
        usr_nz[1] = 2;
        usr_zone[1][0].lo = 36, usr_zone[1][0].hi = 36;          /* (lane 0: BD 36) */
        usr_zone[1][1].lo = 38, usr_zone[1][1].hi = 42;          /* (lanes SD 38, 39 CP, 42 CH, 40) */
        fun5_with(b24, 0, 0, E0, 37 + 1);                        /* (USR2) */
        ok = sl24_import_buf(b24, sizeof b24, 0) && proj_tmp.cur.dl_hash != 0;
        ok &= dl.src[0] == DL_USR + 1 && dl_hit(dl.ref[0]) == 0 && dl.src[2] == DL_USR + 1 && dl_hit(dl.ref[2]) == 1 &&
              dl.src[4] == DL_USR + 1 && dl.src[1] == 0 && dl.src[5] == 0 && dl_len(dl.ref[0]) == 1024u;
        check("USR2 kit: its lanes play USR2's zones by their GM note (BD zone 0, SD and CH zone 1), the rest the kit", ok);
        fun5_with(b24, 0, 0, E0, 37 + 3);
        ok = sl24_import_buf(b24, sizeof b24, 0) && proj_tmp.cur.dl_hash == 0 && dl.src[0] == 0;
        check("USR4 kit (not one of our slots): the default kit, no lanes", ok);
        usr_nz[1] = 0;
#endif
    }
    printf("sl24 import test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
