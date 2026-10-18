/* SPDX-License-Identifier: GPL-3.0-only */
/* The generic FX slots (fx_slots.c) and their record (fx_rec.c, fx_rec_log.c) on a simulated NOR with the section log,
 * since the per-track insert slots (2026-10-12):
 *   master   four slots for the SENDS only, default ---- CHO DLY REV; an insert id there: empty; a type a second time
 *            leaves its slot empty; an id this build lacks is kept, unheard
 *   tracks   two insert slots each (T1 T2 T3 DR), default S1 DIST, S2 FILTER (built); one type once a track: picking
 *            the other slot's swaps them; the amounts are the types' (P_DIST P_TFLT P_TCOMP)
 *   audio    a type not heard on a track (an insert in none of its slots, a send in no master slot) is the mix with its
 *            amount at 0, sample for sample; the slots' order is the order heard (S1 then S2: each the kernel run by
 *            hand); the default slots are the mix of before (the goldens: tests/regress.c); each track's COMP its own
 *            settings; the drum sounds' DST CMP always heard; the drum track's send levels (100 %: the mix of before)
 *   record   the default writes nothing; a layout round trips; the track slots' TLV 0x42 only when the read-back's
 *            mapping would not give them; an old record or none: the mapping (rules 1..8 of the design); a newer
 *            version, a cut TLV: the defaults; an unknown TLV is skipped
 *   stores   PROJECT SAVE / LOAD keeps the slots; a section saved again without its FX record, or replaced behind its
 *            back, loads the default; a section stored while playing keeps it in the arena; a song section (proj_apply,
 *            not a load) keeps the slots playing; the autosave's; after a restart
 * Run by tests/run_tests.sh (with and without TRK_FILT). */
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
typedef uint8_t ins_t[NTRK][INS_N];
#define D0 INS_DEF0
#define D1 INS_DEF1
static const ins_t INS_D = {{D0, D1}, {D0, D1}, {D0, D1}, {D0, D1}};
static void ins_all(const ins_t s)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        ins_put(k, s[k]);
}
static int ins_is(uint32_t k, uint32_t a, uint32_t b) { return ins_slot[k][0] == a && ins_slot[k][1] == b; }
static int ins_dflt(void) { return !memcmp(ins_slot, INS_D, sizeof INS_D); }

/* ---- the master slots: the sends */
static void t_layout(void)
{
    check("the default: ---- REV DLY CHO (the one order), the three sends heard, SENDS' ids P_REV P_DLY P_CHOR in slot "
          "order, the cards A B C REV DLY CHO",
          is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO) && fxs_live == FXS_LIVE_DEF && fxs_amt(0) == 0xFFu &&
          fxs_send_amt(0) == P_REV && fxs_send_amt(1) == P_DLY && fxs_send_amt(2) == P_CHOR && fxs_send_amt(3) == 0xFFu &&
          fxs_card(0) == FXT_REV && fxs_card(1) == FXT_DLY && fxs_card(2) == FXT_CHO && fxs_card(3) == FXT_NONE &&
          fxs_line[1] == 2u && fxs_line[2] == 1u && fxs_line[3] == 0u && fxs_rsl[0] == 3u && fxs_rsl[1] == 2u && fxs_rsl[2] == 1u);
    layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_REV);
    check("a type a second time: its slot empty (one instance per type)",
          is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_NONE) && fxs_live == (FXT_BIT(FXT_REV) | FXT_BIT(FXT_DLY)) &&
          fxs_amt(3) == 0xFFu && fxs_lm[0] == 31u && fxs_lm[1] == 31u && fxs_lm[2] == 0u);
    layout(FXT_DIST, 200, FXT_COMP, FXT_CHO);
    check("an insert id (an old record's DIST, COMP): empty (the tracks' slots hold the inserts); an id this build lacks "
          "(a newer firmware's): kept, not heard", is_layout(FXT_NONE, 200, FXT_NONE, FXT_CHO) &&
          fxs_live == FXT_BIT(FXT_CHO) && fxs_lm[0] == 0u);
    fxs_set(FXS_DEF);
    fxs_load(0, FXT_DIST);
    check("FX > SLOTS: an insert refused", is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO));
    fxs_load(0, FXT_REV);
    check("the master's 3 returns (fx-rework): slot 0 takes no send", is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO));
    fxs_load(1, FXT_CHO);
    check("... CHO into A swaps with C (nothing lost), the SENDS cards follow the returns (CHO DLY REV)",
          is_layout(FXT_NONE, FXT_CHO, FXT_DLY, FXT_REV) && fxs_send_amt(0) == P_CHOR && fxs_send_amt(2) == P_REV &&
          fxs_card(0) == FXT_CHO && fxs_card(2) == FXT_REV);
    fxs_load(3, FXT_NONE);
    check("... C emptied: its card blank, the others in place", fxs_card(0) == FXT_CHO && fxs_card(1) == FXT_DLY &&
          fxs_card(2) == FXT_NONE && !FXS_ON(FXT_REV));
    layout(FXT_REV, FXT_NONE, FXT_DLY, FXT_CHO);
    check("... a layout's send in slot 0: into the first empty return (REV on A)",
          is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO) && FXS_ON(FXT_REV));
    {   /* FX > SLOTS lists ---- and the sends in the one order first */
        int16_t *v;
        const param_desc_t *d = fxs_desc(1, &v);
        check("FX > SLOTS: ---- REV DLY CHO first, A's value REV", d && fxs_list[0] == FXT_NONE && fxs_list[1] == FXT_REV &&
              fxs_list[2] == FXT_DLY && fxs_list[3] == FXT_CHO && *v == 1);
    }
    fxs_set(FXS_DEF);
}

/* ---- each track's two slots */
static void t_ins(void)
{
    int16_t *v;
    const param_desc_t *d;
    uint32_t k, ok = 1;
    for (k = 0; k < NTRK; k++)
        ok &= ins_is(k, FELUCCA_FX_DIST ? FXT_DIST : FXT_NONE, TFLT_ON ? FXT_FILT : FXT_NONE);
    check("the default on every track: S1 DIST, S2 FILTER (built: else empty) (D5)", ok && ins_has(0, FXT_DIST) == 1u &&
          ins_has(3, FXT_FILT) == (TFLT_ON ? 2u : 0u) && ins_has(1, FXT_COMP) == 0u && ins_amt(2, 0) == P_DIST);
#if FELUCCA_MASTER_COMP
    ins_load(1, 1, FXT_COMP);
    check("COMP into T2's S2: T2 S1 DIST S2 COMP, the others as they were; its amount P_TCOMP (DYN)",
          ins_is(1, FXT_DIST, FXT_COMP) && ins_amt(1, 1) == P_TCOMP && ins_has(1, FXT_COMP) == 2u && ins_dflt() == 0 &&
          ins_is(0, D0, D1) && !strcmp(TP[ins_amt(1, 1)].label, "DYN"));
    ins_load(1, 0, FXT_COMP);
    check("... COMP into S1: the other slot's type, so the two swap (D3): S1 COMP, S2 DIST", ins_is(1, FXT_COMP, FXT_DIST));
    ins_load(1, 1, FXT_NONE);
    check("... S2 emptied: DIST in no slot of T2 (its amount kept, unheard)", ins_is(1, FXT_COMP, FXT_NONE) &&
          !INS_ON(1, FXT_DIST) && ins_amt(1, 1) == 0xFFu);
#endif
    {
        static const uint8_t TWICE[2] = {FXT_DIST, FXT_DIST}, SEND[2] = {FXT_REV, 200};
        ins_put(2, TWICE);
        ok = ins_is(2, FXT_DIST, FXT_NONE);
        ins_put(2, SEND);
        check("ins_put: the same type twice: S2 empty; a send or an unknown id: empty", ok && ins_is(2, FXT_NONE, FXT_NONE));
    }
    ins_all(INS_D);
    song.sel = 1;
    d = fxs_desc(FXS_ID_INS, &v);
    ok = d == &INS_DESC[0] && d->fmt == F_ENUM && d->max == INS_NLIST - 1 && ins_list[*v] == ins_slot[1][0] &&
         !strcmp(ins_names[0], "----");
    for (k = 1; k < INS_NLIST; k++)
        ok &= (FXT_INSERT >> ins_list[k] & 1u) && !strcmp(ins_names[k], TP[FXT_AMT[ins_list[k]]].label);
    check("INSERTS' TYPE: a list of ---- and the inserts built, named by their amounts (DST FILT CMP), at the slot's", ok);
    d = fxs_desc(0, &v);
    ok = d == &FXS_DESC[0];
    for (k = 1; k <= (uint32_t)d->max; k++)
        ok &= FXT_SEND_OK(fxs_list[k]) || (RT_OK >> fxs_list[k] & 1u);
    check("FX > SLOTS' list (fx-pool): ----, the sends and every type a return may hold (no GATER)",
          ok && d->max == FELUCCA_FX_CHORUS + FELUCCA_FX_DELAY + FELUCCA_FX_REVERB + __builtin_popcount(RT_OK));
    song.sel = 0;
}

#if TFLT_ON
static int16_t r_tflt;                                  /* (the renders' track FILTER: part 1 and the drums) */
#endif
#if FELUCCA_MASTER_COMP
static int16_t r_cmp;                                   /* (the renders' COMP insert: part 1) */
#endif
static int16_t r_db[5], r_dbyp, r_kit[3] = {127, 127, 127};   /* (the drum bus: DIST CHO DLY REV COMP; FX off; the levels) */
static uint8_t r_dl[2], r_dslc, r_slc;                  /* (the drum sounds' DIST, COMP; the drum SLICER, part 1's) */
/* ---- audio: a 2-bar song (a pad, the drums) rendered in a child (every state fresh); its hash. lay: the master layout,
 * ins: the tracks' slots (0: the default) */
static uint64_t render(const uint8_t *lay, const ins_t ins, int dist, int cho, int dly, int rev, int lanes)
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
#if FELUCCA_MASTER_COMP
        int16_t cs[NTRK][3];
        for (i = 0; i < NTRK; i++)                     /* (the COMP settings the test set: kept; track values since 2026-10-18) */
            memcpy(cs[i], INS_CSET(i), sizeof cs[i]);
        host_tracks_init();
        for (i = 0; i < NTRK; i++)
            memcpy(INS_CSET(i), cs[i], sizeof cs[i]);
#else
        host_tracks_init();
#endif
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
#if TFLT_ON
        trk[0].p[P_TFLT] = r_tflt, TDRUM->p[P_TFLT] = (int16_t)(r_tflt / 2);
#endif
#if FELUCCA_MASTER_COMP
        trk[0].p[P_TCOMP] = r_cmp;
        TDRUM->p[P_TCOMP] = r_db[4];
#endif
        TDRUM->p[P_DIST] = r_db[0], TDRUM->p[P_CHOR] = r_db[1], TDRUM->p[P_DLY] = r_db[2], TDRUM->p[P_REV] = r_db[3];
        TDRUM->p[P_FXOFF] = r_dbyp;
        memset(dins_amt[0], r_dl[0], sizeof dins_amt[0]);
        memset(dins_amt[1], r_dl[1], sizeof dins_amt[1]);
        for (i = 0; i < 3u; i++)
            dsend_kit[i] = r_kit[i];
        if (r_dslc)
            TDRUM->p[P_SLCR] = (int16_t)r_dslc;
        if (r_slc)
            trk[0].p[P_SLCR] = (int16_t)r_slc;
        fxs_set(lay);
        ins_all(ins ? ins : INS_D);
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
static const ins_t INS_NONE = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
static void t_audio(void)
{
    static const uint8_t NOREV[4] = {FXT_NONE, FXT_CHO, FXT_DLY, FXT_NONE}, NONE[4] = {FXT_NONE, FXT_NONE, FXT_NONE, FXT_NONE};
    static const uint8_t SWAP[4] = {FXT_REV, FXT_DLY, FXT_CHO, FXT_NONE};
    static const ins_t NODIST1 = {{FXT_NONE, D1}, {D0, D1}, {D0, D1}, {D0, D1}};
    uint64_t full = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    check("the mix with every amount up differs from the dry one (the test hears the FX)", full != render(FXS_DEF, 0, 0, 0, 0, 0, 0));
    check("REV in no master slot: the mix with REV at 0 on the part and every drum lane, sample for sample",
          render(NOREV, 0, 90, 60, 50, 80, 1) == render(FXS_DEF, 0, 90, 60, 50, 0, 2));
    check("DIST in none of T1's slots: the mix with its DST 0, sample for sample",
          render(FXS_DEF, NODIST1, 90, 60, 50, 80, 1) == render(FXS_DEF, 0, 0, 60, 50, 80, 1));
    check("every master slot and track slot empty: the dry mix, sample for sample (amounts and lane sends kept)",
          render(NONE, INS_NONE, 90, 60, 50, 80, 1) == render(FXS_DEF, 0, 0, 0, 0, 0, 0));
    check("the sends in another master order: the same mix", render(SWAP, 0, 90, 60, 50, 80, 1) == full);
    {   /* the SLICER first (D2): with it off, DIST's place does not matter; with it on and DIST at 0, nothing moves */
        r_slc = 1;
        check("part 1's SLICER on, DST 0: the mix as without DIST in a slot (the SLICER then the inserts)",
              render(FXS_DEF, 0, 0, 60, 50, 80, 1) == render(FXS_DEF, NODIST1, 0, 60, 50, 80, 1));
        check("... DST 90 heard after the gate (the mix changes)", render(FXS_DEF, 0, 90, 60, 50, 80, 1) != render(FXS_DEF, 0, 0, 60, 50, 80, 1));
        r_slc = 0;
    }
#if TFLT_ON
    {
        static const ins_t FD = {{FXT_FILT, FXT_DIST}, {D0, D1}, {D0, D1}, {D0, D1}};
        uint64_t a, b;
        r_tflt = -40;
        a = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
        b = render(FXS_DEF, FD, 90, 60, 50, 80, 1);
        check("T1 S1 DIST S2 FILTER against S1 FILTER S2 DIST (DST 90, FILT -40): the order heard (the mixes differ)", a != b);
        {
            static const ins_t NOF = {{FXT_DIST, FXT_NONE}, {D0, D1}, {D0, D1}, {FXT_DIST, FXT_NONE}};
            a = render(FXS_DEF, NOF, 90, 60, 50, 80, 1);
            r_tflt = 0;
            check("... FILTER in no slot of T1 nor DR (-40, -20): the mix as at 0, sample for sample",
                  a == render(FXS_DEF, 0, 90, 60, 50, 80, 1));
        }
        r_tflt = 0;
        check("... FILTER at 0: either order the same mix (bypassed)", render(FXS_DEF, 0, 90, 60, 50, 80, 1) == render(FXS_DEF, FD, 90, 60, 50, 80, 1));
    }
    {   /* each order is the two kernels run by hand in that order (fx.c ins_part: dist_run, tflt_part) */
        static int32_t b1[CTL], b2[CTL];
        static const uint8_t DF[2] = {FXT_DIST, FXT_FILT}, FDs[2] = {FXT_FILT, FXT_DIST};
        track_t *t = &trk[0];
        dist_t d0;
        tflt_t f0;
        uint32_t i, r, ok = 1;
        host_tracks_init();
        t->p[P_DIST] = 100, t->p[P_TFLT] = -30;
        for (r = 0; r < 2u; r++) {
            ins_put(0, r ? FDs : DF);
            memset(&t->dist, 0, sizeof t->dist), memset(&tflt[0], 0, sizeof tflt[0]);
            for (i = 0; i < 40u; i++) {                 /* (40 blocks: the filter's glide in, the follower settled) */
                uint32_t j;
                for (j = 0; j < CTL; j++)
                    b1[j] = b2[j] = (int32_t)((j * 2654435761u + i * 40503u) >> 14) - 131072;
                d0 = t->dist, f0 = tflt[0];
                ins_part(t, b1, CTL);
                t->dist = d0, tflt[0] = f0;
                if (!r)
                    dist_run(&t->dist, 100, b2, CTL), tflt_part(t, -30, b2, CTL);
                else
                    tflt_part(t, -30, b2, CTL), dist_run(&t->dist, 100, b2, CTL);
                ok &= !memcmp(b1, b2, sizeof b1);
            }
        }
        check("S1 DIST S2 FILTER, and S1 FILTER S2 DIST: each the two kernels run by hand in that order, sample for sample", ok);
        ins_all(INS_D);
    }
#endif
}

#if FELUCCA_MASTER_COMP
/* ---- a note, silence, then a second note (rendered in a child, every state fresh); the hash of the second's first
 * second. s1: part 1's slots for the first note (COMP 127), s2 and amt2: for the silence and the second; rest: part 1's
 * COMP insert put at rest by hand before the second */
static uint64_t render_gap(const uint8_t *s1, const uint8_t *s2, int16_t amt2, int rest)
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
        ins_put(0, s1);
        trk_note_on(&trk[0], 48, 127);
        for (b = 0; b < FS / 2u / CTL; b++)             /* 0.5 s held: the reduction deep */
            mix_block(o, CTL);
        trk_note_off(&trk[0], 48);
        ins_put(0, s2);
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

/* ---- the COMP insert: heard only in a track's slot, each track's own RATIO ATK REL (D6), at 0 the mix as without, the
 * reduction, the record */
static void t_comp(void)
{
#if FELUCCA_MASTER_COMP
    static const ins_t CMP = {{FXT_DIST, FXT_COMP}, {D0, D1}, {D0, D1}, {D0, D1}};
    static const uint8_t SC[2] = {FXT_DIST, FXT_COMP}, SD[2] = {FXT_DIST, FXT_NONE};
    uint64_t dry, h;
    uint8_t b[FXR_MAX];
    uint32_t n, i;
    int32_t x[CTL], pk = 0;
    mc_t c = {0, 0, 8192, 0};
    const param_desc_t *d;
    int16_t *v;
    uint32_t pg = NPAGES;
    for (i = 0; i < NPAGES; i++)
        if (!strcmp(PAGES[i].title, "DYN"))
            pg = i;
    song.sel = 0;
    check("FX > CMP: there, and hidden while none of the selected track's slots holds the COMP", pg < NPAGES && !page_shown(&PAGES[pg]));
    ins_all(CMP);
    d = fxs_desc(FX_NSLOT + 2u, &v);
    check("... shown on T1 (COMP in its S2); its knobs the master COMP's RATIO ATK REL lists, T1's own values",
          page_shown(&PAGES[pg]) && d == &GP[G_CREL] && v == &INS_CSET(0)[2] && fxs_desc(FX_NSLOT, &v) == &GP[G_CRAT]);
    song.sel = 1;
    d = fxs_desc(FX_NSLOT + 2u, &v);
    check("... T2 selected (no COMP): hidden; its own values", !page_shown(&PAGES[pg]) && v == &INS_CSET(1)[2]);
    song.sel = 0;
    ins_all(INS_D);
    r_cmp = 0;
    dry = render(FXS_DEF, CMP, 90, 60, 50, 0, 0);
    check("COMP in T1's S2, every amount 0: the mix as without it, sample for sample", dry == render(FXS_DEF, 0, 90, 60, 50, 0, 0));
    r_cmp = 120;
    h = render(FXS_DEF, 0, 90, 60, 50, 0, 0);
    check("COMP in no slot of T1, its amount 120: the mix as at 0, sample for sample", h == dry);
    h = render(FXS_DEF, CMP, 90, 60, 50, 0, 0);
    check("COMP in T1's S2 at 120: the mix changes", h != dry);
    INS_CSET(1)[0] = 7;                                 /* (T2's own ratio: T1's sound unchanged) */
    check("... T2's RATIO changed (D6: each track's own): T1's mix the same", render(FXS_DEF, CMP, 90, 60, 50, 0, 0) == h);
    INS_CSET(1)[0] = GP[G_CRAT].def;
    INS_CSET(0)[0] = 7;
    check("... T1's RATIO changed: T1's mix changes", render(FXS_DEF, CMP, 90, 60, 50, 0, 0) != h);
    INS_CSET(0)[0] = GP[G_CRAT].def;
    r_cmp = 0;
    INS_CSET(0)[0] = 7, INS_CSET(0)[1] = 0, INS_CSET(0)[2] = 2;   /* (the top ratio, the fastest attack) */
    for (n = 0; n < 200u; n++) {                       /* a square at -6 dB, 200 blocks: settled */
        for (i = 0; i < CTL; i++)
            x[i] = (i & 8u) ? 16384 : -16384;
        tcomp_run(&c, 127, x, x, CTL, 0);
    }
    for (i = 0; i < CTL; i++)
        pk = x[i] > pk ? x[i] : pk;
    printf("fx_slots: COMP 127 on a -6 dB square: out %d (%.1f dB), GR %.1f dB\n", pk, 20.0 * log10(pk / 32767.0), c.gr16 * 6.0206 / 65536.0);
    check("COMP 127 (threshold -30 dB, the top ratio): a -6 dB square held near -30 dB + its make-up (-21 .. -13 dB)",
          pk > 2900 && pk < 7400);
    for (i = 0; i < 3000u; i++) {                      /* (2 s: REL 200 ms, from 24 dB) */
        int32_t y[CTL];
        memset(y, 0, sizeof y);
        tcomp_run(&c, 0, y, y, CTL, 0);
    }
    check("... back at 0: it lets go, then rests (no reduction, unity gain)", c.gr16 == 0 && c.g13 == 8192);
    INS_CSET(0)[0] = GP[G_CRAT].def, INS_CSET(0)[1] = GP[G_CATK].def, INS_CSET(0)[2] = GP[G_CREL].def;
    check("COMP 127 on a note, then out of T1's slots in the silence: the next note with the insert at rest, sample for "
          "sample", render_gap(SC, SD, 127, 0) == render_gap(SC, SD, 127, 1));
    check("... its amount set to 0 in the silence (COMP still in S2): the same", render_gap(SC, SC, 0, 0) == render_gap(SC, SC, 0, 1));
    check("... at 0, the next note as with COMP in no slot", render_gap(SC, SC, 0, 1) == render_gap(SC, SD, 127, 1));
    check("... at 127 all along: after 8 s of silence it has let go (AUTO), the next note from rest",
          render_gap(SC, SC, 127, 0) == render_gap(SC, SC, 127, 1));
    /* the record: the amounts (the COMP TLV), each track's settings (the track slots' TLV) */
    ins_all(CMP);
    trk[0].p[P_TCOMP] = 33, trk[2].p[P_TCOMP] = 127, TDRUM->p[P_TCOMP] = 9, INS_CSET(0)[2] = 3, INS_CSET(2)[0] = 5;
    n = fxr_encode(b);
    trk[0].p[P_TCOMP] = trk[2].p[P_TCOMP] = TDRUM->p[P_TCOMP] = 0, INS_CSET(0)[2] = 6, INS_CSET(2)[0] = 1;
    ins_all(INS_D);
    check("the record: COMP's amounts, T1's REL and T3's RATIO (each its own), T1's slots back with a load",
          fxr_decode(b, n, 1) && trk[0].p[P_TCOMP] == 33 && trk[1].p[P_TCOMP] == 0 && trk[2].p[P_TCOMP] == 127 &&
          INS_CSET(0)[2] == 3 && INS_CSET(2)[0] == 5 && INS_CSET(1)[0] == 1 && INS_CSET(1)[2] == 6 && ins_is(0, FXT_DIST, FXT_COMP));
    check("... the drum bus's amount first in the COMP TLV, back with a load", TDRUM->p[P_TCOMP] == 9);
    INS_CSET(0)[2] = 6;
    check("... a song section: the amounts, not the settings nor the slots", (ins_all(INS_D), fxr_decode(b, n, 0)) &&
          trk[2].p[P_TCOMP] == 127 && INS_CSET(0)[2] == 6 && ins_dflt());
    {   /* an old record: one RATIO ATK REL for every track (the COMP TLV's): every track takes them */
        uint8_t o[FXR_HEAD + 2u + FXT_TLV_COMP] = {1, 0, FXT_DIST, FXT_CHO, FXT_DLY, FXT_COMP, FXT_COMP, FXT_TLV_COMP,
                                                    0, 20, 0, 0, 3, 2, 5};
        uint32_t k, ok;
        ok = fxr_decode(o, sizeof o, 1);
        for (k = 0; k < NTRK; k++)
            ok &= INS_CSET(k)[0] == 3 && INS_CSET(k)[1] == 2 && INS_CSET(k)[2] == 5;
        check("an old record (COMP in a master slot, the shared settings): every track's COMP takes them; T1's COMP in a slot",
              ok && trk[0].p[P_TCOMP] == 20 && INS_ON(0, FXT_COMP) && !INS_ON(1, FXT_COMP));
    }
    trk[0].p[P_TCOMP] = trk[2].p[P_TCOMP] = TDRUM->p[P_TCOMP] = 0;
    trk[2].p[P_TCOMP] = 50;
    fxr_decode(0, 0, 1);
    check("... a project without a record: every amount 0, the settings the defaults", trk[2].p[P_TCOMP] == 0 &&
          INS_CSET(3)[0] == GP[G_CRAT].def && INS_CSET(3)[2] == GP[G_CREL].def);
    fxs_set(FXS_DEF);
    ins_all(INS_D);
#endif
}

/* ---- the drum bus: its two slots and its own sends, with the drums' FX on; at 0 the mix of before; COMP's gain on every
 * channel; the sends from the bus's mid; the record */
static void t_dbus(void)
{
    static const uint8_t NOREV[4] = {FXT_NONE, FXT_CHO, FXT_DLY, FXT_NONE}, NONE[4] = {FXT_NONE, FXT_NONE, FXT_NONE, FXT_NONE};
    static const int16_t ALL[5] = {100, 70, 60, 90, 127};
    static const ins_t DNO = {{D0, D1}, {D0, D1}, {D0, D1}, {FXT_NONE, FXT_NONE}};
    uint64_t dry, h, base;
    uint8_t b[FXR_MAX];
    uint32_t n, i, k;
    memset(r_db, 0, sizeof r_db);
    base = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    dry = render(FXS_DEF, 0, 0, 0, 0, 0, 0);
    for (k = 0; k < 4u; k++) {   /* the drum track alone: its DIST heard; its own CHO DLY REV not (the bus sends nothing:
                                  * they scale the lanes' sends, drum_sends.c dsend_kit, 2026-10-18) */
        static const char *const NM[4] = {"DIST", "CHO", "DLY", "REV"};
        char m[140];
        memset(r_db, 0, sizeof r_db);
        r_db[k] = ALL[k];
        h = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
        snprintf(m, sizeof m, k ? "the drum track's own %s (%d): unheard, the bus sends nothing (the mix as without it)"
                                : "the drum track's %s (%d): heard (the mix changes)", NM[k], ALL[k]);
        check(m, k ? h == base : h != base);
    }
    memset(r_db, 0, sizeof r_db);
    r_db[3] = 90;
    h = render(NOREV, 0, 90, 60, 50, 80, 1);
    r_db[3] = 0;
    check("the drum track's REV with REV in no master slot: the mix as without it, sample for sample", h == render(NOREV, 0, 90, 60, 50, 80, 1));
    r_db[0] = 100;
    h = render(FXS_DEF, DNO, 90, 60, 50, 80, 1);
    r_db[0] = 0;
    check("the drum track's DIST in none of its slots: the mix as at 0", h == render(FXS_DEF, DNO, 90, 60, 50, 80, 1));
    memcpy(r_db, ALL, sizeof r_db);
    check("every drum bus amount up, every slot empty: the dry mix, sample for sample", render(NONE, INS_NONE, 90, 60, 50, 80, 1) == dry);
    r_dbyp = 1;
    h = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    memset(r_db, 0, sizeof r_db);
    check("the drums' FX off (P_FXOFF): every drum bus amount unheard (the mix as with them at 0)",
          h == render(FXS_DEF, 0, 90, 60, 50, 80, 1));
    r_dbyp = 0;
#if FELUCCA_MASTER_COMP
    {
        static const ins_t CMP = {{D0, D1}, {D0, D1}, {D0, D1}, {FXT_DIST, FXT_COMP}};
        static const uint8_t DCMP[2] = {FXT_DIST, FXT_COMP};
        int32_t gl, gs;
        uint64_t h0;
        r_db[4] = 127;
        h = render(FXS_DEF, CMP, 90, 60, 50, 0, 1);
        h0 = render(FXS_DEF, 0, 90, 60, 50, 0, 1);
        r_db[4] = 0;
        check("the drum track's COMP (127, in its S2): heard", h != render(FXS_DEF, CMP, 90, 60, 50, 0, 1));
        check("... in none of its slots: the mix as at 0", h0 == render(FXS_DEF, 0, 90, 60, 50, 0, 1));
        /* the gain on every channel: a loud bus and an echo send under it, settled; both take the same gain */
        ins_put(TRK_DRUM, DCMP);
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
        ins_all(INS_D);
    }
#endif
    {   /* the drum bus sends nothing of its own (2026-10-18): the drum track's REV DLY CHO only scale the lanes' sends */
        int32_t any = 0;
        for (i = 0; i < CTL; i++)
            mix_l[i] = 3000, mix_r[i] = 1000, send_r[i] = send_d[i] = send_c[i] = 0;
        TDRUM->p[P_REV] = TDRUM->p[P_DLY] = TDRUM->p[P_CHOR] = 127;
        dbus_run(CTL);
        dbus_run(CTL);
        for (i = 0; i < CTL; i++)
            any |= send_r[i] | send_d[i] | send_c[i];
        check("the drum track's own REV DLY CHO at 127: the bus sends nothing (a loud bus, the three buses stay at 0)", !any);
        TDRUM->p[P_REV] = TDRUM->p[P_DLY] = TDRUM->p[P_CHOR] = 0;
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
    check("the record: the default slots with drum bus amounts writes them (a TLV a type), back with a section",
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
    ins_all(INS_D);
}

/* ---- the drum track's send levels (2026-10-12: "it should be like any other track"): 100 % the mix of before, every
 * lane's send of that bus scaled; the record */
static void t_kit(void)
{
    uint64_t h, base = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    uint8_t b[FXR_MAX];
    uint32_t n;
    r_kit[0] = 0;
    h = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    check("the drum track's REV level 0: the mix as with every lane's REV 0 (2: no REV), sample for sample",
          h == (r_kit[0] = 127, render(FXS_DEF, 0, 90, 60, 50, 80, 2)) && h != base);
    r_kit[1] = 64;
    check("... its DLY level at 50 %: the mix changes", render(FXS_DEF, 0, 90, 60, 50, 80, 1) != base);
    r_kit[1] = 127;
    check("... every level at 100 %: the mix of before (the default)", render(FXS_DEF, 0, 90, 60, 50, 80, 1) == base);
    {
        uint32_t v, ok = 1;
        dsend_kit[0] = dsend_kit[1] = dsend_kit[2] = DKS_DEF;
        dsend_key = 0xFFFFFFFFu;
        dsend_table(1);
        for (v = 0; v <= DSEND_MAX; v++)
            ok &= dsend_dt[v] == dsend_lvl(v) * 258 && dsend_ct[v] == dsend_lvl(v) * 258;
        dsend_kit[2] = 64;
        dsend_table(1);
        ok &= dsend_ct[31] == dsend_lvl(31) * 258 * 64 / 127 && dsend_dt[31] == dsend_lvl(31) * 258;
        check("the lanes' send tables: x 1 exactly at 100 %, CHO's x 64 / 127 at 64 (refreshed when a level moves)", ok);
    }
    dsend_kit[0] = 30, dsend_kit[2] = DKS_DEF;
    n = fxr_encode(b);
    dsend_kit[0] = DKS_DEF;
    check("the record: a level below 100 % writes the track slots' TLV; back with a load", n > FXR_HEAD &&
          fxr_decode(b, n, 1) && dsend_kit[0] == 30 && dsend_kit[1] == DKS_DEF);
    dsend_kit[0] = 99;
    check("... a song section keeps the levels playing; no record: 100 %", fxr_decode(b, n, 0) && dsend_kit[0] == 99 &&
          (fxr_decode(0, 0, 1), dsend_kit[0] == DKS_DEF));
}

/* ---- each drum sound's inserts: always heard (D7) with the drums' FX on; at 0 the mix of before; SOUND 3; the record */
static void t_dins(void)
{
    static const ins_t NOD = {{FXT_NONE, FXT_NONE}, {FXT_NONE, FXT_NONE}, {FXT_NONE, FXT_NONE}, {FXT_NONE, FXT_NONE}};
    uint64_t base, h;
    uint8_t b[FXR_MAX];
    uint32_t n, k, ok;
    int16_t *v;
    const param_desc_t *d;
    memset(r_db, 0, sizeof r_db);
    memset(r_dl, 0, sizeof r_dl);
    base = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    r_dl[0] = 110;
    h = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    check("every drum sound's DIST at 110: heard (the mix changes)", h != base);
    h = render(FXS_DEF, NOD, 0, 60, 50, 80, 1);
    r_dl[0] = 0;
    check("... with no DIST in any track's slot: still heard (D7: the sound's own)", h != render(FXS_DEF, NOD, 0, 60, 50, 80, 1));
    r_dl[0] = 110, r_dbyp = 1;
    h = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    r_dl[0] = 0;
    check("... the drums' FX off: as at 0", h == render(FXS_DEF, 0, 90, 60, 50, 80, 1));
    r_dbyp = 0;
    r_dslc = 1;                                         /* (the drum SLICER on: the sounds into its mono buffer) */
    base = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    r_dl[0] = 110;
    h = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    r_dl[0] = 0;
    check("the drum SLICER on (its mono path): every sound's DIST heard", h != base);
    r_dslc = 0;
#if FELUCCA_MASTER_COMP
    base = render(FXS_DEF, 0, 90, 60, 50, 0, 1);
    r_dl[1] = 127;
    h = render(FXS_DEF, 0, 90, 60, 50, 0, 1);
    r_dl[1] = 0;
    check("every drum sound's COMP at 127 (no COMP in any slot): heard (D7)", h != base);
#endif
    /* SOUND 3: DST, then the sends in the master slots' order, then CMP while there is room */
    fxs_set(FXS_DEF);
    check("SOUND 3 in the default layout: DST REV DLY CHO (value ids 19 16 17 18: the returns' order)",
          fxs_lane_id(0) == (FELUCCA_FX_DIST ? 19u : 16u) && (!FELUCCA_FX_DIST || (fxs_lane_id(1) == 16u &&
          fxs_lane_id(2) == 17u && fxs_lane_id(3) == 18u)));
    layout(FXT_REV, FXT_NONE, FXT_NONE, FXT_NONE);
    check("... REV the only send: DST REV CMP (the cells follow)", !FELUCCA_FX_DIST || (fxs_lane_id(0) == 19u &&
          fxs_lane_id(1) == 16u && fxs_lane_id(2) == (FELUCCA_MASTER_COMP ? 20u : 0xFFu)));
    fxs_set(FXS_DEF);
    dsend_set(5, 3, 77);
    d = dsend_desc(5, 3, &v);
    ok = d && !strcmp(d->label, "DST") && *v == 77 && dins_amt[0][5] == 77;
#if FELUCCA_MASTER_COMP
    dsend_set(5, 4, 200);
    d = dsend_desc(5, 4, &v);
    ok &= d && !strcmp(d->label, "DYN") && *v == 127 && dins_amt[1][5] == 127;
#else
    ok &= !dsend_desc(5, 4, &v);
#endif
    check("... the sound's S1 DST and S2 DYN (its default slots): their own amounts (0..127)", ok);
    /* the record */
    n = fxr_encode(b);
    for (k = FXR_HEAD, ok = 0; k + 2u <= n; k += 2u + b[k + 1u])
        ok |= b[k] == FXT_DIST && b[k + 1u] == 1u + DRUM_LANES && b[k + 3u + 5u] == 77u;
    memset(dins_amt, 0, sizeof dins_amt);
    check("the record: DIST's TLV the drum bus then the 16 sounds; back with a section", ok && fxr_decode(b, n, 0) &&
          dins_amt[0][5] == 77
#if FELUCCA_MASTER_COMP
          && dins_amt[1][5] == 127
#endif
    );
    b[1] = 0;                                           /* (the same record from before the track slots: no flag) */
    b[2] = FXT_NONE;                                    /* (its layout without DIST nor COMP in a master slot) */
    check("... an old record (no FXR_F_INS) with DIST and COMP in no master slot: the sounds' DST CMP were never heard: "
          "0 (rule 8)", fxr_decode(b, n, 0) && dins_amt[0][5] == 0 && dins_amt[1][5] == 0);
    fxr_decode(0, 0, 1);
    check("... none: every sound's insert 0", !dins_amt[0][5] && !dins_amt[1][5]);
    dins_st[2][0].d.env = 999;
    dins_restart(2);
    check("a drum voice's inserts restart with its hit (dins_restart)", dins_st[2][0].d.env == 0);
    fxs_set(FXS_DEF);
    ins_all(INS_D);
}

/* ---- an old project on the tracks (fx_rec.c ins_map, the design §4.2): rules 1..8 */
static void t_map(void)
{
    static const uint8_t ALL3[4] = {FXT_DIST, FXT_COMP, FXT_FILT, FXT_REV}, NOINS[4] = {FXT_NONE, FXT_CHO, FXT_DLY, FXT_REV};
    ins_t m;
    uint32_t drop, k;
    host_tracks_init();
    for (k = 0; k < NTRK; k++)                          /* (the values past P_E7: host_tracks_init leaves them) */
        for (drop = P_E7 + 1u; drop < P_COUNT; drop++)
            trk[k].p[drop] = TP[drop].def;
    drop = ins_map(FXS_OLD, 1, m);
    check("no record (an old project, a 2.4 import): every part S1 DIST, S2 FILTER (built; rule 3, rule 5), none dropped",
          !drop && m[0][0] == FXT_DIST && m[1][1] == (TFLT_ON ? FXT_FILT : FXT_NONE) && m[3][0] == FXT_DIST);
    drop = ins_map(FXS_OLD, 0, m);
    check("the old default layout (DIST CHO DLY REV): the same (the new track's default, D5)", !drop &&
          !memcmp(m, INS_D, sizeof m));
    drop = ins_map(NOINS, 0, m);
    check("no insert in the old layout: S1 empty, S2 the FILTER (silent at 0: rule 5)", !drop && m[0][0] == FXT_NONE &&
          m[0][1] == (TFLT_ON ? FXT_FILT : FXT_NONE));
#if TFLT_ON
    trk[1].p[P_TFLT] = -20;
    drop = ins_map(FXS_OLD, 1, m);
    check("no record, T2's FILT in use: S1 DIST S2 FILTER (2.4's order; no send displaced)", m[1][0] == FXT_DIST && m[1][1] == FXT_FILT);
    drop = ins_map(FXS_OLD, 0, m);
    check("an old record without FILT in a slot, T2's FILT set (unheard then): S2 left empty (not heard now either)",
          m[1][0] == FXT_DIST && m[1][1] == FXT_NONE && m[0][1] == FXT_FILT);
    trk[1].p[P_TFLT] = 0;
    TDRUM->p[P_TFLT] = 30;
    drop = ins_map(FXS_OLD, 1, m);
    check("no record, the drum track's FILT in use: S1 FILTER S2 DIST (the drum bus's old order)",
          m[TRK_DRUM][0] == FXT_FILT && m[TRK_DRUM][1] == FXT_DIST);
    TDRUM->p[P_TFLT] = 0;
#if FELUCCA_MASTER_COMP
    trk[0].p[P_TFLT] = -10, trk[0].p[P_TCOMP] = 40, trk[0].p[P_DIST] = 50;
    trk[2].p[P_TCOMP] = 40;
    drop = ins_map(ALL3, 0, m);
    check("DIST COMP FILT all in master slots, T1 using all three: S1 DIST S2 FILTER, COMP dropped (rule 4, TOOLS > MISS)",
          (drop & 1u) && m[0][0] == FXT_DIST && m[0][1] == FXT_FILT);
    check("... T3 using COMP only: S1 DIST (rule 3: room), S2 COMP; not dropped", m[2][0] == FXT_DIST && m[2][1] == FXT_COMP &&
          !(drop & 4u));
    trk[0].p[P_DIST] = 0;
    drop = ins_map(ALL3, 0, m);
    check("... T1's DST 0: S1 FILTER S2 COMP (DIST only kept while there is room)", !(drop & 1u) &&
          m[0][0] == FXT_FILT && m[0][1] == FXT_COMP);
    trk[0].p[P_TFLT] = trk[0].p[P_TCOMP] = trk[2].p[P_TCOMP] = 0;
#endif
#endif
#if FELUCCA_MASTER_COMP && FELUCCA_AUTO
    {   /* rule 2: an automation event on the type (a lock), its amount 0, counts as in use */
        auto_list_t *l = AUTO_L(1);
        static const uint8_t CMPL[4] = {FXT_COMP, FXT_CHO, FXT_DLY, FXT_REV};
        l->n = 1, l->ev[0].place = AUTO_ONLY | 3u, l->ev[0].param = (uint8_t)mot_sid(P_TCOMP), l->ev[0].value = 60;
        drop = ins_map(CMPL, 0, m);
        check("rule 2: a lock on T2's CMP (its amount 0) with COMP in a master slot: T2 keeps COMP; T1 (no use): not",
              m[1][0] == FXT_COMP && m[0][0] != FXT_COMP && m[0][1] != FXT_COMP);
        l->n = 0;
    }
#endif
    {   /* the whole path: an old record (no flag, no TLV 0x42) loads the mapped slots and the sends */
        uint8_t o[FXR_HEAD] = {1, 0, FXT_REV, FXT_DIST, FXT_NONE, FXT_CHO};
        k = (uint32_t)fxr_decode(o, sizeof o, 1);
        check("an old record (REV DIST ---- CHO): the master's returns REV ---- CHO (slot 0's REV on A), every track's S1 DIST", k &&
              is_layout(FXT_NONE, FXT_REV, FXT_NONE, FXT_CHO) && ins_dflt() && !ins_drop);
    }
    fxs_set(FXS_DEF);
    ins_all(INS_D);
}

/* ---- the record */
static void t_record(void)
{
    uint8_t b[FXR_MAX], c[FXR_MAX];
    uint32_t n;
    fxs_set(FXS_DEF);
    ins_all(INS_D);
    check("the default (the master's sends, the tracks' S1 DIST S2 FILTER): nothing to keep (no record)", fxr_encode(b) == 0u);
    layout(FXT_NONE, FXT_REV, FXT_NONE, FXT_DLY);
    n = fxr_encode(b);
    fxs_set(FXS_DEF);
    check("a layout: version 1, the flag of the track slots, the sends, an empty place the insert the tracks hold (DIST: "
          "an older build hears it); no TLV 0x42 (the mapping gives the slots); decoded back the same", n == FXR_HEAD &&
          b[0] == 1u && b[1] == FXR_F_INS && b[2] == FXT_DIST && b[3] == FXT_REV && fxr_decode(b, n, 1) &&
          is_layout(FXT_NONE, FXT_REV, FXT_NONE, FXT_DLY) && ins_dflt());
    {
        static const ins_t X = {{FXT_NONE, FXT_DIST}, {D0, D1}, {D0, D1}, {D0, D1}};
        uint32_t k, at, ok = 0;
        ins_all(X);
        fxs_set(FXS_DEF);
        n = fxr_encode(b);
        for (at = FXR_HEAD; at + 2u <= n; at += 2u + b[at + 1u])
            ok |= b[at] == INS_TLV && b[at + 1u] == INS_TLV_N && b[at + 2u] == FXT_NONE && b[at + 3u] == FXT_DIST;
        ins_all(INS_D);
        check("T1's slots ---- DIST: TLV 0x42 (23 bytes: the 8 slots first); back with a load", ok && fxr_decode(b, n, 1) &&
              ins_is(0, FXT_NONE, FXT_DIST) && ins_is(1, D0, D1));
        k = (uint32_t)fxr_decode(b, n, 0);
        ins_all(INS_D);
        check("... a song section (all 0) keeps the slots playing", k && fxr_decode(b, n, 0) && ins_dflt());
        memcpy(c, b, n);
        c[1] = 0;                                       /* (an older build's reader: no flag known, the TLV skipped) */
    }
    layout(FXT_NONE, FXT_REV, FXT_DIST, FXT_DLY);
    n = fxr_encode(b);
    memcpy(c, b, n);
    c[n] = 77, c[n + 1u] = 3, c[n + 2u] = c[n + 3u] = c[n + 4u] = 9;   /* (a later firmware's type) */
    check("an unknown TLV: skipped, the layout read", fxr_decode(c, n + 5u, 1) && is_layout(FXT_NONE, FXT_REV, FXT_NONE, FXT_DLY));
    check("a TLV cut short: refused, the defaults (a record-less project's)", !fxr_decode(c, n + 3u, 1) &&
          is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO) && ins_dflt());
    c[0] = 2;
    check("a newer version: ignored as a whole, the defaults", !fxr_decode(c, n, 1) && is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO));
    fxs_set(FXS_DEF);
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
    static const ins_t X = {{FXT_NONE, FXT_DIST}, {D0, D1}, {D0, D1}, {D0, D1}};
    static const ins_t Y = {{D0, D1}, {D0, D1}, {FXT_NONE, FXT_NONE}, {D0, D1}};
    int ok;
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    song.playing = 0, transport_req = 0;
    tracks(1);
    layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_NONE);
    ins_all(X);
    project_save(3);
    ok = !strcmp(last_msg, "SAVED") && slg_has(FXR_ID0 + 3);
    tracks(2);
    fxs_set(FXS_DEF);
    project_load(3);
    check("SAVE D with a layout and T1's slots; LOAD it back: the same", ok && is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_NONE) &&
          ins_is(0, FXT_NONE, FXT_DIST));
    tracks(3);
    fxs_set(FXS_DEF);
    project_save(4);
    ok = !strcmp(last_msg, "SAVED") && !slg_has(FXR_ID0 + 4);
    layout(FXT_REV, FXT_NONE, FXT_NONE, FXT_NONE);
    ins_all(X);
    project_load(4);
    check("a section in the default layout: no FX record; loading it gives the defaults", ok &&
          is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO) && ins_dflt());
    tracks(1);
    layout(FXT_CHO, FXT_NONE, FXT_NONE, FXT_NONE);
    project_save(3);
    fxs_set(FXS_DEF);
    project_save(3);
    ok = !slg_has(FXR_ID0 + 3);
    layout(FXT_REV, FXT_NONE, FXT_NONE, FXT_NONE);
    project_load(3);
    check("D saved again in the default layout: its record cleared, D loads the default", ok && is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO));
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
              is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO));
    }
    tracks(10);
    layout(FXT_NONE, FXT_REV, FXT_CHO, FXT_NONE);
    ins_all(Y);
    song.playing = 1;
    section_store(12);
    ok = sec_pend_has(12) && sec_pend_has(SEC_PEND_FX + 12);
    sec_boot();                                         /* (a warm reset: .noinit keeps the arena) */
    ok &= sec_pend_has(SEC_PEND_FX + 12);
    fxs_set(FXS_DEF);
    ins_all(INS_D);
    song.playing = 0;
    section_load(12);
    ok &= is_layout(FXT_NONE, FXT_REV, FXT_CHO, FXT_NONE) && ins_is(2, FXT_NONE, FXT_NONE);
    check("SAVE + key while playing: M's FX record (T3's empty slots too) waits in the arena with it, over a warm reset", ok);
    sections_write();
    ok = slg_has(12) && slg_has(FXR_ID0 + 12) && !sec_pend_has(12) && !sec_pend_has(SEC_PEND_FX + 12);
    fxs_set(FXS_DEF);
    ins_all(INS_D);
    project_load(12);
    check("... written when stopped: M and its FX record in the log; LOAD M: the same", ok &&
          is_layout(FXT_NONE, FXT_REV, FXT_CHO, FXT_NONE) && ins_is(2, FXT_NONE, FXT_NONE));
    fxs_set(FXS_DEF);
    ins_all(INS_D);
    ok = section_cue(12);
    arrangement_apply(12);
    check("a live jump to M (a song section): the layout and the slots playing stay (the project's)", ok &&
          is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO) && ins_dflt());
    {   /* the autosave's: id FXR_ID_AUTO, keyed by its project's sum */
        static project_t as;
        static dlrec_t ad;
        tracks(16);
        layout(FXT_NONE, FXT_REV, FXT_NONE, FXT_NONE);
        ins_all(X);
        proj_capture(&as, &ad);
        ok = fxr_log_put(FXR_ID_AUTO, as.sum, &as, 0) == 0 && slg_has(FXR_ID_AUTO);
        fxs_set(FXS_DEF);
        ins_all(INS_D);
        fxr_for(&as, 1)->psum = 0;
        fxr_log_get(FXR_ID_AUTO, as.sum, &as);
        proj_apply(&as, &ad, 1);
        ok &= is_layout(FXT_NONE, FXT_REV, FXT_NONE, FXT_NONE) && ins_is(0, FXT_NONE, FXT_DIST);
        fxr_log_get(FXR_ID_AUTO, as.sum ^ 1u, &as);     /* another project's key: none */
        proj_apply(&as, &ad, 1);
        check("the autosave's FX record: written beside it, read back with it; another key: the defaults",
              ok && is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO) && ins_dflt());
        layout(FXT_REV, FXT_NONE, FXT_NONE, FXT_NONE);
        ins_all(X);
        fxr_for(&as, 1)->psum = 0;                      /* (an old project: nothing bound to it) */
        proj_apply(&as, &ad, 1);
        check("an old project (no FX record): the default layout, the default slots", is_layout(FXT_NONE, FXT_REV, FXT_DLY, FXT_CHO) &&
              ins_dflt());
    }
    slg_boot();
    fxs_set(FXS_DEF);
    ins_all(INS_D);
    project_load(12);
    check("after a restart (the log scanned again): M loads its layout and slots", is_layout(FXT_NONE, FXT_REV, FXT_CHO, FXT_NONE) &&
          ins_is(2, FXT_NONE, FXT_NONE));
    fxs_set(FXS_DEF);
    ins_all(INS_D);
}

/* ---- fx-rework (2026-10-14): the master's two insert slots, DYN (COMP + LIMIT), FILT = the DJ filter, DUST an insert */
#include "fx_rework_ref.h"
static void sig(int32_t *l, int32_t *r, uint32_t blk)  /* a loud stereo test signal, block blk */
{
    uint32_t i;
    for (i = 0; i < CTL; i++) {
        uint32_t t = blk * CTL + i;
        l[i] = (int32_t)((t * 2654435761u) >> 17) - 16384 + ((t & 64u) ? 9000 : -9000);
        r[i] = (int32_t)((t * 40503u) & 0x7FFFu) - 16384;
    }
}
static void t_master(void)
{
    static const uint8_t DEF[2] = {MST_DEF0, MST_DEF1};
    int16_t *v;
    const param_desc_t *d;
    uint8_t m[INS_N], b[FXR_MAX];
    uint32_t i, k, n, ok, drop;
    int32_t l[CTL], r[CTL], l2[CTL], r2[CTL];
    mst_defaults();
    check("the master's default: S1 FILT, S2 DYN (those built)", mst_slot[0] == DEF[0] && mst_slot[1] == DEF[1]);
#if TFLT_ON && FELUCCA_MASTER_COMP
    mst_load(0, FXT_DYN);
    check("... DYN into S1: the two swap", mst_slot[0] == DEF[1] && mst_slot[1] == DEF[0]);
#endif
    mst_defaults();
    d = fxs_desc(FXS_ID_MST, &v);
    for (i = 0, ok = d != 0; d && i <= (uint32_t)d->max; i++)
        ok &= mst_list[i] != FXT_GATE;                  /* (fx-pool: CHRS ECHO on the master too, an instance of the pool) */
    check("the master's TYPE list: ---- and the inserts built but GATER; S1's entry FILT", ok &&
          (!TFLT_ON || mst_list[*v] == FXT_FILT));
#if FELUCCA_MASTER_COMP
    d = fxs_desc(FXS_ID_MST + 2u + (mst_slot[0] != FXT_DYN), &v);
    check("... S2's amount: DYN's THRS (G_CTHR); its list name DYN", d == &GP[G_CTHR] && v == &song.g[G_CTHR] &&
          !strcmp(TP[P_TCOMP].label, "DYN"));
#endif
    song.g[G_FILT] = song.g[G_DUST] = song.g[G_CTHR] = song.g[G_CGAIN] = song.g[G_CCEIL] = 0;
    drop = mst_map(m);
    check("an old project with no master effect: FILT DYN (unused, at 0: bypassed)", !drop && m[0] == DEF[0] && m[1] == DEF[1]);
#if FELUCCA_FX_DUST && TFLT_ON && FELUCCA_MASTER_COMP
    song.g[G_DUST] = 30;
    drop = mst_map(m);
    check("... DUST used (no COMP, no CEIL): DUST then FILT (the old order)", !drop && m[0] == FXT_DUST && m[1] == FXT_FILT);
    song.g[G_FILT] = -20, song.g[G_CCEIL] = 6;
    drop = mst_map(m);
    check("... DUST FILT and DYN all used: FILT left out (reported: ins_drop's master bit), DUST DYN", drop &&
          m[0] == FXT_DUST && m[1] == FXT_DYN);
    song.g[G_DUST] = 0;
    drop = mst_map(m);
    check("... FILT and DYN used: FILT DYN", !drop && m[0] == FXT_FILT && m[1] == FXT_DYN);
#endif
    song.g[G_FILT] = song.g[G_DUST] = 0, song.g[G_CCEIL] = GP[G_CCEIL].def;
    fxs_set(FXS_DEF);
    ins_all(INS_D);
    mst_defaults();
    check("the new project's master (FILT DYN, CEIL on): no record", fxr_encode(b) == 0u);
    mst_load(0, FELUCCA_FX_DIST ? FXT_DIST : FXT_NONE);
    mst_amt[FXT_DIST] = 77;
    n = fxr_encode(b);
    mst_defaults();
    mst_amt[FXT_DIST] = 0;
    ok = fxr_decode(b, n, 1);
    check("the master's slots and amounts: MST_TLV, back with a load", ok && mst_slot[0] == (FELUCCA_FX_DIST ? FXT_DIST : FXT_NONE) &&
          mst_slot[1] == DEF[1] && mst_amt[FXT_DIST] == (FELUCCA_FX_DIST ? 77 : 0) && (n > FXR_HEAD || !FELUCCA_FX_DIST));
    ok = fxr_decode(0, 0, 1);
    check("... no record: the mapping (FILT DYN), no amount", mst_slot[0] == DEF[0] && mst_slot[1] == DEF[1] && !mst_amt[FXT_DIST]);
#if FELUCCA_MASTER_COMP
    INS_CSET(2)[3] = 5, INS_CSET(2)[4] = 3;
    n = fxr_encode(b);
    INS_CSET(2)[3] = INS_CSET(2)[4] = 0;
    check("a track's DYN GAIN and CEIL: in the slots' TLV (31 bytes), back with a load", fxr_decode(b, n, 1) &&
          INS_CSET(2)[3] == 5 && INS_CSET(2)[4] == 3 && !INS_CSET(1)[3]);
    INS_CSET(2)[3] = INS_CSET(2)[4] = 0;
    {   /* the track limiter: a square far over -1 dB stays under the ceiling (after the lookahead) */
        mc_t c = {0, 0, 8192, 0};
        int32_t x[CTL], pk = 0;
        INS_CSET(0)[4] = 4;                            /* (-1.0 dB) */
        for (n = 0; n < 100u; n++) {
            for (i = 0; i < CTL; i++)
                x[i] = (i & 4u) ? 60000 : -60000;
            tcomp_run(&c, 1, x, x, CTL, 0);
            for (i = 0; n > 4u && i < CTL; i++)
                pk = x[i] > pk ? x[i] : -x[i] > pk ? -x[i] : pk;
        }
        check("DYN's CEIL on a part: a square at +5 dB over full scale held under -1 dB", pk > 20000 && pk <= MLIM_CLIN[3]);
        INS_CSET(0)[4] = 0;
        tcomp_run(&c, 0, x, x, CTL, 0);
        check("... at 0: the limiter off", !tlim[0].on);
    }
    {   /* the drum bus's (stereo, its own right line): noise far over the ceiling, both sides held under it */
        int32_t xl[CTL], xr[CTL], pk = 0;
        uint32_t seed = 1;
        tlim_set(&tlim[TRK_DRUM], tlim_dr, 0);
        tlim_set(&tlim[TRK_DRUM], tlim_dr, 6);         /* (-3.0 dB) */
        for (n = 0; n < 200u; n++) {
            for (i = 0; i < CTL; i++) {
                seed = seed * 1664525u + 1013904223u;
                xl[i] = (int32_t)(seed >> 15) - 65536;
                xr[i] = -xl[i] / 2 + (int32_t)(seed & 4095u);
            }
            tlim_block(&tlim[TRK_DRUM], tlim_dr, xl, xr, CTL);
            for (i = 0; n > 2u && i < CTL; i++) {
                int32_t a = xl[i] < 0 ? -xl[i] : xl[i], b = xr[i] < 0 ? -xr[i] : xr[i];
                pk = a > pk ? a : b > pk ? b : pk;
            }
        }
        tlim_set(&tlim[TRK_DRUM], tlim_dr, 0);
        check("the drum bus's DYN CEIL (stereo, half the lookahead): noise at +6 dB over full scale held at -3 dB on both "
              "sides (within 0.02 dB: the box's rounding)", pk > 15000 && pk <= MLIM_CLIN[5] + MLIM_CLIN[5] / 400);
    }
#endif
#if TFLT_ON
    {   /* FILT on the master: the DJ filter of before, sample for sample (fx_rework_ref.h), LP then HP then open */
        static const int16_t V[6] = {-40, -40, 25, 63, 0, 0};
        uint32_t same = 1;
        song.g[G_CTHR] = song.g[G_CGAIN] = 0, song.g[G_CCEIL] = 0;
        mst_defaults();
        memset(&ref_djf, 0, sizeof ref_djf);
        memset(&tflt[NTRK], 0, sizeof tflt[NTRK]);
        for (k = 0; k < 6u * 40u; k++) {
            song.g[G_FILT] = V[k / 40u];
            sig(l, r, k), sig(l2, r2, k);
            ref_djf_process(l2, r2, CTL);
            mst_run(l, r, CTL);
            same &= !memcmp(l, l2, sizeof l) && !memcmp(r, r2, sizeof r);
        }
        check("FILT on the master: the DJ filter of before, sample for sample (LP, HP, back open)", same);
        mst_put((const uint8_t[2]){FXT_NONE, FXT_NONE});
        song.g[G_FILT] = -50;
        sig(l, r, 7), sig(l2, r2, 7);
        for (k = 0; k < 200u; k++)
            mst_run(l, r, CTL), sig(l, r, 7);
        mst_run(l, r, CTL);
        check("... FILT in no master slot, its knob turned: the mix as it is (glided open, then off)",
              !memcmp(l, l2, sizeof l) && !memcmp(r, r2, sizeof r));
        song.g[G_FILT] = 0;
    }
#endif
#if FELUCCA_FX_DUST
    {   /* DUST on the master: the master DUST of before, sample for sample */
        uint32_t same = 1;
        static const uint8_t DU[2] = {FXT_DUST, FXT_NONE};
        mst_put(DU);
        song.playing = 1;
        for (k = 0; k < 300u; k++) {
            song.g[G_DUST] = (int16_t)(k < 150u ? 90 : k < 250u ? 20 : 0);
            sig(l, r, k), sig(l2, r2, k);
            ref_dust_process(l2, r2, CTL);
            mst_run(l, r, CTL);
            same &= !memcmp(l, l2, sizeof l) && !memcmp(r, r2, sizeof r);
        }
        song.playing = 0, song.g[G_DUST] = 0;
        check("DUST on the master: the master DUST of before, sample for sample", same);
        ins_load(1, 0, FXT_DUST);
        trk[1].p[P_TDUST] = 60;
        check("DUST on a track: its slot, its amount P_TDUST; in the FX record", ins_has(1, FXT_DUST) == 1u && ins_amt(1, 0) == P_TDUST);
        n = fxr_encode(b);
        trk[1].p[P_TDUST] = 0;
        check("... back with a load", fxr_decode(b, n, 1) && trk[1].p[P_TDUST] == 60 && ins_has(1, FXT_DUST) == 1u);
        trk[1].p[P_TDUST] = 0;
        ins_all(INS_D);
    }
#endif
    song.g[G_CCEIL] = GP[G_CCEIL].def;
    mst_defaults();
    fxs_set(FXS_DEF);
}

/* ---- fx-rework stage 2: each drum sound's two insert slots (fx_slots.c lin_slot, fx.c dins_post) */
static void lanes_all(uint32_t a, uint32_t b)
{
    uint32_t l;
    for (l = 0; l < DRUM_LANES; l++) {
        const uint8_t s[INS_N] = {(uint8_t)a, (uint8_t)b};
        lin_put(l, s);
    }
}
static void t_lanes(void)
{
    uint64_t base, h;
    uint8_t b[FXR_MAX];
    uint32_t n, i, ok;
    int16_t *v;
    const param_desc_t *d;
    lin_defaults();
    check("every drum sound's slots by default: S1 DIST, S2 DYN (the DST and CMP of before), no record",
          lin_slot[3][0] == LIN_DEF0 && lin_slot[3][1] == LIN_DEF1 && lin_dflt() && lin_tlv(b) == 0u);
    d = dsend_desc(4, 5, &v);
    for (i = 0, ok = d != 0; d && i <= (uint32_t)d->max; i++)
        ok &= lin_list[i] == FXT_NONE || (LIN_OK >> lin_list[i] & 1u);
    check("S1 TYPE (value id 21): ---- and the voice types built (no CHRS ECHO GATER FLNG WIDE)", ok && lin_list[*v] == LIN_DEF0);
    dins_amt[0][4] = 60, dins_amt[1][4] = 30;
    for (i = 0; d && i <= (uint32_t)d->max && lin_list[i] != LIN_DEF1; i++)
        ;
    dsend_set(4, 5, (int32_t)i);                       /* (S1 to S2's type: they swap, amounts too) */
    check("S1 to DYN: the two swap, their amounts with them", lin_slot[4][0] == LIN_DEF1 && lin_slot[4][1] == LIN_DEF0 &&
          dins_amt[0][4] == 30 && dins_amt[1][4] == 60);
#if TFLT_ON
    for (i = 0; i <= (uint32_t)d->max && lin_list[i] != FXT_FILT; i++)
        ;
    dsend_set(4, 6, (int32_t)i);
    dsend_set(4, 4, -40);
    d = dsend_desc(4, 4, &v);
    check("S2 to FILT: from 0, then -40 (its bipolar range), read back -40", lin_slot[4][1] == FXT_FILT && d && *v == -40 &&
          lin_amt(4, 1) == -40);
    n = fxr_encode(b);
    lin_defaults();
    memset(dins_amt, 0, sizeof dins_amt);
    check("the record: LIN_TLV (the slots), the amounts in DIST's and COMP's TLVs; back with a load",
          fxr_decode(b, n, 1) && lin_slot[4][0] == LIN_DEF1 && lin_slot[4][1] == FXT_FILT && lin_amt(4, 1) == -40 &&
          lin_slot[5][0] == LIN_DEF0);
    fxr_decode(0, 0, 1);
    check("... no record: the defaults", lin_dflt());
    /* the audio: a lane's FILT heard, at 0 as with nothing in the slot */
    memset(r_db, 0, sizeof r_db);
    memset(r_dl, 0, sizeof r_dl);
    lanes_all(FXT_NONE, FXT_NONE);
    base = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    lanes_all(FXT_FILT, FXT_NONE);
    h = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    check("every sound's S1 FILT at 0: the mix as with empty slots, sample for sample", h == base);
    r_dl[0] = (uint8_t)(int8_t)-50;
    h = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    r_dl[0] = 0;
    check("... at -50 (a low-pass): heard (the mix changes)", h != base);
#endif
#if FELUCCA_FX_DUST
    lanes_all(FXT_DUST, FXT_NONE);
    r_dl[0] = 100;
    song.playing = 1;
    h = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    song.playing = 0;
    r_dl[0] = 0;
    lanes_all(FXT_NONE, FXT_NONE);
    check("every sound's S1 DUST at 100: heard", h != render(FXS_DEF, 0, 90, 60, 50, 80, 1));
#endif
#if FELUCCA_INS_FOLD
    lanes_all(FXT_FOLD, FXT_NONE);
    r_dl[0] = 100;
    h = render(FXS_DEF, 0, 90, 60, 50, 80, 1);
    r_dl[0] = 0;
    lanes_all(FXT_NONE, FXT_NONE);
    check("every sound's S1 FOLD at 100: heard", h != render(FXS_DEF, 0, 90, 60, 50, 80, 1));
#endif
    lin_defaults();
    memset(dins_amt, 0, sizeof dins_amt);
    (void)base, (void)h, (void)n, (void)v;
}

/* ---- fx-rework stage 3: the USB input as a track (fx_slots.c ut, fx.c ut_mix) */
#if FELUCCA_USB_AUDIO
static void ut_block(uint32_t seed)                    /* the host's USB block (a stereo signal), the buses cleared, ut_mix */
{
    uint32_t i;
    for (i = 0; i < CTL; i++) {
        host_ua_l[i] = (int32_t)((seed * 2654435761u + i * 40503u) >> 18) - 8192;
        host_ua_r[i] = (int32_t)(i * 997u % 12000u) - 6000;
        mix_l[i] = mix_r[i] = send_c[i] = send_d[i] = send_r[i] = 0;
    }
    ut_mix(CTL);
}
static void t_usb(void)
{
    uint8_t b[FXR_MAX];
    uint32_t i, k, n, ok;
    int16_t *v;
    const param_desc_t *d;
    int32_t pk;
    ut_defaults();
    fxs_set(FXS_DEF);
    host_ua_on = 1;
    for (k = 0, ok = 1; k < 4u; k++) {
        ut_block(k);
        for (i = 0; i < CTL; i++)
            ok &= mix_l[i] == host_ua_l[i] && mix_r[i] == host_ua_r[i] && !send_r[i] && !send_d[i] && !send_c[i];
    }
    check("the USB input at its defaults: its samples into the mix as they are, no send", ok);
    ut.v[UTV_LVL] = 0;
    ut_block(9), ut_block(9);
    for (i = 0, ok = 1; i < CTL; i++)
        ok &= !mix_l[i] && !mix_r[i];
    check("... VOLUME 0 (after a block's ramp): silent", ok);
    ut.v[UTV_LVL] = UT_LVL_DEF, ut.v[UTV_MUTE] = 1;
    ut_block(9), ut_block(9);
    for (i = 0, ok = 1; i < CTL; i++)
        ok &= !mix_l[i] && !mix_r[i];
    check("... MUTE: silent after its fade", ok);
    ut.v[UTV_MUTE] = 0, ut.v[UTV_PAN] = 63;
    ut_block(9), ut_block(9);
    for (i = 0, ok = 1, pk = 0; i < CTL; i++)
        ok &= mix_r[i] == host_ua_r[i], pk |= mix_l[i] != host_ua_l[i];
    check("... PAN right: the left side down, the right as it is", ok && pk);
    ut.v[UTV_PAN] = 0, ut.v[UTV_REV] = 100;
    ut_block(9), ut_block(9);
    for (i = 0, ok = 0; i < CTL; i++)
        ok |= send_r[i] != 0;
    check("... REV 100: its mid into the reverb send", ok);
    ut.v[UTV_FXON] = 0;
    ut_block(9), ut_block(9);
    for (i = 0, ok = 1; i < CTL; i++)
        ok &= !send_r[i] && mix_l[i] == host_ua_l[i];
    check("... FX off: no send, dry", ok);
    ut.v[UTV_FXON] = 1, ut.v[UTV_REV] = 0;
    ut_block(9), ut_block(9);
    d = ut_desc(UT_ID_TY, &v);
    for (i = 0; d && i <= (uint32_t)d->max && ut_list[i] != (FELUCCA_FX_DIST ? FXT_DIST : FXT_DYN); i++)
        ;
    ut_load(0, ut_list[i]);
    ut.amt[0] = 100;
    ut_block(9), ut_block(9);
    for (i = 0, ok = 0; i < CTL; i++)
        ok |= mix_l[i] != host_ua_l[i];
    check("... S1 DIST (or DYN) at 100: heard on the input (both sides through the kernel)", ok && ut_desc(UT_ID_AMT, &v) && v == &ut.amt[0]);
    n = fxr_encode(b);
    ut_defaults();
    check("the record: UT_TLV, the input's values and slots back with a load", fxr_decode(b, n, 1) && ut.amt[0] == 100 &&
          ut.slot[0] != FXT_NONE && ut.v[UTV_FXON] == 1);
    fxr_decode(0, 0, 1);
    check("... no record: the defaults", ut_dflt() && !ut.amt[0]);
    host_ua_on = 0;
    ut_block(1);
    for (i = 0, ok = 1; i < CTL; i++)
        ok &= !mix_l[i] && !mix_r[i];
    check("no stream: nothing mixed", ok);
}
/* 2026-10-15 ("any FX insert on the USB IN track, like any other track"): every insert type a track's slot takes, on the
 * input's S1 and S2: it loads, it changes both sides (WIDE: its side into L only, as on the master), at its neutral amount
 * (EQ3: flat) it lets go to the dry input bit for bit, and the record brings it back; a pooled type with no instance free:
 * FULL, dry */
static int32_t ut_dl[CTL], ut_dr[CTL];                  /* (the dry input of the last ut_xblock) */
static void ut_xblock(uint32_t seed)                    /* a louder input (-6 dBFS: DYN acts), the buses cleared, ut_mix */
{
    uint32_t i;
    for (i = 0; i < CTL; i++) {
        host_ua_l[i] = ut_dl[i] = (int32_t)(((seed * 2654435761u + i * 40503u) >> 18) & 0x3FFFu) * 2 - 16384;
        host_ua_r[i] = ut_dr[i] = (int32_t)((i * 997u + seed * 131u) % 12000u) * 2 - 12000;
        mix_l[i] = mix_r[i] = send_c[i] = send_d[i] = send_r[i] = 0;
    }
    ut_mix(CTL);
}
static void ut_poll(void)
{
#if INS_LIM
    uint32_t k;
    for (k = 0; k < 4u; k++)
        fxi_poll();
#endif
}
static int16_t ut_on_amt(uint32_t t)                    /* a type's amount that is heard */
{
    return t == FXT_EQ3 ? 1 : t == FXT_FILT ? 50 : t == FXT_GATE || t == FXT_JUNO ? 127 : 100;
}
static void t_usb_types(void)
{
    uint8_t b[FXR_MAX];
    uint32_t t, s, k, i, n, nt = 0, okl = 1, okr = 1, okb = 1, okt = 1, oke = 1;
    char why[96] = "";
    host_ua_on = 1;
    fxs_set(FXS_DEF);
    ins_defaults();
    clk_blk = CTL * 240u;                               /* (stopped at 240 BPM: GATER runs on at the tempo) */
    check("the USB input's slots take every insert type a track's do", UT_OK == (FXT_BUILT & FXT_INSERT));
    for (t = 1; t < FXT_N; t++) {
        if (!(UT_OK >> t & 1u))
            continue;
        nt++;
        for (s = 0; s < INS_N; s++) {
            uint32_t dl = 0, dr = 0, same = 1;
            ut_defaults();
#if FELUCCA_INS_EQ3
            eq3_defaults();
#endif
            ut_load(s, t);
            ut_poll();
            okt &= ut.slot[s] == t && ut.slot[s ^ 1u] == FXT_NONE && ut_has(t) == s + 1u;
            ut.amt[s] = ut_on_amt(t);
#if FELUCCA_INS_EQ3
            EQ3_V(NTRK + 1u)[EQV_LO] = -20, EQ3_V(NTRK + 1u)[EQV_HI] = 6;   /* (the input's own bands) */
#endif
            ut.v[UTV_GPAT] = 1, ut.v[UTV_GRATE] = 2;     /* (GATER: 8TH at 1/32, closes within a few blocks) */
            for (k = 0; k < 400u; k++) {
                ut_xblock(k);
                for (i = 0; i < CTL; i++)
                    dl |= mix_l[i] != ut_dl[i], dr |= mix_r[i] != ut_dr[i];
            }
            okl &= dl;
            okr &= dr || t == FXT_WIDE;
            if (!dl || (!dr && t != FXT_WIDE))
                snprintf(why, sizeof why, "type %u slot %u: L %u R %u", t, s, dl, dr), printf("  (%s)\n", why);
            ut.amt[s] = t == FXT_EQ3 ? 1 : 0;             /* (neutral: EQ3 ON but flat below) */
#if FELUCCA_INS_EQ3
            EQ3_V(NTRK + 1u)[EQV_LO] = EQ3_V(NTRK + 1u)[EQV_HI] = 0;
#endif
            for (k = 0; k < (t == FXT_DYN ? 4000u : 200u); k++)   /* (DYN: its AUTO release's slow envelope, seconds) */
                ut_xblock(1000u + k);
            for (k = 0; k < 8u; k++) {
                ut_xblock(2000u + k);
                for (i = 0; i < CTL; i++)
                    same &= mix_l[i] == ut_dl[i] && mix_r[i] == ut_dr[i];
            }
            okb &= same;
            if (!same)
                snprintf(why, sizeof why, "type %u slot %u: not dry at its neutral amount", t, s), printf("  (%s)\n", why);
            /* the record: the type, its amount, the input's own GATER / JUNO values, its EQ3 bands */
            ut.amt[s] = ut_on_amt(t) - 3;
            ut.v[UTV_GPAT] = 12, ut.v[UTV_GRATE] = 4, ut.v[UTV_GSHP] = 77, ut.v[UTV_JMODE] = 2;
            if (t == FXT_EQ3)
                ut.amt[s] = 1;
#if FELUCCA_INS_EQ3
            EQ3_V(NTRK + 1u)[EQV_MID] = -31, EQ3_V(NTRK + 1u)[EQV_FH] = 40;
#endif
            n = fxr_encode(b);
            ut_defaults();
#if FELUCCA_INS_EQ3
            eq3_defaults();
#endif
            k = fxr_decode(b, n, 1) && ut.slot[s] == t && ut.slot[s ^ 1u] == FXT_NONE &&
                ut.amt[s] == (t == FXT_EQ3 ? 1 : ut_on_amt(t) - 3) && ut.v[UTV_GPAT] == 12 && ut.v[UTV_GRATE] == 4 &&
                ut.v[UTV_GSHP] == 77 && ut.v[UTV_JMODE] == 2;
#if FELUCCA_INS_EQ3
            k = k && EQ3_V(NTRK + 1u)[EQV_MID] == -31 && EQ3_V(NTRK + 1u)[EQV_FH] == 40;
#endif
            oke &= k != 0u;
            if (!k)
                snprintf(why, sizeof why, "type %u slot %u: the record", t, s), printf("  (%s)\n", why);
        }
    }
    if (why[0])
        printf("  (%s)\n", why);
    check("... each loads on S1 and on S2 (alone there)", okt && nt > 0u);
    check("... each heard on L, and on R (WIDE: its side into L only)", okl && okr);
    check("... each at its neutral amount (EQ3: flat) lets go: the dry input, bit for bit", okb);
    check("... each back from the record (UT_TLV: the type, its amount, GATER's PAT RATE SHAPE, JUNO's MODE; EQ3's bands)", oke);
    {   /* an UT_TLV of before (11 bytes): GATER's and JUNO's values their defaults */
        static const uint8_t old[UT_TLV_N0] = {100, 0, 0, 1, 0, 0, 0, FXT_DIST, FXT_NONE, 90, 0};
        ut.v[UTV_GPAT] = 3, ut.v[UTV_JMODE] = 1;
        ut_untlv(old, sizeof old);
        check("an UT_TLV of 11 bytes (before): read, GATER's and JUNO's values the defaults", ut.v[UTV_LVL] == 100 &&
              ut.slot[0] == (FELUCCA_FX_DIST ? FXT_DIST : FXT_NONE) && ut.v[UTV_GPAT] == TP[P_TGPAT].def && ut.v[UTV_JMODE] == 0);
    }
#if FELUCCA_INS_CHORUS
    {   /* the pool: CHRS on every instance's track, then on the input: FULL (dry) until a track gives its own back */
        uint32_t same = 1;
        static const uint8_t C2[2] = {FXT_CHRS, FXT_NONE};
        ut_defaults();
        for (k = 0; k < FELUCCA_INS_CHORUS; k++)
            ins_put(k, C2);
        ut_poll();
        ut_load(0, FXT_CHRS);
        ut.amt[0] = 100;
        ut_poll();
        for (k = 0; k < 40u; k++) {
            ut_xblock(k);
            for (i = 0; i < CTL; i++)
                same &= mix_l[i] == ut_dl[i] && mix_r[i] == ut_dr[i];
        }
        check("the pool: CHRS on the input with every instance taken: FULL, the input dry", fxi_ostate(FXI_UT, FXT_CHRS) == FXI_FULL && same);
        ins_defaults();
        for (k = 0; k < 40u; k++)
            ut_xblock(k), ut_poll();
        check("... a track gives its own back: the input's (OK)", fxi_ostate(FXI_UT, FXT_CHRS) == FXI_OK);
        ut_defaults();
        for (k = 0; k < 40u; k++)
            ut_xblock(k), ut_poll();
    }
#endif
    ut_defaults();
    host_ua_on = 0, clk_blk = 0;
}
#endif

int main(void)
{
    host_dyn_eq_defaults();                          /* (DYN EQ3: track values, felucca_init's defaults) */
    t_layout();
    t_ins();
    t_comp();
    t_audio();
    t_dbus();
    t_kit();
    t_dins();
    t_map();
    t_record();
    t_stores();
    t_master();
    t_lanes();
#if FELUCCA_USB_AUDIO
    t_usb();
    t_usb_types();
#endif
    printf("fx_slots test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
