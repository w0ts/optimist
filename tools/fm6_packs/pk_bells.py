# SPDX-License-Identifier: GPL-3.0-only
"""Pack 3, bells and mallets: struck and plucked tuned percussion, from church bells to marimba, kalimba and steel drum.
Programmed from scratch (dx7.py), in dB, seconds and modulation indices (pk_common.py); the level of each voice is set by
levels.json. Inharmonic partials come from non-integer ratios (3.5, 2.76, 4.2 ...); an object struck rings its higher
partials shorter than its low ones, so the modulators fall faster than the carriers."""
from dx7 import op, voice
from pk_common import ATK, L, OLI, R, RT, adsr, decay, hold, perc, swell

TITLE = "bells and mallets"
FAST = ATK(2)

VOICES = [
    # a bell (alg 5, three pairs): x1 and x2.4 carriers against x3.5 / x1.41 / x5.2 modulators, long decay, the
    # modulators gone sooner so that the sound ends pure
    voice("BELL FM", 5, [
        op(92, 1, vel=2, **decay(FAST, 12, -4, 4, 0.8)),
        op(OLI(3.0), 3.5, vel=3, **decay(FAST, 26, -24, 8, 0.8)),
        op(84, 2.4, vel=2, **decay(FAST, 18, -6, 6, 0.6)),
        op(OLI(2.0), 1.41, vel=3, **decay(FAST, 36, -26, 10, 0.6)),
        op(72, 5.2, vel=2, **decay(FAST, 24, -8, 8, 0.5)),
        op(OLI(1.0), 7, vel=4, **decay(FAST, 50, -30, 14, 0.5))], fb=0),
    # tubular bells: the strike partials of a hung tube (1 : 2.76 : 5.4 : 8.9), an index that falls in a second
    voice("TUBULAR", 5, [
        op(92, 1, vel=3, **decay(FAST, 10, -4, 3, 1.0)),
        op(OLI(2.6), 3.5, vel=4, **decay(FAST, 22, -22, 7, 1.0)),
        op(80, 2.76, vel=2, **decay(FAST, 14, -8, 5, 0.8)),
        op(OLI(1.8), 1, vel=3, **decay(FAST, 30, -26, 9, 0.8)),
        op(68, 5.4, vel=2, **decay(FAST, 20, -10, 7, 0.6)),
        op(OLI(1.0), 1.5, vel=3, **decay(FAST, 40, -28, 12, 0.6))], fb=0),
    # a vibraphone: a pure x1 bar with a bright x4 flick, the motor's tremolo (the LFO's amplitude on the carriers), rings
    voice("VIBES", 5, [
        op(92, 1, vel=3, ams=2, **decay(FAST, 8, -3, 5, 0.5)),
        op(OLI(1.5), 4, vel=4, **decay(FAST, 70, -34, 14, 0.4)),
        op(70, 4, vel=2, ams=2, **decay(FAST, 40, -8, 16, 0.3)),
        op(OLI(0.8), 10, vel=4, **perc(110, 0.2)),
        None, None], fb=0,
        lfo=(24, 0, 0, 50, 4, 0)),
    # a marimba: the x1 bar and the x4 overtone (struck, so tuned two octaves up), the mallet a x1 modulator that is over at 80 ms
    voice("MARIMBA", 5, [
        op(94, 1, vel=4, **decay(FAST, 34, -6, 18, 0.2)),
        op(OLI(2.2), 1, vel=5, **perc(110, 0.1)),
        op(78, 4, vel=3, **perc(60, 0.1)),
        op(OLI(1.0), 4, vel=5, **perc(160, 0.06)),
        None, None], fb=1),
    # a xylophone: a short, hard, bright bar: x1 with an x3 modulator and an x6.5 click
    voice("XYLOPHONE", 5, [
        op(94, 1, vel=4, **perc(70, 0.08)),
        op(OLI(2.6), 3, vel=5, **perc(180, 0.05)),
        op(80, 3, vel=3, **perc(100, 0.06)),
        op(OLI(1.2), 6.5, vel=5, **perc(220, 0.04)),
        None, None], fb=0, trnsp=12),
    # a glockenspiel: very high steel bars; x1 with a x3.5 modulator and a x6.9 overtone, rings
    voice("GLOCKEN", 5, [
        op(92, 1, vel=3, **decay(FAST, 14, -5, 6, 0.5)),
        op(OLI(1.8), 3.5, vel=4, **decay(FAST, 40, -30, 12, 0.4)),
        op(74, 6.9, vel=2, **decay(FAST, 28, -10, 10, 0.3)),
        op(OLI(0.8), 4.1, vel=4, **perc(100, 0.2)),
        None, None], fb=0, trnsp=12),
    # a kalimba: tines on a box; an x1 tone with a quick x5.4 twang and a soft pluck of the pitch
    voice("KALIMBA", 5, [
        op(94, 1, vel=3, **decay(FAST, 28, -6, 14, 0.2)),
        op(OLI(1.7), 5.4, vel=5, **perc(120, 0.1)),
        op(76, 2, vel=2, **decay(FAST, 50, -14, 20, 0.15)),
        op(OLI(0.9), 1, vel=3, **perc(80, 0.1)),
        None, None], fb=0, peg=((99, 70, 99, 99), (50, 50, 50, 52))),
    # a steel drum: the octave pair is its voice (x1 and x2), a x1.5 modulator gives the pan its hollow ring
    voice("STEEL DRUM", 5, [
        op(94, 1, vel=3, **decay(FAST, 26, -6, 12, 0.3)),
        op(OLI(1.4), 1.5, vel=4, **decay(FAST, 48, -20, 14, 0.3)),
        op(88, 2, vel=3, det=2, **decay(FAST, 30, -8, 14, 0.3)),
        op(OLI(1.0), 3, vel=4, **perc(60, 0.2)),
        op(70, 3, vel=2, **perc(40, 0.2)),
        op(OLI(0.6), 1, **perc(70, 0.2))], fb=0),
    # a music box: a comb of high pins, x2 with an x5.1 modulator, the spring's bright and thin tone, a long soft ring
    voice("MUSIC BOX", 5, [
        op(92, 2, vel=3, **decay(FAST, 18, -6, 8, 0.4)),
        op(OLI(1.2), 5.1, vel=4, **decay(FAST, 50, -30, 14, 0.3)),
        op(76, 4, vel=2, **decay(FAST, 26, -10, 12, 0.3)),
        op(OLI(0.7), 7, vel=4, **perc(100, 0.2)),
        None, None], fb=0, trnsp=12),
    # crystal: a held shimmer: x1 x3 x5 carriers with slow-breathing x7 / x4 modulators; a pad of glass
    voice("CRYSTAL", 5, [
        op(88, 1, **swell(ATK(80), -3, 6, 1.2)),
        op(OLI(0.9), 7, ams=2, **swell(ATK(200), -6, 10, 1.2)),
        op(80, 3, det=2, **swell(ATK(90), -4, 6, 1.2)),
        op(OLI(0.7), 4, ams=2, **swell(ATK(300), -6, 10, 1.2)),
        op(72, 5, det=-3, **swell(ATK(120), -6, 6, 1.2)),
        op(OLI(0.5), 3, **swell(ATK(400), -8, 10, 1.2))], fb=1,
        lfo=(16, 0, 0, 40, 4, 0)),
    # wind chimes: six inharmonic sines (additive, alg 32) of different ring times, the high ones shortest
    voice("CHIMES", 32, [
        op(86, 1, vel=2, **decay(FAST, 14, -4, 4, 1.2)),
        op(80, 2.4, vel=2, **decay(FAST, 18, -4, 5, 1.0)),
        op(76, 3.6, vel=2, **decay(FAST, 22, -4, 6, 0.9)),
        op(70, 5.4, vel=2, **decay(FAST, 28, -4, 8, 0.8)),
        op(64, 6.9, vel=2, **decay(FAST, 34, -4, 10, 0.7)),
        op(58, 8.9, vel=2, **decay(FAST, 40, -4, 12, 0.6))], fb=0, trnsp=12),
    # a gamelan gong-chime: pairs tuned apart so that they beat (ombak), a x1.41 modulator for the metal
    voice("GAMELAN", 5, [
        op(92, 1, vel=2, det=-7, **decay(FAST, 12, -4, 4, 0.8)),
        op(OLI(2.0), 1.41, vel=3, **decay(FAST, 26, -24, 8, 0.6)),
        op(92, 1, vel=2, det=7, **decay(FAST, 12, -4, 4, 0.8)),
        op(OLI(1.6), 2.24, vel=3, **decay(FAST, 30, -24, 9, 0.6)),
        op(70, 2, vel=2, **decay(FAST, 20, -8, 6, 0.6)),
        op(OLI(1.0), 3.16, vel=3, **decay(FAST, 40, -26, 12, 0.5))], fb=2),
    # handbells: a clean x1 with a x4.1 clapper, medium ring, a little belly of tremolo
    voice("HANDBELLS", 5, [
        op(92, 1, vel=3, **decay(FAST, 16, -5, 6, 0.5)),
        op(OLI(2.0), 4.1, vel=4, **decay(FAST, 36, -26, 12, 0.4)),
        op(78, 3, vel=2, **decay(FAST, 22, -8, 8, 0.4)),
        op(OLI(1.2), 6.2, vel=4, **perc(80, 0.3)),
        None, None], fb=0),
    # a glass harp: a bowed glass; very pure, a slow bloom, a late wobble
    voice("GLASS HARP", 5, [
        op(92, 1, **swell(ATK(180), -2, 8, 0.6)),
        op(OLI(0.5), 1, **swell(ATK(300), -4, 8, 0.6)),
        op(70, 2, det=2, **swell(ATK(260), -6, 8, 0.6)),
        op(OLI(0.4), 3, **swell(ATK(380), -8, 8, 0.6)),
        None, None], fb=0,
        lfo=(32, 80, 5, 0, 4, 2)),
    # a timpani: a drum head: the pitch dips at the stroke, a x1.5 / x2 modulator for the head's inharmonic tone, soft
    voice("TIMPANI", 5, [
        op(94, 0.5, vel=3, **decay(FAST, 20, -6, 8, 0.25)),
        op(OLI(1.4), 1.5, vel=4, **perc(80, 0.2)),
        op(80, 1, vel=2, **decay(FAST, 26, -8, 12, 0.2)),
        op(OLI(0.9), 2, vel=3, **perc(100, 0.15)),
        None, None], fb=2, peg=((92, 99, 99, 30), (50, 50, 50, 62))),
    # a dulcimer: hammered strings in pairs, tuned a hair apart; x1 and x2 with an x3 modulator, a bright decaying chorus
    voice("DULCIMER", 5, [
        op(92, 1, vel=3, det=-3, **decay(FAST, 26, -6, 10, 0.3)),
        op(OLI(2.0), 3, vel=5, **decay(FAST, 70, -28, 16, 0.3)),
        op(80, 1, vel=3, det=3, **decay(FAST, 28, -6, 10, 0.3)),
        op(OLI(1.6), 2, vel=5, **decay(FAST, 60, -26, 14, 0.3)),
        op(76, 2, vel=2, **decay(FAST, 40, -10, 16, 0.2)),
        op(OLI(0.9), 5, vel=5, **perc(140, 0.1))], fb=1),
]
