/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: a track's automation (seq/auto.h: locks, motion, nudges, fills, chance; included by editor.c with
 * FELUCCA_AUTO; web/EDITOR_PROTOCOL.md "The automation store"). v11 (phase 3; v10 is main's FX slots, 86 FX, 87 FX_PUSH): one pair, AUTO_GET / AUTO_SET, reads and
 * writes the list as it is. Writes take the IRQ off. (The older step extras' commands 72..77, SLOOP 2.4's 37..42, are gone
 * since 2026-10-19: the web editor reads and writes the list.)
 *   92 AUTO_GET  track [, first]          -> track, n (the list's events, 2 x 7 bit), the PLAY bit, first, count, then
 *                count x (place, param (2 x 7 bit: the stored id), value v14); at most 64 a reply: ask again from
 *                first + count
 *   93 AUTO_SET  track, op, ..           -> track, op, rc (0 done, 1 refused: not one this track takes, 2 the list is
 *                full), n (2 x 7 bit), then op 0: the value kept (v14)
 *                op 0 SET place, param (2 x 7 bit), v14 (clamped to the value's range)
 *                op 1 DEL place, param (2 x 7 bit)
 *                op 2 CLEAR which (1 the hold events, 2 the step-only ones, 3 all)
 *                op 3 PLAY on (0 / 1: the track's hold events play) */
#include "ed_reply.h"         /* (ed_b2..ed_b4, ed_b32) */
#include "ed_out.h"            /* (ED_PAYLOAD_N) */
enum { ED_AUTO_GET = 92, ED_AUTO_SET = 93 };   /* (85 is kept for a push of the patterns: EDITOR_PROTOCOL.md) */
#define ED_AUTO_PAGE 64u
_Static_assert(9u + 5u * ED_AUTO_PAGE <= ED_PAYLOAD_N, "AUTO_GET: a page of events fits one reply");

/* track t takes event (place, stored param) at all: its range into *d (0: a pseudo-parameter) */
static int ed_auto_ok(const track_t *t, uint32_t place, uint32_t sp, const param_desc_t **d)
{
    uint32_t id = mot_id(sp);
    *d = 0;
    if (place & 0x80u)
        return 0;
    if (AUTO_PSEUDO(sp))
        return (place & AUTO_ONLY) != 0;
    if (AUTO_IS_NGATE(sp))
        return 0;                                         /* (a note's own end: the step's bytes, never a working list's) */
    if (AUTO_IS_LANE(sp)) {                               /* (a drum lane's value: the drum track only, auto.h) */
#if DLA_ON
        if (is_drum(t) && ((place & AUTO_ONLY) ? FELUCCA_PLOCK : FELUCCA_MOTION))
            return *d = &DLA_DESC[AUTO_LANE_V(sp)], 1;
#endif
        return 0;
    }
    if (place & AUTO_ONLY) {
#if FELUCCA_PLOCK
        if (lock_ok(t, id))
            return *d = lock_desc(t, id), 1;
#endif
        return 0;
    }
#if FELUCCA_MOTION
    if (motion_param(t, id))
        return *d = track_desc(t, id), 1;
#endif
    (void)t, (void)id;
    return 0;
}

static int ed_auto(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    track_t *t = &trk[a[0] % NTRK];
    auto_list_t *l = AL(t);
    uint32_t i, rc = 0, n;
    int32_t v = 0;
    if (cmd == ED_AUTO_GET) {
        uint32_t first = na >= 2u ? a[1] : 0u;
        n = l->n;
        ed_b4(n, n >> 7, (auto_w.on >> trk_index(t)) & 1u, first);
        n = first < n ? n - first : 0u;
        n = n < ED_AUTO_PAGE ? n : ED_AUTO_PAGE;
        ed_b(n);
        for (i = first; i < first + n; i++) {
            ed_b3(l->ev[i].place, l->ev[i].param, (uint32_t)l->ev[i].param >> 7);
            ed_v(l->ev[i].value);
        }
        return 1;
    }
    if (na < 2u)
        return 0;
    ed_b(a[1]);
    if (a[1] != 3u)
        ed_undo_edit(t);                                      /* (v14: the editor's write is an undo level; PLAY is not the pattern) */
    fm1_irq_off();
    switch (a[1]) {
    case 0:
    case 1: {
        const param_desc_t *d;
        uint32_t pl = na >= 5u ? a[2] : 0x80u, sp = na >= 5u ? ed_r32(a + 3, 2) : 0u;
        if (na < 5u || sp > 0xFFu || !ed_auto_ok(t, pl, sp, &d)) {
            rc = 1;
            break;
        }
        if (a[1] == 1u) {
            int q = auto_find(l, pl, sp);
            if (q >= 0)
                auto_del(l, (uint32_t)q);
            break;
        }
        if (na < 7u) {
            rc = 1;
            break;
        }
        v = ed_rv(a + 5);
        if (d)
            rc = auto_put(l, pl, sp, clamp(v, d->min, d->max)) < 0 ? 2u : 0u;
        else if (!auto_pseudo_ok(sp, v) && v != (sp == AUTO_CHANCE ? 100 : 0))
            rc = 1;                                       /* (out of its range; its default: the event goes) */
        else
            rc = auto_pseudo_set(l, pl & AUTO_STEP, sp, v) ? 0u : 2u;
        if (!rc && !(pl & AUTO_ONLY))
            auto_w.on |= (uint8_t)(1u << trk_index(t));
        (void)auto_get(l, pl, sp, &v);
        break;
    }
    case 2:
        (void)auto_drop_kind(l, na >= 3u ? a[2] & 3u : 0u);
        break;
    case 3:
        if (na >= 3u && a[2])
            auto_w.on |= (uint8_t)(1u << trk_index(t));
        else
            auto_w.on &= (uint8_t)~(1u << trk_index(t));
        break;
    default:
        rc = 1;
    }
    auto_touch(trk_index(t));
    fm1_irq_on();
#if FELUCCA_MOTION
    if (a[1] == 3u && !((auto_w.on >> trk_index(t)) & 1u)) {
        fm1_irq_off();
        motion_restore(t);                                /* (its motion off: the patch back) */
        fm1_irq_on();
    }
#endif
    ed_b3(rc, l->n, (uint32_t)l->n >> 7);
    if (a[1] == 0u)
        ed_v(v);
    return 1;
}

static int ed_stepx(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    if ((cmd != ED_AUTO_GET && cmd != ED_AUTO_SET) || na < 1u || a[0] >= NTRK)
        return 0;
    ed_b(a[0]);
    return ed_auto(cmd, a, na);
}
#define ED_STEPX_FN ed_stepx,               /* (editor.c: its table of commands) */
