/* SPDX-License-Identifier: GPL-3.0-only */
/* Each note's own end (POLY notes recorded over each other with REC QUANTIZE OFF; docs/UI-OPTIMIST-DESIGN.md).
 *
 * In RAM a synth step carries ngate[4] (core.h step_t): note k ends ngate[k] / 128 of the step after the step's start,
 * 0 = at the step's GATE, as before. The playback (seq.c seq_step, seq_noffs), the edits, undo and the displays use it
 * as it is. A stored step has no room for it (pstep_t, 10 bytes: project format FUNB is full), so every stored copy
 * keeps it as step-only events of the track's automation list (auto.h):
 *
 *   place  the step | AUTO_ONLY
 *   param  AUTO_NGATE0 + k (0x78 .. 0x7B, note slot k): stored ids past every build's track values (auto.c
 *          MOT_TAIL_END, 79) and below the drum lanes' (0x7C..), so neither a track's value nor a lane's on any track
 *   value  ngate[k], 1 .. 127
 *
 * Unfolded (ngate -> events) where the working tracks are captured into a project buffer or a stage (project.c
 * proj_capture, pat.c), folded back (events -> ngate, the events removed: the working lists never hold them) where a
 * buffer is applied to the working tracks (proj_apply, pat.c pat_take). A buffer's store is what every stored form
 * (the project's extras record, a section, a pattern record, a snapshot) is written from, so they all carry them.
 * An older build keeps the events and plays none of them (mot_id: P_COUNT, no lock): its notes end at the steps.
 * A list full when unfolding: that note's end is not kept (it plays the step's GATE after a load).
 *
 * The drum track's ACCENT lane (core.h DRUM_ACC, dstep_t.acc) is kept the same way, at the same places: each step with
 * it on is a step-only event AUTO_ACC (auto.h), value its acc byte; no event: no accent (a project from before). */
#ifndef FELUCCA_NGATE_H
#define FELUCCA_NGATE_H

#include "auto.h"

/* a track's stored steps <-> its working steps (the drum track: its 16-lane steps, 10 bytes each stored; the ACCENT
 * lane's byte: an event, below) */
static inline void steps_store(pstep_t *d, const track_t *t, uint32_t k)
{
    uint32_t i;
    if (k == TRK_DRUM) {
        for (i = 0; i < NSTEP; i++)
            memcpy(&d[i], &t->dstep[i], DSTEP_SN);
        return;
    }
    for (i = 0; i < NSTEP; i++)
        memcpy(&d[i], &t->step[i], STEP_SN);
}
static inline void steps_load(track_t *t, uint32_t k, const pstep_t *s)
{
    uint32_t i;
    if (k == TRK_DRUM) {
        memset(t->step, 0, sizeof t->step);          /* (the ACCENT lane too: ngate_fold puts it back) */
        for (i = 0; i < NSTEP; i++)
            memcpy(&t->dstep[i], &s[i], DSTEP_SN);
        return;
    }
    for (i = 0; i < NSTEP; i++) {
        memcpy(&t->step[i], &s[i], STEP_SN);
        memset(t->step[i].ngate, 0, sizeof t->step[i].ngate);
    }
}
/* note k of step s plays its own end (the step's notes: a NOTE step, no ratchet) */
static inline uint32_t ngate_of(const step_t *s, uint32_t k)
{
    return s->time == ST_NOTE && k < s->n && k < 4u && !s->rat ? s->ngate[k] : 0u;
}
/* the drum track's ACCENT lane (st: its steps' union, trk[].step) added to list l as events; -> the steps that did not fit */
static inline uint32_t dacc_unfold(auto_list_t *l, const step_t *st)
{
    const dstep_t *d = (const dstep_t *)(const void *)st;
    uint32_t i, cut = 0;
    for (i = 0; i < NSTEP; i++)
        if ((d[i].acc & 1u) && auto_put(l, i | AUTO_ONLY, AUTO_ACC, d[i].acc & 31u) < 0)
            cut++;
    return cut;
}
/* the steps' own ends added to list l as events (track k; the drum track: its ACCENT lane); -> the ends that did not fit */
static inline uint32_t ngate_unfold(auto_list_t *l, const step_t *st, uint32_t k)
{
    uint32_t i, j, cut = 0;
    if (k == TRK_DRUM)
        return dacc_unfold(l, st);
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < 4u; j++) {
            uint32_t g = ngate_of(&st[i], j);
            if (g && auto_put(l, i | AUTO_ONLY, AUTO_NGATE0 + j, (int32_t)g) < 0)
                cut++;
        }
    return cut;
}
/* list l's own-end events into the steps (track k), every one of them taken out of l (the drum track: dropped,
 * they mean nothing there); the ACCENT lane's events the same way (the drum track's steps; on a part: dropped);
 * -> how many were taken out */
static inline uint32_t ngate_fold(step_t *st, auto_list_t *l, uint32_t k)
{
    uint32_t i, n = 0, out = 0;
    for (i = 0; i < l->n && i < AUTO_MAX; i++) {
        const auto_ev_t *e = &l->ev[i];
        if (e->param == AUTO_ACC) {
            out++;
            if (k == TRK_DRUM && (e->place & AUTO_ONLY) && (e->value & 1))
                ((dstep_t *)(void *)st)[e->place & AUTO_STEP].acc = (uint8_t)(e->value & 31);
            continue;
        }
        if (!AUTO_IS_NGATE(e->param)) {
            l->ev[n++] = *e;
            continue;
        }
        out++;
        if (k != TRK_DRUM && (e->place & AUTO_ONLY) && e->value >= 1) {
            step_t *s = &st[e->place & AUTO_STEP];
            uint32_t j = e->param - AUTO_NGATE0;
            if (s->time == ST_NOTE && j < s->n)
                s->ngate[j] = (uint8_t)e->value;
        }
    }
    l->n = (uint8_t)n;
    return out;
}
/* track k's steps hold an own end (the SLOOP 2.4 export: 2.4 has one GATE a step) */
static inline int ngate_any(const step_t *st, uint32_t k)
{
    uint32_t i, j;
    if (k == TRK_DRUM)
        return 0;
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < 4u; j++)
            if (ngate_of(&st[i], j))
                return 1;
    return 0;
}
/* the drum track's steps (st: the union) hold an ACCENT lane step (the SLOOP 2.4 export: 2.4 has none) */
static inline int dacc_any(const step_t *st)
{
    const dstep_t *d = (const dstep_t *)(const void *)st;
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (d[i].acc & 1u)
            return 1;
    return 0;
}
/* the own ends a track's steps hold (the list's room: the live recording keeps one only while the list has room) */
static inline uint32_t ngate_count(const step_t *st)
{
    uint32_t i, j, n = 0;
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < 4u; j++)
            n += ngate_of(&st[i], j) != 0;
    return n;
}

#endif
