/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad's Session page (owner 2026-11-25): the Digitone's 128 patterns, 8 banks (A..H) of 16. Two pad rows per bank, four banks a page, from the top: A01..A08,
 * A09..A16, then B, C, D; up / down switch to banks E..H and back. Lights as Launchpad 95's session skin (SkinMK2.py Session): the playing pattern GREEN (21) PULSING, a
 * cued one GREEN FLASHING, a pattern of the chain GREEN_THIRD (23), a pattern that holds trigs RED_THIRD (7), an empty pattern (or one not read yet) dark.
 *
 * What it knows comes from the Digitone: PATTERNS (51, jank_dn2live.h) per bank, read once for all 8 banks when the page opens (one request every JLSS_OPEN_GAP_MS), then only
 * the playing pattern's bank every JLSS_POLL_MS while the page is shown, nothing while it is closed (each bank costs the Digitone up to about 0.8 ms in its MIDI task); PATTERN's
 * (60, provisional) answer or read: the next pattern. With LIVE STATUS protocol 7 the playing and next patterns come with the status: no PATTERN read, and the playing bank only
 * every JLSS_POLL_SLOW_MS (its trigs). CHAIN (59): read when the page opens, after a SET or a CLEAR, and when the status's chain count changes.
 *
 * A press cues its pattern (PATTERN how 0): it flashes at once, and the Digitone's "nothing next" is not believed for JLSS_CUE_HOLD_MS; it stops when the pattern plays or the
 * Digitone names another. A CHAIN: pads pressed while the first pad is still HELD (on either page) follow it, in order; letting go of the first pad sends them all (CHAIN SET,
 * 2..64 patterns; the first was already cued). Holding the PLAYING pattern's pad alone for JLSS_CLEAR_HOLD_MS clears the chain (CHAIN CLEAR). Pure; host-tested
 * (tests/test_lpsess.c). */
#ifndef JANK_LPSESS_H
#define JANK_LPSESS_H
#include <stddef.h>
#include <stdint.h>
#include "jank_dn2live.h"
#include "jank_lpx.h"

#define JLSS_HALVES 2u                 /* the two pages: banks A..D, E..H */
#define JLSS_BANKS_SHOWN 4u
#define JLSS_OPEN_GAP_MS 60u           /* the opening reads, one bank each (the step modes' quick reads are 60 ms apart too) */
#define JLSS_POLL_MS 500u              /* then the playing pattern's bank, this often */
#define JLSS_POLL_SLOW_MS 2000u        /* ... or this often when the status (protocol 7) gives the playing and next patterns */
#define JLSS_CUE_HOLD_MS 500u          /* a press's cue is not undone by an older "nothing next" */
#define JLSS_CLEAR_HOLD_MS 1000u       /* the playing pattern's pad held alone this long: the chain is cleared */
#define JLSS_COLOR_PLAYING 21u         /* GREEN, pulsing (Session.ClipStarted) */
#define JLSS_COLOR_CUED 21u            /* GREEN, flashing with dark (Session.ClipTriggeredPlay) */
#define JLSS_COLOR_CHAIN 23u           /* GREEN_THIRD: a pattern of the chain (and of one being built) */
#define JLSS_COLOR_FILLED 7u           /* RED_THIRD: a pattern that holds trigs */
#define JLSS_UNKNOWN 255u

/* what to ask the Digitone now (jlss_due) */
enum jlss_req_kind { JLSS_REQ_NONE, JLSS_REQ_BANK, JLSS_REQ_CUE_READ, JLSS_REQ_CHAIN_READ };
struct jlss_req {
    uint8_t kind;                      /* enum jlss_req_kind */
    uint8_t bank;                      /* JLSS_REQ_BANK: 0..7, or JDL_BANK_PLAYING; JLSS_REQ_CHAIN_READ: the first index to read */
};
/* what the Digitone takes (jlss_due's `ok`) */
#define JLSS_OK_READS 1u               /* PATTERNS */
#define JLSS_OK_CUE 2u                 /* PATTERN */
#define JLSS_OK_CHAIN 4u               /* CHAIN */
#define JLSS_OK_LIVE7 8u               /* the status is protocol 7 (the patterns come with it) */

enum jlss_press { JLSS_PRESS_CUE, JLSS_PRESS_ADDED, JLSS_PRESS_FULL };
enum jlss_release { JLSS_RELEASE_NONE, JLSS_RELEASE_SET, JLSS_RELEASE_CLEAR };

struct jlss {
    uint16_t mask[JDL_BANKS];          /* bit k: pattern 16 x bank + k holds a trig */
    uint8_t have;                      /* bit b: bank b has been read */
    uint8_t playing, cued;             /* JLSS_UNKNOWN / JDL_NO_PATTERN: none known */
    uint8_t half;                      /* 0: banks A..D, 1: E..H */
    uint8_t sweep;                     /* the next bank of the opening reads; JDL_BANKS when they are done */
    uint8_t cue_due;                   /* a PATTERN read goes with the next request */
    uint8_t retry_due;                 /* ... and a bank that never answered, after it */
    uint8_t asked;                     /* a request went at last_ms since the page opened */
    uint32_t last_ms, cue_ms;          /* the last read; the last cue pressed */
    uint8_t chain[JDL_PATTERNS / 8u];  /* bit p: pattern p is in the Digitone's chain (as last read, or as last sent) */
    uint16_t chain_count;              /* the chain's length as the Digitone last said (status or CHAIN) */
    uint8_t chain_due, chain_first;    /* CHAIN is to be read from chain_first */
    uint8_t hold_on, hold_pad;         /* a pad held (0..63): the first pattern of a chain being built */
    uint32_t hold_ms;
    uint8_t build_n;                   /* the chain being built: build[0..build_n) */
    uint8_t build[JDL_CHAIN_MAX];
};

void jlss_init(struct jlss *s);
/* the page opens (or live control came back): every bank is read again, the chain, then PATTERN once (a build without it says so at once, not at the first press) */
void jlss_open(struct jlss *s);
/* live control went away: nothing known is shown any more */
void jlss_forget(struct jlss *s);
/* The next request due at now_ms (1 and *r filled; the caller sends it and may ask again at once), else 0. `ok`: the JLSS_OK_ bits. The opening reads, one bank per
 * JLSS_OPEN_GAP_MS; the chain when due; then every JLSS_POLL_MS (JLSS_POLL_SLOW_MS with protocol 7) the playing bank, the PATTERN read right after it (not with protocol 7), and one
 * bank that never answered */
int jlss_due(struct jlss *s, uint32_t now_ms, unsigned ok, struct jlss_req *r);
/* the Digitone's answers: a bank (PATTERNS), the playing and next patterns (PATTERN, or the protocol-7 status: jlss_on_next), the chain (CHAIN, or its count in the status) */
void jlss_on_patterns(struct jlss *s, const struct jdl_patterns *p);
void jlss_on_next(struct jlss *s, uint8_t playing, uint8_t next, uint32_t now_ms);
void jlss_on_chain(struct jlss *s, const struct jdl_chain *c);
void jlss_on_chain_count(struct jlss *s, uint16_t count);
/* the pattern of pad (x, y from the bottom) on the half shown, 0..127 */
uint8_t jlss_pattern_of_pad(const struct jlss *s, uint8_t x, uint8_t y);
/* A pad (0..63, y * 8 + x) pressed for `pattern`: the first one held is a cue (and the start of a chain); with it held, the others are added to the chain. Its release
 * (jlss_release): the held pad let go sends the chain (2 or more) or, the playing pattern held alone JLSS_CLEAR_HOLD_MS, clears it */
int jlss_press(struct jlss *s, uint8_t pad, uint8_t pattern, uint32_t now_ms);
int jlss_release(struct jlss *s, uint8_t pad, uint32_t now_ms);
int jlss_holds(const struct jlss *s, uint8_t pad);
/* the chain to send (after JLSS_RELEASE_SET; it stays until the next press), and what was sent: shown at once, read back */
const uint8_t *jlss_chain_list(const struct jlss *s, size_t *n);
void jlss_chain_sent(struct jlss *s);
void jlss_chain_cleared(struct jlss *s);
/* a press cued `pattern`: it flashes until the Digitone says otherwise */
void jlss_cue(struct jlss *s, uint8_t pattern, uint32_t now_ms);
/* up (-1) / down (+1): the half; 1 when it changed, 0 at the limit */
int jlss_half_step(struct jlss *s, int dir);
/* "A01" .. "H16" into out (4 bytes) */
void jlss_name(uint8_t pattern, char out[4]);
/* the 64 pads (led[JLX_PAD0 ..]) */
void jlss_frame(const struct jlss *s, struct jlx_frame *f);
#endif
