/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 second core (CPU1) bring-up and the shared mailbox. EXPERIMENTAL: validated
 * in the emulator only, never on a device (docs/DUAL-CORE.md).
 *
 * Start sequence: the one the stock M-VAVE application uses (its code at XIP
 * 0x02059a2e, disassembled with the vendor objdump):
 *   [0x01C7FFF8] = CPU1 entry      (the SPL RAM handoff vector; = vector slot 126, SOFT6)
 *   [0x10008]   |= 8               (kept until CPU1 answered, then restored; meaning unknown)
 *   C1_CON      |= 8, &= ~2        (run, out of reset)
 *   wait for CPU1 to write an acknowledgement
 * Stop (AC79 SDK cpu/wl82/debug.c, halting the other core): C1_CON &= ~8, |= 2.
 *
 * The entry (fm1_dual.S) sets CPU1's stacks and returns (rti) into C code, as
 * the stock entry does. CPU1 runs with its interrupts off, except one: with FELUCCA_DUAL_IDLE it sleeps
 * (idle) between jobs and CPU0 wakes it with soft interrupt 124 (SOFT4, the SDK's CPU1 IPI), whose
 * handler runs the job (fm1_dual_wake_*, below; dual.c).
 *
 * Flash rule: while CPU0 programs or erases the flash (XIP off), CPU1 must not
 * fetch from XIP. CPU1 runs XIP code only between a job's request and its done
 * flag, and both happen inside CPU0's audio interrupt; the rest of the time it
 * spins in fm1_dual_idle_ram (below, .ram_text, no calls, no constants). */
#pragma once
#include <stdint.h>
#include "fm1_flash.h"            /* RAMFN, FL_FAR, fl_csync */
#include "fm1_time.h"
#include "fm1_irq.h"               /* FM1_DBG_*, fm1_fatal_stubs */
#include "fm1_dual_guard.h"       /* the request check and the boot canary (misread RAM on some FM-1s) */

#define FM1_C1_CON   (*(volatile uint32_t *)0x1EEE004u)   /* corex2 C1_CON: b1 reset/halt, b3 run */
#define FM1_SYSCLK1  (*(volatile uint32_t *)0x10008u)     /* stock: |= 8 around the CPU1 start */
#define FM1_C1_VEC   (*(volatile uint32_t *)0x01C7FFF8u)  /* CPU1 handoff vector */

static inline __attribute__((always_inline)) uint32_t fm1_cnum(void)                     /* this core's number: 0 or 1 */
{
    uint32_t id;
    __asm__ volatile("%0 = cnum" : "=r"(id));
    return id;
}
static inline __attribute__((always_inline)) void fm1_dual_sync(void) { __asm__ volatile("csync" ::: "memory"); }

/* shared state; every field is written by one core only (noted), read by the other. Each core's fields on cache
 * lines of their own (64 B, Jangada's and X0X's layout, zednaked/jangada, fm1-x0x hal/fm1_cpu1.h, GPL-3.0) */
typedef struct {
    volatile uint32_t req;        /* CPU0: job sequence number (a new value = a new job) */
    volatile uint32_t arg;        /* CPU0: the job's argument (which parts) */
    volatile uint32_t ping;       /* CPU0: pong follows it while CPU1 waits (liveness) */
    volatile uint32_t check;      /* CPU0: FM1_DUAL_CHECK(req, arg), written before req (fm1_dual_guard.h) */
    uint32_t pad0[12];
    volatile uint32_t done;       /* CPU1: the last job finished (= req: idle) */
    volatile uint32_t hello;      /* CPU1: FM1_DUAL_HELLO once it runs C code */
    volatile uint32_t count;      /* CPU1: its beat: spin loops (every FM1_DUAL_BEAT_EVERY), or returns from `idle` */
    volatile uint32_t pong;       /* CPU1 */
    volatile uint32_t fault;      /* CPU1: its exception vector + 1 after a fault (CPU0 writes it once CPU1 is held) */
    volatile uint32_t bad;        /* CPU1: requests it would not run (a misread): CPU0 holds it at the first */
    volatile uint32_t bad_req, bad_done;   /* CPU1: the last one, as read, and its own done then */
    volatile uint32_t canary;     /* CPU1: 1 + the canary words it read wrong at boot (0: not run) */
    volatile uint32_t took;       /* CPU1 asleep between jobs: the last request its wake picked up (CPU0 wakes it again
                                   * while a posted one is not taken: dual.c dual_join); the spin loop sets it too */
    volatile uint32_t stage;      /* CPU1: how far it got, the furthest (dual_mode.h C1S_*: SYSTEM > INFO CPU1 HELLO) */
    volatile uint32_t wakes;      /* CPU1 asleep: its wake handler's entries */
    volatile uint32_t icfg;       /* CPU1: its icfg as it went to sleep (read back after sti), 0: never */
    uint32_t pad1[3];
} fm1_dual_mb_t;
#define FM1_DUAL_HELLO 0x43505531u                        /* "CPU1" */
/* (2026-10-15, B10a: in a 256-byte block of its own in .pool, away from the BLE baseband RAM it had been next to: the
 * fault stayed, so it is back in .bss) */
static fm1_dual_mb_t fm1_dual_mb __attribute__((aligned(64)));
static volatile uint32_t fm1_dual_canary[FM1_DUAL_CANARY_N];   /* CPU0 fills it before the start, CPU1 reads it */

/* CPU1: a request it will not run (fm1_dual_take FM1_DG_BAD): noted, never run (the spin loop counts each read of
 * it until CPU0 holds CPU1: a count, not a number of misreads) */
static inline __attribute__((always_inline)) void fm1_dual_note_bad(uint32_t r, uint32_t done)
{
    fm1_dual_mb.bad_req = r;
    fm1_dual_mb.bad_done = done;
    fl_csync();
    fm1_dual_mb.bad++;
}

/* RAM read-error counters (the second-core test image, FELUCCA_DUALCFG; 2026-10-15: a bit-22 flip in TIMER5's vector as
 * dispatched, Jangada's / X0X's bit-21 misreads on CPU1): each core reads back a pattern only it owns, a few words at a
 * time (CPU0 each main-loop pass, CPU1 every FM1_DUAL_BEAT_EVERY spin passes), and counts what differs (the first
 * address, the value read, the one written: their XOR is the flipped bits); a word read wrong is written again */
#if FELUCCA_DUALCFG
#define FM1_FLIP_N 512u
static volatile uint32_t fm1_flip_pat[2][FM1_FLIP_N];
typedef struct { volatile uint32_t n, checks, addr, got, want; } fm1_flip_t;
static fm1_flip_t fm1_flip[2];                            /* [core]: written by that core only */
static uint32_t fm1_vec_flips, fm1_vec_got;               /* TIMER5's vector read back wrong by CPU0's main loop (main.c) */
static volatile uint8_t fm1_ins_bypass;                   /* dualcfg ins off: the parts', the drum lanes' and the drum bus's
                                                           * inserts not run (fx.c, drums.c; a test of the hang) */
static inline uint32_t fm1_vec_word(uint32_t n) { return FM1_VEC[n & 127u]; }   /* (main.c: a vector word read back) */
static inline __attribute__((always_inline)) uint32_t fm1_flip_want(uint32_t c, uint32_t i)
{
    return (i * 0x9E3779B1u) ^ (c ? 0xA5A5A5A5u : 0x5A5A5A5Au);
}
static void fm1_flip_fill(void)
{
    uint32_t c, i;
    for (c = 0; c < 2u; c++)
        for (i = 0; i < FM1_FLIP_N; i++)
            fm1_flip_pat[c][i] = fm1_flip_want(c, i);
}
static inline __attribute__((always_inline)) uint32_t fm1_flip_check(uint32_t c, uint32_t i, uint32_t k)
{
    fm1_flip_t *f = &fm1_flip[c];
    for (; k; k--, i = (i + 1u) & (FM1_FLIP_N - 1u)) {
        uint32_t v = fm1_flip_pat[c][i], w = fm1_flip_want(c, i);
        if (v != w) {
            if (!f->n) {
                f->addr = (uint32_t)(uintptr_t)&fm1_flip_pat[c][i];
                f->got = v;
                f->want = w;
            }
            f->n++;
            fm1_flip_pat[c][i] = w;
        }
        f->checks++;
    }
    return i;
}
#endif

/* CPU1 between jobs, from RAM: publish `done`, then wait for a request it may run (fm1_dual_take; a bad one is noted
 * and waited past, in RAM). Gentle on the shared RAM (2026-10-15: the first spinning build on the FM-1 was followed by
 * a boot AFTER CRASH, cause not known): one read of req a pass, then FM1_DUAL_SPIN_NOPS no-ops (no bus access, about
 * 0.1 us at 314 MHz); the beat and the ping answered every FM1_DUAL_BEAT_EVERY passes, no store in the other passes.
 * COUNT != 0: count the beat. Returns the new request; *ARG its argument, read once */
#ifndef FM1_DUAL_SPIN_NOPS
#define FM1_DUAL_SPIN_NOPS 32u                            /* (a test build sets more: OPTIMIST_DUAL_SPIN_NOPS) */
#endif
static volatile uint32_t fm1_dual_poll = FM1_DUAL_SPIN_NOPS;   /* (the console's dualcfg poll: set before the start) */
#define FM1_DUAL_BEAT_EVERY 256u
#ifndef FM1_DUAL_NO_SPIN                                  /* (CPU1 asleep between jobs: no spin loop in RAM) */
static inline __attribute__((always_inline)) uint32_t fm1_dual_idle_body(uint32_t done, uint32_t count, uint32_t allowed,
                                                                         uint32_t *arg);
RAMFN static uint32_t fm1_dual_idle_ram(uint32_t done, uint32_t count, uint32_t allowed, uint32_t *arg)
{
    return fm1_dual_idle_body(done, count, allowed, arg);
}
#if FELUCCA_DUALCFG
/* B23 test image, `dualcfg spin` (2026-10-16: CPU1 merely spinning, no job, crashed CPU0; asleep it did not):
 * FM1_SPIN_RAM the loop above; FM1_SPIN_XIP the same loop fetched from the flash (.text, through the shared cache);
 * FM1_SPIN_NOPS no-ops only in RAM, no data access at all (no beat, no ping answer, no job ever taken: jobs 0x80) */
enum { FM1_SPIN_RAM, FM1_SPIN_XIP, FM1_SPIN_NOPS };
static volatile uint8_t fm1_dual_spinv;                    /* (set before the start, as fm1_dual_poll) */
static __attribute__((noinline, used)) uint32_t fm1_dual_idle_xip(uint32_t done, uint32_t count, uint32_t allowed, uint32_t *arg)
{
    return fm1_dual_idle_body(done, count, allowed, arg);
}
RAMFN static void fm1_dual_nops_ram(void)
{
    for (;;)
        __asm__ volatile("nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop");
}
#endif
static inline __attribute__((always_inline)) uint32_t fm1_dual_idle_body(uint32_t done, uint32_t count, uint32_t allowed,
                                                                         uint32_t *arg)
{
    fm1_dual_mb_t *mb = &fm1_dual_mb;
    uint32_t r, a = 0, n = 0, k;
    fl_csync();                                           /* the job's stores before `done` */
    mb->done = done;
    fl_csync();
    for (;;) {
        r = mb->req;
        if (r != done) {
            fl_csync();                                   /* the request's data after `req` */
            a = mb->arg;
            if (fm1_dual_take(r, done, a, mb->check, allowed) == FM1_DG_RUN)
                break;
            fm1_dual_note_bad(r, done);
        }
        for (k = fm1_dual_poll; k; k--)
            __asm__ volatile("nop");
        if (++n == FM1_DUAL_BEAT_EVERY) {
            n = 0;
#if FELUCCA_DUALCFG
            {
                static uint32_t fi;                       /* (CPU1's place in its pattern) */
                fi = fm1_flip_check(1u, fi, 16u);
            }
#endif
            if (mb->ping != mb->pong)
                mb->pong = mb->ping;
            if (count)
                mb->count++;
        }
    }
    *arg = a;
    return r;
}
#endif

/* CPU1 after a fault (fm1_irq.h fm1_fault_c): report and stay in RAM, off the flash */
RAMFN static void fm1_dual_dead_ram(uint32_t vec)
{
    fm1_dual_mb.fault = vec + 1u;
    fl_csync();
    for (;;)
        ;
}

/* CPU1's stacks and its entry: like the stock entry, set the stack pointers, put the C entry into reti, rti.
 * After the rti CPU1's C code runs on usp (the emulator's reading, and X0X's on the FM-1: "after rti the core runs on
 * this one: the render's"); an interrupt (the wake, asleep builds) on the sp value the rti left in ssp. So a spinning
 * CPU1 renders its jobs on usp, an asleep one in the wake interrupt on ssp / sp: the busy one gets 5 KiB, the other 1 KiB
 * of the 6 KiB (two entries, fm1_dual_start picks one), the deepest job (ACID, 2,948 B static) with 2 KiB to spare.
 * 2026-10-15 (B .. B4 on the FM-1): usp had only the lowest 2 KiB (+2048, sp +6144): the spinning builds rendered
 * their jobs there, an ACID part 2.9 KB deep, through the bottom of the stack into the top of part_mem (the parts'
 * engine state, just below in .pool), and CPU0, rendering from that state, crashed in its first blocks (B3: a bus
 * error on garbage read from its stack; B4: a return to address 2). The emulator's projects stayed above it (1.3 KB).
 * tools/build.py cpu1_stack checks every DUAL build against CPU1_STACK_ROOM; fm1_dual_start fills both with
 * FM1_C1_MARK (the console's cpu1_stack_*: their depth) */
#ifndef FELUCCA_DUAL_PARK                                /* (dual_mode.h: the same default) */
#define FELUCCA_DUAL_PARK 0
#endif
#if FELUCCA_DUAL_PARK
#define FM1_C1_SMALL 2u                                  /* words: CPU1 is parked (never started, dual_mode.h): the symbol stays */
#define FM1_C1_STACK_WORDS 4u
#else
#define FM1_C1_SMALL 256u                                /* words: the idle stack's 1 KiB, at the bottom */
#define FM1_C1_STACK_WORDS 1536u                         /* 6 KiB (the pool has no more room in the mots build) */
#endif
#define FM1_C1_MARK 0x43314D4Bu                          /* "C1MK" (X0X's) */
uint32_t fm1_c1_stack[FM1_C1_STACK_WORDS] __attribute__((section(".pool"), aligned(8), used));
void fm1_cpu1_main(void);                                 /* the C entry (src/dual.c), never returns */
/* (cli, icfg = 0 first: CPU1's interrupts off whatever the ROM's hand-over left, as X0X's and Melodee's entries on
 * the FM-1, fm1-x0x hal/fm1_cpu1.S, keremimo/melodee crt0.S, GPL-3.0) */
__asm__(".section .text.fm1_cpu1,\"ax\",@progbits\n"
        "\t.globl fm1_cpu1_entry\n"
        "fm1_cpu1_entry:\n"
        "\tcli\n"
        "\tr0 = 0\n"
        "\ticfg = r0\n"
        "\tusp = fm1_c1_stack + 6144\n"
        "\tssp = fm1_c1_stack + 1024\n"
        "\tsp = fm1_c1_stack + 1024\n"
        "\treti = fm1_cpu1_main\n"
        "\trti\n"
        "\t.globl fm1_cpu1_entry_sleep\n"
        "fm1_cpu1_entry_sleep:\n"              /* (asleep: the jobs in the wake interrupt, on ssp / sp) */
        "\tcli\n"
        "\tr0 = 0\n"
        "\ticfg = r0\n"
        "\tusp = fm1_c1_stack + 1024\n"
        "\tssp = fm1_c1_stack + 6144\n"
        "\tsp = fm1_c1_stack + 6144\n"
        "\treti = fm1_cpu1_main\n"
        "\trti\n"
        "\t.previous\n");
extern void fm1_cpu1_entry(void);
extern void fm1_cpu1_entry_sleep(void);

/* CPU1, first thing in its C entry: its own EMU bank (CPU0's is at 0x1EEF0D0) as X0X and Melodee set it on the FM-1
 * (fm1-x0x hal/fm1_cpu1.h fm1_c1_main, keremimo/melodee hal/fm1_multicore.h, GPL-3.0): a stack limit over its 6 KiB
 * less the lowest 256 bytes, the divide-by-zero trap (bit 2) and the float exception traps (16..20) off, the stack
 * limit (bit 3) on. Never set before 2026-10-15: CPU1 ran with whatever the chip left there (CPU0's own div0 trap had
 * to be turned off, fm1_irq.h) */
static void fm1_dual_c1_emu(void)
{
    uint32_t lo = (uint32_t)(uintptr_t)fm1_c1_stack + 256u, hi = (uint32_t)(uintptr_t)(fm1_c1_stack + FM1_C1_STACK_WORDS) - 1u;
    *(volatile uint32_t *)0x1EEF2D8u = hi;
    *(volatile uint32_t *)0x1EEF2DCu = lo;
    *(volatile uint32_t *)0x1EEF2E0u = hi;
    *(volatile uint32_t *)0x1EEF2E4u = lo;
    *(volatile uint32_t *)0x1EEF2D0u = (*(volatile uint32_t *)0x1EEF2D0u & ~((1u << 2) | (0x1Fu << 16))) | (1u << 3);
    *(volatile uint32_t *)0x1EEF3C0u |= 1u;               /* its branch trace on, from CPU1 too (ETM_CON) */
}

/* CPU1's interrupt bank (CPU0's at 0x1EEF100), all 32 words off: before every start, in every build (X0X, Melodee) */
static void fm1_dual_bank_clear(void)
{
    uint32_t i;
    for (i = 0; i < 32u; i++)
        *(volatile uint32_t *)(0x1EEF300u + 4u * i) = 0;
}

static void fm1_dual_halt(void);
static volatile uint8_t fm1_dual_on;                      /* CPU1 started and answered, not held since (fm1_dual_start,
                                                           * fm1_dual_halt): C1_CON's bits do not read back as written
                                                           * on the FM-1 (2026-10-15, B3: the test on them missed) */

/* An exception about CPU1 (keremimo/melodee hal/fm1_irq.h fm1_core1_fault_c, GPL-3.0, measured on the FM-1): CPU1
 * enables no interrupt of its own, so the debug unit's errors about it (a fetch outside the PC limits, a bus error,
 * DBG_MSG's CPU1 bits) raise the exception on CPU0, whose state is intact: the crash report used to reset the unit
 * for them (the boot after it: 2ND CORE OFF AFTER CRASH). Now CPU1 is held, the fault recorded (fm1_dual_exc; SYSTEM >
 * INFO CPU1 FAULT), fm1_dual_mb.fault set (dual.c: CPU1 goes down as FAULT) and CPU0 resumes (fm1_vec.S). CPU1 taking
 * an exception itself records it and parks in RAM; the echo CPU0 may then get with DBG_MSG already cleared resumes
 * once per CPU1 fault. 1: handled, CPU0 resumes; 0: CPU0's own, the crash report */
#define FM1_DBG_C1 ((1u << 6) | (1u << 8) | (1u << 10) | (1u << 11) | (7u << 16))
#define FM1_DBG_C0 ((1u << 7) | (1u << 9) | (1u << 12) | (1u << 13) | (7u << 19))
static struct {
    volatile uint32_t count;
    uint32_t echoes, cpu, dbg, emu, pc, rets, vec;
    uint32_t c1con;                                       /* C1_CON as read then (what it reads back: not known) */
    uint32_t c1etm[4];                                    /* CPU1's branch trace then (q32DSP(1)->ETM_PC0..3) */
} fm1_dual_exc;

/* CPU1's state at an exception, read by CPU0 (the AC79 SDK's cpu/wl82/debug.c does the same): CMD_PAUSE stops it,
 * then q32DSP(1)'s read-only mirror of its registers (asm/csfr.h: DR00..15 at +0x00, RETI +0x40, RETS +0x4C, PSR
 * +0x54, ICFG +0x6C, USP / SSP / SP +0x70..78, PCRS (where it runs) +0x7C, EMU_MSG +0xD4), and corex2's
 * AXI_RD_INV_ID / AXI_WR_INV_ID (+0x370 / +0x374: which bus master made an "axi_rd_inv" / "axi_wr_inv", DBG_MSG bits
 * 4 / 5, the SDK's names). Kept across the reset (the console's `crash`, `status`) */
#define FM1_C1_SNAP_MAGIC 0x43315350u                     /* "C1SP" */
typedef struct {
    uint32_t magic, dbg, axi_rd_id, axi_wr_id, pcrs, reti, rets, psr, icfg, usp, ssp, sp, emu, r[16];
    uint32_t ble[5];              /* the BLE breadcrumb then (hal/fm1_ble_rf.h fm1_ble_bc: now, irqs, op, gop, addr) */
    uint32_t c1_wr_num;           /* C1_WR_LIMIT_ERR_NUM then: the write-limit window a CPU1 write hit (bit n: window n) */
    uint32_t ticks;               /* TIMER4 then */
    uint32_t boot;                /* felucca_dbg.boots then: which boot the snapshot is from (console: snap_boot) */
} fm1_c1_snap_t;
fm1_c1_snap_t fm1_c1_snap __attribute__((section(".noinit")));   /* (not static: see fm1_crash) */
static uint32_t fm1_dual_bootno;                          /* main.c: felucca_dbg.boots of this boot (the snapshot's stamp) */
static void fm1_dual_snap_ble(fm1_c1_snap_t *s);          /* (hal/fm1_ble_rf.h in a BLE build, else system/main.c) */
static void fm1_dual_snap(uint32_t dbg)
{
    volatile uint32_t *q = (volatile uint32_t *)0x1EEF200u;   /* q32DSP(1) */
    uint32_t i;
    fm1_c1_snap.dbg = dbg;
    fm1_c1_snap.axi_rd_id = *(volatile uint32_t *)0x1EEE370u;
    fm1_c1_snap.axi_wr_id = *(volatile uint32_t *)0x1EEE374u;
    fm1_c1_snap.c1_wr_num = *(volatile uint32_t *)0x1EEE364u;
    if (fm1_dual_on) {
        q[0x26] = 0xFFFFFFFFu;                            /* CMD_PAUSE (debug.c putpchex) */
        __asm__ volatile("csync" ::: "memory");
    }
    fm1_c1_snap.pcrs = q[0x1F];
    fm1_c1_snap.reti = q[0x10];
    fm1_c1_snap.rets = q[0x13];
    fm1_c1_snap.psr = q[0x15];
    fm1_c1_snap.icfg = q[0x1B];
    fm1_c1_snap.usp = q[0x1C];
    fm1_c1_snap.ssp = q[0x1D];
    fm1_c1_snap.sp = q[0x1E];
    fm1_c1_snap.emu = q[0x35];
    for (i = 0; i < 16u; i++)
        fm1_c1_snap.r[i] = q[i];
    fm1_c1_snap.ticks = fm1_ticks();
    fm1_c1_snap.boot = fm1_dual_bootno;
    fm1_dual_snap_ble(&fm1_c1_snap);
    fm1_c1_snap.magic = FM1_C1_SNAP_MAGIC;
}
static int fm1_dual_exc_c(const uint32_t *f)
{
    uint32_t cpu = fm1_cnum(), dbg = FM1_DBG_MSG, was, vec = (f[19] - (uint32_t)(uintptr_t)fm1_fatal_stubs) / 6u - 1u;
    if (!cpu && (dbg & FM1_DBG_C0))
        return 0;                                         /* CPU0's own */
    if (!cpu && !(dbg & FM1_DBG_C1)) {
        /* 2026-10-15 (B, B2 on the FM-1): every power-on boot took vector 1 on CPU0 in its first audio blocks, the
         * first ones CPU1 rendered, at plain stack loads and stores (not a fault of CPU0's own instruction), EMU_MSG 0,
         * and this function did not take it: DBG_MSG had neither core's bits (the bus-invalid bits 4, 5 belong to
         * neither mask) or was already cleared. While CPU1 runs, such an exception (no CPU0 bits, EMU_MSG 0) holds
         * CPU1 first; the same again with CPU1 held is CPU0's and gets the crash report */
        uint32_t c1_runs = fm1_dual_on;
        if (fm1_dual_exc.echoes < fm1_dual_exc.count) {
            fm1_dual_exc.echoes++;
            return 1;                                     /* CPU1's, echoed */
        }
        if (!c1_runs || vec != 1u || FM1_EMU_MSG != 0u)
            return 0;                                     /* CPU0's own */
    }
    if (!cpu && fm1_dual_exc.count >= 16u)
        return 0;                                         /* a hold that did not take */
    if (!cpu)
        fm1_dual_snap(dbg);                               /* (CPU1 paused, its registers read, before the hold) */
    {
        uint32_t i;
        fm1_dual_exc.c1con = FM1_C1_CON;
        for (i = 0; i < 4u; i++)
            fm1_dual_exc.c1etm[i] = *(volatile uint32_t *)(0x1EEF3C4u + 4u * i);   /* (before the hold) */
    }
    if (!cpu)
        fm1_dual_halt();
    fm1_dual_exc.cpu = cpu;
    fm1_dual_exc.dbg = dbg;
    fm1_dual_exc.emu = *(volatile uint32_t *)(cpu ? 0x1EEF2D4u : 0x1EEF0D4u);   /* the faulting core's EMU_MSG */
    fm1_dual_exc.pc = f[16];
    fm1_dual_exc.rets = f[25];
    fm1_dual_exc.vec = vec;
    fm1_dual_exc.count++;                                 /* (before DBG_MSG clears: the echo above) */
    was = FM1_DBG_WR_EN & 1u;                             /* (writing 0xE7 toggles the unlock) */
    if (!was)
        FM1_DBG_WR_EN = 0xE7u;
    FM1_DBG_MSG_CLR = dbg;
    if (!was)
        FM1_DBG_WR_EN = 0xE7u;
    fl_csync();
    if (cpu)
        FL_FAR(fm1_dual_dead_ram)(vec);                   /* CPU1: parks in RAM, never returns */
    fm1_dual_mb.fault = vec + 1u;                         /* (CPU1 held: CPU0 writes it) */
    return 1;
}

/* start CPU1 and wait up to TIMEOUT_US for its hello; 1 = running. On a timeout CPU1 is put back
 * into reset (and stays there). Call with the vectors still writable (before fm1_guard_lock_top). */
static __attribute__((noinline)) int fm1_dual_start(uint32_t timeout_us, int sleeps)
{
    uint32_t sys = FM1_SYSCLK1, t0;
    int ok;
    fm1_dual_mb.hello = 0;
    fm1_dual_mb.fault = 0;
    fm1_dual_mb.bad = 0;
    fm1_dual_mb.canary = 0;
    fm1_dual_mb.stage = 0;
    fm1_dual_mb.count = 0;
    fm1_dual_mb.wakes = 0;
    {
        uint32_t i;
        for (i = 0; i < FM1_C1_STACK_WORDS; i++)
            fm1_c1_stack[i] = FM1_C1_MARK;                /* (how deep each of CPU1's stacks went: the console) */
    }
    fm1_dual_canary_fill(fm1_dual_canary);                /* (CPU1 reads it back before its hello) */
    fm1_dual_sync();
    *(volatile uint32_t *)0x1EEF3C0u |= 1u;               /* CPU1's branch trace on (its ETM_CON, as CPU0's: fm1_irq.h) */
    FM1_C1_VEC = (uint32_t)(uintptr_t)(sleeps ? fm1_cpu1_entry_sleep : fm1_cpu1_entry);
    FM1_SYSCLK1 = sys | 8u;
    FM1_C1_CON |= 8u;
    FM1_C1_CON &= ~2u;
    t0 = fm1_ticks();
    while (fm1_dual_mb.hello != FM1_DUAL_HELLO && fm1_ticks() - t0 < timeout_us * FM1_TICKS_PER_US)
        ;
    ok = fm1_dual_mb.hello == FM1_DUAL_HELLO;
    FM1_SYSCLK1 = sys;
    if (!ok) {
        FM1_C1_CON &= ~8u;
        FM1_C1_CON |= 2u;
    }
    fm1_dual_on = (uint8_t)ok;
    return ok;
}

/* ---- CPU1 asleep between jobs (FELUCCA_DUAL_IDLE). Each core has its interrupt registers at 0x1EEF100 + 0x200 x
 * its number (the AC79 SDK's q32DSP(n), asm/csfr.h: ICFG00..31, IPND0..7, ILAT_SET, ILAT_CLR, IPMASK); the soft
 * interrupts' latch is set through core 0's ILAT_SET (the SDK's CPU_TASK_SW, asm/cpu.h) and the SDK registers SOFT4
 * (124) on CPU1 at priority 6 or 7 (apps/demo .. app_main.c irq_info_table). Not verified on a device: none of the
 * firmwares that run CPU1 on the FM-1 (stock, X0X, Melodee, Jangada) wakes it by interrupt, they all poll in RAM.
 * 2026-10-15 (A, the FM-1): CPU1 reached `idle` and never left it, at the old priority 1 with IPMASK untouched.
 * CPU0 never enables 124 in its bank. */
#define FM1_IRQ_C1_WAKE 124u                                 /* SOFT4 */
#define FM1_DUAL_WAKE_PRIO 6u                                /* (the SDK's, with IPMASK; 7 without) */
#define FM1_ICFG_C1(n) (*(volatile uint32_t *)(0x1EEF300u + 4u * ((n) >> 3)))
#define FM1_IPMASK_C1 (*(volatile uint32_t *)0x1EEF3A8u)     /* CPU1's priority mask (q32DSP(1)->IPMASK) */
#define FM1_ILAT_SET_ (*(volatile uint32_t *)0x1EEF1A0u)     /* (= fm1_irq.h FM1_ILAT_SET / _CLR) */
#define FM1_ILAT_CLR_ (*(volatile uint32_t *)0x1EEF1A4u)
#define FM1_ILAT_SET_C1 (*(volatile uint32_t *)0x1EEF3A0u)   /* (q32DSP(1)'s: a rewake sets it too, in case CPU1's
                                                              * latch is its own on the chip) */
#define FM1_ILAT_CLR_C1 (*(volatile uint32_t *)0x1EEF3A4u)
#define FM1_VEC_ ((volatile uint32_t *)0x01C7FE00u)
/* CPU0, before CPU1 starts (vectors writable): CPU1's bank all off but the wake, at the SDK's priority, its priority
 * mask 0 (whatever the ROM left) */
static void fm1_dual_wake_setup(void (*isr)(void))
{
    fm1_dual_bank_clear();
    FM1_IPMASK_C1 = 0;
    FM1_ILAT_CLR_ = 1u << (FM1_IRQ_C1_WAKE - 120u);
    FM1_ILAT_CLR_C1 = 1u << (FM1_IRQ_C1_WAKE - 120u);
    FM1_VEC_[FM1_IRQ_C1_WAKE] = (uint32_t)(uintptr_t)isr;
    FM1_ICFG_C1(FM1_IRQ_C1_WAKE) = ((FM1_DUAL_WAKE_PRIO << 1) | 1u) << ((FM1_IRQ_C1_WAKE & 7u) * 4u);
}
/* CPU1's pending word 3 (q32DSP(1)->IPND3, bit 28: 124, the wake; on CPU0's bank the FM-1 shows soft interrupt k at
 * bit 24 + k when it is enabled there, fm1-emulator SMP.md): read by CPU0 when CPU1 goes down (INFO P1 / P0) */
static inline __attribute__((always_inline)) uint32_t fm1_dual_wake_pending(void)
{
    return (*(volatile uint32_t *)(0x1EEF380u + 4u * (FM1_IRQ_C1_WAKE >> 5)) >> (FM1_IRQ_C1_WAKE & 31u)) & 1u;
}
/* CPU0: a job is posted (after req) */
static inline __attribute__((always_inline)) void fm1_dual_wake(void)
{
    FM1_ILAT_SET_ = 1u << (FM1_IRQ_C1_WAKE - 120u);
}
/* CPU0: a posted job CPU1 has not picked up: the wake again, latch cleared first (a fresh edge). Whether `idle`
 * wakes on a wake already latched when it executes is not known on the FM-1 [HW?]: if not, a wake latched while
 * CPU1 returns from its handler (the next block's job posted as it leaves) would be lost and the join time out.
 * A wake that finds nothing (CPU1 busy, or the job done meanwhile) is harmless: the handler sees req == done */
static inline __attribute__((always_inline)) void fm1_dual_rewake(void)
{
    FM1_ILAT_CLR_ = 1u << (FM1_IRQ_C1_WAKE - 120u);
    __asm__ volatile("csync" ::: "memory");
    FM1_ILAT_SET_ = 1u << (FM1_IRQ_C1_WAKE - 120u);
    FM1_ILAT_SET_C1 = 1u << (FM1_IRQ_C1_WAKE - 120u);
}
/* CPU1, in the handler, before it looks at req: a later wake stays latched (no lost wake) */
static inline __attribute__((always_inline)) void fm1_dual_wake_ack(void)
{
    FM1_ILAT_CLR_ = 1u << (FM1_IRQ_C1_WAKE - 120u);
    FM1_ILAT_CLR_C1 = 1u << (FM1_IRQ_C1_WAKE - 120u);
    __asm__ volatile("csync" ::: "memory");
}
/* CPU1: its interrupts on (icfg bit 8, sti), then asleep in RAM for good: every job runs in the
 * handler, which returns to the idle. Each return from `idle` counts its beat (fm1_dual_mb.count: an `idle` that
 * returns without the handler having run shows as beats with no wakes).
 * A core woken from `idle` runs 4 more issue slots before the interrupt enters (FM-1_996, measured
 * on CPU0 with TIMER3; the emulator models it). A bare `idle; goto idle` reaches the next `idle`
 * inside that window and goes back to sleep without ever taking the interrupt: CPU1 never ran its
 * first job, CPU0 waited DUAL_JOIN_US for it (the first audio half late, 6.5 ms at boot) and put
 * CPU1 into reset for good. Four csyncs after the `idle` cover the window (tools/build.py checks
 * that no `idle` is reached again within 4 instructions of one). */
RAMFN static void fm1_dual_sleep_ram(void)
{
    uint32_t v;
    fm1_dual_mb.stage = 3u;                               /* (dual_mode.h C1S_LOOP: in its wait loop) */
    fl_csync();
    __asm__ volatile("%0 = icfg" : "=r"(v));
    __asm__ volatile("icfg = %0" ::"r"(v | 0x100u) : "memory");   /* (icfg 0 from the entry: 0x100) */
    __asm__ volatile("csync\n\tsti" ::: "memory");
    __asm__ volatile("%0 = icfg" : "=r"(v));
    fm1_dual_mb.icfg = v | 0x80000000u;                   /* (INFO CPU1 REGS: what it slept with; bit 31: read) */
    for (;;) {
        __asm__ volatile("idle\n\tcsync\n\tcsync\n\tcsync\n\tcsync" ::: "memory");
        fm1_dual_mb.count++;
    }
}
#if defined(DUAL_FAILTEST) && DUAL_FAILTEST == 7
/* (the emulator's test of a wake that never reaches CPU1: in its loop, in RAM, its interrupt never on) */
RAMFN static void fm1_dual_deaf_ram(void)
{
    fm1_dual_mb.stage = 3u;
    for (;;)
        fl_csync();
}
#endif
/* the handler's entry, in RAM with the code it calls (core.h HOT: .ram_hot): as fm1_isr.S */
void fm1_cpu1_wake(void);                                 /* the C body (src/dual.c) */
__asm__(".section .ram_hot.isr_c1_wake,\"ax\",@progbits\n"
        "\t.globl isr_c1_wake\n"
        "isr_c1_wake:\n"
        "\t[--sp] = {psr, rets, reti}\n"
        "\t[--sp] = {r3-r0}\n"
        "\tcall fm1_cpu1_wake\n"
        "\t{r3-r0} = [sp++]\n"
        "\t{psr, rets, reti} = [sp++]\n"
        "\tcsync\n"
        "\trti\n"
        "\t.previous\n");
extern void isr_c1_wake(void);

/* hold CPU1 in reset (a timeout, a fault): it stops wherever it is */
/* corex2's per-core counters (AC79 SDK cpu/wl82/debug.c cpu_effic_init / _calc, under #if 0 there): C0_IF_UACNTL ..
 * C1_TL_CKCNTL, the low words, I = 0..7 (c0 if, rd, wr, tl, c1 if, rd, wr, tl); what IF / RD / WR count is not documented
 * (the SDK prints them as % of TL, the clocks). fm1_pcnt_enable: DBG_CON |= 0x707 as cpu_effic_init ("if_inv_en of_inv_en
 * ex_inv_en", meaning not known: off unless `dualcfg pcnt on`) */
static inline uint32_t fm1_pcnt(uint32_t i) { return *(volatile uint32_t *)(0x1EEE200u + 8u * (i & 7u)); }
static inline void fm1_pcnt_enable(void) { *(volatile uint32_t *)0x1EEE344u |= 0x707u; }
static inline uint32_t fm1_dual_c1con(void) { return FM1_C1_CON; }   /* (the console: what it reads back) */
/* CPU1 paused (1: the pause request, bit 2) or resumed (0: bit 3), as the stock firmware's flash exclusion (the emulator's
 * model, fm1-emulator SMP.md): the run bit kept, reset (bit 1) never touched */
static inline void fm1_dual_pause(int on)
{
    uint32_t c = FM1_C1_CON;
    FM1_C1_CON = on ? ((c & ~0x1Cu) | 4u) : ((c & ~0x14u) | 8u);
    __asm__ volatile("csync" ::: "memory");
}
/* bytes of CPU1's stack REGION (0: the small one at the bottom, 1 KiB; 1: the big one above it, 5 KiB) ever used (the
 * marks overwritten); 0: never started */
static uint32_t fm1_dual_stack_used(uint32_t region)
{
    const uint32_t *b = fm1_c1_stack + (region ? FM1_C1_SMALL : 0u);
    uint32_t n = region ? FM1_C1_STACK_WORDS - FM1_C1_SMALL : FM1_C1_SMALL, i = 0;
    while (i < n && b[i] == FM1_C1_MARK)
        i++;
    return (n - i) * 4u;
}
static void fm1_dual_halt(void)
{
    fm1_dual_on = 0;
    FM1_C1_CON &= ~8u;
    FM1_C1_CON |= 2u;
    fm1_dual_sync();
}
