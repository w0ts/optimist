/* SPDX-License-Identifier: GPL-3.0-only */
/* Launchpad X -> Digitone II, Note mode (jank_lpapp.h). */
#include "jank_lpapp.h"
#include "jank_lpstr.h"
#include "jank_lpapp_int.h"
#include "jank_lpscales.h"

#define BUTTON_UP 0u                   /* the top buttons, left to right */
#define BUTTON_DOWN 1u
#define BUTTON_LEFT 2u
#define BUTTON_RIGHT 3u
#define BUTTON_NOTE 5u
#define WHY_EVERY_MS 2000u             /* "why the pads do not play" is said at most this often */
#define QUADRANT 4u                    /* the step modes' lower quadrants are 4 x 4 */
#define FIXED_VEL(a) ((a)->fixed_vel || (a)->no_vel)

enum jlapp_role jlapp_role_from_name(const char *product)
{
    if (!product)
        return JLAPP_OTHER;
    if (jlx_model_find(product))       /* any of the Launchpads jank drives (core/jank_lpx.h) */
        return JLAPP_LPX;
    if (jlx_has(product, "Digitone"))
        return JLAPP_DN2;
    return JLAPP_OTHER;
}

unsigned jlapp_midi_interface_index(enum jlapp_role r) { return r == JLAPP_LPX ? 1u : 0u; }

void jlapp_init(struct jlapp *a, const struct jln_scale *scale)
{
    memset(a, 0, sizeof *a);
    jln_init(&a->n);
    a->scale = scale;
    a->minor_idx = JLS_MINOR;
    a->play_ok = 1;
    a->step_kind = JLST_MELODIC;
    jlst_init(&a->step, JLST_MELODIC);
    jlmx_init(&a->mix);
    a->mix_writes = 1;
    a->record_ok = 1;
    a->dn2_present = 1;
    jlapp_page_init(a);
    jltr_init(&a->tr);
    for (uint32_t i = 0; i < JLAPP_PADS; i++)
        a->held[i].note = -1;
}

static void say_chart_page(struct jlapp *a) { jlapp_say(a, "COLOURS %u-%u", (unsigned)a->palette_page * 64u, (unsigned)a->palette_page * 64u + 63u); }

void jlapp_tick(struct jlapp *a, uint32_t now_ms)
{
    a->now_ms = now_ms;
    if (a->sess_down && (uint32_t)(now_ms - a->sess_down_ms) >= JLAPP_SESSION_HOLD_MS) {   /* Session held: the colour chart (jank_lpapp.h), on its first page, while it is still down */
        a->sess_down = 0;
        a->palette_prev = a->mode;     /* it goes back to the mode it came from (the Session page too) */
        a->palette_page = 0;
        a->mode = JLAPP_MODE_PALETTE;
        say_chart_page(a);
    }
}

void jlapp_set_live(struct jlapp *a, int state, int play_ok, int read_ok)
{
    int was_live = a->live_state == JDL_LIVE;
    a->live_state = (uint8_t)state;
    if (state == (int)JDL_LIVE && !was_live && a->mode == JLAPP_MODE_SESSION)
        jlapp_page_open(a, 1);         /* live control is back on the Session page: everything is read again */
    if (state != (int)JDL_LIVE && was_live) {
        jlapp_page_open(a, 0);         /* what the page showed may not be true any more */
        jltr_lost(&a->tr);             /* TRANSPORT must answer again */
    }
    if (state != (int)JDL_LIVE)
        a->dn2_seen = 0;               /* the next reading after live control is back is a first one again */
    a->play_ok = play_ok != 0;
    a->push = state == (int)JDL_LIVE;
    a->can_play = a->push && play_ok;
    a->read_ok = read_ok != 0;
}

/* ---- the messages: upper case, 7-bit, at most JLAPP_MSG_MAX - 1 characters; the newest replaces one nobody has taken. The format knows %s and %u, all the core uses: no
 * C library, and the compiler's own varargs (owner 2026-11-26: the FM-1 has no <stdio.h> or <stdarg.h>) ---- */

void jlapp_say(struct jlapp *a, const char *fmt, ...)
{
    __builtin_va_list ap;
    char num[10];
    size_t i = 0;
    __builtin_va_start(ap, fmt);
    for (; *fmt; fmt++) {
        const char *s = fmt;
        size_t n = 1;
        if (*fmt == '%' && fmt[1] == 's') {
            s = __builtin_va_arg(ap, const char *);
            n = (size_t)-1;            /* to its end */
            fmt++;
        } else if (*fmt == '%' && fmt[1] == 'u') {
            unsigned v = __builtin_va_arg(ap, unsigned);
            char *d = num + sizeof num;
            do
                *--d = (char)('0' + v % 10u);
            while (v /= 10u);
            s = d;
            n = (size_t)(num + sizeof num - d);
            fmt++;
        }
        for (; n && *s && i < JLAPP_MSG_MAX - 1u; n--, s++) {
            char c = *s >= 'a' && *s <= 'z' ? (char)(*s - 'a' + 'A') : *s;
            a->msg[i++] = c >= ' ' && c <= '~' ? c : '?';
        }
    }
    __builtin_va_end(ap);
    a->msg[i] = 0;
    a->msg_new = 1;
}

size_t jlapp_take_message(struct jlapp *a, char *out, size_t cap)
{
    size_t n = 0;
    if (!a->msg_new || !cap)
        return 0;
    for (; a->msg[n] && n + 1u < cap; n++)
        out[n] = a->msg[n];
    out[n] = 0;
    a->msg_new = 0;
    return n;
}

static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* a press that cannot play says why (at most every WHY_EVERY_MS): the pads never fall back to MIDI */
void jlapp_say_why_not(struct jlapp *a)
{
    if (a->why_on && (uint32_t)(a->now_ms - a->why_ms) < WHY_EVERY_MS)
        return;
    a->why_on = 1;
    a->why_ms = a->now_ms;
    if (a->live_state == JDL_REMOTE_OFF)
        jlapp_say(a, "REMOTE CONTROL OFF");
    else if (a->live_state == JDL_NO_LIVE)
        jlapp_say(a, "NO LIVE CONTROL");
    else if (a->live_state == JDL_LIVE && !a->play_ok)
        jlapp_say(a, "DN2 HAS NO PLAY");
    else
        jlapp_say(a, "DN2 NOT READY");
}

/* ---- pads ---- */

/* another pad that is down holds the same note on the same track */
static int held_elsewhere(const struct jlapp *a, uint32_t pad, const struct jlapp_held *h)
{
    for (uint32_t i = 0; i < JLAPP_PADS; i++)
        if (i != pad && a->held[i].note == h->note && a->held[i].track == h->track)
            return 1;
    return 0;
}

/* the rows the track pads take from the bottom of the layout */
static uint8_t note_shift(const struct jlapp *a) { return a->track_pads ? (uint8_t)JLAPP_TRACK_ROWS : 0u; }

static size_t pad_event(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    uint32_t i = ev->row * 8u + ev->col;
    struct jlapp_held *h = &a->held[i];
    if (ev->value) {
        struct jln_pad p;
        if (h->note >= 0 || max < 1 || ev->row < note_shift(a))
            return 0;
        jln_pad(&a->n, a->scale, ev->col, (uint8_t)(ev->row - note_shift(a)), &p);
        if (p.note < 0)
            return 0;
        if (!a->can_play) {
            jlapp_say_why_not(a);
            return 0;
        }
        h->note = p.note;
        h->track = a->n.track;
        jlapp_set_act(out, JLAPP_ACT_NOTE_ON, h->track, (uint8_t)p.note, ev->value);
        return 1;
    }
    if (h->note < 0)
        return 0;
    {
        struct jlapp_held was = *h;
        h->note = -1;
        if (held_elsewhere(a, i, &was) || max < 1)
            return 0;
        jlapp_set_act(out, JLAPP_ACT_NOTE_OFF, was.track, (uint8_t)was.note, 0);
        return 1;
    }
}

/* The track a pad of the bottom rows stands for, laid out as the Digitone's trig keys are: tracks 1..8 in the upper of the two rows (pad row 1), 9..16 in the
 * lower one (pad row 0) (owner 2026-11-24); a unit with 8 tracks or fewer has one row. A pad past the unit's last track: JLN_NO_TRACK, dark, does nothing */
static uint8_t track_of_pad(uint8_t row, uint8_t col) { return (uint8_t)((JLAPP_TRACK_ROWS - 1u - row) * 8u + col); }

/* a pad of the two bottom rows while the track pads are on: it plays that track's own note */
static size_t chose_track(struct jlapp *a, uint8_t before, int announce, struct jlapp_act *out, size_t max);

static size_t track_pad_event(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    uint8_t t = track_of_pad(ev->row, ev->col);
    uint16_t bitv = (uint16_t)(1u << t);
    if (JLN_NO_TRACK(t))
        return 0;
    if (a->track_pads == JLAPP_TRACKS_SELECT && ev->value) {   /* SELECT: the pad's track becomes the current one, nothing sounds */
        uint8_t before = a->n.track;
        a->note_used = 1;
        a->n.track = t;
        return chose_track(a, before, 1, out, max);
    }
    if (ev->value) {
        if ((a->track_held & bitv) || max < 1)
            return 0;
        if (!a->can_play) {
            jlapp_say_why_not(a);
            return 0;
        }
        a->track_held |= bitv;
        jlapp_set_act(out, JLAPP_ACT_TRACK_ON, t, 0, 0);
        return 1;
    }
    if (!(a->track_held & bitv))
        return 0;
    a->track_held &= (uint16_t)~bitv;
    if (max < 1)
        return 0;
    jlapp_set_act(out, JLAPP_ACT_TRACK_OFF, t, 0, 0);
    return 1;
}

/* ---- the scale page (jank_lpapp.h): rows are counted from the top, as Launchpad 95 does, row 0 being the top one (pad row 7) ---- */
static const uint8_t WHITE_KEY[7] = {0, 2, 4, 5, 7, 9, 11};      /* C D E F G A B */
#define FROM_TOP(y) (7u - (y))
#define SCALE_PAGE_ROW 3u              /* the row above the scales: the page pads (owner 2026-11-24; the octave row that was here is gone) */
#define SCALE_PAGES ((JLS_COUNT + JLAPP_SCALES_PER_PAGE - 1u) / JLAPP_SCALES_PER_PAGE)   /* ... one pad a page: the Digitone's 36 scales three, the FM-1's 16 one */
#define FIRST_SCALE_ROW 4u             /* the unit's scales in banks of 2 rows of 8 (JLAPP_SCALES_PER_PAGE) */
#define FIRST_TRACK_ROW (8u - JLAPP_TRACK_ROWS)   /* the bottom rows: the tracks, a press selects (owner 2026-11-24) */
#define FIFTH_UP 7u                   /* the circle of fifths: a fifth up is 7 semitones, a fifth down 5 */
#define FIFTH_DOWN 5u
#define MINOR_THIRD_UP 3u
#define MAJOR_SIXTH_UP 9u              /* the relative major of a minor key is a minor third up, its relative minor a minor third down (9 up) */

static uint8_t track_colour(const struct jlapp *a, uint8_t t);

static int is_black_key_pad(uint8_t x) { return x == 0 || x == 1 || x == 3 || x == 4 || x == 5; }

static void say_key(struct jlapp *a) { jlapp_say(a, "KEY %s", NOTE_NAME[a->n.key % 12u]); }

static void set_scale(struct jlapp *a, unsigned idx)
{
    const struct jln_scale *s = jls_get(idx);
    if (s) {
        a->scale = s;
        a->scale_page = (uint8_t)(idx / JLAPP_SCALES_PER_PAGE);   /* the page of the scale in use is the one shown */
        jlapp_say(a, "SCALE %s", s->name);
    }
}

/* the scale page opens on the page that holds the scale in use */
static void show_scale_page_of_scale(struct jlapp *a)
{
    int idx = jls_index_of(a->scale);
    if (idx >= 0)
        a->scale_page = (uint8_t)((unsigned)idx / JLAPP_SCALES_PER_PAGE);
}

/* major <-> the minor last used, minor pentatonic <-> major pentatonic; another scale has no relative one */
static void relative_scale(struct jlapp *a)
{
    int idx = jls_index_of(a->scale);
    struct jln *n = &a->n;
    if (idx == (int)JLS_MAJOR) {
        set_scale(a, a->minor_idx);
        n->key = (uint8_t)((n->key + MAJOR_SIXTH_UP) % 12u);
    } else if (idx == (int)JLS_MINOR || idx == (int)JLS_HARMONIC_MINOR || idx == (int)JLS_MELODIC_MINOR) {
        a->minor_idx = (uint8_t)idx;
        set_scale(a, JLS_MAJOR);
        n->key = (uint8_t)((n->key + MINOR_THIRD_UP) % 12u);
    } else if (idx == (int)JLS_MINOR_PENTATONIC) {
        set_scale(a, JLS_MAJOR_PENTATONIC);
        n->key = (uint8_t)((n->key + MINOR_THIRD_UP) % 12u);
    } else if (idx == (int)JLS_MAJOR_PENTATONIC) {
        set_scale(a, JLS_MINOR_PENTATONIC);
        n->key = (uint8_t)((n->key + MAJOR_SIXTH_UP) % 12u);
    } else {
        jlapp_say(a, "NO RELATIVE SCALE");
        return;
    }
    jlapp_say(a, "%s %s", NOTE_NAME[n->key % 12u], a->scale->name);     /* the key and the scale it is now: "A Minor" */
}

static void layout_pad(struct jlapp *a, enum jln_layout l, const char *name)
{
    jln_set_layout(&a->n, l);
    jlapp_say(a, "LAYOUT %s", name);
}

static void page_pad(struct jlapp *a, uint8_t x, uint8_t y)
{
    struct jln *n = &a->n;
    uint8_t r = (uint8_t)FROM_TOP(y);
    if (r == 0) {
        if (x == 0) {
            n->absolute ^= 1u;
            jlapp_say(a, "ABSOLUTE ROOT %s", n->absolute ? "ON" : "OFF");
        } else if (x == 1 && n->layout == JLN_DIATONIC) {
            n->horizontal ^= 1u;
            jlapp_say(a, "HORIZONTAL %s", n->horizontal ? "ON" : "OFF");
        } else if (x == 1) {
            jlapp_say(a, "DIATONIC ONLY");
        } else if (x == 2) {           /* fixed velocity (owner 2026-11-25): Note mode, the step modes' keyboard; the track pads always play at the track's VEL */
            a->fixed_vel ^= 1u;
            jlapp_say(a, "FIXED VELOCITY %s", FIXED_VEL(a) ? "ON" : "OFF");
        } else if (x == 4) {
            layout_pad(a, JLN_CHORDS, "CHORDS");
        } else if (x == 5) {
            layout_pad(a, JLN_DIATONIC, "DIATONIC");
        } else if (x == 6) {
            layout_pad(a, JLN_CHROMATIC, "CHROMATIC");
        } else if (x == 7) {
            static const char *const NAME[JLAPP_TRACKS_MODES] = {"OFF", "TRIGGER", "SELECT"};
            a->track_pads = (uint8_t)((a->track_pads + 1u) % JLAPP_TRACKS_MODES);   /* OFF -> TRIGGER -> SELECT (owner 2026-11-25) */
            jlapp_say(a, "TRACK PADS %s", NAME[a->track_pads]);
        }
    } else if (r == 1) {
        if (is_black_key_pad(x)) {
            n->key = (uint8_t)(WHITE_KEY[x] + 1u);
            say_key(a);
        } else if (x == 2) {
            relative_scale(a);
        } else if (x == 6) {
            n->key = (uint8_t)((n->key + FIFTH_DOWN) % 12u);
            say_key(a);
        }
    } else if (r == 2) {
        if (x < 7u)
            n->key = WHITE_KEY[x];
        else
            n->key = (uint8_t)((n->key + FIFTH_UP) % 12u);
        say_key(a);
    } else if (r == SCALE_PAGE_ROW) {  /* the scales' page pads: only browsing, the scale in use is not changed */
        if (x < SCALE_PAGES) {
            unsigned first = x * JLAPP_SCALES_PER_PAGE, last = first + JLAPP_SCALES_PER_PAGE;
            a->scale_page = x;
            jlapp_say(a, "SCALES %u-%u", first + 1u, last < jls_count() ? last : jls_count());
        }
    } else if (r < FIRST_TRACK_ROW) {
        unsigned idx = a->scale_page * JLAPP_SCALES_PER_PAGE + (unsigned)(r - FIRST_SCALE_ROW) * 8u + x;
        if (idx < jls_count())
            set_scale(a, idx);
    }
}

static uint8_t on_off(int on, uint8_t on_colour, uint8_t off_colour) { return on ? on_colour : off_colour; }

/* the colour of the scale page's pad (x, y), 0 for none: Launchpad 95's (SkinMK2.py Scale.*; see jank_lpapp.h) */
static uint8_t page_colour(const struct jlapp *a, uint8_t x, uint8_t y)
{
    const struct jln *n = &a->n;
    uint8_t r = (uint8_t)FROM_TOP(y);
    if (r == 0) {
        if (x == 0)                    /* AbsoluteRoot */
            return on_off(n->absolute, JLAPP_RED, JLAPP_RED_THIRD);
        if (x == 1)                    /* Horizontal (LP95 shows it in every layout, though only the diatonic one takes it) */
            return on_off(n->horizontal, JLAPP_GREEN, JLAPP_GREEN_THIRD);
        if (x == 2)                    /* fixed velocity (not Launchpad 95's) */
            return on_off(FIXED_VEL(a), JLAPP_COLOR_FIXED_VEL, JLAPP_COLOR_FIXED_VEL_OFF);
        if (x == 4)                    /* Mode: the layouts */
            return on_off(n->layout == JLN_CHORDS, JLAPP_RED, JLAPP_RED_THIRD);
        if (x == 5)
            return on_off(n->layout == JLN_DIATONIC, JLAPP_RED, JLAPP_RED_THIRD);
        if (x == 6)
            return on_off(n->layout == JLN_CHROMATIC, JLAPP_RED, JLAPP_RED_THIRD);
        if (x == 7)                    /* the track pads (not Launchpad 95's: its drum rack pad is here): dim green off, then the mode's own colour */
            return a->track_pads == JLAPP_TRACKS_TRIGGER ? JLAPP_COLOR_TRIG_IDLE : a->track_pads == JLAPP_TRACKS_SELECT ? JLAPP_COLOR_TRACK_IDLE : JLAPP_COLOR_MODE;   /* off: Note mode's light blue (owner 2026-11-25) */
        return 0;
    }
    if (r == 1) {
        if (is_black_key_pad(x))       /* Key */
            return on_off(n->key == WHITE_KEY[x] + 1u, JLAPP_GREEN, JLAPP_GREEN_THIRD);
        return x == 2 || x == 6 ? JLAPP_BLUE : 0;      /* RelativeScale, CircleOfFifths */
    }
    if (r == 2)
        return x < 7u ? on_off(n->key == WHITE_KEY[x], JLAPP_GREEN, JLAPP_GREEN_THIRD) : JLAPP_BLUE;
    if (r >= FIRST_TRACK_ROW)          /* the tracks: WHITE, the selected one AMBER (a track that sounds RED) */
        return track_colour(a, track_of_pad(y, x));
    if (r == SCALE_PAGE_ROW)           /* the page pads: AMBER, not the scales' blue */
        return x < SCALE_PAGES ? on_off(x == a->scale_page, JLAPP_AMBER, JLAPP_AMBER_THIRD) : 0;
    {
        unsigned idx = a->scale_page * JLAPP_SCALES_PER_PAGE + (unsigned)(r - FIRST_SCALE_ROW) * 8u + x;   /* Modus: the two rows of the page shown */
        return idx < jls_count() ? on_off((int)idx == jls_index_of(a->scale), JLAPP_BLUE, JLAPP_BLUE_THIRD) : 0;
    }
}

/* ---- buttons ---- */

static void show_track(struct jlapp *a)
{
    a->select_on = 1;
    a->select_ms = a->now_ms;
}

/* a track the Launchpad chose: shown, said, and the Digitone's active track too (not trusted back from it for a moment: jlapp_on_active_track) */
static size_t chose_track(struct jlapp *a, uint8_t before, int announce, struct jlapp_act *out, size_t max)
{
    show_track(a);
    a->hold_on = 1;
    a->hold_ms = a->now_ms;
    if (announce)
        jlapp_say(a, "TRACK %u", (unsigned)a->n.track + 1u);
    if (a->n.track == before || max < 1)
        return 0;
    jlapp_set_act(out, JLAPP_ACT_SELECT, a->n.track, 0, 0);
    return 1;
}

/* both left and right: FOLLOW on / off (the first press already stepped the track: put it back) */
static size_t follow_pair(struct jlapp *a, uint8_t other_idx, struct jlapp_act *out, size_t max)
{
    uint8_t stepped_to = a->n.track;
    a->n.track = a->before[other_idx];
    if (!a->follow && !(a->live_state == JDL_LIVE && a->read_ok)) {   /* it cannot run: say why, leave it off */
        if (a->live_state == JDL_LIVE)
            jlapp_say(a, "DN2 CANNOT FOLLOW");
        else
            jlapp_say(a, "DN2 NOT READY");
    } else {
        a->follow ^= 1u;
        jlapp_say(a, "FOLLOW %s", a->follow ? "ON" : "OFF");
    }
    return chose_track(a, stepped_to, 0, out, max);
}

/* left / right: the previous / next track; both together make no step but switch FOLLOW */
static size_t track_button(struct jlapp *a, unsigned idx, int dir, int pressed, struct jlapp_act *out, size_t max)
{
    uint8_t bit = (uint8_t)(1u << idx), other = (uint8_t)(1u << (1u - idx)), before = a->n.track;
    if (!pressed) {
        a->buttons &= (uint8_t)~bit;
        return 0;
    }
    a->before[idx] = before;
    jln_track_step(&a->n, dir);
    if (a->buttons & other) {
        a->buttons |= bit;
        a->n.track = before;           /* this press makes no step either: the first one's step is put back by follow_pair */
        return follow_pair(a, 1u - idx, out, max);
    }
    a->buttons |= bit;
    return chose_track(a, before, 1, out, max);
}

/* ---- the modes ---- */

static struct jlst_ctx step_ctx(const struct jlapp *a, struct jln *n)
{
    struct jlst_ctx c;
    c.n = n;
    c.scale = a->scale;
    c.can_play = a->can_play;
    c.transport_playing = a->transport;
    c.now_ms = a->now_ms;
    c.recent = a->recent;
    c.recent_n = JLAPP_RECENT;
    c.deleting = a->erase_on;
    return c;
}

static int step_holds(const struct jlapp *a)
{
    if (a->step.track_held)
        return 1;
    for (unsigned i = 0; i < JLST_KEYS; i++)
        if (a->step.key_note[i] >= 0)
            return 1;
    return 0;
}

/* Custom: from Note mode into the step mode last used (melodic at first), in a step mode to the other one */
static void custom_button(struct jlapp *a)
{
    if (a->mode == JLAPP_MODE_STEP)
        a->step_kind = a->step_kind == JLST_MELODIC ? JLST_DRUM : JLST_MELODIC;
    a->mode = JLAPP_MODE_STEP;
    a->step.kind = a->step_kind;
    jlapp_say(a, a->step_kind == JLST_MELODIC ? "MELODIC STEP" : "DRUM STEP");
}

/* the Note button: held, the scale page; a tap (short, nothing pressed on the page) from a step mode is Note mode */
static void note_button(struct jlapp *a, int down)
{
    if (down) {
        a->page = 1;
        show_scale_page_of_scale(a);
        a->note_used = 0;
        a->note_down_ms = a->now_ms;
        return;
    }
    a->page = 0;
    if (a->mode != JLAPP_MODE_NOTE && !a->note_used && (uint32_t)(a->now_ms - a->note_down_ms) < JLAPP_TAP_MS) {
        a->mode = JLAPP_MODE_NOTE;
        jlapp_say(a, "NOTE MODE");
    }
}

static size_t step_pad(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    struct jlst_ctx c = step_ctx(a, &a->n);
    uint8_t before = a->n.track;
    size_t n = jlst_pad(&a->step, &c, ev->col, ev->row, ev->value, out, max);
    if (a->n.track != before) {        /* a drum track pad chose the track */
        a->hold_on = 1;
        a->hold_ms = a->now_ms;
        jlapp_say(a, "TRACK %u", (unsigned)a->n.track + 1u);
    } else if (ev->value && !n && !a->push && ev->row >= 4u) {   /* a step with no live control to read or write it */
        jlapp_say_why_not(a);
    }
    return n;
}

/* the palette index on a pad of the chart (y from the bottom, 0 the bottom row): reading order from the top left */
static uint8_t palette_index(const struct jlapp *a, uint8_t x, uint8_t y) { return (uint8_t)(a->palette_page * 64u + (7u - y) * 8u + x); }

/* Session (jank_lpapp.h): in the chart a press turns its page, then leaves it, as before. Elsewhere the press only starts the clock: let go within JLAPP_SESSION_HOLD_MS, it
 * is the Session page (the unit's: jank_lpapp_int.h; on it, back to where it came from); held that long, jlapp_tick opens the chart */
static void session_button(struct jlapp *a, int down)
{
    if (down) {
        if (a->mode != JLAPP_MODE_PALETTE) {
            a->sess_down = 1;
            a->sess_down_ms = a->now_ms;
        } else if (a->palette_page + 1u < JLAPP_PALETTE_PAGES) {
            a->palette_page++;
            say_chart_page(a);
        } else {
            a->mode = a->palette_prev;
            if (a->mode == JLAPP_MODE_SESSION)
                jlapp_page_open(a, 1);
            jlapp_say(a, "COLOURS OFF");
        }
        return;
    }
    if (!a->sess_down)                 /* the release of a chart press, or of the hold that opened the chart */
        return;
    a->sess_down = 0;
    if (a->mode != JLAPP_MODE_SESSION) {
        a->sess_prev = a->mode;
        a->mode = JLAPP_MODE_SESSION;
        jlapp_page_open(a, 1);         /* everything read again: patterns may have been filled or cleared since */
        jlapp_say(a, "SESSION %s", JLAPP_HALF_NAME(a->sess.half));
        return;
    }
    a->mode = a->sess_prev;
    jlapp_say(a, "SESSION OFF");
}

static size_t button_event(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    if (ev->idx == BUTTON_NOTE) {
        note_button(a, ev->value != 0);
        return 0;
    }
    if (ev->idx == BUTTON_LEFT || ev->idx == BUTTON_RIGHT)
        return track_button(a, ev->idx - BUTTON_LEFT, ev->idx == BUTTON_LEFT ? -1 : 1, ev->value != 0, out, max);
    if (ev->idx == BUTTON_SESSION_IDX) {   /* its release counts: a short press and a hold are two things */
        session_button(a, ev->value != 0);
        return 0;
    }
    if (!ev->value)
        return 0;
    if (ev->idx == BUTTON_CUSTOM_IDX) {
        custom_button(a);
        return 0;
    }
    if (ev->idx == BUTTON_CAPTURE_IDX) {   /* the mixer, on its overview (Capture again from a sub mode comes back to it, as Launchpad 95's); a tap on Note goes back to Note mode, Custom to the step modes */
        a->mode = JLAPP_MODE_MIXER;
        jlmx_overview(&a->mix);
        jlapp_say(a, "MIXER");
        return 0;
    }
    if (a->mode == JLAPP_MODE_MIXER && (ev->idx == BUTTON_UP || ev->idx == BUTTON_DOWN)) {   /* the banks, as the Digitone's trig keys lie: up the upper row (1-8), down the lower (9-16) */
        if (jlmx_bank_step(&a->mix, ev->idx == BUTTON_UP ? -1 : 1))
            jlapp_say(a, "TRACKS %u-%u", a->mix.bank * JLMX_COLUMNS + 1u, a->mix.bank ? (unsigned)JLN_TRACKS : JLMX_COLUMNS);
        return 0;
    }
    if (a->mode == JLAPP_MODE_SESSION && (ev->idx == BUTTON_UP || ev->idx == BUTTON_DOWN)) {   /* the Session page's two halves: up the first (banks A-D, slots 1-8), down the other */
        if (a->sess.half != (ev->idx == BUTTON_DOWN)) {
            a->sess.half = ev->idx == BUTTON_DOWN;
            jlapp_say(a, "%s", JLAPP_HALF_NAME(a->sess.half));
        }
        return 0;
    }
    if ((ev->idx == BUTTON_UP || ev->idx == BUTTON_DOWN) && !(a->mode == JLAPP_MODE_STEP && a->step_kind == JLST_DRUM)) {   /* the octave; drum step has none */
        jln_octave_step(&a->n, ev->idx == BUTTON_UP ? 1 : -1);
        jlapp_say(a, "OCTAVE %u", (unsigned)a->n.octave);
    }
    return 0;                          /* the rest has no job yet */
}

static const char *const VIEW_NAME[JLMX_VIEWS] = {"VOLUME", "PAN", "DELAY SEND", "REVERB SEND", "CHORUS SEND", "MUTE AND SOLO", "MUTE AND SOLO"};

static void mix_act(struct jlapp_act *out, const struct jlmx_write *w)
{
    if (w->kind == JLMX_WRITE_VALUE) {
        jlapp_set_act(out, JLAPP_ACT_MIX, w->track, 0, w->value);
        out->op = w->param;
    } else {
        jlapp_set_act(out, JLAPP_ACT_MUTE, 0, (uint8_t)(w->mask & 0xFFu), 0);
        out->op = w->which;
        out->step = (uint8_t)(w->mask >> 8);
    }
}

/* a pad of the mixer screen: mute / solo write at the press; a fader pad is held and its release starts the slow fader (jank_lpmix.h), whose first step is written then.
 * A value row of the overview opens its sub mode (no write). Without live control it says why, and without the Digitone's mixer writes (a build without stage 6) it says
 * MIXER READ ONLY, at most every WHY_EVERY_MS */
static size_t mixer_pad(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    struct jlmx_write w;
    if (!ev->value) {
        if (max < 1 || a->live_state != JDL_LIVE || !a->mix_writes || !jlmx_release(&a->mix, ev->col, ev->row, a->now_ms, &w))
            return 0;
        mix_act(out, &w);
        return 1;
    }
    if (jlmx_open(&a->mix, ev->row)) {
        jlapp_say(a, "%s", VIEW_NAME[a->mix.view]);
        return 0;
    }
    if (a->live_state != JDL_LIVE) {
        jlapp_say_why_not(a);
        return 0;
    }
    if (!a->mix_writes) {
        if (!a->why_on || (uint32_t)(a->now_ms - a->why_ms) >= WHY_EVERY_MS) {
            a->why_on = 1;
            a->why_ms = a->now_ms;
            jlapp_say(a, "MIXER READ ONLY");
        }
        return 0;
    }
    if (max < 1 || !jlmx_press(&a->mix, ev->col, ev->row, a->now_ms, &w))
        return 0;
    mix_act(out, &w);
    return 1;
}

size_t jlapp_pump_acts(struct jlapp *a, struct jlapp_act *out, size_t max)
{
    struct jlmx_write w[JLMX_SLIDES];
    size_t n;
    if (a->live_state != JDL_LIVE || !a->mix_writes) {
        jlmx_stop_slides(&a->mix);     /* nothing could be written: a slide does not wait to jump later */
        return 0;
    }
    n = jlmx_slides(&a->mix, a->now_ms, w, max < JLMX_SLIDES ? max : JLMX_SLIDES);
    for (size_t i = 0; i < n; i++)
        mix_act(&out[i], &w[i]);
    return n;
}

/* the scene buttons (Launchpad 95's Note mode): 5 starts / stops the transport; the others have no job yet. In the mixer they pick the view (jank_lpmix.h) */
/* Delete (scene 6), held: the Digitone erases the track's steps as its own playhead passes (LIVE ERASE); let go ends it, in any mode (it can be held into the mixer) */
static size_t delete_button(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    if (!ev->value) {
        if (!a->erase_on || max < 1)
            return 0;
        a->erase_on = 0;
        jlapp_set_act(out, JLAPP_ACT_ERASE, a->erase_track, 0, 0);
        return 1;
    }
    if (a->erase_on || max < 1)
        return 0;
    if (a->live_state != JDL_LIVE) {
        jlapp_say_why_not(a);
        return 0;
    }
    a->erase_on = 1;
    a->erase_track = a->n.track;
    jlapp_say(a, "DELETE TRACK %u", (unsigned)a->n.track + 1u);
    jlapp_set_act(out, JLAPP_ACT_ERASE, a->erase_track, 0, 1);
    return 1;
}

/* Record (scene 8): arm / disarm the Digitone's own live record. The light follows at once and then the Digitone's own state (jlapp_set_record) */
static size_t record_button(struct jlapp *a, struct jlapp_act *out, size_t max)
{
    if (max < 1)
        return 0;
    if (!a->record_ok) {
        jlapp_say(a, "RECORD: DN2 TOO OLD");
        return 0;
    }
    if (a->live_state != JDL_LIVE) {
        jlapp_say_why_not(a);
        return 0;
    }
    a->record_on = a->record_on ? 0u : 1u;
    a->record_hold = 1;
    a->record_ms = a->now_ms;
    jlapp_say(a, "RECORD %s", a->record_on ? "ON" : "OFF");
    jlapp_set_act(out, JLAPP_ACT_RECORD, a->n.track, 0, a->record_on);
    return 1;
}

void jlapp_set_record(struct jlapp *a, int ok, int bits_valid, uint8_t bits, uint32_t busy_count)
{
    if (a->record_ok && !ok) {         /* a Digitone without RECORD: the button goes dark and says so on the next press */
        a->record_on = 0;
        jlapp_say(a, "RECORD: DN2 TOO OLD");
    }
    a->record_ok = ok != 0;
    if (busy_count < a->record_busy_seen)      /* the Digitone's counters restarted (live control came back) */
        a->record_busy_seen = busy_count;
    if (busy_count != a->record_busy_seen) {   /* GRID or STEP RECORDING is on: the Digitone did not arm */
        a->record_busy_seen = busy_count;
        a->record_on = 0;
        a->record_hold = 0;
        jlapp_say(a, "GRID/STEP REC IS ON");
    }
    if (bits_valid && !(a->record_hold && (uint32_t)(a->now_ms - a->record_ms) < JLAPP_RECORD_HOLD_MS))
        a->record_on = (bits & (JDL_REC_ARMED | JDL_REC_RECORDING)) != 0;
}

/* scene 5: while the Digitone leads, its TRANSPORT (PLAY / STOP: no clock, no Start byte from Jank); else Jank's session, as before (another unit: its own transport) */
static size_t start_stop(struct jlapp *a, struct jlapp_act *out, size_t max)
{
    uint8_t op;
    if (max < 1)
        return 0;
    if (jltr_press(&a->tr, a->live_state == JDL_LIVE, a->now_ms, &op)) {
        jlapp_say(a, op == JDL_TR_PLAY ? "START" : "STOP");
        jlapp_set_act(out, JLAPP_ACT_DN2_TRANSPORT, 0, 0, op);
        return 1;
    }
    jlapp_say(a, a->transport ? "STOP" : "START");
    jlapp_set_act(out, JLAPP_ACT_TRANSPORT, a->n.track, 0, 0);
    return 1;
}

/* the scene buttons; a unit without Delete or Record (jank_lpunit.h JLAPP_ERASE, JLAPP_RECORD) has them dark, doing nothing */
static size_t side_event(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    int own_scene = a->mode == JLAPP_MODE_MIXER || a->mode == JLAPP_MODE_SESSION;   /* the Session page has no Delete or Record (owner 2026-11-25) */
    if (JLAPP_ERASE && ev->idx == JLAPP_SIDE_DELETE && (!own_scene || (!ev->value && a->erase_on)))
        return delete_button(a, ev, out, max);
    if (a->mode == JLAPP_MODE_MIXER) {
        if (ev->value && jlmx_scene(&a->mix, ev->idx))
            jlapp_say(a, "%s", VIEW_NAME[a->mix.view]);
        return 0;
    }
    if (JLAPP_RECORD && ev->idx == JLAPP_SIDE_RECORD && ev->value && a->mode != JLAPP_MODE_SESSION)
        return record_button(a, out, max);
    if (ev->idx != JLAPP_SIDE_START_STOP || !ev->value)
        return 0;
    return start_stop(a, out, max);    /* the Digitone's TRANSPORT, or Jank's session (jank_lptrans.h) */
}

static size_t lpx_acts(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    if (ev->kind == JLX_SIDE)
        return side_event(a, ev, out, max);
    if (ev->kind == JLX_PAD && ev->row < 8u && ev->col < 8u) {
        int track_pad = a->track_pads && ev->row < JLAPP_TRACK_ROWS;
        if (a->page && ev->value) {    /* a press on the scale page edits it; a release still ends a note that was down before the page opened */
            int idx0 = jls_index_of(a->scale), idx1;
            uint8_t key0 = a->n.key, before = a->n.track;
            a->note_used = 1;
            if (ev->row < JLAPP_TRACK_ROWS) {   /* the two bottom rows of the page select a track (owner 2026-11-24) */
                if (JLN_NO_TRACK(track_of_pad(ev->row, ev->col)))
                    return 0;
                a->n.track = track_of_pad(ev->row, ev->col);
                return chose_track(a, before, 1, out, max);
            }
            page_pad(a, ev->col, ev->row);
            idx1 = jls_index_of(a->scale);
            if (max >= 1 && idx1 >= 0 && (idx1 != idx0 || a->n.key != key0)) {   /* the scale or the key changed: the Digitone is told */
                jlapp_set_act(out, JLAPP_ACT_SCALE, a->n.track, (uint8_t)idx1, a->n.key);
                return 1;
            }
            return 0;
        }
        if (a->mode == JLAPP_MODE_PALETTE && ev->value) {   /* the colour chart: a pad says its palette index */
            jlapp_say(a, "COLOUR %u", (unsigned)palette_index(a, ev->col, ev->row));
            return 0;
        }
        if (a->mode == JLAPP_MODE_SESSION) {   /* the Session page: a press, the release of its held pad (another release still ends a note that was down when it opened, below) */
            size_t n = jlapp_session_pad(a, ev, out, max);
            if (n || ev->value)
                return n;
        }
        if (a->mode == JLAPP_MODE_MIXER && !(!ev->value && step_holds(a) && ev->row < QUADRANT && ev->col < QUADRANT))   /* (a note held when the mode changed still ends below) */
            return mixer_pad(a, ev, out, max);
        if (a->mode == JLAPP_MODE_STEP || (!ev->value && step_holds(a) && ev->row < QUADRANT && ev->col < QUADRANT))   /* a release also ends a step mode pad's note after a mode change */
            return step_pad(a, ev, out, max);
        if (track_pad && (ev->value || (a->track_held & (1u << track_of_pad(ev->row, ev->col)))))
            return track_pad_event(a, ev, out, max);
        return pad_event(a, ev, out, max);
    }
    if (ev->kind == JLX_TOP)
        return button_event(a, ev, out, max);
    return 0;
}

size_t jlapp_on_lpx_acts(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    size_t n = lpx_acts(a, ev, out, max);
    if (FIXED_VEL(a))                  /* fixed velocity: every note the pads start plays at the track's own VEL (PLAY's velocity 0, docs/live-control.md 53) */
        for (size_t i = 0; i < n; ++i)
            if (out[i].kind == JLAPP_ACT_NOTE_ON)
                out[i].vel = 0;
    return n;
}

/* ---- from the Digitone ---- */

void jlapp_on_dn2_scale(struct jlapp *a, unsigned idx, int has_root, unsigned root)
{
    int first = !a->dn2_seen;
    if (idx >= jls_count())
        return;
    if (first || idx != a->dn2_idx) {  /* the Digitone's scale is new to us: take it (no message: the Digitone's own screen says it) */
        a->scale = jls_get(idx);
        a->scale_page = (uint8_t)(idx / JLAPP_SCALES_PER_PAGE);
    }
    if (has_root && root < 12u && (first || !a->dn2_has_root || root != a->dn2_root))
        a->n.key = (uint8_t)root;
    a->dn2_seen = 1;
    a->dn2_idx = (uint8_t)idx;
    a->dn2_has_root = has_root != 0 && root < 12u;
    a->dn2_root = (uint8_t)(a->dn2_has_root ? root : 0u);
}

int jlapp_following(const struct jlapp *a) { return a->follow && a->live_state == JDL_LIVE && a->read_ok; }

void jlapp_on_active_track(struct jlapp *a, unsigned track)
{
    if (!a->follow || track >= JLN_TRACKS || track == a->n.track)
        return;
    if (a->hold_on && (uint32_t)(a->now_ms - a->hold_ms) < JLAPP_FOLLOW_HOLD_MS)
        return;
    a->n.track = (uint8_t)track;
    show_track(a);
}

void jlapp_on_push(struct jlapp *a, const struct jdl_note *notes, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        struct jlapp_recent *r = &a->recent[a->recent_head];
        if (notes[i].track >= JLN_TRACKS || notes[i].note > 127u)
            continue;
        r->at_ms = a->now_ms;
        r->track = notes[i].track;
        r->note = notes[i].note;
        r->used = 1;
        a->recent_head = (uint8_t)((a->recent_head + 1u) % JLAPP_RECENT);
        if (notes[i].track == a->n.track)      /* the step that played, for the step modes' playhead */
            jlst_on_push(&a->step, notes[i].track, notes[i].step, a->now_ms);
    }
}

static void refresh_clock(struct jlapp *a) { jlst_set_clock(&a->step, a->transport && a->beat_ok, a->tick16); }

void jlapp_set_transport(struct jlapp *a, int playing)
{
    a->transport = playing != 0;
    jltr_set_jank(&a->tr, playing, a->now_ms);   /* (a change Jank made: the Digitone following it is not sent back) */
    refresh_clock(a);
}

void jlapp_on_dn2_rt(struct jlapp *a, uint8_t byte) { jlst_on_rt(&a->step, byte, a->now_ms); }

void jlapp_set_beat(struct jlapp *a, int known, uint32_t tick16)
{
    a->beat_ok = known != 0;
    a->tick16 = tick16;
    refresh_clock(a);
}

size_t jlapp_step_wanted(const struct jlapp *a, uint8_t pages[2])
{
    if (a->mode != JLAPP_MODE_STEP || a->live_state != JDL_LIVE)
        return 0;
    return jlst_wanted(&a->step, pages);
}

void jlapp_on_page(struct jlapp *a, const struct jdl_page *pg) { jlst_on_page(&a->step, a->n.track, pg, a->now_ms); }

int jlapp_step_urgent(const struct jlapp *a) { return a->mode == JLAPP_MODE_STEP && a->live_state == JDL_LIVE && jlst_urgent(&a->step, a->n.track); }

size_t jlapp_release_acts(struct jlapp *a, struct jlapp_act *out, size_t max)
{
    size_t n = 0;
    for (uint32_t i = 0; i < JLAPP_PADS && n < max; i++) {
        struct jlapp_held was = a->held[i];
        if (was.note < 0)
            continue;
        for (uint32_t j = i; j < JLAPP_PADS; j++)          /* one end for a note that several pads hold */
            if (a->held[j].note == was.note && a->held[j].track == was.track)
                a->held[j].note = -1;
        jlapp_set_act(&out[n++], JLAPP_ACT_NOTE_OFF, was.track, (uint8_t)was.note, 0);
    }
    for (uint8_t t = 0; t < JLN_TRACKS && n < max; t++)
        if (a->track_held & (1u << t)) {
            a->track_held &= (uint16_t)~(1u << t);
            jlapp_set_act(&out[n++], JLAPP_ACT_TRACK_OFF, t, 0, 0);
        }
    if (JLAPP_ERASE && a->erase_on && n < max) {   /* the Delete button was held: the Digitone stops erasing */
        a->erase_on = 0;
        jlapp_set_act(&out[n++], JLAPP_ACT_ERASE, a->erase_track, 0, 0);
    }
    return n + jlst_release(&a->step, out + n, max - n);
}

/* ---- lights ---- */

/* a pushed note sounded in the last JLAPP_PUSH_LIGHT_MS on this track (and with this pitch, unless note < 0) */
static int pushed(const struct jlapp *a, uint8_t track, int note)
{
    for (uint32_t i = 0; i < JLAPP_RECENT; i++) {
        const struct jlapp_recent *r = &a->recent[i];
        if (r->used && r->track == track && (note < 0 || r->note == note) && (uint32_t)(a->now_ms - r->at_ms) < JLAPP_PUSH_LIGHT_MS)
            return 1;
    }
    return 0;
}

static int select_shown(const struct jlapp *a) { return a->select_on && (uint32_t)(a->now_ms - a->select_ms) < JLAPP_SELECT_MS; }

/* a track pad's colour (jank_lpapp.h): a track that sounds (or whose pad is down) RED, the selected one AMBER, the rest WHITE */
static uint8_t track_colour(const struct jlapp *a, uint8_t t)
{
    if (JLN_NO_TRACK(t))
        return 0;
    if ((a->track_held & (1u << t)) || (a->push && pushed(a, t, -1)))
        return JLAPP_COLOR_TRACK_PLAYING;
    return t == a->n.track ? JLAPP_COLOR_TRACK_CURRENT : JLAPP_COLOR_TRACK_IDLE;
}

/* the track pads in Note mode: TRIGGER in the drum quadrant's colours, else (SELECT, or the overlay of a track chosen with the pads off) as the track rows */
static uint8_t track_pad_colour(const struct jlapp *a, uint8_t t)
{
    if (a->track_pads != JLAPP_TRACKS_TRIGGER || JLN_NO_TRACK(t))
        return track_colour(a, t);
    if ((a->track_held & (1u << t)) || (a->push && pushed(a, t, -1)))
        return JLAPP_COLOR_TRIG_PLAYING;
    return t == a->n.track ? JLAPP_COLOR_TRIG_CURRENT : JLAPP_COLOR_TRIG_IDLE;
}

void jlapp_set_dn2(struct jlapp *a, int present) { a->dn2_present = present != 0; }

void jlapp_frame(const struct jlapp *a, struct jlx_frame *f)
{
    uint8_t own[16], shift = note_shift(a);
    int chosen_shown = select_shown(a);
    memset(f, 0, sizeof *f);
    if (!a->dn2_present)
        return;                        /* no Digitone: everything dark */
    memset(own, 0, sizeof own);
    for (uint32_t i = 0; i < JLAPP_PADS; i++) {
        const struct jlapp_held *h = &a->held[i];
        if (h->note >= 0 && h->track == a->n.track)
            own[h->note >> 3] |= (uint8_t)(1u << (h->note & 7));
    }
    if (a->mode == JLAPP_MODE_STEP && !a->page) {   /* the step modes draw all 64 pads */
        struct jln n = a->n;
        struct jlst_ctx c = step_ctx(a, &n);
        jlst_frame(&a->step, &c, f);
    } else if (a->mode == JLAPP_MODE_MIXER && !a->page) {   /* so does the mixer */
        jlmx_frame(&a->mix, a->now_ms, f);
    } else if (a->mode == JLAPP_MODE_PALETTE && !a->page) {   /* and the colour chart: the palette index is the colour */
        for (uint8_t y = 0; y < 8; y++)
            for (uint8_t x = 0; x < 8; x++)
                f->led[JLX_PAD0 + y * 8u + x].color = palette_index(a, x, y);
    } else if (a->mode == JLAPP_MODE_SESSION && !a->page) {   /* and the Session page: the patterns */
        jlapp_page_frame(a, f);
    }
    for (uint8_t y = 0; y < 8; y++)
        for (uint8_t x = 0; x < 8; x++) {
            struct jln_pad p;
            int playing;
            if (a->mode != JLAPP_MODE_NOTE && !a->page)
                break;
            if (a->page) {
                f->led[JLX_PAD0 + y * 8u + x].color = page_colour(a, x, y);
                continue;
            }
            if (y < JLAPP_TRACK_ROWS && (shift || chosen_shown)) {
                f->led[JLX_PAD0 + y * 8u + x].color = track_pad_colour(a, track_of_pad(y, x));
                continue;
            }
            jln_pad(&a->n, a->scale, x, (uint8_t)(y - shift), &p);
            playing = p.note >= 0 && ((own[p.note >> 3] >> (p.note & 7)) & 1 || (a->push && pushed(a, a->n.track, p.note)));
            f->led[JLX_PAD0 + y * 8u + x].color = jln_color(&p, playing);
        }
    /* the arrows are green, and dim at the end of what they do (Launchpad 95's Note.Octave): the octave's limits, the first and last track; left and right are
     * amber while FOLLOW is on */
    if (a->mode == JLAPP_MODE_MIXER) {   /* the mixer's up / down are the banks: up goes to tracks 1-8, down to 9-16, dim where there is nothing further */
        f->led[JLX_TOP0 + BUTTON_UP].color = a->mix.bank > 0u ? JLAPP_COLOR_ARROW_ON : JLAPP_COLOR_ARROW_OFF;
        f->led[JLX_TOP0 + BUTTON_DOWN].color = a->mix.bank + 1u < JLMX_BANKS ? JLAPP_COLOR_ARROW_ON : JLAPP_COLOR_ARROW_OFF;
    } else if (a->mode == JLAPP_MODE_SESSION) {   /* the Session page's: up the first half, down the other, as the mixer's */
        f->led[JLX_TOP0 + BUTTON_UP].color = a->sess.half ? JLAPP_COLOR_ARROW_ON : JLAPP_COLOR_ARROW_OFF;
        f->led[JLX_TOP0 + BUTTON_DOWN].color = a->sess.half ? JLAPP_COLOR_ARROW_OFF : JLAPP_COLOR_ARROW_ON;
    } else if (!(a->mode == JLAPP_MODE_STEP && a->step_kind == JLST_DRUM)) {   /* drum step has no octave */
        f->led[JLX_TOP0 + BUTTON_UP].color = a->n.octave + 1u < jln_octaves(&a->n) ? JLAPP_COLOR_ARROW_ON : JLAPP_COLOR_ARROW_OFF;
        f->led[JLX_TOP0 + BUTTON_DOWN].color = a->n.octave > 0u ? JLAPP_COLOR_ARROW_ON : JLAPP_COLOR_ARROW_OFF;
    }
    f->led[JLX_TOP0 + BUTTON_LEFT].color = a->n.track > 0u ? (a->follow ? JLAPP_COLOR_FOLLOW_ON : JLAPP_COLOR_ARROW_ON)
                                                           : (a->follow ? JLAPP_COLOR_FOLLOW_OFF : JLAPP_COLOR_ARROW_OFF);
    f->led[JLX_TOP0 + BUTTON_RIGHT].color = a->n.track + 1u < JLN_TRACKS ? (a->follow ? JLAPP_COLOR_FOLLOW_ON : JLAPP_COLOR_ARROW_ON)
                                                                         : (a->follow ? JLAPP_COLOR_FOLLOW_OFF : JLAPP_COLOR_ARROW_OFF);
    f->led[JLX_TOP0 + BUTTON_NOTE].color = a->mode == JLAPP_MODE_NOTE ? JLAPP_COLOR_MODE : JLAPP_COLOR_MODE_DIM;
    /* A button that does something is always lit: dim when its mode is not on, full when it is (owner 2026-11-24). Custom leads to the step mode last used (melodic at
     * first): deep blue for melodic, yellow for drum (owner 2026-11-25). */
    {
        int on = a->mode == JLAPP_MODE_STEP;
        if (a->step_kind == JLST_MELODIC)
            f->led[JLX_TOP0 + BUTTON_CUSTOM_IDX].color = on ? JLAPP_COLOR_CUSTOM_MELODIC : JLAPP_COLOR_CUSTOM_MELODIC_DIM;
        else
            f->led[JLX_TOP0 + BUTTON_CUSTOM_IDX].color = on ? JLAPP_COLOR_CUSTOM_DRUM : JLAPP_COLOR_CUSTOM_DRUM_DIM;
    }
    /* the mixer's button (Launchpad 95's Mixer: MINT, dim when it is not on), always lit */
    f->led[JLX_TOP0 + BUTTON_CAPTURE_IDX].color = a->mode == JLAPP_MODE_MIXER ? JLMX_COLOR_BUTTON : JLMX_COLOR_BUTTON_DIM;
    f->led[JLX_TOP0 + BUTTON_SESSION_IDX].color = a->mode == JLAPP_MODE_PALETTE || a->mode == JLAPP_MODE_SESSION ? JLAPP_COLOR_PALETTE_ON : JLAPP_COLOR_PALETTE_OFF;   /* white on the page or in the chart, dim grey otherwise */
    if (a->mode == JLAPP_MODE_MIXER) {   /* the scene buttons pick the view; start / stop is the other modes' */
        for (uint8_t i = 0; i < 8u; i++)
            f->led[JLX_SIDE0 + i].color = jlmx_scene_colour(&a->mix, i);
    } else {
        f->led[JLX_SIDE0 + JLAPP_SIDE_START_STOP].color = jltr_lit(&a->tr, a->live_state == JDL_LIVE) ? JLAPP_GREEN : JLAPP_GREEN_THIRD;   /* start / stop (owner 2026-11-25): always green, full while playing (the Digitone's transport while it leads), third when stopped */
        if (a->mode != JLAPP_MODE_SESSION) {   /* the Session page: only start / stop (owner 2026-11-25: "rec and mute(delete/erase) should not be on session") */
            f->led[JLX_SIDE0 + JLAPP_SIDE_DELETE].color = !JLAPP_ERASE ? 0 : a->erase_on ? JLAPP_PINK : JLAPP_PINK_THIRD;        /* delete: bright while held */
            f->led[JLX_SIDE0 + JLAPP_SIDE_RECORD].color = JLAPP_RECORD && a->record_ok ? (a->record_on ? JLAPP_RED : JLAPP_RED_THIRD) : 0;
        }   /* record: red armed / recording, dim red off, dark when the Digitone has none */
    }
}

int jlapp_mixer_wanted(const struct jlapp *a) { return a->mode == JLAPP_MODE_MIXER && a->live_state == JDL_LIVE; }

void jlapp_on_mixer(struct jlapp *a, const struct jdl_mixer *m) { jlmx_on_mixer(&a->mix, m, a->now_ms); }

void jlapp_set_mixer_writes(struct jlapp *a, int ok) { a->mix_writes = ok != 0; }
