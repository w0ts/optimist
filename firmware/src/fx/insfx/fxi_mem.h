/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects memory (2026-10-23, the user: one shared memory decides how many of each line-owning insert can be live):
 * one pool memory in units of FXA_U samples (1 KB) for the delay send's line and every line of the limited insert types,
 * ECHO (4096: 8), JUNO CHORUS (1024 x 32 bits: 4), FLNG (2 x 256: 1) and XDLY (2 x FELUCCA_XDLY_LEN), and the CHO DLY
 * REV inserts (2026-10-29, fxi_send.c: CHO_LEN 4, DLY_LEN, the ROOM tank's 19).
 * A type is built or not (registry.h FELUCCA_INS_*: any non-zero is built); how many of it play at once is what the
 * memory holds when the owners pick it (fxi.c places a line when an owner takes the type, frees it when it lets go).
 * The units, by number:
 *   0 .. FXA_S - 1        the delay send's line (fx.c dly_buf): the send's while its return is in a master slot, lent
 *                         to the inserts while not (fxi.c "Lending")
 *   FXA_S .. FXA_N - 1    the inserts' own (fxa_ins)
 *   FXA_N .. FXA_T - 1    the chorus send's line (fx.c cho_buf), lent as the delay's
 * The size: FELUCCA_DLY_ARENA (KB, the delay send's line included); 0 (auto; the builder always leaves it 0, DLY_ARENA
 * retired 2026-10-29): the send's line and one line of each type built, so every built type can play at least once at the same time.
 * The CHO DLY REV inserts (built with their effect, in most builds) are not counted in it (one DLY line is the send's
 * size, 128 KB at the longest; a build's RAM stays as it was): with an arena (FXM_NEED) each copy is a block of the arena
 * of its own (fx/fx_arena.c fxm_cp_*: as many as it holds, FXI_CPN at most); without, a copy plays where the memory has
 * room: a line another type leaves free in the own room, or a send's line lent (that send in no master slot). Included by insfx.c (before FLNG's
 * kernel: its table is sized here) with the limited types built (INS_LIM) */
#define FXI_NK (NTRK + 5u)                              /* the owners (fxi_kern.c FXI_O*): T1 T2 T3 DR, the master, the
                                                         * USB input, the returns A B C */
#define FXL_MIN(a, b) ((a) < (b) ? (a) : (b))
#define FXA_U 512u                                      /* samples a unit (1 KB) */
#define FXA_S (FELUCCA_FX_DELAY ? FELUCCA_DLY_LEN / FXA_U : 0u)   /* (preprocessor arithmetic: FELUCCA_DLY_LEN) */
#define FXC_S (FELUCCA_FX_CHORUS ? CHO_LEN / FXA_U : 0u)
/* each type's line in units, by its index j (fxi_kern.c FXI_J: 0 CHO, 1 ECHO, 2 JUNO CHORUS, 3 FLNG, 4 XDLY, 5 REV,
 * 6 DLY) */
#define FXA_Z0 (INS_CHO ? CHO_LEN / FXA_U : 1u)           /* (CHO: the chorus send's line, 2048 samples) */
#define FXA_Z1 8u
#define FXA_Z2 4u                                        /* (JUNO CHORUS: its line at 32 bits) */
#define FXA_Z3 1u
#define FXA_Z4 (2u * FELUCCA_XDLY_LEN / FXA_U)
#if INS_REV                                              /* REV (fxi_send.c): the ROOM's four lines and its diffusers */
#define FXA_Z5 ((REV_TANK_LEN + REV_AP_N + FXA_U - 1u) / FXA_U)
#else
#define FXA_Z5 1u
#endif
#define FXA_Z6 (INS_DLY ? FELUCCA_DLY_LEN / FXA_U : 1u)   /* DLY (fxi_send.c): a line as long as the delay send's */
#define FXI_B0 (INS_CHO != 0)
#define FXI_B1 (FELUCCA_INS_DELAY != 0)
#define FXI_B2 (FELUCCA_INS_JCHORUS != 0)
#define FXI_B3 (FELUCCA_INS_FLANGER != 0)
#define FXI_B4 (FELUCCA_INS_XDLY != 0)
#define FXI_B5 (INS_REV != 0)
#define FXI_B6 (INS_DLY != 0)
#define FXI_CPN FXM_CPN                                  /* (the arena's blocks for the CHO DLY REV copies, engines.c) */
#ifndef FELUCCA_FXI_LINE_BLOCKS
#define FELUCCA_FXI_LINE_BLOCKS 0                        /* (engines.c sets it with an arena) */
#endif
/* with an arena, every line type's copies are blocks of their own (engines.c FELUCCA_FXI_LINE_BLOCKS): no own room */
#define FXI_LB (FXI_CPN && FELUCCA_FXI_LINE_BLOCKS)
#define FXI_IS_CP(j) (FXI_CPN && (FXI_LB || (j) == 0u || (j) == 5u || (j) == 6u))   /* (a type whose copies are arena blocks) */
#define FXA_AUTO (FXI_LB ? 0u : FXI_B1 * FXA_Z1 + FXI_B2 * FXA_Z2 + FXI_B3 * FXA_Z3 + FXI_B4 * FXA_Z4)   /* (CHO DLY REV: none) */
#define FXA_I (FELUCCA_DLY_ARENA ? FELUCCA_DLY_ARENA - FXA_S : FXA_AUTO)
#define FXA_N (FXA_S + FXA_I)
#define FXA_T (FXA_N + FXC_S)
/* the most lines of z units the memory holds at once (each region on its own: a line never spans two) and the instance
 * table of a type (built b): that, at most an owner each */
#define FXI_MX(z) (FXA_I / (z) + FXA_S / (z) + FXC_S / (z))
#define FXI_TNX(b, z) ((b) ? FXL_MIN(FXI_NK, FXI_MX(z)) : 0u)
#define FXI_TNC(b, z) ((b) ? FXL_MIN(FXI_NK, FXI_MX(z) + FXI_CPN) : 0u)   /* (CHO DLY REV: + the arena's) */
#define FXI_TN0 FXI_TNC(FXI_B0, FXA_Z0)
#define FXI_TNL(b, z) (FXI_LB ? FXI_TNC(b, z) : FXI_TNX(b, z))   /* (the limited types: the arena's blocks too) */
#define FXI_TN1 FXI_TNL(FXI_B1, FXA_Z1)
#define FXI_TN2 FXI_TNL(FXI_B2, FXA_Z2)
#define FXI_TN3 FXI_TNL(FXI_B3, FXA_Z3)
#define FXI_TN4 FXI_TNL(FXI_B4, FXA_Z4)
#define FXI_TN5 FXI_TNC(FXI_B5, FXA_Z5)
#define FXI_TN6 FXI_TNC(FXI_B6, FXA_Z6)
_Static_assert(!FELUCCA_DLY_ARENA || FELUCCA_DLY_ARENA >= FXA_S,
               "FELUCCA_DLY_ARENA: KB, at least the delay send's line (0: auto)");
_Static_assert(FXA_T <= 1024u, "the effects memory: 1 MB at most (fxi.c fxa_bm)");
_Static_assert(!FXI_B1 || FXI_LB || FXI_MX(FXA_Z1) >= 1u, "FELUCCA_DLY_ARENA: no room for one ECHO line (8 KB)");
_Static_assert(!FXI_B2 || FXI_LB || FXI_MX(FXA_Z2) >= 1u, "FELUCCA_DLY_ARENA: no room for one JUNO CHORUS line (4 KB)");
_Static_assert(!FXI_B3 || FXI_LB || FXI_MX(FXA_Z3) >= 1u, "FELUCCA_DLY_ARENA: no room for one FLNG line (1 KB)");
_Static_assert(!FXI_B4 || FXI_LB || FXI_MX(FXA_Z4) >= 1u, "FELUCCA_DLY_ARENA: no room for one XDLY (2 lines of FELUCCA_XDLY_LEN)");
/* (CHO DLY REV: no room may be FULL, their table empty, FXI_TN*: a build with nowhere to place one never offers it) */
_Static_assert(FXA_Z5 <= 255u && FXA_Z6 <= 255u, "fxi_kern.c FXA_SZS: a byte");
_Static_assert(FELUCCA_XDLY_LEN % 256 == 0 && CHO_LEN % FXA_U == 0, "the lines: whole units");
#if FXA_I && FXM_NEED
static int16_t *fxa_ins;                                /* (in the arena while a limited type is held: fx/fx_arena.c) */
#define FXR_MOVING() arn_moving(NPART + 3u)             /* (the own room moving, core/arena.c: the kernels let it be) */
#elif FXA_I
static int16_t fxa_ins[FXA_I * FXA_U] __attribute__((section(".pool"), aligned(4)));
#endif
#ifndef FXR_MOVING
#define FXR_MOVING() 0
#endif
#if FXI_CPN
static int16_t *fxm_cp_p[FXI_CPN];                      /* the CHO DLY REV copies' blocks (fx_arena.c), units FXA_T + c */
#endif
#if FXI_LB && FXM_NEED
/* instance i of type j's block is moving (core/arena.c, FXM_CP_MOVES: only while it is at rest): the kernel lets it be */
#define FXI_CP_MOVING(j, i) (fxa_u[j][i] >= FXA_T && arn_moving(FXM_CP_K0 + (fxa_u[j][i] - FXA_T) % FXI_CPN))
#else
#define FXI_CP_MOVING(j, i) 0
#endif
/* unit u's samples (fxi.c: an instance's line; 4-byte aligned: the kernels zero a line a word at a time) */
AINL int16_t *fxa_line(uint32_t u)
{
#if FXI_CPN
    if (u >= FXA_T)
        return fxm_cp_p[(u - FXA_T) % FXI_CPN];
#endif
#if FXC_S
    if (u >= FXA_N)
        return cho_buf + (u - FXA_N) * FXA_U;
#endif
#if FXA_I
    if (u >= FXA_S)
        return fxa_ins + (u - FXA_S) * FXA_U;
#endif
    return dly_buf + u * FXA_U;
}
