/* SPDX-License-Identifier: GPL-3.0-only */
/* Inside the Launchpad app (jank_lpapp.h), shared by its files: jank_lpapp.c (Note mode, the buttons, the lights) and the unit's Session page: jank_lpsessapp.c (the
 * Digitone's patterns, jank_lpsess.h) or jank_lpclips.c (clips, jank_lpunit.h JLAPP_CLIPS). Not for the firmware. */
#ifndef JANK_LPAPP_INT_H
#define JANK_LPAPP_INT_H
#include "jank_lpapp.h"

/* a message (jank_lpapp.h: upper case, at most JLAPP_MSG_MAX - 1 characters, the newest replaces one not taken); the format knows %s and %u only */
void jlapp_say(struct jlapp *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* why a press cannot do anything (no live control ...), at most every 2 s */
void jlapp_say_why_not(struct jlapp *a);

/* The Session page, the unit's: its struct is a->sess, with `half` (0: the top 8 rows of its 16, 1: the others; up / down, jank_lpapp.c) */
#if JLAPP_CLIPS
#define JLAPP_HALF_NAME(h) ((h) ? "SLOTS 9-16" : "SLOTS 1-8")
#else
#define JLAPP_HALF_NAME(h) ((h) ? "BANKS E-H" : "BANKS A-D")
#endif
void jlapp_page_init(struct jlapp *a);
/* live 1: the page is shown (again): everything is read again; 0: live control went away, what it showed may not be true any more */
void jlapp_page_open(struct jlapp *a, int live);
/* a pad of the page: a press, or a release (0 acts and nothing done for a pad it does not hold) */
size_t jlapp_session_pad(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max);
/* its 64 pads (led[JLX_PAD0 ..]) */
void jlapp_page_frame(const struct jlapp *a, struct jlx_frame *f);
#endif
