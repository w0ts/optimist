/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.0: BASS+, the small speaker
 * mode, hugelton/Felucca 727f272, fx.c spk_bass / master_out, ui_menu.c SPEAKER) */
/* BASS+ (FELUCCA_BASSPLUS, backports.h): the menu's LOWCUT gets a third value, OFF / LOWCUT / BASS+
 * (settings.lowcut 0 / 1 / 2; fx_lowcut). BASS+ is for the FM-1's own small speaker: what it cannot play is
 * heard through its harmonics. The bass below ~150 Hz is clipped at its own envelope (a level-following
 * trapezoid: odd harmonics), band-passed ~220 Hz .. 1 kHz and added, while the low cut moves up to ~220 Hz
 * (12 dB/oct). A build without the switch reads the 2 as LOWCUT (any non-zero value is ON there). Included by
 * fx.c (master_out); runs from RAM (HOT2: RAMTEXT is full), per sample, only while BASS+ is on. */
static int32_t sb_lp1, sb_lp2, sb_env, sb_h1, sb_h2, sb_hl;
AINL int32_t spk_bass(int32_t m)
{
    int32_t a, t, u;
    sb_lp1 += ((m - sb_lp1) * 692) >> 15;
    sb_lp2 += ((sb_lp1 - sb_lp2) * 692) >> 15;
    a = sb_lp2 < 0 ? -sb_lp2 : sb_lp2;
    if (a > sb_env)
        sb_env += (a - sb_env) >> 2;
    else if (sb_env > 0)
        sb_env -= (sb_env >> 11) + 1;
    t = clamp(sb_lp2 * 8, -sb_env, sb_env);
    sb_h1 += (t - sb_h1) >> 5;
    u = t - sb_h1;
    sb_h2 += (u - sb_h2) >> 5;
    u -= sb_h2;
    sb_hl += (u - sb_hl) >> 3;
    return sb_hl * 3;
}

AINL int32_t lowcut5(int32_t x, int32_t *lc, int32_t *err)   /* x minus its one-pole low-pass (~220 Hz) */
{
    int32_t e = x - *lc + *err, d = e >> 5;
    *err = e - (d << 5);
    *lc += d;
    return x - *lc;
}

/* the master's low cut in BASS+ (the LOWCUT filters' states, one octave up) and the bass' harmonics */
static HOT2 __attribute__((noinline)) void bassplus_out(int32_t *l, int32_t *r)
{
    int32_t b = spk_bass((*l + *r) >> 1);
    *l = lowcut5(*l, &lc_l1, &lce[0]);
    *l = lowcut5(*l, &lc_l2, &lce[1]) + b;
    *r = lowcut5(*r, &lc_r1, &lce[2]);
    *r = lowcut5(*r, &lc_r2, &lce[3]) + b;
}
