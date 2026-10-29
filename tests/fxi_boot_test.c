/* SPDX-License-Identifier: GPL-3.0-only */
/* The limited inserts at power-on (the FM-1's crash, 2026-10-29: fxi_sync cleared a REV insert's 19 KB line at address
 * 0). main.c never ran fxi_init, so the device booted with every unit of the effects memory free (fxa_bm zeroed with
 * the pool) while the inserts' own room was in no block of the arena (fxa_ins 0): the first limited insert picked while
 * the arena could not take the own room was placed in it, at fxa_ins + its offset, and zeroed there. Built as
 * fx_arena_test (FELUCCA_FX_ARENA, one FM6 and every line fit; run_tests.sh), with ASan / UBSan where the compiler has
 * them: the write at 0 is a fault, not a pass.
 *   1. the state the device boots with (no fxi_init, the pool zeroed): parts on FM6 until the own room does not fit,
 *      a REV insert on T1, FLNG on T2: no line placed outside a block, each FULL (or in a block of its own), no write
 *   2. the REV insert FULL: T1's reverb send still reaches the bus (the slot bypassed, as an empty slot)
 *   3. fxi_init as main.c now runs it: the own room's units taken while it is not placed (fx_arena_test's invariant)
 *   fxi_boot_test */
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
#if !FXM_NEED || !FELUCCA_INS_FLANGER || !FELUCCA_ENG_FM6 || !FXA_I || !INS_REV
#error "fxi_boot_test: built with FELUCCA_FX_ARENA, FELUCCA_ARENA_BYTES, FM6, FLNG and the reverb (run_tests.sh)"
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
static void settle(uint32_t n)
{
    while (n--) {
        pass();
        blocks(2);
    }
    pass();
}
static int any16(const int16_t *p, uint32_t n)
{
    while (n--)
        if (*p++)
            return 1;
    return 0;
}
/* every instance handed out has its line inside a block: a send line placed, the own room placed, or its own block */
static int lines_placed(void)
{
    uint32_t j, i;
    for (j = 0; j < FXI_NL; j++)
        for (i = 0; i < FXI_TN[j]; i++) {
            uint32_t u = fxa_u[j][i], r;
            if (fxi_own[j][i] == FXI_NONE)
                continue;
            r = fxa_reg(u);
            if (r == FXL_NS && !fxa_ins)
                return 0;
            if (r < FXL_NS && !(r ? dly_buf : cho_buf))
                return 0;
#if FXI_CPN
            if (r == FXL_CP && !fxm_cp_p[(u - FXA_T) % FXI_CPN])
                return 0;
#endif
        }
    return 1;
}
static void slots(uint32_t k, uint32_t a, uint32_t b)
{
    uint8_t s[2] = {(uint8_t)a, (uint8_t)b};
    ins_put(k, s);
}

int main(void)
{
    uint32_t k, st;
    track_t *t0 = &trk[2];
    host_tracks_init();                                 /* (no fxi_init: the device's power-on, main.c felucca_init) */
    CHECK(!fxa_ins && !fxa_busy(FXA_S), "the premise: the own room in no block, its units free (the zeroed pool)");
    for (k = 0; k < NPART; k++)
        set_engine_of(&trk[k], eng_silent_slot());
    blocks(64);
    settle(8);
    set_engine_of(&trk[0], ENG_IX_FM6);
    blocks(64);
    settle(4);
    set_engine_of(&trk[1], ENG_IX_FM6);                /* (two FM6: the own room no longer fits) */
    blocks(64);
    settle(4);
    part_mem_refused.on = 0;
    CHECK(!arn_fits(FXM_K(FXM_INS), FXM_INS_BYTES, 0), "the premise: two FM6 and the send lines leave no room for the own room");
    for (k = 0; k < NPART; k++)
        trk[k].p[P_REV] = trk[k].p[P_TFLNG] = 100;
    slots(2, FXT_REV, FXT_NONE);                        /* (the crash: the REV insert's line zeroed at fxa_ins 0) */
    slots(1, FXT_FLNG, FXT_NONE);                       /* (a smaller line, from the own room's top: fxa_ins + 1 KB ...) */
    settle(4);
    st = fxi_state(2, 0);
    CHECK(!fxa_ins && lines_placed(), "1. REV on T3, FLNG on T2 with the own room not placed: no line outside a block");
    CHECK(st == FXI_FULL || (st == FXI_OK && ifx_lim_at(FXT_REV, 2) != FXI_NONE),
          "... T3's REV insert FULL, or in a block of its own (state %u)", (unsigned)st);
    CHECK(fxi_state(1, 0) == FXI_FULL || fxi_state(1, 0) == FXI_OK, "... T2's FLNG FULL or placed, never waiting forever");
    if (st == FXI_FULL) {
        memset(rev_line, 0, REV_LINE_N * sizeof *rev_line);
        trk_note_on(t0, 60, 110);
        blocks(200);
        trk_note_off(t0, 60);
        CHECK(rev_line && any16(rev_line, REV_LINE_N), "2. T3's REV insert FULL: its reverb send reaches the bus");
    }
    blocks(400);
    for (k = 0; k < NTRK; k++)
        slots(k, FXT_NONE, FXT_NONE);
    settle(8);
    fxi_init();                                         /* (as main.c felucca_init now runs it) */
    settle(4);
    CHECK(fxa_ins || fxa_busy(FXA_S), "3. fxi_init: the own room's units taken while it is in no block");
    slots(0, FXT_REV, FXT_NONE);
    settle(4);
    CHECK(lines_placed(), "... REV picked again: its line in a block or FULL");
    printf("%s: %d checks, %d failed\n", bad ? "FAIL" : "PASS", checks, bad);
    return bad != 0;
}
