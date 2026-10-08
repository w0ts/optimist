/* SPDX-License-Identifier: GPL-3.0-only */
/* Motion (motion.c) with the projects: a store per project buffer, outside project_t (FUN8 is full). Included by
 * project.c after proj_slot. The four slots (FELUCCA_SECTIONS 4) have theirs; the autosave buffer and the song's backup of the loop
 * (arranger_scene.c song_keep) take one of MOTION_AUX on their first capture or flash read. A store belongs to
 * the project whose sum it holds (psum): a project changed without it (a build without the switch, an older
 * save) plays no motion. proj_capture writes the patch (the base) into the project, not the values the motion
 * set, then hands the working motion to the buffer's store; proj_apply takes the buffer's store (a load, a song
 * section). The flash copy: motion_flash.c. */
#if SEC_LOGGED
/* FELUCCA_SECTIONS 8 / 16: no slots; the sections carry their motion in their records (sec_codec.c SEC_MOT). The
 * buffers a section passes through (proj_tmp, the stage), the autosave and the song's backup: one store each,
 * bound at power-on (persist_boot: never taken at run time, by the ISR or the main loop) */
#define MOTION_AUX 4u
#else
#define MOTION_AUX 2u
static motion_store_t motion_slot[4] __attribute__((section(".pool")));
#endif
static motion_store_t motion_aux[MOTION_AUX] __attribute__((section(".pool")));
static const project_t *motion_aux_p[MOTION_AUX];

/* the store of a project buffer; mk: take a free one for a buffer that has none (0: none) */
static motion_store_t *motion_for(const project_t *p, int mk)
{
    uint32_t i;
#if !SEC_LOGGED
    if (p >= proj_slot && p < proj_slot + 4)
        return &motion_slot[p - proj_slot];
#endif
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

/* proj_capture: track k's patch under the motion, by parameter id (v[P_COUNT]). project.c stores it as it
 * stores any track's values (pj_from_p, pj_x): a stored track holds fewer values than P_COUNT */
static void motion_base_params(int16_t *v, uint32_t k)
{
    uint32_t id;
    for (id = 0; id < P_COUNT; id++)
        v[id] = motion_base_value(&trk[k], id);
}

#if FELUCCA_ANALOG2
/* a store recorded with format 10's parameter numbers (its project converted by proj_va_fix; dst: where each
 * part's AMT2 went): P_E0 .. moved up by the four amounts that replaced DST2; an AMT2 that went to another
 * destination takes its motion along */
#define MOTION_VA_E0 (P_E0 - 3u)                       /* format 10's P_E0 (DST2 its last value before) */
static void motion_from_va(motion_store_t *m, const int8_t *dst)
{
    uint32_t i;
    for (i = 0; i < m->count && i < MOTION_MAX; i++) {
        motion_ev_t *e = &m->ev[i];
        uint32_t k = e->place >> 6;
        if (e->param >= MOTION_VA_E0)
            e->param = (uint8_t)(e->param + 3u);
        else if (e->param == P_A2FENV && k < NPART && dst[k] > A2E_CUT && dst[k] <= A2E_SDTN)
            e->param = (uint8_t)(P_A2EPIT + dst[k] - 1);
    }
}
#endif

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
