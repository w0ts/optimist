/* SPDX-License-Identifier: GPL-3.0-only */
/* Run control for scripted tests (console builds; included by console.c, main loop only, -Os: size_fns SIZE_FILES).
 *
 *   play | pause | stop   the PLAY key's request (transport_req, as op_input.c op_play / ui_input.c B_PLAY: the sequencer
 *                         takes it at the next audio block). The sequencer has no pause: both are STOP, a later PLAY
 *                         starts from the top
 *   rec on | off          the REC key's arm (as op_rec / rec_toggle: playing: record now; stopped: armed)
 *   proj load N           project_load(N): the slot the UI's LOAD reads (0..3; refused while playing, as the UI)
 *   heavy [NOTES]         the stress project, built through the model's own setters while stopped (below)
 *   wdtreset              `reboot`, but through the watchdog (p3_rst bit 2 instead of the soft reset's bit 6)
 * Every one only sets the flags and fields the UI sets, from the main loop: nothing runs in an interrupt and the audio
 * engine is untouched. The reply carries the state read back (`playing`, `rec`) so a script can check it. */

static void con_run_state(void)
{
    con_kv("playing", song.playing);
    con_kv("transport_req", transport_req);
    con_kv("rec", song.rec);
    con_kv("rec_wait", rec_wait);
}

static void con_run_play(void)
{
#if FELUCCA_ARRANGER
    if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) {
        con_puts("refused: EMPTY SECTION (song mode)\r\n");   /* (op_play's check) */
        con_run_state();
        return;
    }
#endif
    if (!song.playing && transport_req != 1u)
        transport_req = 1;                              /* (not a toggle: a second `play` does nothing) */
    con_puts("play requested\r\n");
    con_run_state();
}

static void con_run_stop(void)
{
    if (song.playing || transport_req == 1u)
        transport_req = 2;
    con_puts("stop requested\r\n");
    con_run_state();
}

static void con_run_rec(int on)
{
    if (on) {
        if (song.playing && arrangement_enabled) {
            con_puts("refused: STOP THE SONG FIRST\r\n");
        } else if (!song.rec && !rec_wait) {
            arrangement_enabled = 0;
            if (song.playing)
                rec_begin();                            /* playing: record now */
            else
                rec_wait = 1;                           /* stopped: the first note starts it */
        }
    } else {
        song.rec = 0;
        rec_wait = 0;
    }
    con_run_state();
}

static void con_run_proj_load(uint32_t slot)
{
    if (song.playing || transport_req) {
        con_puts("refused: playing (stop first)\r\n");
        return;
    }
    project_load(slot);                                 /* (the UI's path: flash fetch, check, project_apply) */
    con_kv("proj_load_slot", (int32_t)slot);
    con_kv("project_empty", project_empty());
}

/* ---- heavy: every step of every track filled, an insert pair on every track and every drum lane with amounts up,
 * the sends up. Built while stopped, under fm1_irq_off like project_apply, through ins_put / lin_put / lin_set and the
 * parameters. Synth tracks T1..T3: a NOTES-note step (1..4) on all NSTEP steps, LEN NSTEP; drum track: all 16 lanes on
 * every step, every 4th with the accent lane. Inserts: S1 DIST, S2 COMP (FILT where COMP is not built) on T1 T2 T3 and
 * the drum bus, the lanes S1 DIST, S2 DYN; their amounts 3/4 of the range. Sends: each track's CHO DLY REV 90, every
 * lane's 31 (the top). The master's slots, the returns and the sound of each track are as they were (load the sound
 * first). The working project changes (autosave may write it ~10 s after the last change, a 50 ms audio gap). */
static void con_run_amt(uint32_t trkn, uint32_t id)
{
    if (id < P_COUNT)
        trk[trkn].p[id] = (int16_t)(TP[id].min + (TP[id].max - TP[id].min) * 3 / 4);
}

static void con_run_heavy(uint32_t notes)
{
    static const uint8_t SCALE[8] = {0, 3, 7, 10, 12, 15, 19, 22};
    uint32_t k, i, n, l;
    uint8_t ins[INS_N], lane[INS_N];
    ins[0] = FXT_DIST;
    ins[1] = (FXT_BUILT >> FXT_COMP & 1u) ? FXT_COMP : FXT_FILT;
    lane[0] = FXT_DIST;
    lane[1] = FXT_DYN;
    if (song.playing || transport_req) {
        con_puts("refused: playing (stop first)\r\n");
        return;
    }
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        t->p[P_SLEN] = NSTEP;
        t->p[P_FXOFF] = 0;
        t->p[P_MUTE] = 0;
        t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 90;
        ins_put(k, ins);
        for (i = 0; i < INS_N; i++)
            con_run_amt(k, ins_amt(k, i));
        if (k == TRK_DRUM) {
            for (i = 0; i < NSTEP; i++) {
                dstep_t *s = &t->dstep[i];
                memset(s, 0, sizeof *s);
                s->on[0] = s->on[1] = 0xFFu;
                s->acc = (i & 3u) == 0u ? 1u : 0u;
            }
            continue;
        }
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &t->step[i];
            memset(s, 0, sizeof *s);
            for (n = 0; n < notes; n++)
                s->note[n] = (uint8_t)(36u + 12u * k + SCALE[(i + 3u * n) & 7u]);
            s->n = (uint8_t)notes;
            s->time = ST_NOTE;
            s->flags = (i & 3u) == 0u ? SF_ACCENT : 0u;
            s->vel = 100;
        }
    }
    for (l = 0; l < DRUM_LANES; l++) {
        lin_put(l, lane);
        lin_set(l, 0, 96);
        lin_set(l, 1, 96);
        dsend[l] = dsend_word(31, 31, 31);
    }
    fm1_irq_on();
    ui.force = 1;
    con_kv("heavy_notes", (int32_t)notes);
    con_kv("heavy_steps", NSTEP);
    con_kv("heavy_lanes", DRUM_LANES);
}

/* the dispatcher: 1 when the line was one of these commands */
static int con_run(const char *p)
{
    con_run_t r = con_run_parse(p);
    switch (r.op) {
    case CR_NONE:
        return 0;
    case CR_BAD:
        con_puts("usage: play | pause | stop | rec on|off | proj load 0..3 | heavy [1..4] | wdtreset\r\n");
        break;
    case CR_PLAY:
        con_run_play();
        break;
    case CR_STOP:
        con_run_stop();
        break;
    case CR_REC_ON:
    case CR_REC_OFF:
        con_run_rec(r.op == CR_REC_ON);
        break;
    case CR_PROJ_LOAD:
        con_run_proj_load(r.arg);
        break;
    case CR_HEAVY:
        con_run_heavy(r.arg);
        break;
    case CR_WDTRESET:
#if FELUCCA_DUAL && FELUCCA_DUALCFG
        con_puts("wdtreset\r\n");
        con_reboot_req = 2;                             /* (main loop: as `reboot`, then the watchdog) */
#else
        con_puts("wdtreset: not in this build (needs the dualcfg image)\r\n");
#endif
        break;
    }
    return 1;
}
