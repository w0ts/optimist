# SPDX-License-Identifier: GPL-3.0-only
"""Pack 4, pads, leads, brass and FX: sustained voices. Programmed from scratch (dx7.py), in dB, seconds and modulation
indices (pk_common.py); the level of each voice is set by levels.json. Carriers are detuned against each other for
width; the brass voices bring their modulators in after their carriers, which is what makes an FM brass blare."""
from dx7 import op, voice
from pk_common import ATK, L, OLI, R, RT, adsr, decay, hold, perc, swell

TITLE = "pads, leads, brass and FX"
FAST = ATK(2)

VOICES = [
    # a warm pad: three carriers a few cents apart (alg 5), soft modulators at x1 and x2, a slow attack and release
    voice("WARM PAD", 5, [
        op(78, 1, det=-5, **swell(ATK(600), -2, 6, 1.2)),
        op(OLI(1.0), 1, **swell(ATK(700), -4, 8, 1.2)),
        op(92, 1, **swell(ATK(650), -2, 6, 1.2)),
        op(OLI(1.0), 2, det=2, **swell(ATK(750), -4, 8, 1.2)),
        op(78, 1, det=5, **swell(ATK(700), -2, 6, 1.2)),
        op(OLI(0.8), 1, det=-2, **swell(ATK(800), -4, 8, 1.2))], fb=2, role="held"),
    # string ensemble: a feedback sawtooth (OP6) into three carriers a few cents apart, the pair on the left the body; a late vibrato
    voice("STRING ENS", 22, [
        op(88, 1, **swell(ATK(250), -2, 6, 0.6)),
        op(OLI(1.4), 2, **swell(ATK(300), -4, 8, 0.6)),
        op(76, 1, det=-6, **swell(ATK(280), -2, 6, 0.6)),
        op(88, 1, **swell(ATK(250), -2, 6, 0.6)),
        op(76, 1, det=6, **swell(ATK(260), -2, 6, 0.6)),
        op(OLI(2.2), 1, **swell(ATK(300), -6, 8, 0.6))], fb=7, role="held",
        lfo=(30, 70, 5, 0, 4, 2)),
    # a choir: six harmonics (alg 32) shaped round the formants of an open "ah" (the 3rd and 5th strong), a slow breathing vibrato
    voice("CHOIR AAH", 32, [
        op(86, 1, **swell(ATK(220), -1, 6, 0.8)),
        op(82, 2, **swell(ATK(240), -1, 6, 0.8)),
        op(94, 3, **swell(ATK(260), -1, 6, 0.8)),
        op(84, 4, **swell(ATK(280), -1, 6, 0.8)),
        op(90, 5, **swell(ATK(300), -1, 6, 0.8)),
        op(70, 7, **swell(ATK(320), -1, 6, 0.8))], fb=0, role="held",
        lfo=(28, 60, 6, 0, 4, 2)),
    # a sweep pad: the modulators open slowly (an index from 0.1 to 2.5 over three seconds), a long tail
    voice("SWEEP PAD", 5, [
        op(78, 1, det=-3, **swell(ATK(900), -2, 8, 2.0)),
        op(OLI(2.5), 1, r=(ATK(3000), R(2), 99, RT(2.0)), l=(99, L(-3), L(-3), 0)),
        op(78, 1, det=3, **swell(ATK(1000), -2, 8, 2.0)),
        op(OLI(2.0), 2, r=(ATK(3500), R(2), 99, RT(2.0)), l=(99, L(-3), L(-3), 0)),
        op(92, 1, **swell(ATK(1100), -2, 8, 2.0)),
        op(OLI(1.6), 3, r=(ATK(4000), R(2), 99, RT(2.0)), l=(99, L(-3), L(-3), 0))], fb=1, role="held"),
    # an ice pad: x1 x2 x3 carriers over x7 modulators at a whisper, thin and glassy, a sheen from a slow tremolo
    voice("ICE PAD", 5, [
        op(86, 1, **swell(ATK(500), -2, 8, 1.5)),
        op(OLI(0.7), 7, ams=1, **swell(ATK(700), -4, 8, 1.5)),
        op(80, 2, det=2, **swell(ATK(550), -2, 8, 1.5)),
        op(OLI(0.6), 5, ams=1, **swell(ATK(800), -4, 8, 1.5)),
        op(74, 3, det=-2, **swell(ATK(600), -2, 8, 1.5)),
        op(OLI(0.5), 7, **swell(ATK(900), -4, 8, 1.5))], fb=0, role="held",
        lfo=(18, 0, 0, 36, 4, 0)),
    # a solo sawtooth lead: OP6's feedback sawtooth into three carriers tuned a hair apart (alg 22), a fast attack and a vibrato
    voice("SOLO SAW", 22, [
        op(88, 1, **adsr(ATK(12), 3, -2, 0.12)),
        op(OLI(1.4), 2, **adsr(ATK(12), 3, -4, 0.12)),
        op(90, 1, det=-3, **adsr(ATK(12), 3, -2, 0.12)),
        op(84, 1, det=3, **adsr(ATK(12), 3, -2, 0.12)),
        op(86, 1, **adsr(ATK(12), 3, -2, 0.12)),
        op(OLI(2.2), 1, **adsr(ATK(12), 5, -5, 0.12))], fb=6, role="melody",
        lfo=(34, 60, 6, 0, 4, 2)),
    # a sync lead: a non-integer modulator (x2.3) into a x1 carrier makes the sweeping, sync-like overtones; the sweep falls in a second
    voice("SYNC LEAD", 16, [
        op(92, 1, **adsr(ATK(8), 3, -2, 0.12)),
        op(OLI(2.8), 2.3, **decay(ATK(8), 8, -14, 3, 0.1)),
        op(OLI(1.8), 1, **decay(ATK(8), 10, -12, 4, 0.1)),
        op(OLI(1.4), 3.1, **decay(ATK(8), 12, -16, 4, 0.1)),
        op(OLI(1.0), 1, **adsr(ATK(8), 4, -4, 0.1)),
        op(OLI(1.6), 1, **adsr(ATK(8), 4, -4, 0.1))], fb=6, role="melody",
        lfo=(36, 50, 5, 0, 4, 2)),
    # a flute: a pure x1 tone, a breath of x1 modulation, and a puff of high noise-like partials (x7.3) at the attack; a late vibrato
    voice("FLUTE", 5, [
        op(92, 1, **adsr(ATK(55), 4, -2, 0.15)),
        op(OLI(0.55), 1, **adsr(ATK(55), 4, -2, 0.15)),
        op(60, 1, **adsr(ATK(80), 4, -2, 0.15)),
        op(OLI(0.45), 7.3, **decay(ATK(10), 40, -26, 20, 0.1)),
        None, None], fb=2, role="melody",
        lfo=(32, 90, 4, 0, 4, 2)),
    # a clarinet: an x2 modulator on an x1 carrier gives odd harmonics only (the cylindrical bore); a throaty chalumeau
    voice("CLARINET", 5, [
        op(92, 1, **adsr(ATK(35), 4, -2, 0.12)),
        op(OLI(1.7), 2, **adsr(ATK(35), 4, -3, 0.12)),
        op(70, 1, det=2, **adsr(ATK(40), 4, -2, 0.12)),
        op(OLI(1.2), 2, **adsr(ATK(40), 4, -3, 0.12)),
        None, None], fb=3, role="melody",
        lfo=(32, 80, 4, 0, 4, 2)),
    # a brass section: three detuned x1 carriers; the x1 modulators come in after the carriers (an index of 2.6, a tenth of a second later)
    voice("BRASS SEC", 5, [
        op(80, 1, det=-3, vel=2, r=(ATK(40), R(5), 99, RT(0.15)), l=(99, L(-3), L(-3), 0)),
        op(OLI(2.6), 1, vel=3, r=(ATK(75), R(14), 99, RT(0.15)), l=(99, L(-6), L(-6), 0)),
        op(92, 1, vel=2, r=(ATK(40), R(5), 99, RT(0.15)), l=(99, L(-3), L(-3), 0)),
        op(OLI(2.6), 1, det=2, vel=3, r=(ATK(75), R(14), 99, RT(0.15)), l=(99, L(-6), L(-6), 0)),
        op(80, 1, det=3, vel=2, r=(ATK(40), R(5), 99, RT(0.15)), l=(99, L(-3), L(-3), 0)),
        op(OLI(2.4), 1, det=-2, vel=3, r=(ATK(75), R(14), 99, RT(0.15)), l=(99, L(-6), L(-6), 0))], fb=3, role="comp",
        lfo=(32, 70, 4, 0, 4, 2)),
    # a synth brass: a big, slow-opening one: OP6's feedback sawtooth into three carriers and a pair, a 0.15 s swell
    voice("SYN BRASS", 22, [
        op(88, 1, **adsr(ATK(100), 4, -3, 0.2)),
        op(OLI(1.8), 1, **adsr(ATK(130), 6, -6, 0.2)),
        op(76, 1, det=-4, **adsr(ATK(100), 4, -3, 0.2)),
        op(76, 1, det=4, **adsr(ATK(100), 4, -3, 0.2)),
        op(86, 1, **adsr(ATK(100), 4, -3, 0.2)),
        op(OLI(2.4), 1, **adsr(ATK(140), 8, -8, 0.2))], fb=5, role="comp"),
    # a solo trumpet: one bright stack: a modulator of x1 that overshoots at the front (index 3.4, settling to 2.2) for the blat; a late vibrato
    voice("TRUMPET", 5, [
        op(94, 1, vel=2, **adsr(ATK(30), 4, -2, 0.1)),
        op(OLI(3.4), 1, vel=4, r=(ATK(40), R(40), 99, RT(0.1)), l=(99, L(-10), L(-10), 0)),
        op(76, 2, vel=2, **adsr(ATK(32), 4, -4, 0.1)),
        op(OLI(2.0), 1, vel=4, r=(ATK(45), R(30), 99, RT(0.1)), l=(99, L(-9), L(-9), 0)),
        None, None], fb=4, role="melody",
        lfo=(34, 90, 4, 0, 4, 2)),
    # a riser: a rising effect: the pitch climbs about a octave and a half as a feedback tone swells and brightens over a second and a half
    voice("RISER", 5, [
        op(90, 1, **swell(ATK(1400), -1, 3, 0.4)),
        op(OLI(3.0), 1.41, **swell(ATK(1600), -1, 3, 0.4)),
        op(84, 2.01, det=3, **swell(ATK(1500), -1, 3, 0.4)),
        op(OLI(2.4), 3, **swell(ATK(1700), -1, 3, 0.4)),
        None, None], fb=7, role="held",
        peg=((24, 99, 99, 60), (82, 50, 50, 28))),
    # a laser: a zap: a high start that falls three octaves in a quarter second, a sharp metallic modulator
    voice("LASER ZAP", 5, [
        op(94, 1, **decay(FAST, 60, -6, 60, 0.1)),
        op(OLI(2.5), 2.7, **decay(FAST, 50, -20, 50, 0.1)),
        op(84, 1, det=5, **decay(FAST, 60, -6, 60, 0.1)),
        op(OLI(1.8), 1.7, **decay(FAST, 50, -20, 50, 0.1)),
        None, None], fb=6, role="melody",
        peg=((78, 99, 99, 60), (50, 50, 50, 96))),
    # wind: broadband, a feedback tone at a high ratio goes to noise; a slow gust from the LFO's amplitude
    voice("WIND", 32, [
        op(86, 3, **swell(ATK(900), -4, 6, 1.5)),
        op(80, 5, **swell(ATK(1100), -4, 6, 1.5)),
        None, None, None,
        op(88, 9, **swell(ATK(1000), -2, 6, 1.5))], fb=7, role="held",
        lfo=(10, 0, 0, 90, 0, 0)),
    # sci-fi: an alien pad; inharmonic modulators (x1.414, x2.236) that swing with a slow amplitude LFO (AMS 3)
    voice("SCI-FI", 7, [
        op(88, 1, **swell(ATK(700), -2, 8, 1.2)),
        op(OLI(1.4), 1.41, ams=3, **swell(ATK(1000), -3, 8, 1.2)),
        op(86, 1, det=4, **swell(ATK(800), -2, 8, 1.2)),
        op(OLI(2.0), 2.24, ams=3, **swell(ATK(1200), -3, 8, 1.2)),
        op(OLI(1.6), 3.16, ams=2, **swell(ATK(1400), -3, 8, 1.2)),
        op(OLI(2.0), 1, **swell(ATK(1000), -3, 8, 1.2))], fb=4, role="held",
        lfo=(12, 0, 0, 70, 0, 0)),
]
