/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: a step on the wire (included by editor.c; also by tests/editor_sync_test.c). STEP_GET / TRACK_STEP
 * bytes (n, note0..3, time, flags, vel, lvl, hi, rat; the drum track as GM notes for v1..v4 editors) and the DRUM_STEP
 * bytes (on 3 x 7 bit, lvl 5 x 7 bit, rat 5 x 7 bit); the v9 STEPS push (ed_sync9.c) sends the same. */
/* the drum track's step as a v1..v4 step (old editors): its first 4 lanes as GM notes, ACC when one is hard */
static void ed_dstep_old(const dstep_t *d, uint32_t *n, uint8_t *notes, uint32_t *time, uint32_t *flags, uint32_t *vel)
{
    uint32_t l, k = 0, hard = 0;
    for (l = 0; l < DRUM_LANES && k < 4u; l++)
        if (dstep_has(d, l)) {
            notes[k++] = LANE_NOTE[l];
            hard |= dstep_lvl(d, l) == LV_HARD;
        }
    *n = k;
    while (k < 4u)
        notes[k++] = 0;
    *time = *n ? ST_NOTE : ST_REST;
    *flags = hard ? SF_ACCENT : 0u;
    *vel = *n ? 100u : 0u;
}
/* a step's bytes, as STEP_GET sends them (v5: + level, ratchet) */
static void ed_step_out(const track_t *t, uint32_t i)
{
    uint32_t k;
    if (is_drum(t)) {
        uint32_t n, time, flags, vel;
        uint8_t nt[4];
        ed_dstep_old(&t->dstep[i], &n, nt, &time, &flags, &vel);
        ed_b(n);
        for (k = 0; k < 4u; k++)
            ed_b(nt[k]);
        ed_b(time);
        ed_b(flags);
        ed_b(vel);
        ed_b(0);                                         /* v5: lvl, hi, rat (DRUM_STEP has the lanes' own) */
        ed_b(0);
        ed_b(0);
        return;
    }
    {
        const step_t *st = &t->step[i];
        ed_b(st->n);
        for (k = 0; k < 4u; k++)
            ed_b(st->note[k]);
        ed_b(st->time);
        ed_b(st->flags);
        ed_b(st->vel);
        ed_b(st->lvl);                                   /* v5 (7 bits each: 4 x 2-bit fields, the top 1 below) */
        ed_b(st->lvl >> 7 | (st->rat >> 7) << 1);
        ed_b(st->rat);
    }
}
/* the drum track's step as DRUM_STEP sends it: on (16 bits as 3 x 7), lvl and rat (32 bits each as 5 x 7) */
static void ed_dstep_out(const dstep_t *d)
{
    uint32_t i, on = dstep_mask(d);
    uint32_t lv = (uint32_t)d->lvl[0] | (uint32_t)d->lvl[1] << 8 | (uint32_t)d->lvl[2] << 16 | (uint32_t)d->lvl[3] << 24;
    uint32_t rt = (uint32_t)d->rat[0] | (uint32_t)d->rat[1] << 8 | (uint32_t)d->rat[2] << 16 | (uint32_t)d->rat[3] << 24;
    ed_b(on);
    ed_b(on >> 7);
    ed_b(on >> 14);
    for (i = 0; i < 5u; i++)
        ed_b(lv >> (7u * i));
    for (i = 0; i < 5u; i++)
        ed_b(rt >> (7u * i));
}
