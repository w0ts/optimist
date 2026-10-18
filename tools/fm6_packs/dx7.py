# SPDX-License-Identifier: GPL-3.0-only
"""A small language for writing DX7 voices (FM6's packs, tools/gen_fm6_packs.py) and the C they become.

Every voice in the packs is written here, from scratch: no byte of it is copied from a Yamaha ROM or cartridge, or from
anyone else's .syx. A voice is six operators (OP1 first), an algorithm, feedback, a pitch envelope and an LFO:

  op(ol, f=1.0, r=(..4..), l=(..4..), vel=0, ams=0, rs=0, det=0, ksl=None, fixed=None)
      ol      output level 0..99 (a carrier: its loudness, 0.75 dB a step above 20; a modulator: its index, about
              1.02 rad at 70, 2.3 at 80, 6.6 at 90, 12 at 99, as the top of its envelope)
      f       frequency ratio (0.5, 1, 2, 3.5 ...): becomes the DX7's coarse and fine; fixed=Hz plays that frequency
              on every key (only 1 Hz .. 9.77 kHz in the DX7's 10^(decade + fine / 100) steps)
      r, l    envelope rates R1..R4 and levels L1..L4 (the envelope starts at L4, goes to L1 at R1, to L2 at R2, to L3 at
              R3 and holds it while the key is down; the key up goes to L4 at R4). Rate 50 takes about 0.9 s for 60 dB,
              every 6 steps double or halve that
      vel     velocity sensitivity 0..7, ams amplitude-modulation sensitivity 0..3, rs key rate scaling 0..7
      det     detune -7..7
      ksl     key level scaling (break point 0..99 = A-1 .. C8, left depth, right depth, left curve, right curve; the
              curves 0 -LIN, 1 -EXP, 2 +EXP, 3 +LIN)
  voice(name, alg, ops, fb=0, peg=None, lfo=None, oks=1, trnsp=0, role='comp')
      alg     the algorithm 1..32 (DX7 numbering), ops the six operators OP1..OP6 (None: switched off, level 0)
      fb      feedback 0..7 (on the algorithm's feedback operator)
      peg     pitch envelope ((r1..r4), (l1..l4)), levels 50 = no change, 1 step 1/32 octave around 50..., None: flat
      lfo     (speed, delay, pmd, amd, wave, pms) wave 0 TRI 1 SAW DOWN 2 SAW UP 3 SQUARE 4 SINE 5 S&H; None: none
      trnsp   semitones -24..24 (the note the voice sounds is transposed; 0 = none)
      role    which phrase sets its level: 'bass', 'held', 'comp' or 'melody' (tests/fm6_pack_check.c)
"""
from dataclasses import dataclass, field, replace
from typing import Optional, Tuple

# the algorithms: the carriers (operators that go to the output), 1-based, by algorithm number
ALG_CARRIERS = {
    1: (1, 3), 2: (1, 3), 3: (1, 4), 4: (1, 4), 5: (1, 3, 5), 6: (1, 3, 5), 7: (1, 3), 8: (1, 3), 9: (1, 3), 10: (1, 4),
    11: (1, 4), 12: (1, 3), 13: (1, 3), 14: (1, 3), 15: (1, 3), 16: (1,), 17: (1,), 18: (1,), 19: (1, 4, 5), 20: (1, 2, 4),
    21: (1, 2, 4, 5), 22: (1, 3, 4, 5), 23: (1, 2, 4, 5), 24: (1, 2, 3, 4, 5), 25: (1, 2, 3, 4, 5), 26: (1, 2, 4),
    27: (1, 2, 4), 28: (1, 3, 6), 29: (1, 2, 3, 5), 30: (1, 2, 3, 6), 31: (1, 2, 3, 4, 5), 32: (1, 2, 3, 4, 5, 6),
}
# the operator that feeds back in each algorithm
ALG_FB = {1: 6, 2: 2, 3: 6, 4: 6, 5: 6, 6: 6, 7: 6, 8: 4, 9: 2, 10: 3, 11: 6, 12: 2, 13: 6, 14: 6, 15: 2, 16: 6, 17: 2, 18: 3,
          19: 6, 20: 3, 21: 3, 22: 6, 23: 6, 24: 6, 25: 6, 26: 6, 27: 3, 28: 5, 29: 6, 30: 5, 31: 6, 32: 6}


@dataclass(frozen=True)
class Op:
    ol: int = 0
    f: float = 1.0
    r: Tuple[int, int, int, int] = (99, 99, 99, 99)
    l: Tuple[int, int, int, int] = (99, 99, 99, 0)
    vel: int = 0
    ams: int = 0
    rs: int = 0
    det: int = 0
    ksl: Optional[Tuple[int, int, int, int, int]] = None
    fixed: Optional[float] = None

    def coarse_fine(self):
        if self.fixed is not None:                 # 10^(coarse + fine / 100) Hz, coarse 0..3 (the DX7 shows 1 Hz .. 1 kHz)
            import math
            x = math.log10(self.fixed)
            c = max(0, min(3, int(x)))
            return c, max(0, min(99, round((x - c) * 100)))
        f = self.f
        if f < 1.0:
            return 0, max(0, min(99, round((f / 0.5 - 1) * 100)))
        c = int(f)
        return c, max(0, min(99, round((f / c - 1) * 100)))

    def check(self, where):
        assert 0 <= self.ol <= 99, (where, 'ol', self.ol)
        assert all(0 <= x <= 99 for x in self.r + self.l), (where, 'env', self.r, self.l)
        assert 0 <= self.vel <= 7 and 0 <= self.ams <= 3 and 0 <= self.rs <= 7 and -7 <= self.det <= 7, (where, 'sens')
        c, fi = self.coarse_fine()
        assert 0 <= c <= 31 and 0 <= fi <= 99, (where, 'freq')


def op(ol, f=1.0, r=(99, 99, 99, 99), l=(99, 99, 99, 0), vel=0, ams=0, rs=0, det=0, ksl=None, fixed=None):
    return Op(ol, f, tuple(r), tuple(l), vel, ams, rs, det, tuple(ksl) if ksl else None, fixed)


OFF = None


@dataclass(frozen=True)
class Voice:
    name: str
    alg: int
    ops: tuple
    fb: int = 0
    peg: Optional[tuple] = None
    lfo: Optional[tuple] = None
    oks: int = 1
    trnsp: int = 0
    role: str = 'comp'
    trim: int = 0                    # output level steps added to every carrier (levels.json: tools/fm6_packs_check.py)

    def check(self):
        assert 1 <= len(self.name) <= 10 and self.name == self.name.upper(), self.name
        assert all(32 <= ord(c) < 127 for c in self.name), self.name
        assert 1 <= self.alg <= 32 and len(self.ops) == 6 and 0 <= self.fb <= 7, self.name
        assert self.role in ('bass', 'held', 'comp', 'melody'), self.name
        for i, o in enumerate(self.ops):
            if o is not None:
                o.check((self.name, i + 1))
        assert -24 <= self.trnsp <= 24, self.name
        if self.peg:
            assert len(self.peg[0]) == 4 and len(self.peg[1]) == 4 and all(0 <= x <= 99 for x in self.peg[0] + self.peg[1])
        if self.lfo:
            assert len(self.lfo) == 6 and all(0 <= x <= 99 for x in self.lfo[:4]) and 0 <= self.lfo[4] <= 5 and 0 <= self.lfo[5] <= 7

    def carriers(self):
        return ALG_CARRIERS[self.alg]


def voice(name, alg, ops, fb=0, peg=None, lfo=None, oks=1, trnsp=0, role='comp'):
    v = Voice(name, alg, tuple(ops), fb, peg, lfo, oks, trnsp, role)
    v.check()
    return v


def with_trim(v, trim):
    return replace(v, trim=trim)


KSL_NONE = (39, 0, 0, 0, 0)


def c_op(o, carrier, trim):
    """one operator as the OP / OPX macro of eng_fm6_rom.h"""
    if o is None:
        return 'OP_OFF'
    c, fi = o.coarse_fine()
    ol = o.ol + (trim if carrier else 0)
    ol = max(0, min(99, ol))
    bp, ld, rd, lc, rc = o.ksl or KSL_NONE
    macro = 'OPX' if o.fixed is not None else 'OP'
    return (f'{macro}({o.r[0]}, {o.r[1]}, {o.r[2]}, {o.r[3]}, {o.l[0]}, {o.l[1]}, {o.l[2]}, {o.l[3]}, {bp}, {ld}, {rd}, {lc}, {rc}, '
            f'{o.rs}, {o.ams}, {o.vel}, {ol}, {c}, {fi}, {o.det})')


def c_voice(v):
    """the voice as C: VOICE(OP1 .. OP6, PEG, alg, fb, oks, LFO, name)"""
    car = set(v.carriers())
    ops = [c_op(o, (i + 1) in car, v.trim) for i, o in enumerate(v.ops)]
    pr, pl = v.peg if v.peg else ((99, 99, 99, 99), (50, 50, 50, 50))
    peg = f'PEG({pr[0]}, {pr[1]}, {pr[2]}, {pr[3]}, {pl[0]}, {pl[1]}, {pl[2]}, {pl[3]})'
    if v.lfo:
        sp, dl, pmd, amd, wave, pms = v.lfo
        lfo = f'LFO({sp}, {dl}, {pmd}, {amd}, 1, {wave}, {pms})'
    else:
        lfo = 'NO_LFO'
    return 'VOICE(' + ',\n          '.join(ops) + f',\n          {peg}, {v.alg}, {v.fb}, {v.oks}, {lfo}, "{v.name:<10}")'


def pack_vmem(v):
    """the voice as a DX7 VMEM voice (128 bytes), for the checks"""
    car = set(v.carriers())
    b = bytearray(128)
    for n in range(6):                       # OP6 first
        o = v.ops[5 - n]
        base = n * 17
        if o is None:
            vals = [99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0]
            ol, c, fi, mode, dt, rs, ams, vel = 0, 1, 0, 0, 0, 0, 0, 0
            r, l, ksl = (99, 99, 99, 99), (99, 99, 99, 0), KSL_NONE
        else:
            c, fi = o.coarse_fine()
            ol = max(0, min(99, o.ol + (v.trim if (6 - n) in car else 0)))
            mode, dt, rs, ams, vel = (1 if o.fixed is not None else 0), o.det, o.rs, o.ams, o.vel
            r, l, ksl = o.r, o.l, o.ksl or KSL_NONE
        bp, ld, rd, lc, rc = ksl
        b[base:base + 4] = bytes(r)
        b[base + 4:base + 8] = bytes(l)
        b[base + 8], b[base + 9], b[base + 10] = bp, ld, rd
        b[base + 11] = lc | rc << 2
        b[base + 12] = rs | (dt + 7) << 3
        b[base + 13] = ams | vel << 2
        b[base + 14] = ol
        b[base + 15] = mode | c << 1
        b[base + 16] = fi
    pr, pl = v.peg if v.peg else ((99, 99, 99, 99), (50, 50, 50, 50))
    b[102:106] = bytes(pr)
    b[106:110] = bytes(pl)
    b[110] = v.alg - 1
    b[111] = v.fb | v.oks << 3
    sp, dl, pmd, amd, wave, pms = v.lfo if v.lfo else (35, 0, 0, 0, 0, 0)
    b[112:116] = bytes((sp, dl, pmd, amd))
    b[116] = 1 | wave << 1 | pms << 4                    # (LFO key sync on: each note restarts it)
    b[117] = v.trnsp + 24
    b[118:128] = f'{v.name:<10}'.encode('ascii')
    return bytes(b)
