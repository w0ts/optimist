/* SPDX-License-Identifier: GPL-3.0-only */
/* Launchpad X -> Digitone II, Note mode: what a pad press asks of the Digitone, what the buttons do, what the pads show, and the short messages that say what a
 * press did. Pure: it takes decoded Launchpad events and the Digitone's live-control news, and gives back acts (jlapp_act), an LED frame (jank_lpx.h) and
 * messages; the USB side is firmware/main. Host-tested (tests/test_lpapp.c). Another unit (the FM-1) builds it with its own configuration (jank_lpunit.h: its
 * tracks, the clips page) and an adapter of its own: docs/launchpad.md, "Using the core on another unit" (tests/test_lpunit.c).
 *
 * SysEx only (owner 2026-11-24): the Digitone is played and told what track to use through its live control (jank_dn2live.h, SETTINGS > SYSTEM > USB CONFIG >
 * REMOTE CONTROL ON), never through MIDI channels, which depend on how the Digitone is set up and would mix with the MIDI it gets otherwise. Without live
 * control, or without its PLAY command, the pads play nothing and say why (a message).
 *
 * Note mode: a pad plays its note on the current track (PLAY); up / down = octave; left / right = the previous / next track, one at a time, no wrap, and the
 * Digitone's active track follows. For JLAPP_SELECT_MS after a track press the 16 bottom pads show the track, laid out as the Digitone's trig keys: tracks 1..8 in the upper of the two rows, 9..16 in the lower, the chosen
 * one white. Lights come from the Digitone's own note push: a pad lights while its pitch on the current track (a track pad: its track) sounded in the last
 * JLAPP_PUSH_LIGHT_MS, and a pad that is down is lit; every pad with the same pitch lights (the layouts overlap).
 *
 * Left and right together (owner 2026-11-24) switch FOLLOW: the Launchpad's current track follows the Digitone's (read every JLAPP_FOLLOW_MS, jdl_track_read) as
 * well as the other way round; left and right then show in AMBER. The two presses make no track step. Not available (a message) while the Digitone cannot be asked.
 *
 * Scale page (hold Note): Launchpad 95's scale editor instead of notes, rows from the top: 1 the absolute-root and horizontal toggles (pads 1, 2), fixed velocity (pad 3), the layouts
 * (pads 5 chords, 6 diatonic, 7 chromatic) and the TRACK PADS switch (pad 8); 2 the black keys, the relative scale (pad 3) and the circle of fifths back (pad 7);
 * 3 the white keys C to B and the circle of fifths forward (pad 8); 4 the scale pages (three amber pads); 5 and 6 the scales (jank_lpscales.h); 7 and 8 the 16 tracks, a press
 * SELECTS one (owner 2026-11-24: the octave row is gone, up / down do the octave): tracks 1-8 on row 7, 9-16 on row 8, WHITE, the selected one AMBER. Releasing Note goes back to playing.
 * TRACK PADS (owner 2026-11-25: "should cycle between: on/trigger/select"): pad 8 cycles OFF -> TRIGGER -> SELECT. With them on, the two bottom rows are the 16 tracks
 * and the note layout moves up to the six rows left. TRIGGER: a pad plays its track on its own default note (PLAY's op 2), lit as the drum quadrant (YELLOW, the
 * current track GREEN, one that sounds RED). SELECT: a pad selects its track (as the scale page's track rows), lit WHITE, the current track AMBER, one that sounds RED.
 *
 * Messages (what the Launchpad 95 shows in Live's status bar): each change of the scale page, each octave and track step, FOLLOW, and a press that cannot play
 * leave a message of at most JLAPP_MSG_MAX - 1 characters, upper case, 7-bit; the newest replaces an unread one. The firmware shows it on Jank's own screen if
 * it has one, else on the Digitone. */
#ifndef JANK_LPAPP_H
#define JANK_LPAPP_H
#include <stddef.h>
#include <stdint.h>
#include "jank_dn2live.h"
#include "jank_lpact.h"
#include "jank_lpmix.h"
#include "jank_lpnote.h"
#if JLAPP_CLIPS
#include "jank_lpclips.h"               /* the Session page: the unit's clips (jank_lpunit.h) */
#else
#include "jank_lpsess.h"                /* the Session page: the Digitone's patterns */
#endif
#include "jank_lpstep.h"
#if !JLAPP_CLIPS
#include "jank_lptrans.h"               /* the Digitone's transport (another unit has its own) */
#endif
#include "jank_lpx.h"

/* The colours are Launchpad 95's (SkinMK2.py), as palette indexes (manual p. 12) under the names its ColorsMK2.py gives them; "on" is the bright one and "off"
 * its THIRD: the scale page's Horizontal and Key are green, AbsoluteRoot, Mode (the layouts) and Octave are red, Modus (the scales) is blue, and the relative
 * scale and the circle of fifths are plain BLUE; Note mode's arrows (Note.Octave) are green, dim at a limit, and the Note mode button (Mode.Note) LIGHT_BLUE. */
#define JLAPP_GREEN 21u
#define JLAPP_GREEN_THIRD 23u
#define JLAPP_RED 5u
#define JLAPP_RED_THIRD 7u
#define JLAPP_BLUE 45u
#define JLAPP_BLUE_THIRD 47u
/* Custom, by the step mode it leads to (owner 2026-11-25: "yellow for drum mode, deep blue for note mode"): full while that step mode is on, third otherwise */
#define JLAPP_COLOR_CUSTOM_MELODIC JLAPP_BLUE
#define JLAPP_COLOR_CUSTOM_MELODIC_DIM JLAPP_BLUE_THIRD
#define JLAPP_COLOR_CUSTOM_DRUM 13u                  /* YELLOW */
#define JLAPP_COLOR_CUSTOM_DRUM_DIM 15u              /* YELLOW_THIRD */
#define JLAPP_AMBER 9u
#define JLAPP_AMBER_THIRD 11u
#define JLAPP_COLOR_FIXED_VEL JLAPP_AMBER            /* the Note page's pad 3, fixed velocity on (my pick, not the owner's yet) ... */
#define JLAPP_COLOR_FIXED_VEL_OFF JLAPP_AMBER_THIRD  /* ... and off */

#define JLAPP_PADS 64u
#define JLAPP_SCALES_PER_PAGE 16u                    /* the scale page shows the unit's scales two rows of eight at a time, on pages picked by pads on the row above them (the Digitone's 36: three) */
#define JLAPP_TRACK_ROWS ((JLN_TRACKS + 7u) / 8u)    /* the track pads: the bottom rows, 8 tracks a row (the Digitone's 16: two) */
#define JLAPP_SELECT_MS 1000u                        /* the bottom 16 pads show the chosen track this long after left / right */
#define JLAPP_FOLLOW_MS 250u                         /* FOLLOW asks the Digitone for its active track this often */
#define JLAPP_FOLLOW_HOLD_MS 500u                    /* ... and does not trust an answer this long after the Launchpad chose a track (the Digitone takes a select at its next UI pass) */
#define JLAPP_MSG_MAX 21u                            /* a message, with its NUL: 20 characters (the Digitone's MESSAGE command takes that many) */
/* The track pads (the 16 bottom pads while the track is shown, and the track pads mode), owner 2026-11-24: not blue (it reads as the scale's root) and not green or purple (the
 * Digitone's own mute modes): every track WHITE, the selected one AMBER, a track that is playing (or held) RED. */
#define JLAPP_COLOR_TRACK_IDLE 3u                    /* WHITE */
#define JLAPP_COLOR_TRACK_CURRENT 9u                 /* AMBER */
#define JLAPP_COLOR_TRACK_PLAYING 5u                 /* RED */
/* the track pads in TRIGGER mode (owner 2026-11-25: "yellow (like drum sequencer yellow)"): the drum quadrant's colours (jank_lpstep.h JLST_COLOR_TRACK_*) */
#define JLAPP_COLOR_TRIG_IDLE 13u                    /* YELLOW */
#define JLAPP_COLOR_TRIG_CURRENT 21u                 /* GREEN */
#define JLAPP_COLOR_TRIG_PLAYING 5u                  /* RED */
enum jlapp_tracks { JLAPP_TRACKS_OFF, JLAPP_TRACKS_TRIGGER, JLAPP_TRACKS_SELECT, JLAPP_TRACKS_MODES };

/* the arrow buttons' and the mode button's palette indexes: Note.Octave On / Off, and Mode.Note On (LIGHT_BLUE) */
#define JLAPP_COLOR_ARROW_ON JLAPP_GREEN             /* a direction that can still be taken */
#define JLAPP_COLOR_ARROW_OFF JLAPP_GREEN_THIRD      /* at the limit: the lowest or highest octave, the first or last track */
#define JLAPP_COLOR_FOLLOW_ON JLAPP_AMBER            /* left / right while FOLLOW is on; dim at the first or last track */
#define JLAPP_COLOR_FOLLOW_OFF JLAPP_AMBER_THIRD
#define JLAPP_COLOR_MODE 37u
#define JLAPP_COLOR_MODE_DIM 39u            /* the Note button while a step mode is on: LIGHT_BLUE_THIRD */

/* the hosted devices that matter here, told apart by their USB product string (jank's: another unit tells its devices apart its own way, a Launchpad with jlx_model_find) */
enum jlapp_role { JLAPP_OTHER, JLAPP_LPX, JLAPP_DN2 };
#if !JLAPP_CLIPS
enum jlapp_role jlapp_role_from_name(const char *product);
/* which of a device's USB-MIDI streaming interfaces is the one to use (0 = the first). The Launchpad X has only one (diag ifs:1: "DAW" and "MIDI" are its two
 * CABLES, not two interfaces), so 1 is clamped to that one; the cable matters, not the interface: usb_lp.c sends on cable 1, the MIDI port */
unsigned jlapp_midi_interface_index(enum jlapp_role r);
#endif

struct jlapp_held {
    int16_t note;                      /* -1: the pad is up */
    uint8_t track;                     /* the track it was played on: its end goes there, whatever the track is by then */
};

/* Modes (owner 2026-11-24): the Note button (a tap, shorter than JLAPP_TAP_MS) is Note mode; Custom goes to the step modes and cycles melodic <-> drum
 * (jank_lpstep.h); held, the Note button is the scale page in every mode. Scene button 5 (side index 4) starts / stops the transport, as Launchpad 95's Note mode (the Digitone's or Jank's session: jank_lptrans.h).
 * Up / down are the octave in Note mode and in melodic step, nothing in drum step. */
enum jlapp_mode { JLAPP_MODE_NOTE, JLAPP_MODE_STEP, JLAPP_MODE_MIXER, JLAPP_MODE_PALETTE, JLAPP_MODE_SESSION };
/* The Session button (owner 2026-11-25): a short press (let go within JLAPP_SESSION_HOLD_MS) is the Session page (jank_lpsess.h: the Digitone's patterns; again: back to the
 * mode it came from); HELD for JLAPP_SESSION_HOLD_MS it opens the colour chart (owner 2026-11-24: "put all the colors in there, it will help us to pick colors"), at once
 * while still held: the Launchpad X's 128 palette indexes on the 64 pads, two pages, indexes 0-63 (reading order, 0 top left); in the chart a Session press shows 64-127,
 * the next goes back to the mode it came from; a pad pressed says its index (COLOUR 51). Note, Custom and Capture leave both as always. Session is lit: WHITE on the page or
 * in the chart, DARK_GREY (dim) otherwise. On the Session page the scene buttons are Note mode's (5 start / stop, 6 Delete, 8 Record; the rest dark): the page has no use
 * for them, and they stay where the hand knows them (the mixer is the one page that takes them over, for its views). */
#define BUTTON_SESSION_IDX 4u
#define JLAPP_SESSION_HOLD_MS 1000u
#define JLAPP_PALETTE_PAGES 2u
#define JLAPP_COLOR_PALETTE_ON 3u
#define JLAPP_COLOR_PALETTE_OFF 1u
#define BUTTON_CAPTURE_IDX 7u               /* the top button that enters the mixer (Launchpad 95's mixer button is the last one too) */
#define JLAPP_TAP_MS 400u
#define JLAPP_SIDE_START_STOP 4u            /* the side buttons, top (scene 1) = 0: scene 5 */
#define JLAPP_SIDE_DELETE 5u                /* scene 6 (owner 2026-11-24): held, the Digitone erases the track's steps as its playhead passes; Note and step modes (the mixer's scene 6 is a view).
                                             * Scene 8 will be Record, lit once the Digitone has a record command (dn2-wishes stage 7) */
#define JLAPP_SIDE_RECORD 7u                /* scene 8 (owner 2026-11-24, red): the Digitone's own live record, RECORD 58 (dn2-wishes stage 7); Note and step modes. RED when armed or recording, RED_THIRD
                                             * when not, dark on a Digitone that has no RECORD. It does not start the sequencer: it arms, and recording runs while the sequencer plays */
#define JLAPP_RECORD_HOLD_MS 1000u          /* after a press the Digitone's older LIVE STATUS is not allowed to put the light back */
#define JLAPP_PINK 53u                     /* the Delete button: PINK held, PINK_THIRD otherwise (my pick, the owner asked for one) */
#define JLAPP_PINK_THIRD 55u
#define BUTTON_CUSTOM_IDX 6u

/* (the small fields first, the arrays and the modes' own structs last: short offsets make shorter code on the 32-bit units) */
struct jlapp {
    struct jln n;
    uint8_t mode;                      /* enum jlapp_mode */
    uint8_t page;                      /* the Note button is held: the pads are the scale page */
    uint8_t live_state;                /* enum jdl_state, as the firmware last told (jlapp_set_live) */
    uint8_t push;                      /* live control is on: the lights come from its note push */
    uint8_t can_play;                  /* live control is on and PLAY is there: a press can play */
    uint8_t play_ok;                   /* PLAY has not been refused */
    uint8_t read_ok;                   /* the Digitone can be asked for its active track */
    uint8_t follow;                    /* FOLLOW is on */
    uint8_t track_pads;                /* enum jlapp_tracks: the two bottom rows are the 16 tracks, to trigger or to select (page switch, pad 8) */
    uint8_t step_kind;                 /* enum jlst_kind: the step mode Custom goes to (and the one last used) */
    uint8_t transport;                 /* the clock runs (set by the firmware) */
    uint8_t beat_ok;                   /* the session's beat is known (jlapp_set_beat) */
    uint8_t scale_page;                /* which 24 scales the page shows (0: 1-24, 1: 25-36); the page of the scale in use when the page opens or the scale changes */
    uint8_t minor_idx;                 /* the minor scale the relative pad returns to (jank_lpscales.h index) */
    uint8_t note_used;                 /* a page pad was pressed while the Note button was held: its release is not a tap */
    uint8_t buttons;                   /* bit 0 left, bit 1 right: held (for the two together) */
    uint8_t before[2];                 /* the track before the left / right press (the pair puts it back) */
    uint8_t select_on;                 /* the bottom pads show the track since select_ms */
    uint8_t hold_on;                   /* the Launchpad itself chose a track at hold_ms */
    uint8_t why_on;                    /* "why the pads do not play" was said at why_ms */
    uint8_t erase_on, erase_track;     /* the Delete button is held, and the track it erases (the current track when it went down) */
    uint8_t palette_page, palette_prev;   /* the colour chart's page (0: indexes 0-63, 1: 64-127) and the mode Session came from */
    uint8_t sess_prev;                 /* the mode the Session page came from */
    uint8_t sess_down;                 /* Session is down since sess_down_ms, and has not opened the chart yet */
    uint8_t dn2_present;               /* jlapp_set_dn2: 0 = no Digitone, the Launchpad dark */
    uint8_t mix_writes;                /* the Digitone takes MIXER SET / MUTE SET (not refused by a build without them) */
    uint8_t msg_new;
    uint8_t recent_head;
    uint8_t record_ok;                 /* the Digitone has RECORD (not refused by a build without it) */
    uint8_t record_on;                 /* the Record button is lit: armed or recording (the Digitone's LIVE STATUS, or the press until it answers) */
    uint8_t record_hold;               /* a press at record_ms: LIVE STATUS is not taken for JLAPP_RECORD_HOLD_MS */
    uint8_t fixed_vel;                 /* fixed velocity (the Note page's pad 3, owner 2026-11-25): the pads' notes play at the track's own VEL, not the press */
    uint8_t no_vel;                    /* the Launchpad's pads send no velocity (Mini MK3, MK2: always 127): fixed velocity whatever fixed_vel says */
    uint8_t dn2_seen;                  /* the Digitone's scale has been read since live control came up */
    uint8_t dn2_idx, dn2_root, dn2_has_root;   /* ... and what it said last: only a CHANGE of it is taken, so a scale chosen here is not undone by an unchanged reading */
#if !JLAPP_CLIPS
    uint8_t pat_reads_ok, pat_cue_ok;  /* the Digitone takes PATTERNS / PATTERN (not refused) */
    uint8_t chain_ok, live7;           /* ... CHAIN (not refused); its LIVE STATUS is protocol 7 (the patterns and the transport come with it) */
#endif
    uint16_t track_held;               /* a bit for each track pad that is down */
    const struct jln_scale *scale;
    uint32_t now_ms, select_ms, hold_ms, why_ms;
    uint32_t sess_down_ms;
    uint32_t note_down_ms;
    uint32_t tick16;                   /* the session's beat, in sixteenth notes */
    uint32_t record_ms;
    uint32_t record_busy_seen;         /* the Digitone's count of "GRID / STEP RECORDING is on" refusals already said */
    char msg[JLAPP_MSG_MAX];
#if JLAPP_CLIPS
    struct jlcl sess;                  /* the Session page: the unit's clips */
#else
    struct jltr tr;                    /* who starts and stops: the Digitone or Jank (jank_lptrans.h; another unit: its own transport) */
    struct jlss sess;                  /* the Session page: the Digitone's patterns */
#endif
    struct jlapp_held held[JLAPP_PADS];
    struct jlapp_recent recent[JLAPP_RECENT];
    struct jlmx mix;
    struct jlst step;
};

/* `scale` is one of jank_lpscales.h's (the page marks it) or any other */
void jlapp_init(struct jlapp *a, const struct jln_scale *scale);

/* the clock the select overlay and the push lights run on: call before the other calls with the current time (a wrap of the 32 bits is fine). Session held for
 * JLAPP_SESSION_HOLD_MS opens the colour chart here (with its message): call it often while the Launchpad is up */
void jlapp_tick(struct jlapp *a, uint32_t now_ms);

/* what live control is: `state` an enum jdl_state, `play_ok` PLAY has not been refused, `read_ok` the active track can be read (jdl_can_read_track). Live
 * and PLAY there: a press plays; live: the lights come from the push, FOLLOW can run */
void jlapp_set_live(struct jlapp *a, int state, int play_ok, int read_ok);

/* The Digitone's scale (owner 2026-11-24: synced both ways; jdl_take_scale): the first reading after live control came up, and then every CHANGE of its reading, becomes the
 * Launchpad's scale (and, when the Digitone says it, the root = the key); the scale page shows the page that holds it. A reading that has not changed leaves what the
 * Launchpad chose alone (a Digitone that refuses SCALE SET keeps its own and the Launchpad its own, until the Digitone changes). The other way round: a scale, key,
 * relative-scale or fifths press on the scale page comes out as JLAPP_ACT_SCALE. `idx` is 0..35; `has_root` 0 below protocol 5. */
void jlapp_on_dn2_scale(struct jlapp *a, unsigned idx, int has_root, unsigned root);

/* a Launchpad pad or button; what it asks of the Digitone comes out as acts, at most `max` (one per event); returns how many. A pad press that finds no
 * room (max 0) changes nothing */
size_t jlapp_on_lpx_acts(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max);

/* the notes the Digitone pushed (jdl_input): they light for JLAPP_PUSH_LIGHT_MS */
void jlapp_on_push(struct jlapp *a, const struct jdl_note *notes, size_t n);

/* FOLLOW is on: the Digitone's active track (jdl_take_active). The Launchpad's current track becomes it, shown on the bottom pads, unless the Launchpad chose
 * a track in the last JLAPP_FOLLOW_HOLD_MS */
void jlapp_on_active_track(struct jlapp *a, unsigned track);
/* 1 while FOLLOW is on and can run: the firmware then asks the Digitone for its active track every JLAPP_FOLLOW_MS (jdl_track_read) */
int jlapp_following(const struct jlapp *a);

/* the clock runs (the step modes' lower quadrant then only selects instead of playing; the start / stop scene button lights) */
void jlapp_set_transport(struct jlapp *a, int playing);
/* the session's beat in sixteenth notes (beat x 4), `known` 0 outside a session: the step modes' playhead runs on it while the transport runs (jank_lpstep.h) */
void jlapp_set_beat(struct jlapp *a, int known, uint32_t tick16);
/* a real-time byte (Start / Continue / Stop / clock) from the Digitone's USB MIDI: the playhead follows its own transport while its clock ticks (jank_lpstep.h) */
#if !JLAPP_CLIPS                       /* (jank's: another unit's playhead runs on jlapp_set_beat) */
void jlapp_on_dn2_rt(struct jlapp *a, uint8_t byte);
#endif
/* The mixer screen (jank_lpmix.h): jlapp_mixer_wanted is 1 while it is on and live control is up (the firmware reads MIXER every JLMX_READ_MS then); jlapp_on_mixer takes the
 * Digitone's answer (jdl_take_mixer); jlapp_set_mixer_writes says whether MIXER SET / MUTE SET are taken (jdl.mix_set_ok): when not, a press says MIXER READ ONLY */
int jlapp_mixer_wanted(const struct jlapp *a);
void jlapp_on_mixer(struct jlapp *a, const struct jdl_mixer *m);
void jlapp_set_mixer_writes(struct jlapp *a, int ok);
/* the Digitone is attached (1) or not (0; owner 2026-11-25: "when DN2 is disconnected, LP should be black"): without it every light of the Launchpad is off.
 * jlapp_init starts with 1; the firmware sets 0 at the start and follows the Digitone's attach / detach */
void jlapp_set_dn2(struct jlapp *a, int present);
/* the mixer's slow faders (jank_lpmix.h): the MIXER SET writes due now (call it often, after jlapp_tick: a slide moves every JLMX_SLIDE_TICK_MS); how many filled in out.
 * Without live control or the Digitone's mixer writes the slides end */
size_t jlapp_pump_acts(struct jlapp *a, struct jlapp_act *out, size_t max);
/* The Digitone's record state, as the firmware last knows it (jdl: record_ok, st.rec_valid / rec_bits, record_busy): the Record button's light follows the armed / recording bits
 * (when the Digitone sends them: protocol 6), a refusal count that grew says GRID / STEP REC IS ON and puts the light back, and a Digitone that has no RECORD darkens the button
 * (it says RECORD: DN2 TOO OLD once) */
#if JLAPP_RECORD                       /* (a unit without Record, jank_lpunit.h, has nothing to tell) */
void jlapp_set_record(struct jlapp *a, int ok, int bits_valid, uint8_t bits, uint32_t busy_count);
#endif
#if JLAPP_CLIPS
/* The clips page's news (jank_lpclips.h): every track's playing and queued slot and the slots that hold a pattern, and the scenes stored (bit n: scene n). The page shows
 * only this: call it whenever the clips change, right after a JLAPP_ACT_CLIP or SCENE was applied too */
void jlapp_on_clips(struct jlapp *a, const struct jlcl_track t[JLCL_TRACKS], uint32_t scenes);
#else
/* The Session page (jank_lpsess.h). jlapp_session_due: the next read to send now (1 and *r filled; call again until 0), only while the page is shown and live control is up;
 * jlapp_on_patterns / jlapp_on_pattern take the Digitone's answers (jdl_take_patterns / jdl_take_pattern); jlapp_set_patterns says whether PATTERNS and PATTERN are taken
 * (jdl.patterns_ok, jdl.pattern_ok): a Digitone that refuses PATTERN says PATTERN: DN2 TOO OLD (then at each press) and its pads send nothing */
int jlapp_session_due(struct jlapp *a, struct jlss_req *r);
void jlapp_on_patterns(struct jlapp *a, const struct jdl_patterns *p);
void jlapp_on_pattern(struct jlapp *a, const struct jdl_pattern *p);
void jlapp_set_patterns(struct jlapp *a, int reads_ok, int cue_ok);
/* The Digitone's stage 8 (jank_lptrans.h, jank_lpsess.h): jlapp_set_peers: Link (or Jank radio) peers are there (Link leads); jlapp_set_stage8: TRANSPORT and CHAIN are taken (a
 * refusal says TRANSP: DN2 TOO OLD / CHAIN: DN2 TOO OLD once); jlapp_on_dn2_transport: TRANSPORT's answer; jlapp_on_live7: a protocol-7 status (the transport, the playing and
 * next patterns, the chain count). Both return what Jank's own session must do (1 start, 0 stop, JLTR_NO_REQUEST nothing): only while Link leads and the Digitone's own keys
 * changed its transport. jlapp_on_chain: CHAIN's answer; jlapp_chain_list: the chain a JLAPP_ACT_CHAIN SET sends; jlapp_dn2_leads: no peers, and TRANSPORT is there */
void jlapp_set_peers(struct jlapp *a, int peers);
void jlapp_set_stage8(struct jlapp *a, int transport_ok, int chain_ok);
int jlapp_on_dn2_transport(struct jlapp *a, int state);
int jlapp_on_live7(struct jlapp *a, const struct jdl_live7 *s);
void jlapp_on_chain(struct jlapp *a, const struct jdl_chain *c);
const uint8_t *jlapp_chain_list(const struct jlapp *a, size_t *n);
int jlapp_dn2_leads(const struct jlapp *a);
#endif
/* the step modes: the pages of the current track to read now (pages[0..n), 0 outside the step modes), and a page the Digitone answered (jdl_take_page) */
size_t jlapp_step_wanted(const struct jlapp *a, uint8_t pages[2]);
void jlapp_on_page(struct jlapp *a, const struct jdl_page *pg);
/* 1 while a step mode lacks the pages it shows (the track or the page was just switched): the firmware reads at once instead of at its next tick */
int jlapp_step_urgent(const struct jlapp *a);

/* an end for every pad that is down (the Launchpad went away): acts, at most max; returns how many */
size_t jlapp_release_acts(struct jlapp *a, struct jlapp_act *out, size_t max);

/* the message to show now, copied to out (cap bytes, NUL-terminated); returns its length, 0 if none. Takes it: the next call gives the next one */
size_t jlapp_take_message(struct jlapp *a, char *out, size_t cap);

/* what the Launchpad should show now */
void jlapp_frame(const struct jlapp *a, struct jlx_frame *f);
#endif
