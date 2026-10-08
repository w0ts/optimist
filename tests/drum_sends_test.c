/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' sends (firmware/src/drum_sends.c: the drums' only sends) and their storage (drum_store.c,
 * drum_kits.c, ed_dsend.c) on the host:
 *   audio    per-lane REV / DLY / CHO from each voice into the bus inputs; lanes as they are send exactly as the old
 *            default (GLO > DRUMS REV 16) did (the delay and chorus inputs stay exactly 0); the FX bypass leaves
 *            every lane dry; the SLICER takes the sends after it when the lanes are all alike, else before it; a
 *            pattern with reverb on the snare only: the reverb bus hears the snare and nothing else, its tail
 *            starts at the first snare; GLO > MACRO SPACE moves every lane's REV as it moved DRUMS REV
 *   migrate  a project from before (its G_DRREV, TRK lanes): each TRK lane takes G_DRREV (the nearest level), a lane
 *            with its own REV keeps it; the default (16): every word unchanged; new projects carry DRREV_MOVED
 *   project  FUNA (format 10) + the drum record on a simulated NOR (storage.c): round trip, no record for the kit as it
 *            is, no second write of an unchanged record, FUN8 in flash, torn writes at every program of a
 *            save (the old project with its record or the new one with its own), a damaged record object,
 *            a slot kept over a reset finding its record again
 *   kits     DKB3 (sends after the kits): store / load with sends, an old kit's TRK lanes load REV 4, DKB1 banks read
 *            with every lane as it is, a v1 import clears them
 *   editor   36 / 37 / 39 / 40 version 2 (with sends; REV -1 from an older editor: 4), version 1 unchanged
 *   UI       SOUND 3: REV 0..31 (4 as it is), DLY, CHO; the page is there on the drum track */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static uint8_t nor[0x100000];
static int fail_after = -1;                       /* torn write: the n-th program fails */
static uint32_t progs, dl_writes;                 /* programs done; erases / programs in FL_DLANE */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    dl_writes += off >= 0xE5000u && off < 0xE7000u;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    progs++;
    dl_writes += off >= 0xE5000u && off < 0xE7000u;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static union {                                    /* as project.c has it (flash builds) */
    project_t cur;
    uint8_t v8[PROJ_V8_N];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"

static struct { int force; uint8_t arm, arm_t; } ui;
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static void ui_say(const char *a, const char *b)
{
    str_cpy(last_msg, a, sizeof last_msg);
    str_cpy(last_msg + str_len(last_msg), b, sizeof last_msg - str_len(last_msg));
}
#define UK_HOST 1
#include "../firmware/src/drums/drum_kits.c"

static uint8_t ed_out[600];                       /* the editor's reply builder, as editor.c has it */
static uint32_t ed_n;
static uint8_t flash_ok = 1;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_str(const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
#include "../firmware/src/io/editor/ed_drums.c"
static uint32_t pack(const void *p, uint32_t n, uint8_t *o)   /* pack7, as the editor sends */
{
    const uint8_t *b = p;
    uint32_t k = 0;
    while (n) {
        uint32_t c = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < c; j++)
            m |= (uint32_t)(b[j] >> 7) << j;
        o[k++] = (uint8_t)m;
        for (j = 0; j < c; j++)
            o[k++] = b[j] & 0x7Fu;
        b += c;
        n -= c;
    }
    return k;
}
static int cmd(uint32_t c, const uint8_t *a, uint32_t na) { ed_n = 0; return ed_drums(c, a, na); }

static int fails;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* ------------------------------------------------------------------ audio */
enum { L_KICK = 0, L_SNARE = 2, L_HAT = 4 };
typedef struct { double dry, rev, dly, cho; uint32_t nz_rev, nz_dly, nz_cho; } sends_t;
static int32_t rev_buf[48 * CTL], dry_buf[48 * CTL];   /* the last hit's reverb input and dry output */

/* one hit of note on the current kit, rendered for nb blocks straight through drums_render; the energy of
 * the dry output and of each bus input (fx.c send_r / send_d / send_c as drums_mix writes them) */
static sends_t hit(uint32_t note, uint32_t nb, int mono)
{
    sends_t e;
    uint32_t b, i;
    memset(&e, 0, sizeof e);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    rng_state = 0x1234567u;
    drum_on(note, 110);
    for (b = 0; b < nb; b++) {
        int32_t l[CTL] = {0}, r[CTL] = {0}, m[CTL] = {0};
        memset(send_r, 0, sizeof send_r);
        memset(send_d, 0, sizeof send_d);
        memset(send_c, 0, sizeof send_c);
        if (mono)
            drums_render_mono(m, CTL);
        else
            drums_render(l, r, send_r, CTL);
        for (i = 0; i < CTL; i++) {
            double x = mono ? m[i] : l[i];
            e.dry += x * x;
            e.rev += (double)send_r[i] * send_r[i];
            e.dly += (double)send_d[i] * send_d[i];
            e.cho += (double)send_c[i] * send_c[i];
            e.nz_rev += send_r[i] != 0;
            e.nz_dly += send_d[i] != 0;
            e.nz_cho += send_c[i] != 0;
            if (b < 48u) {
                rev_buf[b * CTL + i] = send_r[i];
                dry_buf[b * CTL + i] = mono ? m[i] : l[i];
            }
        }
    }
    return e;
}
static void sends_zero(void) { memset(dsend, 0, sizeof dsend); }
#define OLD_TRK(d, c) ((uint16_t)((d) << 5 | (c) << 10))   /* a word saved before 2026-10 with REV TRK */

static void audio_tests(void)
{
    sends_t a, b;
    static int32_t ref[48 * CTL];
    uint32_t i, ok, l;
    host_tracks_init();
    TDRUM->p[P_E0] = DRUM_SAMPLED;                /* 808 */
    TDRUM->p[P_PAN] = 0;
    song.g[G_DRLVL] = 100;
    sends_zero();
    a = hit(38, 40, 0);
    memcpy(ref, rev_buf, sizeof ref);
    ok = a.dry > 0 && a.nz_rev > 0;
    for (i = 0; i < 40u * CTL && ok; i++)         /* one voice, pan centre: the old default, mulq15(s, 16 * 258) */
        ok = rev_buf[i] == mulq15(dry_buf[i], 16 * 258);
    check("lanes as they are: the snare's reverb send is the old default's (DRUMS REV 16), sample for sample", ok);
    check("... the delay and chorus inputs stay exactly 0 (their buses stay idle)", a.nz_dly == 0u && a.nz_cho == 0u);
    dsend[L_SNARE] = (uint16_t)(DSEND_OWN | DSEND_DEF);
    b = hit(38, 40, 0);
    check("a lane at REV 4 (a non-canonical word): the same, sample for sample", !memcmp(ref, rev_buf, sizeof ref) && b.dry == a.dry);

    dsend[L_SNARE] = dsend_word(31, 0, 0);
    a = hit(38, 40, 0);
    ok = a.rev > 0;
    for (i = 0; i < 40u * CTL && ok; i++)
        ok = rev_buf[i] == mulq15(dry_buf[i], 127 * 258);
    check("lane REV 31: the send of a synth track's REV at 127, sample for sample", ok);
    dsend[L_KICK] = dsend_word(0, 0, 0);
    a = hit(36, 40, 0);
    check("... the kick at REV 0: no reverb", a.dry > 0 && a.nz_rev == 0u && a.nz_dly == 0u);
    sends_zero();
    dsend[L_HAT] = dsend_word(DSEND_DEF, 31, 0);
    a = hit(42, 20, 0);
    b = hit(36, 20, 0);
    check("DLY on the hat: the delay input hears the hat, not the kick", a.nz_dly > 0u && b.nz_dly == 0u && a.nz_rev > 0u);
    dsend[L_HAT] = dsend_word(DSEND_DEF, 0, 16);
    a = hit(42, 20, 0);
    check("CHO on the hat: the chorus input, no delay", a.nz_cho > 0u && a.nz_dly == 0u);
    dsend[L_HAT] = dsend_word(DSEND_DEF, 31, 31);
    TDRUM->p[P_FXOFF] = 1;                        /* the FX bypass */
    a = hit(42, 20, 0);
    check("FX bypass: every lane dry (no reverb, delay, chorus)", a.dry > 0 && !a.nz_rev && !a.nz_dly && !a.nz_cho);
    TDRUM->p[P_FXOFF] = 0;
    sends_zero();
    dsend[L_SNARE] = dsend_word(20, 10, 0);
    a = hit(76, 20, 0);                           /* the click's wood block: no lane, a lane's sends as it is */
    check("the click: REV as a lane as it is, no delay", a.nz_rev > 0u && !a.nz_dly);
    a = hit(38, 20, 1);
    check("SLICER path (mono), the lanes not alike: each voice sends before it", a.nz_rev > 0u && a.nz_dly > 0u);
    sends_zero();
    a = hit(38, 20, 1);
    check("SLICER path, the lanes all alike: no send before it (after it, from the sum, as before)", !a.nz_rev && !a.nz_dly);
    for (l = 0; l < DRUM_LANES; l++)
        dsend[l] = dsend_word(10, 0, 0);
    ok = dsend_one(1) == 41 * 258 && dsend_one(0) == 0;
    sends_zero();
    ok &= dsend_one(1) == 16 * 258;
    dsend[3] = dsend_word(DSEND_DEF, 0, 1);
    ok &= dsend_one(1) == -1;
    dsend[3] = dsend_word(5, 0, 0);
    ok &= dsend_one(1) == -1;
    sends_zero();
    check("dsend_one: the shared reverb send of lanes all alike (no DLY / CHO), else -1; bypassed 0", ok);
    check("levels: 31 = a track's 127, 4 = 16 (the old default), 0 = none; the nearest of 16, 40, 100, 127",
          dsend_lvl(31) == 127 && dsend_lvl(4) == 16 && dsend_lvl(0) == 0 && dsend_near(16) == 4u && dsend_near(40) == 10u &&
          dsend_near(100) == 24u && dsend_near(127) == 31u && dsend_near(0) == 0u);
#if FELUCCA_MACROS
    dsend_msc = 64, dsend_madd = 40;              /* SPACE at +63: + 40 steps, as DRUMS REV 16 -> 56 */
    ok = dsend_one(1) == 56 * 258;
    dsend_msc = 32, dsend_madd = 0;               /* a scaling: 16 -> 8 */
    ok &= dsend_one(1) == 8 * 258;
    dsend_msc = 64, dsend_madd = 0;
    check("MACRO SPACE: every lane's REV moved as DRUMS REV was (16 + 40 = 56; x 1/2 = 8)", ok);
#endif
}

/* a 2-bar pattern through the whole mix (mix_block): kick on every beat, snare on 2 and 4; every lane's REV 0 but
 * the snare's, 31: the reverb input carries the snare only, the reverb output starts at the first snare */
static void pattern_test(void)
{
    uint32_t b, i, k, prev_snare = 0, first_snare = 0, first_wet = 0, rev_without_snare = 0, rev_with_snare = 0, kick_only_blocks = 0;
    int32_t o[CTL * 2];
    host_tracks_init();
    song.g[G_BPM] = 120;
    TDRUM->p[P_E0] = DRUM_SAMPLED;
    TDRUM->p[P_SLEN] = 16;
    host_drum_rev(0);
    dsend[L_SNARE] = dsend_word(31, 0, 0);
    for (i = 0; i < 16u; i++) {
        if (i % 4u == 0u)
            dstep_set(&TDRUM->dstep[i], L_KICK, LV_NORM, 0);
        if (i % 8u == 4u)
            dstep_set(&TDRUM->dstep[i], L_SNARE, LV_NORM, 0);
    }
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    transport_req = 1;
    for (b = 0; b < 2u * 2u * FS / CTL; b++) {   /* 2 bars at 120 */
        uint32_t snare_on = prev_snare, any_on = 0;   /* (a snare that ended in this block sounded in it) */
        mix_block(o, CTL);
        uint32_t now_snare = 0;
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].active) {
                any_on = 1;
                snare_on |= lane_of_note(drums.v[k].note) == L_SNARE;
                now_snare |= lane_of_note(drums.v[k].note) == L_SNARE;
            }
        prev_snare = now_snare;
        for (i = 0; i < CTL; i++) {
            if (send_r[i] && !snare_on)
                rev_without_snare++;
            if (send_r[i])
                rev_with_snare++;
            if (!first_wet && wet_l[i])
                first_wet = b * CTL + i + 1u;
        }
        if (snare_on && !first_snare)
            first_snare = b * CTL + 1u;
        kick_only_blocks += any_on && !snare_on;
    }
    transport_req = 2;
    mix_block(o, CTL);
    printf("pattern: first snare at %u, reverb output from %u; reverb input %u samples, %u without a snare sounding\n",
           first_snare, first_wet, rev_with_snare, rev_without_snare);
    check("pattern: kicks and snares play", first_snare && kick_only_blocks > 0u);
    check("... the reverb input only while a snare sounds", rev_with_snare > 0u && rev_without_snare == 0u);
    check("... the reverb output starts after the first snare (not at the kick on 1), within 0.1 s", first_wet &&
          first_wet >= first_snare && first_wet <= first_snare + FS / 10u);
    sends_zero();
}

/* ---------------------------------------------------------------- project */
static project_t P, Q;
static dlrec_t D, E;
static void lanes_set(uint32_t seed)              /* a recognisable working drum state */
{
    memset(&dl, 0, sizeof dl);
    sends_zero();
    dl.ofs[seed & 15u][DE_TUNE] = (int8_t)(seed % 20u + 1u);
    dl.src[(seed + 1u) & 15u] = DL_USR + 1u;
    dsend[L_SNARE] = dsend_word(seed % 31u, seed % 7u, 3);
}
static uint32_t dl_flash_writes(void) { return dl_writes; }
static int same_pair(const project_t *p, const dlrec_t *d, const project_t *q, const dlrec_t *e)
{
    return !memcmp(p, q, sizeof *p) && !memcmp(d, e, sizeof *d);
}

static void project_tests(void)
{
    static project_t P0, P1;
    static dlrec_t D0, D1;
    uint32_t w, k, bad;
    int ok;
    host_tracks_init();
    memset(nor, 0xFF, sizeof nor);
    lanes_set(5);
    proj_capture(&P, &D);
    check("FUNB: 3640 B, four .noinit slots leave room; the drum record 236 B", sizeof(project_t) == 3640u &&
          4u * sizeof(project_t) + 256u <= 0x3D50u && sizeof(dlrec_t) == 236u && P.magic == 0x46554E42u);
    check("put slot 2 (lanes + sends): ok", proj_put(OBJ_PROJECT0 + 1, &P, &D) == 0);
    memset(&Q, 0, sizeof Q);
    memset(&E, 0xAA, sizeof E);
    ok = proj_get(OBJ_PROJECT0 + 1, &Q, &E) && same_pair(&P, &D, &Q, &E) && proj_ok(&Q);
    check("... get: the project and its drum record, as saved", ok);
    lanes_set(5);
    sends_zero();
    memset(&dl, 0, sizeof dl);
    proj_apply(&Q, &E, 1);
    check("... applied: the sends play again", dsend[L_SNARE] == dsend_word(5, 5, 3) && dl.ofs[5][DE_TUNE] == 6);
    check("a project saved now: G_DRREV says moved (DRREV_MOVED)", P.g[G_DRREV] == DRREV_MOVED && Q.g[G_DRREV] == DRREV_MOVED);
    w = dl_flash_writes();
    TDRUM->p[P_SLEN] = 7;                         /* the project changes, not the lanes */
    proj_capture(&P, &D);
    check("save again, lanes unchanged: the drum record is not written again", proj_put(OBJ_PROJECT0 + 1, &P, &D) == 0 &&
          dl_flash_writes() == w);
    memset(&dl, 0, sizeof dl);
    sends_zero();
    proj_capture(&P, &D);
    w = dl_flash_writes();
    check("the kit as it is (all zero, key 0): no drum record written", P.dl_hash == 0u && proj_put(OBJ_PROJECT0 + 3, &P, &D) == 0 &&
          dl_flash_writes() == w && proj_get(OBJ_PROJECT0 + 3, &Q, &E) && same_pair(&P, &D, &Q, &E));
    {   /* a FUN8 project in flash (the format before): its lanes inline, sends TRK / 0, its DRUMS REV */
        static uint8_t v8[PROJ_V8_N];
        lanes_set(9);
        proj_capture(&P, &D);
        P.g[G_DRREV] = 100;
        memcpy(v8, &P, PROJ_V7_N);
        memcpy(v8 + PROJ_V7_N, &dl, sizeof dl);
        ((uint32_t *)v8)[0] = PROJ_MAGIC_V8;
        ((uint32_t *)v8)[1] = PROJ_V8_N;
        *(uint32_t *)(v8 + PROJ_V8_N - 4u) = proj_hash(v8, PROJ_V8_N - 4u);
        st_save(OBJ_PROJECT0 + 2, v8, sizeof v8);
        ok = proj_get(OBJ_PROJECT0 + 2, &Q, &E) && proj_ok(&Q) && !memcmp(&E.l, &dl, sizeof dl) && Q.dl_hash == dlrec_hash(&E) &&
             !memcmp(Q.t, P.t, sizeof P.t);
        for (k = 0; k < DRUM_LANES; k++)
            ok &= E.snd[k] == 0u;
        check("a FUN8 project in flash: loads, its lanes from it, sends TRK / 0", ok);
        proj_apply(&Q, &E, 1);
        check("... applied: its TRK lanes take its DRUMS REV 100 (REV 24: 99)", dsend[L_SNARE] == dsend_word(24, 0, 0) &&
              dsend[0] == dsend_word(24, 0, 0));
        check("... saved again: FUNA + its drum record, read back the same", proj_put(OBJ_PROJECT0 + 2, &Q, &E) == 0 &&
              proj_get(OBJ_PROJECT0 + 2, &P, &D) && same_pair(&P, &D, &Q, &E));
    }
    /* torn writes: a save of slot 1 cut at every program it makes; after each, the slot reads back as the
     * old project with its record or the new one with its own */
    lanes_set(11);
    proj_capture(&P0, &D0);
    check("slot 1: the old project saved", proj_put(OBJ_PROJECT0, &P0, &D0) == 0);
    lanes_set(12);
    TDRUM->p[P_SLEN] = 9;
    proj_capture(&P1, &D1);
    {
        static uint8_t snap[0x10000];             /* the data flash around both objects, before the save */
        uint32_t n, total;
        memcpy(snap, nor + 0x97000u, 0x9000u);
        memcpy(snap + 0x9000u, nor + 0xE5000u, 0x2000u);
        progs = 0;
        proj_put(OBJ_PROJECT0, &P1, &D1);
        total = progs;
        bad = 0;
        for (n = 0; n < total; n++) {
            memcpy(nor + 0x97000u, snap, 0x9000u);
            memcpy(nor + 0xE5000u, snap + 0x9000u, 0x2000u);
            fail_after = (int)n;
            ok = proj_put(OBJ_PROJECT0, &P1, &D1) != 0;
            fail_after = -1;
            ok &= proj_get(OBJ_PROJECT0, &Q, &E) && proj_ok(&Q) && Q.dl_hash == dlrec_hash(&E) &&
                  (same_pair(&Q, &E, &P0, &D0) || same_pair(&Q, &E, &P1, &D1));
            bad += !ok;
        }
        printf("torn: %u programs in a save, each one cut\n", total);
        check("torn at every program of a save: old project + record, or new + its own", total >= 4u && !bad);
        /* twice in a row: a save torn after its record (the project not written), then another torn the
         * same way: the old project still finds its record (the entry the flash project names stays) */
        memcpy(nor + 0x97000u, snap, 0x9000u);
        memcpy(nor + 0xE5000u, snap + 0x9000u, 0x2000u);
        progs = 0;
        fail_after = (int)total - 1;              /* the last program: the project's commit record */
        ok = proj_put(OBJ_PROJECT0, &P1, &D1) != 0;
        fail_after = -1;
        lanes_set(13);
        proj_capture(&P, &D);
        fail_after = (int)total - 1;
        ok &= proj_put(OBJ_PROJECT0, &P, &D) != 0;
        fail_after = -1;
        ok &= proj_get(OBJ_PROJECT0, &Q, &E) && same_pair(&Q, &E, &P0, &D0);
        check("two saves torn in the project write: the old project keeps its record", ok);
    }
    check("slot 1 saved for good", proj_put(OBJ_PROJECT0, &P1, &D1) == 0 && proj_get(OBJ_PROJECT0, &Q, &E) &&
          same_pair(&Q, &E, &P1, &D1));
    lanes_set(14);                                /* the working project (autosave): entries of its own */
    proj_capture(&P, &D);
    check("autosave: its own entries, slot 1 untouched", proj_put(OBJ_AUTOSAVE, &P, &D) == 0 && proj_get(OBJ_AUTOSAVE, &Q, &E) &&
          same_pair(&Q, &E, &P, &D) && proj_get(OBJ_PROJECT0, &Q, &E) && same_pair(&Q, &E, &P1, &D1));
    {   /* a reset: slot 1 still valid in .noinit, its drum record (RAM) gone: found again in flash */
        proj_slot[0] = P1;
        memset(&proj_dl[0], 0, sizeof proj_dl[0]);
        ok = proj_slot_boot(0) && !memcmp(&proj_dl[0], &D1, sizeof D1);
        check("reset: a valid slot finds its drum record by its key (as in flash: not dirty)", ok);
        proj_slot[0] = P1;
        proj_slot[0].t[1].p[P_SLEN] = 3;          /* a section stored, not yet in flash */
        proj_slot[0].sum = proj_sum(&proj_slot[0]);
        memset(&proj_dl[0], 0, sizeof proj_dl[0]);
        ok = !proj_slot_boot(0) && !memcmp(&proj_dl[0], &D1, sizeof D1);
        check("... a newer section (same lanes): its record found, marked to be written", ok);
        lanes_set(15);
        proj_capture(&proj_slot[0], &proj_dl[0]);  /* a section with lanes never written */
        memset(&proj_dl[0], 0, sizeof proj_dl[0]);
        ok = proj_slot_boot(0) && same_pair(&proj_slot[0], &proj_dl[0], &P1, &D1);
        check("... a section whose record never reached flash: the slot as flash has it", ok);
    }
    {   /* the record object damaged (both copies): the project still loads, the kit as it is */
        nor[0xE5000u + 300u] ^= 1u;
        nor[0xE6000u + 300u] ^= 1u;
        ok = proj_get(OBJ_PROJECT0, &Q, &E) && proj_ok(&Q) && Q.dl_hash == 0u && dlrec_hash(&E) == 0u &&
             !memcmp(Q.t, P1.t, sizeof P1.t);
        check("drum records damaged: the project loads with the kit as it is", ok);
        memset(nor + 0xE5000u, 0xFF, 0x2000u);
    }
    {   /* where the records live */
        int in = st_sector(OBJ_DLANES, 0) == 0xE5000u && st_sector(OBJ_DLANES, 1) == 0xE6000u;
        uint32_t o, c, k, apart = 1;
        for (c = 0; c < 2u; c++) {               /* no other object's sector, nor USR1..3 (0xA0000..0xDBFFF: the kit /
                                                  * FM6 bank carved from USR3's end included), nor the update staging */
            uint32_t a = st_sector(OBJ_DLANES, c);
            for (o = 0; o < OBJ_COUNT; o++)
                for (k = 0; k < 2u; k++)
                    apart &= o == OBJ_DLANES || st_sector(o, k) != a;
            apart &= (a >= SMP_USER_BASE + 3u * SMP_USER_SIZE || a + 4096u <= SMP_USER_BASE) && !(a < 0xE5000u && a + 4096u > 0xE0000u);
        }
        check("the drum records' sectors: 0xE5000 / 0xE6000 (FL_DLANE)", in);
        check("... apart from every other object, USR1..3 (kit banks at USR3's end) and the update staging", apart);
    }
}

/* projects from before: G_DRREV and TRK lanes */
static void migrate_tests(void)
{
    {
        static project_t O;
        static dlrec_t OD;
        uint32_t l, same = 1;
        host_tracks_init();
        memset(&dl, 0, sizeof dl);
        sends_zero();
        proj_capture(&O, &OD);
        O.g[G_DRREV] = 16;                        /* the default, every lane TRK / 0: no record */
        O.dl_hash = 0;
        dsend[3] = dsend_word(9, 9, 9);
        proj_apply(&O, 0, 1);
        for (l = 0; l < DRUM_LANES; l++)
            same &= dsend[l] == 0u;
        check("migrate: an old default project (DRUMS REV 16, lanes TRK / 0): every lane as it is (bit-identical)", same);
        memset(&OD, 0, sizeof OD);
        OD.snd[L_SNARE] = OLD_TRK(5, 0);          /* TRK, its delay */
        OD.snd[L_HAT] = (uint16_t)(DSEND_OWN | 20u);   /* its own REV 20 */
        OD.l.ofs[1][DE_TUNE] = 2;
        O.g[G_DRREV] = 40;
        O.dl_hash = dlrec_hash(&OD);
        proj_apply(&O, &OD, 1);
        check("migrate: DRUMS REV 40: each TRK lane REV 10 (41), its DLY kept; an own REV kept",
              dsend[L_SNARE] == dsend_word(10, 5, 0) && dsend[L_HAT] == dsend_word(20, 0, 0) && dsend[L_KICK] == dsend_word(10, 0, 0) &&
              dl.ofs[1][DE_TUNE] == 2);
        O.g[G_DRREV] = 0;
        proj_apply(&O, &OD, 0);                   /* a song section (all 0): the same rule */
        check("migrate: an old song section, DRUMS REV 0: its TRK lanes dry, the hat's own REV kept",
              dsend[L_SNARE] == dsend_word(0, 5, 0) && dsend[L_KICK] == dsend_word(0, 0, 0) && dsend[L_HAT] == dsend_word(20, 0, 0));
        proj_capture(&O, &OD);
        sends_zero();
        proj_apply(&O, &OD, 1);
        check("... captured again (DRREV_MOVED) and applied: kept as they are, no second migration",
              O.g[G_DRREV] == DRREV_MOVED && dsend[L_SNARE] == dsend_word(0, 5, 0) && dsend[L_KICK] == dsend_word(0, 0, 0));
        sends_zero();
    }
}

/* ------------------------------------------------------------------- kits */
static void kit_tests(void)
{
    uint16_t s[DRUM_LANES];
    ukit_t k;
    uint32_t l;
    int ok;
    memset(nor + 0xDA000u, 0xFF, 0x2000u);
    uk_read = 0;
    TDRUM->p[P_E0] = DRUM_SAMPLED + 1u;
    memset(&dl, 0, sizeof dl);
    sends_zero();
    dl.ofs[2][DE_CUT] = -10;
    dsend[L_SNARE] = dsend_word(25, 4, 0);
    dsend[L_HAT] = dsend_word(DSEND_DEF, 12, 30);
    check("kit: SAVE into slot 3 with sends (DKB3)", ukit_store(2) == 0 &&
          ((const ukit_bank_t *)(void *)st_buf)->magic == UK_MAGIC);
    sends_zero();
    memset(&dl, 0, sizeof dl);
    ok = ukit_load(2) && dsend[L_SNARE] == dsend_word(25, 4, 0) && dsend[L_HAT] == dsend_word(DSEND_DEF, 12, 30) &&
         dl.ofs[2][DE_CUT] == -10 && dsend[0] == 0u;
    check("... load: the lanes and their sends", ok);
    check("... the bank's tag is DKB3: nameless 196 B kits, then the sends", UK_MAGIC == 0x33424B44u &&
          sizeof(ukit_t) == 196u && ((const ukit_bank_t *)(void *)st_buf)->rsize == 196u);
    ukit_get(2, &k);
    {   /* a kit saved before 2026-10: its snare TRK with a delay, the hat its own REV 4 (bits set) */
        ukit_bank_t *nb = UK_TMP;
        memcpy(nb, uk_bank(), sizeof *nb);
        nb->snd[2][L_SNARE] = OLD_TRK(7, 0);
        nb->snd[2][L_HAT] = (uint16_t)(DSEND_OWN | DSEND_DEF);
        st_save(OBJ_UKIT, nb, sizeof *nb);
        uk_read = 0;
        ok = ukit_load(2) && dsend_rev(dsend[L_SNARE]) == DSEND_DEF && dsend_dly(dsend[L_SNARE]) == 7u && dsend[L_HAT] == 0u;
        check("an old kit: a TRK lane loads REV 4 (no G_DRREV in a kit), its DLY kept; words canonical", ok);
    }
    check("... a v1 import (no sends) over it: each lane as it is", ukit_put(2, &k) == 0 && ukit_sends(2, s) && s[L_SNARE] == 0u &&
          s[L_HAT] == 0u);
    {   /* a DKB1 bank in flash (the first drum kits firmware: 204 B kits with a name, no sends): its kits load
         * with every lane as it is, the names dropped */
        static uint8_t v1[8u + UK_N * UK_RSIZE1];
        uint32_t m = UK_MAGIC1;
        uint16_t rs = UK_RSIZE1, nk = UK_N;
        uint8_t *r = v1 + 8u + 5u * UK_RSIZE1;
        memset(v1, 0, sizeof v1);
        memcpy(v1, &m, 4);
        memcpy(v1 + 4, &rs, 2);
        memcpy(v1 + 6, &nk, 2);
        memcpy(r, &k, 2);                                     /* used, base */
        memcpy(r + 2, "OLD\0\0\0\0\0", 8);                /* the name */
        memcpy(r + 10, (const uint8_t *)&k + 2, sizeof k - 2u);
        st_save(OBJ_UKIT, v1, sizeof v1);
        uk_read = 0;
        dsend[L_SNARE] = dsend_word(9, 9, 9);
        ok = ukit_used(5) && ukit_count() == 1u && ukit_load(5) && dl.ofs[2][DE_CUT] == -10;
        for (l = 0; l < DRUM_LANES; l++)
            ok &= dsend[l] == 0u;
        check("a DKB1 bank: read, its kit loads with every lane's sends as it is", ok);
        dsend[L_HAT] = dsend_word(3, 0, 0);
        ok = ukit_store(6) == 0 && ((const ukit_bank_t *)(void *)st_buf)->magic == UK_MAGIC && ukit_used(5) &&
             ukit_sends(6, s) && s[L_HAT] == dsend_word(3, 0, 0) && ukit_sends(5, s) && s[L_HAT] == 0u;
        check("... a save rewrites it as DKB3, the old kit kept", ok);
    }
}

/* ----------------------------------------------------------------- editor */
static void editor_tests(void)
{
    uint8_t a[400], b[260];
    uint32_t n, l;
    int ok;
    memset(&dl, 0, sizeof dl);
    sends_zero();
    dl.ofs[1][DE_DRIVE] = 7;
    dsend[L_SNARE] = dsend_word(DSEND_DEF, 5, 6);
    dsend[L_HAT] = dsend_word(17, 0, 0);
    a[0] = 2;
    ok = cmd(ED_DRUM_LANES, a, 1) && ed_out[0] == 2 && ed_unpack7(ed_out + 1, ed_n - 1u, b, 252) == 252u &&
         !memcmp(b, &dl, sizeof dl) && b[204 + 3 * L_SNARE] == DSEND_DEF && b[204 + 3 * L_SNARE + 1] == 5 &&
         b[204 + 3 * L_SNARE + 2] == 6 && b[204 + 3 * L_HAT] == 17;
    check("36 v2 get: 2, lanes + sends (252 B; REV 0..31, never -1)", ok);
    ok = cmd(ED_DRUM_LANES, a, 0) && ed_unpack7(ed_out, ed_n, b, 252) == 204u && !memcmp(b, &dl, sizeof dl);
    check("36 v1 get: the 204 B as before", ok);
    memcpy(b, &dl, sizeof dl);
    memset(b + 204, 0, 48);
    for (l = 0; l < DRUM_LANES; l++)
        b[204 + 3 * l] = 0xFF;                    /* REV -1 (TRK, an older editor): 4 */
    b[204 + 3 * 9] = 30, b[204 + 3 * 9 + 1] = 99, b[204 + 3 * 9 + 2] = 2;   /* lane 10: REV 30, DLY clamped to 31 */
    b[204 + 3 * L_HAT] = 0xFF;
    a[0] = 2;
    n = 1u + pack(b, 252, a + 1);
    ok = n == 289u && cmd(ED_DRUM_LANES, a, n) && ed_out[0] == 2 && dsend[9] == dsend_word(30, 31, 2) && dsend[L_HAT] == 0u &&
         dsend[L_SNARE] == 0u && dl.ofs[1][DE_DRIVE] == 7;
    check("36 v2 set (289 bytes): sends set and clamped, REV -1 read as 4", ok);
    {   /* a v1 set whose first byte (the first group's top bits) happens to be 2: byte 1 negative */
        dlanes_t t = dl;
        memset(t.ofs[0], 0, sizeof t.ofs[0]);
        t.ofs[0][DE_DECAY] = -5;
        n = pack(&t, sizeof t, a);
        ok = a[0] == 2 && n == 234u && cmd(ED_DRUM_LANES, a, n) && ed_unpack7(ed_out, ed_n, b, 252) == 204u &&
             dl.ofs[0][DE_DECAY] == -5;
    }
    check("36 v1 set starting with byte 2: still a v1 set", ok);
    a[0] = 0x40 | 9;
    ok = cmd(ED_DRUM_LANE, a, 1) && ed_out[0] == (0x40 | 9) && ed_unpack7(ed_out + 1, ed_n - 1u, b, 15) == 15u && b[12] == 30 &&
         b[13] == 31 && b[14] == 2;
    check("37 v2 get: lane 10 with its sends (15 B)", ok);
    memset(b, 0, 15);
    b[DE_TUNE] = 3;
    b[12] = 0xFF, b[13] = 0, b[14] = 8;
    a[0] = 0x40 | L_KICK;
    n = 1u + pack(b, 15, a + 1);
    ok = cmd(ED_DRUM_LANE, a, n) && dl.ofs[L_KICK][DE_TUNE] == 3 && dsend[L_KICK] == dsend_word(DSEND_DEF, 0, 8) &&
         dsend[9] == dsend_word(30, 31, 2);
    check("37 v2 set: lane 1's sound and sends, the others kept", ok);
    a[0] = L_KICK;
    ok = cmd(ED_DRUM_LANE, a, 1) && ed_out[0] == L_KICK && ed_unpack7(ed_out + 1, ed_n - 1u, b, 15) == 12u && b[DE_TUNE] == 3;
    check("37 v1 get: 12 bytes as before", ok);
    a[0] = 16;
    check("37 with lane 16 (neither form): no reply", !cmd(ED_DRUM_LANE, a, 1));
    /* kits */
    memset(nor + 0xDA000u, 0xFF, 0x2000u);
    uk_read = 0;
    dsend[L_SNARE] = dsend_word(12, 1, 2);
    ukit_store(4);
    a[0] = 0x40 | 4;
    ok = cmd(ED_UKIT_GET, a, 1) && ed_out[0] == (0x40 | 4) && ed_out[1] == 1 &&
         ed_unpack7(ed_out + 2, ed_n - 2u, b, 244) == 244u && b[0] == UK_USED &&
         b[196 + 3 * L_SNARE] == 12 && b[196 + 3 * L_SNARE + 1] == 1 && b[196 + 3 * L_SNARE + 2] == 2;
    check("39 v2: the kit (196 B) with its sends (48 B)", ok);
    a[0] = 4;
    ok = cmd(ED_UKIT_GET, a, 1) && ed_out[1] == 1 && ed_unpack7(ed_out + 2, ed_n - 2u, b, 244) == 196u;
    check("39 v1: the 196 B kit", ok);
    ed_unpack7(ed_out + 2, ed_n - 2u, b, 196);
    memset(b + 196, 0, 48);
    for (l = 0; l < DRUM_LANES; l++)
        b[196 + 3 * l] = 0xFF;
    b[196 + 3 * L_HAT + 1] = 20;
    b[196 + 3 * L_HAT] = 0xFF;
    a[0] = 0x40 | 7;
    n = 1u + pack(b, 244, a + 1);
    {
        uint16_t s[DRUM_LANES];
        ok = cmd(ED_UKIT_PUT, a, n) && ed_out[0] == (0x40 | 7) && ed_out[1] == 0 && ukit_sends(7, s) &&
             s[L_HAT] == dsend_word(DSEND_DEF, 20, 0) && s[L_SNARE] == 0u;
        check("40 v2: a kit with sends written (an import)", ok);
        song.playing = 1;
        check("... refused while playing (rc 3)", cmd(ED_UKIT_PUT, a, n) && ed_out[1] == 3);
        song.playing = 0;
    }
    for (l = 0; l < DRUM_LANES; l++)
        dsend[l] = 0;
}

/* --------------------------------------------------------------------- UI */
static void ui_tests(void)
{
    const page_t *pg = 0;
    int16_t *vp = 0;
    const param_desc_t *d;
    char val[8];
    const char *unit;
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (str_eq(PAGES[i].title, "SOUND 3"))
            pg = &PAGES[i];
    song.sel = TRK_DRUM;
    check("SOUND 3: REV DLY CHO and an empty slot, on the drum track", pg && pg->id[0] == 16u && pg->id[3] == 0xFFu &&
          page_shown(pg));
    song.sel = 0;
    check("... not on a synth track", pg && !page_shown(pg));
    sends_zero();
    d = dsend_desc(L_SNARE, 0, &vp);
    param_format(d, *vp, val, &unit);
    check("REV of a fresh lane: 4 (16, the old DRUMS REV default)", d && *vp == 4 && !strcmp(val, "4") && d->def == 4);
    dsend_set(L_SNARE, 0, 40);
    dsend_set(L_SNARE, 1, 7);
    dsend_set(L_SNARE, 2, -3);
    d = dsend_desc(L_SNARE, 0, &vp);
    param_format(d, *vp, val, &unit);
    check("knobs: REV clamped to 31, DLY 7, CHO 0", *vp == 31 && !strcmp(val, "31") && dsend[L_SNARE] == dsend_word(31, 7, 0));
    dsend_set(L_SNARE, 0, -1);
    check("REV turned below 0: 0 (no TRK any more)", dsend[L_SNARE] == dsend_word(0, 7, 0) && (dsend[L_SNARE] & DSEND_OWN));
    dsend_set(L_SNARE, 0, 4);
    check("REV back to 4: the word has no REV bits (equal sends, equal words)", dsend[L_SNARE] == dsend_word(4, 7, 0) &&
          !(dsend[L_SNARE] & (DSEND_OWN | 31u)));
    sends_zero();
}

int main(void)
{
    audio_tests();
    pattern_test();
    project_tests();
    migrate_tests();
    kit_tests();
    editor_tests();
    ui_tests();
    printf(fails ? "DRUM SENDS TEST FAILED\n" : "drum sends test passed\n");
    return fails != 0;
}
