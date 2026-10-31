/* SPDX-License-Identifier: GPL-3.0-only */
/* The limited inserts' lines as arena blocks of their own (2026-10-31, ramaudit: engines.c FELUCCA_FXI_LINE_BLOCKS,
 * fxi_mem.h FXI_LB): with an arena, an ECHO, JUNO CHORUS, FLNG or XDLY copy takes a block of its own line's size, not
 * the whole own room (every type's line, 77 KB on the user's build) placed for the first one held. Built with
 * FELUCCA_FX_ARENA, ECHO, FLNG, JUNO CHORUS and XDLY (run_tests.sh):
 *   1. no own room (FXA_I 0); a FLNG on track 1 takes one block of its line's size (1 KB), the arena's room less that
 *   2. an ECHO on track 2: 8 KB more; an XDLY on track 3: its two lines more; every instance's line its block
 *   3. let go: each faded out, its block given back; the arena's room as before
 *   4. a quiet copy's block moves (a load's pack): its instance's line repointed, its contents kept
 *   fx_lineblk_test */
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
#if !FXM_NEED || !FXI_LB || !FELUCCA_INS_FLANGER || !FELUCCA_INS_DELAY || !FELUCCA_INS_XDLY
#error "fx_lineblk_test: built with FELUCCA_FX_ARENA, FELUCCA_ARENA_BYTES, FLNG, ECHO and XDLY (run_tests.sh)"
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
static void pass(void)
{
    fxm_poll();
    fxi_poll();
}
static void settle(uint32_t n)
{
    while (n--) {
        pass();
        blocks(2);
    }
    pass();
}
static void put(uint32_t k, uint32_t ty)
{
    uint8_t s[2] = {(uint8_t)ty, FXT_NONE};
    ins_put(k, s);
}
/* the copies' blocks: how many placed, their bytes; every instance's line is its block's start */
static uint32_t cp_bytes(uint32_t *n)
{
    uint32_t c, b = 0;
    *n = 0;
    for (c = 0; c < FXI_CPN; c++)
        if (fxm_cp_p[c])
            (*n)++, b += arn_cur[FXM_CP_K(c)].len;
    return b;
}
static int lines_ok(void)
{
    uint32_t j, i, c;
    for (c = 0; c < FXI_CPN; c++)
        if (fxm_cp_p[c] && fxm_cp_p[c] != arn_ptr(FXM_CP_K(c)))
            return 0;
    for (j = 0; j < FXI_NL; j++)
        for (i = 0; i < FXI_TN[j]; i++)
            if (fxi_own[j][i] != FXI_NONE && fxa_line(fxa_u[j][i]) != (fxa_u[j][i] >= FXA_T ? fxm_cp_p[(fxa_u[j][i] - FXA_T) % FXI_CPN] : fxa_line(fxa_u[j][i])))
                return 0;
    return 1;
}

int main(void)
{
    uint32_t n, b, room0, k, j;
    host_tracks_init();
    fxi_init();
    {
        uint8_t none[FX_NSLOT];
        memset(none, FXT_NONE, sizeof none);
        fxs_set(none);                                  /* (no send in a master slot: the inserts alone) */
    }
    settle(8);
    room0 = arn_room();
    CHECK(FXA_I == 0u && FXM_BYTES[FXM_INS] == 0u, "no own room (FXA_I %u)", (unsigned)FXA_I);
    put(0, FXT_FLNG);
    settle(4);
    b = cp_bytes(&n);
    k = fxi_at[0][FXI_J(FXT_FLNG)];
    CHECK(n == 1u && b == FXA_SZ(3) * FXA_U * 2u && k != FXI_NONE && fxa_u[3][k] >= FXA_T && lines_ok() &&
          room0 - arn_room() == b, "FLNG on track 1: one block of %u B (its line), the arena's room %u B less",
          (unsigned)b, (unsigned)(room0 - arn_room()));
    put(1, FXT_ECHO);
    settle(4);
    b = cp_bytes(&n);
    CHECK(n == 2u && b == (FXA_SZ(3) + FXA_SZ(1)) * FXA_U * 2u && lines_ok(), "ECHO on track 2: a second block, 8 KB (%u B in all)",
          (unsigned)b);
    put(2, FXT_XDLY);
    settle(4);
    b = cp_bytes(&n);
    CHECK(n == 3u && b == (FXA_SZ(3) + FXA_SZ(1) + FXA_SZ(4)) * FXA_U * 2u && lines_ok() && room0 - arn_room() == b,
          "XDLY on track 3: its two lines a third block (%u B in all, the arena's room that much less)", (unsigned)b);
    /* 4. a quiet copy moves in a load's pack: its line repointed, its contents kept */
    j = FXI_J(FXT_ECHO);
    k = fxi_at[1][j];
    fxa_line(fxa_u[j][k])[5] = 1234;
    arn_pk.load = 1;
    (void)arn_pack_all();
    for (n = 0; n < 64u && arn_busy(); n++)
        arn_poll();
    arn_pk.load = 0;
    CHECK(lines_ok() && fxa_line(fxa_u[j][k])[5] == 1234 && ifx_echo[k].line == fxa_line(fxa_u[j][k]),
          "a load's pack: the blocks moved together, each instance's line its block, its contents kept");
    /* 3. let go */
    for (k = 0; k < NTRK; k++)
        put(k, FXT_NONE);
    settle(60);
    b = cp_bytes(&n);
    CHECK(n == 0u && b == 0u && arn_room() == room0, "let go: every copy faded out and its block given back, the room as before");
#if FELUCCA_FXM_IDLE_FREE
    /* 5. (FELUCCA_FXM_IDLE_FREE) the send lines only while their bus is heard */
    fxs_set(FXS_DEF);
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_CHOR] = trk[k].p[P_DLY] = trk[k].p[P_REV] = 0;
    settle(8);
    CHECK(!cho_buf && !dly_buf && !rev_line && arn_room() == room0,
          "the sends in the master slots, nothing sent: no line placed (the arena's room as with none)");
    trk[0].p[P_DLY] = 100;
    trk_note_on(&trk[0], 60, 110);
    for (n = 0; n < 40u && !dly_buf; n++)
        blocks(1), pass();
    blocks(8);
    pass();
    CHECK(dly_buf && !cho_buf && !rev_line && fxm_st[FXM_DLY] == FXM_ON && !(fxm_off & 2u),
          "a note sent to the delay: its line placed and heard after %u blocks; CHO and REV still none", (unsigned)n);
    trk_note_off(&trk[0], 60);
    trk[0].p[P_DLY] = 0;
    for (n = 0; n < (FXM_IDLE_S + 8u) * FS / CTL && dly_buf; n++) {
        blocks(1);
        if (!(n & 15u))
            pass();
    }
    settle(4);
    CHECK(!dly_buf && fxm_st[FXM_DLY] == FXM_NONE && arn_room() == room0,
          "... its tail over and %u s of nothing: the line given back (%u blocks)", (unsigned)FXM_IDLE_S, (unsigned)n);
#endif
    printf("fx_lineblk_test: %d checks, %d failed\n", checks, bad);
    return bad != 0;
}
