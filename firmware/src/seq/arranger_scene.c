/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared scene application for firmware and host audio integration tests. */
#if !SEC_LOGGED                                /* (FELUCCA_SECTIONS 8 / 16: sections.c) */
static uint32_t arrangement_ready(void)
{
    uint32_t i, ready = 0;
    for (i = 0; i < ARR_SCENES; i++)
        if (proj_ok(&proj_slot[i])) ready |= 1u << i;
    return ready;
}


#if FELUCCA_QCHAIN
/* the bars a section's loop takes: its longest pattern, ceil(LEN x step / bar), 1..64 (SLOOP 2.4 section_bars) */
static uint32_t project_bars(const project_t *p)
{
    uint32_t k, bars = 1;
    for (k = 0; k < NTRK; k++) {
        uint32_t len = (uint32_t)clamp(p->t[k].p[P_SLEN], 1, NSTEP), u = div_units((uint32_t)p->t[k].p[P_SDIV] % NDIV_STEP);
        uint32_t b = (len * u + 4u * BEAT_U - 1u) / (4u * BEAT_U);   /* (64 x 8 beats fits 32 bits) */
        if (b > bars)
            bars = b;
    }
    return bars > 64u ? 64u : bars;
}
static uint32_t section_bars(uint32_t s) { return proj_ok(&proj_slot[s % ARR_SCENES]) ? project_bars(&proj_slot[s % ARR_SCENES]) : 1u; }
#endif

/* Called only at an audio-block boundary, after validation of all slots at
 * start. The song keeps one tempo and global FX across every section (only the drum
 * level and reverb come from it). */
static void arrangement_apply(uint32_t scene)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        seq_release(t);
        trk_all_off(t);
        t->nheld = t->arp_phys = t->arp_note = t->rh_n = t->rskip_n = 0;
        t->rskip_lanes = 0;
    }
    proj_apply(&proj_slot[scene], &proj_dl[scene], 0);
    sync_reload = 1;
    ui.force = 1;
}

#endif

/* ---- song mode keeps the loop you made: PLAY in song mode puts it aside (each section then plays
 * over the tracks), STOP (or the song's end) brings it back */
static project_t song_keep __attribute__((section(".pool")));
static dlrec_t song_keep_dl;                   /* its drum record */
static uint8_t song_kept;
static void song_backup(void)                  /* (audio ISR: seq_start) */
{
    proj_capture(&song_keep, &song_keep_dl);
    song_kept = 1;
}
static void song_restore(void)                 /* (audio ISR: seq_stop) */
{
    uint32_t i;
    if (!song_kept)
        return;
    song_kept = 0;
    for (i = 0; i < NTRK; i++) {
        trk_all_off(&trk[i]);
        trk[i].nheld = trk[i].arp_phys = trk[i].arp_note = 0;
    }
    proj_apply(&song_keep, &song_keep_dl, 1);
    song.sel = (uint8_t)(song_keep.sel < NTRK ? song_keep.sel : 0u);
    sync_reload = 1;
    ui.force = 1;
}
