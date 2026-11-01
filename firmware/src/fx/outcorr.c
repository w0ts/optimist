/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The output's correction by the headphone jack (the user, 2026-10-29): SYSTEM > GENERAL's SPK LOWCUT on the FM-1's own
 * speaker, HPHN BASS on headphones, each only on its side of the jack (PB5, system/hpjack.h), and only on the FM-1's own
 * codec output: never on Bluetooth, USB or the card's recording.
 *
 *   SPK LOWCUT  OFF / ON / BASS+   only while nothing is in the jack. ON: the ~110 Hz 12 dB/oct low cut (lowcut1 x 2);
 *               BASS+: Felucca 1.0's small-speaker mode (bassplus/bassplus.c: the low cut an octave up, the bass'
 *               harmonics added). Default ON.
 *   HPHN BASS     OFF / +3 dB / +6 dB  only while headphones are in: a low shelf, x + g * LP(x), LP the one-pole ~110 Hz
 *               of the low cut (lowcut_ef, shift 6), g 0.414 (+3 dB) or 1 (+6 dB) at DC. Default OFF.
 *
 * Where: before the limiter, where the low cut always was (fx.c master_pre, in the output stage of mix_finish: the
 * limiter's input is today's, in every case). The block's choice is made before it is rendered (oc_begin, from the
 * audio interrupt: core/audio.c audio_block, fm1-base's optimist/core/opt_core.c):
 *   - Bluetooth streamed the last block (the FM-1's own output is muted then): no correction at all; Bluetooth gets the
 *     plain output. A stream that starts during a corrected block gets it as silence (oc_bt_frames: 0), and the blocks
 *     after it while LIMIT's lookahead still holds corrected samples (OC_LA, ~1.5 ms): never a corrected sample.
 *   - the card records (SD_REC) while a correction is on: the recorder gets an output stage of its own, the plain one
 *     (fx.c sd_mst_out: the volume, the DC blocker, the limiter and the knee or LIMIT's brickwall, on its own state,
 *     taken over from the output's when it starts: oc_rec_frames); one stage more a sample, only then.
 *   - USB never reads the output (its MASTER is ua_mst, its own output stage; the stems are taps before the master).
 * CPU: today's low cut, behind the choice (none while the setting of the jack's side is OFF); HPHN BASS one one-pole a
 * side and a multiply. fx_lowcut (fx.c) is the block's correction before the limiter (OC_*; the tests set it). */
enum { OC_NONE, OC_LC, OC_BP, OC_HP3, OC_HP6 };
static volatile uint8_t spk_lc = 1;     /* SYSTEM > SPK LOWCUT: 0 OFF, 1 ON, 2 BASS+ (settings, panel.c) */
static volatile uint8_t hp_bass;        /* SYSTEM > HPHN BASS: 0 OFF, 1 +3 dB, 2 +6 dB (settings) */
static volatile uint8_t hp_in;          /* the jack: 1 headphones in (debounced by the main loop: system/hpjack.h) */
static uint8_t oc_last, oc_rec;         /* (the audio interrupt's: the correction of the last block; the recorder takes
                                         * its own plain stage this block, sd_mst) */
static int32_t hb_l, hb_r, hbe[2];      /* HPHN BASS's low-passes and their remainders */

AINL uint32_t oc_want(void)             /* the correction the jack's side asks for */
{
    if (hp_in)
        return hp_bass ? OC_HP3 - 1u + (hp_bass > 2u ? 2u : hp_bass) : OC_NONE;
    return spk_lc > 2u ? OC_LC : spk_lc;                /* (OC_LC 1, OC_BP 2: the setting's values) */
}
static void oc_reset(void)              /* the filters from rest (a correction turned on: no old state's step) */
{
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = 0;
    lce[0] = lce[1] = lce[2] = lce[3] = 0;
    spk_bass_reset();
    hb_l = hb_r = hbe[0] = hbe[1] = 0;
}
/* the settings word (settings.lowcut, the settings record's lowcut slot): bits 0..1 SPK LOWCUT, 4..5 HPHN BASS. A record
 * written before has 0 / 1 / 2 there, the old LOWCUT's OFF / LOWCUT / BASS+: SPK LOWCUT's same three, HPHN BASS OFF
 * (a firmware from before reads a word with HPHN BASS set as LOWCUT, its value over 2) */
AINL uint32_t oc_word(uint32_t spk, uint32_t hp) { return (spk & 3u) | (hp & 3u) << 4; }
static uint32_t oc_set_word(uint32_t w)   /* the word into the settings -> the word as kept (out of range: the default) */
{
    uint32_t s = w & 3u, h = w >> 4 & 3u;
    spk_lc = (uint8_t)(s > 2u ? 1u : s);
    hp_bass = (uint8_t)(h > 2u ? 0u : h);
    return oc_word(spk_lc, hp_bass);
}
/* (LIMIT's brickwall delays the output by its lookahead: corrected samples still come out that long after the last
 * corrected block) */
#if FELUCCA_MASTER_COMP
#define OC_LA 64u                       /* (master_comp.h MLIM_N: fx.c checks it) */
#else
#define OC_LA 0u
#endif
static uint32_t oc_left;                /* frames from the start of the coming call that may still carry a corrected sample */
static uint8_t oc_dirty, oc_clean;      /* this call's output may carry one; the output's lookahead held none at its start */
/* the block's correction, before it is rendered: bt the last block streamed to Bluetooth (the FM-1's own output muted),
 * rec the card records this block, n the frames this call renders */
static void oc_begin(uint32_t bt, uint32_t rec, uint32_t n)
{
    uint32_t w = bt ? OC_NONE : oc_want();
    oc_clean = (uint8_t)(!oc_left && !oc_last);
    if (w != oc_last) {
        if (w)
            oc_reset();
        oc_last = (uint8_t)w;
    }
    fx_lowcut = (uint8_t)w;
    oc_dirty = (uint8_t)(w || oc_left);
    oc_left = w ? OC_LA : oc_left > n ? oc_left - n : 0u;
    oc_rec = (uint8_t)(rec && oc_dirty);
}
/* HPHN BASS: the low shelf, both sides (HOT2 as bassplus_out: one copy for mout_block and mlim_block) */
static HOT2 __attribute__((noinline)) void hp_shelf(int32_t *l, int32_t *r, uint32_t m)
{
    int32_t a = *l - lowcut_ef(*l, &hb_l, &hbe[0], 6), b = *r - lowcut_ef(*r, &hb_r, &hbe[1], 6);
    if (m == OC_HP3)
        a = (a * 53) >> 7, b = (b * 53) >> 7;           /* 0.414: +3 dB at DC */
    *l += a;
    *r += b;
}
AINL void oc_apply(uint32_t m, int32_t *l, int32_t *r)   /* correction m on one frame */
{
    if (m == OC_LC) {                   /* two one-pole high-passes, error feedback as dc_block (the */
        *l = lowcut1(*l, &lc_l1, &lce[0]);          /* rounded step stopped at |x - lc| < 32: an offset) */
        *l = lowcut1(*l, &lc_l2, &lce[1]);
        *r = lowcut1(*r, &lc_r1, &lce[2]);
        *r = lowcut1(*r, &lc_r2, &lce[3]);
    } else if (m == OC_BP)
        bassplus_out(l, r);                         /* BASS+: an octave up, the bass' harmonics added */
    else
        hp_shelf(l, r, m);
}
/* the frames Bluetooth takes from this block: the output, or 0 (silence) when it is corrected (a stream that started
 * during it: the corrected samples never go out) */
static const int32_t *oc_bt_frames(const int32_t *out) { return oc_dirty ? 0 : out; }
#ifndef OC_REC
#define OC_REC FELUCCA_SD_REC            /* (the card's recorder; tests/outcorr_test.c builds it alone) */
#endif
#if OC_REC
/* the recorder's own plain output stage (oc_begin: rec while the output may carry a corrected sample; fx.c sd_mst_out
 * fills it) */
static int32_t sd_mst[2u * CTL] __attribute__((section(".pool")));
static mout_t sd_mo;
static uint8_t sd_mo_on;                /* sd_mo (and sd_mlim) in use: 0, taken over from the output's at the next */
/* the frames the card records from this block: the output, or the recorder's own plain stage's */
static const int32_t *oc_rec_frames(const int32_t *out) { return oc_rec ? sd_mst : out; }
#endif
