/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' SOUND values against what they do (the user, 2026-11-08: "snap on hihats?"). For every kit that is
 * built and every instrument a lane can take (INST_*), one hit is rendered with each of TUNE DECAY SNAP CLICK BEND CUT
 * DRIVE LEVEL at its minimum, at its maximum and at 0 (the same seed and noise state), and the value counts as heard
 * when either end differs from the 0 render by more than -60 dB of the hit's energy. Locked: a value the SOUND pages
 * SHOW (dsnd_desc.c dsnd_desc_lane) changes that lane's sound; a value they HIDE does not, no waiver (the user,
 * 2026-11-08: "keep values that make changes to the sound": SNAP on a hat, only its noise's level, stays). Also the X0X masks
 * (drum_x0x.c X9_SHOW / X8_SHOW) are checked this way: they are what dsnd_desc_lane takes for those kits.
 *   build/host/drum_show_test [-v]    -v: the whole table, kit x instrument x value (else the mismatches only)
 * Built with the X0X kits (run_tests.sh $X0X_ON) and without. */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static struct { int force, redraw; } ui;
static uint8_t settings_later;
static void ui_message(const char *m) { (void)m; }
static void ui_say(const char *a, const char *b) { (void)a, (void)b; }
static int project_used(uint32_t i) { return i < 2; }
static void arrangement_apply(uint32_t s) { (void)s; }
static void arrangement_save(void) {}
static uint32_t arrangement_ready(void) { return 3; }
static void section_store(uint32_t s) { live_sec = (int8_t)s; }
static void section_load(uint32_t s) { live_sec = (int8_t)s; }
#if FELUCCA_QCHAIN
static uint32_t section_bars(uint32_t s) { (void)s; return 1u; }
#endif
static void song_backup(void) {}
static void song_restore(void) {}
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return 1; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { (void)k; b[0] = 0; }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static uint32_t up_engine_slot(uint32_t k) { (void)k; return NENGINES; }
#include "../firmware/src/core/model.c"
#include "../firmware/src/drums/dsnd_desc.c"
#if FELUCCA_DRUM_KITS
static uint8_t kit_nor[0x2000];
static int st_read(uint32_t off, void *dst, uint32_t n)
{ if (off < 0xDA000u || off + n > 0xDC000u) return -1; memcpy(dst, kit_nor + off - 0xDA000u, n); return 0; }
static int st_erase(uint32_t off) { if (off < 0xDA000u || off >= 0xDC000u) return -1; memset(kit_nor + off - 0xDA000u, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{ uint32_t i; if (off < 0xDA000u || off + n > 0xDC000u) return -1; for (i = 0; i < n; i++) kit_nor[off - 0xDA000u + i] &= ((const uint8_t *)src)[i]; return 0; }
#include "../firmware/src/storage/storage.c"
static uint32_t kit_tmp[4096 / 4];
#define UK_HOST 1
#define UK_TMP ((ukit_bank_t *)(void *)kit_tmp)
#include "../firmware/src/drums/drum_kits.c"
#endif

#define RN (FS * 2u / CTL * CTL)                        /* 2 s (whole blocks) */
static int32_t base[RN], var[RN];
static const char *const VN[DE_N] = {"TUNE", "DECAY", "SNAP", "CLICK", "BEND", "CUT", "DRIVE", "LEVEL"};

/* one hit of instrument ins on lane 0 of kit, offsets x: the left channel into out; the energy of it */
static double render(uint32_t kit, uint32_t ins, const int8_t *x, int32_t *out)
{
    uint32_t j, k;
    double e = 0;
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
    rng_state = 0x1234567u;
#if DRUM_X0X
    memset(&xd, 0, sizeof xd);
    memset(&xc, 0, sizeof xc);
#endif
    memset(&dl, 0, sizeof dl);
    dl.ins[0] = (uint8_t)(ins + 1u);
    if (x)
        memcpy(dl.ofs[0], x, DE_N);
    TDRUM->p[P_E0] = (int16_t)kit;
    TDRUM->p[P_PAN] = 0;
    song.g[G_DRLVL] = 100;
    drum_on(DLANE_NOTE(0), 110);
    for (j = 0; j < RN / CTL; j++) {
        int32_t l[CTL] = {0}, r[CTL] = {0}, rev[CTL] = {0};
        drums_render(l, r, rev, CTL);
        for (k = 0; k < CTL; k++) {
            out[j * CTL + k] = l[k];
            e += (double)l[k] * l[k];
        }
    }
    return e;
}
/* the energy of (a - b) over the energy of a, as dB (-200: equal) */
static double diff_db(const int32_t *a, const int32_t *b, double ea)
{
    double e = 0;
    uint32_t i;
    for (i = 0; i < RN; i++)
        e += ((double)a[i] - b[i]) * ((double)a[i] - b[i]);
    return e <= 0 || ea <= 0 ? -200.0 : 10.0 * log10(e / ea);
}

int main(int argc, char **argv)
{
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    uint32_t kit, ins, id, nsilent = 0, ncell = 0, bad_shown = 0, bad_hidden = 0;
    host_tracks_init();
    for (kit = 0; kit < DRUM_KITS; kit++) {
        if (!drum_kit_built(kit))
            continue;
        for (ins = 0; ins < INST_N; ins++) {
            double e0 = render(kit, ins, 0, base);
            if (e0 <= 0) {                              /* (the kit has no such sound: silent) */
                nsilent++;
                continue;
            }
            for (id = 0; id < DE_N; id++) {
                int8_t x[DE_N] = {0};
                int16_t *vp;
                double d, dm;
                int heard, shown;
                x[id] = DE_MIN[id];
                render(kit, ins, x, var);
                d = diff_db(base, var, e0);
                x[id] = DE_MAX[id];
                render(kit, ins, x, var);
                dm = diff_db(base, var, e0);
                d = d > dm ? d : dm;
                heard = d > -60.0;
                memset(&dl, 0, sizeof dl);
                dl.ins[0] = (uint8_t)(ins + 1u);
                TDRUM->p[P_E0] = (int16_t)kit;
                shown = dsnd_desc_lane(0, id, &vp) != 0;
                ncell++;
                if (shown && !heard)
                    bad_shown++;
                if (!shown && heard)
                    bad_hidden++;
                if (shown != heard)
                    printf("MISMATCH kit %u %s lane %s %s: %s, %.1f dB\n", kit, DRUM_KIT_NAMES[kit], INST_NAME[ins], VN[id],
                           shown ? "shown, no effect" : "hidden, has effect", d);
                if (verbose)
                    printf("kit %2u %-12s %-10s %-5s shown %d heard %d (%.1f dB)\n", kit, DRUM_KIT_NAMES[kit], INST_NAME[ins],
                           VN[id], shown, heard, d);
            }
        }
    }
    printf("drum_show: %u cells (%u silent kit x instrument skipped), %u shown without effect, %u hidden with effect\n", ncell,
           nsilent, bad_shown, bad_hidden);
    printf("drum_show: %s\n", bad_shown + bad_hidden ? "FAILED" : "ok");
    return bad_shown + bad_hidden != 0;
}
