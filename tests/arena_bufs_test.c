/* SPDX-License-Identifier: GPL-3.0-only */
/* The arena's other buffers (firmware/src/core/arena_bufs.c; docs/SD-MODE-DESIGN.md 6b): built with FELUCCA_FX_ARENA,
 * WAVE, the X0X 909 and 808 and the SLICER (run_tests.sh), the delay's line 16384 samples, and an arena of one FM6
 * part's memory, every effect line, WAVE's table, one SLICER recording, both X0X machines and 64 B:
 *   1. at boot nothing is placed (no STUT, no WAVE track, the kit no X0X one)
 *   2. SLICER STUT on track 1: its recording placed at the next pass, zeroed, written by the live steps, the repeats
 *      heard; OFF: its pointer 0 at once (the repeat ended first), given back two blocks later, the audio runs on
 *   3. WAVE on part 2: its table placed, built by wt_service, heard; another engine: given back
 *   4. the X0X 909 kit: its machine placed, a hit plays on it; a lane on an 808 voice: the 808 placed too; the kit and
 *      the lane elsewhere: both given back once their voices are silent
 *   5. refused: a second STUT track finds no room: bypassed (never a repeat), MEM FULL SLICER said once, its card FULL,
 *      kept in the project; placed by itself once the first track's is given back; never while a part's refused change
 *      waits. A change to WAVE with no room for its table: refused as any engine change, a part proposed and dropped,
 *      then on WAVE with its table; on a load: the list page (the table its need, FITS with a part dropped). The X0X kit with no room:
 *      its hits play the stand-in kit
 *   6. a part's change that fits once a buffer is back waits for it (mem_retry, fxm_pending), then goes through
 *   7. random switches (STUT on and off, WAVE, FM6, the kit, a lane's X0X voice, the effects, blocks between): the
 *      blocks never overlap, every pointer is its block, set only while placed; at the end everything given back
 *   arena_bufs_test */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
static struct { char msg[40]; uint32_t msg_t; uint8_t layer, hold_kind; int force; } ui;
static uint8_t settings_later;
static void ui_message(const char *m) { str_cpy(ui.msg, m, sizeof ui.msg); ui.msg_t = 40; }
static void ui_say(const char *a, const char *b) { (void)b; ui_message(a); }
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
#include "../firmware/src/core/model.c"
#if !FXM_NEED || !FELUCCA_ENG_FM6 || !FELUCCA_ENG_WAVE || !FELUCCA_DRUM_X909 || !FELUCCA_DRUM_X808 || !FELUCCA_FX_SLICER
#error "arena_bufs_test: built with FELUCCA_FX_ARENA, FELUCCA_ARENA_BYTES, FM6, WAVE, the X0X kits and the SLICER"
#endif

static int bad, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { bad++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } else printf("ok   " __VA_ARGS__), printf("\n"); } while (0)

#define ENG_WAVE ((uint32_t)ENG_SLOT_WAVE)
static int32_t out_l[CTL * 4096];
static uint32_t out_n;
static void blocks(uint32_t n)                          /* n audio blocks, the left output kept (out_l, out_n) */
{
    static int32_t o[CTL * 2];
    uint32_t i;
    out_n = 0;
    while (n--) {
        events_block(CTL);
        mix_block(o, CTL);
        for (i = 0; i < CTL && out_n < sizeof out_l / sizeof out_l[0]; i++)
            out_l[out_n++] = o[2u * i];
    }
}
static int64_t energy(void)
{
    int64_t e = 0;
    uint32_t i;
    for (i = 0; i < out_n; i++)
        e += (int64_t)out_l[i] * out_l[i] >> 8;
    return e;
}
static void pass(void)                                  /* a main loop pass (main.c): the arena, the instances, WAVE */
{
    fxm_poll();
    arb_poll();
    fxi_poll();
    wt_service();
}
static void settle(uint32_t n)                          /* passes with blocks between */
{
    while (n--) {
        pass();
        blocks(2);
    }
    pass();
}
static void *arb_ptr(uint32_t c)                        /* client c's pointer as the audio reads it */
{
    if (c < (uint32_t)ARB_SL0 + NTRK)
        return sl_bp[c - ARB_SL0];
    if (c == (uint32_t)ARB_WAVE)
        return wt_mip;
    if (c == (uint32_t)ARB_X909)
        return x909_p;
    return x808_p;
}
static int zeros(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        if (*b++)
            return 0;
    return 1;
}
static int any16(const int16_t *p, uint32_t n)
{
    while (n--)
        if (*p++)
            return 1;
    return 0;
}
/* the invariants: blocks inside the arena and apart; each buffer's pointer its block while placed (ON), 0 otherwise;
 * WAVE's scratch after its copies; the effects' lines as fx_arena_test checks them */
static int arena_ok(void)
{
    uint32_t i, j, c;
    for (i = 0; i < ARN_PARTS; i++) {
        if (!arn_cur[i].len)
            continue;
        if (arn_cur[i].off + arn_cur[i].len > ARN_BYTES)
            return 0;
        for (j = i + 1u; j < ARN_PARTS; j++)
            if (arn_cur[j].len && arn_cur[i].off < arn_cur[j].off + arn_cur[j].len &&
                arn_cur[j].off < arn_cur[i].off + arn_cur[i].len)
                return 0;
    }
    for (c = 0; c < ARB_NC; c++) {
        void *p = arb_ptr(c);
        if (arb_st[c] == FXM_ON ? p != arn_ptr(ARB_K(c)) || !p || arn_cur[ARB_K(c)].len < arb_bytes(c) : p != 0)
            return 0;
        if (arb_st[c] == FXM_NONE && arn_cur[ARB_K(c)].len)
            return 0;
    }
    if (wt_mip ? (uint8_t *)wt_fr != (uint8_t *)wt_mip + WT_NF * WT_PTS * 2u || wt_fi != wt_fr + 1024 : wt_fr || wt_fi)
        return 0;
    for (c = 0; c < FXM_NC; c++) {
        void *p = c == FXM_CHO ? (void *)cho_buf : c == FXM_DLY ? (void *)dly_buf : c == FXM_REV ? (void *)rev_line
                                                                                               : (void *)fxa_ins;
        if (p != arn_ptr(FXM_K(c)))
            return 0;
    }
    return 1;
}
static void quiet(void)                                 /* every part silent, no SLICER, a kit with no X0X machine */
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        set_engine_of(&trk[k], eng_silent_slot());
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_SLCR] = SL_OFF;
    TDRUM->p[P_E0] = (int16_t)DRUM_SAMPLED;
    for (k = 0; k < DRUM_LANES; k++)
        dl.src[k] = DL_KIT;
    blocks(256);
    settle(8);
}
static void put_chrs(uint32_t k, uint32_t on)
{
    uint8_t s[2] = {(uint8_t)(on ? FXT_CHRS : FXT_NONE), FXT_NONE};
    ins_put(k, s);
}
static void stut(uint32_t k, uint32_t on)
{
    track_t *t = &trk[k];
    t->p[P_SLCR] = (int16_t)(on ? SL_STUT : SL_OFF);
    t->p[P_SLPAT] = 1;                                  /* (x.x.: a repeat every second step) */
    t->p[P_SLRATE] = 16;                                /* (1/16) */
    t->p[P_SLDEPTH] = 127;
}
static int64_t part_play(uint32_t k, uint32_t frames)   /* part k's note held over frames blocks: the output's energy */
{
    int64_t e = 0;
    uint32_t f;
    trk_note_on(&trk[k], 60, 110);
    for (f = 0; f < frames; f += 64u) {
        blocks(frames - f < 64u ? frames - f : 64u);
        e += energy();
        pass();
    }
    trk_note_off(&trk[k], 60);
    return e;
}
static uint32_t drum_hits(uint32_t note, uint32_t n)    /* n hits a beat apart: the X0X channels that sounded */
{
    uint32_t live = 0, i, j;
    for (i = 0; i < n; i++) {
        drum_on(note, 110);
        for (j = 0; j < FS / 2u / CTL; j++) {
            blocks(1);
            live |= xc.live;
        }
        pass();
    }
    return live;
}

int main(void)
{
    uint32_t c, f, k, SL = SL_LEN * 2u, FM = arn_round(part_mem_bytes(ENG_IX_FM6)), lines = 0;
    uint32_t X9 = arn_round(x0x_mem_bytes(0)), X8 = arn_round(x0x_mem_bytes(1)), WT = arn_round(WT_MEM_BYTES);
    int64_t e_off, e_on;
    track_t *t0 = &trk[0], *t1 = &trk[1], *t2 = &trk[2];
    host_tracks_init();
    fxi_init();
    for (c = 0; c < FXM_NC; c++)
        lines += arn_round(FXM_BYTES[c]);
    CHECK(ARB_NC == NTRK + 3u && ARN_BYTES >= FM + lines + WT + SL + X9 + X8 && ARN_BYTES <= FM + lines + WT + SL + X9 + X8 + 64u,
          "the premise: one FM6, the lines, WAVE's table, one SLICER recording and both X0X machines fit (arena %u B: FM6 %u, "
          "lines %u, WAVE %u, SLICER %u, 909 %u, 808 %u)", (unsigned)ARN_BYTES, (unsigned)FM, (unsigned)lines,
          (unsigned)WT, (unsigned)SL, (unsigned)X9, (unsigned)X8);
    if (bad)
        return 1;
    /* 1. at boot */
    quiet();
    for (c = 0, f = 1; c < ARB_NC; c++)
        f &= arb_st[c] == FXM_NONE && !arb_ptr(c);
    CHECK(f && !arb_full && arena_ok(), "at boot: no SLICER on STUT, no WAVE track, no X0X kit: nothing placed");
    /* 2. the SLICER */
    set_engine_of(t0, 0);                               /* (ANALOG: keeps nothing in the arena) */
    blocks(64);
    stut(0, 1);
    pass();
    CHECK(arb_st[ARB_SL0] == FXM_ON && sl_bp[0] && zeros(sl_bp[0], SL) && !sl_bp[1] && arena_ok(),
          "STUT on track 1: its recording placed at the next pass, zeroed; none for the other tracks");
    slicer_start(0);
    e_on = part_play(0, 600);
    CHECK(any16(sl_bp[0], SL_LEN) && e_on > 0, "... a note through it: the live steps recorded into the arena, heard");
    stut(0, 0);
    pass();
    CHECK(arb_st[ARB_SL0] != FXM_ON || sl[0].loop || sl[0].rec_on, "OFF: kept while a repeat still plays");
    for (f = 0; f < 200u && arb_st[ARB_SL0] == FXM_ON; f++) {
        blocks(1);
        pass();
    }
    CHECK(arb_st[ARB_SL0] == FXM_GOING && !sl_bp[0] && arn_cur[ARB_K(ARB_SL0)].len && arena_ok(),
          "... the repeat over: its pointer 0 at once, its block kept two blocks");
    blocks(2);
    pass();
    CHECK(arb_st[ARB_SL0] == FXM_NONE && !arn_cur[ARB_K(ARB_SL0)].len && arena_ok(), "... two blocks later: given back");
    stut(0, 1);
    t0->p[P_SLCR] = SL_OFF;                             /* (OFF as the reference: the same note, no SLICER) */
    slicer_start(0);
    e_off = part_play(0, 600);
    CHECK(e_off > 0 && arb_st[ARB_SL0] == FXM_NONE, "... OFF played: nothing placed (the reference for the bypass)");
    blocks(256);
    /* 3. WAVE */
    set_engine_of(t1, ENG_WAVE);
    blocks(64);
    pass();
    CHECK(arb_st[ARB_WAVE] == FXM_ON && wt_mip && wt_have < 0 && arena_ok(), "WAVE on part 2: its table's block placed, the table not built yet");
    settle(8);
    CHECK(wt_have == (int16_t)wt_wanted() && any16(wt_mip[0], WT_PTS), "... built by wt_service into the arena");
    CHECK(part_play(1, 200) > 0, "... heard");
    set_engine_of(t1, eng_silent_slot());
    blocks(64);
    settle(2);
    CHECK(arb_st[ARB_WAVE] == FXM_NONE && !wt_mip && wt_have < 0 && arena_ok(), "part 2 off WAVE: its table given back");
    CHECK(part_play(1, 64) >= 0, "... the audio runs on with its pointer 0");
    /* 4. the X0X machines */
    TDRUM->p[P_E0] = (int16_t)DRUM_UID_X909;
    pass();
    CHECK(arb_st[ARB_X909] == FXM_ON && x909_p && !x808_p && arena_ok(), "the X0X 909 kit: its machine placed, not the 808");
    k = drum_hits(36, 2);
    CHECK((k & 1u) && (xd.ready & 1u), "... a kick plays on it (its channel live, set up at the hit)");
    dl.src[3] = (uint8_t)(DL_X808 + 1u);                /* (a lane on the 808's snare) */
    pass();
    CHECK(arb_st[ARB_X808] == FXM_ON && x808_p && arena_ok(), "a lane on an 808 voice: the 808 placed too");
    TDRUM->p[P_E0] = (int16_t)DRUM_SAMPLED;
    dl.src[3] = DL_KIT;
    pass();
    CHECK(xc.live & 1u ? arb_st[ARB_X909] == FXM_ON : 1, "the kit and the lane elsewhere: the 909 kept while its kick sounds");
    for (f = 0; f < 4000u && (arb_st[ARB_X909] != FXM_NONE || arb_st[ARB_X808] != FXM_NONE); f++) {
        blocks(1);
        pass();
    }
    CHECK(!x909_p && !x808_p && arb_st[ARB_X909] == FXM_NONE && arb_st[ARB_X808] == FXM_NONE && arena_ok(),
          "... silent: both given back");
    /* 5. refused */
    put_chrs(0, 1);                                     /* (a limited insert: the inserts' own room placed too) */
    set_engine_of(t0, ENG_IX_FM6);
    set_engine_of(t1, ENG_WAVE);
    blocks(64);
    stut(0, 1);
    TDRUM->p[P_E0] = (int16_t)DRUM_UID_X909;
    dl.src[3] = (uint8_t)(DL_X808 + 1u);
    settle(8);
    CHECK(t0->engine == ENG_IX_FM6 && cho_buf && dly_buf && rev_line && fxa_ins && wt_mip && sl_bp[0] && x909_p &&
          x808_p && !arb_full && arn_free_bytes() < 64u && arena_ok(),
          "FM6, the lines and the own room, WAVE, STUT on track 1, both machines: all placed, the arena full (%u B left)",
          (unsigned)arn_free_bytes());
    (void)arb_said();
    stut(2, 1);
    pass();
    {
        const char *m = arb_said();
        CHECK(arb_st[ARB_SL0 + 2] == FXM_NONE && !sl_bp[2] && sl_mode(t2) == SL_OFF && (arb_full >> (ARB_SL0 + 2) & 1u) &&
              t2->p[P_SLCR] == SL_STUT && m && !strcmp(m, "MEM FULL SLICER") && arb_sl_refused(&t2->p[P_SLCR]) &&
              !arb_sl_refused(&t0->p[P_SLCR]),
              "STUT on track 3: no room: refused, bypassed (OFF), MEM FULL SLICER, its card FULL, kept in the project");
    }
    pass();
    CHECK(!arb_said() && (arb_full >> (ARB_SL0 + 2) & 1u), "... tried again every pass, said once only");
    {                                                   /* the bypass: no repeat, no gate, the live sound */
        uint32_t rep = 0;
        set_engine_of(t2, 0);                           /* (ANALOG: its preset's SLICER OFF, STUT again) */
        blocks(64);
        stut(2, 1);
        pass();
        slicer_start(0);
        trk_note_on(t2, 60, 110);
        for (f = 0; f < 600u; f++) {
            blocks(1);
            rep |= sl[2].loop | (uint32_t)sl[2].w | (uint32_t)sl[2].gc | sl[2].rec_on;
        }
        trk_note_off(t2, 60);
        CHECK(!rep && !sl_bp[2], "... a note through it: never a repeat or a closed step, nothing recorded (OFF's path)");
    }
    stut(0, 0);
    for (f = 0; f < 400u && !sl_bp[2]; f++) {
        blocks(1);
        pass();
    }
    CHECK(sl_bp[2] && arb_st[ARB_SL0 + 2] == FXM_ON && !sl_bp[0] && !(arb_full >> (ARB_SL0 + 2) & 1u) && arena_ok(),
          "track 1's STUT off: its block given back, track 3's placed by itself");
    CHECK(!arb_sl_refused(&t2->p[P_SLCR]), "... its card no longer FULL");
    stut(2, 0);
    for (f = 0; f < 400u && arb_st[ARB_SL0 + 2] != FXM_NONE; f++) {
        blocks(1);
        pass();
    }
    set_engine_of(t2, eng_silent_slot());
    stut(0, 1);
    part_mem_refused.on = 1;                            /* (a part's refused change waits: the buffers wait) */
    settle(2);
    CHECK(!sl_bp[0] && arb_st[ARB_SL0] == FXM_NONE, "a part's refused change waiting: STUT not placed meanwhile");
    part_mem_refused.on = 0;
    pass();
    CHECK(sl_bp[0] && arena_ok(), "... none waiting: placed");
    /* WAVE with no room */
    set_engine_of(t1, eng_silent_slot());
    blocks(64);
    settle(4);
    set_engine_of(t2, ENG_IX_FM6);                      /* (a second FM6: WAVE's room taken) */
    blocks(64);
    settle(2);
    (void)arb_said();
    set_engine_of(t1, ENG_WAVE);
    CHECK(part_mem_refused.on && part_mem_refused.part == 1u && part_mem_refused.eng == ENG_WAVE &&
          part_mem_refused.drop > 0 && t1->eng_req != ENG_WAVE && !wt_mip && !arn_has_next[ARB_K(ARB_WAVE)],
          "WAVE on part 2 with no room for its table: the change refused as any engine's, the parts to drop proposed "
          "(%d), nothing set aside", part_mem_refused.drop);
    {
        uint32_t x;
        CHECK(mem_offer(&x) && x < NPART && x != 1u, "... the proposal: a part first (part %u)", (unsigned)(x + 1u));
    }
    mem_drop(2);                                        /* (YES on "DROP T3 FM6") */
    for (f = 0; f < 50u && part_mem_refused.on; f++) {
        pass();
        blocks(1);
        (void)mem_retry();
    }
    blocks(64);
    settle(8);
    CHECK(!part_mem_refused.on && t1->engine == ENG_WAVE && t2->silent && wt_mip && wt_have == (int16_t)wt_wanted() &&
          !arb_full && !arb_said() && arena_ok(), "part 3 dropped (a placeholder): part 2 on WAVE, its table placed and "
          "built, no MEM FULL");
    set_engine_of(t2, eng_silent_slot());
    blocks(64);
    settle(2);
    CHECK(part_play(1, 200) > 0, "... heard");
    /* on a load: WAVE's table on the list page (model.c mem_load_*) */
    set_engine_of(t1, eng_silent_slot());
    blocks(64);
    settle(4);
    set_engine_of(t2, ENG_IX_FM6);
    blocks(64);
    settle(4);
    CHECK(!wt_mip && t2->engine == ENG_IX_FM6, "load: part 2 off WAVE (its table given back), part 3 on FM6");
    part_mem_load_begin();                              /* (as project.c's load: part 2 on WAVE, refused, a placeholder) */
    if (part_mem_load(1, ENG_WAVE))
        t1->silent = 1, t1->eng_req = (uint8_t)eng_silent_slot();
    CHECK((part_mem_refused.lmask >> 1 & 1u) && part_mem_refused.leng[1] == ENG_WAVE && t1->silent &&
          !arn_has_next[ARB_K(ARB_WAVE)], "load: part 2's WAVE refused (no room for its table), a placeholder");
    {
        uint8_t it[NPART + 3u];
        uint32_t n = mem_load_items(it), i, has3 = 0;
        for (i = 0; i < n; i++)
            has3 |= it[i] == 2u;
        CHECK(n >= 3u && it[0] == 1u && has3 && mem_item_bytes(1) == WT_MEM_BYTES && mem_load_need(0) == WT_MEM_BYTES,
              "load: the list: the refused part 2 first (its need: the table, %u B), part 3 among the drops", (unsigned)mem_load_need(0));
        CHECK(!mem_load_fits(0) && mem_load_fits(1u << 2) && !arn_has_next[ARB_K(ARB_WAVE)],
              "load: FITS only with part 3 dropped (a trial: nothing set aside)");
    }
    mem_load_commit(1u << 2);
    for (f = 0; f < 50u && mem_load_settle() != 1; f++) {
        blocks(1);
        pass();
    }
    blocks(64);
    settle(8);
    CHECK(!part_mem_refused.lmask && t1->engine == ENG_WAVE && !t1->silent && t2->silent && wt_mip &&
          wt_have == (int16_t)wt_wanted() && !arb_full && arena_ok(),
          "load: part 3 dropped: part 2 on WAVE with the project's sound, its table placed and built");
    /* the X0X kit with no room: the stand-in */
    TDRUM->p[P_E0] = (int16_t)DRUM_SAMPLED;
    dl.src[3] = DL_KIT;
    blocks(4000);
    settle(4);
    set_engine_of(t2, ENG_IX_FM6);
    blocks(64);
    settle(4);
    k = arn_free_bytes();
    (void)arb_said();
    TDRUM->p[P_E0] = (int16_t)DRUM_UID_X909;
    pass();
    {
        const char *m = arb_said();
        uint32_t live;
        CHECK(k < X9 && !x909_p && (arb_full >> ARB_X909 & 1u) && m && !strcmp(m, "MEM FULL X0X"),
              "the X0X 909 kit with no room (%u B free): refused, MEM FULL X0X", (unsigned)k);
        live = drum_hits(36, 2);
        CHECK(!live && energy() >= 0, "... its kick plays the stand-in kit (no X0X channel)");
    }
    /* 6. a part's change waits for a buffer going back */
    TDRUM->p[P_E0] = (int16_t)DRUM_SAMPLED;
    set_engine_of(t2, eng_silent_slot());
    blocks(4000);
    settle(4);
    TDRUM->p[P_E0] = (int16_t)DRUM_UID_X909;            /* (the arena full again: FM6, the lines, WAVE, STUT, the machines) */
    dl.src[3] = (uint8_t)(DL_X808 + 1u);
    settle(4);
    CHECK(wt_mip && t0->engine == ENG_IX_FM6 && sl_bp[0] && x909_p && x808_p && arn_free_bytes() < FM,
          "FM6 on part 1, WAVE on part 2, STUT, both machines");
    set_engine_of(t2, ENG_IX_FM6);
    CHECK(part_mem_refused.on && part_mem_refused.part == 2u, "FM6 on part 3: refused");
    part_mem_refused.on = 0;
    set_engine_of(t1, eng_silent_slot());
    blocks(64);
    pass();                                             /* (WAVE let go: its table on its way back) */
    set_engine_of(t2, ENG_IX_FM6);
    CHECK(part_mem_refused.on && fxm_pending() && mem_retry() == 0,
          "part 2 off WAVE, FM6 on part 3 again: refused while the table is still held, mem_retry waits for it");
    for (f = 0; f < 50u && part_mem_refused.on; f++) {
        pass();
        blocks(1);
        (void)mem_retry();
    }
    blocks(64);
    CHECK(!part_mem_refused.on && t2->engine == ENG_IX_FM6 && !wt_mip && arena_ok(), "... the table given back: part 3 on FM6");
    put_chrs(0, 0);
    /* 7. random switches */
    quiet();
    {
        uint32_t n, okall = 1, worst = 0, nfull = 0, nsl = 0, nwt = 0, nx = 0, nwr = 0;
        srand(11);
        for (n = 0; n < 20000u && okall; n++) {
            uint32_t r = (uint32_t)rand(), op = r % 8u, a = (r >> 8) % 4u;
            if (op == 0u)
                stut(a, (r >> 12) & 1u);
            else if (op == 1u)
                set_engine_of(&trk[a % NPART], (r >> 12) & 1u ? ENG_WAVE : eng_silent_slot());
            else if (op == 2u)
                set_engine_of(&trk[a % NPART], (r >> 12) & 1u ? ENG_IX_FM6 : 0u);
            else if (op == 3u)
                TDRUM->p[P_E0] = (int16_t)((r >> 12) % 3u == 0u ? DRUM_UID_X909 : (r >> 12) % 3u == 1u ? DRUM_UID_X808 : DRUM_SAMPLED);
            else if (op == 4u)
                dl.src[a] = (uint8_t)((r >> 12) & 1u ? DL_X808 + a : (r >> 13) & 1u ? DL_X909 + a : DL_KIT);
            else if (op == 5u)
                fxs_load(1u + a % 3u, (r >> 12) & 1u ? FXS_LT[a % 3u] : FXT_NONE);
            else if (op == 6u)
                drum_on(36u + a, 110);
            else if (op == 7u)
                trk_note_on(&trk[a % NPART], 60u + a, 100), trk_note_off(&trk[(a + 1u) % NPART], 60u + (a + 1u));
            if (part_mem_refused.on && (r >> 20) & 1u)
                part_mem_refused.on = 0;                /* (given up, as MEM FULL) */
            pass();
            wt_service();
            blocks(1u + (r >> 16) % 3u);
            okall = arena_ok();
            worst = n;
            nfull += arb_full != 0;
            nsl += sl_bp[0] || sl_bp[1] || sl_bp[2] || sl_bp[3];
            nwt += wt_mip != 0;
            nx += x909_p || x808_p;
            nwr += part_mem_refused.on && part_mem_refused.eng == ENG_WAVE;
            (void)arb_said();
        }
        CHECK(okall && nfull > 50u && nwr > 50u && nsl > 1000u && nwt > 1000u && nx > 1000u,
              "20,000 random switches: the invariants held at every step (to %u; steps with a buffer refused %u, a change "
              "to WAVE refused %u, a SLICER recording placed %u, WAVE's table %u, an X0X machine %u)", (unsigned)worst,
              (unsigned)nfull, (unsigned)nwr, (unsigned)nsl, (unsigned)nwt, (unsigned)nx);
        part_mem_refused.on = 0;
        quiet();
        blocks(8000);
        settle(8);
        for (c = 0, f = 1; c < ARB_NC; c++)
            f &= arb_st[c] == FXM_NONE && !arb_ptr(c) && !arn_cur[ARB_K(c)].len;
        CHECK(f && !arb_full && !arb_pending() && arena_ok(), "... everything let go: every buffer given back");
    }
    printf("arena bufs test: %d checks, %d failed\n", checks, bad);
    return bad ? 1 : 0;
}
