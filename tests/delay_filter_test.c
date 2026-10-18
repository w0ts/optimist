/* SPDX-License-Identifier: GPL-3.0-only */
/* The delay's filter (2026-10-18; core.h G_DFTYPE G_DFSLOPE G_DFRES, its FREQ G_DCOLOR; fx.c dly_filt / dly_step;
 * fx_rec.c DLYF_TLV; project.c DLY_FREQ_MARK).
 *   audio    the first echo alone (FDBK 0): a sine's gain through it. LP and HP: 8 x and 1/8 of the cutoff cut, the
 *            other side passed; 24 dB cuts far more than 12 dB (the first echo is filtered: COLOR never touched it);
 *            BP: unity at the cutoff; RES lifts LP at the cutoff, PEAK's lift at the cutoff over RES; a loop at
 *            FDBK 100 % with PEAK at full RES under loud noise never reaches the int16 rails, and lowered it rings out
 *            to exactly 0 and goes idle
 *   record   LP 12 dB RES 0: nothing kept; others round trip; a song section keeps them; no record: the defaults
 *   project  FREQ kept + DLY_FREQ_MARK and read back; an older project's COLOR -> the FREQ of its cutoff; SLOOP 2.4's
 *            COLOR out; PROJECT SAVE / LOAD keeps the filter
 * Run by tests/run_tests.sh. */
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
#include "../firmware/src/storage/sections/sections.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("delay_filter: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---- the delay bus alone */
static void dly_reset(int type, int slope, int freq, int res, int fdbk)
{
    memset(dly_buf, 0, sizeof dly_buf);
    memset(fx.dly_f, 0, sizeof fx.dly_f);
    fx.dly_q = FX_Q_MAX;
    song.g[G_BPM] = 120;
    song.g[G_DTIME] = 2;                                 /* 1/16 at 120 BPM: 5512 samples */
    song.g[G_DMIX] = 127;
    song.g[G_DRET] = 127;
    song.g[G_DFDBK] = (int16_t)fdbk;
    song.g[G_DFTYPE] = (int16_t)type;
    song.g[G_DFSLOPE] = (int16_t)slope;
    song.g[G_DCOLOR] = (int16_t)freq;
    song.g[G_DFRES] = (int16_t)res;
}

static void bus(const int32_t *in, int32_t *out, uint32_t n)   /* in NULL: silence; out NULL: dropped */
{
    static const int32_t z[CTL];
    int32_t wl[CTL], wr[CTL];
    uint32_t b, i;
    for (b = 0; b + CTL <= n; b += CTL) {
        if (fx_buses(z, in ? in + b : z, z, wl, wr, CTL) == 0)
            memset(wl, 0, sizeof wl);
        for (i = 0; out && i < CTL; i++)
            out[b + i] = wl[i];
    }
}

#define SN 4096u                                         /* the sine's length (shorter than the delay: one echo) */
#define RUN (6144u + SN)
static int32_t sig[RUN], wet[RUN];

/* the first echo's gain for a sine of hz (its steady part over the input's) */
static double echo_gain(int type, int slope, int freq, int res, double hz)
{
    uint32_t i, d;
    double si = 0, so = 0;
    dly_reset(type, slope, freq, res, 0);
    d = delay_samples();
    for (i = 0; i < RUN; i++)
        sig[i] = i < SN ? (int32_t)(12000.0 * sin(2.0 * M_PI * hz * i / FS)) : 0;
    bus(sig, wet, RUN);
    for (i = 1024; i < SN; i++) {
        si += (double)sig[i] * sig[i];
        so += (double)wet[d + i] * wet[d + i];
    }
    return sqrt(so / si);
}

static double db(double g) { return 20.0 * log10(g > 1e-9 ? g : 1e-9); }

static void t_audio(void)
{
    const int f = 64;                                    /* a cutoff of CUTOFF_HZ[64] (about 700 Hz) */
    const double fc = CUTOFF_HZ[f];
    double lp_hi = echo_gain(DLYF_LP, 0, f, 0, fc * 8), lp_lo = echo_gain(DLYF_LP, 0, f, 0, fc / 8);
    double lp24_hi = echo_gain(DLYF_LP, 1, f, 0, fc * 8);
    double hp_lo = echo_gain(DLYF_HP, 0, f, 0, fc / 8), hp_hi = echo_gain(DLYF_HP, 0, f, 0, fc * 8);
    double hp24_lo = echo_gain(DLYF_HP, 1, f, 0, fc / 8);
    double bp_c = echo_gain(DLYF_BP, 0, f, 0, fc), bp_lo = echo_gain(DLYF_BP, 0, f, 0, fc / 8);
    double lp_c0 = echo_gain(DLYF_LP, 0, f, 0, fc), lp_c1 = echo_gain(DLYF_LP, 0, f, 127, fc);
    double pk_c0 = echo_gain(DLYF_PEAK, 0, f, 0, fc), pk_c1 = echo_gain(DLYF_PEAK, 0, f, 127, fc);
    double pk_lo = echo_gain(DLYF_PEAK, 0, f, 127, fc / 8);
    printf("delay_filter: dB  LP 12: 8fc %.1f fc/8 %.1f  LP 24: 8fc %.1f  HP 12: fc/8 %.1f 8fc %.1f  HP 24: fc/8 %.1f\n",
           db(lp_hi), db(lp_lo), db(lp24_hi), db(hp_lo), db(hp_hi), db(hp24_lo));
    printf("delay_filter: dB  BP: fc %.1f fc/8 %.1f  LP at fc: RES 0 %.1f RES 127 %.1f  PEAK at fc: RES 0 %.1f RES 127 %.1f, fc/8 %.1f\n",
           db(bp_c), db(bp_lo), db(lp_c0), db(lp_c1), db(pk_c0), db(pk_c1), db(pk_lo));
    check("LP 12 dB: the first echo filtered: 8 x the cutoff 30 dB down or more, 1/8 of it within 1 dB",
          db(lp_hi) < -30 && fabs(db(lp_lo)) < 1);
    check("LP 24 dB: 8 x the cutoff 25 dB further down than 12 dB", db(lp24_hi) < db(lp_hi) - 25);
    check("HP 12 dB: 1/8 of the cutoff 30 dB down or more, 8 x it within 1 dB", db(hp_lo) < -30 && fabs(db(hp_hi)) < 1);
    check("HP 24 dB: 1/8 of the cutoff 25 dB further down than 12 dB", db(hp24_lo) < db(hp_lo) - 25);
    check("BP: unity at the cutoff (within 1 dB), 1/8 of it 11 dB down or more (12 dB: 6 dB an octave a side)", fabs(db(bp_c)) < 1 && db(bp_lo) < -11);
    check("RES: LP at the cutoff lifted 10 dB or more at full RES (-6 dB at 0, +6 dB at 127)", db(lp_c1) > db(lp_c0) + 10);
    check("PEAK: unity at the cutoff at RES 0 (within 1 dB), +10 dB or more at full RES, unity away from it",
          fabs(db(pk_c0)) < 1 && db(pk_c1) > 10 && fabs(db(pk_lo)) < 1.5);
    {   /* the loop: FDBK 100 %, PEAK at full RES, loud noise for 1 s, then 4 s of nothing */
        static int32_t nz[FS];
        uint32_t i, b, rnd = 1u, rail = 0;
        dly_reset(DLYF_PEAK, 1, f, 127, 120);
        for (i = 0; i < FS; i++) {
            rnd = rnd * 1664525u + 1013904223u;
            nz[i] = (int32_t)(rnd >> 16) - 32768;
        }
        for (b = 0; b < 5u; b++) {
            bus(b ? NULL : nz, NULL, FS);
            for (i = 0; i < DLY_LEN; i++)
                rail |= dly_buf[i] >= 32767 || dly_buf[i] <= -32768;
        }
        check("a loop at FDBK 100 % with PEAK 24 dB at full RES and loud noise: never at the int16 rails (the saturator)",
              !rail);
        {   /* below 100 %: FDBK at its knee (102, 0.716 a pass): the cutoff's repeats fade as without the filter */
            uint32_t d;
            double g1 = 0, g2 = 0;
            dly_reset(DLYF_PEAK, 1, f, 127, 102);
            d = delay_samples();
            for (i = 0; i < RUN; i++)
                sig[i] = i < SN ? (int32_t)(8000.0 * sin(2.0 * M_PI * fc * i / FS)) : 0;
            {
                static int32_t w[4u * 6144u + SN];
                static int32_t s2[4u * 6144u + SN];
                memcpy(s2, sig, sizeof sig);
                bus(s2, w, sizeof w / sizeof w[0]);
                for (i = 1024; i < SN; i++)
                    g1 += (double)w[d + i] * w[d + i], g2 += (double)w[2u * d + i] * w[2u * d + i];
            }
            check("RES does not run the loop away: PEAK 24 dB at full RES, FDBK 102: the cutoff's 2nd repeat over the 1st "
                  "about 0.716 (within 0.1)", fabs(sqrt(g2 / g1) - 0.716) < 0.1);
            dly_reset(DLYF_PEAK, 1, f, 127, 120);
            bus(nz, NULL, FS);
        }
        song.g[G_DFDBK] = 60;
        for (b = 0; b < 20u && !(fx.dly_q >= DLY_LEN && !DLY_FST()); b++)
            bus(NULL, NULL, FS);
        {
            int32_t any = DLY_FST();
            for (i = 0; i < DLY_LEN; i++)
                any |= dly_buf[i];
            check("... FDBK lowered to 60: rings out to exactly 0 (line and filter) and goes idle", !any && fx.dly_q >= DLY_LEN);
        }
    }
}

static void dlyf(int t, int s, int r) { song.g[G_DFTYPE] = (int16_t)t, song.g[G_DFSLOPE] = (int16_t)s, song.g[G_DFRES] = (int16_t)r; }
static int dlyf_is(int t, int s, int r) { return song.g[G_DFTYPE] == t && song.g[G_DFSLOPE] == s && song.g[G_DFRES] == r; }

static void t_record(void)
{
    uint8_t b[FXR_MAX];
    uint32_t n;
    host_tracks_init();
    fxs_set(FXS_DEF);
    dlm_none();
    check("power-on: LP, 12 dB, RES 0, FREQ 107 (about 6 kHz: COLOR 70's cutoff)",
          dlyf_is(DLYF_LP, 0, 0) && GP[G_DFTYPE].def == DLYF_LP && GP[G_DCOLOR].def == 107);
    check("at the defaults: nothing to keep (no record)", fxr_encode(b) == 0u);
    dlyf(DLYF_HP, 1, 90);
    n = fxr_encode(b);
    check("HP 24 dB RES 90: a record with its TLV (0x45, TYPE SLOPE RES)",
          n == FXR_HEAD + 5u && b[FXR_HEAD] == 0x45 && b[FXR_HEAD + 1] == 3 && b[FXR_HEAD + 2] == DLYF_HP &&
              b[FXR_HEAD + 3] == 1 && b[FXR_HEAD + 4] == 90);
    dlyf(0, 0, 0);
    check("... decoded back (a load)", fxr_decode(b, n, 1) && dlyf_is(DLYF_HP, 1, 90));
    dlyf(DLYF_BP, 0, 5);
    check("... a song section (all 0): the filter playing kept", fxr_decode(b, n, 0) && dlyf_is(DLYF_BP, 0, 5));
    check("no record (a project from before): LP 12 dB RES 0", !fxr_decode(0, 0, 1) && dlyf_is(DLYF_LP, 0, 0));
    b[FXR_HEAD + 2] = 9;
    check("a TYPE out of range: clamped (PEAK)", fxr_decode(b, n, 1) && song.g[G_DFTYPE] == DLYF_PEAK);
    dlyf(0, 0, 0);
}

static void t_project(void)
{
    static project_t p;
    static dlrec_t d;
    host_tracks_init();
    song.g[G_DCOLOR] = 33;
    proj_capture(&p, &d);
    check("FREQ 33 kept as 33 + DLY_FREQ_MARK (161)", p.g[G_DCOLOR] == 161);
    song.g[G_DCOLOR] = 100;
    project_apply(&p, 0);
    check("... applied: FREQ 33", song.g[G_DCOLOR] == 33);
    p.g[G_DCOLOR] = 70;
    project_apply(&p, 0);
    check("an older project's COLOR 70 (a 6 kHz one-pole): FREQ 107", song.g[G_DCOLOR] == 107);
    p.g[G_DCOLOR] = 0;
    project_apply(&p, 0);
    check("... COLOR 0 (440 Hz): FREQ 54", song.g[G_DCOLOR] == 54);
    p.g[G_DCOLOR] = 127;
    project_apply(&p, 0);
    check("... COLOR 127 (open): FREQ 127", song.g[G_DCOLOR] == 127);
    {
        int ok = 1, c;
        for (c = 0; c < 128; c++) {
            double a = (2000.0 + 240.0 * c) / 32768.0, hz = -log(1.0 - a) * FS / (2.0 * M_PI);
            double want = 127.0 * log(hz / 30.0) / log(16000.0 / 30.0);
            want = want > 127 ? 127 : want;
            ok &= fabs(dly_freq_in(c) - want) <= 2.5;
        }
        check("every COLOR 0..127: the FREQ of its old cutoff within 2.5 steps", ok);
    }
    check("SLOOP 2.4's COLOR (sl24_export.c sl24_g_out): our FREQ 107 -> 70; an older project's COLOR 50 -> 50; FREQ under 54 -> 0",
          dly_color_out(128 + 107) == 70 && dly_color_out(50) == 50 && dly_color_out(128 + 20) == 0);
}

static void t_stores(void)
{
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    song.playing = 0, transport_req = 0;
    host_tracks_init();
    fxs_set(FXS_DEF);
    dlyf(DLYF_PEAK, 1, 77);
    song.g[G_DCOLOR] = 40;
    project_save(3);
    check("SAVE D with PEAK 24 dB RES 77 FREQ 40: saved", !strcmp(last_msg, "SAVED"));
    dlyf(0, 0, 0);
    song.g[G_DCOLOR] = 107;
    project_load(3);
    check("... LOAD D: PEAK 24 dB RES 77 FREQ 40", dlyf_is(DLYF_PEAK, 1, 77) && song.g[G_DCOLOR] == 40);
    dlyf(0, 0, 0);
    project_save(4);
    project_load(3);
    project_load(4);
    check("SAVE E at the defaults, LOAD D then E: LP 12 dB RES 0", dlyf_is(DLYF_LP, 0, 0));
}

int main(void)
{
    t_audio();
    t_record();
    t_project();
    t_stores();
    printf(bad ? "delay_filter: %d FAILED\n" : "delay_filter: all ok\n", bad);
    return bad != 0;
}
