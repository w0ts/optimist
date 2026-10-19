/* SPDX-License-Identifier: GPL-3.0-only */
/* A SLOOP 2.7 project into Optimist (FELUCCA_SL24_IMPORT: sl24_import.c, sl24_guard.c), from tests/sl27_fun6.bin, written
 * with SLOOP 2.7.1's own types (tests/sl25_gen.c, SL_VER 27; read back by 2.7.1's own project.c: FUN6, saved by 2.7).
 * 2.7 still writes FUN6 (3816 B); what it adds there, checked here:
 *   - the drum track: SLOOP's 16 fixed sounds (KICK KICK 2 SNARE CLAP HAT .. COWBELL, our INST_* order) -> our 10 lanes,
 *     each used sound on a lane playing that instrument, its hits there with their level and ratchet; the sounds'
 *     TUNE (-8 .. +7 semitones) and DECAY (-4 .. +3: its DECAY_K steps, the same as our lane DECAY's) on that lane;
 *     more than 10 sounds used: the 10 with the most hits, the others said cut;
 *   - a NOTE step's notes held from the step before (2.7: flags bits 3..6, core.h SF_CONT) -> our SF_CARRY; the bits
 *     2..6 (our chance, seq/chance.c) cleared: never read as a chance;
 *   - the arp's modes past ORD (2.7: 15 modes, ours 6) -> the nearest of ours;
 *   - the slot shows as SLOOP's, LOAD twice imports it, the original (and its settings' side record) untouched.
 * Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_DRUM_EDIT 1
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
static uint8_t f27[3816], side[80];
static int load(const char *path, uint8_t *b, uint32_t n)
{
    FILE *f = fopen(path, "rb");
    int ok = f && fread(b, 1, n, f) == n && fgetc(f) == EOF;
    if (f)
        fclose(f);
    return ok;
}
#define T27(b, k) ((b) + 76u + (k) * 934u)                 /* FUN6's track k */
#define T27_STEPS 134u                                      /* its steps (10 B each) */
static void resum(uint8_t *b)
{
    uint32_t h = proj_hash(b, 3812u);
    memcpy(b + 3812, &h, 4);
}

/* the golden's drum hits and the sounds' TUNE / DECAY (tests/sl25_gen.c sl27_extras) */
static const uint8_t HIT[7][4] = {{0, INST_KICK, 3, 0}, {0, INST_CH, 0, 0}, {4, INST_SNARE, 0, 0}, {8, INST_KICK2, 0, 0},
                                  {8, INST_CONGA, 0, 1}, {12, INST_COWBELL, 0, 0}, {12, INST_CLAP, 2, 0}};
static const int8_t TUNE[16] = {5, -8, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1};
static const int8_t DEC[16] = {0, 0, -4, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0};
static const int8_t DK_IDX[8] = {-26, -17, -10, -4, 0, 6, 13, 21};   /* (2.7's drum_synth.c: DECAY -4 .. +3) */

/* the lane playing instrument in, -1 none */
static int lane_of(uint32_t in)
{
    uint32_t l;
    for (l = 0; l < DRUM_LANES; l++)
        if (lane_inst(l) == in)
            return (int)l;
    return -1;
}

static void import_flow(void)
{
    static uint8_t keep[4096];
    uint32_t sec = st_sector(OBJ_PROJECT0 + 1, 0);
    int ok;
    check("2.7's FUN6: SLOOP's (sl26_is), ours does not read it as a project",
          sl26_is(f27, sizeof f27) && !proj_import(&proj_tmp.cur, f27, sizeof f27));
    memset(nor, 0xFF, sizeof nor);
    (void)st_save(OBJ_PROJECT0 + 1, f27, sizeof f27);   /* (2.7's storage writes the same object) */
    st_prog(sec + 64u, side, sizeof side);              /* (2.7's side record place: its project copies leave it erased;
                                                         * written here to see it kept) */
    memcpy(keep, nor + sec, sizeof keep);
    sec_pend_clear();
#if FELUCCA_SL24_XSTEP
    auto_init();
#endif
    sl24_boot_scan();
    sec_boot();
    host_tracks_init();
    song.playing = 0, transport_req = 0;
    project_load(1);
    check("flash: a 2.7 slot shows as SLOOP's (never EMPTY); LOAD offers",
          project_state(1) == PJ_SL24 && !strcmp(last_msg, "SLOOP: LOAD = IMPORT"));
    fm1_ms += 500;
    project_load(1);
    ok = strstr(last_msg, "IMPORTED") != 0;
    check("... LOAD again imports it", ok);
    check("... 2.7's original untouched (its sector whole)", !memcmp(keep, nor + sec, sizeof keep));
}

static void drum_lanes(void)
{
    uint32_t i, l, ok = 1, n = 0;
    for (i = 0; i < 7u; i++) {
        int ln = lane_of(HIT[i][1]);
        const dstep_t *s;
        if (ln < 0) {
            ok = 0;
            continue;
        }
        s = &trk[TRK_DRUM].dstep[HIT[i][0]];
        ok &= dstep_has(s, (uint32_t)ln) && dstep_lvl(s, (uint32_t)ln) == HIT[i][2] && dstep_rat(s, (uint32_t)ln) == HIT[i][3];
    }
    check("drums: each of the 7 sounds used on a lane playing that instrument, its hits there (level, ratchet)", ok);
    for (i = 0; i < NSTEP; i++)
        for (l = 0; l < DRUM_LANES; l++)
            n += (uint32_t)dstep_has(&trk[TRK_DRUM].dstep[i], l);
    check("... no other hit (7 in all)", n == 7u);
    for (i = 0, ok = 1; i < 7u; i++) {
        uint32_t in = HIT[i][1];
        int ln = lane_of(in);
        ok &= ln >= 0 && dl.ofs[ln][DE_TUNE] == TUNE[in] && dl.ofs[ln][DE_DECAY] == DK_IDX[DEC[in] + 4];
    }
    check("... each sound's TUNE (semitones) and DECAY (2.7's DECAY_K steps) on its lane", ok);
}

static void synth_steps(void)
{
    uint32_t k, i, ok = 1;
    for (k = 0; k < NPART; k++)
        for (i = 0; i < NSTEP; i++) {
            uint32_t f = trk[k].step[i].flags, want = (k + i) & 3u;   /* (the golden: accent / slide from its pattern) */
            const uint8_t *g = T27(f27, k) + T27_STEPS + 10u * i;
            want = g[6] & 3u;
            if (i == 4u || i == 5u)
                want |= SF_CARRY;
            ok &= f == want;
        }
    check("synth steps: 2.7's held notes (SF_CONT, bits 3..6) -> SF_CARRY; no chance bits; accent / slide as stored", ok);
}

static void arp(void)
{
    check("arp: 2.7's PINKY (11) -> UP, CHORD (14) -> OFF; OFF with KBD HOLD stays OFF",
          trk[0].p[P_AMODE] == 1 && trk[2].p[P_AMODE] == 0 && trk[1].p[P_AMODE] == 0);
}

/* 11 sounds used (one hit each but KICK 3, SNARE 2): the 10 with the most hits kept (a tie: SLOOP's order), said cut */
static void drums_over(void)
{
    static uint8_t b[3840];
    uint32_t s, ok;
    uint8_t *st = T27(b, TRK_DRUM) + T27_STEPS;
    memcpy(b, f27, sizeof f27);
    memset(st, 0, 640u);
    for (s = 0; s < 11u; s++)
        st[10u * s + (s >> 3)] |= (uint8_t)(1u << (s & 7u));
    st[10u * 20u] |= 1u, st[10u * 21u] |= 1u;          /* KICK twice more */
    st[10u * 22u] |= 4u;                                /* SNARE once more */
    resum(b);
    host_tracks_init();
    ok = sl24_import_buf(b, sizeof f27) && strstr(last_msg, "DRUMS CUT") != 0;
    ok &= lane_of(INST_KICK) >= 0 && lane_of(INST_SNARE) >= 0 && lane_of(INST_HT) < 0;
    for (s = 0; s < 10u; s++)
        ok &= lane_of(s) >= 0;
    check("11 drum sounds used: the 10 with the most hits (a tie: SLOOP's order; HI TOM out), said DRUMS CUT", ok);
}

#if FELUCCA_SL24_EXPORT
/* the imported project exported (SL24_EXPORT: a FUN5, which 2.7 reads) -> build/host/sl27_export.bin (for 2.7.1's own
 * project.c to read back, outside this test), then imported again: the same lanes, TUNE / DECAY, held notes */
static void export_round(void)
{
    static project_t q;
    static dlrec_t td;
    static uint8_t out[3840];
    FILE *f;
    uint32_t lost, i, n, ok = 1;
    proj_capture(&q, &td);
    lost = proj_to_sl24(&q, 0, out);
    for (i = 0, n = 0; i < 64u; i++) {                  /* (its drum track's steps: SLOOP's 16 sounds) */
        const uint8_t *s = out + 12u + 2u * PJ_NG + TRK_DRUM * 940u + 124u + 10u * i;
        n += (uint32_t)__builtin_popcount((uint32_t)s[0] | (uint32_t)s[1] << 8);
    }
    for (i = 0; i < 7u; i++) {
        const uint8_t *s = out + 12u + 2u * PJ_NG + TRK_DRUM * 940u + 124u + 10u * HIT[i][0];
        ok &= (s[HIT[i][1] >> 3] >> (HIT[i][1] & 7u)) & 1u;
    }
    check("export: each lane's hits on the sound of its instrument (7 hits, as 2.7 had them)", n == 7u && ok);
    check("... the steps' chance never written (none here), SF_CARRY as 2.7's held notes",
          !(lost & SX24_CHANCE) && (out[12u + 2u * PJ_NG + 124u + 10u * 5u + 6u] & 0x78u) == 0x78u);
    if ((f = fopen("build/host/sl27_export.bin", "wb")))
        fwrite(out, 1, sizeof out, f), fclose(f);
    host_tracks_init();
    ok = sl24_import_buf(out, sizeof out) != 0;
    check("... imported back (FUN5)", ok);
    drum_lanes();
    synth_steps();
}
#endif

int main(void)
{
    if (!load("tests/sl27_fun6.bin", f27, sizeof f27) || !load("tests/sl27_side.bin", side, sizeof side)) {
        printf("tests/sl27_fun6.bin, sl27_side.bin missing (run from the repo root)\n");
        return 1;
    }
    import_flow();
    drum_lanes();
    synth_steps();
    arp();
#if FELUCCA_SL24_EXPORT
    export_round();
#endif
    drums_over();
    printf("%s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
