/* SPDX-License-Identifier: GPL-3.0-only */
/* SD card mode's sample browser on the sound's own knob (FELUCCA_SD_UI && FELUCCA_SD_STREAM; the user, 2026-11-09: "when in
 * sd sample mode the k1 then scroll thru sample of the folder, opening the sample window (we click save to switch to other
 * source of sample). for both"). The card is SAMPLE's SET (GRAIN's, STRETCH's SRC) or a drum lane's SRC, showing SD:
 *   its knob     (sample mode, the default) the folder's next / previous sample: the popup (PROJECT > SAMPLES' folder list
 *                in a list as PRESETS', op_preset.c draw_pop, pop_kind 6) opens on the first turn, on the folder of the
 *                sample the part or lane plays (on that sample), else a lane's type folder (Samples/Kick, HH ..), else
 *                Samples. The list stops at its ends. The sample the item rests on SDC_LANE_SETTLE_MS is heard alone
 *                (PREVIEW's voice, sd_preview.h), playing or not; the one heard last becomes the part's or the lane's when
 *                the popup goes (sd_mode.c sdm_do USE): one project record and one stream plan a browse, never one a
 *                sample (on the FM-1, 2026-11-09: a USE a sample wrote the flash's log and placed the plan again every
 *                time: the screen and the keys flashed, playback stopped, the preview died). Any knob moves it while the
 *                popup is up; it goes SDC_BR_IDLE_MS after the last move
 *   a key        (popup up) its item on the page shown, heard
 *   SAVE         popup up: a folder: into it; a sample: the knob back to the source (the popup gone, "SOURCE"); popup down:
 *                the same switch, and from the source back to the samples (the popup up)
 *   HOME         (popup up) up a folder; at Samples the popup goes
 * In source mode the knob steps the source as any value (SD, KIT, a set ..); a source changed: sample mode again for it */
#if FELUCCA_SD_STREAM
#define SDC_BR_IDLE_MS 2500u                            /* the popup up this long after the last turn, key or SAVE */
static struct {
    uint8_t on, to, ask, gen, pick;                     /* open; the part, NPART + a lane; a sample to hear; bumped at every
                                                         * folder change (sdc_br_sig); the item heard + 1, taken when the popup goes */
    uint16_t src;                                       /* the cards in source mode, a bit each (part, NPART + lane) */
    uint32_t at, last;                                  /* when the item moved; the last action (fm1_ms) */
} sdc_br;
static int sdm_smp_dir(uint32_t k);                     /* storage/sd/sd_mode.c */
static void sdm_use_for(uint32_t k);
static const char *sdm_card_label(uint32_t k);
static int sdc_br_on(void) { return sdc_br.on; }
static void sdc_br_show(void)                           /* the popup held open */
{
    ui.toast_t = OP_TOAST_FRAMES;
    ui.toast_col = sdc_br.to >= NPART ? op_lane_col(sdc_br.to - NPART) : trk_col(sdc_br.to);
    pop_kind = 6;
}
static void sdc_br_end(void)                            /* the popup gone: the sample heard last the part's or the lane's */
{
    uint32_t k = sdc_br.pick;
    sdc_br.pick = 0;
    if (k && k - 1u < sdm_count() && sdc_used(k - 1u) && !sdc_is_dir(k - 1u) && !sdm_reading()) {
        sdm_use_for(sdc_br.to);
        sdc_use_now(k - 1u, NENGINES);                  /* (the part's sampler kept: sd_mode.c sdm_do) */
    }
    sdc_br.on = 0;
    ui.toast_t = 0;
    pop_kind = 0;
    ui.redraw = 1;
    ui.force = 1;
}
static void sdc_br_say(const char *m)                   /* a toast in the card's colour */
{
    ui.toast_next = 1;
    ui_message(m);
    ui.toast_next = 0;
    ui.toast_col = sdc_br.to >= NPART ? op_lane_col(sdc_br.to - NPART) : trk_col(sdc_br.to);
}
static void sdc_br_find(uint32_t k)                     /* the item on sample k's file when the page shows it */
{
    uint32_t i, j;
    const char *nm = sdm_card_label(k), *f;
    for (i = 0; nm[0] && i < sdm_count(); i++) {        /* ("KICK" is "KICK.WAV": both upper case, the label without its .wav) */
        if (!sdc_used(i) || sdc_is_dir(i))
            continue;
        f = sdc_name(i);
        for (j = 0; nm[j] && f[j] == nm[j]; j++)
            ;
        if (!nm[j] && f[j] == '.') {
            sdc.item = (uint8_t)i;
            return;
        }
    }
}
/* open on part k (NPART + a lane); 0 no card (said) */
static int sdc_br_open(uint32_t k)
{
    if (!sdm_ready()) {
        sdm_mount_try();
        if (!sdm_ready()) {
            ui_message("NO SD CARD");
            return 0;
        }
    }
    sdc_br.to = (uint8_t)k;
    sdc_br.ask = 0;
    sdc_br.pick = 0;
    sdc.view[SDC_SMP] = SDV_FOLDER;
    sdc.stale = 1;                                      /* (Samples again, then the lane's type folder: sdc_sync) */
    sdc_sync(SDC_SMP);
    sdm_wait();
    sdc.item = 0;
    if (sdm_smp_dir(k)) {                               /* (the folder of the sample it plays, on it) */
        sdm_wait();
        sdc_br_find(k);
    }
    sdc_br.gen++;
    sdc_br.on = 1;
    sdc_br.last = fm1_ms;
    sdc_br_show();
    return 1;
}
static void sdc_br_step(int32_t s)                      /* the item s on (no wrap: the list's ends stop it), taken once settled */
{
    uint32_t n = (uint32_t)(s < 0 ? -s : s);
    if (sdm_reading())
        return;
    if (n > 8u)
        n = 8u;
    while (n--)
        sdc_item_step(SDC_SMP, s > 0 ? 1 : -1);
    sdc_br.ask = 1;
    sdc_br.at = fm1_ms;
    sdc_br.last = fm1_ms;
}
static void sdc_br_take(void)                           /* the item settled: heard alone, the one the popup's end takes */
{
    uint32_t k = sdc.item;
    if (!sdc_br.ask || sdm_reading() || (uint32_t)(fm1_ms - sdc_br.at) < SDC_LANE_SETTLE_MS)
        return;
    sdc_br.ask = 0;
    if (!sdc_used(k) || sdc_is_dir(k)) {
        sdm_pv_stop();
        return;
    }
    sdc_br.pick = (uint8_t)(k + 1u);
    sdm_pv_play(k);
}
static void sdc_br_source(void)                         /* the knob back to the source: the popup gone */
{
    sdc_br.src |= (uint16_t)(1u << sdc_br.to);
    sdc_br_end();
    sdc_br_say("SOURCE");
}
static void sdc_br_yes(void)                            /* SAVE, popup up: a folder: into it; a sample: the source */
{
    uint32_t k = sdc.item;
    if (sdm_reading())
        return;
    if (sdc_used(k) && sdc_is_dir(k)) {
        sdm_pv_stop();
        sdc_br.pick = 0;
        if (sdm_into(k))
            sdc.item = 0, sdc_br.gen++;
        sdc_br.last = fm1_ms;
        return;
    }
    if (sdc_used(k))
        sdc_br.pick = (uint8_t)(k + 1u);                /* (not settled yet: this one) */
    sdc_br_source();
}
static void sdc_br_keys(void)                           /* the white keys: the page's slots, taken and heard at once */
{
    while (lk_r != lk_w) {
        uint32_t e = lk_q[lk_r % LKQ];
        int32_t w;
        lk_r++;
        if (!((e >> 7) & 1u) || (e >> 8) != LY_STEP)
            continue;
        w = punch_key(e & 0x7Fu);
        if (w < 0 || (uint32_t)w >= sdm_count() || sdm_reading() || !sdc_used((uint32_t)w))
            continue;
        sdc.item = (uint8_t)w;
        sdc_br.ask = 1;
        sdc_br.at = fm1_ms - SDC_LANE_SETTLE_MS;
        sdc_br.last = fm1_ms;
    }
}
static void sdc_br_frame(uint32_t pressed)              /* one UI frame of the browser (op_input.c ui_input) */
{
    uint32_t k;
    int32_t s = panel_enc(EN_PRESET) + panel_enc(EN_SELECT);
    for (k = 0; k < 4u; k++)
        s += panel_enc(EN_K1 + k);
    (void)panel_enc(EN_ALGO);
    if (!sdm_ready()) {
        sdc_br.pick = 0;                                /* (the card gone: nothing to take) */
        sdc_br_end();
        return;
    }
    if (s)
        sdc_br_step(s);
    sdc_br_keys();
    if (pressed & (1u << panel.btn[B_HOME])) {
        sdm_pv_stop();
        if (sdm_at_top() || !sdm_up()) {                /* (gone: the one heard taken) */
            sdc_br_end();
            return;
        }
        sdc.item = 0;
        sdc_br.pick = 0;
        sdc_br.gen++;
        sdc_br.last = fm1_ms;
    }
    if (pressed & (1u << panel.btn[B_SAVE])) {
        sdc_br_yes();
        if (!sdc_br.on)
            return;
    }
    if (!sdm_reading() && sdc.item >= sdm_count() && sdm_count())
        sdc.item = (uint8_t)(sdm_count() - 1u);         /* (a shorter last page, come) */
    sdc_br_take();
    if (!sdc_br.ask && (uint32_t)(fm1_ms - sdc_br.last) >= SDC_BR_IDLE_MS) {
        sdc_br_end();                                   /* (left alone: gone, the sample kept, its preview let ring) */
        return;
    }
    sdc_br_show();
}
/* the card of page pg's knob k showing SD: its part (NPART + a lane), else -1 */
static int32_t sdc_br_card(const page_t *pg, uint32_t k)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(pg, k, &vp);
    if (!d || !vp)
        return -1;
    if (pd_names(d) == SMP_ALL_NAMES && *vp == (int16_t)SMP_CARD && song.sel < NPART)
        return (int32_t)song.sel;
#if FELUCCA_SD_LANES
    if (pg->scope == SC_DSND && page_id(pg, k) == 8u && is_drum(TSEL) && dl.src[lane_selected() % DRUM_LANES] == DL_CARD)
        return (int32_t)(NPART + lane_selected() % DRUM_LANES);
#endif
    return -1;
}
/* page_turn's first look: the card in sample mode: the samples, 1 taken; in source mode: the source's turn (0), the mode
 * back to the samples for it when the source leaves SD */
static int sdc_br_turn(const page_t *pg, uint32_t k, int32_t s)
{
    int32_t c = sdc_br_card(pg, k);
    if (c < 0 || s == OP_RESET)
        return 0;
    if (sdc_br.src >> c & 1u) {
        sdc_br.src &= (uint16_t)~(1u << c);             /* (the source turned off SD: samples next time it shows SD) */
        return 0;
    }
    if (!sdc_br_open((uint32_t)c))
        return 1;
    sdc_br_step(s);
    return 1;
}
/* SAVE on the card showing SD (popup down): sample mode -> source mode, source mode -> the samples (the popup up). 1 done */
static int sdc_br_save(const page_t *pg, uint32_t k)
{
    int32_t c = sdc_br_card(pg, k);
    if (c < 0)
        return 0;
    if (sdc_br.src >> c & 1u) {
        sdc_br.src &= (uint16_t)~(1u << c);
        (void)sdc_br_open((uint32_t)c);
    } else {
        sdc_br.to = (uint8_t)c;
        sdc_br_source();
    }
    return 1;
}
static uint32_t sdc_br_sig(void)                        /* what the popup shows (op_preset.c pre_sig) */
{
    return ((sdm_page() * 64u + sdc.item) * 2u + (uint32_t)sdm_reading()) * 131u + sdm_count() * 7u + sdc_br.gen * 977u;
}
#endif
