/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad's mixer screen on the Digitone (owner 2026-11-24), after Launchpad 95's mixer: each of the 8 pad columns is one track; up / down switch the bank, tracks
 * 1-8 / 9-16. The mixer opens on the OVERVIEW (Capture again comes back to it), and the scene buttons (right side, top to bottom) switch to the sub modes:
 *
 *   1 volume   2 pan   3 delay send   4 reverb send   5 chorus send   6 mute   7 solo   (8: nothing yet)
 *
 * Overview, a pad row per thing, from the top in the scene buttons' order (Launchpad 95: volume, pan, send A, send B, stop, mute, solo, arm): rows 1-5 volume, pan, delay,
 * reverb, chorus, in their colours, a press opens that sub mode; row 6 mute (bright GREEN, dim when muted, as Elektron) and row 7 solo (bright BLUE when soloed, dim when not), a press toggles that
 * track; row 8 dark. Sub modes: volume, delay, reverb and chorus are 8-pad faders with Launchpad 95's value steps (volume dB steps, its send steps; the bottom pad is 0 and
 * lights nothing), pan a bar from the centre pair out to the value. Scene buttons 6 and 7 both show the MUTE / SOLO screen: on / off buttons for all 16 tracks in the lower four
 * pad rows, from the top mute 1-8, mute 9-16, solo 1-8, solo 9-16 (the top four rows dark; the bank does not matter). Colours as Launchpad 95's (SkinMK2.py Mixer.*: MINT, YELLOW,
 * BLUE), except the faders' own colours (owner 2026-11-25, picked on the Session colour chart): volume 25, pan 17, the three sends 50 (purple; scene button 48 on, 51 off). The values come from MIXER
 * (jank_dn2live.h), so the faders move when the Digitone's values do. Mute and solo write at the press (MUTE SET, the Digitone's stage 6).
 *
 * The faders are SLOW FADERS, as Launchpad 95's device mode does it (owner 2026-11-25: "hold long to make fader move slower"; its DeviceControllerStrip with ENABLE_TDC and
 * ParameterSlides.py): a pad pressed is only remembered, and while it is held it steps through ten colours (red, amber, yellow, green, mint, light blue, blue, pink, purple,
 * white), one every 0.2 s. Let go, the fader travels to that pad: the time held (up to 2 s) picks how long a sweep of the whole range takes, 0 0.75 1.5 3 5 8 12 17 25 40 s
 * (TDC_MAP), so a quick tap lands at once and a 2-second hold takes 40 s end to end. The slide writes MIXER SET as it moves (jlmx_slides, every JLMX_SLIDE_TICK_MS), keeps
 * running when the view, the bank or the mode changes, is replaced by a new one on the same track and fader, and stops when the Digitone's value leaves the path it travelled
 * (the Digitone's own knob moved it). Pure; host-tested (tests/test_lpmix.c). */
#ifndef JANK_LPMIX_H
#define JANK_LPMIX_H
#include <stdint.h>
#include "jank_dn2live.h"
#include "jank_lpunit.h"
#include "jank_lpx.h"

#define JLMX_COLUMNS 8u
#define JLMX_BANKS ((JLN_TRACKS + JLMX_COLUMNS - 1u) / JLMX_COLUMNS)   /* tracks 1-8, 9-16 (the unit's: up to 8 a bank) */
#define JLMX_READ_MS 300u              /* the firmware reads the mixer this often while the screen is on (the answer is 106 bytes) */

#define JLMX_COLOR_BUTTON 29u          /* MINT: the Capture button, the mixer's mode button (Launchpad 95's Mixer colour) */
#define JLMX_COLOR_BUTTON_DIM 31u      /* MINT_THIRD */
#define JLMX_COLOR_VOL 25u             /* the owner's picks (Session chart, first page, row 4 col 2 / row 3 col 2 / row 8 col 5); the empty part of a fader is dark */
#define JLMX_COLOR_PAN 17u
#define JLMX_COLOR_SEND 50u            /* delay, reverb and chorus: purple (owner 2026-11-25, chart row 7 col 3; was 60, which showed red on the scene button) */
/* the view buttons (scene 1-5) in their fader's colour (owner 2026-11-25: "the scene button color must match their relative faders"): full (the fader's own) while
 * the view is shown, the same colour's third otherwise (the palette's groups of four are light, full, half, third: RED 4 5 6 7; 60 is the light one of 60..63) */
#define JLMX_COLOR_VOL_DIM 27u
#define JLMX_COLOR_PAN_DIM 19u
#define JLMX_COLOR_SEND_DIM 51u        /* ... a send's scene button when its view is not shown (chart row 7 col 4) */
#define JLMX_COLOR_SEND_ON 48u         /* ... and when it is (chart row 7 col 1; first 49, row 7 col 2): the sends' own pick, not the fader's colour */
#define JLMX_COLOR_UNMUTED 21u         /* GREEN (owner 2026-11-25: "use green for mute (this is what elektron uses)"; was Launchpad 95's YELLOW) */
#define JLMX_COLOR_MUTED 23u           /* GREEN_THIRD */
#define JLMX_COLOR_SOLO 45u            /* BLUE */
#define JLMX_COLOR_NOSOLO 47u          /* BLUE_THIRD */

enum jlmx_view { JLMX_VOL, JLMX_PAN, JLMX_DEL, JLMX_REV, JLMX_CHR, JLMX_MUTE, JLMX_SOLO, JLMX_VIEWS };
#define JLMX_OVERVIEW JLMX_VIEWS       /* the page the mixer opens on: not a scene button's view */

#define JLMX_HOLD_STEPS 10u            /* Launchpad 95: TDC_MAP's ten sweep times, and the ten colours of a held pad */
#define JLMX_HOLD_STEP_MS 200u         /* ... one every TDC_MAX_TIME (2 s) / 10 */
#define JLMX_SLIDE_TICK_MS 100u        /* a slide moves this often (Launchpad 95: Live's update_display) */
#define JLMX_SLIDE_FIRST_MS 50u        /* ... and its first step (RATE_PERIOD) is taken at the release */
#define JLMX_SLIDES 16u                /* slides at once; a 17th replaces the oldest */
#define JLMX_HOLD_MS 400u              /* after a press the Digitone's older reading is not allowed to put the fader back (its write applies at its next UI pass) */

struct jlmx_held {                     /* a fader pad held (one per column: a new press replaces it) */
    uint8_t on, y, track, view;
    uint32_t ms;                       /* when it went down */
};

struct jlmx_slide {
    uint8_t on, track, view;
    uint8_t from, target, sent;        /* the value at the release, the pad's value, the last one written */
    uint32_t start_ms, sweep_ms;       /* the slide moves 127 per sweep_ms from start_ms */
    uint32_t serial;                   /* the oldest is replaced when all are taken */
};

struct jlmx {
    uint8_t view;                      /* enum jlmx_view */
    uint8_t bank;                      /* 0: tracks 1-8, 1: tracks 9-16 */
    uint8_t have;                      /* a MIXER answer has come */
    uint8_t held;                      /* a press changed the local copy at hold_ms */
    uint32_t hold_ms;
    struct jdl_mixer m;
    struct jlmx_held hold[JLMX_COLUMNS];
    struct jlmx_slide slide[JLMX_SLIDES];
    uint32_t slide_serial, tick_ms;
};

/* what a press asks the Digitone to write (jlmx_press) */
enum jlmx_write_kind { JLMX_WRITE_NONE, JLMX_WRITE_VALUE, JLMX_WRITE_MASK };
struct jlmx_write {
    uint8_t kind;                      /* enum jlmx_write_kind */
    uint8_t track;                     /* JLMX_WRITE_VALUE: the track, 0..15 */
    uint8_t param;                     /* ... enum jdl_mix_param */
    uint8_t value;                     /* ... 0..127 */
    uint8_t which;                     /* JLMX_WRITE_MASK: 0 mute, 1 solo */
    uint16_t mask;                     /* ... the whole new mask (bit t = track t + 1) */
};

void jlmx_init(struct jlmx *s);
/* the Digitone's answer (jdl_take_mixer); within JLMX_HOLD_MS of a press it is not taken (the fader would jump back and forth), the next one is */
void jlmx_on_mixer(struct jlmx *s, const struct jdl_mixer *m, uint32_t now_ms);
/* A pad (x 0..7, y 0..7 from the bottom) pressed. Mute / solo (the 4-row screen, and the overview's rows 6 and 7): the pad's track toggles, 1 and *w filled, the local copy
 * changed at once. A fader pad (volume and the sends: the pad's step, the bottom pad 0; pan: the 8 positions, the two middle ones the centre) is only held: 0, the write comes
 * at its release (jlmx_release). 0 too when there is no answer from the Digitone yet, or for an overview pad that opens a view (jlmx_open) */
int jlmx_press(struct jlmx *s, uint8_t x, uint8_t y, uint32_t now_ms, struct jlmx_write *w);
/* The held fader pad (x, y) let go: the slide to its value starts (the time held picks its speed) and its first step is due now: 1 and *w filled (the local copy moved);
 * 0 for a pad that was not the column's held one */
int jlmx_release(struct jlmx *s, uint8_t x, uint8_t y, uint32_t now_ms, struct jlmx_write *w);
/* the slides' writes due now (at most one per slide, every JLMX_SLIDE_TICK_MS): how many filled in w (up to max); the local copy moves with them */
size_t jlmx_slides(struct jlmx *s, uint32_t now_ms, struct jlmx_write *w, size_t max);
void jlmx_stop_slides(struct jlmx *s);  /* (live control or the mixer writes are gone) */
/* The overview's pad row y (from the bottom) pressed: rows 1-5 from the top open that sub mode (the view is set), 1; anything else, or not on the overview: 0 */
int jlmx_open(struct jlmx *s, uint8_t y);
/* back to the overview (Capture) */
void jlmx_overview(struct jlmx *s);
/* a scene button (index 0 = the top one): 1 and the view is picked if it is one of the views, else 0 */
int jlmx_scene(struct jlmx *s, uint8_t idx);
/* up (+1) / down (-1): the bank; 1 when it changed, 0 at the limit */
int jlmx_bank_step(struct jlmx *s, int dir);
/* the 64 pads of the screen (led[JLX_PAD0 ..]), a held fader pad in its hold colour at now_ms; nothing known yet: dark */
void jlmx_frame(const struct jlmx *s, uint32_t now_ms, struct jlx_frame *f);
/* the colour of scene button idx: a view button its fader's colour while its view is shown, that colour's third otherwise; mute GREEN, solo BLUE (full when their screen is shown);
 * 0 for a button with no job */
uint8_t jlmx_scene_colour(const struct jlmx *s, uint8_t idx);
#endif
