/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad's two step sequencer modes on the Digitone (owner 2026-11-24): MELODIC (one track, a keyboard) and DRUM (16 tracks to pick from). Same layout, after
 * Launchpad 95's drum step sequencer (its lower-left 4x4 note selector, top 4x8 sequence, lower-right 4x4 loop selector):
 *
 *   rows 8..5 (top 4)    32 steps, as the Digitone shows them: steps 1-8 on the top row, 9-16 below it (one page of 16), then the next page on the two rows below.
 *                        A press toggles the step's trig. A step that holds a trig is GREEN, one without DARK_GREY, one past the pattern's length dark; the step the
 *                        sequencer just played (from the Digitone's note push, 150 ms) WHITE.
 *   rows 4..1, left 4x4  DRUM: the 16 tracks, reading order (track 1 top left): a press selects the track the steps show and edit. MELODIC: a 4x4 keyboard (the
 *                        Note mode's layout, scale and octave): a press selects the note a step press places. Either way the pad also PLAYS (the track's own note /
 *                        the key) while the transport is STOPPED, and does not while it plays (the owner's rule); the start / stop scene button is the Note mode's.
 *   rows 4..1, right 4x4 page pads on the top row of the quadrant (4 pages of 16 = the Digitone's 64 steps): the page the top rows start at. The rest is free.
 *
 * Melodic step press: with no key picked yet, as the drum one (the trig with the track's own note); with a key picked, a step with that note is cleared, any other step
 * gets the trig with that note (STEP op 2). Velocity and length stay on the Digitone. The pages come from the Digitone (STEPS), a press is shown at once (the next read
 * does not overwrite it for 300 ms). Pure; host-tested (tests/test_lpstep.c). */
#ifndef JANK_LPSTEP_H
#define JANK_LPSTEP_H
#include <stddef.h>
#include <stdint.h>
#include "jank_dn2live.h"
#include "jank_lpact.h"
#include "jank_lpnote.h"
#include "jank_lpx.h"

#define JLST_PAGES 4u                  /* of 16 steps: the Digitone's 64 */
#define JLST_HEAD_MS 150u              /* the step that just played is lit this long */
#define JLST_STALE_MS 600u             /* after a track change the old track's steps stay drawn this long, until the new track's pages come (no dark flash) */
#define JLST_EDIT_HOLD_MS 300u        /* a page read does not overwrite what a press just changed for this long */
#define JLST_KEYS 16u                  /* the left quadrant */

/* Launchpad 95-style palette indexes */
#define JLST_COLOR_ON 21u              /* GREEN: a step with a trig */
#define JLST_COLOR_OFF 1u              /* DARK_GREY: a step without */
#define JLST_COLOR_HEAD 3u             /* WHITE: the step that just played, the picked key */
/* the drum quadrant, as Launchpad 95's drum rack in its step sequencer (SkinMK2.py DrumGroup.PadFilled = YELLOW, StepSequencer.NoteSelector Selected = GREEN, Playing =
 * RED): every track yellow, the track the steps show GREEN, a track pad that is down (auditioning) RED */
#define JLST_COLOR_TRACK_IDLE 13u      /* YELLOW */
#define JLST_COLOR_TRACK_CURRENT 21u   /* GREEN */
#define JLST_COLOR_TRACK_PLAYING 5u    /* RED */
#define JLST_RT_ALIVE_MS 500u          /* the Digitone's clock counts as running this long after its last tick */
#define JLST_TICKS_PER_STEP 6u         /* MIDI clock: 24 a beat, a step is a sixteenth */
#define JLST_COLOR_PLAYHEAD 45u       /* BLUE: the playhead on a step without a trig; RED (5) on a step that plays */
#define JLST_COLOR_PLAYHEAD_NOTE 5u
#define JLST_COLOR_PAGE_ON 21u        /* a page shown: GREEN; another page that exists GREEN_THIRD */
#define JLST_COLOR_PAGE_OFF 23u

enum jlst_kind { JLST_MELODIC, JLST_DRUM };

struct jlst {
    uint8_t kind;                      /* enum jlst_kind */
    uint8_t first_page;                /* the top rows show pages first_page and first_page + 1 */
    int16_t note;                      /* melodic: the key a step press places; -1: none picked */
    uint8_t pages_known;               /* the pattern's pages (of the track last read), 0 until a read says */
    uint8_t have[2];                   /* pg[i] holds page first_page + i, of the track pg[i].track */
    struct jdl_page pg[2];
    uint32_t pages_ms;                 /* the last page accepted */
    uint32_t edit_ms;                  /* a press changed pg at edit_ms (edit_on) */
    uint8_t edit_on;
    int16_t key_note[JLST_KEYS];       /* melodic keyboard pads held: the note they play (-1: up) */
    uint8_t key_track[JLST_KEYS];
    uint16_t track_held;               /* drum: a bit for each track pad that auditions */
    uint8_t head_on, head_track, head_step;   /* the step that played last (absolute step number): shown white while the clock is not known */
    uint32_t head_ms;
    uint8_t run;                       /* the transport runs and the clock is known (jlst_set_clock) */
    uint32_t tick16;                   /* the clock in sixteenth notes since the session's start */
    uint8_t offset;                    /* what a pushed note taught: the Digitone's step = (the clock in sixteenths + offset) mod length */
    uint8_t offset_src;                /* ... for which clock (1 Jank's session beat, 2 the Digitone's own MIDI clock): another source starts from 0 */
    uint8_t rt_run;                    /* the Digitone sent MIDI Start (or Continue) and not Stop since */
    uint32_t rt_ticks;                 /* its MIDI clock ticks since its Start (24 a beat, 6 a sixteenth) */
    uint32_t rt_last_ms;               /* its last clock tick: the clock is alive for JLST_RT_ALIVE_MS after it */
    uint32_t rt_clocks, rt_starts, rt_stops;   /* counted for diag */
};

/* what the pad logic needs from the rest of the Launchpad app */
struct jlst_ctx {
    struct jln *n;                     /* the layout, octave and the CURRENT TRACK (a drum track pad sets it) */
    const struct jln_scale *scale;
    uint8_t can_play;                  /* live control with PLAY: a quadrant pad may play */
    uint8_t transport_playing;         /* the clock runs: the quadrant pads only select */
    uint32_t now_ms;
    const struct jlapp_recent *recent; /* what the Digitone played lately (jlapp_on_push): the quadrant flashes it */
    unsigned recent_n;
    uint8_t deleting;                  /* the Delete scene button is held (owner 2026-11-24): a step pad press clears that step, whatever it held, and never sets one */
};

void jlst_init(struct jlst *s, enum jlst_kind kind);

/* a pad (x 0..7 left to right, y 0..7 from the bottom) pressed (value > 0) or released: acts for the Digitone, at most max (2 at most are made: a drum track pad
 * selects and plays). Returns how many. A press with no room (max < 2 for a drum pad) changes nothing */
size_t jlst_pad(struct jlst *s, const struct jlst_ctx *c, uint8_t x, uint8_t y, uint8_t value, struct jlapp_act *out, size_t max);

/* which pages of the current track to read now: pages[0..n); the first page is always one that exists once the pattern's length is known (page 0 before that) */
size_t jlst_wanted(const struct jlst *s, uint8_t pages[2]);
/* 1 while a page to show is missing or belongs to another track than `track` (it was just switched): the firmware then reads at once, not at its next tick */
int jlst_urgent(const struct jlst *s, uint8_t track);
/* a page the Digitone answered (jdl_take_page); `track` is the current track: a page of another track is dropped. The steps on the pads are the previous track's until
 * its pages come (jlst_frame draws them for JLST_STALE_MS), but a press only acts on pages of the current track */
void jlst_on_page(struct jlst *s, uint8_t track, const struct jdl_page *pg, uint32_t now_ms);
/* a pushed note of the current track played step `step` (absolute, 0..127) */
void jlst_on_push(struct jlst *s, uint8_t track, uint8_t step, uint32_t now_ms);
/* The playhead (owner 2026-11-24: like Launchpad 95's, on the step rows): Jank is the clock, so it knows where the sequencer is: tick16 = the session's beat x 4 (sixteenth
 * notes); the Digitone's own playing-step byte is wrong on firmware 1.12. The step is (tick16 + offset) mod the pattern's length; a pushed note (its step is exact) corrects
 * the offset. BLUE on a step with no trig, RED on one that plays (Launchpad 95's metronome and playing colours). Not running: only the step that just played, white */
void jlst_set_clock(struct jlst *s, int running, uint32_t tick16);
/* The Digitone's own MIDI transport (owner 2026-11-24: "the playhead is not shown" when the sequencer was started on the Digitone, not from Jank): a real-time byte it sent
 * (0xFA Start, 0xFB Continue, 0xFC Stop, 0xF8 clock). While its clock ticks the playhead follows IT (6 ticks a sixteenth from its Start), whoever started it; else Jank's
 * session beat (jlst_set_clock). Read-only and channel-free: real-time bytes carry no channel. */
void jlst_on_rt(struct jlst *s, uint8_t byte, uint32_t now_ms);

/* the pads held end (the Launchpad went away or the mode changed): acts, at most max */
size_t jlst_release(struct jlst *s, struct jlapp_act *out, size_t max);

/* the 64 pads of the mode (led[JLX_PAD0 ..]) */
void jlst_frame(const struct jlst *s, const struct jlst_ctx *c, struct jlx_frame *f);
#endif
