/* SPDX-License-Identifier: GPL-3.0-only */
/* FX MEMORY (2026-11-01, the user: "on-device settings for memory for the big fishes: delay, verb etc."; ruling: per FX,
 * per project): the size of the arena blocks the big effects' lines take, chosen per project on PROJECT > FX MEM
 * (ui/optimist/op_fxmem.c), kept in the project's FX record (fx_rec.c FXZ_TLV; none: the build's sizes, every project of
 * before). The build's items stay the most each can take (DLY_LEN, REV_POOL, XDLY_LEN: docs/BUILDER.md); a smaller one
 * frees the difference for the parts and the other lines (core/arena.c):
 *   DLY   the delay send's line: FULL (DLY_LEN), 1/2, 1/4, 1/8 (never under 4096 samples, 93 ms); a TIME longer than
 *         the line plays at half, a quarter .. of it, still on the beat (fx.c delay_samples), and says so once
 *   REV   the reverb's ring: FULL or SMALL (FDN8's 16 KB layout of a REV_POOL=0 build: its eight lines 29..57 ms instead of
 *         62..117 ms; PLATE and ROOM are the same in both); only a build whose ring is the 32 KB one (REV_POOL with
 *         FDN8, no AIRWIN, whose own lengths follow the ring at build time)
 *   XDLY  each XDLY insert's two lines: FULL (FELUCCA_XDLY_LEN 16384) or HALF (8192: every TIME half, as an XDLY_LEN
 *         8192 build)
 * Not offered (fixed, shown with their KB): the chorus send's line (4 KB: DEPTH reaches 1,163 samples back, a 1,024 line
 * would cut the deep settings), ECHO (93 ms: its 75 ms repeat), JUNO CHORUS (the BBD's own line), FLNG (1 KB).
 * Only with the effects' lines in the arena (FXM_NEED): without, the lines are static and nothing would be freed.
 * A change applies at once (fx_arena.c fxz_poll): the line given back (its effect silent a few blocks), its new size
 * placed when the arena has room; no room: MEM FULL as any refused line (op_input.c) */
#ifndef FELUCCA_DLY_LEN
#define FELUCCA_DLY_LEN 65536u                           /* (fx.c's default) */
#endif
#if FXM_NEED && FELUCCA_FX_DELAY
#if FELUCCA_DLY_LEN >= 32768
#define FXZ_DLY_N 4u
#elif FELUCCA_DLY_LEN >= 16384
#define FXZ_DLY_N 3u
#elif FELUCCA_DLY_LEN >= 8192
#define FXZ_DLY_N 2u
#else
#define FXZ_DLY_N 1u
#endif
#else
#define FXZ_DLY_N 1u
#endif
#if FXM_NEED && FELUCCA_FX_REVERB && FELUCCA_REV_FDN8 && FELUCCA_REV_POOL && !FELUCCA_REV_AIRWIN
#define FXZ_REV_N 2u                                     /* (the ring 16384 samples: SMALL 8192) */
#else
#define FXZ_REV_N 1u
#endif
#if FXM_NEED && FELUCCA_INS_XDLY && FELUCCA_XDLY_LEN == 16384 && FELUCCA_FXI_LINE_BLOCKS
#define FXZ_XDL_N 2u
#else
#define FXZ_XDL_N 1u
#endif
#define FXZ_ANY (FXZ_DLY_N > 1u || FXZ_REV_N > 1u || FXZ_XDL_N > 1u)
/* the sizes playing now (fx_arena.c sets them while the line is not placed: the audio reads them); the project's wanted
 * ones are song.g[G_FXZ_DLY ..] */
static uint8_t fxsz_dly, fxsz_rev, fxsz_xdl;
#define FXSZ_DLY_LEN() ((uint32_t)FELUCCA_DLY_LEN >> fxsz_dly)   /* the delay send's line now, samples */
static uint8_t fxsz_xoff;                               /* XDLY MEM wanted differs: the XDLY owners let go until none holds a line */
static uint8_t fxz_hold;                                /* the send lines changing size: not placed meanwhile (fx_arena.c fxm_needs) */
