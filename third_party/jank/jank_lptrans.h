/* SPDX-License-Identifier: GPL-3.0-only */
/* Who starts and stops: the owner's TRANSPORT RULE (2026-11-25): "if there are no link peers, then DN2 is master. but if there are then link is", and "play and rec button
 * state should be synced bi-directionally".
 *
 *   No peers, and the Digitone has TRANSPORT (61, its stage 8): the Digitone leads. The Launchpad's start / stop (scene 5) sends TRANSPORT (PLAY when it is stopped or paused,
 *   STOP when it plays), Jank's own session is not touched and no clock goes to the Digitone (the firmware asks for no Start, so nothing forces the clock on). The light
 *   follows the Digitone's transport (LIVE STATUS protocol 7, or TRANSPORT's answer); a press shows at once and an older status does not put it back for JLTR_HOLD_MS.
 *   Peers there: Link leads. Scene 5 starts / stops Jank's session as before (and the clock goes to the Digitone). The Digitone's own PLAY / STOP keys, seen as a change of its
 *   transport, start / stop Jank's session too. The guard against a loop: a change only counts when it differs from what Jank's session does, and not within JLTR_GUARD_MS
 *   of a change Jank made itself (its Start reaches the Digitone, whose transport then changes: that must not come back).
 *   A Digitone without TRANSPORT (status 2), or before its first answer: as before (scene 5 is Jank's session, and the clock is forced on for the Digitone).
 * "Peers" is the bridge's count (bridge_peers_heard): Link peers on a Wi-Fi network, the Jank radio's peers otherwise; the same count that lets the clock out. Pure; host-tested
 * (tests/test_lptrans.c). */
#ifndef JANK_LPTRANS_H
#define JANK_LPTRANS_H
#include <stdint.h>

#define JLTR_GUARD_MS 1000u            /* after a change Jank made, the Digitone's transport following it is not sent back */
#define JLTR_HOLD_MS 500u              /* after a TRANSPORT press, an older status does not undo the light */
#define JLTR_NO_REQUEST (-1)
#define JLTR_STOP_FOLLOW_MS 30u        /* Jank's clock sent its MIDI Stop this long ago: TRANSPORT STOP follows (the MIDI Stop is out first) */

struct jltr {
    uint8_t peers;                     /* Link (or Jank radio) peers: Link leads */
    uint8_t cmd_ok;                    /* TRANSPORT has not been refused */
    uint8_t has_cmd;                   /* ... and has answered (jank_lpsessapp.c probes it when live control comes up): until then scene 5 is as before */
    uint8_t known, state;              /* the Digitone's transport as last read (enum jdl_transport_state) */
    uint8_t jank;                      /* Jank's own session plays */
    uint8_t guard_on, hold_on;
    uint32_t guard_ms, hold_ms;
};

void jltr_init(struct jltr *t);
void jltr_set_peers(struct jltr *t, int peers);
/* 1 while the Digitone leads: no peers, TRANSPORT answered and not refused, live control up */
int jltr_dn2_leads(const struct jltr *t, int live);
/* scene 5 pressed: 1 and *op (enum jdl_transport_op) when TRANSPORT is to be sent; 0: Jank's own session, as before */
int jltr_press(struct jltr *t, int live, uint32_t now_ms, uint8_t *op);
/* The Digitone's transport (a status, or TRANSPORT's own answer: `answer` 1, always taken). Returns what Jank's session must do, only while Link leads and the change is the
 * Digitone's own: 1 start, 0 stop, JLTR_NO_REQUEST nothing */
int jltr_on_dn2(struct jltr *t, unsigned state, int answer, uint32_t now_ms);
/* live control went away: TRANSPORT must answer again before the Digitone leads (it may be another Digitone, or another build) */
void jltr_lost(struct jltr *t);
/* Jank's own session plays or not (often; a change starts the guard) */
void jltr_set_jank(struct jltr *t, int playing, uint32_t now_ms);
/* Jank's clock just sent a MIDI Stop, which leaves the Digitone PAUSED (stock's state after an external Stop: a Continue would resume). 1: TRANSPORT STOP is to follow so it
 * ends stopped, as its own STOP key does (owner 2026-11-25): Link leads (peers: the Stop reached it), the Digitone has TRANSPORT (answered, not refused), live control up */
int jltr_stop_follows(const struct jltr *t, int live);
/* scene 5's light: 1 full (playing), 0 dim: the Digitone's transport while it leads and is known, else Jank's session */
int jltr_lit(const struct jltr *t, int live);
#endif
