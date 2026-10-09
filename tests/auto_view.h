/* SPDX-License-Identifier: GPL-3.0-only */
/* Host tests: the automation store (firmware/src/seq/auto.c, phase 3) seen as motion's old shared store, so the tests
 * written against motion.c's 64 events (place = track << 6 | step) keep their checks: mview() is the working store's
 * hold events in that form (read only), motion_load() puts such a store in (the step-only events kept), motion_reset()
 * drops every hold event, motion_of(p) is a project buffer's store in that form (its psum; 0 none). Include after the
 * firmware sources (FELUCCA_AUTO). */
#ifndef AUTO_VIEW_H
#define AUTO_VIEW_H
static motion_store_t mview_buf, mview_of;
static const motion_store_t *mview(void)
{
    mview_buf.psum = 0;
    (void)auto_to_motion(&mview_buf, auto_w.l, auto_w.on);
    return &mview_buf;
}
static void motion_reset(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        (void)auto_drop_kind(&auto_w.l[k], AUTO_HOLDS);
    auto_w.on = 0;
}
static void motion_load(const motion_store_t *m)
{
    motion_reset();
    (void)auto_from_motion(auto_w.l, m);
    auto_w.on = m->on;
}
/* two lists hold the same events (each (place, param) with its value; the order aside) */
static int auto_list_same(const auto_list_t *a, const auto_list_t *b)
{
    uint32_t i;
    int32_t v;
    if (a->n != b->n)
        return 0;
    for (i = 0; i < a->n; i++)
        if (!auto_get(b, a->ev[i].place, a->ev[i].param, &v) || v != a->ev[i].value)
            return 0;
    return 1;
}
static int auto_store_same(const auto_store_t *a, const auto_store_t *b)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (!auto_list_same(&a->l[k], &b->l[k]))
            return 0;
    return a->on == b->on;
}
#ifdef AUTO_VIEW_PROJ
static const motion_store_t *motion_of(const project_t *p)
{
    const auto_store_t *a = auto_for(p, 0);
    if (!a)
        return 0;
    mview_of.psum = a->psum;
    (void)auto_to_motion(&mview_of, a->l, a->on);
    return &mview_of;
}
/* project p's store := the old store m (its psum, its events as hold events) */
static void motion_store_put(const project_t *p, const motion_store_t *m)
{
    auto_store_t *a = auto_for(p, 1);
    uint32_t k;
    if (!a)
        return;
    for (k = 0; k < NTRK; k++)
        a->l[k].n = 0;
    a->psum = m->psum;
    (void)auto_from_motion(a->l, m);
    a->on = m->on;
}
#endif
#endif
