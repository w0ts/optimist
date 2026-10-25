/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects' lines in the arena (firmware/src/fx/fx_arena.c; docs/SD-MODE-DESIGN.md 6b, phase 3b): built with
 * FELUCCA_FX_ARENA and an arena of one FM6 part's memory plus every effect line and 40 B (run_tests.sh), CHRS and ECHO
 * built, the delay's line 16384 samples:
 *   1. placed by need: the sends in the master slots get their lines at the first pass, zeroed, the gate open; nothing
 *      for the inserts' own room while no limited type is held
 *   2. the lines sound (the wet written into the arena)
 *   3. a send out of the slots: gated, given back two blocks later, the audio runs on with its pointer 0
 *   4. back in a slot: placed again, zeroed, heard
 *   5. a part's change that fits once an effect's line is back waits for it (mem_retry), then goes through; the drop
 *      proposal names parts only
 *   6. refused: the send stays in its slot (the project keeps it), bypassed, FULL, said once; never placed while a
 *      part's refused change waits; restored by itself once a part is dropped (its block idle, taken back)
 *   7. the inserts' own room: placed while a limited type is held, given back after the last fade-out; refused, the
 *      chorus send's line (in no slot) placed and lent instead; the drop proposal: the parts, then the effects' lines
 *      (the largest first), a dropped effect bypassed as when refused, the part's change made, the effect back later
 *   8. random switches (sends in and out of the slots, inserts, parts' engines, drops, blocks between): the blocks never
 *      overlap, every pointer is its block, a gate open only on a placed line; at the end everything given back
 *   fx_arena_test */
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
#if !FXM_NEED || !FELUCCA_INS_CHORUS || !FELUCCA_INS_DELAY || !FELUCCA_ENG_FM6 || !FXA_I
#error "fx_arena_test: built with FELUCCA_FX_ARENA, FELUCCA_ARENA_BYTES, FM6, CHRS and ECHO (run_tests.sh)"
#endif

static int bad, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { bad++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } else printf("ok   " __VA_ARGS__), printf("\n"); } while (0)

static void blocks(uint32_t n)
{
    static int32_t o[CTL * 2];
    while (n--) {
        events_block(CTL);
        mix_block(o, CTL);
    }
}
static void pass(void)                                  /* a main loop pass (main.c): the arena, then the instances */
{
    fxm_poll();
    fxi_poll();
}
static void settle(uint32_t n)                          /* passes with blocks between (a gate's two blocks, a fade) */
{
    while (n--) {
        pass();
        blocks(2);
    }
    pass();
}
/* a project's load as storage/project.c proj_apply makes it (its engines only): part k on FM6 when bit k of want, else
 * on an engine that keeps nothing; a part with no room a placeholder, recorded (engines.c part_mem_load) */
static void load_like(uint32_t want)
{
    uint32_t k;
    fm1_irq_off();
    part_mem_load_begin();
    for (k = 0; k < NPART; k++) {
        uint32_t e = (want >> k & 1u) ? ENG_IX_FM6 : eng_silent_slot(), no = part_mem_load(k, e) != 0;
        trk[k].silent = (uint8_t)no;
        trk[k].eng_req = (uint8_t)(no ? eng_silent_slot() : e);
    }
    fm1_irq_on();
}
static void *fx_ptr(uint32_t c)
{
    return c == FXM_CHO ? (void *)cho_buf : c == FXM_DLY ? (void *)dly_buf : c == FXM_REV ? (void *)rev_line : (void *)fxa_ins;
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
/* the invariants: blocks inside the arena and apart, each line's pointer its block, a gate open only on a placed line,
 * the own room's units taken while it is not placed, every instance's line where its units are */
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
    for (c = 0; c < FXM_NC; c++) {
        void *p = fx_ptr(c);
        if (p != arn_ptr(FXM_K(c)) || (p && arn_cur[FXM_K(c)].idle))
            return 0;
        if (c < 3u && ((fxm_st[c] == FXM_NONE) != !p || (fxm_st[c] == FXM_ON) != !(fxm_off >> c & 1u)))
            return 0;
    }
    if (!fxa_ins)
        for (i = FXA_S; i < FXA_N; i++)
            if (!fxa_busy(i))
                return 0;
    for (j = 0; j < FXI_NL; j++)
        for (i = 0; i < FXI_TN[j]; i++) {
            uint32_t u = fxa_u[j][i], r;
            if (fxi_own[j][i] == FXI_NONE)
                continue;
            r = fxa_reg(u);
            if ((r == FXL_NS && !fxa_ins) || (r < FXL_NS && fxm_st[r ? FXM_DLY : FXM_CHO] != FXM_ON))
                return 0;
        }
    return 1;
}
static void quiet(void)                                 /* every part on an engine that keeps nothing, no insert */
{
    uint32_t k;
    uint8_t none[2] = {FXT_NONE, FXT_NONE};
    for (k = 0; k < NPART; k++)
        set_engine_of(&trk[k], eng_silent_slot());
    for (k = 0; k < NTRK; k++)
        ins_put(k, none);
    blocks(64);
    settle(8);
}
static void put_chrs(uint32_t k, uint32_t on)
{
    uint8_t s[2] = {(uint8_t)(on ? FXT_CHRS : FXT_NONE), FXT_NONE};
    ins_put(k, s);
}

int main(void)
{
    uint32_t c, f, k, sum = 0, FM = arn_round(part_mem_bytes(ENG_IX_FM6));
    track_t *t0 = &trk[0], *t1 = &trk[1], *t2 = &trk[2];
    host_tracks_init();
    fxi_init();
    for (c = 0; c < FXM_NC; c++)
        sum += FXM_BYTES[c];
    CHECK(ARN_BYTES >= FM + sum && ARN_BYTES < FM + sum + 40u + 1u && ARN_BYTES < 3u * FM + sum - FXM_BYTES[FXM_CHO],
          "the premise: one FM6 and every line fit, not 3 FM6 and the lines but CHO's (arena %u B: FM6 %u, lines %u %u %u %u)",
          (unsigned)ARN_BYTES, (unsigned)FM, FXM_BYTES[0], FXM_BYTES[1], FXM_BYTES[2], FXM_BYTES[3]);
    if (bad)
        return 1;                                       /* (the checks below are laid out for it) */
    CHECK(fxm_off == 7u && !cho_buf && !dly_buf && !rev_line && !fxa_ins, "at boot: no line placed, every send gated");
    quiet();
    /* 1. placed by need */
    CHECK(fxm_st[FXM_CHO] == FXM_ON && fxm_st[FXM_DLY] == FXM_ON && fxm_st[FXM_REV] == FXM_ON && !fxm_off && !fxa_ins &&
          arena_ok(), "the sends in the master slots: their lines placed, the gates open; no own room (no limited type held)");
    CHECK(zeros(cho_buf, FXM_BYTES[FXM_CHO]) && zeros(dly_buf, FXM_BYTES[FXM_DLY]) && zeros(rev_line, FXM_BYTES[FXM_REV]),
          "... zeroed");
    /* 2. heard */
    for (k = 0; k < NPART; k++)
        trk[k].p[P_CHOR] = trk[k].p[P_DLY] = trk[k].p[P_REV] = 100;
    trk_note_on(t0, 60, 110);
    blocks(200);
    trk_note_off(t0, 60);
    CHECK(any16(cho_buf, CHO_LEN) && any16(dly_buf, DLY_LEN) && any16(rev_line, REV_LINE_N),
          "a note sent to CHO DLY REV: each bus writes its line in the arena");
    /* 3. out of the slots */
    fxs_load(3, FXT_NONE);
    pass();
    CHECK(fxm_st[FXM_REV] == FXM_GOING && (fxm_off & 4u) && rev_line, "REV out of the slots: gated at once, its line kept two blocks");
    blocks(1);
    pass();
    CHECK(fxm_st[FXM_REV] == FXM_GOING && rev_line, "... one block later: still kept (CPU1's kept block)");
    blocks(1);
    pass();
    CHECK(fxm_st[FXM_REV] == FXM_NONE && !rev_line && !arn_cur[FXM_K(FXM_REV)].len && arena_ok(), "... two blocks: given back");
    trk_note_on(t0, 62, 110);
    blocks(400);
    trk_note_off(t0, 62);
    CHECK(1, "... the audio runs on with the reverb's pointer 0 (it never reads it: no fault)");
    /* 4. back */
    fxs_load(3, FXT_REV);
    pass();
    CHECK(fxm_st[FXM_REV] == FXM_ON && !(fxm_off & 4u) && rev_line && zeros(rev_line, FXM_BYTES[FXM_REV]) && arena_ok(),
          "REV back in a slot: placed again, zeroed, the gate open");
    trk_note_on(t0, 64, 110);
    blocks(200);
    trk_note_off(t0, 64);
    CHECK(any16(rev_line, REV_LINE_N), "... heard: its line written");
    blocks(4u * FS / CTL);
    /* 5. a part's change waits for an effect's line going back */
    set_engine_of(t0, ENG_IX_FM6);
    blocks(64);
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_TCHRS] = trk[k].p[P_TECHO] = 100;   /* (heard: a let go fades out) */
    put_chrs(0, 1);
    settle(2);
    CHECK(t0->engine == ENG_IX_FM6 && fxa_ins && fxi_state(0, 0) == FXI_OK && arena_ok(),
          "FM6 on part 1, CHRS on track 1: the own room placed, its instance in it: the arena full");
    set_engine_of(t1, ENG_IX_FM6);
    CHECK(part_mem_refused.on && part_mem_refused.part == 1u && t1->eng_req != ENG_IX_FM6 && part_mem_refused.drop == 1,
          "FM6 on part 2: refused; the proposal names part 1 (parts only: no effect)");
    part_mem_refused.on = 0;                            /* (the UI's MEM FULL: given up) */
    fxs_load(3, FXT_NONE);                              /* (REV out: its line on its way back) */
    set_engine_of(t1, ENG_IX_FM6);
    CHECK(part_mem_refused.on && mem_retry() == 0 && (part_mem_refused.waiting & MEM_W_FX),
          "REV out, FM6 on part 2 again: refused while REV's line is still held, mem_retry waits for it");
    for (f = 0; f < 50u && part_mem_refused.on; f++) {
        pass();
        blocks(1);
        (void)mem_retry();
    }
    blocks(64);
    CHECK(!part_mem_refused.on && t1->engine == ENG_IX_FM6 && !rev_line && arena_ok(),
          "... REV's line given back: the change went through, part 2 on FM6");
    /* 6. refused, kept, restored */
    fxm_news = 0;
    fxs_load(3, FXT_REV);
    pass();
    CHECK(fxm_st[FXM_REV] == FXM_NONE && (fxm_off & 4u) && fxm_refused(FXT_REV) && (fxm_news & 4u) && fxs_slot[3] == FXT_REV &&
          FXS_HAS(FXT_REV) && arena_ok(), "REV back with no room: refused, gated (bypassed), FULL, said, kept in its slot");
    fxm_news = 0;
    trk_note_on(t0, 60, 110);
    blocks(200);
    trk_note_off(t0, 60);
    pass();
    CHECK(fxm_refused(FXT_REV) && !fxm_news && !rev_line, "... played: still bypassed, said once only");
    part_drop(t1);
    blocks(64);
    part_mem_refused.on = 1;                            /* (a part's refused change waiting: the effects wait) */
    settle(2);
    CHECK(fxm_st[FXM_REV] == FXM_NONE && arn_cur[1].idle, "part 2 dropped (its block idle) while a part's change waits: REV not placed");
    part_mem_refused.on = 0;
    pass();
    CHECK(fxm_st[FXM_REV] == FXM_ON && !fxm_refused(FXT_REV) && !(fxm_off & 4u) && !arn_cur[1].len && arena_ok(),
          "... none waiting: REV placed by itself, the idle block taken back");
    /* 7. the inserts' own room */
    put_chrs(0, 0);
    pass();
    CHECK(fxa_ins, "CHRS let go: the own room kept while its instance fades out");
    settle(40);
    CHECK(!fxa_ins && !arn_cur[FXM_K(FXM_INS)].len && fxi_free(FXT_CHRS) > 0u && fxi_can(0, FXT_CHRS) && arena_ok(),
          "... faded: the room given back; the UI still counts it (the arena would take it)");
    fxs_load(1, FXT_NONE);                              /* (CHO in no slot: its line given back, then lendable) */
    settle(4);
    set_engine_of(t1, ENG_IX_FM6);                      /* (three FM6: no room left for the own room) */
    set_engine_of(t2, ENG_IX_FM6);
    blocks(64);
    settle(2);
    CHECK(t1->engine == ENG_IX_FM6 && t2->engine == ENG_IX_FM6 && !cho_buf && !arn_fits(FXM_K(FXM_INS), FXM_INS_BYTES) &&
          arn_fits(FXM_K(FXM_CHO), FXM_BYTES[FXM_CHO]), "three FM6, CHO in no slot: no room for the own room, room for CHO's line");
    fxm_news = 0;
    put_chrs(2, 1);
    settle(8);
    k = fxi_at[2][0];
    CHECK(!fxa_ins && (fxm_full >> FXM_INS & 1u) && (fxm_news >> FXM_INS & 1u) && cho_buf && k != FXI_NONE &&
          ifx_chrs[k].line == cho_buf && fxi_state(2, 0) == FXI_OK && fxl_st[0] == FXL_LENT && arena_ok(),
          "CHRS on track 3: the own room refused; the chorus send's line placed and lent, the instance on it");
    put_chrs(2, 0);
    settle(40);
    CHECK(fxl_st[0] == FXL_SEND && !cho_buf && arena_ok(), "... let go: the line back to its send, then given back (CHO in no slot)");
    /* 7b. the drop proposal: the parts, then the effects' lines (the user, 2026-10-25) */
    quiet();
    fxs_set(FXS_DEF);
    set_engine_of(t0, ENG_IX_FM6);
    blocks(64);
    put_chrs(0, 1);
    settle(4);
    CHECK(t0->engine == ENG_IX_FM6 && cho_buf && dly_buf && rev_line && fxa_ins && arena_ok(),
          "drop: FM6 on part 1, CHO DLY REV and the own room: the arena full");
    set_engine_of(t1, ENG_IX_FM6);
    {
        uint32_t o[5], n = 0;
        CHECK(part_mem_refused.on && part_mem_refused.part == 1u, "drop: FM6 on part 2 refused");
        while (n < 5u && mem_offer(&o[n]))
            n++;
        CHECK(n == 4u && o[0] == 0u && o[1] == NPART + FXM_DLY && o[2] == NPART + FXM_REV && o[3] == NPART + FXM_CHO,
              "drop: proposed part 1, then the effects' lines, the largest first: DLY REV CHO (not the own room), each once");
        CHECK(!fxm_pending() && fxm_st[FXM_DLY] == FXM_ON, "drop: proposing changes nothing");
    }
    mem_drop(NPART + FXM_DLY);                          /* (YES on "DROP DLY 32 KB") */
    CHECK((fxm_off & 2u) && fxm_refused(FXT_DLY) && fxm_pending() && (part_mem_refused.waiting & MEM_W_FX),
          "drop: DLY dropped: gated at once, FULL, the part's change waits for its line");
    fxm_news = 0;
    for (f = 0; f < 50u && part_mem_refused.on; f++) {
        pass();
        blocks(1);
        (void)mem_retry();
    }
    blocks(64);
    pass();
    CHECK(!part_mem_refused.on && t1->engine == ENG_IX_FM6 && !dly_buf && fxm_refused(FXT_DLY) && fxs_slot[2] == FXT_DLY &&
          !fxm_news && arena_ok(), "drop: part 2 on FM6; DLY bypassed as when refused, kept in its slot, nothing said");
    set_engine_of(t1, eng_silent_slot());
    blocks(64);
    settle(2);
    CHECK(dly_buf && fxm_st[FXM_DLY] == FXM_ON && !fxm_refused(FXT_DLY) && arena_ok(),
          "drop: part 2 off FM6 (its block idle): DLY placed again by itself");
    put_chrs(0, 0);
    settle(40);
    /* 7c. a load's list (ui/optimist/op_memlist.c): the parts first, then the effects' lines placed (REV DLY CHO); HOME
     * leaves the placeholder; an effect dropped there (SAVE) bypassed, the refused part placed */
    put_chrs(0, 1);
    settle(4);
    {
        uint8_t it[NPART + 3u];
        uint32_t m, f2;
        int st;
        load_like(3u);                                  /* (FM6 on part 1 as it is, on part 2: the arena full) */
        CHECK(part_mem_refused.lmask == 2u && part_mem_refused.leng[1] == ENG_IX_FM6 && t1->silent &&
              t1->eng_req == eng_silent_slot() && !part_mem_refused.on, "list: a load's FM6 on part 2 refused: recorded, a placeholder");
        for (f2 = 0, st = 0; f2 < 200u && !st; f2++) {
            pass();
            blocks(1);
            st = mem_load_settle();
        }
        m = mem_load_items(it);
        CHECK(st < 0 && m == 5u && it[0] == 1u && it[1] == 0u && it[2] == NPART + FXM_REV && it[3] == NPART + FXM_DLY &&
              it[4] == NPART + FXM_CHO, "list: still short once settled; the items: part 2 (refused), part 1, then REV DLY CHO (not the own room)");
        CHECK(!mem_load_fits(0) && mem_load_fits(1u << 0) && mem_load_fits(1u << (NPART + FXM_DLY)) &&
              mem_item_bytes(NPART + FXM_DLY) == FXM_BYTES[FXM_DLY] && mem_item_bytes(0) >= part_mem_bytes(ENG_IX_FM6) &&
              fxm_st[FXM_DLY] == FXM_ON && arena_ok(),
              "list: nothing dropped does not fit; part 1 or DLY dropped fits (the trial changes nothing)");
        mem_load_cancel();                              /* (HOME) */
        settle(4);
        blocks(64);
        CHECK(!part_mem_refused.lmask && t1->silent && t1->engine == eng_silent_slot() && !strcmp(trk_eng_name(t1), "NONE") &&
              t0->engine == ENG_IX_FM6 && dly_buf && arena_ok(), "list: HOME: part 2 stays a placeholder (NONE), the rest as loaded");
        load_like(3u);
        for (f2 = 0, st = 0; f2 < 200u && !st; f2++) {
            pass();
            blocks(1);
            st = mem_load_settle();
        }
        CHECK(st < 0 && part_mem_refused.lmask == 2u, "list: the same load again: part 2 refused");
        mem_load_commit(1u << (NPART + FXM_DLY));       /* (DLY's key, SAVE) */
        for (f2 = 0, st = 0; f2 < 200u && !st; f2++) {
            pass();
            blocks(1);
            st = mem_load_settle();
        }
        blocks(64);
        settle(4);
        CHECK(st > 0 && !part_mem_refused.lmask && t1->engine == ENG_IX_FM6 && !t1->silent && t0->engine == ENG_IX_FM6 &&
              !dly_buf && fxm_refused(FXT_DLY) && fxs_slot[2] == FXT_DLY && arena_ok(),
              "list: DLY dropped (SAVE): part 2 on FM6, part 1 kept; DLY bypassed, kept in its slot");
        set_engine_of(t1, eng_silent_slot());
        blocks(64);
        settle(4);
        CHECK(dly_buf && !fxm_refused(FXT_DLY) && arena_ok(), "list: part 2 off FM6: DLY back by itself");
    }
    put_chrs(0, 0);
    settle(40);
    /* 8. random switches */
    {
        uint32_t n, okall = 1, worst = 0, nfull = 0, nref = 0, nlent = 0, nown = 0, nmv = 0, ndrop = 0;
        uint32_t nlist = 0, nfit = 0, nlied = 0;   /* (a load's list: shown, FITS committed, FITS that did not) */
        srand(7);
        for (n = 0; n < 20000u && okall; n++) {
            uint32_t r = (uint32_t)rand(), op = r % 9u, a = (r >> 8) % 4u;
            if (op == 0u)
                fxs_load(1u + a % 3u, (r >> 12) & 1u ? FXS_LT[a % 3u] : FXT_NONE);
            else if (op == 1u)
                put_chrs(a % NTRK, (r >> 12) & 1u);
            else if (op == 2u) {
                uint8_t s[2] = {(uint8_t)((r >> 12) & 1u ? FXT_ECHO : FXT_NONE), FXT_NONE};
                ins_put(a % NTRK, s);
            } else if (op == 3u)
                set_engine_of(&trk[a % NPART], (r >> 12) & 1u ? ENG_IX_FM6 : eng_silent_slot());
            else if (op == 4u)
                part_drop(&trk[a % NPART]);
            else if (op == 6u && !((r >> 15) & 3u))
                load_like((r >> 12) & 7u);              /* (a project's load now and then) */
            else if (op == 5u && part_mem_refused.on && !mem_retry()) {
                uint32_t x, got = mem_offer(&x);
                while (got && x < NPART && (r >> 13) & 1u)
                    got = mem_offer(&x);                /* (NO on the parts, sometimes: the effects come next) */
                if ((r >> 12) & 1u && got)
                    mem_drop(x), ndrop += x >= NPART;   /* (YES on what is proposed: a part or an effect) */
                else
                    part_mem_refused.on = 0;            /* (given up, as MEM FULL) */
            }
            if (part_mem_refused.lmask) {               /* a load's refusals (or a switch that found no room, its idle
                                                         * state taken back): the list, as the UI's (op_memlist.c ml_tick) */
                int st = mem_load_settle();
                if (st < 0) {
                    uint8_t it[NPART + 3u];
                    uint32_t m = mem_load_items(it), sel = 0, i;
                    nlist++;
                    for (i = 0; i < m; i++)
                        if ((r >> (17u + i)) & 1u)
                            sel |= 1u << it[i];
                    if (mem_load_fits(sel)) {
                        uint32_t f2;
                        mem_load_commit(sel);           /* (SAVE: then the frames until the drops' switches are done) */
                        nfit++;
                        for (f2 = 0, st = 0; f2 < 400u && !st; f2++) {
                            pass();
                            blocks(1);
                            st = mem_load_settle();
                        }
                        nlied += st <= 0;               /* (FITS said, the commit then short: never) */
                    } else
                        mem_load_cancel();              /* (HOME: placeholders) */
                }
            }
            pass();
            blocks(1u + (r >> 16) % 3u);
            okall = arena_ok();
            worst = n;
            nfull += fxm_full != 0, nref += part_mem_refused.on, nlent += fxl_st[0] != FXL_SEND || fxl_st[1] != FXL_SEND;
            nown += fxa_ins != 0, nmv += fxm_mv != 0;
        }
        CHECK(!nlied && nlist > 20u && nfit > 5u, "... a load's list (a project's load now and then, or a switch that found no room): shown %u times, FITS committed %u times, the "
              "commit always placed what FITS said (%u short)", (unsigned)nlist, (unsigned)nfit, (unsigned)nlied);
        mem_load_cancel();
        CHECK(okall && nfull > 1000u && nref > 100u && nlent > 1000u && nown > 1000u && nmv > 10u && ndrop > 5u,
              "20,000 random switches: the invariants held at every step (to %u; steps with an effect refused %u, a part's "
              "change refused %u, a send's line lent %u, the own room placed %u, silent lines moved %u, effects dropped %u)",
              (unsigned)worst, (unsigned)nfull, (unsigned)nref, (unsigned)nlent, (unsigned)nown, (unsigned)nmv, (unsigned)ndrop);
        part_mem_refused.on = 0;
        for (k = 1; k < FX_NSLOT; k++)
            fxs_load(k, FXT_NONE);
        quiet();
        settle(60);
        CHECK(!cho_buf && !dly_buf && !rev_line && !fxa_ins && fxm_off == 7u && arena_ok(),
              "... everything let go: every effect's line given back, every send gated");
        fxs_set(FXS_DEF);
        settle(4);
        CHECK(cho_buf && dly_buf && rev_line && !fxm_off && arena_ok(), "... the default slots again: CHO DLY REV placed");
    }
    printf("fx arena test: %d checks, %d failed\n", checks, bad);
    return bad ? 1 : 0;
}
