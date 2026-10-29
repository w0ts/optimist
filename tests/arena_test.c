/* SPDX-License-Identifier: GPL-3.0-only */
/* The arena (firmware/src/core/arena.c; docs/SD-MODE-DESIGN.md 6b): the parts' engine memories placed by need.
 *   arena_test           first fit (a smaller arena): fixed cases, then random switches, some left pending while
 *                        other parts reserve, against the invariants (no block or reservation overlaps another part's,
 *                        every block inside the arena, a part's block moves only when it switches, what to drop is
 *                        always enough)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IN_POOL
static void fm1_irq_off(void) {}                        /* (the host: one thread) */
static void fm1_irq_on(void) {}
#define ARN_PARTS 4u
#define ARN_BYTES 4032u                                 /* 4 x 1008: four engines of 1008 B, as engines.c sizes it */
#include "core/arena.c"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t off_of(uint32_t k) { return (uint32_t)((uint8_t *)arn_ptr(k) - arn_mem); }

static void reset(void)
{
    memset(arn_cur, 0, sizeof arn_cur);
    memset(arn_next, 0, sizeof arn_next);
    memset(arn_has_next, 0, sizeof arn_has_next);
}

static int sw(uint32_t k, uint32_t n)                    /* a switch as engines.c does it: reserve, then take / rest */
{
    if (arn_reserve(k, n))
        return -1;
    if (n)
        arn_take(k);
    else
        arn_rest(k);
    return 0;
}

static int hits(arn_blk_t a, arn_blk_t b) { return a.len && b.len && a.off < b.off + b.len && b.off < a.off + a.len; }

static int sound(void)                                   /* the invariants */
{
    uint32_t i, j;
    for (i = 0; i < ARN_PARTS; i++) {
        if ((arn_cur[i].len && (arn_cur[i].off + arn_cur[i].len > ARN_BYTES || arn_cur[i].off % ARN_ALIGN)) ||
            (arn_has_next[i] && arn_next[i].len && arn_next[i].off + arn_next[i].len > ARN_BYTES))
            return 0;
        for (j = 0; j < ARN_PARTS; j++) {
            if (i == j)
                continue;
            if (hits(arn_cur[i], arn_cur[j]) || (arn_has_next[i] && hits(arn_next[i], arn_cur[j])) ||
                (arn_has_next[i] && arn_has_next[j] && hits(arn_next[i], arn_next[j])))
                return 0;
        }
    }
    return 1;
}

static void fixed_cases(void)
{
#if ARN_TOPS                              /* (parts 2 and 3 from the top, best fit; 0 and 1 droppable) */
    reset();
    CHECK(sw(0, 1000) == 0 && sw(2, 1000) == 0 && sw(3, 600) == 0 && sw(1, 1000) == 0 && sound(), "four placed");
    CHECK(off_of(0) == 0 && off_of(1) == 1000 && off_of(2) == 3032 && off_of(3) == 2432,
          "0 and 1 first fit from the bottom, 2 and 3 at the top end of the smallest gap that holds them");
    arn_give(0);
    arn_give(2);
    CHECK(!arn_ptr(0) && !arn_ptr(2) && !arn_has_next[0] && sound(), "given back: no block, no reservation");
    CHECK(sw(2, 400) == 0 && off_of(2) == 2032, "400 B for part 2: the 432 B gap (not the 1000 B ones), at its top");
    CHECK(arn_fits(2, 1000, 0) && arn_fits(0, 1000, 0) && !arn_fits(0, 1100, 0) && arn_cur[0].len == 0 && !arn_has_next[0],
          "arn_fits: 1000 B would fit, 1100 B not; nothing reserved by asking");
    CHECK(!arn_fits(0, 2100, 1u << 1) && arn_fits(0, 2000, 1u << 1), "2000 B with part 1 given back: 0 .. 2000; 2100 B not");
#else
    reset();
    CHECK(sw(0, 1000) == 0 && sw(1, 1000) == 0 && sw(2, 1000) == 0, "three placed");
    arn_give(1);
    CHECK(!arn_ptr(1) && arn_fits(1, 1000, 0) && !arn_fits(1, 1040, 0) && arn_fits(3, 1032, 0) && !arn_fits(3, 1040, 0) &&
          arn_cur[1].len == 0,
          "arn_give: the block gone; arn_fits says what would fit, nothing reserved");
    reset();
    CHECK(sw(0, 1000) == 0 && sw(1, 1000) == 0 && sw(2, 1000) == 0 && sw(3, 1000) == 0, "four of the largest fit");
    CHECK(off_of(0) == 0 && off_of(1) == 1000 && off_of(2) == 2000 && off_of(3) == 3000 && sound(),
          "laid out in order, 8-aligned");
    CHECK(arn_free_bytes() == 32, "32 B left (4032 - 4 x 1000)");
    CHECK(sw(1, 600) == 0 && off_of(1) == 1000 && arn_cur[1].len == 1000, "a smaller engine reuses its own block");
    CHECK(sw(2, 0) == 0 && arn_cur[2].len == 1000 && arn_cur[2].idle && arn_ptr(2) != 0,
          "to an engine without memory: the block stays, idle (switching back finds the state)");
    CHECK(sw(2, 1000) == 0 && off_of(2) == 2000 && !arn_cur[2].idle, "and back: the same block");
    CHECK(sw(2, 0) == 0 && sw(3, 1400) == 0 && off_of(3) == 2000 && arn_cur[2].len == 0 && sound(),
          "a larger one: part 2's idle block taken back without asking, its own block free for it: 2000..3400");
    CHECK(arn_reserve(0, 2600) == -1, "2600 B for part 0: no room (part 1's 1000 B in the way): refused");
    CHECK(!arn_fits(0, 2000, 0) && arn_fits(0, 2000, 1u << 1), "2000 B: with part 1 given back (part 0's own block: 0..2000)");
    CHECK(!arn_fits(0, 2600, 1u << 1) && !arn_fits(0, 2600, 1u << 3) && arn_fits(0, 2600, (1u << 1) | (1u << 3)),
          "2600 B: parts 1 and 3 (neither alone)");
    CHECK(!arn_fits(0, 5000, 0xFu), "larger than the arena: nothing would help");
    CHECK(arn_fits(3, 900, 0), "its own block holds it: fits as it is");
    CHECK(sw(1, 0) == 0 && arn_reserve(0, 2000) == 0 && arn_next[0].off == 0,
          "part 1 dropped: part 0's 2000 B over its own old block and part 1's idle one");
    CHECK(arn_cur[0].len == 1000 && off_of(0) == 0 && sound(), "part 0 still plays from its old block until the switch");
    CHECK(arn_reserve(2, 600) == 0 && arn_next[2].off == 3400 && sound(),
          "another part meanwhile: never over part 0's old block or its reservation");
    arn_take(0);
    arn_take(2);
    CHECK(off_of(0) == 0 && arn_cur[0].len == 2000 && off_of(2) == 3400 && sound(), "after the switches");
#endif
}

#if ARN_MOVES                             /* (arena_pack_test: every part movable, a window a pass, ARN_MV_STEP) */
static void fill(uint32_t k, uint8_t v)
{
    memset(arn_ptr(k), v, arn_cur[k].len);
}
static int holds(uint32_t k, uint8_t v)
{
    uint32_t i;
    const uint8_t *p = arn_ptr(k);
    for (i = 0; p && i < arn_cur[k].len; i++)
        if (p[i] != v)
            return 0;
    return p != 0;
}
static uint32_t run_plan(uint32_t *seen)                 /* arn_poll until done; the passes; *seen: passes a block moved */
{
    uint32_t n = 0;
    *seen = 0;
    while ((arn_busy() || arn_pk.all) && n < 1000u) {
        arn_poll();
        *seen += arn.mk != ARN_NO;
        n++;
    }
    return n;
}
static void pack_cases(void)
{
    uint32_t seen, before;
    reset();
    arn.mk = ARN_NO;
    CHECK(sw(0, 1000) == 0 && sw(1, 1000) == 0 && sw(2, 1000) == 0 && sw(3, 1000) == 0, "four placed");
    fill(0, 0xA0), fill(1, 0xA1), fill(2, 0xA2), fill(3, 0xA3);
    arn_give(0);
    arn_give(2);
    CHECK(arn_reserve(0, 2000) == -1 && arn_room() >= 2000, "2000 B refused, though 2032 B are free (cut in two)");
    CHECK(arn_pack(0, 2000, ARN_BYTES) == 1 && arn_busy(), "arn_pack: a plan (the free room holds it)");
    before = arn_probe.moves;
    arn_poll();
    CHECK(arn_moving(1) && !arn_moving(3) && arn_cur[1].off == 1000 && arn.mo == 0,
          "a window: part 1 moving (the audio skips it), still at its place, its target 0 taken");
    CHECK(arn_reserve(2, 900) == 0 && arn_next[2].off >= 2000 && sound(),
          "a reservation meanwhile: never over the moving block's place nor its target");
    arn_has_next[2] = 0;
    CHECK(run_plan(&seen) > 1u && arn_probe.moves == before + 1u && !arn_moving(1) && !arn_moving(3),
          "the plan done in windows of 256 B: one move (the fewest in address order)");
    CHECK(off_of(1) == 0 && off_of(3) == 3000 && holds(1, 0xA1) && holds(3, 0xA3) && sound(),
          "part 1 moved down to 0, its contents as they were; part 3 not moved");
    CHECK(arn_reserve(0, 2000) == 0 && arn_next[0].off == 1000, "and the 2000 B fit now (1000 .. 3000)");
    arn_take(0);
    /* a fixed place (a send line): a part's block there moved out of it */
    reset();
    arn.mk = ARN_NO;
    CHECK(sw(0, 1000) == 0 && sw(1, 1000) == 0 && sw(2, 1000) == 0, "three placed");
    fill(2, 0xC2);
    arn_give(1);
    CHECK(arn_reserve_at(3, 2000, 1000) == -1, "part 2 at the fixed place 2000: refused");
    CHECK(arn_pack(3, 1000, 2000) == 1 && run_plan(&seen) && off_of(2) == 1000 && holds(2, 0xC2),
          "packed: part 2 moved to the free 1000, its contents kept");
    CHECK(arn_reserve_at(3, 2000, 1000) == 0 && sound(), "the fixed place free: placed");
    arn_take(3);
    /* the room's kind: moved up (ARN_UPS) */
    reset();
    arn.mk = ARN_NO;
    CHECK(sw(0, 1000) == 0 && sw(1, 1000) == 0 && arn_pack(2, 3000, ARN_BYTES) == 0, "nothing to move for 3000 B");
    CHECK(arn_pack(2, 4000, ARN_BYTES) == 0 && !arn_busy(), "4000 B: more than the room: no plan");
    /* a load: every block packed once nothing is pending */
    reset();
    arn.mk = ARN_NO;
    CHECK(sw(0, 1000) == 0 && sw(1, 1000) == 0 && sw(2, 1000) == 0, "three placed");
    fill(2, 0xD2);
    arn_give(0);
    arn_give(1);
    CHECK(arn_reserve(0, 500) == 0 && arn_next[0].off == 0, "a load's reservation, pending");
    arn_pack_all();
    arn_poll();
    CHECK(arn_pk.all && !arn_moving(2), "arn_pack_all waits while a part's switch is pending");
    arn_take(0);
    run_plan(&seen);
    CHECK(off_of(0) == 0 && off_of(2) == 504 && holds(2, 0xD2) && !arn_busy(), "then packed: part 2 down to 504");
}
#endif

int main(void)
{
    uint32_t i, round, moved = 0, refused = 0, dropped = 0;
    fixed_cases();
#if ARN_MOVES
    pack_cases();
#endif
    srand(5);
    reset();
    for (round = 0; round < 300000u && !moved; round++) {
        uint32_t k = (uint32_t)rand() % ARN_PARTS, op = (uint32_t)rand() % 8u;
        uint32_t n = (uint32_t)(rand() % 4) ? 8u + (uint32_t)(rand() % 1000) : 0u;
        uint32_t before[ARN_PARTS];
        for (i = 0; i < ARN_PARTS; i++)
            before[i] = arn_cur[i].len ? arn_cur[i].off + 1u : 0u;
        if (op < 4u) {                                  /* reserve, left pending */
            if (arn_reserve(k, n)) {
                uint32_t j = 0;
                refused++;
                CHECK(!arn_fits(k, n, 0), "round %u: refused, yet 'fits as it is'", round);
                while (j < ARN_PARTS && (j == k || !arn_cur[j].len || !arn_fits(k, n, 1u << j)))
                    j++;                                /* (a part whose block alone would make room: proposed first) */
                if (j < ARN_PARTS) {                    /* the user drops it (no pending switch of theirs) */
                    arn_cur[j].len = 0, arn_has_next[j] = 0;
                    dropped++;
                    CHECK(arn_reserve(k, n) == 0, "round %u: dropping the part arn_fits named did not make room", round);
                }
            }
        } else if (op < 7u) {                           /* the audio switches a part with a reservation */
            if (arn_has_next[k]) {
                if (arn_next[k].len)
                    arn_take(k);
                else
                    arn_rest(k);
            }
        }
        if (!sound()) { CHECK(0, "round %u: a block or a reservation overlaps another part's", round); break; }
        for (i = 0; i < ARN_PARTS; i++)
            if (arn_cur[i].len && before[i] && before[i] != arn_cur[i].off + 1u && !(op >= 4u && op < 7u && i == k))
            {
                moved = 1;
                CHECK(0, "round %u: part %u's block moved without its switch", round, i);
            }
    }
    printf("  %u rounds, %u refused, %u made to fit by dropping\n", round, refused, dropped);
    CHECK(round == 300000u, "random switches, some pending: invariants hold");
    printf("arena test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
