# SPDX-License-Identifier: GPL-3.0-only
"""Pack 5, 80s mega classics: recreations of the sounds that defined 80s records, played on FM: the e-piano, the bass, the
bells, the brass, the marimba, the harmonica, the flute, the clav, the koto, the strings, the voices, the lead, the stab, the
fretless, the sax, the bright piano. Every voice is programmed from scratch from the description of its sound (how its
partials move, how long it rings), in dB, seconds and modulation indices (pk_common.py); none is a copy of a ROM, a
cartridge or a .syx, and none is named as Yamaha named its own. The level of each is set by levels.json."""
from dx7 import op, voice
from pk_common import ATK, L, OLI, R, RT, adsr, decay, hold, perc, swell

TITLE = "80s mega classics"
FAST = ATK(2)

VOICES = [
    # THE e-piano of the decade: a smooth tine tone: x1 carriers (one an octave up, for the shimmer) against x1 modulators
    # that fall from a bark to a whisper, and an x14 bell that is gone in a tenth of a second; a slow chorus in the tuning
    voice("80S EP", 5, [
        op(92, 1, vel=2, **decay(FAST, 36, -8, 7, 0.3)),
        op(OLI(3.2), 1, vel=4, ksl=(45, 0, 36, 0, 0), **decay(FAST, 26, -20, 5, 0.3)),
        op(88, 1, vel=2, det=3, **decay(FAST, 38, -9, 8, 0.3)),
        op(OLI(1.7), 14, vel=5, ksl=(51, 0, 50, 0, 0), **perc(100, 0.1)),
        op(76, 2, vel=2, det=-2, **decay(FAST, 34, -12, 8, 0.3)),
        op(OLI(1.2), 1, vel=3, **decay(FAST, 30, -18, 6, 0.3))], fb=2),
    # THE bass of the decade: a hard, round FM pluck: an x1 stack with a 3.6-radian bite that falls in 80 ms over a body that
    # rings, and a hollow x0.5 under it
    voice("80S BASS", 1, [
        op(94, 1, vel=3, **decay(FAST, 32, -6, 9, 0.12)),
        op(OLI(3.6), 1, vel=5, **decay(FAST, 100, -30, 18, 0.1)),
        op(80, 0.5, vel=2, **decay(FAST, 26, -8, 8, 0.12)),
        op(OLI(2.0), 3, vel=4, **perc(140, 0.08)),
        op(OLI(1.4), 1, **perc(60, 0.1)),
        op(OLI(2.4), 1, vel=3, **decay(FAST, 50, -24, 12, 0.1))], fb=3, role="bass"),
    # a tubular bell: x1 carrier, the x3.5 modulator itself modulated by an x1 (a three-deep chain, alg 3: OP3 -> OP2 -> OP1), a
    # second stack at x2.76 with a hard feedback for the shimmer
    voice("TUBE BELL", 3, [
        op(92, 1, vel=3, **decay(FAST, 10, -4, 3, 1.0)),
        op(OLI(2.4), 3.5, vel=4, **decay(FAST, 20, -22, 7, 1.0)),
        op(OLI(1.2), 1, vel=4, **decay(FAST, 34, -26, 10, 0.8)),
        op(82, 2.76, vel=2, **decay(FAST, 14, -8, 5, 0.8)),
        op(OLI(1.6), 5.4, vel=3, **decay(FAST, 26, -24, 9, 0.8)),
        op(OLI(1.0), 1.5, vel=3, **decay(FAST, 40, -28, 12, 0.7))], fb=3, role="melody"),
    # the big brass of the decade (alg 22): a feedback sawtooth into three carriers and an x1 pair, a lip scoop up into the note
    voice("BRASS 80S", 22, [
        op(88, 1, vel=2, **adsr(ATK(50), 3, -2, 0.18)),
        op(OLI(2.0), 1, vel=3, r=(ATK(70), R(16), 99, RT(0.18)), l=(99, L(-8), L(-8), 0)),
        op(76, 1, det=-4, vel=2, **adsr(ATK(50), 3, -2, 0.18)),
        op(76, 1, det=4, vel=2, **adsr(ATK(50), 3, -2, 0.18)),
        op(88, 1, vel=2, **adsr(ATK(50), 3, -2, 0.18)),
        op(OLI(2.6), 1, vel=3, r=(ATK(85), R(18), 99, RT(0.18)), l=(99, L(-9), L(-9), 0))], fb=5, role="comp",
        peg=((72, 99, 99, 99), (50, 50, 50, 47)), lfo=(32, 70, 4, 0, 4, 2)),
    # a marimba: a bright, wooden bar: its fundamental, the bar's fourth-partial ring (x4 stack) and a click of x10 that is gone at once
    voice("MARIMBA 80", 5, [
        op(94, 1, vel=4, **decay(FAST, 32, -6, 16, 0.2)),
        op(OLI(1.7), 1, vel=5, **perc(100, 0.1)),
        op(84, 4, vel=3, **perc(70, 0.1)),
        op(OLI(1.1), 4, vel=5, **perc(160, 0.06)),
        op(70, 1, **decay(FAST, 40, -10, 20, 0.15)),
        op(OLI(0.9), 10, vel=5, **perc(260, 0.04))], fb=0, role="melody"),
    # a harmonica: a free reed (alg 22, OP6's feedback sawtooth into x1 and x2 carriers), a breathy front and a hand-wobble tremolo
    voice("HARMONICA", 22, [
        op(90, 1, **adsr(ATK(30), 3, -2, 0.1)),
        op(OLI(1.4), 2, **adsr(ATK(30), 3, -3, 0.1)),
        op(88, 1, det=4, **adsr(ATK(34), 3, -2, 0.1)),
        op(80, 2, det=-3, **adsr(ATK(34), 3, -3, 0.1)),
        op(72, 1, **adsr(ATK(36), 3, -3, 0.1)),
        op(OLI(2.4), 1, **adsr(ATK(36), 4, -5, 0.1))], fb=5, role="melody",
        lfo=(40, 40, 3, 30, 4, 3)),
    # a flute of the decade: a clean tone with a chiff (an x5 modulator, index 1.4, over in 120 ms) and a vibrato that opens late
    voice("BREATH FL", 5, [
        op(92, 1, **adsr(ATK(45), 4, -2, 0.15)),
        op(OLI(0.6), 1, **adsr(ATK(45), 4, -2, 0.15)),
        op(66, 2, **adsr(ATK(70), 4, -4, 0.15)),
        op(OLI(1.4), 5, **decay(ATK(8), 60, -30, 60, 0.1)),
        None, None], fb=3, role="melody",
        lfo=(32, 80, 4, 0, 4, 2)),
    # a funky clavinet: the pickup's pluck: x1 and x2 stacks with a very hard feedback; the wah is the modulators' fall
    voice("FUNK CLAV", 5, [
        op(94, 1, vel=4, rs=2, **decay(FAST, 56, -10, 20, 0.08)),
        op(OLI(3.8), 1, vel=6, **decay(FAST, 90, -28, 26, 0.08)),
        op(90, 2, vel=4, rs=2, **decay(FAST, 60, -12, 22, 0.08)),
        op(OLI(2.4), 3, vel=6, **perc(130, 0.06)),
        op(70, 4, vel=3, **perc(80, 0.06)),
        op(OLI(1.6), 5, vel=6, **perc(200, 0.05))], fb=7, role="comp"),
    # a koto: a plucked string that bends up into the note from just under it, bright x3 / x5 overtones falling fast
    voice("FM KOTO", 5, [
        op(94, 1, vel=3, **decay(FAST, 22, -6, 10, 0.3)),
        op(OLI(2.6), 3, vel=5, **decay(FAST, 70, -28, 18, 0.25)),
        op(84, 2, vel=2, **decay(FAST, 30, -10, 12, 0.25)),
        op(OLI(1.6), 5, vel=5, **perc(120, 0.15)),
        None, None], fb=1, role="melody",
        peg=((62, 99, 99, 99), (50, 50, 50, 43))),
    # an ensemble of strings (alg 22): a sawtooth into carriers a few cents apart, an octave pair on top for the air, a slow bow
    voice("STRINGS 80", 22, [
        op(88, 1, **swell(ATK(400), -2, 5, 0.7)),
        op(OLI(1.6), 2, **swell(ATK(450), -4, 6, 0.7)),
        op(76, 1, det=-5, **swell(ATK(420), -2, 5, 0.7)),
        op(74, 1, det=5, **swell(ATK(440), -2, 5, 0.7)),
        op(80, 2, det=2, **swell(ATK(460), -4, 5, 0.7)),
        op(OLI(2.4), 1, **swell(ATK(480), -6, 6, 0.7))], fb=6, role="held",
        lfo=(30, 80, 5, 0, 4, 2)),
    # an angelic choir: six harmonics (alg 32) shaped round an "ooh" (the fundamental and the 2nd strong, the rest falling away),
    # a slow bloom and a gentle vibrato, a little breath in a feedback partial
    voice("ANGEL VOX", 32, [
        op(94, 1, **swell(ATK(500), -1, 6, 1.0)),
        op(88, 2, **swell(ATK(550), -1, 6, 1.0)),
        op(80, 3, **swell(ATK(600), -1, 6, 1.0)),
        op(72, 4, **swell(ATK(650), -1, 6, 1.0)),
        op(60, 5, **swell(ATK(700), -1, 6, 1.0)),
        op(52, 7, **swell(ATK(700), -1, 6, 1.0))], fb=2, role="held",
        lfo=(26, 70, 5, 0, 4, 2)),
    # the lead of the decade (alg 18: three modulators on one carrier): shrill, a wide index (5 radians) that eases to 3, a vibrato
    voice("LEAD 80", 18, [
        op(92, 1, **adsr(ATK(12), 3, -2, 0.15)),
        op(OLI(5.0), 1, **decay(ATK(10), 14, -8, 3, 0.15)),
        op(OLI(3.0), 2, **decay(ATK(10), 16, -10, 4, 0.15)),
        op(OLI(2.2), 3, **decay(ATK(10), 18, -10, 4, 0.15)),
        op(OLI(1.4), 1, **adsr(ATK(10), 3, -2, 0.15)),
        op(OLI(1.0), 1, **adsr(ATK(10), 3, -2, 0.15))], fb=5, role="melody",
        lfo=(34, 60, 6, 0, 4, 2)),
    # a synth stab: a short, punchy, brassy chord hit; octave carriers over a stack, down to a quarter in 0.2 s
    voice("FM STAB", 5, [
        op(92, 1, vel=2, **decay(ATK(6), 20, -10, 6, 0.1)),
        op(OLI(2.8), 1, vel=3, **decay(ATK(10), 40, -26, 12, 0.1)),
        op(90, 1, det=4, vel=2, **decay(ATK(6), 22, -10, 7, 0.1)),
        op(OLI(2.4), 2, vel=3, **decay(ATK(10), 44, -26, 12, 0.1)),
        op(84, 2, det=-3, vel=2, **decay(ATK(6), 24, -12, 8, 0.1)),
        op(OLI(2.0), 1, vel=3, **decay(ATK(10), 36, -22, 10, 0.1))], fb=4, role="comp"),
    # a fretless bass: round and singing: a mwah bloom, a quick x7 buzz of the string on the fingerboard, a vibrato late
    voice("FRETLESS80", 1, [
        op(94, 1, vel=3, **decay(FAST, 16, -4, 5, 0.2)),
        op(OLI(2.2), 1, vel=4, r=(ATK(70), R(24), R(8), RT(0.2)), l=(99, L(-12), L(-20), 0)),
        op(82, 2, vel=2, **decay(FAST, 22, -10, 7, 0.2)),
        op(OLI(1.0), 3, vel=4, **decay(FAST, 50, -30, 14, 0.15)),
        op(OLI(0.8), 7, vel=5, **perc(150, 0.08)),
        op(OLI(1.0), 1, vel=3, **decay(FAST, 30, -20, 10, 0.15))], fb=3, role="bass",
        lfo=(28, 70, 6, 0, 4, 2)),
    # a saxophone: a reed with a growl: x1 and x2 modulators on an x1 carrier (all the harmonics), a throaty feedback, a late vibrato
    voice("SMOOTH SAX", 5, [
        op(92, 1, vel=2, **adsr(ATK(32), 4, -2, 0.12)),
        op(OLI(2.2), 1, vel=3, r=(ATK(40), R(16), 99, RT(0.12)), l=(99, L(-6), L(-6), 0)),
        op(84, 1, det=3, vel=2, **adsr(ATK(34), 4, -3, 0.12)),
        op(OLI(1.4), 2, vel=3, r=(ATK(44), R(20), 99, RT(0.12)), l=(99, L(-8), L(-8), 0)),
        op(70, 2, **adsr(ATK(40), 4, -4, 0.12)),
        op(OLI(1.2), 1, **adsr(ATK(44), 4, -6, 0.12))], fb=4, role="melody",
        lfo=(32, 80, 5, 0, 4, 2)),
    # the bright electric piano: a glassy attack: x1 carriers with x7 and x14 bells and a x3 sparkle, the body falls gently
    voice("BRIGHT EP", 5, [
        op(90, 1, vel=2, **decay(FAST, 34, -8, 8, 0.25)),
        op(OLI(3.8), 1, vel=4, ksl=(48, 0, 26, 0, 0), **decay(FAST, 22, -14, 6, 0.25)),
        op(86, 1, det=2, vel=2, **decay(FAST, 36, -9, 8, 0.25)),
        op(OLI(2.2), 7, vel=5, ksl=(50, 0, 40, 0, 0), **decay(FAST, 60, -28, 14, 0.2)),
        op(80, 2, det=-3, vel=2, **decay(FAST, 32, -10, 8, 0.25)),
        op(OLI(1.8), 14, vel=5, ksl=(51, 0, 50, 0, 0), **perc(100, 0.1))], fb=3, role="comp"),
]
