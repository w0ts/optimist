/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4's step extras (isod89/sloop-fm1 v2.4, 8d3823f, core.h: per-step nudge, parameter locks, fill
 * conditions), FELUCCA_SL24_XSTEP (backports24.h). One track's is stepx_t, laid out byte for byte as 2.4 keeps it in
 * its track_t and as its FUN5 project stores it after each track's steps (project.c proj_trk_t: micro at +764, lock
 * at +828, fill at +924 of the 940-byte track; 176 bytes):
 *
 *   int8_t  micro[64]   each step's nudge, MICRO_MIN..MICRO_MAX in 1/64 of the step (- early, + late; 0 on the grid)
 *   plock_t lock[24]    parameter locks, unsorted, several a step (other params); step LOCK_FREE = a free slot
 *   uint8_t fill[16]    each step's condition, 2 bits: step i in byte i / 4, bits 2 (i % 4): FC_* (3 reads as 0)
 *
 * One difference, on purpose (docs/SLOOP24-BACKPORT.md 1.3): a lock's param is OUR P_* id. 2.4's ids differ from 50
 * up (its P_TFLT 50, P_STRUM 51, P_VLEAD 52, P_E0 53; ours P_FXOFF 50, ANALOG 2 51.., P_E0 67): the importer and the
 * exporter (sl24_import.c, to come) convert. Everything else is 2.4's value as is.
 *
 * Header only: the type, the limits and small pure helpers (no state), so the sequencer (phase 2), the codec, the
 * importer and the host tests share it. The working copy and its storage (the section log, the autosave): stepx.c,
 * next (FELUCCA_SL24_XSTEP). */
#ifndef FELUCCA_STEPX_H
#define FELUCCA_STEPX_H

#define SX_NSTEP 64u                     /* (NSTEP: core.h) */
#define MICRO_MIN (-32)                  /* a step's nudge in 1/64 of its length: half a step early .. */
#define MICRO_MAX 31                     /* .. just under half a step late (0 = on the grid) */
#define NLOCK 24u                        /* parameter locks per track (several may share a step: other params) */
#define LOCK_FREE 0xFFu                  /* plock_t.step of a free slot */
enum { FC_NORM, FC_FILL, FC_NOFILL };    /* a step's condition: plays always / only in a fill / never in one (3: NORM) */

typedef struct {                         /* a lock: on step `step` the track's p[param] is `val` (4 bytes) */
    uint8_t step;                        /* 0..63; LOCK_FREE = a free slot */
    uint8_t param;                       /* OUR P_* id (2.4's converted at the edges) */
    int16_t val;
} plock_t;
typedef struct {                         /* one track's extras: 2.4's FUN5 track tail */
    int8_t micro[SX_NSTEP];
    plock_t lock[NLOCK];
    uint8_t fill[SX_NSTEP / 4u];
} stepx_t;
_Static_assert(sizeof(plock_t) == 4u && sizeof(stepx_t) == 176u && __builtin_offsetof(stepx_t, lock) == 64u &&
               __builtin_offsetof(stepx_t, fill) == 160u, "SLOOP 2.4's track tail (FUN5 +764 / +828 / +924)");

static inline void stepx_clear(stepx_t *x)            /* none: no nudge, every lock slot free, every step NORM */
{
    uint32_t i;
    for (i = 0; i < SX_NSTEP; i++)
        x->micro[i] = 0;
    for (i = 0; i < NLOCK; i++)
        x->lock[i].step = LOCK_FREE, x->lock[i].param = 0, x->lock[i].val = 0;
    for (i = 0; i < SX_NSTEP / 4u; i++)
        x->fill[i] = 0;
}
static inline uint32_t stepx_fill(const stepx_t *x, uint32_t i)   /* step i's FC_* (3 -> FC_NORM) */
{
    uint32_t c = (x->fill[(i % SX_NSTEP) / 4u] >> (2u * (i % 4u))) & 3u;
    return c == 3u ? FC_NORM : c;
}
static inline void stepx_fill_set(stepx_t *x, uint32_t i, uint32_t c)
{
    uint8_t *b = &x->fill[(i % SX_NSTEP) / 4u];
    uint32_t s = 2u * (i % 4u);
    *b = (uint8_t)((*b & ~(3u << s)) | ((c > FC_NOFILL ? FC_NORM : c) << s));
}
static inline int stepx_lock_used(const plock_t *l) { return l->step < SX_NSTEP; }
static inline int stepx_is_empty(const stepx_t *x)    /* nothing a step would do differently */
{
    uint32_t i;
    for (i = 0; i < SX_NSTEP; i++)
        if (x->micro[i] || stepx_fill(x, i))
            return 0;
    for (i = 0; i < NLOCK; i++)
        if (stepx_lock_used(&x->lock[i]))
            return 0;
    return 1;
}
/* the lock of (step, param), -1 none */
static inline int stepx_lock_find(const stepx_t *x, uint32_t step, uint32_t param)
{
    uint32_t i;
    for (i = 0; i < NLOCK; i++)
        if (x->lock[i].step == step && x->lock[i].param == param)
            return (int)i;
    return -1;
}
/* set (step, param) to val: its lock, else a free slot; -1 all 24 taken */
static inline int stepx_lock_set(stepx_t *x, uint32_t step, uint32_t param, int16_t val)
{
    int i = stepx_lock_find(x, step, param);
    uint32_t k;
    if (i < 0)
        for (k = 0; k < NLOCK && i < 0; k++)
            if (!stepx_lock_used(&x->lock[k]))
                i = (int)k;
    if (i < 0 || step >= SX_NSTEP)
        return -1;
    x->lock[i].step = (uint8_t)step, x->lock[i].param = (uint8_t)param, x->lock[i].val = val;
    return i;
}
static inline void stepx_step_clear(stepx_t *x, uint32_t step)   /* 2.4's OCT-: the step's nudge, locks and fill */
{
    uint32_t i;
    x->micro[step % SX_NSTEP] = 0;
    stepx_fill_set(x, step, FC_NORM);
    for (i = 0; i < NLOCK; i++)
        if (x->lock[i].step == step)
            x->lock[i].step = LOCK_FREE, x->lock[i].param = 0, x->lock[i].val = 0;
}

/* The stored form (the section log, the autosave, the editor; stepx.c): per track, three counted lists, only what
 * is not the default:
 *   n_micro, n_micro x (step, int8 nudge); n_lock, n_lock x (step, param, int16 val LE); n_fill, n_fill x (step, c)
 * Four tracks of nothing: 12 zero bytes, and stepx_encode says 0 (store nothing). Worst case STEPX_ENC_MAX. */
#define STEPX_ENC_TRK_MAX (3u + 2u * SX_NSTEP + 4u * NLOCK + 2u * SX_NSTEP)
static inline uint32_t stepx_encode_trk(const stepx_t *x, uint8_t *o)   /* -> bytes written */
{
    uint32_t i, n = 0, c;
    uint8_t *cnt = &o[n++];
    *cnt = 0;
    for (i = 0; i < SX_NSTEP; i++)
        if (x->micro[i])
            o[n++] = (uint8_t)i, o[n++] = (uint8_t)x->micro[i], (*cnt)++;
    cnt = &o[n++];
    *cnt = 0;
    for (i = 0; i < NLOCK; i++)
        if (stepx_lock_used(&x->lock[i])) {
            o[n++] = x->lock[i].step, o[n++] = x->lock[i].param;
            o[n++] = (uint8_t)x->lock[i].val, o[n++] = (uint8_t)((uint16_t)x->lock[i].val >> 8);
            (*cnt)++;
        }
    cnt = &o[n++];
    *cnt = 0;
    for (i = 0; i < SX_NSTEP; i++)
        if ((c = stepx_fill(x, i)) != FC_NORM)
            o[n++] = (uint8_t)i, o[n++] = (uint8_t)c, (*cnt)++;
    return n;
}
/* *a .. e -> x (cleared first); 0 cut short or out of range (x then cleared) */
static inline int stepx_decode_trk(stepx_t *x, const uint8_t **a, const uint8_t *e)
{
    const uint8_t *p = *a;
    uint32_t n, k;
    stepx_clear(x);
    if (p >= e || (n = *p++) > SX_NSTEP || (uint32_t)(e - p) < 2u * n)
        goto bad;
    for (k = 0; k < n; k++, p += 2) {
        if (p[0] >= SX_NSTEP || (int8_t)p[1] < MICRO_MIN || (int8_t)p[1] > MICRO_MAX)
            goto bad;
        x->micro[p[0]] = (int8_t)p[1];
    }
    if (p >= e || (n = *p++) > NLOCK || (uint32_t)(e - p) < 4u * n)
        goto bad;
    for (k = 0; k < n; k++, p += 4) {
        if (p[0] >= SX_NSTEP)
            goto bad;
        x->lock[k].step = p[0], x->lock[k].param = p[1], x->lock[k].val = (int16_t)(p[2] | p[3] << 8);
    }
    if (p >= e || (n = *p++) > SX_NSTEP || (uint32_t)(e - p) < 2u * n)
        goto bad;
    for (k = 0; k < n; k++, p += 2) {
        if (p[0] >= SX_NSTEP || p[1] > FC_NOFILL)
            goto bad;
        stepx_fill_set(x, p[0], p[1]);
    }
    *a = p;
    return 1;
bad:
    stepx_clear(x);
    return 0;
}

#endif
