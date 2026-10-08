/* SPDX-License-Identifier: GPL-3.0-only */
/* The automation store (docs/UI-OPTIMIST-DESIGN.md section 6, phase 3): one list of events per pattern (a track's
 * working pattern, a pattern record), FELUCCA_AUTO. An event is Felucca's motion event, 3 bytes, with one bit given a
 * meaning:
 *
 *   byte 0  place: bits 0..5 the step (0..63), bit 6 STEP-ONLY (AUTO_ONLY), bit 7 0 (reserved)
 *   byte 1  the parameter: a stored id (motion.c mot_sid: build-independent), or a pseudo-parameter past every
 *           build's P_COUNT: AUTO_NUDGE (-32..31, 1/64 of a step), AUTO_FILL (1 FILL ONLY, 2 NO FILL), AUTO_CHANCE
 *           (0..99 %; none: 100 %), always STEP-ONLY
 *   byte 2  the value, a signed byte (every lockable and recordable value fits: tests/auto_int8_check.c)
 *
 * A step-only event is a lock (Elektron style: it holds its step, the parameter goes back at the next step without
 * one); a hold event is motion (it holds until the next event of that parameter, or the pattern restart). A list
 * holds at most AUTO_MAX events, each (place, param) once (a hold and a step-only event of one parameter on one step
 * are two). The order is the order they were written; the sequencer reads them in it.
 *
 * Header only: the type, small pure helpers, SLOOP 2.4's stepx record (stepx.h) as an import / export form, and the
 * stored forms (a track's list: n, n x 3 bytes; a store's: AUTO_ENC_TAG, version, the PLAY bits, the four lists), so
 * the sequencer (auto.c), the storage, the editor and the host tests share them. */
#ifndef FELUCCA_AUTO_H
#define FELUCCA_AUTO_H

#include "stepx.h"

#define AUTO_MAX 128u                    /* events a list (a pattern) */
#define AUTO_NTRK 4u                     /* (NTRK: core.h; the store's tracks) */
#define AUTO_STEP 0x3Fu                  /* place: the step */
#define AUTO_ONLY 0x40u                  /* place: this step only (a lock); 0 = a hold (motion) */
#define AUTO_NUDGE 0xFDu                 /* the pseudo-parameters (stored ids: past every build's P_COUNT) */
#define AUTO_FILL 0xFEu
#define AUTO_CHANCE 0xFFu
#define AUTO_PSEUDO(p) ((uint32_t)(p) >= AUTO_NUDGE)
#define AUTO_HOLDS 1u                    /* auto_drop_*: which kinds */
#define AUTO_ONLYS 2u

typedef struct { uint8_t place, param; int8_t value; } auto_ev_t;
typedef struct {
    uint8_t n;                           /* events in use */
    auto_ev_t ev[AUTO_MAX];
} auto_list_t;
_Static_assert(sizeof(auto_ev_t) == 3u && sizeof(auto_list_t) == 1u + 3u * AUTO_MAX, "the event: 3 bytes");

/* a pseudo-parameter's value is one it can hold (its default, which needs no event, is not) */
static inline int auto_pseudo_ok(uint32_t param, int32_t v)
{
    return param == AUTO_NUDGE ? v >= MICRO_MIN && v <= MICRO_MAX && v != 0
         : param == AUTO_FILL ? v == FC_FILL || v == FC_NOFILL
         : v >= 0 && v <= 99;
}
/* an event as a list may hold it (structure only: whether this build plays the parameter is the sequencer's) */
static inline int auto_ev_ok(const auto_ev_t *e)
{
    return !(e->place & 0x80u) && (!AUTO_PSEUDO(e->param) || ((e->place & AUTO_ONLY) && auto_pseudo_ok(e->param, e->value)));
}
static inline int auto_find(const auto_list_t *l, uint32_t place, uint32_t param)   /* its index, -1 none */
{
    uint32_t i;
    for (i = 0; i < l->n && i < AUTO_MAX; i++)
        if (l->ev[i].place == place && l->ev[i].param == param)
            return (int)i;
    return -1;
}
static inline void auto_del(auto_list_t *l, uint32_t i)                /* event i goes, the order kept */
{
    if (i >= l->n)
        return;
    for (; i + 1u < l->n; i++)
        l->ev[i] = l->ev[i + 1u];
    l->n--;
}
/* (place, param) set to v: its event's value, else a new one at the end; -1 the list is full */
static inline int auto_put(auto_list_t *l, uint32_t place, uint32_t param, int32_t v)
{
    int i = auto_find(l, place, param);
    if (i < 0) {
        if (l->n >= AUTO_MAX)
            return -1;
        i = l->n++;
        l->ev[i].place = (uint8_t)place;
        l->ev[i].param = (uint8_t)param;
    }
    l->ev[i].value = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
    return i;
}
/* the value of (place, param) into *v: 1 there is one */
static inline int auto_get(const auto_list_t *l, uint32_t place, uint32_t param, int32_t *v)
{
    int i = auto_find(l, place, param);
    if (i >= 0)
        *v = l->ev[i].value;
    return i >= 0;
}
/* a pseudo-parameter of step `step` set (its default: the event goes); 0 the list is full */
static inline int auto_pseudo_set(auto_list_t *l, uint32_t step, uint32_t param, int32_t v)
{
    uint32_t pl = (step & AUTO_STEP) | AUTO_ONLY;
    int i;
    if (!auto_pseudo_ok(param, v)) {
        if ((i = auto_find(l, pl, param)) >= 0)
            auto_del(l, (uint32_t)i);
        return 1;
    }
    return auto_put(l, pl, param, v) >= 0;
}
static inline int32_t auto_pseudo(const auto_list_t *l, uint32_t step, uint32_t param, int32_t none)
{
    int32_t v;
    return auto_get(l, (step & AUTO_STEP) | AUTO_ONLY, param, &v) ? v : none;
}
/* the events of step `step` of the kinds `which` (AUTO_HOLDS, AUTO_ONLYS) go; -> how many */
static inline uint32_t auto_drop_step(auto_list_t *l, uint32_t step, uint32_t which)
{
    uint32_t i, n = 0, k = 0;
    for (i = 0; i < l->n; i++) {
        const auto_ev_t *e = &l->ev[i];
        if ((e->place & AUTO_STEP) == step && (which & (e->place & AUTO_ONLY ? AUTO_ONLYS : AUTO_HOLDS)))
            k++;
        else
            l->ev[n++] = *e;
    }
    l->n = (uint8_t)n;
    return k;
}
static inline uint32_t auto_drop_kind(auto_list_t *l, uint32_t which)   /* every event of the kinds `which` */
{
    uint32_t i, n = 0, k = 0;
    for (i = 0; i < l->n; i++)
        if (which & (l->ev[i].place & AUTO_ONLY ? AUTO_ONLYS : AUTO_HOLDS))
            k++;
        else
            l->ev[n++] = l->ev[i];
    l->n = (uint8_t)n;
    return k;
}
static inline uint32_t auto_count(const auto_list_t *l, uint32_t which)
{
    uint32_t i, k = 0;
    for (i = 0; i < l->n; i++)
        k += (which & (l->ev[i].place & AUTO_ONLY ? AUTO_ONLYS : AUTO_HOLDS)) != 0;
    return k;
}

/* ---- SLOOP 2.4's stepx record (stepx.h), the import / export form. Its lock params are OUR ids, which are the
 * stored ids for every lockable value (they differ only past P_E7, where the one lockable value, 2.4's FILT, is
 * P_ENG_END in both) */
enum { AUTO_CUT = 1, AUTO_CLAMP = 2 };   /* auto_from_stepx: the list was full, a lock's value clamped to int8 */
enum { AUTO_X_HOLD = 1, AUTO_X_CHANCE = 2, AUTO_X_LOCKS = 4 };   /* auto_to_stepx: not in the stepx form */
/* x's nudges, locks and fills added to l as step-only events (in that order, the locks in slot order) */
static inline uint32_t auto_from_stepx(auto_list_t *l, const stepx_t *x)
{
    uint32_t i, r = 0;
    for (i = 0; i < SX_NSTEP; i++)
        if (x->micro[i] >= MICRO_MIN && x->micro[i] <= MICRO_MAX && x->micro[i] && !auto_pseudo_set(l, i, AUTO_NUDGE, x->micro[i]))
            r |= AUTO_CUT;
    for (i = 0; i < NLOCK; i++) {
        const plock_t *k = &x->lock[i];
        if (!stepx_lock_used(k) || AUTO_PSEUDO(k->param))
            continue;
        if (k->val < -128 || k->val > 127)
            r |= AUTO_CLAMP;
        if (auto_put(l, k->step | AUTO_ONLY, k->param, k->val) < 0)
            r |= AUTO_CUT;
    }
    for (i = 0; i < SX_NSTEP; i++)
        if (stepx_fill(x, i) != FC_NORM && !auto_pseudo_set(l, i, AUTO_FILL, (int32_t)stepx_fill(x, i)))
            r |= AUTO_CUT;
    return r;
}
/* l's step-only nudges, fills and its first NLOCK step-only parameter events -> x (cleared first); what x cannot
 * hold: AUTO_X_HOLD (hold events), AUTO_X_CHANCE, AUTO_X_LOCKS (step-only events past the 24th) */
static inline uint32_t auto_to_stepx(stepx_t *x, const auto_list_t *l)
{
    uint32_t i, m = 0, r = 0;
    stepx_clear(x);
    for (i = 0; i < l->n; i++) {
        const auto_ev_t *e = &l->ev[i];
        uint32_t s = e->place & AUTO_STEP;
        if (!(e->place & AUTO_ONLY))
            r |= AUTO_X_HOLD;
        else if (e->param == AUTO_NUDGE)
            x->micro[s] = e->value;
        else if (e->param == AUTO_FILL)
            stepx_fill_set(x, s, (uint32_t)e->value);
        else if (e->param == AUTO_CHANCE)
            r |= AUTO_X_CHANCE;
        else if (m < NLOCK)
            x->lock[m].step = (uint8_t)s, x->lock[m].param = e->param, x->lock[m].val = e->value, m++;
        else
            r |= AUTO_X_LOCKS;
    }
    return r;
}

/* ---- the stored forms. A track's list (a pattern record's chunk): n, n x (place, param, value) */
#define AUTO_TRK_ENC_MAX (1u + 3u * AUTO_MAX)
static inline uint32_t auto_enc_trk(const auto_list_t *l, uint8_t *o)
{
    uint32_t i;
    o[0] = l->n;
    for (i = 0; i < l->n; i++)
        o[1u + 3u * i] = l->ev[i].place, o[2u + 3u * i] = l->ev[i].param, o[3u + 3u * i] = (uint8_t)l->ev[i].value;
    return 1u + 3u * l->n;
}
/* *a .. e -> l; 0 cut short, too many, a malformed event or one twice (l then empty) */
static inline int auto_dec_trk(auto_list_t *l, const uint8_t **a, const uint8_t *e)
{
    const uint8_t *p = *a;
    uint32_t n, i;
    l->n = 0;
    if (p >= e || (n = *p++) > AUTO_MAX || (uint32_t)(e - p) < 3u * n)
        return 0;
    for (i = 0; i < n; i++, p += 3) {
        auto_ev_t v = {p[0], p[1], (int8_t)p[2]};
        if (!auto_ev_ok(&v) || auto_find(l, v.place, v.param) >= 0) {
            l->n = 0;
            return 0;
        }
        l->ev[l->n++] = v;
    }
    *a = p;
    return 1;
}
/* a store's lists (the extras records, the snapshots, the editor's backup): AUTO_ENC_TAG (a stepx record never
 * starts so: its first count is <= 64), the version, the PLAY bits, then each track's list. An older build reads
 * the tag as a bad stepx record: refused, nothing played or kept wrong */
#define AUTO_ENC_TAG 0xFFu
#define AUTO_ENC_V1 1u
#define AUTO_ENC_MAX (3u + AUTO_NTRK * AUTO_TRK_ENC_MAX)
static inline uint32_t auto_enc_store(const auto_list_t *l, uint32_t on, uint8_t *o)
{
    uint32_t k, n = 3;
    o[0] = AUTO_ENC_TAG, o[1] = AUTO_ENC_V1, o[2] = (uint8_t)on;
    for (k = 0; k < AUTO_NTRK; k++)
        n += auto_enc_trk(&l[k], o + n);
    return n;
}
static inline int auto_is_store(const uint8_t *a, uint32_t n) { return n >= 1u && a[0] == AUTO_ENC_TAG; }
/* n bytes at a (auto_is_store) -> l[AUTO_NTRK], *on; 0 refused (l empty) */
static inline int auto_dec_store(auto_list_t *l, uint8_t *on, const uint8_t *a, uint32_t n)
{
    const uint8_t *e = a + n;
    uint32_t k;
    for (k = 0; k < AUTO_NTRK; k++)
        l[k].n = 0;
    *on = 0;
    if (n < 3u || a[0] != AUTO_ENC_TAG || a[1] != AUTO_ENC_V1 || (a[2] & ~((1u << AUTO_NTRK) - 1u)))
        return 0;
    for (k = 0, a += 3; k < AUTO_NTRK; k++)
        if (!auto_dec_trk(&l[k], &a, e))
            break;
    if (k < AUTO_NTRK || a != e) {
        for (k = 0; k < AUTO_NTRK; k++)
            l[k].n = 0;
        return 0;
    }
    *on = (e - n)[2];
    return 1;
}

#endif
