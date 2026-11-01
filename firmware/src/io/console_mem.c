/* SPDX-License-Identifier: GPL-3.0-only */
/* membench (a bench build only: FELUCCA_MEMBENCH, OPTIMIST_MEMBENCH=1 at the build): whether the FM-1's SRAM is one
 * memory at one speed, so that app.ld's fixed split (RAMTEXT, RAM, POOL) could become one packed region
 * (docs/MEMORY-MAP.md section 2: unmeasured until now). Included by console.c.
 * - code: the same ALU loop (xorshift, registers only, no loads) linked three times: RAMTEXT (.ram_hot), main RAM
 *   (.ram_hot2) and XIP; called through a pointer (FAR: RAM and XIP are out of a direct call's reach). The POOL is
 *   not tried: fm1_guard.h's PC windows allow XIP and .ram_text .. .ram_hot2 only (a pool PC would fault).
 * - data: a 2 KiB scratch in each region (its own: nothing live is touched), read with tabbench's stride, read in
 *   order, written in order; the loops' code is XIP (the same code for every region).
 * The best of 8 runs, interrupts on (the audio runs): compare the lines with each other. Units: 0.1 ns a loop step
 * (code: one step = 8 xorshifts) or a 32-bit access. */
#if FELUCCA_MEMBENCH && defined(__PI32V2__)
#define MB_N 512u                                     /* 32-bit words: 2 KiB a region (RAMTEXT has ~3.7 KB free) */
#define MB_RUNS 4096u
static __attribute__((section(".ram_hot.mbd"), used)) int32_t mb_rt[MB_N];
static int32_t mb_ram[MB_N];
static __attribute__((section(".pool"))) int32_t mb_pool[MB_N];

#define MB_X(x) (x ^= x << 13, x ^= x >> 17, x ^= x << 5)
/* (aligned alike: the same loop at the same fetch alignment in every region) */
#define MB_ALU(name, sect)                                                                         \
    static sect __attribute__((noinline, used, aligned(16))) uint32_t name(uint32_t n)            \
    {                                                                                              \
        uint32_t x = 2463534242u ^ n, i;                                                           \
        for (i = 0; i < n; i++) {                                                                  \
            MB_X(x); MB_X(x); MB_X(x); MB_X(x); MB_X(x); MB_X(x); MB_X(x); MB_X(x);                \
        }                                                                                          \
        return x;                                                                                  \
    }
MB_ALU(mb_alu_rt, __attribute__((section(".ram_hot.mbc"))))
MB_ALU(mb_alu_ram, __attribute__((section(".ram_hot2.mbc"))))
MB_ALU(mb_alu_xip, )

typedef uint32_t (*mb_fn)(uint32_t);
static uint32_t mb_sink;                              /* (every result kept: no loop folded away) */

static uint32_t mb_best(uint32_t d, uint32_t best) { return d < best ? d : best; }
static uint32_t mb_tenths(uint32_t ticks) { return ticks * 10000u / (FM1_TICKS_PER_US * MB_RUNS); }   /* 0.1 ns a step */

static uint32_t mb_code(mb_fn f)
{
    uint32_t r, t0, best = 0xFFFFFFFFu;
    for (r = 0; r < 8u; r++) {
        t0 = fm1_ticks();
        mb_sink += f(MB_RUNS);
        best = mb_best(fm1_ticks() - t0, best);
    }
    return mb_tenths(best);
}

/* what: 0 strided read, 1 read in order, 2 write in order */
static uint32_t mb_data(volatile int32_t *b, uint32_t what)
{
    uint32_t r, i, k, t0, best = 0xFFFFFFFFu;
    for (r = 0; r < 8u; r++) {
        int32_t a = 0;
        t0 = fm1_ticks();
        if (what == 0u)
            for (i = 0, k = 0; i < MB_RUNS; i++, k = (k + 37u) & (MB_N - 1u))
                a += b[k];
        else if (what == 1u)
            for (i = 0; i < MB_RUNS; i++)
                a += b[i & (MB_N - 1u)];
        else
            for (i = 0; i < MB_RUNS; i++)
                b[i & (MB_N - 1u)] = (int32_t)i;
        best = mb_best(fm1_ticks() - t0, best);
        mb_sink += (uint32_t)a;
    }
    return mb_tenths(best);
}

static void mb_line(const char *name, const void *at, uint32_t v)
{
    con_puts(name);
    con_hex((uint32_t)(uintptr_t)at, 8);
    con_puts("  ");
    con_dec((int32_t)v);
    con_puts("\r\n");
}

static void mb_data_lines(const char *name, volatile int32_t *b)
{
    con_puts(name);
    con_hex((uint32_t)(uintptr_t)b, 8);
    con_puts("  stride ");
    con_dec((int32_t)mb_data(b, 0u));
    con_puts("  seq ");
    con_dec((int32_t)mb_data(b, 1u));
    con_puts("  write ");
    con_dec((int32_t)mb_data(b, 2u));
    con_puts("\r\n");
}

static void con_membench(void)
{
    con_puts("code (0.1 ns a step of 8 xorshifts)\r\n");
    mb_line("  RAMTEXT ", (const void *)FAR(mb_alu_rt), mb_code(FAR(mb_alu_rt)));
    mb_line("  RAM     ", (const void *)FAR(mb_alu_ram), mb_code(FAR(mb_alu_ram)));
    mb_line("  XIP     ", (const void *)mb_alu_xip, mb_code(mb_alu_xip));
    con_puts("data (0.1 ns a 32-bit access)\r\n");
    mb_data_lines("  RAMTEXT ", mb_rt);
    mb_data_lines("  RAM     ", mb_ram);
    mb_data_lines("  POOL    ", mb_pool);
    con_puts("(sink ");
    con_dec((int32_t)mb_sink);
    con_puts(")\r\n");
}
#endif
