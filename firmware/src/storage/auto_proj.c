/* SPDX-License-Identifier: GPL-3.0-only */
/* The automation store (seq/auto.h, auto.c: motion, locks, nudges, fills, chance; FELUCCA_AUTO) with the projects:
 * a store per project buffer, outside project_t (project_t is full: FUNB fills a flash sector's payload), named by the
 * project's sum (psum): a project changed without it (a build without the store, an older save) plays none. Included
 * by project.c. It replaces motion_proj.c and stepx_proj.c (phase 3): one store holds what their two held.
 *
 *   a buffer's store  proj_capture copies the working store (auto_w) into the store of the buffer it captured into;
 *                     proj_apply takes it from the store of the buffer it applies (none, or another project's:
 *                     cleared). The buffers: the four slots (FELUCCA_SECTIONS 4), proj_tmp (a section saved / loaded),
 *                     the stage (the song's next part), the autosave, the song's backup of the loop. Bound at
 *                     power-on (persist_boot), never taken at run time.
 *
 * In flash a store has today's two forms, as long as they hold it exactly, so a project with motion, locks, nudges and
 * fills is stored byte for byte as before and an older firmware reads it whole: its hold events as motion (the MOTN
 * record beside the project, motion_flash.c; a section's SEC_MOT chunk, sec_codec.c; at most 64 for the four tracks),
 * its step-only events as SLOOP 2.4's step extras (the extras record, stepx_log.c: 24 locks a track, the nudges, the
 * fills). What they cannot hold (more than 64 hold events, more than 24 locks a track, a chance, a hold event of a
 * lockable-only value) makes the extras record the whole store in its new form (auto.h AUTO_ENC_TAG), which an
 * older firmware refuses (the section loads there with the motion the MOTN record or the chunk still has). Read: the
 * motion form first, then the extras record (the old form adds the step-only events, the new form is the store).
 * A pattern record carries its track's list itself (pat.c). */
#define AUTO_AUX 4u
#if !SEC_LOGGED
static auto_store_t auto_slot[4] __attribute__((section(".pool")));
#endif
static auto_store_t auto_aux[AUTO_AUX] __attribute__((section(".pool")));
static const project_t *auto_aux_p[AUTO_AUX];

/* the store of a project buffer; mk: take a free one for a buffer that has none (0: none) */
static auto_store_t *auto_for(const project_t *p, int mk)
{
    uint32_t i;
#if !SEC_LOGGED
    if (p >= proj_slot && p < proj_slot + 4)
        return &auto_slot[p - proj_slot];
#endif
    for (i = 0; i < AUTO_AUX; i++)
        if (auto_aux_p[i] == p)
            return &auto_aux[i];
    if (mk)
        for (i = 0; i < AUTO_AUX; i++)
            if (!auto_aux_p[i]) {
                auto_aux_p[i] = p;
                return &auto_aux[i];
            }
    return 0;
}
static void auto_store_clear(auto_store_t *m)
{
    uint32_t k;
    m->on = 0;
    for (k = 0; k < NTRK; k++)
        m->l[k].n = 0;
}
/* the store of buffer p as p's own, emptied (a record is read into it next); 0 none */
static auto_store_t *auto_fresh(const project_t *p)
{
    auto_store_t *m = auto_for(p, 1);
    if (m) {
        auto_store_clear(m);
        m->psum = p->sum;
    }
    return m;
}
/* p's store, when it is p's (0: none) */
static const auto_store_t *auto_of(const project_t *p)
{
    const auto_store_t *m = auto_for(p, 0);
    return m && m->psum == p->sum ? m : 0;
}

#if FELUCCA_MOTION
/* proj_capture: track k's patch under the motion, by parameter id (v[P_COUNT]). project.c stores it as it
 * stores any track's values (pj_from_p, pj_x): a stored track holds fewer values than P_COUNT */
static void motion_base_params(int16_t *v, uint32_t k)
{
    uint32_t id;
    for (id = 0; id < P_COUNT; id++)
        v[id] = motion_base_value(&trk[k], id);
}
#if FELUCCA_ANALOG2
/* a motion store recorded with format 10's parameter numbers (its project converted by proj_va_fix; dst: where each
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
#endif

/* the working store empty: at power-on (persist_boot; a host test: before it sets any) */
static void auto_init(void)
{
    auto_store_clear(&auto_w);
    auto_w.psum = 0;
    auto_touch_all();
}
/* proj_capture, after the sum: the working store into the buffer's */
static void auto_capture_store(const project_t *p)
{
    auto_store_t *m = auto_for(p, 1);
    if (!m)
        return;
    *m = auto_w;
    m->psum = p->sum;
}
/* proj_apply (a load, a song section; the ISR or interrupts off): the buffer's store, if it is this project's */
static void auto_apply_store(const project_t *p)
{
    const auto_store_t *m = auto_of(p);
#if FELUCCA_MOTION
    uint32_t k, w;
    for (k = 0; k < NTRK; k++)
        for (w = 0; w < MOTION_WORDS; w++)
            motion_active[k][w] = 0;                  /* (the project's values replace the tracks' now) */
    motion_base_valid = 0;
    motion_full = 0;
#endif
    if (m)
        auto_w = *m;
    else
        auto_store_clear(&auto_w);
    auto_w.psum = 0;
    auto_touch_all();
}
/* a hash of the working store (the autosave saves when the project or its automation changed) */
static uint32_t auto_hash(void)
{
    return proj_hash(&auto_w.on, sizeof auto_w - __builtin_offsetof(auto_store_t, on));
}

/* m has hold events or a PLAY bit (a motion form to write) */
static int auto_has_motion(const auto_store_t *m)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (auto_count(&m->l[k], AUTO_HOLDS))
            return 1;
    return m->on != 0;
}
/* ---- the stored forms. The hold events as the old motion form (the first 64; 1: all of them) */
static int auto_motion_of(const auto_store_t *m, motion_store_t *o)
{
    o->psum = m->psum;
    return auto_to_motion(o, m->l, m->on);
}
/* track k's list in today's pattern record (V1, pat.c): at most 64 hold events (recordable here, motion builds; a PLAY
 * bit only with them), its step-only events as SLOOP 2.4's extras */
static int auto_trk_old_ok(const auto_store_t *m, uint32_t k)
{
    const auto_list_t *l = &m->l[k % NTRK];
    uint32_t i, holds = 0;
    stepx_t x;
    for (i = 0; i < l->n; i++)
        if (!(l->ev[i].place & AUTO_ONLY)) {
#if FELUCCA_MOTION
            if (!motion_param(&trk[k % NTRK], mot_id(l->ev[i].param)) &&
                (l->ev[i].param < P_ENG_END || l->ev[i].param >= MOT_TAIL_END))
                return 0;
#endif
            holds++;
        }
    if (auto_to_stepx(&x, l) & ~(uint32_t)AUTO_X_HOLD)
        return 0;
#if FELUCCA_MOTION
    return holds <= MOTION_MAX;
#else
    return !holds && !((m->on >> k) & 1u);
#endif
}
/* today's two forms hold store m exactly: its hold events (motion builds; at most 64 for the four tracks, recordable
 * here) and its step-only events (stepx: no chance, at most 24 locks a track) */
static int auto_old_ok(const auto_store_t *m)
{
    uint32_t k, holds = 0;
    for (k = 0; k < NTRK; k++) {
        if (!auto_trk_old_ok(m, k))
            return 0;
        holds += auto_count(&m->l[k], AUTO_HOLDS);
    }
    return holds <= MOTION_MAX;
}
/* the extras record's bytes of store m -> o (AUTO_ENC_MAX), their length; 0 nothing (no step-only event, in the old
 * form) */
#define AX_ENC_MAX (AUTO_ENC_MAX > NTRK * STEPX_ENC_TRK_MAX ? AUTO_ENC_MAX : NTRK * STEPX_ENC_TRK_MAX)
static uint32_t ax_encode(const auto_store_t *m, uint8_t *o)
{
    uint32_t k, n = 0, any = 0;
    stepx_t x;
    if (!auto_old_ok(m))
        return auto_enc_store(m->l, m->on, o);
    for (k = 0; k < NTRK; k++) {
        (void)auto_to_stepx(&x, &m->l[k]);
        any |= !stepx_is_empty(&x);
        n += stepx_encode_trk(&x, o + n);
    }
    return any ? n : 0u;
}
/* n bytes of an extras record -> m: the old form's step extras added to its lists (the motion form read before), the
 * new form the whole store; 0 refused (the old form: nothing added; the new form: m emptied) */
static int ax_decode(auto_store_t *m, const uint8_t *a, uint32_t n)
{
    const uint8_t *e = a + n;
    uint32_t k;
    stepx_t x;
    if (auto_is_store(a, n))
        return auto_dec_store(m->l, &m->on, a, n);
    for (k = 0; k < NTRK; k++) {                      /* (checked whole first: a bad record adds nothing) */
        if (!stepx_decode_trk(&x, &a, e))
            return 0;
    }
    if (a != e)
        return 0;
    for (k = 0, a = e - n; k < NTRK; k++) {
        (void)stepx_decode_trk(&x, &a, e);
        (void)auto_from_stepx(&m->l[k], &x);
    }
    return 1;
}
