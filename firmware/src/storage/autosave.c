/* SPDX-License-Identifier: GPL-3.0-only */
/* ---- the working project, kept in flash by itself (autosave_tick), loaded at power-on (autosave_resume). Included by
 * project.c (main loop only; tests/autosave_test.c drives it on the host). When:
 *   without LIVE_SAVE  only while stopped and silent (a flash erase stops the audio for ~50 ms);
 *   with LIVE_SAVE     also while playing and while a voice sounds (2026-11-08: "i lost some changes after black
 *                      reboot": the work edited while playing was written only after a stop and the silence, a
 *                      power-off before that lost it; a held drone, a stuck voice or the computer's USB audio stream
 *                      kept it from ever being written). Its erases go in 1 ms windows, the audio playing on
 *                      (hal/fm1_flash_slice.h), as LIVE_SAVE's sections and patterns (project.c sections_flush); the
 *                      main loop (screen, panel) waits for the write, tens of ms. Not while recording (the audio ISR
 *                      writes the steps), not in a song (the tracks hold its section: persist_flush_now's rule), and
 *                      back to stopped and silent once the flash part held an erase in one window (live_save_off,
 *                      until the next power-on). Playing, the capture is taken twice and written only when both agree
 *                      (no copy torn by the ISR: a pattern swapped at its bar, a lock).
 * Its change hash taken every second:
 *   settled   the work has not changed for AUTOSAVE_SETTLE: saved (no panel idle time: an edit is a change of the
 *             hash, whatever made it);
 *   changing  the work keeps changing (a tweak every few seconds, MIDI CCs): saved anyway once
 *             it has been unsaved for AUTOSAVE_MAX_WAIT, then again at most every AUTOSAVE_MAX_WAIT while it goes on;
 *   gap       never two saves within AUTOSAVE_MIN_GAP (the flash wear, the user's choice 2026-10-13); the first save
 *             after power-on is not held back. Before fix/autosave nothing was saved in the first 20 s after power-on
 *             (and a save waited for 2.5 s without input): a power-off there lost the edit.
 * Wear: a save erases one 4 KiB sector, the other copy of OBJ_AUTOSAVE (A 0x9F000 /
 * B 0xFE000, alternately: each copy 0.5 erase a save); the drum record pair (0xE5000 / 0xE6000) only when the drum
 * lanes changed; the motion form is programmed into the copy just written (no erase); the extras, FX and LFO records
 * are appended to the section log (8 sectors, ~16..400 B a save: ~0.006..0.12 erase of a log sector a save). The
 * flash, a Puya P25Q80H (JEDEC 0x856014, hal/fm1_flash.h), is rated 100k erase cycles a sector: the copies last
 * 200k saves. 4 h a day of editing: the worst (a save every MIN_GAP) 240 saves/day ~2.3 years; continuous changes (one
 * save per MAX_WAIT) 120/day ~4.6 years; a settled edit every 5 min 48/day ~11 years. (Without the gap, the settle
 * rule alone allowed a save every 11 s: ~1,300/day, ~150 days.) */
#define AUTOSAVE_SETTLE 10000u                 /* ms the work stays the same before it is saved */
#define AUTOSAVE_MAX_WAIT 120000u              /* ms the work may stay unsaved while it keeps changing */
#define AUTOSAVE_MIN_GAP 60000u                /* ms between two saves at least (the first after power-on: none) */
#define AUTOSAVE_HANDS_MS 400u                 /* ms after the last press or knob turn (and nothing down) before a check */
#if FELUCCA_LIVE_SAVE
#define AUTOSAVE_LIVE() (!live_save_off)       /* the erases in windows, the audio on: playing or sounding, written */
#if FELUCCA_ARRANGER
#define AUTOSAVE_SONG() arrangement_clock.running
#else
#define AUTOSAVE_SONG() 0
#endif
#define AUTOSAVE_WAITS_PLAY() (transport_req || (song.playing && (!AUTOSAVE_LIVE() || song.rec || AUTOSAVE_SONG())))
#else
#define AUTOSAVE_LIVE() 0
#define AUTOSAVE_WAITS_PLAY() (song.playing || transport_req)
#endif
#ifdef SEC_RB_WORK
#define autosave_buf SEC_RB_WORK               /* (sections.c sec_rb: shared with the section records' buffer) */
#else
static project_t autosave_buf __attribute__((section(".pool")));
#endif
static dlrec_t autosave_dl;                    /* its drum record */
static uint32_t autosave_hash, autosave_checked;   /* the saved work's hash; the last check */
static uint32_t autosave_seen, autosave_seen_ms;   /* the work's hash at the last check, since when it is that */
static uint32_t autosave_dirty_ms, autosave_put_ms; /* since when the work differs from the saved one; the last try */
static uint8_t autosave_dirty, autosave_tried; /* it differs (autosave_dirty_ms counts); a save tried since power-on */

#if FELUCCA_FLASH
/* the change test of the working project captured in p: everything stored with it (the project, its automation,
 * its FX record, its drum lanes' LFO record) */
static uint32_t autosave_hash_of(const project_t *p)
{
    return p->sum ^ AUTO_HASH() ^ fxr_hash(p) ^ DLR_HASH(p);   /* (the sum covers the drum record) */
}
/* autosave_buf (captured, its hash h) into OBJ_AUTOSAVE with its records; 0 written */
static int autosave_put(uint32_t h)
{
    int rc = proj_put(OBJ_AUTOSAVE, &autosave_buf, &autosave_dl);
    if (rc != 0)
        return rc;
    MOTION_SAVED(OBJ_AUTOSAVE, &autosave_buf);
#if FELUCCA_AUTO && SEC_LOGGED
    (void)sx_log_put(SX_ID_AUTO, autosave_buf.sum, &autosave_buf, 0);   /* its extras record (stepx_log.c) */
#endif
#if SEC_LOGGED
    (void)fxr_log_put(FXR_ID_AUTO, autosave_buf.sum, &autosave_buf, 0);   /* its FX record (fx_rec_log.c) */
    (void)dlr_log_put(DLR_ID_AUTO, autosave_buf.sum, &autosave_buf, 0);   /* its LFO record (dlfo_rec_log.c) */
#endif
    autosave_hash = h;
    return 0;
}
#endif

static int audio_quiet(void)
{
    uint32_t p, i;
#if FELUCCA_USB_AUDIO
    if (ua_streaming(fm1_ticks()))                         /* a flash write holds every IRQ off: the computer's stream would glitch */
        return 0;
#endif
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active)
                return 0;
    for (i = 0; FELUCCA_DRUMS && i < NDRUM; i++)
        if (drums.v[i].active)
            return 0;
#if DRUM_X0X
    if (x0x_sounding())                         /* (the X0X kits' channels) */
        return 0;
#endif
    return 1;
}

static void autosave_tick(void)                /* main loop */
{
#if FELUCCA_FLASH
    uint32_t h, now = fm1_ms;
    int settled, overdue;
#if STG_LEND
    stg_poll();                                         /* (the STORAGE reserve back to the undo history when idle) */
#endif
    if (!flash_ok || AUTOSAVE_WAITS_PLAY() || rec_wait || ft_on || ui.menu || now - autosave_checked < 1000u)
        return;
#if FELUCCA_PATTERNS
    if (slg.proj != open_proj)
        return;                                         /* (the log's index lent to another project: a backup, ed_backup.c) */
#endif
    if (fm1_in.notes || fm1_in.buttons || now - ui_input_ms < AUTOSAVE_HANDS_MS)
        return;                                         /* (hands on the panel: the capture and its hash, a few ms, wait for them to
                                                         * rest, never between a press and its release; checked again next pass) */
    autosave_checked = now;
    proj_capture(&autosave_buf, &autosave_dl);
    h = autosave_hash_of(&autosave_buf);
    if (song.playing) {                                 /* (the audio ISR runs on, the capture under it: a second one must agree,
                                                         * else one was torn, a pattern swapped at its bar: nothing written, settle
                                                         * again) */
        proj_capture(&autosave_buf, &autosave_dl);
        if (autosave_hash_of(&autosave_buf) != h) {
            autosave_seen_ms = now;
            return;
        }
    }
    if (h != autosave_seen) {                           /* changed (or the first check after playing): settle again */
        autosave_seen = h;
        autosave_seen_ms = now;
    }
    if (h == autosave_hash)
        autosave_dirty = 0;
    else if (!autosave_dirty) {
        autosave_dirty = 1;
        autosave_dirty_ms = now;
    }
    settled = now - autosave_seen_ms >= AUTOSAVE_SETTLE;
    overdue = autosave_dirty && now - autosave_dirty_ms >= AUTOSAVE_MAX_WAIT;
    if (!(settled || overdue) || (autosave_tried && now - autosave_put_ms < AUTOSAVE_MIN_GAP) ||
        !(AUTOSAVE_LIVE() || audio_quiet()))
        return;
#if FELUCCA_PATTERNS
    pat_state_save();                                   /* the tracks' pattern sources (pat.c), when they changed */
#endif
    if (!autosave_dirty)
        return;
    autosave_put_ms = now;                              /* (a failed write: tried again after AUTOSAVE_MIN_GAP) */
    autosave_tried = 1;
    if (autosave_put(h) == 0)
        autosave_dirty = 0;
    else
        autosave_seen_ms = now;
#endif
}

static void autosave_resume(void)              /* power-on: the project as it was left (felucca_init) */
{
#if FELUCCA_FLASH
    project_t *q = &autosave_buf;
    int n;
    if (!flash_ok)
        return;
    if (!proj_get(OBJ_AUTOSAVE, q, &autosave_dl))
        return;
    MOTION_READ(OBJ_AUTOSAVE, q);                       /* (its motion first: the extras record adds to it) */
#if FELUCCA_AUTO && SEC_LOGGED
    sx_log_get(SX_ID_AUTO, q->sum, q);                  /* its extras record (stepx_log.c), applied with it */
#endif
#if SEC_LOGGED
    fxr_log_get(FXR_ID_AUTO, q->sum, q);                /* its FX record (fx_rec_log.c), applied with it */
    dlr_log_get(DLR_ID_AUTO, q->sum, q);                /* its LFO record (dlfo_rec_log.c), applied with it */
#endif
    proj_apply(q, &autosave_dl, 1);
    autosave_hash = autosave_hash_of(q);                /* (all of it, as autosave_tick: else a needless save each power-on) */
    song.sel = (uint8_t)(q->sel < NTRK ? q->sel : 0u);
#if FELUCCA_PATTERNS
    pat_state_load();                                   /* the tracks' pattern sources as they were left (pat.c) */
#endif
    for (n = 0; n < NPART; n++)
        trk[n].engine = trk[n].eng_req;        /* (nothing sounds yet: no fade) */
#endif
}
