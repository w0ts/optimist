# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 the Optimist contributors
"""The drawings of Optimist's parameter icons, one per name of assets/icons.json (tools/draw_icons.py writes
the atlas). Each is a function of a draw_icons.Canvas: lines, polylines, sampled curves, rings, or a bitmap
typed as text ('.' 0, ':' 1, '+' 2, '#' 3 = no ink, a third, two thirds, full ink). Drawn from scratch,
clean room (see tools/draw_icons.py)."""
import math

ICONS = {}


def icon(*names):
    def reg(fn):
        for n in names:
            ICONS[n] = fn
        return fn
    return reg


def grid_icon(name, text):
    ICONS[name] = lambda c: c.grid(text)


# ---------------------------------------------------------------- shared drawings
TAU = 2 * math.pi
WAVE_X0, WAVE_X1, WAVE_YC, WAVE_AMP = 1, 10, 5.5, 3.5     # the waveform frame: x 1..10, y 2..9


def sine(t):
    return math.sin(TAU * t)


ENV = [(1, 10), (3, 2), (5, 6), (8, 6), (10, 10)]          # attack, decay, sustain, release


def envelope(c, lit):
    """the ADSR, stage `lit` (0..3, or None: all) in full ink, the rest in a third"""
    for i, (a, b) in enumerate(zip(ENV, ENV[1:])):
        if lit is not None and i != lit:
            c.line(*a, *b, v=1)
    for i, (a, b) in enumerate(zip(ENV, ENV[1:])):
        if lit is None or i == lit:
            c.line(*a, *b, v=3)


def note(c, x, y, v=3, stem=True, flag=True):
    """a note head 4 x 3 with its top left at (x, y); the stem rises 6 px from its right edge"""
    c.grid(".###\n####\n###.".replace("#", "#:+#"[v]), x, y)
    if stem:
        c.line(x + 3, y, x + 3, y - 6, v)
        if flag:
            c.line(x + 3, y - 6, x + 5, y - 4, v)
            c.put(x + 5, y - 3, v)


def lowpass(c, peak):
    """the filter response: flat, then falling past the cutoff, the band it passes shaded in a third;
    `peak` adds the resonance"""
    if peak:
        top = [6, 6, 6, 5, 3, 2, 3, 6, 10, 10]             # rows of the curve at x 1..10
    else:
        top = [3, 3, 3, 3, 3, 4, 5, 7, 10, 10]
    for x, y in zip(range(1, 11), top):
        if y < 10:
            c.line(x, y + 1, x, 10, 1)
    c.poly([(x, y) for x, y in zip(range(1, 10), top)])
    c.line(1, 10, 10, 10, 1)


def keyboard(c, y0, y1):
    """three white keys (x 1..10) and two black ones, rows y0..y1"""
    c.rect(1, y0, 10, y1)
    black = y0 + (y1 - y0) // 2                     # the black keys' last row
    for x in (4, 7):
        c.line(x, black + 1, x, y1)
    for x in (3, 7):
        c.fill(x, y0, x + 1, black)


def tray(c):
    c.poly([(1, 7), (1, 10), (10, 10), (10, 7)])


def sample_blob(c, v=3):
    h = [1, 4, 3, 4, 2, 3, 2, 2, 1, 1]                      # half heights of the columns x 1..10
    for i, hh in enumerate(h):
        c.line(1 + i, 6 - hh, 1 + i, 5 + hh, v)


# ---------------------------------------------------------------- the icons
@icon("generic")
def _generic(c):                                       # a knob
    c.ring(1, 1, 10)
    c.line(5, 6, 8, 3)


@icon("attack")
def _attack(c):
    envelope(c, 0)


@icon("decay")
def _decay(c):
    envelope(c, 1)


@icon("sustain")
def _sustain(c):
    envelope(c, 2)


@icon("release")
def _release(c):
    envelope(c, 3)


@icon("env")
def _env(c):
    envelope(c, None)


@icon("cutoff")
def _cutoff(c):
    lowpass(c, False)


@icon("reso")
def _reso(c):
    lowpass(c, True)


@icon("keytrack")
def _keytrack(c):                                      # the cutoff follows the keys
    keyboard(c, 5, 10)
    c.line(1, 3, 9, 1, 2)
    c.put(8, 0, 2)
    c.put(8, 2, 2)


@icon("rate")
def _rate(c):                                          # a speed dial
    c.grid("""
        ............
        ............
        ............
        ....####....
        ..##....##..
        ..#......#..
        .#.....#..#.
        .#....#...#.
        .#...##...#.
        .....##.....
        ............
        ............""")


@icon("lfo_wave")
def _lfo_wave(c):                                      # a slow wave about its axis
    c.dotted(1, 6, 10, 6, v=1)
    c.curve(sine, WAVE_X0, WAVE_X1, 5.5, 3.5)


@icon("wave")
def _wave(c):                                          # a sine that turns square: the oscillator's shape
    c.curve(lambda t: math.sin(math.pi * t), 1, 6, 6, 4)
    c.poly([(6, 6), (6, 9), (10, 9), (10, 6)])


@icon("pulse")
def _pulse(c):                                         # a narrow pulse and its width
    c.poly([(1, 8), (3, 8), (3, 2), (6, 2), (6, 8), (10, 8)])
    c.line(3, 10, 6, 10, 2)


@icon("shape")
def _shape(c):                                         # a line bent into a curve
    c.line(1, 10, 10, 1, 1)
    c.curve(lambda t: t ** 2.6, 1, 10, 10, 9)


@icon("pitch")
def _pitch(c):
    note(c, 3, 7)


@icon("tune")
def _tune(c):                                          # a tuning fork
    c.grid("""
        ............
        ...#....#...
        ...#....#...
        ...#....#...
        ...#....#...
        ...#....#...
        ...:#..#:...
        ....:##:....
        .....##.....
        .....##.....
        .....##.....
        ............""")


@icon("detune")
def _detune(c):                                        # two waves a little apart in pitch
    c.curve(lambda t: math.sin(TAU * 1.3 * t), WAVE_X0, WAVE_X1, WAVE_YC, WAVE_AMP, v=1)
    c.curve(sine, WAVE_X0, WAVE_X1, WAVE_YC, WAVE_AMP)


@icon("transpose")
def _transpose(c):                                     # a note moved up or down
    note(c, 1, 7, flag=False)
    c.line(8, 1, 8, 4)
    c.arrow_head(8, 1, "u", size=1)
    c.line(8, 7, 8, 10)
    c.arrow_head(8, 10, "d", size=1)


@icon("octave")
def _octave(c):                                        # the same note, higher
    note(c, 1, 8, flag=False)
    note(c, 6, 4, flag=False)


@icon("level")
def _level(c):
    c.fill(2, 8, 3, 10)
    c.fill(5, 5, 6, 10)
    c.fill(8, 2, 9, 10)


@icon("pan")
def _pan(c):
    c.line(1, 6, 10, 6, 1)
    c.arrow_head(1, 6, "l", size=1)
    c.arrow_head(10, 6, "r", size=1)
    c.fill(5, 3, 6, 9)


grid_icon("mute", """
    ............
    ............
    .....#......
    ....##......
    .###.#.#..#.
    .#.#.#..##..
    .#.#.#..##..
    .###.#.#..#.
    ....##......
    .....#......
    ............
    ............""")


@icon("dist")
def _dist(c):                                          # a clipped sine
    c.curve(lambda t: max(-1.0, min(1.0, 1.9 * sine(t))), WAVE_X0, WAVE_X1, WAVE_YC, WAVE_AMP)


@icon("drive")
def _drive(c):                                         # an amplifier
    c.poly([(2, 1), (2, 10), (10, 6), (10, 5), (2, 1)])
    c.fill(3, 4, 4, 7, 1)
    c.line(5, 5, 5, 6, 1)


@icon("fold")
def _fold(c):                                          # a sine folded back at the top and bottom
    def f(t):
        s = 1.7 * sine(t)
        return 2 - s if s > 1 else (-2 - s if s < -1 else s)
    c.curve(f, WAVE_X0, WAVE_X1, WAVE_YC, WAVE_AMP)


@icon("bits")
def _bits(c):                                          # a sine in a few steps
    ys = [6 - 2 * round(2 * sine((2 * k + 1) / 10)) for k in range(5)]      # 5 levels, 2 px apart
    pts = [(1, ys[0])]
    for k, y in enumerate(ys):
        x1 = min(3 + 2 * k, 10)
        pts += [(pts[-1][0], y), (x1, y)]
    c.poly(pts)


@icon("chorus")
def _chorus(c):                                        # one wave and its copy, a little later
    c.curve(sine, 1, 10, 3, 2)
    c.curve(lambda t: sine(t - 0.12), 1, 10, 8, 2, 2)


@icon("delay")
def _delay(c):                                         # an impulse and its echoes
    c.line(1, 2, 1, 10)
    c.line(4, 4, 4, 10)
    c.line(7, 6, 7, 10, 2)
    c.line(10, 8, 10, 10, 1)


@icon("reverb")
def _reverb(c):                                        # an impulse and its tail
    c.line(1, 2, 1, 10)
    for x in range(3, 11):
        top = round(10 - 7 * math.exp(-(x - 3) / 2.6))
        c.line(x, top, x, 10, 1)
        c.put(x, top, 3 if x < 7 else 2)


@icon("feedback")
def _feedback(c):                                      # the output fed back to the input
    c.line(1, 8, 10, 8)
    c.arrow_head(10, 8, "r", size=1)
    c.poly([(7, 8), (7, 3), (3, 3), (3, 6)], 2)
    c.arrow_head(3, 6, "d", v=2, size=1)


@icon("arp")
def _arp(c):                                           # notes played one after another, upwards
    c.poly([(2, 8), (5, 5), (9, 2)], 1)
    for x, y in ((1, 8), (5, 5), (9, 2)):
        c.fill(x, y, x + 1, y + 1)


@icon("gate")
def _gate(c):                                          # the note held open
    c.fill(4, 4, 7, 8, 1)
    c.poly([(1, 9), (3, 9), (3, 3), (8, 3), (8, 9), (10, 9)])


@icon("swing")
def _swing(c):                                         # steps on the beat and late
    for x in (1, 4, 7, 10):
        c.line(x, 1, x, 10, 1)
    for x in (1, 5, 7):
        c.fill(x, 5, x + 1, 6)


@icon("tempo")
def _tempo(c):                                         # a metronome
    c.poly([(2, 10), (9, 10), (7, 1), (4, 1), (2, 10)])
    c.line(5, 8, 9, 2)
    c.fill(7, 4, 8, 4, 2)


@icon("scale")
def _scale(c):                                         # keys
    keyboard(c, 1, 10)


@icon("quantize")
def _quantize(c):                                      # a line snapped to steps
    c.line(1, 10, 10, 1, 1)
    c.poly([(1, 10), (1, 7), (4, 7), (4, 4), (7, 4), (7, 1), (10, 1)])


@icon("glide")
def _glide(c):                                         # one note gliding to the next
    c.line(1, 9, 3, 9)
    c.curve(lambda t: (1 - math.cos(math.pi * t)) / 2, 3, 8, 9, 7)
    c.line(8, 2, 10, 2)


@icon("slide")
def _slide(c):                                         # two notes tied by a slide
    c.fill(1, 8, 4, 9)
    c.fill(7, 2, 10, 3)
    c.line(4, 8, 7, 3, 2)


@icon("voice")
def _voice(c):                                         # a chord: several voices on one stem
    for y in (0, 4, 8):
        c.grid(".###.\n#####\n.###.", 2, y)
    c.line(7, 0, 7, 9)


@icon("mod")
def _mod(c):                                           # a wave acting on a value
    c.curve(sine, 1, 10, 3, 2)
    c.line(5, 6, 5, 10, 2)
    c.arrow_head(5, 10, "d", v=2, size=2)


@icon("noise")
def _noise(c):
    ys = [6, 2, 8, 4, 9, 3, 7, 5, 2, 8]
    for k in range(9):
        c.line(1 + k, ys[k], 2 + k, ys[k + 1])


@icon("sub")
def _sub(c):                                           # a square and the one an octave below
    c.poly([(1, 4), (1, 2), (3, 2), (3, 4), (5, 4), (5, 2), (7, 2), (7, 4), (9, 4), (9, 2), (10, 2)], 2)
    c.poly([(1, 10), (1, 6), (5, 6), (5, 10), (9, 10), (9, 6), (10, 6)])


@icon("ratio")
def _ratio(c):                                         # a small wheel against a large one
    c.ring(1, 4, 4)
    c.ring(5, 3, 6)


grid_icon("algorithm", """
    ............
    ....####....
    ....#..#....
    ....####....
    ....+..+....
    ...+....+...
    ..+......+..
    .####..####.
    .#..#..#..#.
    .####..####.
    ............
    ............""")


@icon("sample")
def _sample(c):
    sample_blob(c)


@icon("steps")
def _steps(c):                                         # a row of steps, some on
    on = [3, 1, 3, 1, 3, 1, 1, 3, 1]
    for k, v in enumerate(on):
        x, y = 1 + (k % 3) * 4, 1 + (k // 3) * 4
        c.fill(x, y, x + 1, y + 1, v)


@icon("accent")
def _accent(c):
    c.line(2, 2, 9, 5)
    c.line(9, 6, 2, 9)


@icon("time")
def _time(c):                                          # a clock
    c.ring(1, 1, 10)
    c.line(5, 3, 5, 6)
    c.line(6, 6, 8, 6)


@icon("phase")
def _phase(c):                                         # an angle on the circle
    c.line(1, 6, 10, 6, 1)
    c.ring(2, 2, 8)
    c.line(6, 6, 8, 4, 2)
    c.fill(8, 3, 9, 4)


@icon("fade")
def _fade(c):                                          # a ramp of rising ink
    for x in range(1, 11):
        top = 10 - (x - 1)
        c.line(x, top, x, 10, 1 if x < 4 else (2 if x < 8 else 3))
    c.line(1, 10, 10, 1)


@icon("mix")
def _mix(c):                                           # three faders
    for x, y in ((3, 7), (6, 3), (9, 5)):
        c.line(x, 1, x, 10, 1)
        c.fill(x - 1, y, x + 1, y + 1)


@icon("size")
def _size(c):                                          # a room's corners
    for x, y, dx, dy in ((1, 1, 1, 1), (10, 1, -1, 1), (1, 10, 1, -1), (10, 10, -1, -1)):
        c.line(x, y, x + 3 * dx, y)
        c.line(x, y, x, y + 3 * dy)
    c.rect(4, 4, 7, 7, 1)


@icon("damp")
def _damp(c):                                          # a ringing that dies away
    c.curve(lambda t: math.exp(-2.2 * t) * math.sin(TAU * 1.5 * t), 1, 10, 5.5, 4.5)


@icon("tone")
def _tone(c):                                          # a tilt: less low, more high
    c.line(1, 6, 10, 6, 1)
    c.line(1, 9, 10, 2)
    c.fill(5, 5, 6, 6, 3)


@icon("sweep")
def _sweep(c):                                         # a sweep upwards
    c.curve(lambda t: t * t, 1, 9, 10, 8)
    c.arrow_head(9, 2, "u", size=2)


@icon("vibrato")
def _vibrato(c):                                       # a held note that wavers
    c.curve(lambda t: math.sin(TAU * 2 * t), 1, 10, 4, 1.6)
    c.line(1, 9, 10, 9, 1)


@icon("loop")
def _loop(c):                                          # round and round
    c.grid("""
        .......#....
        ....#####...
        ..##...#....
        ..#.........
        .#........#.
        .#........#.
        .#........#.
        .#........#.
        ..#......#..
        ..##....##..
        ....####....
        ............""")


@icon("hold")
def _hold(c):                                          # a lock: the notes stay
    c.grid("""
        ............
        ....####....
        ...#....#...
        ...#....#...
        ...#....#...
        ..########..
        ..########..
        ..####.###..
        ..####.###..
        ..########..
        ..########..
        ............""")


@icon("order")
def _order(c):                                         # up and down
    c.line(3, 2, 3, 10)
    c.arrow_head(3, 2, "u", size=2)
    c.line(8, 1, 8, 9)
    c.arrow_head(8, 9, "d", size=2)


@icon("prob")
def _prob(c):                                          # a die
    c.grid("""
        ............
        .:########:.
        .#........#.
        .#.##.....#.
        .#.##.....#.
        .#...##...#.
        .#...##...#.
        .#.....##.#.
        .#.....##.#.
        .#........#.
        .:########:.
        ............""")


@icon("length")
def _length(c):                                        # from here to there
    c.line(1, 3, 1, 9)
    c.line(10, 3, 10, 9)
    c.line(3, 6, 8, 6)
    c.arrow_head(3, 6, "l", size=1)
    c.arrow_head(8, 6, "r", size=1)


@icon("division")
def _division(c):
    c.fill(5, 1, 6, 2)
    c.line(1, 5, 10, 5)
    c.fill(5, 8, 6, 9)


grid_icon("chip", """
    ............
    ............
    ...######...
    ...#:...#...
    .###....###.
    ...#....#...
    .###....###.
    ...#....#...
    .###....###.
    ...#....#...
    ...######...
    ............""")


@icon("midi")
def _midi(c):                                          # a five-pin socket
    c.ring(1, 1, 10)
    for x, y in ((3, 6), (3, 4), (8, 4), (8, 6)):
        c.put(x, y)
    c.fill(5, 3, 6, 3)
    c.fill(5, 9, 6, 9, 2)


@icon("save")
def _save(c):                                          # into the tray
    tray(c)
    c.line(5, 1, 5, 7)
    c.arrow_head(5, 7, "d", size=2)


@icon("load")
def _load(c):                                          # out of the tray
    tray(c)
    c.line(5, 1, 5, 7)
    c.arrow_head(5, 1, "u", size=2)


grid_icon("clear", """
    ............
    ....####....
    .##########.
    ............
    ..#......#..
    ..#.:..:.#..
    ..#.:..:.#..
    ..#.:..:.#..
    ..#.:..:.#..
    ..#......#..
    ...######...
    ............""")


@icon("w_sin")
def _w_sin(c):
    c.curve(sine, WAVE_X0, WAVE_X1, WAVE_YC, WAVE_AMP)


@icon("w_tri")
def _w_tri(c):
    c.curve(lambda t: 2 / math.pi * math.asin(sine(t)), WAVE_X0, WAVE_X1, WAVE_YC, WAVE_AMP)


@icon("w_saw")
def _w_saw(c):
    c.poly([(1, 9), (5, 2), (5, 9), (6, 9), (10, 2), (10, 9)])


@icon("w_sqr")
def _w_sqr(c):
    c.poly([(1, 9), (1, 2), (5, 2), (5, 9), (9, 9), (9, 2), (10, 2)])


@icon("w_pls")
def _w_pls(c):
    c.poly([(1, 9), (1, 2), (2, 2), (2, 9), (6, 9), (6, 2), (7, 2), (7, 9), (10, 9)])


@icon("w_pwm")
def _w_pwm(c):                                         # a square whose width moves
    c.poly([(1, 9), (1, 2), (4, 2), (4, 9), (10, 9)])
    c.line(6, 2, 6, 8, 1)
    c.line(5, 2, 5, 2, 1)
    c.line(8, 2, 8, 9, 2)
    c.line(9, 2, 10, 2, 2)


@icon("w_sh")
def _w_sh(c):                                          # sample and hold: random steps
    ys = [6, 6, 3, 3, 8, 8, 4, 4, 9, 9]
    for k, y in enumerate(ys):
        c.put(1 + k, y)
        if k and ys[k - 1] != y:
            c.line(1 + k, ys[k - 1], 1 + k, y)


@icon("w_dsin")
def _w_dsin(c):                                        # phase distortion: two humps a cycle
    c.curve(lambda t: abs(math.sin(TAU * t)) * 2 - 1 if t < 0.5 else -math.sin(math.pi * (2 * t - 1)),
            WAVE_X0, WAVE_X1, WAVE_YC, WAVE_AMP)


@icon("w_spls")
def _w_spls(c):                                        # phase distortion: a ramp, then a pulse
    c.poly([(1, 9), (4, 2), (5, 2), (5, 9), (6, 9), (8, 2), (9, 2), (9, 9), (10, 9)])


def resonance(c, window):
    """phase-distortion resonance: a fast ripple inside the window `window(t)` (0..1), one cycle"""
    pts = []
    for k, x in enumerate(range(1, 11)):
        a = window((x - 1) / 9)
        pts.append((x, round(5.5 + (-1 if k % 2 == 0 else 1) * 3.5 * a)))
    c.poly(pts)


@icon("w_rsaw")
def _w_rsaw(c):
    resonance(c, lambda t: 1 - t)


@icon("w_rtri")
def _w_rtri(c):
    resonance(c, lambda t: 1 - abs(2 * t - 1))


@icon("w_rtrp")
def _w_rtrp(c):
    resonance(c, lambda t: min(1.0, 3 * t, 3 * (1 - t)))


grid_icon("tape", """
    ............
    .##########.
    .#........#.
    .#.##..##.#.
    .##..##..##.
    .##..##..##.
    .#.##..##.#.
    .#........#.
    .#.::::::.#.
    .##########.
    ............
    ............""")

grid_icon("drum", """
    .........##.
    ........##..
    .......##...
    ..#####.#...
    .#:::::##...
    .#:::::::#..
    .#########..
    .#.:.:.:.#..
    .#.:.:.:.#..
    .#########..
    ............
    ............""")                                   # a snare seen from the side and a little above, a stick
                                                       # coming down on its head (no dots in rows: not a face)

grid_icon("mouth", """
    ............
    ..########..
    .#........#.
    .#....#...#.
    .#..#.#.#.#.
    .#.##.#.#.#.
    .#..#.#...#.
    ..###.####..
    ....#.#.....
    ....##......
    ....#.......
    ............""")


@icon("trio")
def _trio(c):                                          # three voices
    c.ring(4, 1, 4)
    c.ring(1, 6, 4)
    c.ring(7, 6, 4)


@icon("drawbar")
def _drawbar(c):                                       # drawbars pulled out to different lengths
    c.line(1, 1, 10, 1)
    for x, end in ((2, 8), (5, 4), (8, 6)):
        c.line(x, 2, x, 10, 1)
        c.line(x + 1, 2, x + 1, 10, 1)
        c.fill(x, 2, x + 1, end)


@icon("slice")
def _slice(c):                                         # a sample cut in pieces
    sample_blob(c, 2)
    for x in (4, 7):
        c.line(x, 0, x, 11, 0)
        c.line(x, 1, x, 10)


@icon("grain")
def _grain(c):                                         # a cloud of short grains
    for x, y in ((4, 1), (8, 2), (1, 4), (5, 5), (9, 6), (2, 8), (6, 9)):
        c.fill(x, y, x + 1, y + 1)
    for x, y in ((1, 1), (10, 10), (8, 9), (3, 6), (7, 4), (10, 3)):
        c.put(x, y, 2)

