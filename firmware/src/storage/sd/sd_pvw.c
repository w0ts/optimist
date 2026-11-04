/* SPDX-License-Identifier: GPL-3.0-only */
/* A card preset tried on the selected track (the user, 2026-11-05: "temporarily load this synth and see how it sounds on this
 * track, and we revert to the track's setting if not loaded"; op_sdcard.c, PRESETS' last black key). Included by sd_mode.c.
 *   sdm_pvw_show(k)   slot k's preset on the track, loaded as a real one (upreset.c up_card_give: its memory asked first, MEM
 *                     FULL or MISSING said, the track as it was) but marked nowhere: no snd_mark, no card reference. The first
 *                     takes a copy of the track's sound
 *   sdm_pvw_end()     the copy back, whatever track is selected now
 *   sdm_pvw_keep()    the preview is the track's sound from now on, as a LOAD's bookkeeping leaves it
 *   sdm_pvw_drop()    forgotten: a project came in over it (storage/project.c project_apply)
 * On the drum track the same with a kit (the lanes, their sends, their card samples, the base kit, the card reference).
 * The preview never reaches a store: autosave waits on ui.menu while it is on, and whatever else captures the project ends it
 * first (project.c proj_capture, sections.c sec_capture). */
#ifdef PART_MEM_SHORT
#define SDM_PVW_NO_OFFER() (part_mem_refused.on = 0)   /* (engines.c: no make-room question over a preview) */
#else
#define SDM_PVW_NO_OFFER() ((void)0)
#endif
static struct {
    uint8_t on, part, eng_req, preset, user, silent, pend, pend_eng;
    uint8_t k;                                       /* the slot playing */
    uint16_t pkey;
    uint64_t id;
    sdm_ref_t ref;
    char pname[14];
    char path[SDB_NAME * 2];
    int16_t p[P_COUNT];
#if FELUCCA_ENG_FM6
    int16_t fm6[FM6_NP], fm6_cur;
#endif
#if FELUCCA_ENG_CZ
    uint8_t cz_tone[128];
    cz_ed_t cz_ed;
#endif
#if FELUCCA_DRUM_KITS
    __typeof__(dl) kit_dl;                           /* a kit on trial (kit): the lanes as they were */
    __typeof__(dsend) kit_send;
    int16_t kit_e0, kit_dl_e0;
    uint8_t kit, smp_dirty;
#if FELUCCA_SD_LANES
    sdm_smp_t kit_smp[DRUM_LANES];
    uint16_t lane_on;
#endif
#endif
} sdm_pvw IN_POOL;

static int sdm_pvw_on(void) { return sdm_pvw.on; }
static uint32_t sdm_pvw_part(void) { return sdm_pvw.part; }
#if FELUCCA_DRUM_KITS
static void sdm_pvw_kit_take(void)
{
    sdm_pvw.kit = 1;
    sdm_pvw.part = TRK_DRUM;
    sdm_pvw.kit_dl = dl;
    memcpy(sdm_pvw.kit_send, dsend, sizeof sdm_pvw.kit_send);
    sdm_pvw.kit_e0 = TDRUM->p[P_E0];
    sdm_pvw.kit_dl_e0 = (int16_t)dl_e0;
    sdm_pvw.ref = sdm_ref[NPART];
    sdm_pvw.smp_dirty = sdm_smp_dirty;
#if FELUCCA_SD_LANES
    memcpy(sdm_pvw.kit_smp, &sdm_smp[NPART], sizeof sdm_pvw.kit_smp);
    sdm_pvw.lane_on = (uint16_t)sdm_lane_on;
#endif
}
static void sdm_pvw_kit_end(void)
{
    sdm_pvw.kit = 0;
    fm1_irq_off();
    dl = sdm_pvw.kit_dl;
    memcpy(dsend, sdm_pvw.kit_send, sizeof sdm_pvw.kit_send);
    TDRUM->p[P_E0] = sdm_pvw.kit_e0;
    dl_e0 = sdm_pvw.kit_dl_e0;
    fm1_irq_on();
    sdm_ref[NPART] = sdm_pvw.ref;
#if FELUCCA_SD_LANES
    memcpy(&sdm_smp[NPART], sdm_pvw.kit_smp, sizeof sdm_pvw.kit_smp);
    sdm_lane_on = 0xFFFFu;                           /* (the lanes' samples looked at again) */
#endif
    sdm_smp_dirty = sdm_pvw.smp_dirty;
    sync_reload = 1;
    ui.redraw = 1;
    MISS_BUMP();
}
#endif
static void sdm_pvw_take(uint32_t part)
{
    const track_t *t = &trk[part];
    sdm_pvw.part = (uint8_t)part;
    sdm_pvw.eng_req = t->eng_req;
    sdm_pvw.preset = t->preset;
    sdm_pvw.user = t->user;
#if PART_DROPS
    sdm_pvw.silent = t->silent;
#endif
    memcpy(sdm_pvw.p, t->p, sizeof sdm_pvw.p);
    sdm_pvw.pkey = sdm_pkey[part];
    memcpy(sdm_pvw.pname, sdm_pname[part], sizeof sdm_pvw.pname);
    sdm_pvw.ref = sdm_ref[part];
#if PRESETS_CARD
    sdm_pvw.pend = sdm_fac_pend[part];
    sdm_pvw.pend_eng = sdm_fac_eng[part];
#endif
#if FELUCCA_ENG_FM6
    memcpy(sdm_pvw.fm6, fm6_ed[part], sizeof sdm_pvw.fm6);
    sdm_pvw.fm6_cur = fm6_cur[part];
#endif
#if FELUCCA_ENG_CZ
    if (t->engine == (uint8_t)ENG_SLOT_CZ) {
        memcpy(sdm_pvw.cz_tone, cz_tone(part), sizeof sdm_pvw.cz_tone);
        sdm_pvw.cz_ed = cz_ed(part);
    }
#endif
}
/* the sound as it was: all of it, in one go for the audio (as up_load's), the old one fading out */
static void sdm_pvw_end(void)
{
    uint32_t part = sdm_pvw.part;
    track_t *t = &trk[part];
    if (!sdm_pvw.on)
        return;
    sdm_pvw.on = 0;
    ui.menu = 0;
#if FELUCCA_DRUM_KITS
    if (sdm_pvw.kit) {
        sdm_pvw_kit_end();
        return;
    }
#endif
    if (part_mem_reserve(part, sdm_pvw.eng_req)) {
        SDM_PVW_NO_OFFER();                          /* (nothing took the room but the preview itself: never expected) */
        ui_say("MEM FULL", "");
    }
    sound_change(t);
    panic_req |= (uint8_t)(1u << part);
    fm1_irq_off();
    t->eng_req = sdm_pvw.eng_req;
    memcpy(t->p, sdm_pvw.p, sizeof sdm_pvw.p);
    t->preset = sdm_pvw.preset;
    t->user = sdm_pvw.user;
#if PART_DROPS
    t->silent = sdm_pvw.silent;
#endif
#if FELUCCA_ENG_FM6
    memcpy(fm6_ed[part], sdm_pvw.fm6, sizeof sdm_pvw.fm6);
    fm6_cur[part] = sdm_pvw.fm6_cur;
#endif
#if FELUCCA_ENG_CZ
    if (sdm_pvw.eng_req == (uint8_t)ENG_SLOT_CZ) {   /* (its tone lives in its memory: the switch now, as sdm_cz_load's) */
        uint32_t i;
        if (t->engine != (uint8_t)ENG_SLOT_CZ) {
            for (i = 0; i < NVOICE; i++)
                t->v[i].active = 0;
            t->engine = (uint8_t)ENG_SLOT_CZ;
            part_mem_claim(part, ENG_SLOT_CZ);
        }
        memcpy(cz_tone(part), sdm_pvw.cz_tone, sizeof sdm_pvw.cz_tone);
        cz_ed(part) = sdm_pvw.cz_ed;
        cz_key[part] = 0xFFFFFFFFu;                  /* (the points built again from these bytes, cz_block) */
    }
#endif
    fm1_irq_on();
    sdm_pkey[part] = sdm_pvw.pkey;
    memcpy(sdm_pname[part], sdm_pvw.pname, sizeof sdm_pvw.pname);
    sdm_ref[part] = sdm_pvw.ref;
#if PRESETS_CARD
    sdm_fac_pend[part] = sdm_pvw.pend;
    sdm_fac_eng[part] = sdm_pvw.pend_eng;
#endif
    sync_reload = 1;
    ui.redraw = 1;
    MISS_BUMP();
}
static void sdm_pvw_drop(void)
{
    sdm_pvw.on = 0;
#if FELUCCA_DRUM_KITS
    sdm_pvw.kit = 0;
#endif
    ui.menu = 0;
}
#if FELUCCA_DRUM_KITS
/* slot k's kit on the drum track: 1 it plays */
static int sdm_pvw_kit_show(uint32_t k)
{
    uint64_t id = 0;
    uint32_t n;
    if (!sdm_ready() || !sdb_path(&sdm.b, k, sdm.path, sizeof sdm.path)) {
        ui_message("NOT ON THE CARD");
        return 0;
    }
    n = sdm_small_get_id(sdm.path, "OPKT", sd_blk, sizeof sd_blk, &id);
    if (!n) {
        ui_message("NOT A KIT");
        return 0;
    }
    if (!sdm_pvw.on)
        sdm_pvw_kit_take();
#if FELUCCA_SD_LANES
    n = sdm_kit_lanes_in(sd_blk, n);                 /* (its lanes' card samples: sdm_poll stores no record while this is on) */
#endif
    if (ukit_card_give(sd_blk, n)) {
        if (!sdm_pvw.on)
            sdm_pvw_kit_end();
        ui_message("NOT A KIT");
        return 0;
    }
    sdm_pvw.on = 1;
    sdm_pvw.k = (uint8_t)k;
    sdm_pvw.id = id;
    sdl_cat(sdm_pvw.path, 0, sizeof sdm_pvw.path, sdm.path);
    ui.menu = 1;
    ui.redraw = 1;
    return 1;
}
#endif
/* slot k's preset on the selected track: 1 it plays (a file that is no preset: 0, said) */
static int sdm_pvw_show(uint32_t k)
{
    uint32_t part = song.sel, n, fresh = !sdm_pvw.on;
    uint64_t id = 0;
    int rc;
#if FELUCCA_DRUM_KITS
    if (sdm.list == SDC_KIT)
        return sdm_pvw_kit_show(k);
#endif
    if (is_drum(TSEL)) {
        ui_message("DRUM TRACK: NO SOUND");
        return 0;
    }
    if (!sdm_ready() || !sdb_path(&sdm.b, k, sdm.path, sizeof sdm.path)) {
        ui_message("NOT ON THE CARD");
        return 0;
    }
    n = sdm_small_get_id(sdm.path, "OPPR", sd_blk, sizeof sd_blk, &id);
    if (!n) {
        ui_message("NOT A PRESET");
        return 0;
    }
    if (fresh)
        sdm_pvw_take(part);
    rc = up_card_give(sd_blk, n);
    if (rc) {                                        /* (up_load said why: MEM FULL, MISSING; no room made in a preview) */
        SDM_PVW_NO_OFFER();
        if (rc < 0)
            ui_message("NOT A PRESET");
#if PRESETS_CARD
        if (fresh)
            sdm_fac_pend[part] = sdm_pvw.pend;       /* (up_load's sdm_factory_drop: the track is as it was) */
#endif
        ui.redraw = 1;
        ui.force = 0;
        return 0;
    }
    sdm_pvw.on = 1;
    sdm_pvw.k = (uint8_t)k;
    sdm_pvw.id = id;
    sdl_cat(sdm_pvw.path, 0, sizeof sdm_pvw.path, sdm.path);
    ui.menu = 1;                                     /* (autosave waits) */
    sdm_named(part, sdb_base(sdm.path));             /* (the header says what plays) */
    ui.force = 0;                                    /* (up_load's clear: a black flash a pick) */
    ui.redraw = 1;
    return 1;
}
static void sdm_pvw_keep(void)
{
    const uint32_t part = sdm_pvw.part;
    if (!sdm_pvw.on)
        return;
    sdm_pvw.on = 0;
    ui.menu = 0;
#if FELUCCA_DRUM_KITS
    if (sdm_pvw.kit) {
        sdm_pvw.kit = 0;
        sdm_ref_set(NPART, sdm_pvw.path, sdm_pvw.id);   /* (the project's kit: found again by its id; its samples' record too) */
        snd_mark(TRK_DRUM);
        ui_say("LOADED ", sdm_disp(sdb_base(sdm_pvw.path)));
        return;
    }
#endif
    sdm_ref_set(part, sdm_pvw.path, sdm_pvw.id);     /* (the part's preset: found again by its id) */
    snd_mark(part);
    ui_say("LOADED ", sdm_disp(sdb_base(sdm_pvw.path)));
}
