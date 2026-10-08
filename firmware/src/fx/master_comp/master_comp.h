/* SPDX-License-Identifier: GPL-3.0-only */
/* The master COMP and LIMIT (FELUCCA_MASTER_COMP, registry.h; master_comp.c).
 *
 * Where: fx.c mix_finish, after DUST / PUNCH / FILT and before the master volume, runs the compressor on the mix bus
 * (mc_master, a block); while CEIL is set, mix_finish's output loop is mlim_block: the volume, the DC blocker and the
 * low cut as master_out has them (master_pre), then the limiter instead of the old peak limiter + tanh knee. With THRS
 * OFF, GAIN 0 and CEIL OFF (every project from before: project.c mc_pack) nothing here touches a sample: the master is
 * as before.
 *
 * COMPRESSOR, a feed-forward "glue" bus compressor (D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range
 * Compressor Design - A Tutorial and Analysis", JAES 60(6), 2012: the formulas, no code taken):
 *   detector   the stereo-linked peak (max |L|, |R|) of each 4-sample sub-block, to log2 (Q8: 1/256 octave)
 *   gain comp. a soft knee 6 dB wide, the ratio as a slope 1 - 1/R (their eq. 4), in the log domain
 *   ballistics a smooth branching one-pole on the gain reduction, in the log domain (their recommended placement:
 *              no attack / release interaction with the level); AUTO release: a fast envelope (60 ms) and a slow one
 *              (1.2 s) that charges only under sustained reduction (~400 ms), the larger wins: a transient recovers
 *              fast, a long squeeze slowly (program-dependent release, as the SSL bus compressor's AUTO is described)
 *   make-up    GAIN, 0..15 dB, after the compressor (also with THRS OFF: a drive into the limiter)
 *   gain       2^x from a 65-point table, per sub-block, linear across its 4 samples, applied in Q13 with an exact
 *              split multiply (no 64-bit product per sample): at 0 dB of gain the output is the input, bit for bit
 * LIMITER, a lookahead brickwall: 64 samples (1.45 ms) of delay; the reduction each sample needs, held for 65 samples
 *   (a two-stage running maximum, no search), released over 80 ms, then averaged over the last 64 (a box filter: the
 *   attack ramps over the lookahead). Each averaged value covers the delayed sample's own need, so no sample passes
 *   the ceiling (tests/master_comp_test.c: full-scale bursts, noise, impulses). CEIL: -0.1 .. -6 dB below the output's
 *   full scale (Q15 32767 = the -6 dBFS I2S ceiling audio.c keeps). As in Signalsmith Audio's "Designing a
 *   straightforward limiter" (G. Luff, 2022: peak hold + box filter, the idea only).
 *
 * REUSE (design for later, not built): the compressor is an instance, mc_t (its state) run with mc_set_t (its settings
 * resolved once a block) on a key and an audio pair. No file-level state inside mc_run: the same code can run as a
 * per-track INSERT (one mc_t per track, its settings from the track's own parameters) with no rewrite. This build has
 * one instance, the master's (mc), so the RAM is as measured.
 * SIDECHAIN (later, not built): mc_run takes the key (kl, kr) apart from the audio (l, r). Today the key is the master
 * itself, the same pointers: no copy, no cost. A SOURCE parameter would pick it (MASTER / TRACK n / DRUMS / KICK: the
 * project has no reserved bit left for it (project.c mc_pack), so it needs a format change): a track's buffer, a
 * send bus, the drum track, or the kick lane, through an optional high-pass on the key (an HPF so the bass does not
 * pump the mix). DUCK (fx.c duck_block) is today's primitive sidechain: every kick dips the synth parts along a fixed
 * curve over an eighth note. It would become a preset of this compressor (key KICK, a fast attack, a release of an
 * eighth note, on the parts' bus instead of the master); the old DUCK stays for projects that use it. */
#ifndef MASTER_COMP_H
#define MASTER_COMP_H

#define MC_SUB 4u                       /* samples a sub-block (the detector and the gain's step) */
#define MLIM_N 64u                      /* the limiter's lookahead and box (a power of two) */
#define MLIM_HOLD (MLIM_N + 1u)         /* the hold: one longer than the box (the delay is the box's length) */

typedef struct {                        /* one compressor's settings, resolved once a block (mc_settings) */
    int32_t on;                         /* the compressor (THRS not OFF) */
    int32_t thr8;                       /* threshold, log2 Q8 of |x| (Q15 full scale = 15 << 8) */
    int32_t slope14;                    /* 1 - 1 / RATIO, Q14 */
    int32_t katk, krel;                 /* one-pole steps a sub-block, Q20; krel 0: AUTO */
    int32_t gain16;                     /* make-up, log2 Q16 */
} mc_set_t;

typedef struct {                        /* one compressor's state (an instance: the master's now, a track's later) */
    int32_t gr16;                       /* the gain reduction now, log2 Q16 (>= 0) */
    int32_t slow16;                     /* AUTO: the slow envelope */
    int32_t g13;                        /* the linear gain at the end of the last sub-block, Q13 */
    int32_t gr_pk;                      /* the largest gr16 since the meters last took it */
} mc_t;

typedef struct {                        /* the limiter */
    int32_t on;                         /* CEIL set (fx.c mix_finish: mlim_block instead of the limiter and knee) */
    int32_t c8;                         /* the ceiling, log2 Q8 (less a margin) */
    int32_t clin;                       /* |x| up to which no reduction is needed */
    int32_t hold, m2, cnt;              /* the two-stage running maximum of the need (log2 Q8) */
    int32_t env16;                      /* held, then released (log2 Q16) */
    int32_t sum;                        /* the box: the sum of ring[] */
    uint32_t pos;
    int32_t gr_pk;                      /* the largest reduction applied since the meters took it (log2 Q8) */
    int32_t s8, g13;                    /* the last reduction applied and its gain (Q13) */
    uint16_t ring[MLIM_N];              /* the released need of the last MLIM_N samples (log2 Q8, rounded up) */
    int32_t dl[MLIM_N], dr[MLIM_N];     /* the delay */
} mlim_t;

#endif
