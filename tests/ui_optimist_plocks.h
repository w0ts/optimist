/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, FELUCCA_PLOCK: every cell of every p-lock page (the page buttons with a step held) on every track and
 * engine: a lockable cell records a lock (lock_set, lock_get) and plays it (lock_apply moves the value, the step after
 * lets it go); a cell that is not lockable is listed (PLOCK_LIST=1 prints them). Included by ui_optimist_test.c. */
#if FELUCCA_PLOCK
static void plock_audit_tests(void)
{
    uint32_t tk, e, i, k, rec = 0, bad = 0, dead = 0;
    for (tk = 0; tk < NTRK; tk++)
        for (e = 0; e < (is_drum(&trk[tk]) ? 1u : (uint32_t)NENGINES); e++) {
            track_t *t = &trk[tk];
            reset_ui();
            song.sel = (uint8_t)tk;
            if (!is_drum(t))
                t->engine = (uint8_t)e;
            for (i = 0; i < NPAGES; i++) {
                const page_t *pg = &PAGES[i];
                if (pg->fam != FAM_ENV && pg->fam != FAM_LFO && pg->fam != FAM_FX && pg->fam != FAM_SCL && pg->fam != FAM_ARP &&
                    pg->fam != FAM_EDIT)
                    continue;
                if (!lock_pg_ok(pg))
                    continue;
                for (k = 0; k < 4u; k++) {
                    const param_desc_t *d = 0;
                    int32_t id = lock_param(pg, k, &d);
                    int16_t *vp;
                    const param_desc_t *dd = page_desc(pg, k, &vp);
                    if (!dd || !vp || PARAM_HIDDEN(dd) || is_go(dd) || dd->max <= dd->min)
                        continue;
                    if (id < 0) {
                        dead++;
                        if (getenv("PLOCK_LIST"))
                            printf("  plock: T%u eng %u page %u (%s) k%u %s: not lockable (vp %s)\n", tk + 1u, e, i, pg->title, k,
                                   dd->label, vp >= t->p && vp < t->p + P_COUNT ? "p[]" : "outside p[]");
                        continue;
                    }
                    rec++;
                    if (!(id & LOCK_LANE) && (lock_desc(t, (uint32_t)id)->min < -128 || lock_desc(t, (uint32_t)id)->max > 127)) {
                        bad++;
                        printf("  plock: %s does not fit an event's signed byte\n", dd->label);
                    }
                    if (!(id & LOCK_LANE) && lock_desc(t, (uint32_t)id)->max > lock_desc(t, (uint32_t)id)->min) {
                        int32_t v;
                        int16_t keep = t->p[id];
                        fm1_irq_off();
                        AL(t)->n = 0;
                        (void)lock_set(t, 5, (uint32_t)id, lock_desc(t, (uint32_t)id)->max);
                        if (!lock_get(t, 5, (uint32_t)id, &v) || v != lock_desc(t, (uint32_t)id)->max) {
                            bad++;
                            if (getenv("PLOCK_LIST"))
                                printf("  plock: T%u eng %u page %u (%s) %s: not recorded id %d got %d, want %d (lock_desc %d..%d)\n", tk + 1u, e, i, pg->title, dd->label, id, lock_get(t, 5, (uint32_t)id, &v) ? v : -999, dd->max, lock_desc(t, (uint32_t)id)->min, lock_desc(t, (uint32_t)id)->max);
                        }
                        {   /* played: the step's lock takes hold, the next step without it lets go */
                            uint8_t lp = (uint8_t)id;
                            int8_t lv = (int8_t)lock_desc(t, (uint32_t)id)->max;
                            int16_t was = (int16_t)lock_desc(t, (uint32_t)id)->min;   /* (a value of this engine's range) */
                            t->p[id] = was;
                            lock_apply(t, &lp, &lv, 1);
                            if (t->p[id] != lv) {
                                bad++;
                                printf("  plock: %s: not applied\n", dd->label);
                            }
                            lock_apply(t, 0, 0, 0);
                            if (t->p[id] != was) {
                                bad++;
                                printf("  plock: %s: not let go\n", dd->label);
                            }
                        }
                        AL(t)->n = 0;
                        t->p[id] = keep;
                        fm1_irq_on();
                    }
                }
            }
        }
    printf("  plock audit: %u cells lockable, %u not recorded, %u cells not lockable\n", rec, bad, dead);
    check(bad == 0u, "p-lock pages: every lockable cell records its lock");
}
#endif
