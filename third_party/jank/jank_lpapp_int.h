/* SPDX-License-Identifier: GPL-3.0-only */
/* Inside the Launchpad app (jank_lpapp.h), shared by its files: jank_lpapp.c (Note mode, the buttons, the lights) and jank_lpsessapp.c (the Session button and page, and who
 * starts and stops). Not for the firmware. */
#ifndef JANK_LPAPP_INT_H
#define JANK_LPAPP_INT_H
#include "jank_lpapp.h"

/* a message (jank_lpapp.h: upper case, at most JLAPP_MSG_MAX - 1 characters, the newest replaces one not taken) */
void jlapp_say(struct jlapp *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* why a press cannot do anything (no live control ...), at most every 2 s */
void jlapp_say_why_not(struct jlapp *a);
void jlapp_set_act(struct jlapp_act *o, uint8_t kind, uint8_t track, uint8_t note, uint8_t vel);

/* jank_lpsessapp.c: Session held (jlapp_tick), the Session button, a pad of the Session page (a press, or the release of its held pad), the start / stop scene button */
void jlapp_open_chart(struct jlapp *a);
void jlapp_session_button(struct jlapp *a, int down);
size_t jlapp_session_pad(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max);
size_t jlapp_start_stop(struct jlapp *a, struct jlapp_act *out, size_t max);
#endif
