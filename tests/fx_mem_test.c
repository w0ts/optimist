/* SPDX-License-Identifier: GPL-3.0-only */
/* FX MEMORY (2026-11-01, fx/fx_memsz.h): the project's sizes of the big effects' lines. Built with the arena's effects'
 * lines, DLY_LEN 32768, FDN8 in the 32 KB ring (REV_POOL), XDLY (run_tests.sh):
 *   1. DLY MEM FULL .. 1/8: the delay send's block halves each step, the arena's room grows by as much
 *   2. REV MEM SMALL: the reverb's block 16 KB less (the ring 8192, or the ROOM's own lines when longer)
 *   3. XDLY MEM HALF: an XDLY insert's block 32 KB instead of 64, its instance taken again at the new size
 *   4. (the FX record: fx_mem_rec_test.c)
 *   5. a TIME longer than a short line: halved until it fits, said (fxsz_dly_cut); the delay still sounds
 *   6. a heavy project MEM FULL at FULL sizes places everything with smaller ones
 *   fx_mem_test */
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
#if !FXM_NEED || FXZ_DLY_N != 4u || FXZ_REV_N != 2u || FXZ_XDL_N != 2u
#error "fx_mem_test: built with the arena's effects' lines, DLY_LEN 32768, FDN8 + REV_POOL, XDLY (run_tests.sh)"
#endif

static int bad, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { bad++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } else printf("ok   " __VA_ARGS__), printf("\n"); } while (0)

static int32_t peak;
static void blocks(uint32_t n)
{
    static int32_t o[CTL * 2];
    uint32_t i;
    while (n--) {
        events_block(CTL);
        mix_block(o, CTL);
        for (i = 0; i < CTL * 2u; i++)
            peak = o[i] > peak ? o[i] : -o[i] > peak ? -o[i] : peak;
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
static uint32_t blk(uint32_t c) { return arn_ptr(FXM_K(c)) ? arn_cur[FXM_K(c)].len : 0u; }   /* send line c's block */
static uint32_t xdly_bytes(void)                         /* the XDLY instances' blocks */
{
    uint32_t i, b = 0, j = FXI_J(FXT_XDLY);
    for (i = 0; i < FXI_TN[j]; i++)
        if (fxi_own[j][i] != FXI_NONE && fxa_u[j][i] >= FXA_T)
            b += arn_cur[FXM_CP_K((fxa_u[j][i] - FXA_T) % FXI_CPN)].len;
    return b;
}
static void sends(void)                                  /* every send in its master slot, a track sending to each */
{
    fxs_set(FXS_DEF);
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 90;
}

int main(void)
{
    uint32_t room0, r, b, k, rev_full, rev_small, n;
    host_tracks_init();
    fxi_init();
    sends();
    settle(8);
    /* 1. DLY MEM */
    room0 = arn_room();
    b = blk(FXM_DLY);
    CHECK(b == 32768u * 2u && dly_buf, "DLY MEM FULL: the delay send's block is DLY_LEN's, %u B", (unsigned)b);
    for (k = 1; k < FXZ_DLY_N; k++) {
        song.g[G_FXZ_DLY] = (int16_t)k;
        settle(6);
        CHECK(blk(FXM_DLY) == (32768u * 2u >> k) && fxsz_dly == k && dly_buf && fxm_st[FXM_DLY] == FXM_ON &&
                  arn_room() - room0 == 32768u * 2u - (32768u * 2u >> k),
              "DLY MEM %s: its block %u B, the arena's room %u B more", N_FXZD[k], (unsigned)blk(FXM_DLY),
              (unsigned)(arn_room() - room0));
    }
    song.g[G_FXZ_DLY] = 0;
    settle(6);
    CHECK(blk(FXM_DLY) == 32768u * 2u && arn_room() == room0, "DLY MEM FULL again: the block and the room as before");
    /* 2. REV MEM */
    rev_full = blk(FXM_REV);
    song.g[G_FXZ_REV] = 1;
    settle(6);
    rev_small = blk(FXM_REV);
    CHECK(rev_full >= 16384u * 2u + FXM_REV_AP && rev_full < 16384u * 2u + FXM_REV_AP + 1024u &&
              rev_small >= (REV_LINE_OWN > 8192u ? REV_LINE_OWN : 8192u) * 2u + FXM_REV_AP && rev_small + 15000u < rev_full &&
              arn_room() - room0 == rev_full - rev_small,
          "REV MEM SMALL: the reverb's block %u -> %u B (the ring 8192, the ROOM's lines %u), the room %u B more",
          (unsigned)rev_full, (unsigned)rev_small, (unsigned)REV_LINE_OWN, (unsigned)(arn_room() - room0));
    /* the small ring sounds: FDN8 */
    rsel.type = RT_FDN8;
    rv.size = -1;
    peak = 0;
    trk_note_on(&trk[0], 60, 110);
    blocks(200);
    trk_note_off(&trk[0], 60);
    blocks(400);
    CHECK(peak > 0 && rv.m == 8191u && rv.rb[7] + 1249u + 2u * 8u + 2u <= 8192u,
          "... FDN8 in the 8192 ring (mask %u): its lines inside it, it sounds (peak %d)", (unsigned)rv.m, (int)peak);
    song.g[G_FXZ_REV] = 0;
    settle(6);
    rsel.type = RT_FDN8;                                 /* (the line placed again: the TYPE's tank cleared, ROOM's) */
    trk_note_on(&trk[0], 60, 110);
    blocks(20);
    trk_note_off(&trk[0], 60);
    blocks(20);
    CHECK(blk(FXM_REV) == rev_full && arn_room() == room0 && rv.m == 16383u && rv.rb[7] > 8192u,
          "REV MEM FULL again: %u B, FDN8's 32 KB layout", (unsigned)blk(FXM_REV));
    /* 3. XDLY MEM */
    put(2, FXT_XDLY);
    settle(6);
    b = xdly_bytes();
    song.g[G_FXZ_XDL] = 1;
    for (n = 0; n < 200u && (fxsz_xdl != 1u || !xdly_bytes()); n++)
        settle(1);
    settle(4);
    CHECK(b == 2u * 16384u * 2u && xdly_bytes() == 2u * 8192u * 2u && fxi_at[2][FXI_J(FXT_XDLY)] != FXI_NONE && !fxsz_xoff,
          "XDLY MEM HALF: the insert's block %u -> %u B, taken again by its owner (%u passes)", (unsigned)b,
          (unsigned)xdly_bytes(), (unsigned)n);
    /* 5. a too-long TIME */
    for (k = 0; k < NTRK; k++)
        put(k, FXT_NONE);
    song.g[G_FXZ_DLY] = 3;                               /* (4096 samples, 93 ms) */
    song.g[G_BPM] = 60;
    song.g[G_DTIME] = 13;                                /* (a long note value: far past 93 ms) */
    settle(8);
    fxsz_dly_cut = 0;
    peak = 0;
    trk_note_on(&trk[0], 60, 110);
    blocks(20);
    trk_note_off(&trk[0], 60);
    blocks(60);
    {
        uint32_t fb_;
        int32_t a_, d_;
        dlyf_t f_;
        (void)fb_;
        r = dly_block(&a_, &f_, &d_);
        CHECK(fxsz_dly_cut && r >= DLY_CUR && DLY_CUR == 4096u && peak > 0,
              "TIME %u samples on a %u line: halved to fit, said (cut %u), the delay still sounds", (unsigned)r,
              (unsigned)DLY_CUR, (unsigned)fxsz_dly_cut);
    }
    /* 6. a heavy project */
    song.g[G_FXZ_DLY] = song.g[G_FXZ_REV] = song.g[G_FXZ_XDL] = 0;
    settle(8);
    {                                                    /* the arena filled until the full sizes and two XDLY do not fit */
        uint32_t need = 2u * 2u * 16384u * 2u, fill;
        put(1, FXT_XDLY);
        put(2, FXT_XDLY);
        settle(20);
        r = arn_room();
        fill = r > 8192u ? r - 8192u : 0u;              /* (a buffer of the arena's own, as a big engine's state) */
        (void)need;
        fm1_irq_off();
        (void)arn_reserve(0, fill);
        (void)arn_take(0);
        fm1_irq_on();
        for (k = 0; k < NTRK; k++)
            put(k, FXT_NONE);
        settle(80);
        fxm_full = 0;
        put(1, FXT_XDLY);
        put(2, FXT_XDLY);
        settle(20);
        CHECK(fxi_full[FXI_J(FXT_XDLY)] != 0u, "a heavy project at FULL sizes: an XDLY FULL (no room)");
        song.g[G_FXZ_DLY] = 2;
        song.g[G_FXZ_REV] = 1;
        song.g[G_FXZ_XDL] = 1;
        for (n = 0; n < 400u && (fxi_full[FXI_J(FXT_XDLY)] || fxm_full || fxsz_xdl != 1u || fxsz_dly != 2u || fxsz_rev != 1u || !dly_buf || !rev_line); n++)
            settle(1);
        CHECK(!fxi_full[FXI_J(FXT_XDLY)] && !fxm_full && xdly_bytes() == 2u * 2u * 8192u * 2u && dly_buf && rev_line,
              "... DLY 1/4, REV SMALL, XDLY HALF: both XDLY and every send placed (%u passes)", (unsigned)n);
    }
    printf("fx_mem_test: %d checks, %d failed\n", checks, bad);
    return bad != 0;
}
