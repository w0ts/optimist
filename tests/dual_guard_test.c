/* SPDX-License-Identifier: GPL-3.0-only */
/* The second core's guards (FELUCCA_DUAL): the request check and the boot canary (hal/fm1_dual_guard.h), the 2ND CORE
 * setting and its self-disable (src/system/dual_mode.h), and the supply boost's register steps (hal/fm1_supply.h,
 * FELUCCA_DUAL_BOOST) against a fake P33. Host only: no core, no register. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/hal/fm1_dual_guard.h"
#include "../firmware/src/system/dual_mode.h"

/* ---- a fake P33 for hal/fm1_supply.h: the analog registers, every write logged */
static uint8_t p33[256];
static struct { uint8_t reg, val; } p33_log[64];
static unsigned p33_n, p33_stuck;                  /* p33_stuck: a register that ignores writes (bounded loop) */
static uint32_t delay_total;
static uint8_t fm1_p33_read(uint32_t a) { return p33[a & 0xFFu]; }
static void fm1_p33_write(uint32_t a, uint8_t d)
{
    if (p33_n < 64u) {
        p33_log[p33_n].reg = (uint8_t)a;
        p33_log[p33_n].val = d;
    }
    p33_n++;
    if (!p33_stuck)
        p33[a & 0xFFu] = d;
}
static void fm1_delay_us(uint32_t us) { delay_total += us; }
#define FM1_SUPPLY_HOST 1
#include "../firmware/hal/fm1_supply.h"

static int fails;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

#define PARTS 0x6u                                 /* dual.c DUAL_PARTS: parts 2 and 3 */

static void test_take(void)
{
    uint32_t req = 41, done = 40, b;
    CHECK(fm1_dual_take(40, 40, 0, 0, PARTS) == FM1_DG_IDLE, "nothing new is idle");
    CHECK(fm1_dual_take(req, done, PARTS, FM1_DUAL_CHECK(req, PARTS), PARTS) == FM1_DG_RUN, "the next job runs");
    CHECK(fm1_dual_take(req, done, 0x2u, FM1_DUAL_CHECK(req, 0x2u), PARTS) == FM1_DG_RUN, "a subset of the parts runs");
    CHECK(fm1_dual_take(req, done, 0u, FM1_DUAL_CHECK(req, 0u), 0u) == FM1_DG_RUN, "DUAL_PARTS 0: an empty job runs");
    CHECK(fm1_dual_take(req + 1, done, PARTS, FM1_DUAL_CHECK(req + 1, PARTS), PARTS) == FM1_DG_BAD,
          "a job past the next is refused, its check right or not");
    CHECK(fm1_dual_take(done - 1, done, PARTS, FM1_DUAL_CHECK(done - 1, PARTS), PARTS) == FM1_DG_BAD, "an old job is refused");
    CHECK(fm1_dual_take(req, done, 0x1u, FM1_DUAL_CHECK(req, 0x1u), PARTS) == FM1_DG_BAD, "a part CPU0 renders is refused");
    CHECK(fm1_dual_take(req, done, PARTS | 0x8u, FM1_DUAL_CHECK(req, PARTS | 0x8u), PARTS) == FM1_DG_BAD,
          "a part past NPART is refused");
    CHECK(fm1_dual_take(req, done, PARTS, FM1_DUAL_CHECK(req - 1, PARTS), PARTS) == FM1_DG_BAD, "the last job's check is refused");
    CHECK(fm1_dual_take(0u, 0xFFFFFFFFu, PARTS, FM1_DUAL_CHECK(0u, PARTS), PARTS) == FM1_DG_RUN, "the counter wraps");
    /* every single bit flip in req, arg or check (what a misreading core sees) is refused or idle, never run */
    for (b = 0; b < 32u; b++) {
        uint32_t m = 1u << b, ck = FM1_DUAL_CHECK(req, PARTS), t;
        t = fm1_dual_take(req ^ m, done, PARTS, ck, PARTS);
        CHECK(t != FM1_DG_RUN, "req bit %u flipped ran", (unsigned)b);
        t = fm1_dual_take(req, done, PARTS ^ m, ck, PARTS);
        CHECK(t == FM1_DG_BAD, "arg bit %u flipped not refused", (unsigned)b);
        t = fm1_dual_take(req, done, PARTS, ck ^ m, PARTS);
        CHECK(t == FM1_DG_BAD, "check bit %u flipped not refused", (unsigned)b);
    }
}

/* CPU1's loop as dual.c fm1_cpu1_wake runs it, against a mailbox read through a misreading bus: no misread job runs,
 * every good one runs once and in order, and each misread is counted */
static uint32_t lcg = 12345u;
static uint32_t rnd(void) { return lcg = lcg * 1664525u + 1013904223u; }
static uint32_t misread(uint32_t v) { return (rnd() >> 24) < 8u ? v ^ (1u << (rnd() >> 27)) : v; }   /* ~3 % of reads */

static void test_misreading_cpu1(void)
{
    uint32_t req = 0, done = 0, mb_done = 0, arg = 0, check = 0, bad = 0, ran = 0, wrong = 0, i;
    for (i = 0; i < 20000u; i++) {
        uint32_t r, a, t;
        if (mb_done == req) {                      /* CPU0 posts the next job: arg, check, then req */
            arg = (rnd() & 1u) ? PARTS : 0x2u;
            check = FM1_DUAL_CHECK(req + 1u, arg);
            req++;
        }
        r = misread(req);                          /* CPU1 wakes and reads */
        if (r == done)
            continue;
        a = misread(arg);
        t = fm1_dual_take(r, done, a, misread(check), PARTS);
        if (t != FM1_DG_RUN) {
            bad++;
            continue;                              /* (dual.c: noted, CPU0 then holds CPU1; here: read again) */
        }
        if (r != req || a != arg)
            wrong++;
        ran++;
        done = r;
        mb_done = r;
    }
    CHECK(wrong == 0, "%u jobs ran on a misread request or argument", (unsigned)wrong);
    CHECK(ran > 10000u && bad > 100u, "the loop ran %u jobs, refused %u", (unsigned)ran, (unsigned)bad);
    printf("  misreading CPU1 model: %u jobs run, %u misreads refused, %u run wrong\n", (unsigned)ran, (unsigned)bad, (unsigned)wrong);
}

static void test_canary(void)
{
    uint32_t t[FM1_DUAL_CANARY_N], i, zeros = 0, ones = 0;
    fm1_dual_canary_fill(t);
    for (i = 0; i < FM1_DUAL_CANARY_N; i++) {
        zeros += t[i] == 0u;
        ones += t[i] == 0xFFFFFFFFu;
    }
    CHECK(zeros >= 4u && ones >= 4u, "the canary has all-zero (%u) and all-one (%u) words", (unsigned)zeros, (unsigned)ones);
    CHECK(fm1_dual_canary_scan(t, FM1_DUAL_CANARY_PASSES) == 0u, "a clean table reads clean");
    t[5] ^= 0x00200000u;                           /* X0X's unit: 0x00200000 where core 0 reads 0 */
    CHECK(fm1_dual_canary_scan(t, FM1_DUAL_CANARY_PASSES) == FM1_DUAL_CANARY_PASSES, "one flipped word, every pass");
    fm1_dual_canary_fill(t);
    t[0] ^= 1u;
    t[31] ^= 0x80000000u;
    CHECK(fm1_dual_canary_scan(t, 1u) == 2u, "two flipped words in one pass");
}

static void test_mode(void)
{
    uint32_t w;
    CHECK(dual_boot_why(DUALM_AUTO, 0) == DUAL_UP, "AUTO starts CPU1");
    CHECK(dual_boot_why(DUALM_OFF, 0) == DUAL_SETOFF, "OFF does not");
    CHECK(dual_boot_why(DUALM_AUTO, 1) == DUAL_EARLY, "a boot after an early crash stays on one core");
    CHECK(dual_boot_why(DUALM_OFF, 2) == DUAL_SETOFF, "OFF wins over the crash (no message)");
    /* the settings word: 0 (a record without the bits) is AUTO; the other bits are kept */
    CHECK(dual_mode_in(0u) == DUALM_AUTO, "no bits: AUTO");
    CHECK(dual_mode_in(3u << 27) == DUALM_AUTO, "an unknown value: AUTO");
    w = dual_mode_out(0x07FFFFFFu, DUALM_OFF);
    CHECK(w == (0x07FFFFFFu | 1u << 27) && dual_mode_in(w) == DUALM_OFF, "OFF round-trips, other bits kept");
    w = dual_mode_out(0xFFFFFFFFu, DUALM_AUTO);
    CHECK(w == 0xE7FFFFFFu && dual_mode_in(w) == DUALM_AUTO, "AUTO clears both bits, keeps 29..31");
    /* the self-disable */
    CHECK(dual_why_parks(DUAL_MISREAD) && dual_why_parks(DUAL_CANARY) && dual_why_parks(DUAL_TIMEOUT) &&
              dual_why_parks(DUAL_FAULT) && dual_why_parks(DUAL_FLASH), "a misread, canary, timeout or fault parks");
    CHECK(!dual_why_parks(DUAL_UP) && !dual_why_parks(DUAL_NO_HELLO) && !dual_why_parks(DUAL_EARLY) &&
              !dual_why_parks(DUAL_SETOFF), "no start, an early crash or the setting do not");
#if DUAL_MODE_MENU
    CHECK(dual_mode_after(DUALM_AUTO, DUALM_AUTO, DUAL_MISREAD) == DUALM_OFF, "AUTO turns itself OFF on a misread");
#else
    CHECK(dual_mode_after(DUALM_AUTO, DUALM_AUTO, DUAL_MISREAD) == DUALM_AUTO,
          "without a menu to set it back the self-disable lasts the session only");
#endif
    CHECK(dual_mode_after(DUALM_AUTO, DUALM_AUTO, DUAL_NO_HELLO) == DUALM_AUTO, "a start that never answered keeps AUTO");
    CHECK(dual_mode_after(DUALM_AUTO, DUALM_OFF, DUAL_MISREAD) == DUALM_AUTO, "set to AUTO during the session: kept");
    CHECK(dual_mode_after(DUALM_OFF, DUALM_OFF, DUAL_SETOFF) == DUALM_OFF, "OFF stays OFF");
}

static void test_supply(void)
{
    unsigned i, vdc_last = 0, first_sys = 64;
    uint32_t was;
    memset(p33, 0, sizeof p33);
    p33[FM1_P3_VDC14] = 0x18u | 3u;                /* VD14_HD_SEL [4:3] set: kept; VDC14 3 (1.40 V, the boot loader's) */
    p33[FM1_P3_SYSVDD] = 0xA0u | 11u;              /* bits above [3:0] kept; SYSVDD 11 (1.26 V) */
    p33_n = 0;
    delay_total = 0;
    was = fm1_supply_boost();
    CHECK(was == (3u << 4 | 11u), "the levels found: %02x", (unsigned)was);
    CHECK(p33[FM1_P3_VDC14] == (0x18u | 5u), "VDC14 at 5 (1.50 V), the other bits kept: %02x", p33[FM1_P3_VDC14]);
    CHECK(p33[FM1_P3_SYSVDD] == (0xA0u | 15u), "SYSVDD at 15 (1.38 V), the other bits kept: %02x", p33[FM1_P3_SYSVDD]);
    CHECK(p33_n == 2u + 4u, "one write a step: 2 for VDC14, 4 for SYSVDD (%u)", p33_n);
    for (i = 0; i < p33_n && i < 64u; i++) {
        uint32_t reg = p33_log[i].reg;
        CHECK(reg == FM1_P3_VDC14 || reg == FM1_P3_SYSVDD, "a write to P33 %02x", (unsigned)reg);
        if (reg == FM1_P3_VDC14) {
            vdc_last = i;
        } else if (first_sys == 64u) {
            first_sys = i;
        }
        if (i)
            CHECK((p33_log[i].val & 15u) == (p33_log[i - 1].reg == reg ? (p33_log[i - 1].val & 15u) + 1u :
                                                                         (reg == FM1_P3_SYSVDD ? 12u : 4u)),
                  "write %u steps by one", i);
    }
    CHECK(vdc_last < first_sys, "VDC14 is raised before SYSVDD");
    CHECK(delay_total >= 6u * 100u + 1000u, "100 us a step and 1 ms to settle (%u us)", (unsigned)delay_total);
    /* never lowered: a rail already above the boost stays */
    p33[FM1_P3_VDC14] = 7u;
    p33[FM1_P3_SYSVDD] = 15u;
    p33_n = 0;
    fm1_supply_boost();
    CHECK(p33_n == 0u && p33[FM1_P3_VDC14] == 7u, "nothing written when already there or above (%u writes)", p33_n);
    /* a register that does not take the write: bounded */
    p33[FM1_P3_VDC14] = 0u;
    p33[FM1_P3_SYSVDD] = 0u;
    p33_n = 0;
    p33_stuck = 1;
    fm1_supply_boost();
    p33_stuck = 0;
    CHECK(p33_n <= 8u + 16u, "a stuck register ends the loop (%u writes)", p33_n);
}

/* SYSTEM > INFO's CPU1 lines (dual_mode.h): the stop record's text on every path, the stages, the short numbers */
static void test_info(void)
{
    char v[14], u[16], b[16];
    dual_stop_t st;
    memset(&st, 0, sizeof st);
    dual_stop_text(&st, v, u);
    CHECK(!strcmp(v, "--") && !u[0], "never down: -- (%s|%s)", v, u);
    /* the hardware report of 2026-10-15: the first job never picked up, before the ms clock ticked */
    st.stopped = st.in_job = st.no_clock = 1;
    st.c1_ms = 41;
    st.job = 0;
    st.wait_us = 5805;
    dual_stop_text(&st, v, u);
    CHECK(!strcmp(v, "41ms* W") && !strcmp(u, " #0"), "first job, never taken (%s|%s)", v, u);
    st.took = 1;
    st.at = 5;
    st.no_clock = 0;
    st.c1_ms = 123456;
    st.job = 4000000u;
    st.wait_us = 120;
    dual_stop_text(&st, v, u);
    CHECK(!strcmp(v, "123s J5") && !strcmp(u, " #4000k 120us"), "in the drums, later (%s|%s)", v, u);
    st.in_job = 0;
    dual_stop_text(&st, v, u);
    CHECK(!strcmp(v, "123s IDLE") && !u[0], "no job posted: a boot check (%s|%s)", v, u);
    st.c1_ms = 4294967295u;
    st.in_job = 1;
    st.took = 1;
    st.at = 6;
    st.job = 4294967295u;
    st.wait_us = 4294967295u;
    dual_stop_text(&st, v, u);
    CHECK(strlen(v) < sizeof v && strlen(u) < sizeof u, "the widest record fits (%s|%s)", v, u);
    b[0] = 0;
    dual_cnum(b, 0);
    dual_cat(b, "/");
    dual_cnum(b, 9999);
    dual_cat(b, "/");
    dual_cnum(b, 10000);
    dual_cat(b, "/");
    dual_cnum(b, 12345678);
    CHECK(!strcmp(b, "0/9999/10k/12M"), "short numbers (%s)", b);
    CHECK(!strcmp(dual_stage_name(C1S_LOOP, 1), "SLEEP") && !strcmp(dual_stage_name(C1S_LOOP, 0), "SPIN") &&
              !strcmp(dual_stage_name(C1S_NONE, 1), "NONE") && !strcmp(dual_stage_name(C1S_TOOK, 0), "TOOK JOB") &&
              !strcmp(dual_stage_name(C1S_DONE, 1), "JOB DONE") && !strcmp(dual_stage_name(99, 1), "?"),
          "the stages' names");
}

int main(void)
{
    test_info();
    test_take();
    test_misreading_cpu1();
    test_canary();
    test_mode();
    test_supply();
    if (fails) {
        printf("dual_guard_test: %d FAILED\n", fails);
        return 1;
    }
    printf("dual_guard_test: ok\n");
    return 0;
}
