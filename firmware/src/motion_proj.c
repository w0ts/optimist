/* SPDX-License-Identifier: GPL-3.0-only */
/* Motion (motion.c) with the projects: a store per project buffer, outside project_t (FUN8 is full). Included by
 * project.c after proj_slot. The four slots have theirs; the autosave buffer and the song's backup of the loop
 * (arranger_scene.c song_keep) take one of MOTION_AUX on their first capture or flash read. A store belongs to
 * the project whose sum it holds (psum): a project changed without it (a build without the switch, an older
 * save) plays no motion. proj_capture writes the patch (the base) into the project, not the values the motion
 * set, then hands the working motion to the buffer's store; proj_apply takes the buffer's store (a load, a song
 * section). The flash copy: motion_flash.c. */
#define MOTION_AUX 2u
static motion_store_t motion_slot[4] __attribute__((section(".pool")));
static motion_store_t motion_aux[MOTION_AUX] __attribute__((section(".pool")));
static const project_t *motion_aux_p[MOTION_AUX];

/* the store of a project buffer; mk: take a free one for a buffer that has none (0: none) */
static motion_store_t *motion_for(const project_t *p, int mk)
{
    uint32_t i;
    if (p >= proj_slot && p < proj_slot + 4)
        return &motion_slot[p - proj_slot];
    for (i = 0; i < MOTION_AUX; i++)
        if (motion_aux_p[i] == p)
            return &motion_aux[i];
    if (mk)
        for (i = 0; i < MOTION_AUX; i++)
            if (!motion_aux_p[i]) {
                motion_aux_p[i] = p;
                return &motion_aux[i];
            }
    return 0;
}

/* proj_capture, before the sum: the patch under the motion into the project's parameters */
static void motion_capture_params(project_t *p)
{
    uint32_t k, id;
    for (k = 0; k < NTRK; k++)
        for (id = 0; id < P_COUNT; id++)
            p->t[k].p[id] = motion_base_value(&trk[k], id);
}

/* proj_capture, after the sum: the working motion into the buffer's store */
static void motion_capture_store(const project_t *p)
{
    motion_store_t *m = motion_for(p, 1);
    if (!m)
        return;
    *m = motion;
    m->psum = p->sum;
}

/* proj_apply (a load, a song section; the ISR or interrupts off): the buffer's store, if it is this project's */
static void motion_apply_store(const project_t *p)
{
    const motion_store_t *m = motion_for(p, 0);
    uint32_t k, w;
    for (k = 0; k < NTRK; k++)
        for (w = 0; w < MOTION_WORDS; w++)
            motion_active[k][w] = 0;                  /* (the project's values replace the tracks' now) */
    motion_base_valid = 0;
    if (m && m->psum == p->sum && motion_valid(m)) {
        motion = *m;
    } else {
        motion.count = motion.on = 0;
    }
    motion.psum = 0;
    motion_full = 0;
}

/* a hash of the working motion (the autosave saves when the project or its motion changed) */
static uint32_t motion_hash(void)
{
    return proj_hash(&motion.count, sizeof motion - __builtin_offsetof(motion_store_t, count));
}
