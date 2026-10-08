/* SPDX-License-Identifier: GPL-3.0-only */
/* Per-track patterns and scenes (docs/PATTERNS-DESIGN.md), included by sections.c. Each track has 16 pattern slots,
 * each a record of the section log (sec_log.c id PAT_ID(track, slot): 24..87); a scene is a section record with
 * SEC_SCN (sec_codec.c): the sound and the mix without the steps, and each track's pattern.
 *
 *   pattern record  flags (PF_*), LEN DIV SWING GATE (a byte each), [PF_MOT: n, n x (step, param, value): the
 *                   track's motion], the step bitmap and steps up to LEN (sec_codec.c's form, codec A or B: PF_B,
 *                   the shorter), [PF_SX: the track's SLOOP 2.4 step extras, stepx.h's stored form]
 *
 * Every build with the log reads them: a scene is flattened as it is read (pat_flatten: sec_read puts its patterns
 * into the project), so a build without FELUCCA_PATTERNS plays a scene as the section it would be, and stores a
 * section over it as before (its patterns stay in the log). FELUCCA_PATTERNS stores scenes: each track's working
 * copy remembers the slot it came from (pat_cur); storing scene s refers to that slot when the copy is unchanged
 * (shared, nothing written), else writes it (into its source when no other scene uses it, else slot s or the
 * lowest free one: copy-on-write), the patterns first, the scene last (the commit). Old sections become scenes at
 * the first start (pat_migrate), one at a time, cut anywhere: the next start goes on (a section not converted
 * still plays as it is). */
#define PAT_ID(k, s) (SEC_ID_PAT0 + PAT_N * (k) + (s))
#define SEC_ID_PSTATE 17u                              /* the working copies' sources (pat_cur), with the autosave */
_Static_assert(PAT_ID(NTRK - 1u, PAT_N - 1u) < 88u, "the patterns' log ids: 24..87");
enum { PF_MOT = 1, PF_B = 2, PF_DRUM = 4, PF_ON = 8, PF_SX = 16, PF_V1 = 0x20, PF_VER = 0xE0 };
static uint32_t pat_missing;                           /* patterns a scene named and the log did not have */

/* pattern (k, s)'s record -> buf (SEC_REC_MAX), its length; 0 none */
static uint32_t pat_get(uint32_t k, uint32_t s, uint8_t *buf)
{
    int n;
#if FELUCCA_PATTERNS
    uint32_t i = SEC_PEND_PAT + PAT_N * k + s;
    if (sec_pend_has(i)) {
        memcpy(buf, sec_pend.data + sec_pend.off[i], sec_pend.len[i]);
        return sec_pend.len[i];
    }
#endif
    n = flash_ok ? slg_get(PAT_ID(k, s), buf) : 0;
    return n > 0 ? (uint32_t)n : 0u;
}

/* a pattern record (n bytes) of track k -> t's steps and its LEN DIV SWING GATE, its motion added to m (0: none;
 * past 64 events: cut), its extras into x (0: none); 0 not a pattern this build reads */
static int pat_decode(const uint8_t *a, uint32_t n, uint32_t k, proj_trk_t *t, void *m, stepx_t *x)
{
    const uint8_t *e = a + n, *ev = 0;
    uint32_t f, i, c = 0;
#if FELUCCA_SL24_XSTEP
    stepx_t sx;
#endif
    if (n < 5u || ((f = a[0]) & PF_VER) != PF_V1 || !(f & PF_DRUM) != (k != TRK_DRUM))
        return 0;
    for (i = 0; i < 4u; i++)
        t->p[P_SLEN + i] = a[1u + i];
    a += 5;
    if (f & PF_MOT) {
        if (a >= e || (c = *a++) > 64u || (uint32_t)(e - a) < 3u * c)
            return 0;
        ev = a, a += 3u * c;
    }
    if (!sec_steps_get(t, k, &a, e, (f & PF_B) != 0))
        return 0;
#if FELUCCA_SL24_XSTEP
    if (!x)
        x = &sx;
    if (f & PF_SX ? !stepx_decode_trk(x, &a, e) : (stepx_clear(x), 0))
        return 0;
#else
    if (f & PF_SX)
        a = e;                                         /* (the extras come last: this build plays none) */
    (void)x;
#endif
    if (a != e)
        return 0;
#if FELUCCA_MOTION
    if (m) {
        motion_store_t *ms = (motion_store_t *)m;
        for (i = 0; i < c; i++, ev += 3)
            if (ms->count < MOTION_MAX) {
                motion_ev_t *d = &ms->ev[ms->count++];
                d->place = (uint8_t)(k << 6 | (ev[0] & 63u)), d->param = ev[1], d->value = (int8_t)ev[2];
            } else
                pat_missing |= 0x100u;                 /* (MOTION CUT: the four patterns hold more than 64) */
        if (f & PF_ON)
            ms->on |= (uint8_t)(1u << k);
    }
#endif
    (void)m, (void)ev;
    return 1;
}

/* a scene read into p (its record's last 4 bytes: refs): each track's pattern put in (none: no steps; keep: what
 * the track plays; missing: no steps, counted), its motion and extras into p's stores; the sum again */
static void pat_flatten(project_t *p, const uint8_t *refs)
{
    uint8_t r[NTRK];
    uint32_t k, n;
    void *m = 0;
    stepx_t *x = 0;
#if FELUCCA_MOTION
    motion_store_t *ms = motion_for(p, 1);
    if (ms)
        ms->count = ms->on = 0, m = ms;
#endif
#if FELUCCA_SL24_XSTEP
    sx_store_t *xs = sx_for(p, 1);
#endif
    memcpy(r, refs, NTRK);                             /* (refs may sit in sec_rbuf: read over below) */
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *t = &p->t[k];
#if FELUCCA_SL24_XSTEP
        x = xs ? &xs->x[k] : 0;
#endif
        if (r[k] == PAT_KEEP) {                        /* (the stage's ISR leaves the track alone: phase 2) */
            uint32_t i;
            memcpy(t->step, trk[k].step, sizeof t->step);
            for (i = 0; i < 4u; i++)
                t->p[P_SLEN + i] = trk[k].p[P_SLEN + i];
            continue;
        }
        if (r[k] < PAT_N && (n = pat_get(k, r[k], sec_rbuf)) != 0 && pat_decode(sec_rbuf, n, k, t, m, x))
            continue;
        pat_missing += r[k] < PAT_N;
        for (n = 0; n < NSTEP; n++)
            sec_step_clear(&t->step[n], k);
        if (x)
            stepx_clear(x);
    }
    p->sum = proj_sum(p);
#if FELUCCA_MOTION
    if (ms)
        ms->psum = p->sum;
#endif
#if FELUCCA_SL24_XSTEP
    if (xs)
        xs->psum = p->sum;
#endif
    (void)m, (void)x;
}

#if FELUCCA_PATTERNS
/* ---- the working copies: each track's source (a slot, PAT_NONE), and those of the project buffers a section
 * passes through (proj_capture writes pat_cur into a buffer's, proj_apply takes it back; a scene read sets its
 * references) */
static uint8_t pat_bref[3][NTRK];                      /* proj_tmp, the stage, the song's backup of the loop */
static uint8_t *pat_refs_of(const project_t *p)
{
    if (p == &proj_tmp.cur)
        return pat_bref[0];
    if (p == &sec_stage_p)
        return pat_bref[1];
#if FELUCCA_ARRANGER && !defined(PROJ_HOST)
    if (p == &song_keep)
        return pat_bref[2];
#endif
    return 0;
}
/* proj_capture (apply 0) / proj_apply (1) of buffer p: its sources follow the tracks (another buffer: none) */
static void pat_mark(const project_t *p, int apply)
{
    uint8_t *r = pat_refs_of(p);
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (!apply) {
            if (r)
                r[k] = pat_cur[k];
        } else if (!r || r[k] != PAT_KEEP)
            pat_cur[k] = r ? r[k] : PAT_NONE;
}

/* the record of track k of project p -> out (SEC_REC_MAX room), its length; 0: an empty pattern (no step, motion
 * or extras: the scene says none) */
static uint32_t pat_encode(const project_t *p, uint32_t k, uint8_t *out)
{
    const proj_trk_t *t = &p->t[k];
    uint8_t *o = out + 5;
    uint32_t i, f = PF_V1 | (k == TRK_DRUM ? PF_DRUM : 0u), b = !sec_test_a && sec_b_gain(t, k) > 0;
#if FELUCCA_MOTION
    const motion_store_t *m = motion_for(p, 0);
    if (m && m->psum == p->sum && m->count <= MOTION_MAX) {
        uint8_t *c = o++;
        *c = 0;
        for (i = 0; i < m->count; i++)
            if ((m->ev[i].place >> 6) == k)
                *o++ = m->ev[i].place & 63u, *o++ = m->ev[i].param, *o++ = (uint8_t)m->ev[i].value, (*c)++;
        if (*c)
            f |= PF_MOT;
        else
            o--;
        if ((m->on >> k) & 1u)
            f |= PF_ON;
    }
#endif
    for (i = 0; i < 4u; i++)
        out[1u + i] = (uint8_t)t->p[P_SLEN + i];
    {
        uint8_t *s = o, any = 0;
        o = sec_steps_put(t, k, o, (int)b, 0);
        for (i = 0; i < (sec_len(t) + 7u) / 8u; i++)
            any |= s[i];                               /* (the bitmap: a step kept) */
        f |= (b ? PF_B : 0u) | (any ? 0u : 0x100u);
    }
#if FELUCCA_SL24_XSTEP
    {
        const sx_store_t *x = sx_for(p, 0);
        if (x && x->psum == p->sum && !stepx_is_empty(&x->x[k])) {
            f |= PF_SX;
            o += stepx_encode_trk(&x->x[k], o);
        }
    }
#endif
    if ((f & 0x100u) && !(f & (PF_MOT | PF_ON | PF_SX)))
        return 0;
    out[0] = (uint8_t)f;
    return (uint32_t)(o - out);
}

/* ---- where patterns are, who uses them */
static uint32_t pat_pend(uint32_t k, uint32_t s) { return SEC_PEND_PAT + PAT_N * k + s; }
static int pat_has(uint32_t k, uint32_t s) { return sec_pend_has(pat_pend(k, s)) || slg_has(PAT_ID(k, s)); }
/* scene i's patterns -> r (NTRK); 1 it is a scene (the arena's record, else the log's) */
static int pat_scene_refs(uint32_t i, uint8_t *r)
{
    uint8_t f;
    if (i < SEC_IDS && sec_pend_has(i)) {
        const uint8_t *a = sec_pend.data + sec_pend.off[i];
        if (!(a[0] & SEC_SCN))
            return 0;
        memcpy(r, a + sec_pend.len[i] - NTRK, NTRK);
        return 1;
    }
    return flash_ok && slg_has(i) && !st_read(slg_at(i) + SEC_HEAD, &f, 1) && (f & SEC_SCN) &&
           !st_read(slg_at(i) + SEC_HEAD + slg.alen[i] - NTRK, r, NTRK);
}
/* the scenes but scene `but` that play pattern (k, s): a mask (the log's 16 ids: an 8-section build keeps I..P) */
static uint32_t pat_users(uint32_t k, uint32_t s, uint32_t but)
{
    uint32_t i, m = 0;
    uint8_t r[NTRK];
    for (i = 0; i < SEC_ID_SONG; i++)
        if (i != but && pat_scene_refs(i, r) && r[k] == s)
            m |= 1u << i;
    return m;
}
/* pattern (k, s) holds the record r (n bytes)? */
static int pat_same(uint32_t k, uint32_t s, const uint8_t *r, uint32_t n)
{
    uint32_t i = pat_pend(k, s), id = PAT_ID(k, s), o, c;
    uint8_t b[32];
    if (sec_pend_has(i))
        return sec_pend.len[i] == n && !memcmp(sec_pend.data + sec_pend.off[i], r, n);
    if (!slg_has(id) || slg.alen[id] != n)
        return 0;
    for (o = 0; o < n; o += c)
        if (c = n - o < sizeof b ? n - o : (uint32_t)sizeof b, st_read(slg_at(id) + SEC_HEAD + o, b, c) || memcmp(b, r + o, c))
            return 0;
    return 1;
}
/* the slot a changed pattern of track k (from slot src) goes to when scene s is stored: its source when no other
 * scene plays it, else slot s, else the lowest; a slot qualifies when no other scene plays it and it is empty, the
 * source, or scene s's own. PAT_NONE: none (NO FREE PATTERN) */
static uint32_t pat_slot(uint32_t k, uint32_t src, uint32_t s)
{
    uint32_t j, c;
    for (j = 0; j < PAT_N + 2u; j++)
        if ((c = j == 0 ? src : j == 1 ? s : j - 2u) < PAT_N && !pat_users(k, c, s) &&
            (c == src || !pat_has(k, c) || pat_users(k, c, PAT_ALL)))
            return c;
    return PAT_NONE;
}

/* ---- storing */
static int sec_room_ids(const uint32_t *ids, const uint32_t *lens, uint32_t cnt, int playing);
static int sec_read(uint32_t s, project_t *p, dlrec_t *d);
static uint32_t sec_capture(void);
static uint32_t pat_scene_enc(const project_t *p, const dlrec_t *d, const uint8_t *ref, uint8_t *out)
{
    uint32_t n = sec_body(p, d, out, 1);
    out[0] |= SEC_SCN;
    memcpy(out + n, ref, NTRK);
    return n + NTRK;
}
/* record r (n bytes) as log id: into the arena (playing), else the log (0 ok) */
static int pat_put(uint32_t id, uint32_t pi, const uint8_t *r, uint32_t n, int arena)
{
    int rc = arena ? sec_pend_put(pi, r, n) ? 3 : 0 : slg_put(id, r, n, 1);
    if (!rc && !arena)
        sec_pend_del(pi);                              /* (an older one waiting in the arena: gone) */
    return rc;
}
/* project p (in proj_tmp.cur: its drum record d, its tracks' sources src) stored as scene s: the changed patterns
 * written first (the slot each goes to: pat_slot), the scene last; arena: into the pending arena (playing), else
 * the log; playing: s is the playing scene (it may use the reserve). src gets the slots the scene names. -> 0 ok,
 * 1 MEM FULL, 3 the arena is full, 0x10 | track: no free pattern on that track, -1 flash (nothing is written but
 * on a flash error) */
static int pat_scene_put(uint32_t s, const project_t *p, const dlrec_t *d, uint8_t *src, int arena, int playing)
{
    uint8_t ref[NTRK], w = 0;
    uint32_t k, c, n, ids[NTRK + 1u], lens[NTRK + 1u], cnt = 0, used = sec_pend.used;
    int rc;
    _Static_assert(NTRK == 4u, "a scene names 4 patterns");
    for (k = 0; k < NTRK; k++) {
        ref[k] = PAT_NONE;
        if ((n = pat_encode(p, k, sec_rbuf)) == 0)
            continue;
        if ((c = src[k]) < PAT_N && pat_same(k, c, sec_rbuf, n)) {
            ref[k] = (uint8_t)c;                       /* (unchanged: shared, nothing written) */
            continue;
        }
        if ((c = pat_slot(k, c, s)) >= PAT_N)
            return 0x10 | (int)k;
        ref[k] = (uint8_t)c, w |= (uint8_t)(1u << k);
        ids[cnt] = PAT_ID(k, c), lens[cnt++] = n;
        used += n - (sec_pend_has(pat_pend(k, c)) ? sec_pend.len[pat_pend(k, c)] : 0u);
    }
    ids[cnt] = s, lens[cnt] = n = pat_scene_enc(p, d, ref, sec_rbuf);
    used += n - (sec_pend_has(s) ? sec_pend.len[s] : 0u);
    if (!sec_room_ids(ids, lens, cnt + 1u, playing))
        return 1;
    if (arena && used > SEC_ARENA)
        return 3;
    for (k = 0; k < NTRK; k++)
        if (((w >> k) & 1u) && (rc = pat_put(PAT_ID(k, ref[k]), pat_pend(k, ref[k]), sec_rbuf, pat_encode(p, k, sec_rbuf), arena)) != 0)
            return rc;
    if ((rc = pat_put(s, s, sec_rbuf, pat_scene_enc(p, d, ref, sec_rbuf), arena)) != 0)
        return rc;
#if FELUCCA_SL24_XSTEP
    sec_pend_del(SEC_IDS + s);                         /* (its extras are the patterns' now: an older record cleared) */
    if (!arena && slg_has(SX_ID0 + s))
        (void)slg_put(SX_ID0 + s, sec_rbuf, 0, 1);
#endif
    if (arena)                                         /* its FX record beside it, keyed by the scene record (in sec_rbuf) */
        rc = fxr_pend(s, lens[cnt]);
    else {
        sec_pend_del(SEC_PEND_FX + s);
        rc = fxr_log_put(FXR_ID0 + s, proj_hash(sec_rbuf, lens[cnt]), p, playing);
    }
    if (rc)
        return rc;
    memcpy(src, ref, NTRK);
    for (sec_last_n = k = 0; k <= cnt; k++)
        sec_last_n += lens[k];                         /* (the gauge: stores of this size, the patterns with it) */
    sec_gen++;
    return 0;
}
/* the working copy (sec_capture: proj_tmp.cur) stored as scene s (pat_scene_put); the tracks' sources follow. Says
 * what went wrong; -> 0 stored */
static int pat_store(uint32_t s, int arena, int playing)
{
    static char msg[] = "T1: NO FREE PATTERN";
    int rc = pat_scene_put(s, &proj_tmp.cur, &sec_tmp_dl, pat_bref[0], arena, playing);
    if (!rc)
        memcpy(pat_cur, pat_bref[0], NTRK);
    else if (rc & 0x10) {
        msg[0] = (rc & 15) == TRK_DRUM ? 'D' : 'T';
        msg[1] = (rc & 15) == TRK_DRUM ? 'R' : (char)('1' + (rc & 15));
        ui_message(msg);
    } else
        ui_message(rc == 1 ? "MEM FULL" : rc == 3 ? "STOP TO SAVE MORE" : "SAVE ERROR");
    return rc;
}

/* ---- the first start of a PATTERNS build: each old section becomes a scene (its tracks' patterns into slot s
 * when it is free, else the lowest; then the scene: the commit). Cut anywhere: what is not converted plays as it
 * is and is converted at the next start; a slot a cut conversion wrote is taken again (its source) */
static void pat_migrate(void)
{
    uint32_t s;
    uint8_t f, src[NTRK];
    for (s = 0; s < SEC_IDS; s++) {
        if (sec_pend_has(s) || !slg_has(s) || st_read(slg_at(s) + SEC_HEAD, &f, 1) || (f & SEC_SCN) ||
            !sec_read(s, &proj_tmp.cur, &sec_tmp_dl))
            continue;
        memset(src, (int)s, NTRK);
        if (pat_scene_put(s, &proj_tmp.cur, &sec_tmp_dl, src, 0, 0))
            return;                                    /* (MEM FULL or flash: the rest stay sections) */
    }
}

/* the sources of the working copies with the autosave (id SEC_ID_PSTATE: 4 bytes; none: every track's none) */
static int pat_state_get(uint8_t *b) { return slg.alen[SEC_ID_PSTATE] == NTRK && slg_get(SEC_ID_PSTATE, b) == (int)NTRK; }
static void pat_state_save(void)
{
    uint8_t b[NTRK];
    if (!pat_state_get(b) || memcmp(b, pat_cur, NTRK))
        (void)slg_put(SEC_ID_PSTATE, pat_cur, NTRK, 1);
}
static void pat_state_load(void)
{
    uint8_t b[NTRK];
    uint32_t k;
    if (pat_state_get(b))
        for (k = 0; k < NTRK; k++)
            pat_cur[k] = b[k] < PAT_N ? b[k] : PAT_NONE;
}

/* ---- launching a pattern on a track (docs/PATTERNS-DESIGN.md 5.2). The main loop decodes it into the stage
 * (sec_stage_p.t[k], its motion and extras into the stage's stores: pat_service), the audio ISR switches the track
 * at its moment (pat_switch, from seq_tick): at the end of the pattern playing (PW_END), on the next bar (PW_BAR)
 * or on the next step, where the pattern is (PW_NOW). A scene staged meanwhile takes the track's pattern with it
 * ("scene C with drums 5"); a song part re-read after a switch (the launch lasts until the next part) */
#if FELUCCA_MOTION
/* track k's events and PLAY bit in dst := src's (the others' kept; past 64: cut) */
static void motion_put_trk(motion_store_t *d, const motion_store_t *s, uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < d->count; i++)
        if ((d->ev[i].place >> 6) != k)
            d->ev[n++] = d->ev[i];
    for (i = 0; s && i < s->count && n < MOTION_MAX; i++)
        if ((s->ev[i].place >> 6) == k)
            d->ev[n++] = s->ev[i];
    d->count = (uint8_t)n;
    d->on = (uint8_t)((d->on & ~(1u << k)) | (s ? s->on & (1u << k) : 0u));
}
#endif
/* pattern s of track k (PAT_NONE or an empty slot: no step, the track's values) -> p's track k, its motion and
 * extras into p's stores */
static void pat_load_trk(uint32_t k, uint32_t s, project_t *p)
{
    proj_trk_t *t = &p->t[k];
    uint32_t n, i;
    void *m = 0;
    stepx_t *x = 0;
#if FELUCCA_MOTION
    motion_store_t *ms = motion_for(p, 1);
    if (ms)
        ms->count = ms->on = 0, m = ms;
#endif
#if FELUCCA_SL24_XSTEP
    sx_store_t *xs = sx_for(p, 1);
    x = xs ? &xs->x[k] : 0;
#endif
    for (i = 0; i < 4u; i++)
        t->p[P_SLEN + i] = trk[k].p[P_SLEN + i];
    if (s >= PAT_N || (n = pat_get(k, s, sec_rbuf)) == 0 || !pat_decode(sec_rbuf, n, k, t, m, x)) {
        for (i = 0; i < NSTEP; i++)
            sec_step_clear(&t->step[i], k);
        if (x)
            stepx_clear(x);
    }
    (void)m;
}
/* p's track k (pat_load_trk) -> the working track k, its motion and extras (the ISR, or the IRQ off) */
static void pat_take(track_t *t, uint32_t k, const project_t *p)
{
    uint32_t i;
    memcpy(t->step, p->t[k].step, sizeof t->step);
    for (i = 0; i < 4u; i++)
        t->p[P_SLEN + i] = (int16_t)clamp(p->t[k].p[P_SLEN + i], TP[P_SLEN + i].min, TP[P_SLEN + i].max);
#if FELUCCA_MOTION
    motion_restore(t);
    motion_put_trk(&motion, motion_for(p, 0), k);
#endif
#if FELUCCA_SL24_XSTEP
    {
        const sx_store_t *x = sx_for(p, 0);
        if (x)
            *STEPX(k) = x->x[k];
        else
            stepx_clear(STEPX(k));
    }
#endif
}
/* launch pattern s (PAT_NONE: none, the track stops) on track k, when (PW_*); stopped: at once */
static void pat_launch(uint32_t k, uint32_t s, uint32_t when)
{
    k %= NTRK;
    if (!song.playing && !transport_req) {
        if (proj_tmp_busy())
            return;
        pat_load_trk(k, s, &proj_tmp.cur);
        fm1_irq_off();
        undo_mark(&trk[k], (undo_sess += 4u) | 3u);
        pat_take(&trk[k], k, &proj_tmp.cur);
        pat_cur[k] = (uint8_t)s;
        pat_req[k] = PAT_NONE;
        pat_staged &= (uint8_t)~(1u << k);
        fm1_irq_on();
        return;
    }
    fm1_irq_off();
    pat_req[k] = (uint8_t)(s < PAT_N ? s : PAT_STOP);
    pat_when[k] = (uint8_t)when;
    pat_bar[k] = clk_beat >> 2;
    pat_staged &= (uint8_t)~(1u << k);                 /* (staged again by pat_service) */
    fm1_irq_on();
}
/* main loop: each launch waiting, decoded into the stage */
static void pat_service(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (pat_req[k] != PAT_NONE && !((pat_staged >> k) & 1u) && !proj_tmp_busy()) {
            uint32_t s = pat_req[k];
            pat_load_trk(k, s == PAT_STOP ? PAT_NONE : s, &proj_tmp.cur);
            fm1_irq_off();
            if (pat_req[k] == s) {                     /* (not launched again meanwhile) */
                memcpy(&sec_stage_p.t[k], &proj_tmp.cur.t[k], sizeof sec_stage_p.t[k]);
#if FELUCCA_MOTION
                if (motion_for(&sec_stage_p, 0))
                    motion_put_trk(motion_for(&sec_stage_p, 0), motion_for(&proj_tmp.cur, 0), k);
#endif
#if FELUCCA_SL24_XSTEP
                if (sx_for(&sec_stage_p, 0) && sx_for(&proj_tmp.cur, 0))
                    sx_for(&sec_stage_p, 0)->x[k] = sx_for(&proj_tmp.cur, 0)->x[k];
#endif
                pat_staged |= (uint8_t)(1u << k);
            }
            fm1_irq_on();
        }
}
/* the audio ISR, seq_tick: track t enters grid step abs (len: its LEN); its launched pattern waits in the stage.
 * Its moment: the switch (an undo level of the track, its take ends), -> the LEN it plays now */
static uint32_t pat_switch_isr(track_t *t, uint32_t abs, uint32_t len)
{
    uint32_t k = trk_index(t), w = pat_when[k];
    if (live_req >= 0 || (w == PW_END && TRK_IDX(t, abs, len)) ||
        (w == PW_BAR && ((clk_beat & 3u) || (clk_beat >> 2) == pat_bar[k])))
        return len;
    undo_mark(t, (undo_sess += 4u) | 3u);
    pat_take(t, k, &sec_stage_p);
    if (w != PW_NOW)
        t->org = abs;                                  /* (from its step 1) */
    song.rec &= (uint8_t)~(1u << k);                   /* (a take does not run on into another pattern) */
    pat_cur[k] = pat_req[k] == PAT_STOP ? PAT_NONE : pat_req[k];
    pat_req[k] = PAT_NONE;
    pat_staged &= (uint8_t)~(1u << k);
    if (sec_stage_id >= 0)
        sec_stage_id = -1;                             /* (the stage's copy of the next part had this track) */
    return trk_len(t);
}
static uint32_t (*pat_switch)(track_t *t, uint32_t abs, uint32_t len) = pat_switch_isr;   /* (seq.c calls it) */
/* the ISR applies a scene (arrangement_apply), before: a track the scene keeps takes what it plays into the stage;
 * after (applied 1): the launches the stage held are played now */
static void pat_scene_apply(int applied)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (!applied && pat_bref[1][k] == PAT_KEEP) {
            uint32_t i;
            memcpy(sec_stage_p.t[k].step, trk[k].step, sizeof trk[k].step);
            for (i = 0; i < 4u; i++)
                sec_stage_p.t[k].p[P_SLEN + i] = trk[k].p[P_SLEN + i];
#if FELUCCA_MOTION
            if (motion_for(&sec_stage_p, 0))
                motion_put_trk(motion_for(&sec_stage_p, 0), &motion, k);
#endif
#if FELUCCA_SL24_XSTEP
            if (sx_for(&sec_stage_p, 0))
                sx_for(&sec_stage_p, 0)->x[k] = *STEPX(k);
#endif
        } else if (applied && ((pat_staged >> k) & 1u)) {
            pat_cur[k] = pat_req[k] == PAT_STOP ? PAT_NONE : pat_req[k];
            pat_req[k] = PAT_NONE;
        }
    if (applied)
        pat_staged = 0;
}

/* ---- the PATTERN layer's operations (ui/sloop/ui_pat.c; the editor's later). Stopped: the log; playing: the
 * arena (written when quiet, as a section stored while playing). -> 0 done, else the message said */
static int pat_write(uint32_t k, uint32_t s, uint32_t n)   /* sec_rbuf (n bytes; 0: cleared) -> pattern (k, s) */
{
    uint32_t id = PAT_ID(k, s), lens = n;
    int rc;
    if (n && !sec_room_ids(&id, &lens, 1, 0))
        rc = 1;
    else if (!n && song.playing)
        rc = 4;                                        /* (the arena keeps no "cleared": a flash write waits) */
    else
        rc = pat_put(id, pat_pend(k, s), sec_rbuf, n, song.playing || !flash_ok);
    if (!rc && song.playing)
        sec_dirty |= 0x8000u;                          /* (sections_write: the patterns first) */
    if (rc)
        ui_message(rc == 1 ? "MEM FULL" : rc == 3 ? "STOP TO SAVE MORE" : rc == 4 ? "STOP FIRST" : "SAVE ERROR");
    sec_gen++;                                         /* (a staged scene naming it: read again) */
    return rc;
}
/* STORE: track k's working copy into slot s (every scene naming s plays it now); it is the track's source then */
static int pat_store_slot(uint32_t k, uint32_t s)
{
    uint32_t n;
    (void)sec_capture();
    n = pat_encode(&proj_tmp.cur, k, sec_rbuf);
    if (pat_write(k, s, n))
        return 1;
    pat_cur[k] = (uint8_t)s;
    return 0;
}
/* COPY pattern (k, a) to (k2, b): the drum track's to the drum track only */
static int pat_copy(uint32_t k, uint32_t a, uint32_t k2, uint32_t b)
{
    uint32_t n;
    if ((k == TRK_DRUM) != (k2 == TRK_DRUM) || (k == k2 && a == b) || (n = pat_get(k, a, sec_rbuf)) == 0) {
        ui_message("NO COPY");
        return 1;
    }
    return pat_write(k2, b, n);
}
/* the tracks whose working copy differs from its source (the PATTERN layer's "*"): a mask */
static uint32_t pat_changed(void)
{
    uint32_t k, n, m = 0;
    (void)sec_capture();
    for (k = 0; k < NTRK; k++) {
        n = pat_encode(&proj_tmp.cur, k, sec_rbuf);
        if (pat_cur[k] < PAT_N ? !pat_same(k, pat_cur[k], sec_rbuf, n) : n != 0u)
            m |= 1u << k;
    }
    return m;
}
/* the first slot of track k with no pattern and no scene naming it, PAT_NONE none */
static uint32_t pat_free(uint32_t k)
{
    uint32_t s;
    for (s = 0; s < PAT_N; s++)
        if (!pat_has(k, s) && !pat_users(k, s, PAT_ALL))
            return s;
    return PAT_NONE;
}
#endif
