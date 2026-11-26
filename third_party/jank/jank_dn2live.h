/* SPDX-License-Identifier: GPL-3.0-only */
/* Jank's side of the Digitone II's live control (docs/live-control.md in dn2-wishes, branches claude/live-ctl2 and claude/live-ctl4): the pads play and select
 * tracks by SysEx, independent of every MIDI channel setting and of the MIDI input stream, and the sequencer's notes come back by track for the lights.
 * Pure (the clock is passed in): it says what to send and when, and reads what comes back. Host-tested (tests/test_dn2live.c).
 *
 *   44 LIVE   sub 0 STATUS, 1 ON (what = 1: push the notes), 2 OFF       any live request within 10 s keeps live control on (the lease): jank asks at
 *                                                                        least every JDL_KEEPALIVE_MS
 *   49 TRACK  u8 track (0..15)                                           the active track, as [TRK]+[TRIG n] does
 *   53 PLAY   u8 track (0..15, 255 = the active one), u8 op, u8 note, u8 vel     op 0 note off, 1 note on (vel 0: the track's own), 2 the track's own default
 *                                                                        note on, 3 all notes off (track 255: every track); op 0 with note 255 ends the
 *                                                                        track's own default note (what op 2 started); PROPOSED by jank, accepted by
 *                                                                        the Digitone session (stage 4, not built yet): a Digitone without it answers status 2
 *   47 push   DN2 -> jank, unasked: u8 n, u16 lost, n x {track, step, note, velocity, length, flags}
 *   status    0 ok, 2 not understood (no live control in this build, or PLAY not there), 4 live control is off (the lease ran out), 7 REMOTE CONTROL is OFF
 * Needs REMOTE CONTROL ON (SETTINGS > SYSTEM > USB CONFIG). */
#ifndef JANK_DN2LIVE_H
#define JANK_DN2LIVE_H
#include <stddef.h>
#include <stdint.h>

#define JDL_CMD_LIVE 44u               /* sub 0 status, 1 on, 3 ERASE (a u16 mask of tracks: the Digitone deletes each step of those tracks just before it plays, on its own playhead, since its lp2) */
#define JDL_CMD_MIXER 45u              /* read the mixer: every track's VOL, PAN, sends and LEVEL, the mute and solo masks */
#define JDL_CMD_MIXER_SET 56u          /* set one value of one track (stage 6) */
#define JDL_CMD_MUTE_SET 57u           /* set the mute or the solo mask (stage 6) */
#define JDL_CMD_RECORD 58u             /* u8 on: 0 off, 1 on, 255 read -> u8 on (stage 7, dn2-wishes 2026-11-24 measured format, NOT built there yet): the Digitone's own live record. On = what
                                        * [RECORD]+[PLAY] does but it does not start the sequencer (it only arms while stopped); off = what [PLAY] does during live record. Status 6 while GRID or
                                        * STEP RECORDING is on; 2 on a build without it. PLAY's notes are recorded as its keys' are (its quantise, PLAY's velocity, on the PLAYED track) */
#define JDL_REC_ARMED 1u               /* LIVE STATUS protocol 6, 73 bytes, byte 72: bit 0 armed, bit 1 recording (the REC LED), bit 2 quantised, bit 3 GRID, bit 4 STEP */
#define JDL_REC_RECORDING 2u
#define JDL_REC_GRID 8u
#define JDL_REC_STEP 16u
#define JDL_REC_BYTE 72u               /* its index in the LIVE STATUS data: it needs protocol >= 6 and 73 bytes */
enum jdl_mix_param { JDL_MIX_VOL = 0, JDL_MIX_PAN = 1, JDL_MIX_DEL = 2, JDL_MIX_REV = 3, JDL_MIX_CHR = 4, JDL_MIX_LEVEL = 5 };
#define JDL_CMD_SCALE 50u              /* read the scale (and, from protocol 5, the root) a track plays */
#define JDL_CMD_SCALE_SET 55u          /* set it (stage 6) */
#define JDL_SCALES 36u
#define JDL_ROOT_UNCHANGED 255u
#define JDL_CMD_STEPS 46u              /* read a page of 16 steps of a track */
#define JDL_CMD_STEP 48u               /* set / clear a trig, set its note */
#define JDL_CMD_TRACK 49u
#define JDL_CMD_MESSAGE 54u            /* u8 duration (100 ms units, 0 = 1 s), then 1..20 characters 0x20..0x7e: a box over the Digitone's page (stage 5) */
#define JDL_MSG_MAX 20u
#define JDL_CMD_PLAY 53u             /* not 50: the Digitone's stage 4 uses 50 SCALE, 51 PATTERNS, 52 SONG (renumbered 2026-11-24 at its request); MESSAGE will be 54 */
#define JDL_CMD_PUSH 47u               /* the push's command number (its cmd byte on the wire is 0xAF) */
#define JDL_NOTES_MAX 16u              /* notes in one push */

#define JDL_KEEPALIVE_MS 4000u         /* a live request at least this often while live (the lease is 10 s) */
#define JDL_ASK_MS 1000u               /* LIVE ON again this often while nothing answers */
#define JDL_IDLE_ASK_MS 5000u          /* ... and this often once the Digitone has said REMOTE CONTROL is off, or that it has no live control */

enum jdl_state {
    JDL_DOWN,                          /* nothing asked yet */
    JDL_ASKING,                        /* LIVE ON sent, no answer yet (or the lease ran out) */
    JDL_LIVE,                          /* live control is on */
    JDL_REMOTE_OFF,                    /* the Digitone answered 7: REMOTE CONTROL is off */
    JDL_NO_LIVE                        /* the Digitone answered 2 to LIVE: a build without live control */
};

enum jdl_play_op { JDL_PLAY_OFF = 0, JDL_PLAY_ON = 1, JDL_PLAY_DEFAULT = 2, JDL_PLAY_ALL_OFF = 3 };

struct jdl_note {
    uint8_t track, step, note, vel, length, flags;
};

/* One page of a track's steps (STEPS, 46: `u8 track (0..15, 255 active), u8 page` -> `u8 track, u8 page, u8 pages, u8 length, u8 playing step (not trusted: wrong on
 * 1.12), u8 playing, u8 active track, u8 pattern index`, then 16 x `{u16 trig flags, u8 note, u8 vel, u8 len, u8 notes}`): trig = flags bit 0; note / vel / len the
 * step's first note entry or the track's own (len is stock's length code, 14 = 1 step ... 127 INF, not converted here). STEP (48: `u8 track, u8 step, u8 op, u8 note,
 * u8 vel, u8 len`): op 0 clear, 1 set a trig (the track's default entry), 2 set a note (places the trig if none); vel 1..127 or 255 (the track's), len 0..127 or 255.
 * Status 2 (bad arguments, or not understood) and 5 (16 writes already waiting) refuse that one request and change nothing else. */
#define JDL_PAGE_STEPS 16u
enum jdl_step_op { JDL_STEP_CLEAR = 0, JDL_STEP_TRIG = 1, JDL_STEP_NOTE = 2 };
struct jdl_step {
    uint8_t trig, note, vel, len, notes;
};
struct jdl_page {
    uint8_t track, page, pages, length;
    struct jdl_step step[JDL_PAGE_STEPS];
};

/* LIVE STATUS data (the reply to every LIVE request, 31 bytes, docs/live-control.md in dn2-wishes): u8 protocol, u8 push on (the `what` bits), u8 seconds of lease
 * left, u8 ring size, u8 most notes a push, then u32 notes made (queued by the sequencer hook), u32 sent, u32 lost, u32 dropped, u32 push messages sent, u32 passes a
 * push waited for room in USB's ring, u16 the next push's sequence number; all numbers big-endian (the doc's rule for the channel) */
#define JDL_STATUS_BYTES 31u
/* Protocol 7 (the Digitone's stage 8, PROVISIONAL layout: final only after its lp8 build passes): the 73 bytes of protocol 6 unchanged, then u8 transport state (enum
 * jdl_transport_state), u8 playing pattern, u8 next pattern (255 = the same), u8 pattern mode, u16 chain count, u16 chain position (0xFFFF cued), then u32 writes applied, u32
 * writes refused. Read when protocol >= 7 and the answer is long enough (the counters only when they are there); protocols 5 and 6 read as before */
#define JDL_P7_BYTE 73u
#define JDL_P7_BYTES 81u               /* through the chain position */
#define JDL_P7_COUNTERS_BYTES 89u
struct jdl_live7 {
    uint8_t transport, playing, next, mode;
    uint16_t chain_count, chain_pos;
    uint8_t counters;                  /* the two counters below came */
    uint32_t applied, refused;
};
struct jdl_status {
    uint8_t valid, proto, push, lease;
    uint32_t made, sent, lost, dropped, msgs, waited;
    uint8_t rec_valid, rec_bits;       /* protocol >= 6: the record byte (JDL_REC_*) */
    uint8_t p7_valid;                  /* protocol >= 7: p7 holds the last answer's */
    struct jdl_live7 p7;
};

/* TRANSPORT (61, the Digitone's stage 8, PROVISIONAL): `u8 op` 0 STOP (its [STOP] key), 1 PLAY (start, or continue when paused; nothing when playing), 2 PAUSE, 255 read ->
 * `u8 state` (0 stopped, 1 playing, 2 paused). It runs the Digitone's own key code. A build without it answers status 2 once; none is sent after that until jdl_init */
#define JDL_CMD_TRANSPORT 61u
enum jdl_transport_op { JDL_TR_STOP = 0, JDL_TR_PLAY = 1, JDL_TR_PAUSE = 2, JDL_TR_READ = 255 };
enum jdl_transport_state { JDL_TR_STOPPED = 0, JDL_TR_PLAYING = 1, JDL_TR_PAUSED = 2 };
/* With protocol 7 the LIVE STATUS is asked this often instead of JDL_KEEPALIVE_MS: it carries the transport and the patterns (not measured: what it costs the Digitone) */
#define JDL_STATUS_FAST_MS 250u

/* The mixer (MIXER, 45, no arguments -> 106 bytes: u32 mute mask (bit t = track t+1), u32 solo mask, u8 active track, u8 16, then 16 x {VOL, PAN, DEL send, REV send, CHR
 * send, LEVEL}, each 0..127; PAN 64 is the centre; LEVEL is real from the Digitone's stage 4). A read only: writing needs the Digitone's stage 6. */
#define JDL_TRACKS 16u
struct jdl_mix_track {
    uint8_t vol, pan, del, rev, chr, level;
};
struct jdl_mixer {
    uint32_t mute, solo;               /* bit t: track t + 1 */
    uint8_t active;                    /* the Digitone's active track, 0..15 */
    struct jdl_mix_track t[JDL_TRACKS];
};

/* The scale of the active track (SCALE, 50: `u8 track (255 active), u8 first name, u8 kind` -> `u8 track, u8 scale 0..35 (0 Chromatic), u8 per-track flag, u8 36, u8 first, u8 n`,
 * then (protocol >= 5) `u8 root 0..11, u8 key mode`, then n names; Jank asks with first = 35, kind = 1 so that one short name follows and reads only the header). SCALE SET
 * (55: `u8 track (255 active), u8 scale 0..35, u8 root 0..11 or 255 (unchanged)` -> `u8 track, u8 scale, u8 root`) writes it at the Digitone's next UI pass (stage 6; status 2
 * on older builds). */
struct jdl_scale {
    uint8_t track, index, per_track;   /* the track asked, its scale, the Digitone keeps a scale per track */
    uint8_t has_root, root, key_mode;  /* root and key mode: only from protocol 5 */
};

/* The patterns (the Session page). PATTERNS (51, the Digitone's stage 4, works today): `u8 bank (0..7, 255 = the playing pattern's bank)` -> `u8 bank, u8 playing pattern
 * (0..127), u16 mask` (bit k: pattern 16 x bank + k holds a trig; big-endian); status 2: bank over 7 (or a build without it). It costs the Digitone up to about 0.8 ms per
 * bank in its MIDI task. PATTERN (60, PROVISIONAL: asked of the Digitone's developers 2026-11-25, not built there; Jank sends it as RECORD was sent before the Digitone had
 * it; stage 8's provisional format): `u8 pattern 0..127, u8 how (0 cue: switch at the playing pattern's end, 1 jump, 255 read)` -> `u8 playing, u8 next (255 = the same),
 * u8 mode, u8 running, u8 jump armed`; a build without it answers status 2 once and no more are sent until live control is asked for afresh (jdl_init). Never a MIDI Program
 * Change (owner: "sysex").
 * CHAIN (59, stage 8, PROVISIONAL): `u8 op` 0 SET `u8 n (2..64), n x pattern` (stock's PTN + held TRIG chain; it loops), 1 CLEAR, 255 READ `u8 first` -> `u8 mode, u16 count,
 * u16 position (0xFFFF cued), u8 first, u8 k, k x pattern`. Status 2 (a build without it: stops it as PATTERN's), 5 (queue full: that request), 4 / 7 (live control's). A MIDI
 * Program Change cancels a chain. */
#define JDL_CMD_PATTERNS 51u
#define JDL_CMD_PATTERN 60u
#define JDL_BANKS 8u                   /* A..H, 16 patterns each */
#define JDL_BANK_PATTERNS 16u
#define JDL_PATTERNS 128u
#define JDL_BANK_PLAYING 255u          /* PATTERNS: the bank of the playing pattern */
#define JDL_NO_PATTERN 255u            /* PATTERN: nothing cued */
enum jdl_pattern_how { JDL_PATTERN_CUE = 0, JDL_PATTERN_JUMP = 1, JDL_PATTERN_READ = 255 };
struct jdl_patterns {
    uint8_t bank, playing;             /* the bank read (0..7), the playing pattern (0..127) */
    uint16_t mask;                     /* bit k: pattern 16 x bank + k holds a trig */
};
struct jdl_pattern {
    uint8_t playing, next;             /* the playing pattern, the next one (JDL_NO_PATTERN: the same, nothing cued) */
    uint8_t mode, running, armed;      /* PATTERN's other bytes (0 when the answer is shorter) */
};
#define JDL_CMD_CHAIN 59u
#define JDL_CHAIN_MIN 2u
#define JDL_CHAIN_MAX 64u
enum jdl_chain_op { JDL_CHAIN_SET = 0, JDL_CHAIN_CLEAR = 1, JDL_CHAIN_READ = 255 };
#define JDL_CHAIN_HEAD 7u              /* mode, count, position, first, k */
struct jdl_chain {
    uint8_t mode, first, k;            /* this answer holds the chain's patterns first .. first + k - 1 */
    uint16_t count, pos;
    uint8_t member[JDL_PATTERNS / 8u]; /* bit p: pattern p is among those k */
};

/* Stage 9 (dn2-wishes lp9, built and passing its emulator, not on the hardware yet; owner 2026-11-25): Jank's own menu on the Digitone's screen, driven by its keys
 * (core/jank_oled_text.h, core/jank_dn2menu.h), and LINK MODE: while Link leads, the Digitone's PLAY / STOP ask Jank to start / stop the session.
 *   62 JANK PAGE   host -> DN2: u8 flags (bit 0 CLOSE, bit 1 line 0 is a title), u8 sel (255 none), u8 cursor column of line sel (255 none: the row is inverted;
 *                  set: only that cell), u8 n (0..7), n x {u8 length byte (below), the characters 0x20..0x7e}. At most 228 bytes; no data back
 *   63 JANK KEY    push 0xBF: u8 n (1..8), n x {u8 kind (1 down, 2 up, 3 turn), u8 code, s8 delta}. Repeats are not sent: Jank repeats
 *   64 JANK OPEN   push 0xC0 (and the reply to a request 64): u8 open, u8 reason (0 the menu, 1 a key, 2 the host's CLOSE, 3 lease lost, 4 a dialog or the screen
 *                  saver), u16 pages drawn. Pushed again every second while open until a page arrives; when live control comes back, open 1 reason 3
 *   65 LINK MODE   host -> DN2: u8 op: 0 OFF, 1 ON, 2 PENDING u16 ms (to the bar line) + u8 request id (answers a start: the Digitone blinks), 3 CANCEL (ends a pending
 *                  request), 4 ACK u8 request id (answers a stop), 255 READ -> u8 on, u8 pending, u8 transport state
 *   66 LINK REQUEST push 0xC2: u8 op (0 stop, 1 start; 2 pause is not used: owner, stop), u8 flags (JDL_LR_*), u8 state, u8 request id. Pushed again once after
 *                  500 ms when not answered. Always answered (PENDING or ACK)
 * All pushes share one sequence counter (LIVE STATUS bytes 29..30). A build without stage 9 answers 62 / 64 / 65 with status 2 once: none is sent after that
 * until jdl_init. */
#define JDL_CMD_JANK_PAGE 62u
#define JDL_CMD_JANK_KEY 63u
#define JDL_CMD_JANK_OPEN 64u
#define JDL_CMD_LINK_MODE 65u
#define JDL_CMD_LINK_REQUEST 66u
/* a page line's length byte (dn2-wishes, confirmed 2026-11-26): bits 0..4 the length, bit 5 always 0; bit 6 the line is centred; bit 7 split at the FIRST double space,
 * the left part at the left, the rest right-aligned at the right edge, the two spaces not drawn. Bits 6 and 7 together, or bit 5, are refused (status 2): never sent.
 * A build that refuses the bits (lp9) answers a page that has them with status 2: it is sent again without, and pages stay plain on that connection */
#define JDL_LINE_LEN 0x1Fu
#define JDL_LINE_CENTRED 0x40u
#define JDL_LINE_SPLIT 0x80u
#define JDL_PAGE_CLOSE 0x01u
#define JDL_PAGE_TITLE 0x02u
#define JDL_PAGE_LINES 7u
#define JDL_PAGE_COLS 31u
#define JDL_PAGE_ARGS_MAX (4u + JDL_PAGE_LINES * (1u + JDL_PAGE_COLS))   /* 228 */
#define JDL_KEYS_MAX 8u
enum jdl_key_kind { JDL_KEY_PRESS = 1, JDL_KEY_RELEASE = 2, JDL_KEY_TURN = 3 };
/* the key codes (dn2-wishes stage 9); encoder pushes 41..48, turns (kind 3) carry the encoder's code */
enum jdl_key_code { JDL_KC_YES = 10, JDL_KC_UP = 11, JDL_KC_NO = 12, JDL_KC_LEFT = 13, JDL_KC_DOWN = 14, JDL_KC_RIGHT = 15, JDL_KC_FUNC = 17 };
struct jdl_key {
    uint8_t kind, code;
    int8_t delta;
};
struct jdl_keys {
    uint8_t n;
    struct jdl_key k[JDL_KEYS_MAX];
};
enum jdl_open_reason { JDL_OPEN_MENU = 0, JDL_OPEN_KEY = 1, JDL_OPEN_HOST_CLOSE = 2, JDL_OPEN_LEASE = 3, JDL_OPEN_DIALOG = 4 };
struct jdl_open {
    uint8_t open, reason;
    uint16_t drawn;                    /* pages the Digitone drew */
};
enum jdl_link_op { JDL_LINK_OFF = 0, JDL_LINK_ON = 1, JDL_LINK_PENDING = 2, JDL_LINK_CANCEL = 3, JDL_LINK_ACK = 4, JDL_LINK_READ = 255 };
enum jdl_link_req_op { JDL_LR_STOP = 0, JDL_LR_START = 1, JDL_LR_PAUSE = 2 };
#define JDL_LR_RECORD 0x01u            /* RECORD was held: RECORD (58) op 1 first */
#define JDL_LR_SECOND_STOP 0x02u       /* a second STOP within 400 ms */
#define JDL_LR_WAS_PAUSED 0x04u
struct jdl_link_req {
    uint8_t op, flags, state, id;
};
/* LIVE STATUS protocol 8 (116 bytes): protocol 7's 89 unchanged (byte 0 says 8), then from byte 89: u8 jank open, u8 reason, u8 sel shown (255 none), u16 pages drawn,
 * u32 pages received (62), u32 key events pushed, u32 key events lost, u8 link on, u8 start pending, u8 last request op (255 none), u8 its flags, u8 its id, u8 acked,
 * u32 requests made */
#define JDL_P8_BYTE 89u
#define JDL_P8_BYTES 116u
struct jdl_live8 {
    uint8_t open, reason, sel;
    uint16_t drawn;
    uint32_t pages, keys, keys_lost;
    uint8_t link_on, pending, req_op, req_flags, req_id, acked;
    uint32_t requests;
};

struct jdl {
    struct jdl_status st;              /* the Digitone's last LIVE STATUS */
    uint8_t p8_valid;                  /* protocol >= 8: p8 holds the last status's stage-9 part */
    uint8_t p8_fresh;                  /* ... and it was not taken yet (jdl_take_live8) */
    struct jdl_live8 p8;
    uint8_t jank_ok;                   /* JANK PAGE / OPEN not refused (a build without stage 9): 1 until they are */
    uint8_t align_ok;                  /* pages carry the placing bits: 1 until a page with them is refused (status 2), then plain pages on this connection */
    uint8_t page_resend;               /* that refusal came: the page must go again, plain (jdl_take_page_resend) */
    uint8_t page_bits;                 /* a page with placing bits has been sent; page_seq: the last one's sequence number */
    uint16_t page_seq;
    uint8_t link_ok;                   /* LINK MODE not refused: 1 until it is */
    uint8_t jank_probed;               /* JANK OPEN has been read once on this connection (jdl_probe_jank) */
    struct jdl_keys keys;              /* the last key push, until taken (keys_fresh) */
    struct jdl_open open;              /* the last OPEN (push or reply), until taken (open_fresh) */
    struct jdl_link_req lreq;          /* the last LINK REQUEST, until taken (lreq_fresh) */
    uint8_t keys_fresh, open_fresh, lreq_fresh;
    uint8_t live7_fresh;               /* a protocol-7 status came and was not taken (jdl_take_live7) */
    uint8_t transport_ok, transport_fresh, transport_state;   /* TRANSPORT (61) not refused; its last answer, until taken */
    uint8_t transport_probed;          /* TRANSPORT has been read once on this connection (jdl_probe_transport) */
    uint8_t chain_ok, chain_fresh;     /* CHAIN (59) not refused; its last answer, until taken */
    uint32_t chain_busy;               /* CHAIN answered 5 (queue full): that request lost */
    struct jdl_chain ch;
    struct jdl_patterns pb;            /* the last PATTERNS answer, until taken (patterns_fresh) */
    struct jdl_pattern pc;             /* the last PATTERN answer, until taken (pattern_fresh) */
    uint8_t patterns_fresh, pattern_fresh;
    uint8_t patterns_ok;               /* PATTERNS has not been refused (status 2): 1 until it is */
    uint8_t pattern_ok;                /* PATTERN has not been refused by a build without it: 1 until it is */
    struct jdl_scale sc;               /* the last SCALE answer (or SCALE SET's), until taken (scale_fresh) */
    uint8_t scale_fresh;
    uint8_t scale_ok;                  /* SCALE has not been refused (status 2): 1 until it is */
    uint8_t scale_set_ok;              /* SCALE SET has not been refused (an older Digitone build): 1 until it is */
    uint32_t scale_set_refused;        /* SCALE SET answered 2 by a build that has it (protocol >= 5): that write refused, the next may go */
    uint8_t mix_set_ok;                /* MIXER SET / MUTE SET have not been refused by a build without them: 1 until they are */
    uint32_t mix_set_refused;          /* ... answered 2 or 5 by a build that has them: that write refused */
    uint8_t record_ok;                 /* RECORD has not been refused by a build without it (status 2): 1 until it is */
    uint32_t record_busy;              /* RECORD answered 6: GRID or STEP RECORDING is on, the Digitone did not arm */
    struct jdl_mixer mx;               /* the last MIXER answer, until taken (mixer_fresh) */
    uint8_t mixer_fresh;
    struct jdl_page pg;                /* the last STEPS answer, until taken (page_fresh) */
    uint8_t page_fresh;
    uint32_t step_busy, step_refused;  /* STEP / STEPS answered 5 (queue full) / 2 (refused) */
    uint16_t seq;
    uint8_t state;                     /* enum jdl_state */
    uint8_t no_push;                   /* LIVE ON without the note push (jdl_want_push) */
    uint8_t play_ok;                   /* PLAY has not been refused (status 2): 1 until it is */
    uint8_t msg_ok;                    /* MESSAGE has not been refused (status 2: a Digitone build without it): 1 until it is */
    uint8_t asked;                     /* a live request has been sent, at sent_ms */
    uint32_t sent_ms, status_ms;       /* the last request of any kind (the lease), the last LIVE STATUS asked (the fast status of protocol 7) */
    uint16_t read_seq, select_seq;     /* the last active-track read and the last select sent */
    uint8_t has_read, has_select;
    uint8_t read_ok;                   /* the no-argument TRACK read has not been refused (status 2): 1 until it is */
    uint8_t play_probed;               /* PLAY has been asked about on this connection (jdl_probe_play) */
    uint8_t fresh, active;             /* an answer to the last read, not yet taken: the Digitone's active track */
};

enum jdl_in {
    JDL_IN_IGNORED,                    /* not a frame of the channel */
    JDL_IN_REPLY,                      /* a reply: the state may have changed */
    JDL_IN_PUSH,                       /* a note push: *count notes in the array given */
    JDL_IN_JANK                        /* stage 9: keys, OPEN (pushed, or the read's answer) or a LINK REQUEST (jdl_take_keys / jdl_take_open / jdl_take_link_request) */
};

/* Stage 9 (core/jank_dn2live9.c). jdl_jank_page: the page (core/jank_oled_text.h) as a JANK PAGE, with the placing bits while align_ok; 0 unless live and not refused.
 * jdl_probe_jank: one OPEN read when live control comes up (is the page open, and is stage 9 there); once per connection. jdl_link_mode: op an enum jdl_link_op (ms and
 * id for PENDING, id for ACK); 0 unless live and not refused. The takes: the last push (or OPEN reply), once (1 and *out filled), else 0. jdl_take_page_resend: 1 once
 * after a page with placing bits was refused (send the page again: it goes plain now) */
struct jot_page;
size_t jdl_jank_page(struct jdl *d, uint32_t now_ms, const struct jot_page *p, uint8_t *out, size_t cap);
size_t jdl_probe_jank(struct jdl *d, uint32_t now_ms, uint8_t *out, size_t cap);
size_t jdl_link_mode(struct jdl *d, uint32_t now_ms, uint8_t op, uint16_t ms, uint8_t id, uint8_t *out, size_t cap);
int jdl_take_keys(struct jdl *d, struct jdl_keys *out);
int jdl_take_open(struct jdl *d, struct jdl_open *out);
int jdl_take_link_request(struct jdl *d, struct jdl_link_req *out);
int jdl_take_page_resend(struct jdl *d);
int jdl_take_live8(struct jdl *d, struct jdl_live8 *out);     /* the protocol-8 part of the last LIVE STATUS, once per status */
/* (jank_dn2live.c's jdl_input calls these) a stage-9 frame: 1 when taken (*kind what jdl_input returns), 0 when it is not one; the protocol-8 part of a LIVE STATUS */
struct jd2_reply;
int jdl9_input(struct jdl *d, const struct jd2_reply *r, enum jdl_in *kind);
void jdl9_status(struct jdl *d, const uint8_t *s, size_t n);
#define JDL_JANK_PAGE_SX_MAX (6u + (3u + JDL_PAGE_ARGS_MAX) + (3u + JDL_PAGE_ARGS_MAX + 6u) / 7u + 1u)   /* 271: the longest page's SysEx */

void jdl_init(struct jdl *d);
/* LIVE ON asks for the note push (the Launchpad's lights) only while on (jdl_init: on); the Digitone alone (Jank's menu, LINK MODE) needs none. Taken at the next
 * LIVE ON */
void jdl_want_push(struct jdl *d, int on);
int jdl_is_live(const struct jdl *d);
int jdl_can_play(const struct jdl *d);                       /* live, and PLAY has not been refused */

/* the request to send now, F0 .. F7 into out, its length; 0 when there is nothing to send yet. LIVE ON while not live (every JDL_ASK_MS, every JDL_IDLE_ASK_MS
 * after a 7 or a 2), a LIVE STATUS every JDL_KEEPALIVE_MS while live (every JDL_STATUS_FAST_MS once the Digitone said protocol 7) */
size_t jdl_poll(struct jdl *d, uint32_t now_ms, uint8_t *out, size_t cap);
/* a request, only while live (else 0): the active track, a PLAY. A request also keeps the lease */
size_t jdl_track(struct jdl *d, uint32_t now_ms, uint8_t track, uint8_t *out, size_t cap);
size_t jdl_play(struct jdl *d, uint32_t now_ms, uint8_t track, enum jdl_play_op op, uint8_t note, uint8_t vel, uint8_t *out, size_t cap);
/* The scale (only while live, else 0). jdl_scale_read: the cheap poll; jdl_scale_set: scale 0..35, root 0..11 or JDL_ROOT_UNCHANGED, and only while SCALE SET has not been
 * refused. jdl_take_scale: the last answer (a read's or a set's), once (1 and *out filled), else 0 */
size_t jdl_scale_read(struct jdl *d, uint32_t now_ms, uint8_t *out, size_t cap);
size_t jdl_scale_set(struct jdl *d, uint32_t now_ms, uint8_t scale, uint8_t root, uint8_t *out, size_t cap);
int jdl_take_scale(struct jdl *d, struct jdl_scale *out);

/* The mixer read (only while live, else 0). jdl_take_mixer: the last MIXER answer, once (1 and *out filled), else 0 */
size_t jdl_mixer_read(struct jdl *d, uint32_t now_ms, uint8_t *out, size_t cap);
int jdl_take_mixer(struct jdl *d, struct jdl_mixer *out);
/* The mixer writes (Digitone stage 6; each applies at its next UI pass, 16 waiting answers status 5; a build without them answers status 2 once and they stop).
 * MIXER SET {track 0..15 (255 = the active one), param (enum jdl_mix_param), value 0..127}; MUTE SET {which 0 mute / 1 solo, u16 mask (bit t = track t + 1)}: the whole mask */
size_t jdl_mixer_set(struct jdl *d, uint32_t now_ms, uint8_t track, enum jdl_mix_param param, uint8_t value, uint8_t *out, size_t cap);
size_t jdl_mute_set(struct jdl *d, uint32_t now_ms, uint8_t which, uint16_t mask, uint8_t *out, size_t cap);
/* RECORD (58): on 0 / 1 (255 reads). 0 unless live control is on and the Digitone has not said it lacks RECORD */
size_t jdl_record(struct jdl *d, uint32_t now_ms, uint8_t on, uint8_t *out, size_t cap);
/* PATTERNS (51): bank 0..7 or JDL_BANK_PLAYING; 0 unless live and not refused. PATTERN (60, provisional): pattern 0..127, how an enum jdl_pattern_how (a read sends pattern 0);
 * 0 unless live and not refused. jdl_take_patterns / jdl_take_pattern: the last answer, once (1 and *out filled), else 0 */
size_t jdl_patterns_read(struct jdl *d, uint32_t now_ms, uint8_t bank, uint8_t *out, size_t cap);
size_t jdl_pattern(struct jdl *d, uint32_t now_ms, uint8_t pattern, uint8_t how, uint8_t *out, size_t cap);
int jdl_take_patterns(struct jdl *d, struct jdl_patterns *out);
int jdl_take_pattern(struct jdl *d, struct jdl_pattern *out);
/* TRANSPORT (61): op an enum jdl_transport_op; 0 unless live and not refused. jdl_probe_transport: one read right after live control comes up (its refusal, or the state, is
 * known before the first press); once per connection. jdl_take_transport: the last answer's state, once, else -1 */
size_t jdl_transport(struct jdl *d, uint32_t now_ms, uint8_t op, uint8_t *out, size_t cap);
size_t jdl_probe_transport(struct jdl *d, uint32_t now_ms, uint8_t *out, size_t cap);
int jdl_take_transport(struct jdl *d);
/* CHAIN (59): SET n (JDL_CHAIN_MIN..JDL_CHAIN_MAX) patterns 0..127, CLEAR, READ from `first`; 0 unless live, not refused and valid. jdl_take_chain: the last answer (its READ
 * part), once */
size_t jdl_chain_set(struct jdl *d, uint32_t now_ms, const uint8_t *patterns, size_t n, uint8_t *out, size_t cap);
size_t jdl_chain_clear(struct jdl *d, uint32_t now_ms, uint8_t *out, size_t cap);
size_t jdl_chain_read(struct jdl *d, uint32_t now_ms, uint8_t first, uint8_t *out, size_t cap);
int jdl_take_chain(struct jdl *d, struct jdl_chain *out);
/* the protocol-7 part of the last LIVE STATUS, once per status (1 and *out filled), else 0 */
int jdl_take_live7(struct jdl *d, struct jdl_live7 *out);
/* LIVE ERASE: bit t of `tracks` = erase on for track t, 0 = all off (the Delete button held / let go). The answer is LIVE STATUS. 0 unless live control is on */
size_t jdl_erase(struct jdl *d, uint32_t now_ms, uint16_t tracks, uint8_t *out, size_t cap);

/* Step pages and edits (only while live; 0 for invalid arguments, which the Digitone would refuse). jdl_take_page: the last STEPS answer, once (1 and *out filled), else 0 */
size_t jdl_steps_read(struct jdl *d, uint32_t now_ms, uint8_t track, uint8_t page, uint8_t *out, size_t cap);
size_t jdl_step_edit(struct jdl *d, uint32_t now_ms, uint8_t track, uint8_t step, enum jdl_step_op op, uint8_t note, uint8_t vel, uint8_t len, uint8_t *out, size_t cap);
int jdl_take_page(struct jdl *d, struct jdl_page *out);

/* A short message for the Digitone's screen (MESSAGE, 54; Jank has no screen of its own): `text` of 1..JDL_MSG_MAX printable ASCII characters, shown `tenths` x 100 ms (0: the
 * Digitone's default, 1 s). Only while live and while MESSAGE has not been refused; else 0 (also for text the Digitone would refuse: empty, too long, a character outside
 * 0x20..0x7e). It changes nothing on the Digitone and never covers one of stock's dialogs. A newer message replaces the one showing. */
int jdl_can_message(const struct jdl *d);
size_t jdl_message(struct jdl *d, uint32_t now_ms, const char *text, uint8_t tenths, uint8_t *out, size_t cap);

/* Right after live control comes up: one PLAY "all notes off, every track", which every Digitone with PLAY takes without a sound and one without it refuses (status 2),
 * so the pads know PLAY's answer before the first press (else the first press went out, was refused, and only the later ones were blocked). Once per connection;
 * 0 when not live or already asked */
size_t jdl_probe_play(struct jdl *d, uint32_t now_ms, uint8_t *out, size_t cap);
/* TRACK with no argument asks for the active track (PROPOSED to the Digitone session 2026-11-24, not confirmed; a refusal, status 2, ends the asking). Only the
 * answer to the last read counts, and only when the read was sent after the last select: jdl_take_active gives that track (0..15) once, else -1 */
int jdl_can_read_track(const struct jdl *d);
size_t jdl_track_read(struct jdl *d, uint32_t now_ms, uint8_t *out, size_t cap);
int jdl_take_active(struct jdl *d);

/* a SysEx from the Digitone (F0 .. F7, n bytes). A reply moves the state (0 ok on a live request: live; 7: REMOTE CONTROL off; 2: no live control, or no PLAY; 4: the
 * lease ran out, ask again); a push fills notes (room for JDL_NOTES_MAX) and *count */
enum jdl_in jdl_input(struct jdl *d, const uint8_t *sx, size_t n, struct jdl_note *notes, size_t *count);
#endif
