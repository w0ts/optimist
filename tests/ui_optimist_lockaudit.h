/* SPDX-License-Identifier: GPL-3.0-only */
/* The p-lock / motion audit (the user, 2026-10-17: "can we get a complete list of missing parameters?"): every card of
 * every page the Optimist UI lists (op_pg), for each engine on track 1 and for the drum track, with whether a held step
 * can lock it (and a recording knob write it as motion: the same rule, seq/auto_base.c auto_param) and, when not, why.
 * Run by ui_optimist_test.c when PLOCK_AUDIT is set (a build with FELUCCA_PLOCK): tab-separated lines on stdout,
 *   LOCK <track> <engine> <page> <family> <card> <label> <verdict>
 * Each verdict is checked against the firmware's own lock_param (op_step.c): a disagreement is a FAIL line. */
#if FELUCCA_PLOCK
static const char *const LA_FAM[FAM_COUNT] = {"HOME", "ENV", "LFO", "FX", "SCL", "EDIT", "GLO", "SAVE", "ARP", "SEQ", "TRK"};
static const char *const LA_SC[] = {"track", "global", "engine", "step", "track (TRK)", "song", "drum", "FM6 key", "drum lane"};

/* why card k of page pg is (not) lockable on the selected track: "OK..." when it is; NULL when the card is empty */
static const char *la_why(const page_t *pg, uint32_t k, const char **label)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(pg, k, &vp);
    uint32_t id;
    *label = d && d->label ? d->label : "";
    if (!d || PARAM_HIDDEN(d))
        return NULL;
    if (is_go(d))
        return "no: a button (YES does it), not a value";
    if (d->max <= d->min)
        return "no: a read-out or a fixed value";
#if DLA_ON
    if (pg->scope == SC_DSND)
        return page_id(pg, k) < DE_N ? "OK (the lane's own value)" : "no: a lane setting, not one of the lane's 8 sound values";
#endif
    if (!vp)
        return "no: no value behind the card";
    if (vp < tsel()->p || vp >= tsel()->p + P_COUNT)
        return pg->scope == SC_GLOBAL || pg->scope == SC_SONG ? "no: a global value (the project's, not the track's)" :
               "no: not a track value (an FX slot, a send, a lane or the master's own store)";
    id = (uint32_t)(vp - tsel()->p);
    if (lock_ok(tsel(), id))
        return "OK";
    switch (id) {
    case P_VOICE: case P_GLMODE: case P_PRIO: case P_ALLOC:
        return "no, by design: the voice allocation (voices in flight)";
    case P_MUTE:
        return "no, by design: the mixer's mute";
    case P_FXOFF:
        return "no, by design: the FX bypass";
    case P_SLEN: case P_SDIV: case P_SSWING:
        return "no, by design: the pattern's own grid (LEN DIV SWING)";
    default:
        break;
    }
    if (LEG_ID(id))
        return "no: an LFO DEST slot / old depth id (stored as the slots)";
    return "no: no stored id for it (past the engine values, not in auto.c MOT_TAIL)";
}

static uint32_t la_fails;
static void la_track(const char *trk_name, const char *eng)
{
    uint32_t i, k;
    for (i = 0; i < OP_NPG; i++) {
        const page_t *pg = op_pg(i);
        for (k = 0; k < 4u; k++) {
            const char *label, *why = la_why(pg, k, &label);
            const param_desc_t *d;
            int32_t id;
            if (!why)
                continue;
            id = lock_param(pg, k, &d);
            if ((id >= 0) != (why[0] == 'O')) {
                printf("lock audit: %s %s %s card %u %s: the audit says \"%s\", lock_param %d FAIL\n", trk_name, eng,
                       pg->title, k + 1u, label, why, (int)id);
                la_fails++;
            }
            printf("LOCK\t%s\t%s\t%s\t%s\t%u\t%s\t%s\n", trk_name, eng, pg->title, LA_FAM[pg->fam % FAM_COUNT], k + 1u,
                   label, why);
        }
    }
}
static uint32_t lock_audit(void)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++) {
        host_preset(&trk[0], e, 0);
        song.sel = 0;
        frames(1);
        la_track("T1", ENGINES[e]->name);
    }
    song.sel = TRK_DRUM;
    frames(1);
    la_track("DR", "-");
    printf("lock audit: %u disagreements with lock_param\n", la_fails);
    return la_fails;
}
#endif
