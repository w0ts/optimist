/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects memory (2026-10-23, the user: one shared memory decides how many of each line-owning insert can be live):
 * one pool memory in units of FXA_U samples (1 KB) for the delay send's line and every line of the limited insert types,
 * CHRS (1024 samples: 2 units), ECHO (4096: 8), JUNO CHORUS (1024: 2), FLNG (2 x 256: 1) and XDLY (2 x FELUCCA_XDLY_LEN).
 * A type is built or not (registry.h FELUCCA_INS_*: any non-zero is built); how many of it play at once is what the
 * memory holds when the owners pick it (fxi.c places a line when an owner takes the type, frees it when it lets go).
 * The units, by number:
 *   0 .. FXA_S - 1        the delay send's line (fx.c dly_buf): the send's while its return is in a master slot, lent
 *                         to the inserts while not (fxi.c "Lending")
 *   FXA_S .. FXA_N - 1    the inserts' own (fxa_ins)
 *   FXA_N .. FXA_T - 1    the chorus send's line (fx.c cho_buf), lent as the delay's
 * The size: the builder's DLY_ARENA (KB, the delay send's line included); 0 (auto): the send's line and one line of
 * each type built, so every built type can play at least once at the same time. Included by insfx.c (before FLNG's
 * kernel: its table is sized here) with the limited types built (INS_LIM) */
#define FXI_NK (NTRK + 5u)                              /* the owners (fxi_kern.c FXI_O*): T1 T2 T3 DR, the master, the
                                                         * USB input, the returns A B C */
#define FXL_MIN(a, b) ((a) < (b) ? (a) : (b))
#define FXA_U 512u                                      /* samples a unit (1 KB) */
#define FXA_S (FELUCCA_FX_DELAY ? FELUCCA_DLY_LEN / FXA_U : 0u)   /* (preprocessor arithmetic: FELUCCA_DLY_LEN) */
#define FXC_S (FELUCCA_FX_CHORUS ? CHO_LEN / FXA_U : 0u)
/* each type's line in units, by its index j (fxi_kern.c FXI_J: 0 CHRS, 1 ECHO, 2 JUNO CHORUS, 3 FLNG, 4 XDLY) */
#define FXA_Z0 2u
#define FXA_Z1 8u
#define FXA_Z2 (FELUCCA_JUNO_FINE ? 4u : 2u)             /* (JUNO_FINE: its line at 32 bits) */
#define FXA_Z3 1u
#define FXA_Z4 (2u * FELUCCA_XDLY_LEN / FXA_U)
#define FXI_B0 (FELUCCA_INS_CHORUS != 0)
#define FXI_B1 (FELUCCA_INS_DELAY != 0)
#define FXI_B2 (FELUCCA_INS_JCHORUS != 0)
#define FXI_B3 (FELUCCA_INS_FLANGER != 0)
#define FXI_B4 (FELUCCA_INS_XDLY != 0)
#define FXA_AUTO (FXI_B0 * FXA_Z0 + FXI_B1 * FXA_Z1 + FXI_B2 * FXA_Z2 + FXI_B3 * FXA_Z3 + FXI_B4 * FXA_Z4)
#define FXA_I (FELUCCA_DLY_ARENA ? FELUCCA_DLY_ARENA - FXA_S : FXA_AUTO)
#define FXA_N (FXA_S + FXA_I)
#define FXA_T (FXA_N + FXC_S)
/* the most lines of z units the memory holds at once (each region on its own: a line never spans two) and the instance
 * table of a type (built b): that, at most an owner each */
#define FXI_MX(z) (FXA_I / (z) + FXA_S / (z) + FXC_S / (z))
#define FXI_TNX(b, z) ((b) ? FXL_MIN(FXI_NK, FXI_MX(z)) : 0u)
#define FXI_TN0 FXI_TNX(FXI_B0, FXA_Z0)
#define FXI_TN1 FXI_TNX(FXI_B1, FXA_Z1)
#define FXI_TN2 FXI_TNX(FXI_B2, FXA_Z2)
#define FXI_TN3 FXI_TNX(FXI_B3, FXA_Z3)
#define FXI_TN4 FXI_TNX(FXI_B4, FXA_Z4)
_Static_assert(!FELUCCA_DLY_ARENA || FELUCCA_DLY_ARENA >= FXA_S,
               "FELUCCA_DLY_ARENA: KB, at least the delay send's line (0: auto)");
_Static_assert(FXA_T <= 1024u, "the effects memory: 1 MB at most (fxi.c fxa_bm)");
_Static_assert(!FXI_B0 || FXI_MX(FXA_Z0) >= 1u, "FELUCCA_DLY_ARENA: no room for one CHRS line (2 KB)");
_Static_assert(!FXI_B1 || FXI_MX(FXA_Z1) >= 1u, "FELUCCA_DLY_ARENA: no room for one ECHO line (8 KB)");
_Static_assert(!FXI_B2 || FXI_MX(FXA_Z2) >= 1u, "FELUCCA_DLY_ARENA: no room for one JUNO CHORUS line (2 KB, 4 with JUNO_FINE)");
_Static_assert(!FXI_B3 || FXI_MX(FXA_Z3) >= 1u, "FELUCCA_DLY_ARENA: no room for one FLNG line (1 KB)");
_Static_assert(!FXI_B4 || FXI_MX(FXA_Z4) >= 1u, "FELUCCA_DLY_ARENA: no room for one XDLY (2 lines of FELUCCA_XDLY_LEN)");
_Static_assert(FELUCCA_XDLY_LEN % 256 == 0 && CHO_LEN % FXA_U == 0, "the lines: whole units");
#if FXA_I
static int16_t fxa_ins[FXA_I * FXA_U] __attribute__((section(".pool"), aligned(4)));
#endif
/* unit u's samples (fxi.c: an instance's line; 4-byte aligned: the kernels zero a line a word at a time) */
AINL int16_t *fxa_line(uint32_t u)
{
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
