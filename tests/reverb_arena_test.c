/* SPDX-License-Identifier: GPL-3.0-only */
/* The reverb in the arena while it plays (2026-10-29, the user on the FM-1: "the FDN8 reverb stops working randomly";
 * the release profile has ARENA, FDN8 in the pool and the CPU guard): built as tests/arena_frag_test.c (the jam
 * profile's engines, the limited inserts, the three sends, an arena of FELUCCA_ARENA_BYTES; run_tests.sh), part 1
 * playing a pattern with REV sent and the reverb's send in its master slot all along, while the rest changes at random
 * through the firmware's own paths: the other parts' engines (model.c set_engine_of, mem_retry; refusals given up),
 * the insert slots of tracks 2..4 (any limited type, CHO DLY REV copies too), the chorus' and the delay's master slots in
 * and out, now and then a project's load (its pack moves blocks), main loop passes between the blocks (fxm_poll,
 * arb_poll, fxi_poll: core/arena.c's moves). Checks:
 *   - the reverb's line stays placed (fxm_st FXM_ON): it never loses its claim while its send is in a slot;
 *   - a gate (fxm_off: a move of the line, fxm_mv_ready) lasts at most RA_GATE blocks, and only while the bus is silent
 *     (fx_arena.c fxm_still: nothing of a tail is cut);
 *   - whenever part 1 sounds and the bus is on (the line placed and not gated, the guard off), the return is heard
 *     within 0.5 s.
 *   build/host/reverb_arena_test [STEPS] [SEED] */
#include <time.h>
#include <stdint.h>
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
#if !FXM_NEED || !INS_LIM || !FELUCCA_REV_FDN8
#error "reverb_arena_test: built with FELUCCA_FX_ARENA, FELUCCA_ARENA_BYTES, the limited inserts and FDN8 (run_tests.sh)"
#endif

#define BPS (FS / CTL)
#define RA_GATE 8u                                      /* blocks a move's gate may last (fxm_mv_ready: 2, the copy) */
static int bad;
static uint32_t seed, step;
#define CHECK(c, ...) do { if (!(c)) { if (bad < 20) { printf("reverb_arena: FAIL line %d (step %u, seed %u): ", __LINE__, \
                           step, seed); printf(__VA_ARGS__); printf("\n"); } bad++; } } while (0)
static uint32_t ra_rng;
static uint32_t rnd(void)
{
    ra_rng ^= ra_rng << 13;
    ra_rng ^= ra_rng >> 17;
    ra_rng ^= ra_rng << 5;
    return ra_rng;
}
static void pass(void)
{
    fxm_poll();
    arb_poll();
    fxi_poll();
#if FELUCCA_ENG_WAVE
    wt_service();
#endif
}
static void load_like(const uint8_t *eng)               /* a project's load: its engines (parts 2, 3; part 1 kept) */
{
    uint32_t k;
    fm1_irq_off();
    part_mem_load_begin();
    for (k = 1; k < NPART; k++) {
        uint32_t no = part_mem_load(k, eng[k]) != 0;
        trk[k].silent = (uint8_t)no;
        trk[k].eng_req = (uint8_t)(no ? eng_silent_slot() : eng[k]);
    }
    fm1_irq_on();
}

static uint32_t gate_run, gate_worst, gates, ra_wait, ra_wworst, heard, sounded, stills, line_moves;
static void block_checks(void)
{
    track_t *t0 = &trk[0];
    uint32_t i;
    int on = 0, gated = (fxm_off >> FXM_REV) & 1u, live = fxm_st[FXM_REV] == FXM_ON && !gated;
    for (i = 0; i < NVOICE; i++)
        on |= t0->v[i].active;
    CHECK(fxm_st[FXM_REV] == FXM_ON || (fxm_full >> FXM_REV & 1u), "the reverb's line not placed and not refused (st %u)",
          fxm_st[FXM_REV]);
    CHECK(fxm_st[FXM_REV] != FXM_NONE, "the reverb's line lost its claim while its send is in a slot (full %x)", fxm_full);
    if (gated) {
        gates += !gate_run;
        if (++gate_run > gate_worst)
            gate_worst = gate_run;
    } else {
        gate_run = 0;
    }
    if (on)
        sounded++;
    if (fx_ret_pk[2] > 0)
        heard++;
    if (!live || trk_silent(t0) || !fx_on(t0) || CG_LEVEL >= CG_L_REVERB || rgd.st != RG_ON || rsel.fade || fx_ret_pk[2] > 0) {
        ra_wait = 0;
    } else if (on && ++ra_wait > BPS / 2u) {
        CHECK(0, "part 1 sounds, the bus on, the return silent for %u blocks", ra_wait);
        ra_wait = 0;
    }
    ra_wworst = ra_wait > ra_wworst ? ra_wait : ra_wworst;
}
static void blocks(uint32_t n)
{
    static int32_t o[CTL * 2];
    while (n--) {
        int gated0 = (fxm_off >> FXM_REV) & 1u;
        uint32_t q0 = fx.rev_q;
        int busy0 = REV_LP_BUSY() || REV_HALF_BUSY();
        const int16_t *p0 = rev_line;
        fx_ret_pk[2] = 0;
        events_block(CTL);
        mix_block(o, CTL);
        stills += fxm_still(FXM_REV) != 0u;
        (void)gated0, (void)q0, (void)busy0;
        block_checks();
        line_moves += rev_line != p0 && p0 && rev_line;
    }
}
static void pass_checked(void)                          /* a pass; a gate it sets: the bus silent then */
{
    int g0 = (fxm_off >> FXM_REV) & 1u, rung = fx.rev_q < REV_Q || REV_LP_BUSY() || REV_HALF_BUSY();
    pass();
    if (!g0 && ((fxm_off >> FXM_REV) & 1u) && fxm_st[FXM_REV] == FXM_ON)
        CHECK(!rung, "the reverb's line gated (a move) while its tail rang (q %u)", fx.rev_q);
}

int main(int argc, char **argv)
{
    static const uint8_t PAT[16] = {48, 0, 55, 0, 60, 0, 0, 63, 48, 0, 0, 58, 0, 60, 0, 0};
    uint8_t eng[NENGINES], lim[8];
    uint32_t steps = argc > 1 ? (uint32_t)atoi(argv[1]) : 4000u, ne = 0, nl = 0, e, j, k, moves0;
    seed = argc > 2 ? (uint32_t)atoi(argv[2]) : 1u;
    ra_rng = 2463534242u ^ (seed * 0x9E3779B9u);
    host_tracks_init();
    fxi_init();
    rev_sel_set((uint32_t)rev_index(RT_FDN8));
    for (e = 0; e < NENGINES; e++)
        if (part_mem_bytes(e))
            eng[ne++] = (uint8_t)e;
    eng[ne++] = (uint8_t)eng_silent_slot();
    lim[nl++] = FXT_NONE;
    for (j = 0; j < FXI_NL; j++)
        if (FXI_TN[j])
            lim[nl++] = (uint8_t)FXI_TY(j);
    host_preset(&trk[0], 0, 4);                         /* part 1: ANALOG (keeps nothing in the arena), REV 100 */
    for (j = 0; j < 16u; j++) {
        uint8_t n = PAT[j];
        put_step(&trk[0], j, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, 0);
    }
    trk[0].p[P_REV] = 100;
    transport_req = 1;
    pass();
    blocks(4);
    pass();
    CHECK(fxm_st[FXM_REV] == FXM_ON, "the reverb's line placed at the start (st %u, full %x)", fxm_st[FXM_REV], fxm_full);
    moves0 = arn_probe.moves;
    for (step = 0; step < steps; step++) {
        uint32_t r = rnd(), op = r % 20u, a = 1u + (r >> 8) % 3u;
        if (op < 7u) {                                  /* part 2 or 3's engine */
            track_t *t = &trk[1u + a % 2u];
            uint32_t fr;
            set_engine_of(t, eng[(r >> 12) % ne]);
            for (fr = 0; fr < 40u && part_mem_refused.on; fr++) {
                int rr;
                pass_checked();
                blocks(1);
                rr = mem_retry();
                if (rr < 0 || (rr == 0 && part_mem_refused.on && !part_mem_refused.waiting))
                    break;
            }
            part_mem_refused.on = 0;                    /* (given up: MEM FULL) */
        } else if (op < 13u) {                          /* an insert slot of tracks 2..4 */
            uint8_t s[2];
            uint32_t tk = a < NTRK ? a : 1u;
            s[0] = ins_slot[tk][0], s[1] = ins_slot[tk][1];
            s[(r >> 12) & 1u] = lim[(r >> 13) % nl];
            if (s[0] == s[1] && s[0] != FXT_NONE)
                s[1] = FXT_NONE;
            ins_put(tk, s);
        } else if (op < 17u) {                          /* the chorus' or the delay's master slot */
            uint32_t s = (r >> 12) & 1u;
            fxs_load(1u + s, (r >> 13) % 3u ? FXS_LT[s] : FXT_NONE);
        } else if (op == 17u && !((r >> 12) & 3u)) {    /* a project's load */
            uint8_t le[NPART];
            for (k = 0; k < NPART; k++)
                le[k] = eng[(r >> (14u + 3u * k)) % ne];
            load_like(le);
            mem_load_cancel();
        } else if (op == 18u) {
            song.g[G_RSIZE] = (int16_t)((r >> 12) % 4u ? (r >> 14) % 91u : (r >> 14) % 128u);
        } else if (op == 19u) {                         /* part 1 muted or back: the reverb rings out, its line may move */
            trk[0].p[P_MUTE] ^= 1;
            for (k = 0; trk[0].p[P_MUTE] && k < 6u * BPS / 4u; k++) {   /* (6 s: the tail rings out, still) */
                blocks(4);
                pass_checked();
                if (k % 64u == 0u)                      /* (a part's change meanwhile: a refusal packs) */
                    set_engine_of(&trk[1u + k / 64u % 2u], eng[(r >> (k / 64u % 16u)) % ne]);
                part_mem_refused.on = 0;
            }
        }
        pass_checked();
        blocks(1u + (r >> 26) % 24u);
        pass_checked();
        for (k = 0; k < (r >> 20) % 3u; k++) {
            blocks(1);
            pass_checked();
        }
    }
    CHECK(gate_worst <= RA_GATE, "a gate of the reverb's line lasted %u blocks (%u gates)", gate_worst, gates);
    CHECK(heard > sounded / 4u, "the return heard at all (%u blocks, part 1 sounding in %u)", heard, sounded);
    printf("reverb_arena: %u steps, seed %u: the arena's moves %u, the reverb line's %u, its gates %u (the longest %u "
           "blocks), blocks with its bus still %u, the longest wait for the return %u blocks, refused %s; %s\n", steps, seed,
           arn_probe.moves - moves0, line_moves, gates, gate_worst, stills, ra_wworst,
           fxm_full >> FXM_REV & 1u ? "now" : "not now", bad ? "FAIL" : "ok");
    return bad != 0;
}
