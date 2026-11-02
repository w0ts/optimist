/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM > TOP (builder item SYS_TOP; the user, 2026-11-01: what takes the CPU and the RAM, largest first): a read-out page
 * (op_gldraw.c gl_info_draw), TOP_VIS lines of it at once, SELECT scrolls them (op_global.c glo_step: past the last or
 * the first line it goes on to the next or the previous page), the figures taken again every TOP_MS:
 *   % OF A BLOCK   AVG MAX C  the columns: the render's times in % of a 32-frame mix block (726 us), as CPU TIMES' (system/
 *                             dual_load.h dual_tm): AVG over the ~8 blocks it last ran in, MAX the most of the last 2 s,
 *                             C the core that ran it in the last block (- not run then)
 *   CPU0, CPU1                each core's whole block (CPU0 its wait for CPU1 in), the headline
 *   then each job             by AVG, largest first: T1..T3 with their engine ("T1 ACID"), DRUMS (the drum track and its
 *                             bus), REV (the reverb's kept block, its type), BT SBC (Bluetooth's frames: a job of the
 *                             render on fm1-base, CPU0's after the block on Optimist's own firmware), PRE (CPU0 alone
 *                             before the fork: the events, the macros, the LFOs' draws), POST (after the join: the merge,
 *                             the buses, the master chain); a job that did not run in the last 2 s has no line
 *   SRAM KB   used/total      the SRAM to the stacks (INFO's RAM line's INFO_SRAM) and what of it is not free (the arena's
 *                             room: arn_room; without an arena, what the link leaves: stk_sram_free)
 *   then each owner, KB       by size, largest first: the arena's blocks now (a part's engine state "T1 ACID", the send
 *                             lines CHO DLY REV, the inserts' room and their copies "INS XDLY", the other buffers, the
 *                             features' blocks "BT AUDIO" ..), the undo history's least, and the build's own RAM (.data,
 *                             .bss, the pool, the RAM code) by module, from its link (TOP_RAM: tools/ramtop.py, the 12
 *                             largest), OTHER last
 * The CPU part needs the render's times (DUAL_TIMES: a dual-core build); without them the page is the RAM part. Read only:
 * no knob does anything. Included by op_project.c */
#define TOP_MS 500u                                     /* the figures taken again this often (the peaks: dual_load.h's 2 s) */
#define TOP_VIS 10u                                     /* the lines shown at once (CPU TIMES' 12 is its most) */
#define TOP_RAM_N 13u                                   /* the build's table: 12 modules and OTHER (tools/ramtop.py N) */
typedef struct {
    char nm[10];                                        /* the module, as shown ("BT AUDIO", "RAM CODE", "OTHER"); "" none */
    uint32_t b;                                         /* its bytes */
} top_ram_t;
/* the table, written by the build between its two links (tools/build.py, tools/fm1base.py: the first link's symbols,
 * tools/ramtop.py) into the generated headers; the first link has none: the same size, empty (the second links the same
 * RAM). TOP_RAM_H: the host tests' own */
#if defined(TOP_RAM_H)
#include TOP_RAM_H
#elif defined(__has_include)
#if __has_include("sys_top_ram.h")
#include "sys_top_ram.h"
#endif
#endif
#ifndef TOP_RAM_DEF
static const top_ram_t TOP_RAM[TOP_RAM_N] = {{"", 0u}};
#endif
#if DUAL_TIMES
#define TOP_NC (2u + DUAL_TM_NJ + 2u)                   /* CPU0, CPU1, the jobs, PRE, POST */
#if defined(DUAL_JOB_REV)
#define TOP_REVJ (DUAL_JOB_REV ? NPART + 1u : 0xFFu)    /* (the reverb's job: dual.c, fm1-base's opt_dual.c) */
#else
#define TOP_REVJ (NPART + 1u)
#endif
#else
#define TOP_NC 0u
#endif
#define TOP_NR 24u                                      /* the RAM's lines at most */
#define TOP_UNDO 0x7Fu                                  /* (a RAM line's id: the undo history's least) */
#define TOP_TAB 0x80u                                   /* (.. TOP_TAB + t: the table's entry t; below: an arena client) */
static struct {
    uint32_t t, seen;                                   /* fm1_ms when taken; when the page was last on screen */
    uint32_t used;                                      /* the SRAM not free, bytes */
    uint8_t off;                                        /* the first line shown */
    uint8_t nc, nr;                                     /* the CPU's lines (CPU0 CPU1 and the jobs), the RAM's */
#if TOP_NC
    uint8_t cq[TOP_NC];                                 /* each CPU line's quantity (dual_load.h DTQ_*) */
    uint8_t ca[TOP_NC], cm[TOP_NC];                     /* .. its AVG, its MAX, % (held at 255) */
    char cc[TOP_NC];                                    /* .. its core */
#endif
    uint8_t rk[TOP_NR];                                 /* each RAM line: an arena client, TOP_UNDO, TOP_TAB + an entry */
    uint32_t rb[TOP_NR];                                /* .. its bytes */
} top;

static void top_num(char *v, uint32_t x, uint32_t w)   /* x right-aligned in w characters, appended to v */
{
    char t[12] = "";
    uint32_t n = str_len(v), k;
    dual_cnum(t, x);
    for (k = str_len(t); k < w; k++)
        v[n++] = ' ';
    v[n] = 0;
    dual_cat(v, t);
}
static void top_trk(uint32_t p, char *b)               /* "T1 ACID": a part and its engine (b: 12 bytes) */
{
    str_cpy(b, "T1 ", 12);
    b[1] = (char)('1' + p % 9u);
    str_app(b, trk_eng_name(&trk[p % NPART]), 12);
}
static void top_rev(char *b)                            /* " FDN8": the reverb's type appended, when there is a choice */
{
#if defined(REV_NLIST) && FELUCCA_FX_REVERB
    if (REV_NLIST > 1u) {
        str_app(b, " ", 12);
        str_app(b, N_RTYPE[rev_sel() % REV_NLIST], 12);
    }
#else
    (void)b;
#endif
}
#if TOP_NC
static void top_job_name(uint32_t q, char *b)           /* a CPU line's name */
{
    uint32_t j = q - DTQ_J0;
    str_cpy(b, q == DTQ_BLK ? "CPU0" : q == DTQ_C1 ? "CPU1" : q == DTQ_PRE ? "PRE" : q == DTQ_POST ? "POST" :
               j == NPART ? "DRUMS" : j == TOP_REVJ ? "REV" : "BT SBC", 12);
    if (q >= DTQ_J0 && j < NPART)
        top_trk(j, b);
    else if (q >= DTQ_J0 && j == TOP_REVJ)
        top_rev(b);
}
static void top_cpu(void)                               /* the CPU's lines: CPU0, CPU1, then the jobs by AVG */
{
    uint32_t i, j, k, q, ran = dual_tm.ran, c1 = dual_tm.c1_mask;
    top.nc = 0;
    for (k = 0; k < TOP_NC; k++) {
        uint32_t a, m;
        q = k == 0u ? DTQ_BLK : k == 1u ? DTQ_C1 : k == 2u ? DTQ_PRE : k == 3u ? DTQ_POST : DTQ_J0 + k - 4u;
        a = dual_tm_pct(dual_tm.avg[q]);
        m = dual_tm_pct_t(dual_tm_peak(q));
        if (k >= 4u && !m)
            continue;                                   /* (a job not run in the last 2 s: no line) */
        j = q - DTQ_J0;
        i = top.nc++;
        top.cq[i] = (uint8_t)q;
        top.ca[i] = (uint8_t)(a < 255u ? a : 255u);
        top.cm[i] = (uint8_t)(m < 255u ? m : 255u);
        top.cc[i] = q == DTQ_C1 ? '1' : q < DTQ_J0 ? '0' : !((ran >> j) & 1u) ? '-' : ((c1 >> j) & 1u) ? '1' : '0';
    }
    for (i = 3; i < top.nc; i++)                        /* (by AVG, then MAX, largest first: CPU0 CPU1 stay on top) */
        for (j = i; j > 2u && (top.ca[j] > top.ca[j - 1u] || (top.ca[j] == top.ca[j - 1u] && top.cm[j] > top.cm[j - 1u])); j--) {
            uint8_t t;
            char ch;
            t = top.cq[j], top.cq[j] = top.cq[j - 1u], top.cq[j - 1u] = t;
            t = top.ca[j], top.ca[j] = top.ca[j - 1u], top.ca[j - 1u] = t;
            t = top.cm[j], top.cm[j] = top.cm[j - 1u], top.cm[j - 1u] = t;
            ch = top.cc[j], top.cc[j] = top.cc[j - 1u], top.cc[j - 1u] = ch;
        }
}
#endif
static void top_add(uint32_t k, uint32_t b)             /* a RAM line, in its place by size (the TOP_NR largest kept) */
{
    uint32_t i = top.nr;
    if (!b)
        return;
    if (i == TOP_NR) {
        if (b <= top.rb[TOP_NR - 1u])
            return;
        i = TOP_NR - 1u;
    } else
        top.nr++;
    for (; i && top.rb[i - 1u] < b; i--)
        top.rb[i] = top.rb[i - 1u], top.rk[i] = top.rk[i - 1u];
    top.rb[i] = b;
    top.rk[i] = (uint8_t)k;
}
#if PART_DROPS
static void top_arb(uint32_t c, char *b)                /* an arena buffer's name (core/arena_bufs.c ARB_*) */
{
#if FXM_NEED
#if FELUCCA_FX_SLICER
    if (c < (uint32_t)ARB_SL0 + NTRK) {                 /* (a track's SLICER recording) */
        str_cpy(b, c - ARB_SL0 < NPART ? "SLICER T1" : "SLICER DR", 12);
        b[8] = c - ARB_SL0 < NPART ? (char)('1' + c - ARB_SL0) : 'R';
        return;
    }
#endif
    str_cpy(b, FELUCCA_ENG_WAVE && c == (uint32_t)ARB_WAVE ? "WAVE TABLE" : FELUCCA_DRUM_X909 && c == (uint32_t)ARB_X909 ? "X0X 909" :
               FELUCCA_DRUM_X808 && c == (uint32_t)ARB_X808 ? "X0X 808" : FELUCCA_PUNCH_SW && c == (uint32_t)ARB_PUNCH ? "PUNCH" :
               FELUCCA_ENG_FM6 && c == (uint32_t)ARB_FM6SIN ? "FM6 SINE" : "BT AUDIO", 12);   /* (the features' blocks: BT AUDIO first) */
#if FMEM_ANY
    {
        uint32_t f;
        for (f = 0; f < FMEM_N; f++)                    /* (a feature's block: its name, core/feat_mem.c) */
            if (FMEM[f].k == NPART + FXM_NC + c)
                str_cpy(b, FMEM[f].name, 12);
    }
#endif
#else
    (void)c, str_cpy(b, "BUFFER", 12);
#endif
}
static void top_arn_name(uint32_t k, char *b)           /* arena client k's name */
{
    if (k < NPART) {
        top_trk(k, b);
        return;
    }
#if FXM_NEED
    k -= NPART;
    if (k < FXM_NC) {                                   /* (fx/fx_arena.c FXM_CHO DLY REV INS) */
        static const char *const FX[FXM_NC] = {"CHO", "DLY", "REV", "INS ROOM"};
        str_cpy(b, FX[k], 12);
        if (k == FXM_REV)
            top_rev(b);
        return;
    }
    k -= FXM_NC;
    if (k < (uint32_t)ARB_NC) {
        top_arb(k, b);
        return;
    }
    k -= (uint32_t)ARB_NC;
#if FELUCCA_SD_STREAM
    if (!k--) {
        str_cpy(b, "SD STREAM", 12);
        return;
    }
#endif
#if defined(FXI_CPN) && FXI_CPN
    if (k < FXI_CPN) {                                  /* an insert's copy: the type of the instance whose line it is */
        static const char *const J[FXI_NL] = {"INS CHO", "INS ECHO", "INS JUNO", "INS FLNG", "INS XDLY", "INS REV", "INS DLY"};
        uint32_t j, i;
        str_cpy(b, "INSERT", 12);
        for (j = 0; j < FXI_NL; j++)
            for (i = 0; i < FXI_IMAX; i++)
                if (fxi_own[j][i] != FXI_NONE && fxa_u[j][i] == FXA_T + k)
                    str_cpy(b, J[j], 12);
        return;
    }
#endif
#endif
    str_cpy(b, "BLOCK", 12);
}
#endif
static void top_ram(void)                               /* the RAM's lines: the arena's blocks, the undo's least, the table */
{
    uint32_t k, o = TOP_RAM_N - 1u;
#if PART_DROPS
    top.used = INFO_SRAM > arn_room() ? INFO_SRAM - arn_room() : 0u;
#else
    top.used = INFO_SRAM > stk_sram_free() ? INFO_SRAM - stk_sram_free() : 0u;
#endif
    top.nr = 0;
#if PART_DROPS
    for (k = 0; k < ARN_PARTS; k++)
        top_add(k, arn_cur[k].len);
#if FELUCCA_UNDO_HISTORY
    top_add(TOP_UNDO, ARN_UNDO_KEEP);
#endif
#endif
    for (k = 0; k < o; k++)
        top_add(TOP_TAB + k, TOP_RAM[k].b);
    if (TOP_RAM[o].b) {                                 /* OTHER last, whatever its size */
        k = top.nr < TOP_NR ? top.nr++ : TOP_NR - 1u;
        top.rk[k] = (uint8_t)(TOP_TAB + o), top.rb[k] = TOP_RAM[o].b;
    }
}
static void top_take(void)
{
    top.t = fm1_ms;
#if TOP_NC
    top_cpu();
#endif
    top_ram();
}
static uint32_t top_all(void)                           /* the page's lines */
{
    return (TOP_NC ? 1u + top.nc : 0u) + 1u + top.nr;
}
static uint32_t top_lines(void)                         /* the lines on screen (op_project.c info_lines: every draw) */
{
    uint32_t n;
    if (fm1_ms - top.seen > 1000u)                      /* (the page opened: from its first line, the figures now) */
        top.off = 0, top.t = fm1_ms - TOP_MS;
    top.seen = fm1_ms;
    if (fm1_ms - top.t >= TOP_MS)
        top_take();
    n = top_all();
    if (top.off + TOP_VIS > n)
        top.off = (uint8_t)(n > TOP_VIS ? n - TOP_VIS : 0u);
    return n < TOP_VIS ? n : TOP_VIS;
}
/* SELECT on TOP (op_global.c glo_step): its lines a detent each; -> the detents left (past the first or the last line) */
static int32_t top_step(int32_t s)
{
    uint32_t n = top_all(), m = n > TOP_VIS ? n - TOP_VIS : 0u;
    for (; s > 0 && top.off < m; s--)
        top.off++;
    for (; s < 0 && top.off; s++)
        top.off--;
    return s;
}
static void top_cell(uint32_t i, cell_t *c)             /* the line i on screen */
{
    static char lb[12];
    uint32_t r = top.off + i;
    char *v = c->val;
    cell_clear(c);
    c->kind = CK_RO;
    c->unit = "";
    c->label = lb;
    v[0] = lb[0] = 0;
#if TOP_NC
    if (r == 0u) {
        c->label = "% OF A BLOCK";
        dual_cat(v, "AVG MAX C");
        return;
    }
    if (r <= top.nc) {
        r--;
        top_job_name(top.cq[r], lb);
        top_num(v, top.ca[r], 3);
        top_num(v, top.cm[r], 4);
        i = str_len(v);
        v[i] = ' ', v[i + 1u] = top.cc[r], v[i + 2u] = 0;
        c->col = r < 2u ? load_col(top.ca[r]) : top.cm[r] > 100u ? C_WARN : 0;
        return;
    }
    r -= 1u + top.nc;
#endif
    if (r == 0u) {                                      /* SRAM KB: used / total */
        c->label = "SRAM KB";
        top_num(v, top.used / 1024u, 1);
        dual_cat(v, "/");
        top_num(v, INFO_SRAM / 1024u, 1);
        return;
    }
    if (--r >= top.nr)
        return;
    if (top.rk[r] >= TOP_TAB)
        str_cpy(lb, TOP_RAM[(top.rk[r] - TOP_TAB) % TOP_RAM_N].nm, 12);
    else if (top.rk[r] == TOP_UNDO)
        str_cpy(lb, "UNDO", 12);
#if PART_DROPS
    else
        top_arn_name(top.rk[r], lb);
#endif
    top_num(v, (top.rb[r] + 1023u) / 1024u, 1);
}
