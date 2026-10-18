# SPDX-License-Identifier: GPL-3.0-only
"""The packs think in decibels, seconds and modulation indices; these turn them into DX7 rates and levels (the engine's
measured behaviour: tests/fm6_test.c, and `fm6_pack_check --dump`). A shape is a dict for op(**shape).

  R(dbps)   the rate at which an envelope falls dbps dB a second (rate 50 takes 0.93 s for 60 dB, +6.25 halves it)
  L(db)     the level db dB under the top (0.75 dB a step; below 20 the steps are bigger, so keep it above -44)
  OLI(idx)  the output level of a modulator that peaks at modulation index idx radians (1.02 rad at 70, doubling every 8)
  RT(sec)   the release rate that takes about sec seconds to fall 60 dB
  ATK(ms)   the attack rate that takes about ms milliseconds to the top (rate 80 is 3 ms, 60 52 ms, 40 0.5 s, 20 3.8 s)
"""
import math


def R(dbps):
    t60 = 60.0 / max(dbps, 0.05)
    return max(0, min(99, round(50 - 6.25 * math.log2(t60 / 0.93))))


def RT(sec):
    return R(60.0 / sec)


def L(db):
    return max(0, min(99, round(99 + db / 0.75)))


def OLI(idx):
    return max(0, min(99, round(70 + 8.03 * math.log2(idx / 1.02))))


_ATK = ((99, 1.0), (90, 1.0), (80, 3.0), (70, 17.0), (60, 52.0), (50, 163.0), (40, 525.0), (30, 1509.0), (20, 3824.0), (10, 9000.0))


def ATK(ms):
    """the attack rate that takes about ms milliseconds from the first sound to the top (measured on the engine, from its
    -50 dB start: rate 80 3 ms, 70 17 ms, 60 52 ms, 50 0.16 s, 40 0.5 s, 30 1.5 s, 20 3.8 s)"""
    for (r0, t0), (r1, t1) in zip(_ATK, _ATK[1:]):
        if t0 <= ms <= t1:
            if t1 == t0:
                return r0
            return round(r0 + (r1 - r0) * (math.log(ms) - math.log(t0)) / (math.log(t1) - math.log(t0)))
    return 99 if ms < 1.0 else 10


def adsr(a, d, sl, r):
    """attack rate a to the top, then falling at d dB/s down to sl dB (held while the key is down), release in r seconds
    (a is a rate, see ATK; the decay and the release in dB/s and seconds)"""
    return dict(r=(a, R(d), 99, RT(r)), l=(99, L(sl), L(sl), 0))


def decay(a, d1, db1, d2, rel, top=99):
    """a struck sound: attack rate a, a first fall at d1 dB/s down to db1 dB under the top, then a slower one at d2 dB/s
    that goes on as long as the key is down; release in rel seconds"""
    return dict(r=(a, R(d1), R(d2), RT(rel)), l=(top, L(db1), 0, 0))


def perc(d, rel=0.1, a=99, top=99):
    """one fall from the top at d dB/s, release in rel seconds"""
    return dict(r=(a, R(d), R(d), RT(rel)), l=(top, 0, 0, 0))


def hold(a, rel, top=99):
    """straight on and off: attack rate a, held at the top, release in rel seconds"""
    return dict(r=(a, 99, 99, RT(rel)), l=(top, top, top, 0))


def swell(a, db, rate, rel):
    """attack a to the top, then settling at rate dB/s to db under the top and held there; release in rel seconds"""
    return dict(r=(a, R(rate), 99, RT(rel)), l=(99, L(db), L(db), 0))
