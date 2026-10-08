/* SPDX-License-Identifier: GPL-3.0-only */
/* The generic FX slots (fx_slots.c) and their record (fx_rec.c, fx_rec_log.c) on a simulated NOR with the section log:
 *   layout   the default is DIST CHO DLY REV; a type a second time leaves its slot empty; an id this build lacks is
 *            kept, unheard
 *   audio    a type in no slot is not heard: its amounts are kept, and the mix is the one with those amounts at 0,
 *            sample for sample (synth parts' DIST and sends, the drum lanes' sends); the default layout is the mix of
 *            before (the goldens: tests/regress.c)
 *   record   the default writes nothing; a layout round trips; a newer version, a cut TLV: the defaults; an unknown
 *            TLV is skipped
 *   stores   PROJECT SAVE / LOAD keeps the layout; a section saved again without its FX record, or replaced behind
 *            its back, loads the default; a section stored while playing keeps it in the arena; a song section
 *            (proj_apply, not a load) keeps the layout; the autosave's; after a restart
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
    printf("fx_slots: %-92s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static void layout(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    const uint8_t s[4] = {(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d};
    fxs_set(s);
}
static int is_layout(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    return fxs_slot[0] == a && fxs_slot[1] == b && fxs_slot[2] == c && fxs_slot[3] == d;
}

/* ---- layout */
static void t_layout(void)
{
    check("the default: DIST CHO DLY REV, all four heard, the FX page's ids P_DIST P_CHOR P_DLY P_REV",
          is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV) && fxs_live == FXS_LIVE_DEF && fxs_amt(0) == P_DIST &&
          fxs_amt(1) == P_CHOR && fxs_amt(2) == P_DLY && fxs_amt(3) == P_REV);
    layout(FXT_REV, FXT_REV, FXT_DLY, FXT_REV);
    check("a type a second time: its slot empty (one instance per type)",
          is_layout(FXT_REV, FXT_NONE, FXT_DLY, FXT_NONE) && fxs_live == (FXT_BIT(FXT_REV) | FXT_BIT(FXT_DLY)) &&
          fxs_amt(1) == 0xFFu && fxs_lm[0] == 31u && fxs_lm[1] == 31u && fxs_lm[2] == 0u);
    layout(FXT_DIST, 200, FXT_NONE, FXT_CHO);
    check("an id this build lacks (a newer firmware's): kept in its slot, not heard, no amount",
          is_layout(FXT_DIST, 200, FXT_NONE, FXT_CHO) && fxs_live == (FXT_BIT(FXT_DIST) | FXT_BIT(FXT_CHO)) &&
          fxs_amt(1) == 0xFFu && fxs_lm[0] == 0u);
    fxs_set(FXS_DEF);
}

/* ---- audio: a 2-bar song (a pad, the drums) rendered in a child (every state fresh); its hash */
static uint64_t render(const uint8_t *lay, int dist, int cho, int dly, int rev, int lanes)
{
    int fd[2];
    uint64_t h = 0;
    if (pipe(fd))
        return 1;
    if (!fork()) {
        static const uint8_t CH[3] = {48, 55, 60}, K[1] = {36}, S[1] = {38};
        int32_t o[CTL * 2];
        uint32_t b, i;
        close(fd[0]);
        host_tracks_init();
        host_preset(&trk[0], 0, 0);
        trk[0].p[P_SLEN] = 16;
        TDRUM->p[P_SLEN] = 16;
        for (i = 0; i < 16u; i += 4u) {
            put_step(&trk[0], i, 3, CH, ST_NOTE, 0);
            put_step(TDRUM, i, 1, i % 8u ? S : K, ST_NOTE, 0);
        }
        trk[0].p[P_DIST] = (int16_t)dist, trk[0].p[P_CHOR] = (int16_t)cho;
        trk[0].p[P_DLY] = (int16_t)dly, trk[0].p[P_REV] = (int16_t)rev;
        for (i = 0; i < DRUM_LANES; i++)
            dsend[i] = dsend_word(lanes == 1 ? 20u : 0u, lanes ? 12u : 0u, lanes ? 9u : 0u);   /* (2: no REV) */
        fxs_set(lay);
        transport_req = 1;
        for (b = 0; b < 2u * 2u * FS / CTL; b++) {
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++)
                h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
        }
        if (write(fd[1], &h, sizeof h) != sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h)
        h = 2;
    close(fd[0]);
    wait(0);
    return h;
}
static void t_audio(void)
{
    static const uint8_t NOREV[4] = {FXT_DIST, FXT_CHO, FXT_DLY, FXT_NONE}, NODIST[4] = {FXT_NONE, FXT_CHO, FXT_DLY, FXT_REV};
    static const uint8_t NONE[4] = {FXT_NONE, FXT_NONE, FXT_NONE, FXT_NONE}, SWAP[4] = {FXT_REV, FXT_DLY, FXT_CHO, FXT_DIST};
    uint64_t full = render(FXS_DEF, 90, 60, 50, 80, 1);
    check("the mix with every amount up differs from the dry one (the test hears the FX)", full != render(FXS_DEF, 0, 0, 0, 0, 0));
    check("REV in no slot: the mix with REV at 0 on the part and every drum lane, sample for sample",
          render(NOREV, 90, 60, 50, 80, 1) == render(FXS_DEF, 90, 60, 50, 0, 2));
    check("DIST in no slot: the mix with DST 0, sample for sample", render(NODIST, 90, 60, 50, 80, 1) == render(FXS_DEF, 0, 60, 50, 80, 1));
    check("every slot empty: the dry mix, sample for sample (amounts and lane sends kept)", render(NONE, 90, 60, 50, 80, 1) == render(FXS_DEF, 0, 0, 0, 0, 0));
    check("the four types in another order: the same mix (the inserts' order is fixed, D4)", render(SWAP, 90, 60, 50, 80, 1) == full);
}

/* ---- the record */
static void t_record(void)
{
    uint8_t b[FXR_MAX], c[FXR_MAX];
    uint32_t n;
    fxs_set(FXS_DEF);
    check("the default layout: nothing to keep (no record)", fxr_encode(b) == 0u);
    layout(FXT_REV, FXT_NONE, FXT_DIST, FXT_DLY);
    n = fxr_encode(b);
    fxs_set(FXS_DEF);
    check("a layout: version 1, the four ids; decoded back the same", n == FXR_HEAD && b[0] == 1u && fxr_decode(b, n, 1) &&
          is_layout(FXT_REV, FXT_NONE, FXT_DIST, FXT_DLY));
    fxs_set(FXS_DEF);
    check("a song section (all 0) keeps the layout playing", fxr_decode(b, n, 0) && is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV));
    memcpy(c, b, n);
    c[n] = 77, c[n + 1u] = 3, c[n + 2u] = c[n + 3u] = c[n + 4u] = 9;   /* (a later firmware's type) */
    check("an unknown TLV: skipped, the layout read", fxr_decode(c, n + 5u, 1) && is_layout(FXT_REV, FXT_NONE, FXT_DIST, FXT_DLY));
    check("a TLV cut short: refused, the default layout", !fxr_decode(c, n + 3u, 1) && is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV));
    c[0] = 2;
    check("a newer version: ignored as a whole, the default layout", !fxr_decode(c, n, 1) && is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV));
}

/* ---- the stores */
static void tracks(uint32_t seed)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_SLEN] = (int16_t)(8 + seed % 8u);
}
static void t_stores(void)
{
    int ok;
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    song.playing = 0, transport_req = 0;
    tracks(1);
    layout(FXT_REV, FXT_DLY, FXT_NONE, FXT_DIST);
    project_save(3);
    ok = !strcmp(last_msg, "SAVED") && slg_has(FXR_ID0 + 3);
    tracks(2);
    fxs_set(FXS_DEF);
    project_load(3);
    check("SAVE D with a layout; LOAD it back: the same layout", ok && is_layout(FXT_REV, FXT_DLY, FXT_NONE, FXT_DIST));
    tracks(3);
    fxs_set(FXS_DEF);
    project_save(4);
    ok = !strcmp(last_msg, "SAVED") && !slg_has(FXR_ID0 + 4);
    layout(FXT_REV, FXT_NONE, FXT_NONE, FXT_NONE);
    project_load(4);
    check("a section in the default layout: no FX record; loading it gives the default", ok && is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV));
    tracks(1);
    layout(FXT_CHO, FXT_NONE, FXT_NONE, FXT_NONE);
    project_save(3);
    fxs_set(FXS_DEF);
    project_save(3);
    ok = !slg_has(FXR_ID0 + 3);
    layout(FXT_REV, FXT_NONE, FXT_NONE, FXT_NONE);
    project_load(3);
    check("D saved again in the default layout: its record cleared, D loads the default", ok && is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV));
    tracks(7);
    layout(FXT_DLY, FXT_NONE, FXT_NONE, FXT_NONE);
    project_save(5);
    {
        uint32_t n;
        tracks(8);
        n = sec_capture();
        slg_put(5, sec_rbuf, n, 0);                     /* (F's record replaced behind its FX record's back) */
        layout(FXT_REV, FXT_NONE, FXT_NONE, FXT_NONE);
        project_load(5);
        check("F's section record replaced without its FX record: the default, never another version's",
              is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV));
    }
    tracks(10);
    layout(FXT_NONE, FXT_REV, FXT_CHO, FXT_NONE);
    song.playing = 1;
    section_store(12);
    ok = sec_pend_has(12) && sec_pend_has(SEC_PEND_FX + 12);
    sec_boot();                                         /* (a warm reset: .noinit keeps the arena) */
    ok &= sec_pend_has(SEC_PEND_FX + 12);
    fxs_set(FXS_DEF);
    song.playing = 0;
    section_load(12);
    ok &= is_layout(FXT_NONE, FXT_REV, FXT_CHO, FXT_NONE);
    check("SAVE + key while playing: M's FX record waits in the arena with it, over a warm reset", ok);
    sections_write();
    ok = slg_has(12) && slg_has(FXR_ID0 + 12) && !sec_pend_has(12) && !sec_pend_has(SEC_PEND_FX + 12);
    fxs_set(FXS_DEF);
    project_load(12);
    check("... written when stopped: M and its FX record in the log; LOAD M: the same", ok && is_layout(FXT_NONE, FXT_REV, FXT_CHO, FXT_NONE));
    fxs_set(FXS_DEF);
    ok = section_cue(12);
    arrangement_apply(12);
    check("a live jump to M (a song section): the layout playing stays (the layout is the project's)", ok && is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV));
    {   /* the autosave's: id FXR_ID_AUTO, keyed by its project's sum */
        static project_t as;
        static dlrec_t ad;
        tracks(16);
        layout(FXT_REV, FXT_DIST, FXT_NONE, FXT_NONE);
        proj_capture(&as, &ad);
        ok = fxr_log_put(FXR_ID_AUTO, as.sum, &as, 0) == 0 && slg_has(FXR_ID_AUTO);
        fxs_set(FXS_DEF);
        fxr_for(&as, 1)->psum = 0;
        fxr_log_get(FXR_ID_AUTO, as.sum, &as);
        proj_apply(&as, &ad, 1);
        ok &= is_layout(FXT_REV, FXT_DIST, FXT_NONE, FXT_NONE);
        fxr_log_get(FXR_ID_AUTO, as.sum ^ 1u, &as);     /* another project's key: none */
        proj_apply(&as, &ad, 1);
        check("the autosave's FX record: written beside it, read back with it; another key: the default",
              ok && is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV));
        layout(FXT_REV, FXT_DIST, FXT_NONE, FXT_NONE);
        fxr_for(&as, 1)->psum = 0;                      /* (an old project: nothing bound to it) */
        proj_apply(&as, &ad, 1);
        check("an old project (no FX record): the default layout", is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV));
    }
    slg_boot();
    fxs_set(FXS_DEF);
    project_load(12);
    check("after a restart (the log scanned again): M loads its layout", is_layout(FXT_NONE, FXT_REV, FXT_CHO, FXT_NONE));
}

int main(void)
{
    t_layout();
    t_audio();
    t_record();
    t_stores();
    printf("fx_slots test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
