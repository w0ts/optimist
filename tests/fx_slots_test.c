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

#if FELUCCA_TRK_FILT
static int16_t r_tflt;                                  /* (the renders' track FILTER: part 1 and the drums) */
#endif
#if FELUCCA_MASTER_COMP
static int16_t r_cmp;                                   /* (the renders' COMP insert: part 1) */
#endif
static int16_t r_db[5], r_dbyp;                         /* (the renders' drum bus: DIST CHO DLY REV COMP; the drums' FX off) */
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
#if FELUCCA_TRK_FILT
        trk[0].p[P_TFLT] = r_tflt, TDRUM->p[P_TFLT] = (int16_t)(r_tflt / 2);
#endif
#if FELUCCA_MASTER_COMP
        trk[0].p[P_TCOMP] = r_cmp;
        TDRUM->p[P_TCOMP] = r_db[4];
#endif
        TDRUM->p[P_DIST] = r_db[0], TDRUM->p[P_CHOR] = r_db[1], TDRUM->p[P_DLY] = r_db[2], TDRUM->p[P_REV] = r_db[3];
        TDRUM->p[P_FXOFF] = r_dbyp;
        if (lay)
            fxs_set(lay);
        else
            fxs_auto();                                 /* (0: the layout of a project without an FX record) */
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

/* ---- FX > SLOTS: loading swaps, the page's list; a project without a record and a FILTER in use (D6) */
static void t_ui(void)
{
    int16_t *v;
    const param_desc_t *d;
    uint32_t k, ok = 1;
    fxs_set(FXS_DEF);
    fxs_load(0, FXT_REV);
    check("SLOTS: REV into S1 swaps with S4 (one instance per type, nothing lost)", is_layout(FXT_REV, FXT_CHO, FXT_DLY, FXT_DIST));
    fxs_load(1, FXT_NONE);
    fxs_load(3, FXT_CHO);
    check("... S2 emptied, CHO into S4: REV ---- DLY CHO (DIST in no slot, its amounts kept)",
          is_layout(FXT_REV, FXT_NONE, FXT_DLY, FXT_CHO) && !FXS_ON(FXT_DIST) && fxs_amt(1) == 0xFFu);
    for (k = 0; k < FX_NSLOT; k++) {
        d = fxs_desc(k, &v);
        ok &= d && d->fmt == F_ENUM && d->max == FXS_NLIST - 1 && fxs_list[*v] == fxs_slot[k];
    }
    ok &= !strcmp(fxs_names[0], "----") && !strcmp(fxs_names[1], "DST") && fxs_list[FXS_NLIST - 1] != FXT_NONE;
    check("... the page: each knob names its slot's type (the amounts' labels: DST CHO DLY REV ..., ---- empty)", ok);
    d = fxs_desc(2, &v);
    check("... S3's value the type's place in the list", !strcmp(d->names[*v], "DLY"));
    fxs_set(FXS_DEF);
#if FELUCCA_TRK_FILT
    {
        static const uint8_t FILT_CHO[4] = {FXT_DIST, FXT_FILT, FXT_DLY, FXT_REV};
        uint64_t a, b;
        r_tflt = -40;
        a = render(0, 90, 0, 50, 80, 0);
        b = render(FILT_CHO, 90, 0, 50, 80, 0);
        check("a project without an FX record, a FILTER in use and no CHO: FILT takes CHO's slot (the same mix)", a == b);
        a = render(0, 90, 60, 0, 80, 0);
        b = render((const uint8_t[4]){FXT_DIST, FXT_CHO, FXT_FILT, FXT_REV}, 90, 60, 0, 80, 0);
        check("... no DLY: FILT takes DLY's slot", a == b);
        check("... the filter is heard (not the mix without it)", a != (r_tflt = 0, render(0, 90, 60, 0, 80, 0)));
        r_tflt = -40;
        a = render(FXS_DEF, 90, 60, 50, 80, 0);
        r_tflt = 0;
        b = render(FXS_DEF, 90, 60, 50, 80, 0);
        check("... FILTER in no slot (the default layout), parts and drum bus at -40 / -20: the mix as at 0, sample for "
              "sample (D6: not heard, its amounts kept)", a == b);
        trk[0].p[P_TFLT] = 0, TDRUM->p[P_TFLT] = 0;
        fxs_auto();
        check("... no FILTER in use: the default layout", is_layout(FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV));
    }
#endif
}

#if FELUCCA_MASTER_COMP
/* ---- a note, silence, then a second note (rendered in a child, every state fresh); the hash of the second's first
 * second. lay1: the layout for the first note (COMP 127 on part 1), lay2 and amt2: for the silence and the second;
 * rest: part 1's COMP insert put at rest by hand before the second (the reference: the master's own states, its DC
 * blocker's remainder, keep the first note's history either way) */
static uint64_t render_gap(const uint8_t *lay1, const uint8_t *lay2, int16_t amt2, int rest)
{
    int fd[2];
    uint64_t h = 0;
    if (pipe(fd))
        return 1;
    if (!fork()) {
        int32_t o[CTL * 2];
        uint32_t b, i;
        close(fd[0]);
        host_tracks_init();
        host_preset(&trk[0], 0, 0);
        trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;   /* (no send: no tail to tell) */
        trk[0].p[P_TCOMP] = 127;
        fxs_set(lay1);
        trk_note_on(&trk[0], 48, 127);
        for (b = 0; b < FS / 2u / CTL; b++)             /* 0.5 s held: the reduction deep */
            mix_block(o, CTL);
        trk_note_off(&trk[0], 48);
        fxs_set(lay2);
        trk[0].p[P_TCOMP] = amt2;
        for (b = 0; b < 8u * FS / CTL; b++)             /* 8 s: the voice's release, then silence */
            mix_block(o, CTL);
        if (rest)
            tcomp[0].gr16 = tcomp[0].slow16 = 0, tcomp[0].g13 = 8192;
        trk_note_on(&trk[0], 48, 127);
        for (b = 0; b < FS / CTL; b++) {
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
#endif

/* ---- the COMP insert (phase 3): heard only in a slot, at 0 the mix as without, the reduction, the record */
static void t_comp(void)
{
#if FELUCCA_MASTER_COMP
    static const uint8_t CMP[4] = {FXT_DIST, FXT_CHO, FXT_DLY, FXT_COMP};
    uint64_t dry, h;
    uint8_t b[FXR_MAX];
    uint32_t n, i;
    int32_t x[CTL], pk = 0;
    mc_t c = {0, 0, 8192, 0};
    const param_desc_t *d;
    int16_t *v;
    uint32_t pg = NPAGES;
    for (i = 0; i < NPAGES; i++)
        if (!strcmp(PAGES[i].title, "CMP"))
            pg = i;
    fxs_set(FXS_DEF);
    check("FX > CMP: there, and hidden while no slot holds the COMP insert", pg < NPAGES && !page_shown(&PAGES[pg]));
    fxs_set(CMP);
    d = fxs_desc(FX_NSLOT + 2u, &v);
    check("... shown with COMP in S4; its knobs the master COMP's RATIO ATK REL lists, the inserts' own values",
          page_shown(&PAGES[pg]) && d == &GP[G_CREL] && v == &fxs_cset[2] && fxs_desc(FX_NSLOT, &v) == &GP[G_CRAT]);
    check("... and FX's S4 knob is the COMP amount (CMP, 0..127)", fxs_amt(3) == P_TCOMP && !strcmp(TP[P_TCOMP].label, "CMP"));
    fxs_set(FXS_DEF);
    r_cmp = 0;
    dry = render(CMP, 90, 60, 50, 0, 0);
    check("COMP loaded, every amount 0: the mix as with REV there and its sends 0, sample for sample",
          dry == render(FXS_DEF, 90, 60, 50, 0, 0));
    r_cmp = 120;
    h = render(FXS_DEF, 90, 60, 50, 0, 0);
    check("COMP in no slot, part 1 at 120: the mix as at 0, sample for sample", h == dry);
    check("COMP loaded, part 1 at 120: the mix changes", render(CMP, 90, 60, 50, 0, 0) != dry);
    r_cmp = 0;
    fxs_cset[0] = 7, fxs_cset[1] = 0, fxs_cset[2] = 2;   /* (the top ratio, the fastest attack) */
    for (n = 0; n < 200u; n++) {                       /* a square at -6 dB, 200 blocks: settled */
        for (i = 0; i < CTL; i++)
            x[i] = (i & 8u) ? 16384 : -16384;
        tcomp_run(&c, 127, x, x, CTL);
    }
    for (i = 0; i < CTL; i++)
        pk = x[i] > pk ? x[i] : pk;
    printf("fx_slots: COMP 127 on a -6 dB square: out %d (%.1f dB), GR %.1f dB\n", pk, 20.0 * log10(pk / 32767.0), c.gr16 * 6.0206 / 65536.0);
    check("COMP 127 (threshold -30 dB, the top ratio): a -6 dB square held near -30 dB + its make-up (-21 .. -13 dB)",
          pk > 2900 && pk < 7400);
    for (i = 0; i < 3000u; i++) {                      /* (2 s: REL 200 ms, from 24 dB) */
        int32_t y[CTL];
        memset(y, 0, sizeof y);
        tcomp_run(&c, 0, y, y, CTL);
    }
    check("... back at 0: it lets go, then rests (no reduction, unity gain)", c.gr16 == 0 && c.g13 == 8192);
    fxs_cset[0] = GP[G_CRAT].def, fxs_cset[1] = GP[G_CATK].def, fxs_cset[2] = GP[G_CREL].def;
    /* the part silent between the notes (the insert not on the mix): its state does not freeze (smoke F2) */
    check("COMP 127 on a note, then out of every slot in the silence: the next note with the insert at rest, sample for "
          "sample", render_gap(CMP, FXS_DEF, 127, 0) == render_gap(CMP, FXS_DEF, 127, 1));
    check("... its amount set to 0 in the silence (COMP still in S4): the same", render_gap(CMP, CMP, 0, 0) == render_gap(CMP, CMP, 0, 1));
    check("... at 0, the next note as with COMP in no slot", render_gap(CMP, CMP, 0, 1) == render_gap(CMP, FXS_DEF, 127, 1));
    check("... at 127 all along: after 8 s of silence it has let go (AUTO), the next note from rest",
          render_gap(CMP, CMP, 127, 0) == render_gap(CMP, CMP, 127, 1));
    /* the record: the parts' amounts and the settings (the drum bus's byte: reserved) */
    fxs_set(CMP);
    trk[0].p[P_TCOMP] = 33, trk[2].p[P_TCOMP] = 127, TDRUM->p[P_TCOMP] = 9, fxs_cset[2] = 3;
    n = fxr_encode(b);
    trk[0].p[P_TCOMP] = trk[2].p[P_TCOMP] = TDRUM->p[P_TCOMP] = 0, fxs_cset[2] = 6;
    fxs_set(FXS_DEF);
    check("the record: COMP's amounts (the parts') and REL back with a load", fxr_decode(b, n, 1) && trk[0].p[P_TCOMP] == 33 &&
          trk[1].p[P_TCOMP] == 0 && trk[2].p[P_TCOMP] == 127 && fxs_cset[2] == 3 && FXS_ON(FXT_COMP));
    check("... the drum bus's amount (its COMP insert: fx.c dbus_run) first in the TLV, back with a load",
          TDRUM->p[P_TCOMP] == 9 && b[FXR_HEAD] == FXT_COMP && b[FXR_HEAD + 2u] == 9);
    b[FXR_HEAD + 2u] = 0;
    check("... (a record of phase 3, its drum byte written 0: the drum bus's COMP at 0)", fxr_decode(b, n, 1) &&
          TDRUM->p[P_TCOMP] == 0 && trk[2].p[P_TCOMP] == 127);
    fxs_cset[2] = 6;
    check("... a song section: the amounts, not the settings nor the layout", fxr_decode(b, n, 0) && trk[2].p[P_TCOMP] == 127 && fxs_cset[2] == 6);
    trk[0].p[P_TCOMP] = trk[2].p[P_TCOMP] = TDRUM->p[P_TCOMP] = 0;
    fxs_cset[2] = GP[G_CREL].def;
    trk[2].p[P_TCOMP] = 50;
    fxr_decode(0, 0, 0);
    check("... a project without a record: every amount 0", trk[2].p[P_TCOMP] == 0);
    fxs_set(FXS_DEF);
#endif
}

/* ---- the drum bus (design phase 4: D5, D7): its inserts and its own sends heard only in a slot, with the drums' FX on;
 * at 0 the mix of before; COMP's gain on every channel; the sends from the bus's mid; the record */
static void t_dbus(void)
{
    static const uint8_t NOREV[4] = {FXT_DIST, FXT_CHO, FXT_DLY, FXT_NONE}, NODIST[4] = {FXT_NONE, FXT_CHO, FXT_DLY, FXT_REV};
    static const uint8_t SWAP[4] = {FXT_REV, FXT_DLY, FXT_CHO, FXT_DIST}, NONE[4] = {FXT_NONE, FXT_NONE, FXT_NONE, FXT_NONE};
    static const int16_t ALL[5] = {100, 70, 60, 90, 127};
    uint64_t dry, h, base;
    uint8_t b[FXR_MAX];
    uint32_t n, i, k;
    memset(r_db, 0, sizeof r_db);
    base = render(FXS_DEF, 90, 60, 50, 80, 1);
    dry = render(FXS_DEF, 0, 0, 0, 0, 0);
    for (k = 0; k < 4u; k++) {                          /* each of DIST CHO DLY REV on the drum track alone: heard */
        static const char *const NM[4] = {"DIST", "CHO", "DLY", "REV"};
        char m[120];
        memset(r_db, 0, sizeof r_db);
        r_db[k] = ALL[k];
        h = render(FXS_DEF, 90, 60, 50, 80, 1);
        snprintf(m, sizeof m, "the drum track's %s (%d): heard (the mix changes)", NM[k], ALL[k]);
        check(m, h != base);
    }
    memset(r_db, 0, sizeof r_db);
    r_db[3] = 90;
    h = render(NOREV, 90, 60, 50, 80, 1);
    r_db[3] = 0;
    check("the drum track's REV with REV in no slot: the mix as without it, sample for sample", h == render(NOREV, 90, 60, 50, 80, 1));
    r_db[0] = 100;
    h = render(NODIST, 0, 60, 50, 80, 1);
    r_db[0] = 0;
    check("the drum track's DIST with DIST in no slot: the mix as at 0", h == render(NODIST, 0, 60, 50, 80, 1));
    memcpy(r_db, ALL, sizeof r_db);
    check("every drum bus amount up, every slot empty: the dry mix, sample for sample", render(NONE, 90, 60, 50, 80, 1) == dry);
    h = render(FXS_DEF, 90, 60, 50, 80, 1);
    check("... the four types in another order: the same mix (the drum bus's inserts in a fixed order, D4)",
          render(SWAP, 90, 60, 50, 80, 1) == h);
    r_dbyp = 1;
    h = render(FXS_DEF, 90, 60, 50, 80, 1);
    memset(r_db, 0, sizeof r_db);
    check("the drums' FX off (P_FXOFF): every drum bus amount unheard (the mix as with them at 0)",
          h == render(FXS_DEF, 90, 60, 50, 80, 1));
    r_dbyp = 0;
#if FELUCCA_MASTER_COMP
    {
        static const uint8_t CMP[4] = {FXT_DIST, FXT_CHO, FXT_DLY, FXT_COMP};
        int32_t gl, gs;
        uint64_t h0;
        r_db[4] = 127;
        h = render(CMP, 90, 60, 50, 0, 1);
        h0 = render(FXS_DEF, 90, 60, 50, 0, 1);
        r_db[4] = 0;
        check("the drum track's COMP (127, COMP in S4): heard", h != render(CMP, 90, 60, 50, 0, 1));
        check("... in no slot: the mix as at 0", h0 == render(FXS_DEF, 90, 60, 50, 0, 1));
        /* the gain on every channel: a loud bus and an echo send under it, settled; both take the same gain */
        fxs_set(CMP);
        TDRUM->p[P_TCOMP] = 127;
        for (n = 0; n < 300u; n++) {
            for (i = 0; i < CTL; i++) {
                mix_l[i] = mix_r[i] = (i & 8u) ? 20000 : -20000;
                send_r[i] = (i & 8u) ? 8000 : -8000, send_d[i] = send_c[i] = 0;
            }
            dbus_run(CTL);
        }
        gl = mix_l[CTL - 1u] < 0 ? -mix_l[CTL - 1u] : mix_l[CTL - 1u];
        gs = send_r[CTL - 1u] < 0 ? -send_r[CTL - 1u] : send_r[CTL - 1u];
        printf("fx_slots: drum bus COMP 127: L %d of 20000, REV send %d of 8000\n", gl, gs);
        check("... the bus and its sends take one gain (stereo-linked, D7): L and the REV send down alike, the empty "
              "sends stay 0", gl < 12000 && gs * 5 >= gl * 2 - 5 && gs * 5 <= gl * 2 + 5 && !send_d[3] && !send_c[3]);
        TDRUM->p[P_TCOMP] = 0;
        for (n = 0; n < 3000u; n++) {
            memset(mix_l, 0, sizeof mix_l), memset(mix_r, 0, sizeof mix_r), memset(send_r, 0, sizeof send_r);
            dbus_run(CTL);
        }
        check("... back at 0: it lets go, then rests", tcomp_rest(&dbus_comp));
        fxs_set(FXS_DEF);
    }
#endif
    {   /* the drum track's sends: from the bus's mid, ramped over a block */
        for (i = 0; i < CTL; i++)
            mix_l[i] = 3000, mix_r[i] = 1000, send_r[i] = send_d[i] = send_c[i] = 0;
        TDRUM->p[P_REV] = 127;
        dbus_run(CTL);
        for (i = 0; i < CTL; i++)
            send_r[i] = 0;
        dbus_run(CTL);
        check("the drum track's REV send at 127: the bus's mid ((L + R) / 2), as a drum sound's own at its top",
              send_r[0] == mulq15(2000, 127 * 258) && send_r[CTL - 1u] == send_r[0] && !send_d[0] && !send_c[0]);
        TDRUM->p[P_REV] = 0;
        dbus_run(CTL);
        dbus_run(CTL);
        check("... back to 0: no send left (dbus_snd 0)", !dbus_snd[2]);
    }
    /* the record: each type's drum bus byte; a section takes them; none: 0; project_t keeps 0 there */
    fxs_set(FXS_DEF);
    TDRUM->p[P_DIST] = 11, TDRUM->p[P_CHOR] = 22, TDRUM->p[P_DLY] = 33, TDRUM->p[P_REV] = 44;
#if FELUCCA_MASTER_COMP
    TDRUM->p[P_TCOMP] = 55;
#endif
    n = fxr_encode(b);
    TDRUM->p[P_DIST] = TDRUM->p[P_CHOR] = TDRUM->p[P_DLY] = TDRUM->p[P_REV] = 0;
#if FELUCCA_MASTER_COMP
    TDRUM->p[P_TCOMP] = 0;
#endif
    check("the record: the default layout with drum bus amounts writes them (a TLV a type), back with a section",
          n > FXR_HEAD && fxr_decode(b, n, 0) && TDRUM->p[P_DIST] == 11 && TDRUM->p[P_CHOR] == 22 && TDRUM->p[P_DLY] == 33 &&
          TDRUM->p[P_REV] == 44
#if FELUCCA_MASTER_COMP
          && TDRUM->p[P_TCOMP] == 55
#endif
    );
    fxr_decode(0, 0, 1);
    check("... a project without a record: the drum bus's amounts 0 (an old project's stale drum slots never heard)",
          !TDRUM->p[P_DIST] && !TDRUM->p[P_CHOR] && !TDRUM->p[P_DLY] && !TDRUM->p[P_REV]);
    {
        static project_t pj;
        static dlrec_t dd;
        host_tracks_init();
        TDRUM->p[P_REV] = 70, TDRUM->p[P_DIST] = 5;
        proj_capture(&pj, &dd);
        TDRUM->p[P_REV] = TDRUM->p[P_DIST] = 0;
        check("PROJECT capture: project_t's drum slots P_DIST..P_REV stay 0; the amounts come back from the FX record",
              !pj.t[TRK_DRUM].p[P_REV] && !pj.t[TRK_DRUM].p[P_DIST] && (proj_apply(&pj, &dd, 1), TDRUM->p[P_REV] == 70) &&
              TDRUM->p[P_DIST] == 5);
        TDRUM->p[P_REV] = TDRUM->p[P_DIST] = 0;
    }
    fxs_set(FXS_DEF);
}

int main(void)
{
    t_layout();
    t_ui();
    t_comp();
    t_audio();
    t_dbus();
    t_record();
    t_stores();
    printf("fx_slots test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
