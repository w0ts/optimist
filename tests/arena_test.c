/* SPDX-License-Identifier: GPL-3.0-only */
/* The arena (firmware/src/core/arena.c; docs/SD-MODE-DESIGN.md 6b): the parts' engine memories placed by need.
 *   arena_test           first fit (a smaller arena): fixed cases, then random switches, some left pending while
 *                        other parts reserve, against the invariants (no block or reservation overlaps another part's,
 *                        every block inside the arena, a part's block moves only when it switches, what to drop is
 *                        always enough)
 *   (built with -DARN_SLOT=1008: the fixed placement of the default arena, never refused up to the slot) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IN_POOL
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
#if !ARN_FIXED && ARN_TOPS                              /* (parts 2 and 3 from the top, best fit; 0 and 1 droppable) */
    reset();
    CHECK(sw(0, 1000) == 0 && sw(2, 1000) == 0 && sw(3, 600) == 0 && sw(1, 1000) == 0 && sound(), "four placed");
    CHECK(off_of(0) == 0 && off_of(1) == 1000 && off_of(2) == 3032 && off_of(3) == 2432,
          "0 and 1 first fit from the bottom, 2 and 3 at the top end of the smallest gap that holds them");
    arn_give(0);
    arn_give(2);
    CHECK(!arn_ptr(0) && !arn_ptr(2) && !arn_has_next[0] && sound(), "given back: no block, no reservation");
    CHECK(sw(2, 400) == 0 && off_of(2) == 2032, "400 B for part 2: the 432 B gap (not the 1000 B ones), at its top");
    CHECK(arn_fits(2, 1000) && arn_fits(0, 1000) && !arn_fits(0, 1100) && arn_cur[0].len == 0 && !arn_has_next[0],
          "arn_fits: 1000 B would fit, 1100 B not; nothing reserved by asking");
    CHECK(arn_drop_for(0, 2100) == -1, "2100 B: only with part 2 dropped too, never named (ARN_DROPPABLE): nothing");
    CHECK(arn_drop_for(0, 2000) == (int32_t)(1u << 1), "2000 B: part 1 named (0 .. 2000)");
#elif !ARN_FIXED
    reset();
    CHECK(sw(0, 1000) == 0 && sw(1, 1000) == 0 && sw(2, 1000) == 0, "three placed");
    arn_give(1);
    CHECK(!arn_ptr(1) && arn_fits(1, 1000) && !arn_fits(1, 1040) && arn_fits(3, 1032) && !arn_fits(3, 1040) && arn_cur[1].len == 0,
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
    CHECK(arn_drop_for(0, 2000) == (int32_t)(1u << 1), "2000 B: drop part 1 (with part 0's own block: 0..2000)");
    CHECK(arn_drop_for(0, 2600) == (int32_t)((1u << 1) | (1u << 3)), "2600 B: parts 1 and 3 (neither alone)");
    CHECK(arn_drop_for(0, 5000) == -1, "larger than the arena: nothing would help");
    CHECK(arn_drop_for(3, 900) == 0, "its own block holds it: fits as it is");
    CHECK(sw(1, 0) == 0 && arn_reserve(0, 2000) == 0 && arn_next[0].off == 0,
          "part 1 dropped: part 0's 2000 B over its own old block and part 1's idle one");
    CHECK(arn_cur[0].len == 1000 && off_of(0) == 0 && sound(), "part 0 still plays from its old block until the switch");
    CHECK(arn_reserve(2, 600) == 0 && arn_next[2].off == 3400 && sound(),
          "another part meanwhile: never over part 0's old block or its reservation");
    arn_take(0);
    arn_take(2);
    CHECK(off_of(0) == 0 && arn_cur[0].len == 2000 && off_of(2) == 3400 && sound(), "after the switches");
#else
    uint32_t k;
    reset();
    CHECK(sw(0, 8) == 0 && sw(1, 1008) == 0 && sw(2, 200) == 0 && sw(3, 1008) == 0, "fixed: any sizes up to the slot");
    CHECK(off_of(0) == 0 && off_of(1) == 1008 && off_of(2) == 2016 && off_of(3) == 3024 && sound(),
          "fixed: part k at k x the slot, as the fixed memories were");
    for (k = 0; k < 4u; k++)
        CHECK(sw(k, 1008) == 0 && off_of(k) == k * 1008u && arn_drop_for(k, 1008) == 0, "fixed: never refused (part %u)", k);
    CHECK(arn_reserve(0, 1016) == -1 && arn_drop_for(0, 1016) == -1, "fixed: past the slot: refused, nothing helps");
#endif
}

int main(void)
{
    uint32_t i, round, moved = 0, refused = 0, dropped = 0;
    fixed_cases();
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
                int32_t m = arn_drop_for(k, n);
                refused++;
                CHECK(m != 0, "round %u: refused, yet 'fits as it is'", round);
                if (m > 0) {                            /* the user drops what it names (no pending switch of theirs) */
                    for (i = 0; i < ARN_PARTS; i++)
                        if ((m >> i) & 1)
                            arn_cur[i].len = 0, arn_has_next[i] = 0;
                    dropped++;
                    CHECK(arn_reserve(k, n) == 0, "round %u: dropping what arn_drop_for named did not make room", round);
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
