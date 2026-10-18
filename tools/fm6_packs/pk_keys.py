# SPDX-License-Identifier: GPL-3.0-only
"""Pack 1, keys and e-pianos: five electric pianos, three pianos, a clav, a harpsichord, a toy piano, a celesta and
four organs. Programmed from scratch (dx7.py), in dB, seconds and modulation indices (pk_common.py); the level of each
voice is set by levels.json."""
from dx7 import op, voice
from pk_common import ATK, L, OLI, R, RT, adsr, decay, hold, perc, swell

TITLE = "keys and e-pianos"
FAST = ATK(2)

VOICES = [
    # a tine electric piano: three carrier / modulator pairs (alg 5). The x1 modulators bark at the attack (index 3) and
    # mellow as they fade; the x14 modulator of the middle pair is the tine's ping, gone in a tenth of a second
    voice("STAGE EP", 5, [
        op(92, 1, vel=2, rs=1, **decay(FAST, 40, -8, 9, 0.25)),
        op(OLI(3.0), 1, vel=4, rs=1, ksl=(45, 0, 30, 0, 0), **decay(FAST, 30, -20, 6, 0.25)),
        op(88, 1, vel=2, rs=1, det=2, **decay(FAST, 42, -9, 9, 0.25)),
        op(OLI(1.6), 14, vel=5, ksl=(51, 0, 50, 0, 0), **perc(90, 0.1)),
        op(86, 1, vel=2, rs=1, det=-3, **decay(FAST, 38, -8, 8, 0.25)),
        op(OLI(2.4), 1, vel=3, **decay(FAST, 34, -18, 7, 0.25))], fb=3),
    # a warm suitcase piano: soft modulators, a slow tremolo on the carriers (the LFO's amplitude, AMS 2)
    voice("SUITCASE", 5, [
        op(94, 1, vel=2, ams=2, rs=1, **decay(FAST, 36, -8, 8, 0.3)),
        op(OLI(1.8), 1, vel=3, ksl=(45, 0, 30, 0, 0), **decay(FAST, 26, -18, 5, 0.3)),
        op(90, 1, vel=2, ams=2, rs=1, det=3, **decay(FAST, 38, -9, 8, 0.3)),
        op(OLI(0.9), 7, vel=4, ksl=(51, 0, 50, 0, 0), **perc(70, 0.1)),
        op(84, 1, vel=2, ams=2, rs=1, det=-4, **decay(FAST, 34, -8, 7, 0.3)),
        op(OLI(1.4), 1, vel=3, **decay(FAST, 30, -16, 6, 0.3))], fb=2,
        lfo=(27, 0, 0, 22, 4, 0)),
    # a reedy electric piano: modulators an octave up and a hard feedback make it buzz; it dies sooner
    voice("WURLY ED", 5, [
        op(92, 1, vel=3, rs=2, **decay(FAST, 48, -8, 12, 0.2)),
        op(OLI(2.6), 2, vel=5, ksl=(45, 0, 40, 0, 0), **decay(FAST, 36, -22, 9, 0.2)),
        op(88, 1, vel=3, rs=2, det=2, **decay(FAST, 50, -9, 12, 0.2)),
        op(OLI(1.3), 4, vel=5, ksl=(51, 0, 60, 0, 0), **perc(80, 0.1)),
        op(86, 1, vel=3, rs=2, det=-2, **decay(FAST, 46, -8, 11, 0.2)),
        op(OLI(2.0), 1, vel=4, **decay(FAST, 40, -18, 9, 0.2))], fb=5),
    # a bright electric piano with a bell: the modulators at x1 and x7 stay bright longer
    voice("DYNO KEYS", 5, [
        op(90, 1, vel=2, rs=1, **decay(FAST, 34, -8, 8, 0.25)),
        op(OLI(3.6), 1, vel=4, ksl=(48, 0, 24, 0, 0), **decay(FAST, 24, -16, 6, 0.25)),
        op(86, 2, vel=2, rs=1, det=1, **decay(FAST, 36, -10, 8, 0.25)),
        op(OLI(2.0), 7, vel=5, ksl=(50, 0, 40, 0, 0), **decay(FAST, 70, -30, 12, 0.25)),
        op(84, 1, vel=2, rs=1, det=-2, **decay(FAST, 32, -8, 7, 0.25)),
        op(OLI(2.8), 1, vel=3, **decay(FAST, 28, -14, 6, 0.25))], fb=4),
    # a glassy electric piano: pure x1 and x2 carriers, x3 and x7 modulators: thin and crystalline
    voice("GLASS EP", 5, [
        op(90, 1, vel=2, **decay(FAST, 30, -8, 7, 0.4)),
        op(OLI(1.7), 3, vel=4, ksl=(45, 0, 30, 0, 0), **decay(FAST, 26, -20, 5, 0.4)),
        op(82, 2, vel=2, det=2, **decay(FAST, 32, -10, 7, 0.4)),
        op(OLI(1.2), 7, vel=4, ksl=(50, 0, 40, 0, 0), **perc(60, 0.2)),
        op(80, 1, vel=2, det=-2, **decay(FAST, 28, -8, 6, 0.4)),
        op(OLI(0.9), 1, vel=3, **decay(FAST, 24, -16, 5, 0.4))], fb=1),
    # a concert piano: a hammer (x4 and x14, a flick), the strings' x1 and x2 pairs, key scaling and rate scaling so
    # that the top notes are bright but short; velocity does a lot
    voice("FM GRAND", 5, [
        op(92, 1, vel=4, rs=2, ksl=(30, 0, 12, 0, 0), **decay(FAST, 34, -10, 6, 0.3)),
        op(OLI(2.4), 1, vel=6, rs=1, ksl=(40, 0, 50, 0, 0), **decay(FAST, 34, -24, 7, 0.3)),
        op(86, 2, vel=4, rs=2, det=1, **decay(FAST, 40, -14, 7, 0.3)),
        op(OLI(2.0), 4, vel=6, rs=1, ksl=(45, 0, 60, 0, 0), **decay(FAST, 90, -34, 12, 0.3)),
        op(80, 1, vel=4, rs=2, det=-2, **decay(FAST, 32, -10, 6, 0.3)),
        op(OLI(1.4), 14, vel=7, ksl=(51, 0, 70, 0, 0), **perc(110, 0.1))], fb=1),
    # a honky-tonk: the same strings tuned apart (detune both ways on every pair), shorter and brighter
    voice("HONKY", 5, [
        op(80, 1, vel=4, rs=2, det=-5, **decay(FAST, 40, -10, 9, 0.2)),
        op(OLI(2.4), 1, vel=5, rs=1, ksl=(40, 0, 40, 0, 0), **decay(FAST, 34, -22, 8, 0.2)),
        op(78, 1, vel=4, rs=2, det=5, **decay(FAST, 42, -10, 9, 0.2)),
        op(OLI(1.8), 3, vel=6, ksl=(45, 0, 50, 0, 0), **decay(FAST, 80, -32, 14, 0.2)),
        op(92, 1, vel=4, rs=2, det=0, **decay(FAST, 38, -10, 8, 0.2)),
        op(OLI(1.6), 2, vel=5, **decay(FAST, 40, -24, 9, 0.2))], fb=2),
    # an upright: dark, a quick thump, little brightness
    voice("UPRIGHT", 5, [
        op(94, 1, vel=3, rs=2, ksl=(30, 0, 20, 0, 0), **decay(FAST, 38, -10, 7, 0.2)),
        op(OLI(1.1), 1, vel=5, rs=1, ksl=(40, 0, 50, 0, 0), **decay(FAST, 30, -22, 6, 0.2)),
        op(86, 2, vel=3, rs=2, det=1, **decay(FAST, 40, -14, 7, 0.2)),
        op(OLI(0.8), 3, vel=5, ksl=(45, 0, 60, 0, 0), **perc(80, 0.1)),
        op(80, 1, vel=3, rs=2, det=-1, **decay(FAST, 36, -12, 6, 0.2)),
        op(OLI(0.7), 1, vel=4, **decay(FAST, 30, -18, 6, 0.2))], fb=0),
    # a clavinet: a plucked string, hard feedback on the modulator, short and snappy; the pickup's brightness falls fast
    voice("CLAVINET", 5, [
        op(94, 1, vel=4, rs=2, **decay(FAST, 40, -10, 14, 0.12)),
        op(OLI(3.2), 1, vel=6, ksl=(45, 0, 30, 0, 0), **decay(FAST, 70, -26, 16, 0.12)),
        op(90, 2, vel=4, rs=2, **decay(FAST, 44, -12, 15, 0.12)),
        op(OLI(1.8), 3, vel=6, ksl=(48, 0, 40, 0, 0), **perc(90, 0.1)),
        None,
        None], fb=6),
    # a harpsichord: 8-foot and 4-foot courses with a pluck, no touch response, the jacks stop it the moment the key rises
    voice("HARPSICHRD", 5, [
        op(94, 1, vel=0, rs=1, **decay(FAST, 26, -10, 12, 0.04)),
        op(OLI(2.2), 3, vel=1, rs=2, **decay(FAST, 50, -24, 12, 0.04)),
        op(90, 2, vel=0, rs=1, det=2, **decay(FAST, 28, -12, 13, 0.04)),
        op(OLI(1.8), 5, vel=1, **perc(90, 0.04)),
        op(84, 1, vel=0, rs=1, det=-2, **decay(FAST, 26, -10, 12, 0.04)),
        op(OLI(1.2), 1, vel=1, **decay(FAST, 40, -22, 14, 0.04))], fb=2),
    # a toy piano: small tuned bars struck, inharmonic ratios (x4.2, x6.4) that fall off in about half a second
    voice("TOY PIANO", 5, [
        op(94, 2, vel=3, rs=2, **perc(30, 0.15)),
        op(OLI(1.4), 4.2, vel=5, **perc(50, 0.1)),
        op(88, 4, vel=3, rs=2, det=1, **perc(32, 0.15)),
        op(OLI(1.2), 6.4, vel=5, **perc(60, 0.1)),
        op(84, 2, vel=3, rs=2, det=-1, **perc(28, 0.15)),
        op(OLI(0.9), 9.6, vel=5, **perc(70, 0.1))], fb=0),
    # a celesta: x4 carriers with x3.5 and x7 modulators, a long thin ring
    voice("CELESTA", 5, [
        op(92, 4, vel=3, rs=1, **decay(FAST, 26, -10, 8, 0.4)),
        op(OLI(1.3), 3.5, vel=5, **decay(FAST, 40, -26, 8, 0.4)),
        op(86, 1, vel=3, rs=1, **decay(FAST, 24, -8, 7, 0.4)),
        op(OLI(0.8), 7, vel=5, **perc(70, 0.2)),
        None,
        None], fb=0),
    # tonewheel drawbars (alg 32, six sines): 16', 5 1/3', 8', 4', 2 2/3', 2' at 8 8 8 6 4 3; an instant key off
    voice("TONEWHEEL", 32, [
        op(94, 1, **hold(FAST, 0.03)),
        op(96, 0.5, **hold(FAST, 0.03)),
        op(92, 2, **hold(FAST, 0.03)),
        op(80, 3, **hold(FAST, 0.03)),
        op(72, 4, **hold(FAST, 0.03)),
        op(86, 1.5, **hold(FAST, 0.03))], fb=0),
    # the percussive organ: drawbars and a third harmonic that rings at the attack and dies in half a second
    voice("PERC ORGAN", 32, [
        op(94, 1, **hold(FAST, 0.03)),
        op(90, 0.5, **hold(FAST, 0.03)),
        op(80, 2, **hold(FAST, 0.03)),
        op(88, 3, vel=3, **decay(FAST, 90, -60, 20, 0.03)),
        op(70, 1.5, **hold(FAST, 0.03)),
        op(62, 4, **hold(FAST, 0.03))], fb=0),
    # a harmonium / reed organ: a feedback sawtooth (OP6) beside an 8' sine and a 4' reed, a breath of attack
    voice("HARMONIUM", 22, [
        op(88, 1, **hold(ATK(25), 0.15)),
        op(OLI(1.5), 1, **hold(ATK(25), 0.15)),
        op(80, 1, det=3, **hold(ATK(25), 0.15)),
        op(76, 2, det=-3, **hold(ATK(25), 0.15)),
        op(70, 1, det=2, **hold(ATK(25), 0.15)),
        op(OLI(2.6), 1, **hold(ATK(25), 0.15))], fb=6,
        lfo=(32, 55, 5, 0, 4, 2)),
    # an accordion: two reeds against each other (musette, detuned), a sawtooth edge, the bellows a quick swell
    voice("ACCORDION", 22, [
        op(86, 1, det=-5, **hold(ATK(20), 0.1)),
        op(OLI(1.6), 2, **hold(ATK(20), 0.1)),
        op(84, 1, det=5, **hold(ATK(20), 0.1)),
        op(80, 2, det=4, **hold(ATK(20), 0.1)),
        op(72, 1, det=-4, **hold(ATK(20), 0.1)),
        op(OLI(2.4), 1, **hold(ATK(20), 0.1))], fb=5,
        lfo=(30, 70, 3, 0, 4, 2)),
]
