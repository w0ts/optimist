# SPDX-License-Identifier: GPL-3.0-only
"""Pack 2, basses: sixteen, from finger and fretless to synth, sub and 808. Programmed from scratch (dx7.py), in dB,
seconds and modulation indices (pk_common.py); the level of each voice is set by levels.json."""
from dx7 import op, voice
from pk_common import ATK, L, OLI, R, RT, adsr, decay, hold, perc, swell

TITLE = "basses"
FAST = ATK(2)
SOFT = ATK(8)

VOICES = [
    # a plucked synth bass (alg 5, three pairs): the x1 modulator is a 3-radian bite that falls in 100 ms, a sub octave under
    # it, a bright octave over it
    voice("PLUCK BASS", 5, [
        op(94, 1, vel=3, **decay(FAST, 30, -6, 12, 0.12)),
        op(OLI(3.0), 1, vel=5, **decay(FAST, 70, -28, 20, 0.12)),
        op(86, 0.5, vel=2, **decay(FAST, 24, -6, 10, 0.12)),
        op(OLI(1.4), 0.5, vel=3, **decay(FAST, 50, -26, 16, 0.12)),
        op(70, 2, vel=2, **decay(FAST, 50, -12, 20, 0.1)),
        op(OLI(1.0), 3, vel=4, **perc(110, 0.1))], fb=2, role="bass"),
    # a slap bass: a hard snap (x7 and x3 modulators gone in 60 ms, velocity 6) over a short, round body
    voice("SLAP BASS", 5, [
        op(94, 1, vel=4, **decay(FAST, 36, -8, 14, 0.1)),
        op(OLI(3.6), 1, vel=6, ksl=(40, 0, 30, 0, 0), **decay(FAST, 120, -30, 20, 0.1)),
        op(80, 2, vel=3, **decay(FAST, 70, -14, 24, 0.1)),
        op(OLI(2.6), 7, vel=7, **perc(240, 0.06)),
        op(76, 1, vel=2, det=2, **decay(FAST, 30, -8, 12, 0.1)),
        op(OLI(1.6), 3, vel=6, **perc(160, 0.06))], fb=3, role="bass"),
    # a fretless: round, a soft index that swells in a tenth of a second (the mwah), a slow late vibrato
    voice("FRETLESS", 1, [
        op(94, 1, vel=3, **decay(FAST, 14, -4, 5, 0.25)),
        op(OLI(1.6), 1, vel=4, r=(ATK(90), R(20), R(8), RT(0.25)), l=(99, L(-14), L(-20), 0)),
        op(82, 2, vel=2, **decay(FAST, 20, -10, 8, 0.2)),
        op(OLI(0.9), 3, vel=4, **decay(FAST, 50, -30, 12, 0.2)),
        op(OLI(0.7), 1, vel=3, **decay(FAST, 40, -24, 10, 0.2)),
        op(OLI(0.8), 1, vel=3, **decay(FAST, 30, -20, 10, 0.2))], fb=2, role="bass",
        lfo=(28, 70, 6, 0, 4, 2)),
    # a sawtooth bass: OP6 feeds itself back to a sawtooth (alg 32: six carriers), a sine an octave under and one beside it,
    # a pluck on the saw's level
    voice("SAW SYNTH", 32, [
        op(88, 0.5, **adsr(FAST, 6, -3, 0.12)),
        op(72, 1, det=-3, **adsr(FAST, 6, -6, 0.12)),
        None, None, None,
        op(86, 1, det=2, **decay(FAST, 60, -4, 5, 0.12))], fb=7, role="bass"),
    # a sub bass: a clean sine and a trace of its octave, no click
    voice("SUB BASS", 32, [
        op(96, 1, **adsr(SOFT, 3, -2, 0.18)),
        op(68, 2, **adsr(SOFT, 8, -12, 0.15)),
        None, None, None, None], fb=0, role="bass"),
    # a square bass: odd harmonics 1 3 5 7 at 1/n (alg 32), a little feedback for the edge
    voice("SQUARE BS", 32, [
        op(94, 1, **adsr(FAST, 5, -2, 0.1)),
        op(81, 3, **adsr(FAST, 7, -3, 0.1)),
        op(76, 5, **adsr(FAST, 9, -4, 0.1)),
        op(72, 7, **adsr(FAST, 11, -5, 0.1)),
        None,
        op(OLI(0.4), 1, **adsr(FAST, 5, -2, 0.1))], fb=2, role="bass"),
    # a reese-like bass: three carriers tuned apart (+-6) against a lightly modulated middle, they beat slowly
    voice("DETUNE BS", 5, [
        op(90, 1, vel=2, det=-6, **adsr(SOFT, 4, -2, 0.2)),
        op(OLI(1.6), 1, vel=3, **decay(SOFT, 18, -10, 5, 0.2)),
        op(90, 1, vel=2, det=6, **adsr(SOFT, 4, -2, 0.2)),
        op(OLI(1.6), 1, vel=3, det=3, **decay(SOFT, 18, -10, 5, 0.2)),
        op(88, 0.5, vel=2, det=1, **adsr(SOFT, 4, -2, 0.2)),
        op(OLI(1.2), 1, vel=3, **decay(SOFT, 16, -10, 5, 0.2))], fb=3, role="bass"),
    # an acid-ish bass: x2 and x3 modulators that sweep down (index 4 to 0.5 in a quarter second), resonance by feedback
    voice("ACID FM", 3, [
        op(94, 1, vel=2, **adsr(FAST, 8, -2, 0.1)),
        op(OLI(4.0), 2, vel=3, **decay(FAST, 22, -18, 6, 0.1)),
        op(OLI(2.4), 3, vel=3, **decay(FAST, 34, -20, 8, 0.1)),
        op(80, 0.5, vel=2, **adsr(FAST, 8, -4, 0.1)),
        op(OLI(1.5), 1, vel=3, **decay(FAST, 26, -16, 8, 0.1)),
        op(OLI(2.6), 1, vel=3, **decay(FAST, 30, -18, 8, 0.1))], fb=6, role="bass"),
    # a finger-style electric bass: a soft pluck, a round body that rings a second or two
    voice("FINGER BS", 1, [
        op(94, 1, vel=3, rs=1, **decay(FAST, 34, -8, 7, 0.2)),
        op(OLI(1.8), 1, vel=4, **decay(FAST, 50, -26, 12, 0.2)),
        op(80, 2, vel=2, **decay(FAST, 46, -16, 10, 0.2)),
        op(OLI(1.0), 2, vel=3, **decay(FAST, 70, -30, 16, 0.2)),
        op(OLI(0.8), 1, vel=3, **decay(FAST, 50, -26, 12, 0.2)),
        op(OLI(1.0), 1, vel=3, **decay(FAST, 40, -22, 10, 0.2))], fb=2, role="bass"),
    # an upright (pizzicato): a wooden thump from a fast pitch dip, then a pure round tone
    voice("UPRIGHT BS", 5, [
        op(94, 1, vel=3, **decay(FAST, 40, -10, 9, 0.15)),
        op(OLI(1.5), 1, vel=5, **decay(FAST, 90, -30, 18, 0.15)),
        op(84, 2, vel=2, **decay(FAST, 50, -20, 14, 0.12)),
        op(OLI(0.9), 3, vel=5, **perc(160, 0.08)),
        None, None], fb=1, role="bass",
        peg=((99, 70, 99, 99), (50, 50, 50, 56))),
    # a clav bass: the funk clavinet an octave down, hard feedback, short
    voice("CLAV BASS", 5, [
        op(94, 1, vel=4, **decay(FAST, 50, -10, 20, 0.1)),
        op(OLI(3.4), 1, vel=6, **decay(FAST, 90, -28, 24, 0.1)),
        op(88, 2, vel=4, **decay(FAST, 56, -14, 22, 0.1)),
        op(OLI(1.9), 3, vel=6, **perc(120, 0.08)),
        None, None], fb=6, role="bass"),
    # a moog-style bass: a fat sawtooth chain (alg 16: three modulator stacks into OP1), the brightness a sweep
    voice("MOOG BASS", 16, [
        op(92, 1, vel=2, **adsr(FAST, 5, -2, 0.1)),
        op(OLI(2.6), 1, **decay(FAST, 24, -16, 5, 0.1)),
        op(OLI(2.0), 1, **decay(FAST, 20, -14, 5, 0.1)),
        op(OLI(1.4), 2, **decay(FAST, 30, -18, 6, 0.1)),
        op(OLI(1.6), 1, det=2, **decay(FAST, 22, -14, 5, 0.1)),
        op(OLI(2.2), 1, **decay(FAST, 26, -16, 5, 0.1))], fb=5, role="bass"),
    # a wobble bass: the LFO (a triangle, 3 Hz) swings the modulators' amplitude (AMS 3): the timbre breathes in and out
    voice("WOBBLE", 3, [
        op(94, 1, **adsr(FAST, 4, -2, 0.1)),
        op(OLI(3.0), 1, ams=3, **adsr(FAST, 4, -2, 0.1)),
        op(OLI(2.0), 2, ams=3, **adsr(FAST, 4, -2, 0.1)),
        op(86, 0.5, **adsr(FAST, 4, -2, 0.1)),
        op(OLI(1.6), 1, ams=3, **adsr(FAST, 4, -2, 0.1)),
        op(OLI(1.4), 1, **adsr(FAST, 4, -2, 0.1))], fb=3, role="bass",
        lfo=(20, 0, 0, 80, 0, 0)),
    # a pop bass: bright, an x2 modulator and a quick pitch blip down into the note
    voice("POP BASS", 1, [
        op(94, 1, vel=3, **decay(FAST, 26, -6, 9, 0.12)),
        op(OLI(2.8), 2, vel=4, **decay(FAST, 70, -26, 14, 0.12)),
        op(84, 1, vel=2, **decay(FAST, 34, -12, 10, 0.12)),
        op(OLI(1.4), 1, vel=3, **decay(FAST, 60, -24, 12, 0.12)),
        op(OLI(1.0), 1, **decay(FAST, 50, -22, 12, 0.12)),
        op(OLI(1.4), 3, vel=4, **perc(150, 0.08))], fb=3, role="bass",
        peg=((99, 85, 99, 99), (50, 50, 50, 62))),
    # an 808-style boom: a sine that starts a seventh high and drops into the note in a tenth of a second, then rings
    voice("BOOM BASS", 32, [
        op(96, 1, **decay(FAST, 3, -2, 3, 0.25)),
        op(60, 2, **perc(40, 0.15)),
        None, None, None, None], fb=0, role="bass",
        peg=((92, 99, 99, 12), (50, 50, 50, 68))),
    # a rubber bass: an elastic boing, a pitch dip on each note and a wide, soft index
    voice("RUBBER BS", 1, [
        op(94, 1, vel=2, **decay(FAST, 18, -5, 7, 0.2)),
        op(OLI(2.2), 1, vel=3, **decay(FAST, 30, -20, 8, 0.2)),
        op(84, 1, vel=2, det=3, **decay(FAST, 20, -8, 8, 0.2)),
        op(OLI(1.8), 2, vel=3, **decay(FAST, 36, -22, 10, 0.2)),
        op(OLI(1.2), 1, **decay(FAST, 30, -20, 8, 0.2)),
        op(OLI(1.6), 1, **decay(FAST, 26, -18, 8, 0.2))], fb=2, role="bass",
        peg=((99, 72, 99, 99), (50, 50, 50, 41))),
]
