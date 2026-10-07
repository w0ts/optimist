/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: SLOOP 2.4's step extras of a track (seq24.c STEPX: nudge, locks, fill), the step detail of the web
 * editor (included by editor.c with SL24_STEPX; web/EDITOR_PROTOCOL.md "Step extras"). 2.4 has the same commands as
 * its 37..42; here 72..77 (37..42 are ours already). A param is OUR P_* id. Writes take the IRQ off.
 *   72 LOCK_GET   track                   -> track, n, n x (step, param, v14)
 *   73 LOCK_SET   track, step, param [, v14: absent = delete] -> track, step, param, rc (1 set / deleted, 0 not
 *                 lockable or the 24 slots full), v14 (the value kept, clamped; 0 deleted)
 *   74 MICRO_GET  track                   -> track, 64 x (nudge + 64)
 *   75 MICRO_SET  track, step, nudge + 64 -> track, step, nudge + 64 (clamped to -32..31)
 *   76 FILL_GET   track                   -> track, 64 x condition (0 normal, 1 fill only, 2 no fill)
 *   77 FILL_SET   track, step, condition  -> track, step, condition */
enum { ED_LOCK_GET = 72, ED_LOCK_SET, ED_MICRO_GET, ED_MICRO_SET, ED_FILL_GET, ED_FILL_SET };

static int ed_stepx(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    track_t *t;
    stepx_t *x;
    uint32_t i, n = 0, at;
    if (cmd < ED_LOCK_GET || cmd > ED_FILL_SET || na < 1u || a[0] >= NTRK)
        return 0;
    t = &trk[a[0]];
    x = TX(t);
    ed_b(a[0]);
    switch (cmd) {
    case ED_LOCK_GET:
        at = ed_n;
        ed_b(0);
        for (i = 0; i < NLOCK; i++)
            if (stepx_lock_used(&x->lock[i])) {
                ed_b(x->lock[i].step);
                ed_b(x->lock[i].param);
                ed_v(x->lock[i].val);
                n++;
            }
        ed_out[at] = (uint8_t)n;
        return 1;
    case ED_LOCK_SET: {
        int q, ok = 0;
        if (na < 3u || a[1] >= NSTEP)
            return 0;
        fm1_irq_off();
        if (na < 5u) {                                    /* (no value: the lock goes) */
            q = stepx_lock_find(x, a[1], a[2]);
            if (q >= 0)
                x->lock[q].step = LOCK_FREE, x->lock[q].param = 0, x->lock[q].val = 0;
            ok = 1;
        } else {
#if FELUCCA_PLOCK
            ok = lock_set(t, a[1], a[2], ed_rv(a + 3));
#endif
        }
        q = stepx_lock_find(x, a[1], a[2]);
        fm1_irq_on();
        ed_b(a[1]);
        ed_b(a[2]);
        ed_b((uint32_t)ok);
        ed_v(q >= 0 ? x->lock[q].val : 0);
        return 1;
    }
    case ED_MICRO_GET:
        for (i = 0; i < NSTEP; i++)
            ed_b((uint32_t)(x->micro[i] + 64));
        return 1;
    case ED_FILL_GET:
        for (i = 0; i < NSTEP; i++)
            ed_b(stepx_fill(x, i));
        return 1;
    case ED_MICRO_SET:
    case ED_FILL_SET:
        if (na < 3u || a[1] >= NSTEP)
            return 0;
        fm1_irq_off();
        if (cmd == ED_MICRO_SET)
            x->micro[a[1]] = (int8_t)clamp((int32_t)a[2] - 64, MICRO_MIN, MICRO_MAX);
        else
            stepx_fill_set(x, a[1], a[2]);
        fm1_irq_on();
        ed_b(a[1]);
        ed_b(cmd == ED_MICRO_SET ? (uint32_t)(x->micro[a[1]] + 64) : stepx_fill(x, a[1]));
        return 1;
    default:
        return 0;
    }
}
