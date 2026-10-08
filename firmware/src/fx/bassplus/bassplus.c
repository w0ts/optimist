/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.0: BASS+, the small speaker
 * mode, hugelton/Felucca 727f272, fx.c spk_bass / master_out, ui_menu.c SPEAKER; the 4-pole low-pass from
 * Felucca 1.0.2, db70550, #42) */
/* BASS+ (FELUCCA_BASSPLUS, backports.h): the menu's LOWCUT gets a third value, OFF / LOWCUT / BASS+
 * (settings.lowcut 0 / 1 / 2; fx_lowcut). BASS+ is for the FM-1's own small speaker: what it cannot play is
 * heard through its harmonics. The bass below ~150 Hz is clipped at its own envelope (a level-following
 * trapezoid: odd harmonics), band-passed ~220 Hz .. 1 kHz and added, while the low cut moves up to ~220 Hz
 * (12 dB/oct). The low-pass has 4 poles (Felucca 1.0.2 #42: with 2, the trapezoid rebuilt the mix's own
 * 300 .. 600 Hz, late, and cancelled up to 6 dB of it; tests/backports_test.c measured -5.7 dB at 440 Hz with 2
 * poles here, see t_bassplus_response). A build without the switch reads the 2 as LOWCUT (any non-zero value
 * is ON there). Included by fx.c (master_out); runs from RAM (HOT2: RAMTEXT is full), per sample, only while
 * BASS+ is on. */
static int32_t sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl;
static inline void spk_bass_reset(void)   /* (the host tests: a cleared state) */
{
    sb_lp1 = sb_lp2 = sb_lp3 = sb_lp4 = sb_env = sb_h1 = sb_h2 = sb_hl = 0;
}
AINL int32_t spk_bass(int32_t m)
{
    int32_t a, t, u;
    sb_lp1 += ((m - sb_lp1) * 692) >> 15;
    sb_lp2 += ((sb_lp1 - sb_lp2) * 692) >> 15;
    sb_lp3 += ((sb_lp2 - sb_lp3) * 692) >> 15;
    sb_lp4 += ((sb_lp3 - sb_lp4) * 692) >> 15;
    a = sb_lp4 < 0 ? -sb_lp4 : sb_lp4;
    if (a > sb_env)
        sb_env += (a - sb_env) >> 2;
    else if (sb_env > 0)
        sb_env = decay_to0(sb_env);   /* (dsp.c) */
    if (sb_env == 0) {           /* no bass left (t would be 0): the band-pass's floored steps never reach 0 from */
        sb_h1 = sb_h2 = sb_hl = 0;   /* below (with 4 poles a 55 Hz tone left sb_hl at -7: -21 of DC after it, */
        return 0;                    /* measured in tests/backports_test.c), so its residue is dropped here */
    }
    t =clamp(sb_lp4 * 8, -sb_env, sb_env);
    sb_h1 += (t - sb_h1) >> 5;
    u = t - sb_h1;
    sb_h2 += (u - sb_h2) >> 5;
    u -= sb_h2;
    sb_hl += (u - sb_hl) >> 3;
    return sb_hl * 3;
}

#define lowcut5(x, lc, err) lowcut_ef(x, lc, err, 5)     /* x minus its one-pole low-pass (~220 Hz; dsp_common.h) */

/* the master's low cut in BASS+ (the LOWCUT filters' states, one octave up) and the bass' harmonics */
static HOT2 __attribute__((noinline)) void bassplus_out(int32_t *l, int32_t *r)
{
    int32_t b = spk_bass((*l + *r) >> 1);
    *l = lowcut5(*l, &lc_l1, &lce[0]);
    *l = lowcut5(*l, &lc_l2, &lce[1]) + b;
    *r = lowcut5(*r, &lc_r1, &lce[2]);
    *r = lowcut5(*r, &lc_r2, &lce[3]) + b;
}
