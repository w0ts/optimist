/* SPDX-License-Identifier: GPL-3.0-only */
/* SD card mode (FELUCCA_SD_UI, docs/SD-CARD-DESIGN.md 9): the card's layout (sd_layout.c ... sd_save.c) wired into the
 * device. Included by felucca.c after sdcard.c (the card), the stores (sections, user presets, kits) and the UI, whose
 * PROJECT page calls these (ui/optimist/op_sdcard.c: the lists, the enums SDL_* SDV_* SDO_*). Main loop only.
 *   the boot     sdm_poll (main.c, every pass): after the boot's mount the device record read (the settings, a card save
 *                pending), sdl_prepare, sdp_boot (a pending save finished from the flash copy), then the sample index's
 *                pass (one pass after each mount, its message shown first); no card: the flash store as without the
 *                card, NO SD CARD said once; a card gone while in use: said, the lists empty, a later mount (PROJECT
 *                entered, SYSTEM > SD CARD's YES) takes it again
 *   the project  the working project stays the one-project flash store's (PROJ_SLOTS 1, project 0): a card SAVE is
 *                the flash SAVE first (prj_save_go), then the card file written from that copy (sdp_save: the staged
 *                bytes are project 0's records as one stream, as the backup's PJ01: u8 id, u16 length, the record, for
 *                every id that is not device-wide); a card LOAD writes a file's records into project 0 (cleared first)
 *                and opens it; the project's name on the card is the folder's, its first 12 characters the flash
 *                project's name (the NAME page's prefill)
 *   the device record  the log's SEC_ID_SDCARD (device-wide, sec_log.c): VERSIONS (sdp_keep), SLACK (sda_scale), the
 *                pending save (sdp_hooks). A pending save whose flash copy is no longer that project's (another
 *                project's name now: NEW, a LOAD) is dropped, not finished with other work
 *   the samples  the project's SEC_ID_SDSMP: each synth part's sample by its XXH64 (the index's), size and a name hint
 *                (its path under SAMPLES/, cut); the player reads them (sdm_sample_of, sdm_sample_path) */
#include "storage/sd/sd_names.h"                     /* the engines' preset folders (Presets/Factory|User/<Engine>) */
#include "storage/sd/sd_ids.h"                       /* the presets' and kits' ids */
_Static_assert(SDC_KEYS == SDB_KEYS && SDC_NAME == SDL_NAME_MAX + 1u && SDC_PNAME == SEC_NAME_LEN + 1u,
               "sd_mode.c: the UI's sizes (op_sdcard.c) are the card's");
#define SDM_ID SEC_ID_SDCARD
#define SDM_HINT 52u
enum { SDM_OFF = 0, SDM_NONE, SDM_MOUNTED, SDM_INDEX, SDM_NAMES, SDM_READY };   /* the card's state for the UI */

typedef struct {
    uint8_t magic[2];                                /* "SD" */
    uint8_t keep, scale;                             /* VERSIONS 0..SDM_KEEP_MAX; SLACK in steps of 25 % (0..16) */
    uint8_t pend;                                    /* a card save pending: p */
    uint8_t pre;                                     /* the pre-buffer (SD_STREAM): SST_PRE_MS[pre - 1], 0 its default */
    uint8_t ids[2];                                  /* the user ids made so far (sdm_new_id), little-endian */
    sdp_pending p;
} sdm_rec_t;

typedef struct {
    uint64_t hash;                                   /* the sample's (sd_index.c), 0 none */
    uint32_t size;
    char hint[SDM_HINT];                             /* its path under SAMPLES/ when picked (cut), 0 ended */
} sdm_smp_t;
#if FELUCCA_SD_LANES
#define SDM_NSMP (NPART + DRUM_LANES)                /* the parts', then the drum lanes' (drums/lane_card.c; a record of
                                                      * before has the parts' alone: read so, the lanes none) */
#else
#define SDM_NSMP NPART
#endif
_Static_assert(sizeof(sdm_smp_t) == 64u && sizeof(sdm_smp_t) * SDM_NSMP <= SEC_REC_MAX, "sd_mode.c: the samples' record");

#define SDM_DX7 FELUCCA_ENG_FM6                      /* DX7 banks in PRESETS (below: the FM6 collections on the card) */
#ifndef SDM_REBOOT
#define SDM_REBOOT() fm1_reboot()
#endif
static uint32_t sdm_reboot_at;                       /* (fm1_ms + 1 when a restore asked for the restart: sdm_poll) */
#define SDM_KEEP_MAX 16u
#if PRESETS_CARD
static int sdm_factory_step(void);
static void sdm_factory_poll(void);
#endif
#define SDM_SCALE_MAX 16u                            /* (x 25 %: 400 %, sd_alloc.c's cap) */
static sdm_rec_t sdm_rec;
static uint8_t sdm_st, sdm_smp_dirty;               /* (the samples' record written once the audio is quiet: sdm_poll) */
static uint8_t sdm_dirty;                            /* the settings changed: 2 on SYSTEM, 1 SYSTEM left (written, quiet) */
/* the index's pass, in slices (sd_index.c): SDM_IX_SLICE_MS of each main loop pass, at most every SDM_IX_EVERY_MS. One unit
 * (an f_open, a 512-byte chunk) past the slice's end at most: ~20 ms a pass, under half the bus left to the streams and
 * the screen. Whoever needs the whole index waits for it: a project's presets and kit are looked up at SDM_READY, the
 * streams' plan again when the pass ends, and no "not on card" is said meanwhile */
#ifndef SDM_IX_SLICE_MS
#define SDM_IX_SLICE_MS 8u
#define SDM_IX_EVERY_MS 16u
#define SDM_FAC_AT_ONCE 0                            /* (a test: the names in one call) */
#endif
static uint8_t sdm_ix_force, sdm_ix_pct;
static uint32_t sdm_ix_at;
static uint8_t sdm_dropped;                          /* the boot's pending save was another project's: dropped */
static sdm_smp_t sdm_smp[SDM_NSMP];                  /* the open project's samples (the player's): parts, lanes */
static uint32_t sdm_smp_key;                         /* the record read (its sequence + 1; 0 none read) */
#if FELUCCA_SD_LANES
static uint16_t sdm_lane_on;                         /* the lanes given to the plan with their sample (CARD), 0xFFFF: again */
/* the lanes on CARD with their sample in the plan, the others' slots free (a lane turned back to KIT, a load) */
static void sdm_lanes_sync(void)
{
    uint32_t l, on = 0;
    for (l = 0; l < DRUM_LANES; l++)
        on |= (uint32_t)(dl.src[l] == DL_CARD && sdm_smp[NPART + l].hash) << l;
    if (on == sdm_lane_on)
        return;
    for (l = 0; l < DRUM_LANES; l++)
        sst_part_set(SST_NPART + l, (on >> l) & 1u ? sdm_smp[NPART + l].hash : 0u);
    sdm_lane_on = (uint16_t)on;
}
#endif
/* the presets and the kit the project's parts were loaded from (the user, 2026-11-04: found again by their id when
 * moved): each part's preset, then the drum kit; with the samples in the project's SEC_ID_SDSMP record (after them).
 * At a project's load: the path first; a file missing there or of another id: the id looked up in the index
 * (sd_index.c sdx_find_id), the path updated from there (the project's record: on the card at its next save); not
 * found: "P1 PRESET MISSING" / "KIT MISSING" once; two files of the id: the first found, "SAME ID TWICE" said */
typedef struct {
    uint64_t id;                                     /* the file's (sd_ids.h), 0 none */
    char hint[56];                                   /* its path from Optimist/ when loaded (cut), 0 ended */
} sdm_ref_t;
#define SDM_NREF (NPART + 1u)
_Static_assert(sizeof(sdm_ref_t) == 64u && sizeof(sdm_smp_t) * SDM_NSMP + sizeof(sdm_ref_t) * SDM_NREF <= SEC_REC_MAX,
               "sd_mode.c: the samples' and refs' record");
static sdm_ref_t sdm_ref[SDM_NREF];
static uint8_t sdm_ref_check;                        /* the refs to look up (a project's record read) */
static uint64_t sdm_small_id(const char *path);      /* (below: a .ops / .opk file's id) */
static uint32_t sdm_smp_gen;                         /* bumped at every change (the player reloads) */
static struct {
    sdb_t b;                                         /* the list shown */
    FIL f;
    uint8_t list, view;
    char vname[SDL_NAME_MAX + 1];                    /* VERSIONS: whose */
    char path[FF_LFN_BUF + 1], shown[SDL_NAME_MAX + 9u];
#if SDM_DX7
    uint8_t bank, bank_page;                         /* a DX7 bank's voices shown (its file's path in bpath), the page */
    char bpath[FF_LFN_BUF + 1];
    char bvn[SDB_KEYS][11];                          /* the page's voice names */
#endif
    struct { uint32_t base; uint16_t id, have; } sc; /* the staged stream's cursor (sdm_stage_read) */
} sdm IN_POOL;

static FRESULT sdm_was(FRESULT r) { return sd_was(r); }
static void sdm_rel_name(char *out, uint32_t cap, const char *rel);
/* a name from the card as the screen shows it (the user, 2026-11-04: the files are lowercase, the UI upper case): an
 * upper-case copy, cut at SDB_NAME - 1 bytes (the next call overwrites it) */
static const char *sdm_disp(const char *s)          /* (a bank's "03 - bass 1.ops": BASS 1) */
{
    static char d[SDB_NAME];
    uint32_t n;
    str_cpy(d, s + (sdb_bank_no(s) ? 5u : 0u), sizeof d);
    if ((n = sdl_ends(d, ".ops")) != 0 || (n = sdl_ends(d, ".opk")) != 0 || (n = sdl_ends(d, ".opp")) != 0)
        d[n] = 0;
    return sdl_upper(d);
}
#if FELUCCA_SD_STREAM
static void sdm_mem_tick(void);
static void sdm_bank_tick(void);
static void sdm_kit_tick(void);
static void sdm_slow_tick(void);
#endif

/* ---- the device record */
static void sdm_apply(void)
{
    sdp_keep = sdm_rec.keep;
    sda_scale = (uint16_t)(sdm_rec.scale * 25u);
#if FELUCCA_SD_STREAM
    sst_pre_set(SST_PRE_MS[sdm_rec.pre ? (sdm_rec.pre - 1u) & 3u : 1u]);
#endif
}
static void sdm_rec_read(void)
{
    if (slg_get(SDM_ID, sec_rbuf) == (int)sizeof sdm_rec && sec_rbuf[0] == 'S' && sec_rbuf[1] == 'D') {
        memcpy(&sdm_rec, sec_rbuf, sizeof sdm_rec);
        if (sdm_rec.keep > SDM_KEEP_MAX)
            sdm_rec.keep = 8;
        if (sdm_rec.scale > SDM_SCALE_MAX)
            sdm_rec.scale = 4;
        if (sdm_rec.pre > 4u)
            sdm_rec.pre = 0;
    } else {
        memset(&sdm_rec, 0, sizeof sdm_rec);
        sdm_rec.magic[0] = 'S', sdm_rec.magic[1] = 'D';
        sdm_rec.keep = 8;                            /* (sd_save.c's default) */
        sdm_rec.scale = 4;                           /* 100 % */
    }
    sdm_apply();
}
static int sdm_rec_put(void)                         /* 0 written (a small device-wide record: never refused for the reserve) */
{
    sdm.sc.have = 0xFFFFu;                           /* (sec_rbuf may be the log's scratch now: the cursor reads again) */
    return slg_put(SDM_ID, (const uint8_t *)&sdm_rec, sizeof sdm_rec, 1) ? -1 : 0;
}

/* ---- the staged copy: project 0's records as one stream (id, length lo, hi, the record) */
static uint32_t sdm_item(uint32_t id) { return !SLG_DEV(id) && slg_has(id) ? 3u + slg.alen[id] : 0u; }
static uint32_t sdm_stage_len(void)
{
    uint32_t id, n = 0;
    for (id = 0; id < SLG_IDS; id++)
        n += sdm_item(id);
    return n;
}
static int sdm_stage_read(uint32_t at, void *p, uint32_t n)   /* (sdp_hooks.read: mostly in order, a sector a call) */
{
    uint8_t *o = (uint8_t *)p;
    uint32_t len, off, t;
    if (!at || at < sdm.sc.base)
        sdm.sc.base = 0, sdm.sc.id = 0;
    while (n) {
        if (sdm.sc.id >= SLG_IDS)
            return -1;
        len = sdm_item(sdm.sc.id);
        if (at >= sdm.sc.base + len) {
            sdm.sc.base += len;
            sdm.sc.id++;
            continue;
        }
        if (sdm.sc.have != sdm.sc.id) {
            if (slg_get(sdm.sc.id, sec_rbuf) != (int)(len - 3u))
                return -1;
            sdm.sc.have = sdm.sc.id;
        }
        off = at - sdm.sc.base;
        if (off < 3u) {
            *o = off ? (uint8_t)((len - 3u) >> (off == 1u ? 0 : 8)) : (uint8_t)sdm.sc.id;
            t = 1;
        } else {
            t = len - off < n ? len - off : n;
            memcpy(o, sec_rbuf + off - 3u, t);
        }
        o += t, at += t, n -= t;
    }
    return 0;
}
static int sdm_pend_set(const sdp_pending *p)
{
    sdm_rec.p = *p;
    sdm_rec.pend = 1;
    return sdm_rec_put();
}
static int sdm_pend_clear(void)
{
    if (!sdm_rec.pend)
        return 0;
    sdm_rec.pend = 0;
    return sdm_rec_put();
}
/* a pending save (sdp_boot): still the flash project's (its name): finished with the copy as it is now (saved again
 * without the card meanwhile: that newer copy); another project's name now: dropped (the card keeps the old one whole) */
static int sdm_pend_get(sdp_pending *p)
{
    char nm[SEC_NAME_LEN + 1u], pn[SEC_NAME_LEN + 1u];
    if (!sdm_rec.pend)
        return 0;
    sec_name(0, nm);
    sdm_rel_name(pn, sizeof pn, sdm_rec.p.name);     /* (its place "A/03 - song": the name SONG, the flash's 12) */
    if (sdl_cmp(nm, pn)) {
        sdm_dropped = 1;
        (void)sdm_pend_clear();
        return 0;
    }
    sdm_rec.p.len = sdm_stage_len();
    if (sdp_stage_hash(sdm_rec.p.len, sd_blk, &sdm_rec.p.hash))
        return 0;
    *p = sdm_rec.p;
    return 1;
}

/* ---- the samples of the open project */
static void sdm_smp_sync(void)                       /* the record read again when the log's changed (a load, NEW, a pick) */
{
    uint32_t key = slg_has(SEC_ID_SDSMP) ? slg.aseq[SEC_ID_SDSMP] + 1u : 1u;
    if (key == sdm_smp_key)
        return;
    sdm_smp_key = key;
    {
        int n = slg_get(SEC_ID_SDSMP, sec_rbuf);
        memset(sdm_smp, 0, sizeof sdm_smp);
        memset(sdm_ref, 0, sizeof sdm_ref);
        if (n == (int)(sizeof sdm_smp + sizeof sdm_ref)) {
            memcpy(sdm_smp, sec_rbuf, sizeof sdm_smp);
            memcpy(sdm_ref, sec_rbuf + sizeof sdm_smp, sizeof sdm_ref);
            sdm_ref_check = 1;
        } else if (n == (int)sizeof sdm_smp || n == (int)(sizeof sdm_smp[0] * NPART)) {
            memcpy(sdm_smp, sec_rbuf, (uint32_t)n);
        }
    }
#if FELUCCA_SD_STREAM
    for (key = 0; key < NPART; key++)
        sst_part_set(key, sdm_smp[key].hash);        /* (the project's CARD samples: the plan places them) */
#endif
#if FELUCCA_SD_LANES
    sdm_lane_on = 0xFFFFu;                           /* (the lanes' slots set again: sdm_lanes_sync) */
#endif
    sdm.sc.have = 0xFFFFu;
    sdm_smp_gen++;
}
/* part k's sample: 1 (its hash, its hint), 0 none */
static int sdm_sample_of(uint32_t k, uint64_t *hash, const char **hint)
{
    if (k >= NPART || !sdm_smp[k].hash)
        return 0;
    *hash = sdm_smp[k].hash;
    *hint = sdm_smp[k].hint;
    return 1;
}
/* part k's sample on the card now: its path from the card's root found by its hash (a renamed or moved file too); its
 * length, 0 none (MISSING: the index has no such hash; a new file is hashed at the next mount's pass) */
static uint32_t sdm_sample_path(uint32_t k, char *out, uint32_t cap)
{
    if (k >= NPART || !sdm_smp[k].hash || sd_state != SD_READY || sdx_open() != FR_OK)
        return 0;
    if (sdx_find_hash(sdm_smp[k].hash, &sdx.rec) < 0)
        return 0;
    return sdl_cat(out, sdl_cat(out, 0, cap, SDL_ROOT "/"), cap, sdx.rec.path) < cap ? sdl_len(out) : 0;
}

/* ref k (a part's preset, NPART the kit) from the file at path (from the card's root) of id */
static void sdm_ref_set(uint32_t k, const char *path, uint64_t id)
{
    if (k >= SDM_NREF)
        return;
    memset(&sdm_ref[k], 0, sizeof sdm_ref[k]);
    sdm_ref[k].id = id;
    if (id)
        sdl_cat(sdm_ref[k].hint, 0, sizeof sdm_ref[k].hint, path + SDX_SKIP);
    sdm_smp_dirty = 1;
}
/* the project's refs found again (sdm_poll, the index ready): each at its path, else by its id -> the refs moved */
static uint32_t sdm_ref_resolve(void)
{
    static char p[SDX_PATH + 16];
    char a[30];
    uint32_t k, dups = 0, moved = 0, n;
    int32_t no;
    const char *miss = 0, *twice = 0;
    a[0] = 0;
    sdm_ref_check = 0;
    for (k = 0; k < SDM_NREF; k++) {
        if (!sdm_ref[k].id)
            continue;
        n = sdl_cat(p, 0, sizeof p, SDL_ROOT "/");
        if (sdl_cat(p, n, sizeof p, sdm_ref[k].hint) < sizeof p && sdm_small_id(p) == sdm_ref[k].id)
            continue;                                /* (where it was) */
        no = sdx_open() == FR_OK ? sdx_find_id(sdm_ref[k].id, &sdx.rec, &dups) : -1;
        if (no < 0) {
            miss = k < NPART ? "PRESET MISSING" : "KIT MISSING";
            str_cpy(a, k < NPART ? (char[3]){'P', (char)('1' + k), 0} : "", sizeof a);
            continue;
        }
        if (dups)
            twice = sdb_base(sdx.rec.path);
        memset(sdm_ref[k].hint, 0, sizeof sdm_ref[k].hint);
        sdl_cat(sdm_ref[k].hint, 0, sizeof sdm_ref[k].hint, sdx.rec.path);
        sdm_smp_dirty = 1;                           /* (its new path: in the project's record, the card's next save) */
        moved++;
    }
    if (miss) {
        if (a[0])
            str_app(a, " ", sizeof a);
        str_app(a, miss, sizeof a);
        ui_message(a);
    } else if (twice) {
        ui_say("SAME ID TWICE: ", twice);
    }
    return moved;
}
/* ---- the boot and the card's presence */
static int sdm_ready(void) { return sd_state == SD_READY && sdm_st >= SDM_INDEX; }
static void sdm_mount_try(void)                      /* (PROJECT entered, SYSTEM > SD CARD: no card yet: quick when none) */
{
    if (sd_state != SD_READY && sd_mount() == SD_READY)
        sdm_st = SDM_MOUNTED;
}
static void sdm_poll(void)
{
    FRESULT r;
    if (sdm_reboot_at && fm1_ms - sdm_reboot_at > 1000u)
        SDM_REBOOT();                                /* (a restore: the settings read at power-on; its message seen) */
    if (sdm_st == SDM_OFF) {                         /* (the first pass: the log is up) */
        sdp_hooks.read = sdm_stage_read;
        sdp_hooks.pend_set = sdm_pend_set;
        sdp_hooks.pend_get = sdm_pend_get;
        sdp_hooks.pend_clear = sdm_pend_clear;
        sdm.sc.have = 0xFFFFu;
        sdm_rec_read();
        sdm_st = sd_state == SD_READY ? SDM_MOUNTED : SDM_NONE;
        if (sdm_st == SDM_NONE)
            ui_message("NO SD CARD");
    }
    if ((sdm_dirty == 1u || sdm_smp_dirty) && !SAVE_WAITS_PLAY()) {   /* (a flash write stops the audio: seq.c LIVE_SAVE) */
        if (sdm_dirty == 1u && !sdm_rec_put())
            sdm_dirty = 0;
        if (sdm_smp_dirty) {                         /* (the samples, then the refs: one record) */
            memcpy(sec_rbuf, sdm_smp, sizeof sdm_smp);
            memcpy(sec_rbuf + sizeof sdm_smp, sdm_ref, sizeof sdm_ref);
            if (!slg_put(SEC_ID_SDSMP, sec_rbuf, sizeof sdm_smp + sizeof sdm_ref, 1))
                sdm_smp_dirty = 0;
        }
    }
    if (!sdm_smp_dirty)
        sdm_smp_sync();
#if FELUCCA_SD_LANES
    sdm_lanes_sync();
#endif
#if FELUCCA_SD_STREAM
    sdm_mem_tick();
    sdm_bank_tick();
    sdm_kit_tick();
    sdm_slow_tick();
#endif
    if (sdm_st >= SDM_MOUNTED && sd_state != SD_READY) {
        sdx_pass_cancel();                           /* (a card pulled mid-pass: index.bin as it was) */
        sdm_st = SDM_NONE;
        sdm.b.n = 0;
        sdm.b.pages = 1;
        ui_message("SD CARD GONE");
        ui.force = 1;
        return;
    }
    if (sdm_st == SDM_MOUNTED) {
        r = sdl_prepare();
        if (r == FR_OK && sdm_rec.pend) {            /* a save the last power-off cut: finished from the flash copy */
            sdm_dropped = 0;
            r = sdp_boot(sd_blk);
            ui_message(r != FR_OK ? "CARD SAVE PENDING" : sdm_dropped ? "CARD SAVE DROPPED" : "CARD SAVE FINISHED");
        } else if (r != FR_OK) {
            ui_message("SD CARD ERROR");
        }
        (void)sdm_was(r);
        (void)sdx_pass_begin(sdm_ix_force);          /* (a card unchanged since the last pass: nothing to walk) */
        sdm_ix_force = 0;
        sdm_ix_pct = 0;
        sdm_st = SDM_INDEX;
        ui.force = 1;
        return;
    }
    if (sdm_st == SDM_INDEX && (uint32_t)(fm1_ms - sdm_ix_at) >= SDM_IX_EVERY_MS) {
        sdm_ix_at = fm1_ms;
        (void)sdm_was(sdx_pass_step(sd_blk, sizeof sd_blk, SDM_IX_SLICE_MS));
        if (sdx_pass_on()) {
            if (sdx_pass_pct() != sdm_ix_pct) {      /* (SYSTEM > SD CARD's SAMPLES line) */
                sdm_ix_pct = sdx_pass_pct();
                ui.force = 1;
            }
        } else {
            sdm_st = SDM_NAMES;
            ui.force = 1;
#if FELUCCA_SD_STREAM
            if (sstp.missing)
                sstp.dirty = 1;                      /* (what the plan looked up in the old index, or none: again) */
#endif
        }
    }
    if (sdm_st == SDM_NAMES) {
#if PRESETS_CARD
        while (!sdm_factory_step())                  /* (the factory presets' names, an engine a pass) */
            if (!SDM_FAC_AT_ONCE)
                return;
#endif
        sdm_st = SDM_READY;
        ui.force = 1;
    }
#if PRESETS_CARD
    sdm_factory_poll();
#endif
    if (sdm_ref_check && sdm_st == SDM_READY && !op_armed())
        (void)sdm_ref_resolve();                     /* (a project's presets and kit: found again by their ids) */
}

/* ---- DX7 banks (the user, 2026-10-26: the FM6 collections on the card): a .SYX of 32 voices (4104 B: F0 43 0n 09 20 00,
 * 32 packed voices of 128 B, the checksum, F7: what every DX7 site and editor offers) anywhere under PRESETS is shown as
 * a folder of its voices, 16 a page; a voice's LOAD reads its 128 B and puts them into the selected part's FM6 voice
 * (eng_fm6.c fm6_unpack; the part on FM6 first if it plays another engine), as a single-voice dump does. Nothing is
 * kept in RAM but the page's names: no received bank needed (FM6_SYSEX) */
#if SDM_DX7
#define SDM_DX7_LEN 4104u
static int sdm_dx7_page(void)                        /* the page's 16 names from the file: 0 ok */
{
    UINT got;
    uint32_t k, n;
    FRESULT r = f_open(&sdm.f, sdm.bpath, FA_READ);
    for (k = 0; r == FR_OK && k < SDB_KEYS; k++) {
        n = sdm.bank_page * SDB_KEYS + k;
        r = f_lseek(&sdm.f, 6u + n * 128u + 118u);
        if (r == FR_OK && (r = f_read(&sdm.f, sdm.bvn[k], 10, &got)) == FR_OK && got != 10u)
            r = FR_INT_ERR;
        for (n = 0; n < 10u; n++)                    /* (7 bits; anything not printable a blank) */
            if (sdm.bvn[k][n] < 32 || sdm.bvn[k][n] > 126)
                sdm.bvn[k][n] = ' ';
        for (n = 10; n && sdm.bvn[k][n - 1u] == ' '; n--)
            ;
        sdm.bvn[k][n] = 0;
    }
    (void)f_close(&sdm.f);
    return sdm_was(r) == FR_OK ? 0 : -1;
}
static int sdm_dx7_open(uint32_t k)                  /* slot k (a .SYX) as a bank: its voices shown; 0 ok */
{
    FILINFO fi;
    uint8_t h[6];
    UINT got;
    if (!sdb_path(&sdm.b, k, sdm.bpath, sizeof sdm.bpath) || f_stat(sdm.bpath, &fi) != FR_OK || fi.fsize != SDM_DX7_LEN ||
        f_open(&sdm.f, sdm.bpath, FA_READ) != FR_OK)
        return -1;
    got = 0;
    (void)f_read(&sdm.f, h, 6, &got);
    (void)f_close(&sdm.f);
    if (got != 6u || h[0] != 0xF0 || h[1] != 0x43 || h[3] != 9u)
        return -1;
    sdm.bank = 1;
    sdm.bank_page = 0;
    return sdm_dx7_page();
}
#if FELUCCA_ENG_CZ
/* a Casio CZ single tone (the user, 2026-10-26: the CZ collections on the card): a .SYX of 264 B (F0 44 00 00 7n 20 pp,
 * 256 nibbles low first, F7: what CZ patch sites offer) under PRESETS, its key pressed: its 128 synthesis bytes into the
 * selected part's tone (eng_cz.c cz_tone; the part on CZ first), as a single-tone dump: kept until the part's TONE or
 * engine changes (a project keeps the TONE number, not the tone) */
#define SDM_CZ_LEN 264u
static int sdm_cz_load(uint32_t k)                   /* 0 loaded (said), -1 not a CZ tone */
{
    UINT got = 0;
    uint32_t i, p = song.sel;
    track_t *t = &trk[p % NTRK];
    if (!sdb_path(&sdm.b, k, sdm.bpath, sizeof sdm.bpath) || f_open(&sdm.f, sdm.bpath, FA_READ) != FR_OK)
        return -1;
    (void)f_read(&sdm.f, sd_blk, SDM_CZ_LEN, &got);
    (void)f_close(&sdm.f);
    if (got != SDM_CZ_LEN || sd_blk[0] != 0xF0 || sd_blk[1] != 0x44 || sd_blk[SDM_CZ_LEN - 1u] != 0xF7)
        return -1;
    if (p >= NPART) {
        ui_message("PICK T1 T2 OR T3");
        return 0;
    }
    if (t->eng_req != (uint8_t)ENG_SLOT_CZ) {
        set_engine_of(t, ENG_SLOT_CZ);
        if (t->eng_req != (uint8_t)ENG_SLOT_CZ)
            return 0;                                /* (refused: memory short, the drop is proposed: model.c) */
    }
    fm1_irq_off();
    if (t->engine != (uint8_t)ENG_SLOT_CZ) {         /* (the switch now, not at the next block: its memory for the tone) */
        for (i = 0; i < NVOICE; i++)
            t->v[i].active = 0;                      /* (the old engine's voices: faded already, sound_change) */
        t->engine = (uint8_t)ENG_SLOT_CZ;
        part_mem_claim(p, ENG_SLOT_CZ);
    }
    for (i = 0; i < 128u; i++)
        cz_tone(p)[i] = (uint8_t)((sd_blk[7u + 2u * i] & 15u) | (sd_blk[8u + 2u * i] & 15u) << 4);
    cz_ed(p).tone = (uint8_t)((uint32_t)t->p[P_E0] % (CZT_N + CZ_NUSER));   /* (its TONE as it is: not built again) */
    cz_key[p] = 0xFFFFFFFFu;                         /* (the points built again from these bytes, cz_block) */
    fm1_irq_on();
    snd_mark(p);
    ui_say("LOADED ", sdm_disp(sdb_base(sdm.bpath)));
    return 0;
}
#endif
static void sdm_dx7_load(uint32_t k)                 /* voice k of the page into the selected part */
{
    uint8_t v[128];
    UINT got = 0;
    uint32_t p = song.sel;
    track_t *t = &trk[p % NTRK];
    if (p >= NPART) {
        ui_message("PICK T1 T2 OR T3");
        return;
    }
    if (f_open(&sdm.f, sdm.bpath, FA_READ) == FR_OK) {
        if (f_lseek(&sdm.f, 6u + (sdm.bank_page * SDB_KEYS + k) * 128u) == FR_OK)
            (void)f_read(&sdm.f, v, 128, &got);
        (void)f_close(&sdm.f);
    }
    if (got != 128u) {
        ui_message("NOT A DX7 BANK");
        return;
    }
    if (t->eng_req != (uint8_t)ENG_SLOT_FM6) {
        set_engine_of(t, ENG_SLOT_FM6);
        if (t->eng_req != (uint8_t)ENG_SLOT_FM6)
            return;                                  /* (refused: memory short, the drop is proposed: model.c) */
    }
    fm1_irq_off();
    FAST_CALL(fm6, fm6_unpack)(fm6_ed[p], v);
    fm6_cur[p] = (int16_t)(t->p[P_E0] + 1);          /* (its VOICE's, no reload over it) */
    fm1_irq_on();
    snd_mark(p);
    ui_say("LOADED ", sdm.bvn[k]);
}
#endif

/* ---- the lists (op_sdcard.c): one folder view at a time, SDV_* its view */
static const uint8_t SDM_FOLDER[SDC_N] = {SDL_PROJECTS, SDL_PRESETS, SDL_KITS, SDL_SAMPLES};
#if SDM_DX7
static uint32_t sdm_page(void) { return sdm.bank ? sdm.bank_page : sdm.b.page; }
static uint32_t sdm_pages(void) { return sdm.bank ? 2u : sdm.b.pages ? sdm.b.pages : 1u; }
static uint32_t sdm_count(void) { return sdm.bank ? SDB_KEYS : sdm.b.n; }
#else
static uint32_t sdm_page(void) { return sdm.b.page; }
static uint32_t sdm_pages(void) { return sdm.b.pages ? sdm.b.pages : 1u; }
static uint32_t sdm_count(void) { return sdm.b.n; }
#endif
/* ---- banks (the user, 2026-11-04): presets and projects in banks of 16, a bank a folder named by a letter (A .. Z)
 * inside its holder, the files in it numbered "01 - name" .. "16 - name" (the number the slot, the key it is on):
 *   Optimist/Presets/Factory/<Engine>/<L>/<nn> - <name>.ops   the zip's (tools/sd_factory.c: 16 a bank, the table's order)
 *   Optimist/Presets/User/<Engine>/<L>/<nn> - <name>.ops      the presets saved on the FM-1
 *   Optimist/Projects/<L>/<nn> - <name>/<nn> - <name>.opp     a project (sd_save.c: "L/nn - name"), Versions/ in it
 * A bank folder is shown as a bank (sd_browse.c SDB_BANK: slot nn - 1 the file numbered nn, free numbers empty
 * slots); the black keys pick banks A .. K (op_sdcard.c), OCT steps one bank either way (to Z) */
#define SDM_BANKS 26u
static uint8_t sdm_bk[SDC_N];                        /* the bank each list shows (0 = A) */
static char sdm_work[SDL_NAME_MAX + 1u];             /* the open project's place under Projects/ ("A/03 - song"), "" none */
static uint8_t sdm_save_k = 0xFFu;                   /* the slot a SAVE was asked on (op_sdcard.c sdm_save_at) */

/* the holder (Optimist/Projects, Optimist/Presets/User/<Engine of the selected part>) into out: its length, 0 */
static uint32_t sdm_holder(uint32_t list, char *out, uint32_t cap)
{
    uint32_t n;
    if (list == SDC_PROJ)
        return sdl_path(out, cap, SDL_PROJECTS, 0);
    if (list == SDC_KIT)
        return sdl_path(out, cap, SDL_KITS, SDL_USER);   /* (Kits/User: the kits saved on the FM-1) */
    n = sdl_path(out, cap, SDL_PRESETS, SDL_USER "/");
    n = n ? sdl_cat(out, n, cap, sdn_engine(eng_uid(trk[song.sel % NTRK].eng_req % NENGINES))) : cap;
    return n < cap ? n : 0u;
}
/* path a holder: Optimist/Projects, Optimist/Kits/Factory|User, or Optimist/Presets/Factory|User/<one folder> */
static int sdm_is_holder(const char *p, uint32_t len)
{
    char h[40];
    uint32_t n, i, w;
    for (w = 0; w < 3u; w++) {
        n = w ? sdl_path(h, sizeof h, SDL_KITS, w == 1u ? SDL_FACTORY : SDL_USER) : sdl_path(h, sizeof h, SDL_PROJECTS, 0);
        if (n && n == len && !sdl_ncmp(p, h, len))
            return 1;
    }
    for (w = 0; w < 2u; w++) {
        n = sdl_path(h, sizeof h, SDL_PRESETS, w ? SDL_USER : SDL_FACTORY);
        if (!n || len <= n + 1u || sdl_ncmp(p, h, n) || p[n] != '/')
            continue;
        for (i = n + 1u; i < len && p[i] != '/'; i++)
            ;
        if (i == len)
            return 1;
    }
    return 0;
}
/* a folder path a bank (<holder>/<L>): L's number (0 = A), else -1 */
static int32_t sdm_bank_letter(const char *p)
{
    const char *b = sdb_base(p);
    uint8_t c = sdl_uc((uint8_t)b[0]);
    if (b == p || b[1] || c < 'A' || c > 'Z' || !sdm_is_holder(p, (uint32_t)(b - p) - 1u))
        return -1;
    return (int32_t)(c - 'A');
}
/* a project's place "A/03 - song": its bank's number (0 = A), -1 none */
static int32_t sdm_bank_letter_rel(const char *rel)
{
    uint8_t c = sdl_uc((uint8_t)rel[0]);
    return c >= 'A' && c <= 'Z' && rel[1] == '/' ? (int32_t)(c - 'A') : -1;
}
/* folder entry name in the folder root: a bank's folder (a single letter inside a holder) */
static int sdm_is_bank_dir(const char *root, const char *name)
{
    uint8_t c = sdl_uc((uint8_t)name[0]);
    return c >= 'A' && c <= 'Z' && !name[1] && sdm_is_holder(root, sdl_len(root));
}
/* a slot's name without its number and extension ("03 - bass 1.ops": "bass 1"), into out */
static void sdm_strip(char *out, uint32_t cap, const char *name, const char *ext)
{
    const char *p = name + (sdb_bank_no(name) ? 5u : 0u);
    uint32_t n = ext[0] && sdl_ends(p, ext) ? sdl_ends(p, ext) : sdl_len(p), k;
    if (ext[0] == '.' && ext[2] == 'p' && ext[3] == 'k' && n > 3u && p[n - 1u] == ')')   /* a kit's " (909, sampled)": */
        for (k = n - 1u; k-- > 0u;)                  /* its name is the part before (drum_kits.c ukit_card_instr) */
            if (p[k] == '(' && k > 0u && p[k - 1u] == ' ') {
                n = k - 1u;
                break;
            }
    str_cpy(out, p, n + 1u < cap ? n + 1u : cap);    /* (the extension off first, then cut to cap) */
}
/* a kit saved over one of its name (the same number) whose instruments changed: the old file (another " (...)") goes */
static void sdm_kit_drop_old(const char *dir, uint32_t nn, const char *keep)
{
    DIR d;
    FILINFO fi;
    static char p[FF_LFN_BUF + 1];
    uint32_t n;
    if (f_opendir(&d, dir) != FR_OK)
        return;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (sdl_hidden(&fi) || sdb_bank_no(fi.fname) != nn || !sdl_ends(fi.fname, ".opk") || !sdl_cmp(fi.fname, keep))
            continue;
        n = sdl_cat(p, sdl_cat(p, 0, sizeof p, dir), sizeof p, "/");
        if (sdl_cat(p, n, sizeof p, fi.fname) < sizeof p)
            (void)f_unlink(p);
    }
    (void)f_closedir(&d);
}
/* the id of bank folder dir's file numbered nn (ext's), 0 none: a preset or kit saved over keeps it */
static uint64_t sdm_same_id(const char *dir, uint32_t nn, const char *ext)
{
    DIR d;
    FILINFO fi;
    static char p[FF_LFN_BUF + 1];
    uint64_t id = 0;
    uint32_t n;
    if (f_opendir(&d, dir) != FR_OK)
        return 0;
    while (!id && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (sdl_hidden(&fi) || sdb_bank_no(fi.fname) != nn || !sdl_ends(fi.fname, ext))
            continue;
        n = sdl_cat(p, sdl_cat(p, 0, sizeof p, dir), sizeof p, "/");
        if (sdl_cat(p, n, sizeof p, fi.fname) < sizeof p)
            id = sdm_small_id(p);
    }
    (void)f_closedir(&d);
    return id;
}
/* bank folder dir: the numbers in use (bit nn - 1) and the number of the file named name (without its number and
 * ext, any case), 0 none */
static uint32_t sdm_bank_scan(const char *dir, const char *name, const char *ext, uint32_t *used)
{
    DIR d;
    FILINFO fi;
    char b[SDL_NAME_MAX + 1u];
    uint32_t nn, same = 0;
    *used = 0;
    if (f_opendir(&d, dir) != FR_OK)
        return 0;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (sdl_hidden(&fi) || !(nn = sdb_bank_no(fi.fname)))
            continue;
        *used |= 1u << (nn - 1u);
        sdm_strip(b, sizeof b, fi.fname, ext);
        if (!same && name && !sdl_cmp(b, name))
            same = nn;
    }
    (void)f_closedir(&d);
    return same;
}
static uint32_t sdm_first_free(uint32_t used)        /* the lowest free number of a bank, 0 none */
{
    uint32_t nn;
    for (nn = 1; nn <= SDB_KEYS; nn++)
        if (!(used >> (nn - 1u) & 1u))
            return nn;
    return 0;
}
/* "<nn> - " into out at at: the new length */
static uint32_t sdm_num(char *out, uint32_t at, uint32_t cap, uint32_t nn)
{
    char t[6] = {(char)('0' + nn / 10u), (char)('0' + nn % 10u), ' ', '-', ' ', 0};
    return sdl_cat(out, at, cap, t);
}
/* list's bank bk shown (its holder's folder bk): FR_OK (a bank not made yet: empty) */
static FRESULT sdm_bank_open(uint32_t list, uint32_t bk)
{
    char l[2] = {(char)('A' + bk % SDM_BANKS), 0};
    uint32_t n = sdm_holder(list, sdm.path, sizeof sdm.path);
    if (!n || sdl_cat(sdm.path, sdl_cat(sdm.path, n, sizeof sdm.path, "/"), sizeof sdm.path, l) >= sizeof sdm.path)
        return FR_INVALID_NAME;
    sdm_bk[list % SDC_N] = (uint8_t)(bk % SDM_BANKS);
    return sdb_open(&sdm.b, SDB_BANK, sdm.path);
}
/* the bank shown: its number (0 = A), -1 none */
static int32_t sdm_bank_shown(void) { return sdm.b.view == SDB_BANK ? sdm_bank_letter(sdm.b.root) : -1; }

static int sdm_open(uint32_t list, uint32_t view, const char *vname)
{
    uint32_t n;
    FRESULT r;
#if SDM_DX7
    if (sdm.bank && list == sdm.list && view == sdm.view && sdm_ready())
        return sdm_dx7_page();                       /* (a bank shown: read again, still shown) */
    sdm.bank = 0;
#endif
    sdm.list = (uint8_t)list;
    sdm.view = (uint8_t)view;
    sdm.b.n = 0;
    sdm.b.page = 0;
    sdm.b.pages = 1;
    if (!sdm_ready())
        return -1;
    if (view == SDV_FOLDER && list != SDC_SMP) {   /* a bank of 16: the list's last one shown */
        r = sdm_was(sdm_bank_open(list, sdm_bk[list]));
        if (r != FR_OK)
            sdm.b.n = 0;
        return r == FR_OK ? 0 : -1;
    }
    if (view == SDV_VERSIONS) {
        sdl_cat(sdm.vname, 0, sizeof sdm.vname, vname);
        n = sdp_path(sdm.path, sdm.vname, SDP_VDIR, 0, 0);
    } else {
        n = sdl_path(sdm.path, sizeof sdm.path, SDM_FOLDER[list % SDC_N], 0);
    }
    if (!n)
        return -1;
    r = sdm_was(sdb_open(&sdm.b, view == SDV_NAME ? SDB_BYNAME : view == SDV_RECENT ? SDB_RECENT : SDB_FOLDER, sdm.path));
    if (r == FR_NO_PATH)
        r = FR_OK;                                   /* (no VERSIONS yet: an empty list) */
    if (r != FR_OK)
        sdm.b.n = 0;
    return r == FR_OK ? 0 : -1;
}
static void sdm_turn(int32_t step)
{
#if SDM_DX7
    if (sdm.bank) {
        sdm.bank_page = step > 0 ? 1u : step < 0 ? 0u : sdm.bank_page;
        (void)sdm_dx7_page();
        return;
    }
#endif
    if (sdm_ready())
        (void)sdm_was(sdb_turn(&sdm.b, step));
}
static int sdm_into(uint32_t k)
{
#if SDM_DX7
    if (sdm.list == SDC_PRE && !sdm.bank && k < sdm.b.n && !sdm.b.slot[k].dir) {   /* (a .SYX: its voices) */
        if (sdm_ready() && !sdm_dx7_open(k))
            return 1;
        sdm.bank = 0;
#if FELUCCA_ENG_CZ
        if (sdm_ready() && !sdm_cz_load(k))
            return 0;                                /* (a CZ tone: on the part, the list as it was) */
#endif
        ui_message("NOT A DX7 BANK OR CZ TONE");
        return 0;
    }
#endif
    if (!sdm_ready() || sdm_was(sdb_into(&sdm.b, k)) != FR_OK)
        return 0;
    if (sdm_bank_letter(sdm.b.root) >= 0) {          /* a bank's folder: as a bank (its 16 numbered slots) */
        sdm_bk[sdm.list % SDC_N] = (uint8_t)sdm_bank_letter(sdm.b.root);
        sdl_cat(sdm.path, 0, sizeof sdm.path, sdm.b.root);
        return sdm_was(sdb_open(&sdm.b, SDB_BANK, sdm.path)) == FR_OK;
    }
    return 1;
}
/* bank bk (0 = A) of the holder shown (the bank's, or the folder's when it is a holder; else the list's own: Projects,
 * Presets/User/<Engine>): 1 shown */
static int sdm_bank_go(uint32_t bk)
{
    uint32_t n;
    char l[2] = {(char)('A' + bk % SDM_BANKS), 0};
    if (!sdm_ready() || bk >= SDM_BANKS || sdm.view == SDV_VERSIONS || sdm.list == SDC_SMP)
        return 0;
#if SDM_DX7
    sdm.bank = 0;
#endif
    n = sdl_cat(sdm.path, 0, sizeof sdm.path, sdm.b.root);
    if (sdm_bank_letter(sdm.path) >= 0)
        n = (uint32_t)(sdb_base(sdm.path) - sdm.path) - 1u;   /* (a bank shown: its holder) */
    else if (!SDB_DIRLIKE(sdm.b.view) || !sdm_is_holder(sdm.path, n))
        n = sdm_holder(sdm.list, sdm.path, sizeof sdm.path);
    if (!n || n >= sizeof sdm.path)
        return 0;
    sdm.path[n] = 0;
    if (sdl_cat(sdm.path, sdl_cat(sdm.path, n, sizeof sdm.path, "/"), sizeof sdm.path, l) >= sizeof sdm.path)
        return 0;
    sdm_bk[sdm.list % SDC_N] = (uint8_t)bk;
    return sdm_was(sdb_open(&sdm.b, SDB_BANK, sdm.path)) == FR_OK;
}
static int sdm_up(void)                              /* up a folder; 0 at the list's own folder */
{
    uint32_t n = sdl_path(sdm.path, sizeof sdm.path, SDM_FOLDER[sdm.list % SDC_N], 0);
#if SDM_DX7
    if (sdm.bank) {                                  /* out of a bank: its folder as it was */
        sdm.bank = 0;
        return 1;
    }
#endif
    FRESULT r = FR_NO_PATH;
    if (!sdm_ready() || !SDB_DIRLIKE(sdm.b.view) || !n || !sdl_cmp(sdm.b.root, sdm.path))
        return 0;
    while (r == FR_NO_PATH && sdl_cmp(sdm.b.root, sdm.path))   /* (a bank shown before its folders were made, an
                                                                 * engine's User/<Engine>/A: up past what is not there) */
        r = sdb_up(&sdm.b);
    return sdm_was(r) == FR_OK;
}
static int sdm_at_top(void)
{
    uint32_t n = sdl_path(sdm.path, sizeof sdm.path, SDM_FOLDER[sdm.list % SDC_N], 0);
#if SDM_DX7
    if (sdm.bank)
        return 0;
#endif
    return !SDB_DIRLIKE(sdm.b.view) || sdm.view == SDV_VERSIONS || !n || !sdl_cmp(sdm.b.root, sdm.path);
}
static void sdm_folder(char *b, uint32_t cap)        /* the folder shown: its last part ("SAMPLES", "DRUMS"); VERSIONS:
                                                      * whose; a bank its holder's and its letter ("ACID B"); upper case
                                                      * (the card's "Samples": SAMPLES) */
{
    char t[SDL_NAME_MAX + 1u];
#if SDM_DX7
    if (sdm.bank) {
        str_cpy(b, sdb_base(sdm.bpath), cap);       /* (cut to cap, always ended: sdl_cat leaves a long one open) */
        sdl_upper(b);
        return;
    }
#endif
    if (sdm.view == SDV_VERSIONS) {
        sdm_strip(b, cap, sdb_base(sdm.vname), "");
    } else if (sdm_bank_shown() >= 0) {
        uint32_t n = (uint32_t)(sdb_base(sdm.b.root) - sdm.b.root) - 1u;
        str_cpy(t, sdm.b.root, n + 1u < sizeof t ? n + 1u : sizeof t);   /* (the holder: "Optimist/Projects") */
        str_cpy(b, sdb_base(t), cap > 2u ? cap - 2u : 1u);
        str_app(b, " ", cap);
        str_app(b, sdb_base(sdm.b.root), cap);
    } else {
        str_cpy(b, sdb_base(sdm.b.root), cap);
    }
    sdl_upper(b);
}
/* s ends in ext (".ops"), any case: s's length without it, else 0 */
static uint32_t sdm_len_is(const char *s, const char *ext) { return sdl_ends(s, ext); }
#if SDM_DX7
static uint32_t sdm_syx(const char *s)               /* s ends in .SYX, any case: its length without it, else 0 */
{
    uint32_t n = sdl_len(s), i;
    if (n < 5u || s[n - 4u] != '.')
        return 0;
    for (i = 1; i < 4u; i++)
        if ((s[n - 4u + i] & ~0x20) != "SYX"[i - 1u])
            return 0;
    return n - 4u;
}
#endif
/* slot k as shown: our own files without their extension, a version as "VERSION 7"; 0: no slot k */
static const char *sdm_slot(uint32_t k, uint32_t *dir, uint32_t *cut)
{
    static const char *const EXT[SDC_N] = {".opp", ".ops", ".opk", ""};
    const sdb_item *it = &sdm.b.slot[k % SDB_KEYS];
    uint32_t n, a;
#if SDM_DX7
    if (sdm.bank) {                                  /* a bank's voice: its name */
        *dir = *cut = 0;
        return k < SDB_KEYS ? sdm.bvn[k] : 0;
    }
#endif
    if (k >= sdm.b.n || (sdm.b.view == SDB_BANK && !it->name[0]))
        return 0;                                    /* (no slot k, or a bank's free number) */
    *dir = it->dir;
    *cut = it->cut;
    if (it->dir && sdm.b.view == SDB_FOLDER && sdm_is_bank_dir(sdm.b.root, it->name))
        *dir = 2;                                    /* (a bank's folder: gone into, in the projects' list too) */
    if (sdm.b.view == SDB_BANK) {                    /* "03 - bass 1.ops": BASS 1 on slot 3 */
        sdm_strip(sdm.shown, sizeof sdm.shown, it->name, it->dir ? "" : EXT[sdm.list % SDC_N]);
        return sdl_upper(sdm.shown);
    }
#if SDM_DX7
    if (sdm.list == SDC_PRE && !it->dir && !it->cut && (n = sdm_syx(it->name)) != 0) {
        *dir = 1;                                    /* (a DX7 bank: a folder of its voices) */
        sdl_cat(sdm.shown, 0, sizeof sdm.shown, it->name);
        sdm.shown[n] = 0;
        return sdl_upper(sdm.shown);
    }
#endif
    sdl_cat(sdm.shown, 0, sizeof sdm.shown, it->name);
    if (sdm.view == SDV_VERSIONS) {                  /* "<name>-0007.opp": "VERSION 7" */
        a = sdl_len(sdb_base(sdm.vname)) + 1u;       /* (vname "A/03 - song": its files "03 - song-0007.opp") */
        if (!it->cut && (n = sdm_len_is(it->name, ".opp")) > a) {
            while (a + 1u < n && it->name[a] == '0')
                a++;
            sdl_cat(sdm.shown, sdl_cat(sdm.shown, 0, sizeof sdm.shown, "VERSION "), sizeof sdm.shown, it->name + a);
            sdm.shown[8u + n - a] = 0;
        }
    } else if (!it->dir && !it->cut && (n = sdm_len_is(it->name, EXT[sdm.list % SDC_N])) != 0) {
        sdm.shown[n] = 0;
    }
    return sdl_upper(sdm.shown);                     /* (the card's lowercase names: the screen's upper case) */
}
/* slot k's name (from the card: never cut; a bank's without its number), upper case as the FM-1 shows and types names
 * (the NAME page's prefill), "" none */
static const char *sdm_base_of(uint32_t k)
{
    if (!sdb_path(&sdm.b, k, sdm.path, sizeof sdm.path))
        return "";
    sdm_strip(sdm.shown, sizeof sdm.shown, sdb_base(sdm.path), sdm.list == SDC_PROJ ? "" : sdm.list == SDC_PRE ? ".ops" : ".opk");
    return sdl_upper(sdm.shown);
}
/* a project's place under Projects/ from its folder's path ("Optimist/Projects/A/03 - song": "A/03 - song"): a
 * pointer into path */
static const char *sdm_proj_rel(const char *path)
{
    uint32_t n = sizeof SDL_ROOT "/Projects/" - 1u;
    return sdl_len(path) > n && !sdl_ncmp(path, SDL_ROOT "/Projects/", n) ? path + n : sdb_base(path);
}
/* slot k's project place under Projects/ (as on the card), "" none (VERSIONS, SAVE before LOAD, a later LOAD) */
static const char *sdm_rel_of(uint32_t k)
{
    return sdb_path(&sdm.b, k, sdm.path, sizeof sdm.path) ? sdm_proj_rel(sdm.path) : "";
}
/* a project place's name as the FM-1 shows it ("A/03 - song": SONG) into out (cap bytes) */
static void sdm_rel_name(char *out, uint32_t cap, const char *rel)
{
    sdm_strip(out, cap, sdb_base(rel), "");
    sdl_upper(out);
}
/* the slot NAME was opened from (a SAVE on slot k: its number when free or the same name's) */
static void sdm_save_at(uint32_t k) { sdm_save_k = (uint8_t)(k < SDB_KEYS ? k : 0xFFu); }
static void sdm_work_of(char *out, uint32_t cap)    /* the open project's place, else its flash name (VERSIONS of the work) */
{
    if (sdm_work[0])
        str_cpy(out, sdm_work, cap);
    else
        sec_name(0, out);
}
/* where a SAVE named name of the projects', the presets' or the kits' list goes (the user, 2026-11-04: a free number in
 * the bank browsed): the bank's folder (made when missing) into dir, the number into *nn: the bank's file of that name
 * (*same 1), else the slot SAVE was asked on when it is free, else the bank's lowest free number. The bank: a project's
 * own (the work's, saved again by its name), else the bank shown, else the list's last; a preset's in its engine's
 * User folder (Presets/User/<Engine>/<the letter shown>), a kit's in Kits/User/<the letter shown>. 0 ok, 1 the bank
 * full, -1 an error */
static int sdm_save_place(uint32_t list, const char *name, char *dir, uint32_t cap, uint32_t *nn, int *same)
{
    const char *ext = list == SDC_PRE ? ".ops" : list == SDC_KIT ? ".opk" : "";
    char wn[SDL_NAME_MAX + 1u];
    uint32_t n, used, shown;
    int32_t bk = sdm_bank_shown();
    *same = 0;
    if (list == SDC_PROJ && sdm_work[0] && sdb_base(sdm_work) != sdm_work) {
        sdm_rel_name(wn, sizeof wn, sdm_work);       /* (the open project saved again by its name: its own place) */
        if (!sdl_cmp(wn, name) && (bk = sdm_bank_letter_rel(sdm_work)) >= 0 && sdb_bank_no(sdb_base(sdm_work))) {
            *nn = sdb_bank_no(sdb_base(sdm_work));
            *same = 1;
            n = sdm_holder(list, dir, cap);
            return n && sdl_cat(dir, sdl_cat(dir, n, cap, "/"), cap, (char[2]){(char)('A' + bk), 0}) < cap ? 0 : -1;
        }
        bk = sdm_bank_shown();
    }
    if (bk < 0 || sdm.list != list)
        bk = (int32_t)sdm_bk[list % SDC_N];
    if (!(n = sdm_holder(list, dir, cap)) || sdl_mkdir(dir) != FR_OK)   /* (Presets/User/<Engine>: made) */
        return -1;
    n = sdl_cat(dir, sdl_cat(dir, n, cap, "/"), cap, (char[2]){(char)('A' + bk % (int32_t)SDM_BANKS), 0});
    if (n >= cap || sdl_mkdir(dir) != FR_OK)
        return -1;
    shown = !sdl_cmp(sdm.b.root, dir) && sdm.b.view == SDB_BANK;
    if ((*nn = sdm_bank_scan(dir, name, ext, &used)) != 0)
        *same = 1;
    else if (shown && sdm_save_k < SDB_KEYS && !(used >> sdm_save_k & 1u))
        *nn = sdm_save_k + 1u;
    else if (!(*nn = sdm_first_free(used)))
        return 1;
    return 0;
}

/* ---- presets and kits: one small file (header + payload, a sector at most: written whole); version 2 carries the
 * file's id (sd_ids.h: a preset's or kit's, found again by it when moved; 0 the device's other files) */
typedef struct {
    char magic[4];                                   /* "OPPR" a preset (upreset.c up_rec_t), "OPKT" a kit (ukit_t + sends) */
    uint16_t ver, len;                               /* 2, the payload's bytes */
    uint64_t hash;                                   /* XXH64 of the payload */
    uint64_t id;                                     /* the preset's or kit's id (sd_ids.h), 0 none */
} sdm_fh;
#define SDM_FH_N 24u
_Static_assert(sizeof(sdm_fh) == SDM_FH_N, "sd_mode.c: the small files' header");
static uint64_t sdm_hash(const uint8_t *p, uint32_t n) { sdh_t s; sdh_init(&s); sdh_add(&s, p, n); return sdh_end(&s); }
static FRESULT sdm_small_put_id(const char *path, const char *magic, const uint8_t *p, uint32_t n, uint64_t id)
{
    sdm_fh h;
    UINT k;
    FRESULT r = f_open(&sdm.f, path, FA_READ | FA_WRITE);
    memset(&h, 0, sizeof h);
    memcpy(h.magic, magic, 4);
    h.ver = 2;
    h.len = (uint16_t)n;
    h.hash = sdm_hash(p, n);
    h.id = id;
    if (r == FR_NO_FILE)
        r = sda_create(&sdm.f, path, SDA_EXACT, sizeof h + n, 0);
    if (r != FR_OK)
        return r;
    r = f_write(&sdm.f, &h, sizeof h, &k);
    if (r == FR_OK)
        r = f_write(&sdm.f, p, n, &k);
    if (r == FR_OK)
        r = f_truncate(&sdm.f);                      /* (saved over a longer one: its old tail gone) */
    if (f_close(&sdm.f) != FR_OK && r == FR_OK)
        r = FR_DISK_ERR;
    return r;
}
static FRESULT sdm_small_put(const char *path, const char *magic, const uint8_t *p, uint32_t n)
{
    return sdm_small_put_id(path, magic, p, n, 0);
}
/* path's payload (magic's) into p: its bytes, 0 none; its id into *id (id 0: not wanted) */
static uint32_t sdm_small_get_id(const char *path, const char *magic, uint8_t *p, uint32_t cap, uint64_t *id)
{
    sdm_fh h;
    UINT k = 0;
    uint32_t n = 0;
    if (f_open(&sdm.f, path, FA_READ) != FR_OK)
        return 0;
    if (f_read(&sdm.f, &h, sizeof h, &k) == FR_OK && k == sizeof h && !memcmp(h.magic, magic, 4) && h.ver == 2u &&
        h.len <= cap && f_read(&sdm.f, p, h.len, &k) == FR_OK && k == h.len && sdm_hash(p, h.len) == h.hash)
        n = h.len;
    (void)f_close(&sdm.f);
    if (id)
        *id = n ? h.id : 0u;
    return n;
}
static uint32_t sdm_small_get(const char *path, const char *magic, uint8_t *p, uint32_t cap)   /* its payload's bytes, 0 none */
{
    return sdm_small_get_id(path, magic, p, cap, 0);
}
/* a new user id (sd_ids.h): the FM-1's flash ID's hash, the device record's count, the milliseconds */
static uint64_t sdm_new_id(void)
{
    uint64_t dev = 0;
    uint32_t c = (uint32_t)sdm_rec.ids[0] | (uint32_t)sdm_rec.ids[1] << 8;
#ifdef DEVNAME_UID
    if (dev_uid_ok)
        dev = sdm_hash(dev_uid, sizeof dev_uid);
#endif
    c = (c + 1u) & 0xFFFFu;
    sdm_rec.ids[0] = (uint8_t)c;
    sdm_rec.ids[1] = (uint8_t)(c >> 8);
    sdm_dirty = sdm_dirty ? sdm_dirty : 1u;          /* (the count kept: the device record written, quiet) */
    return SDI_USER | (dev & 0x7FFFFFFF00000000ull) | (uint64_t)c << 16 | (fm1_ms & 0xFFFFu);
}
/* the id in path's header (a .ops / .opk), 0 none: the header alone read */
static uint64_t sdm_small_id(const char *path)
{
    sdm_fh h;
    UINT k = 0;
    uint64_t id = 0;
    if (f_open(&sdm.f, path, FA_READ) != FR_OK)
        return 0;
    if (f_read(&sdm.f, &h, sizeof h, &k) == FR_OK && k == sizeof h && h.ver == 2u)
        id = h.id;
    (void)f_close(&sdm.f);
    return id;
}


#if PRESETS_CARD
/* ---- the factory presets on the card (PRESETS_CARD; the user, 2026-10-26: "all presets (user or factory) go on the
 * card", "card only in SD mode"): Optimist/Presets/Factory/<Engine>/<L>/<nn> - <name>.ops (banks of 16 in the
 * table's order: preset 18 is B/03), as tools/sd_factory.c writes them
 * (the SD card zip, tools/sd_zip.py). The firmware carries no preset tables (core.h PRESETS_PTR), only their counts.
 *   sdm_factory_step    at a mount, an engine a call: each built engine's folder read once, the names into sdm_fac_nm (the lists, the
 *                       favorites, the web editor: model.c preset_name -> sdm_factory_name; "?" a file missing)
 *   sdm_factory_ask     (model.c apply_preset_to: maybe the interrupts off) part k's engine defaults are set; its
 *                       preset file is loaded on the main loop's next pass (sdm_factory_poll), as a user preset loads
 *                       (upreset.c up_card_give: the record, an FM6 voice), the preset's place kept. A project's load
 *                       or a user preset over it drops the ask (sdm_factory_drop). No card: the defaults, said once */
#define SDM_FAC_N 448u                               /* (model.c NBANK_ALL: every engine's presets on the card) */
#define SDM_FAC_NM 13u
static char sdm_fac_nm[SDM_FAC_N][SDM_FAC_NM] IN_POOL;
static uint8_t sdm_fac_pend[NTRK], sdm_fac_eng[NTRK], sdm_fac_said;
static uint8_t sdm_fac_n[NENGINES];                  /* each engine's presets on the card (the last numbered one's place
                                                      * + 1), 0 not read: its table's count (sdm_factory_count) */
/* engine slot e's factory presets (core.h ENG_NPRE): the card's when it was read, never fewer than its table's (the
 * files beyond: FM6's pack voices, numbered on after its own presets; the user, 2026-11-04) */
static uint32_t sdm_factory_count(uint32_t e)
{
    e %= NENGINES;
    return sdm_fac_n[e] > ENGINES[e]->npresets ? sdm_fac_n[e] : ENGINES[e]->npresets;
}
static uint32_t sdm_fac_ix(uint32_t e, uint32_t k)   /* engine slot e's preset k in the table of names */
{
    uint32_t i, n = 0;
    for (i = 0; i < e && i < NENGINES; i++)
        n += sdm_factory_count(i);
    return n + k;
}
static const char *sdm_factory_name(uint32_t e, uint32_t k)
{
    uint32_t i = sdm_fac_ix(e, k);
    return i < SDM_FAC_N && sdm_fac_nm[i][0] ? sdm_fac_nm[i] : "?";
}
/* engine slot e's bank bk folder into sdm.path (Presets/Factory/<Engine>/<L>: sd_names.h, the zip's 16 a bank): its
 * length, 0 too long */
static uint32_t sdm_fac_dir(uint32_t e, uint32_t bk)
{
    char l[3] = {'/', (char)('A' + bk % SDM_BANKS), 0};
    uint32_t n = sdl_path(sdm.path, sizeof sdm.path, SDL_PRESETS, SDL_FACTORY "/");
    n = n ? sdl_cat(sdm.path, n, sizeof sdm.path, sdn_engine(eng_uid(e))) : sizeof sdm.path;
    n = n < sizeof sdm.path ? sdl_cat(sdm.path, n, sizeof sdm.path, l) : n;
    return n < sizeof sdm.path ? n : 0u;
}
/* engine slot e's banks read (A, B ... up to the first not there): names 0, else each file's name into its place of
 * the table of names; -> the last numbered file's place + 1 */
static uint32_t sdm_fac_pass(uint32_t e, int names)
{
    DIR d;
    FILINFO fi;
    uint32_t k, i, nn, bk, n = 0;
    for (bk = 0; bk < SDM_BANKS && sdm_fac_dir(e, bk) && f_opendir(&d, sdm.path) == FR_OK; bk++) {
        while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
            if (!(nn = sdb_bank_no(fi.fname)) || !sdm_len_is(fi.fname, ".ops"))
                continue;
            k = bk * SDB_KEYS + nn - 1u;
            if (k >= 255u)
                continue;                            /* (a part's preset is a byte, 0xFF INIT) */
            n = k + 1u > n ? k + 1u : n;
            if (names && (i = sdm_fac_ix(e, k)) < SDM_FAC_N) {
                sdm_strip(sdm_fac_nm[i], SDM_FAC_NM, fi.fname, ".ops");   /* ("01 - tb acid.ops": TB ACID, */
                sdl_upper(sdm_fac_nm[i]);                                 /* the tables' name) */
            }
        }
        (void)f_closedir(&d);
    }
    return n;
}
/* at a mount, a call a main loop pass: every built engine's count, then the names (their places depend on every count
 * before them); the lists made again at the end (model.c bank_ready): 1 done */
static int sdm_factory_step(void)
{
    static uint32_t at;
    if (!at)
        memset(sdm_fac_nm, 0, sizeof sdm_fac_nm);
    if (at < NENGINES)
        sdm_fac_n[at] = (uint8_t)sdm_fac_pass(at, 0);
    else
        (void)sdm_fac_pass(at - NENGINES, 1);
    if (++at < 2u * NENGINES)
        return 0;
    at = 0;
    bank_ready = 0;
    return 1;
}
/* engine slot e's preset pi: its file's path into sdm.path (the bank's file numbered pi % 16 + 1, whatever its name):
 * its length, 0 none */
static uint32_t sdm_fac_file(uint32_t e, uint32_t pi)
{
    DIR d;
    FILINFO fi;
    uint32_t n = sdm_fac_dir(e, pi / SDB_KEYS), m = 0;
    if (!n || f_opendir(&d, sdm.path) != FR_OK)
        return 0;
    while (!m && f_readdir(&d, &fi) == FR_OK && fi.fname[0])
        if (sdb_bank_no(fi.fname) == pi % SDB_KEYS + 1u && sdm_len_is(fi.fname, ".ops"))
            m = sdl_cat(sdm.path, sdl_cat(sdm.path, n, sizeof sdm.path, "/"), sizeof sdm.path, fi.fname);
    (void)f_closedir(&d);
    return m < sizeof sdm.path ? m : 0u;
}
static void sdm_factory_ask(track_t *t, uint32_t pi)
{
    uint32_t k = (uint32_t)(t - trk);
    if (k < NTRK) {
        sdm_fac_pend[k] = (uint8_t)(pi + 1u);
        sdm_fac_eng[k] = t->eng_req;
    }
}
static void sdm_factory_drop(uint32_t k)
{
    if (k < NTRK)
        sdm_fac_pend[k] = 0;
}
static void sdm_factory_poll(void)
{
    uint32_t k, n, pi, sel;
    for (k = 0; k < NTRK; k++) {
        if (!sdm_fac_pend[k])
            continue;
        pi = sdm_fac_pend[k] - 1u;
        if (trk[k].eng_req != sdm_fac_eng[k]) {      /* (another engine since: its own ask, or none) */
            sdm_fac_pend[k] = 0;
            continue;
        }
        if (sdm_st < SDM_READY) {                    /* (no card, or not read yet: the defaults; said once) */
            if (sdm_st == SDM_NONE) {
                sdm_fac_pend[k] = 0;
                if (!sdm_fac_said++)
                    ui_message("NO SD CARD: PRESETS INIT");
            }
            return;
        }
        sdm_fac_pend[k] = 0;
        n = sdm_fac_file(trk[k].eng_req % NENGINES, pi);   /* "<Engine>/<L>/<nn> - <name>.ops" */
        n = n ? sdm_small_get(sdm.path, "OPPR", sd_blk, sizeof sd_blk) : 0u;
        if (!n) {
            ui_message("PRESET NOT ON THE CARD");
            continue;
        }
        sel = song.sel;
        song.sel = (uint8_t)k;                       /* (up_card_give loads into the selected part) */
        if (!up_card_give(sd_blk, n))
            trk[k].preset = (uint8_t)pi;             /* (its place in the engine's list, as the table's was) */
        song.sel = (uint8_t)sel;
    }
}
#endif

#if FELUCCA_SD_LANES
/* a kit file's lanes' card samples (the user, 2026-10-26: kits on the card carry them): after the kit's UK_CARD_N bytes,
 * "LCRD" and DRUM_LANES x (hash 8, size 4); a kit of before (none): its lanes as they are */
#define SDM_KL_N (4u + DRUM_LANES * 12u)
_Static_assert(UK_CARD_N + SDM_KL_N <= 512u - SDM_FH_N, "sd_mode.c: a kit with its lanes' samples in one sector");
static uint32_t sdm_kit_lanes_out(uint8_t *b, uint32_t n)
{
    uint32_t l;
    memcpy(b + n, "LCRD", 4);
    for (l = 0; l < DRUM_LANES; l++) {
        uint64_t h = dl.src[l] == DL_CARD ? sdm_smp[NPART + l].hash : 0u;
        memcpy(b + n + 4u + l * 12u, &h, 8);
        memcpy(b + n + 12u + l * 12u, &sdm_smp[NPART + l].size, 4);
    }
    return n + SDM_KL_N;
}
static uint32_t sdm_kit_lanes_in(uint8_t *b, uint32_t n)
{
    uint32_t l;
    if (n != UK_CARD_N + SDM_KL_N || memcmp(b + UK_CARD_N, "LCRD", 4))
        return n;
    for (l = 0; l < DRUM_LANES; l++) {
        uint64_t h;
        memcpy(&h, b + UK_CARD_N + 4u + l * 12u, 8);
        if (!h)
            continue;
        memset(&sdm_smp[NPART + l], 0, sizeof sdm_smp[0]);
        sdm_smp[NPART + l].hash = h;
        memcpy(&sdm_smp[NPART + l].size, b + UK_CARD_N + 12u + l * 12u, 4);
        sdm_smp_dirty = 1;
    }
    sdm_lane_on = 0xFFFFu;
    return UK_CARD_N;
}
#endif

/* ---- the project: the card's file <-> project 0 */
/* the payload's records (len bytes from the open file's pointer) into project 0: 0 ok, 1 MEM FULL, -1 not a project */
static int sdm_feed(uint32_t len)
{
    uint8_t h[3];
    uint32_t at = 0, id, rl;
    UINT got;
    int rc;
    while (at < len) {
        if (f_read(&sdm.f, h, 3, &got) != FR_OK || got != 3u)
            return -1;
        id = h[0], rl = (uint32_t)h[1] | (uint32_t)h[2] << 8;
        if (SLG_UNK(id)) {                           /* a later firmware's id: skipped, the rest loaded (MIDI tracks' phase */
            if (at + 3u + rl > len || f_lseek(&sdm.f, f_tell(&sdm.f) + rl) != FR_OK)   /* 0; before it: the whole */
                return -1;                           /* file refused) */
            at += 3u + rl;
            continue;
        }
        if (SLG_DEV(id) || !rl || rl > SEC_REC_MAX || at + 3u + rl > len ||
            f_read(&sdm.f, sec_rbuf, rl, &got) != FR_OK || got != rl)
            return -1;
        if ((rc = slg_put(id, sec_rbuf, rl, 1)) != 0)
            return rc > 0 ? 1 : -1;
        at += 3u + rl;
    }
    return 0;
}
static void sdm_proj_load_file(const char *path, const char *name)
{
    sdp_head h;
    char nm[SEC_NAME_LEN + 1u];
    int rc;
    if (song.playing || transport_req) {
        ui_message("STOP FIRST");
        return;
    }
    if (sdm_rec.pend && sdm_was(sdp_boot(sd_blk)) != FR_OK) {
        ui_message("CARD SAVE PENDING");           /* (the flash copy is a save's only one: never loaded over) */
        return;
    }
    if (sdm_was(sdp_verify_path(path, &h, sd_blk)) != FR_OK) {   /* (the header and the payload's hash: whole) */
        ui_message("NOT A PROJECT");
        return;
    }
    if (f_open(&sdm.f, path, FA_READ) != FR_OK || f_lseek(&sdm.f, SDP_SECTOR) != FR_OK) {
        (void)f_close(&sdm.f);
        ui_message("LOAD ERROR");
        return;
    }
    rc = project_clear(0) ? -1 : sdm_feed(h.len);  /* (the clear writes what waits in RAM first, then nothing is dirty:
                                                     * the open's flush adds nothing to what the file brought) */
    (void)f_close(&sdm.f);
    sdm.sc.have = 0xFFFFu;
    if (rc) {
        (void)project_clear(0);                     /* (no half project: the work as it was, the card's copy whole) */
        ui_message(rc > 0 ? "MEM FULL: NOT LOADED" : "LOAD ERROR");
        return;
    }
    sdm_rel_name(nm, sizeof nm, name);              /* (its first 12 characters: the flash project's name, upper case
                                                     * as the NAME page types it; on the card "A/03 - song") */
    (void)sec_name_set(0, nm);
    str_cpy(sdm_work, name, sizeof sdm_work);       /* (its place: a SAVE by that name goes back there) */
    prj_load_go(0);
    ui_say("LOADED ", sdm_disp(nm));
}
/* a project saved to the card by name (as typed): into its place (sdm_save_place: Projects/<bank>/<nn> - <name>,
 * lowercase); FR_DENIED the card full, FR_EXIST the bank full (nothing written, said), else FatFs' */
static FRESULT sdm_proj_save(const char *name)
{
    sdp_head h;
    uint32_t number = 1, nn, n;
    int same, rc;
    FRESULT r;
    char lc[SDL_NAME_MAX + 1u], rel[SDL_NAME_MAX + 8u];
    str_cpy(lc, name, sizeof lc);
    sdl_lower(lc);
    rc = sdm_save_place(SDC_PROJ, name, sdm.path, sizeof sdm.path, &nn, &same);
    if (rc) {
        ui_message(rc > 0 ? "BANK FULL" : "CARD SAVE FAILED");
        return rc > 0 ? FR_EXIST : FR_INT_ERR;
    }
    n = sdl_cat(rel, 0, sizeof rel, sdb_base(sdm.path));   /* "A/03 - song" */
    n = sdm_num(rel, sdl_cat(rel, n, sizeof rel, "/"), sizeof rel, nn);
    if (sdl_cat(rel, n, sizeof rel, lc) >= sizeof rel || sdl_len(rel) > SDL_NAME_MAX) {
        ui_message("NAME NOT USABLE");
        return FR_INVALID_NAME;
    }
    if (prj_save_go(0))                              /* the flash copy first (the staged one): MEM FULL, an error said */
        return FR_INT_ERR;
    (void)sec_name_set(0, name);                     /* (the flash project's name as typed: upper case) */
    persist_flush_now();                            /* (whatever waits in RAM for the log: the copy whole) */
    if (same && sdp_pick(rel, sdm.path, sizeof sdm.path, &h, sd_blk) == FR_OK)
        number = h.number + 1u;                     /* (the save counter: the card's copy's + 1) */
    r = sdm_was(sdp_save(rel, number, sdm_stage_len(), sd_blk));
    str_cpy(sdm_work, rel, sizeof sdm_work);        /* (a pending one too: sdp_boot finishes it there) */
    ui.toast_next = 1;
    ui_message(r == FR_OK ? "SAVED TO CARD" : r == FR_DENIED ? "CARD FULL: IN FLASH" : "CARD SAVE FAILED: IN FLASH");
    ui.toast_next = 0;
    return r;
}
static void sdm_load_name(const char *name)          /* project rel's whole copy (the .opp, else the newest whole one) */
{
    sdp_head h;
    sdl_cat(sdm.shown, 0, sizeof sdm.shown, name);   /* (sdp_pick's path goes to sdm.path; name may be in it) */
    if (!sdm_ready() || sdm_was(sdp_pick(sdm.shown, sdm.path, sizeof sdm.path, &h, sd_blk)) != FR_OK)
        ui_message("NOT A PROJECT");
    else
        sdm_proj_load_file(sdm.path, sdm.shown);
}
static FRESULT sdm_rmdir(char *path, uint32_t n, uint32_t depth)   /* a folder and what it holds, two levels at most
                                                                     * (fm1-base fm1.toml stack_bounds sdm_rmdir=2) */
{
    DIR d;
    FILINFO fi;
    FRESULT r = f_opendir(&d, path);
    while (r == FR_OK && (r = f_readdir(&d, &fi)) == FR_OK && fi.fname[0]) {
        uint32_t m = sdl_cat(path, sdl_cat(path, n, FF_LFN_BUF + 1u, "/"), FF_LFN_BUF + 1u, fi.fname);
        if (m > FF_LFN_BUF)
            r = FR_INVALID_NAME;
        else if (fi.fattrib & AM_DIR)
            r = depth ? sdm_rmdir(path, m, depth - 1u) : FR_DENIED;
        else
            r = f_unlink(path);
        path[n] = 0;
    }
    (void)f_closedir(&d);
    return r == FR_OK ? f_unlink(path) : r;
}

/* a project's LOAD asked while playing (the user, 2026-10-26: as the flash slots): the transport stops, then the
 * project loads and plays again (op_edits.c pj_defer, pj_tick). Its file kept now: the list may turn meanwhile */
static struct {
    uint8_t ver;                                     /* a version picked (SAVES): that file; else the name's newest whole */
    char path[sizeof sdm.path], name[sizeof sdm.shown];
} sdm_dl;
static void sdm_load_later(uint32_t k)
{
    if (!sdm_ready() || !sdb_path(&sdm.b, k, sdm_dl.path, sizeof sdm_dl.path)) {
        ui_message("NOT ON THE CARD");
        return;
    }
    sdm_dl.ver = (uint8_t)(sdm.view == SDV_VERSIONS);
    str_cpy(sdm_dl.name, sdm_dl.ver ? sdm.vname : sdm_proj_rel(sdm_dl.path), sizeof sdm_dl.name);
    pj_defer(3, 0);
}
static void sdm_load_now(void)
{
    if (sdm_dl.ver)
        sdm_proj_load_file(sdm_dl.path, sdm_dl.name);
    else
        sdm_load_name(sdm_dl.name);
}

/* ---- a card preset's name on the part (op_cells.c snd_name; the user, 2026-10-26: the mixer said 808 BOOM after SUB
 * BASS was loaded). A factory file (Presets/Factory/<Engine>/<nnn> <name>) sets the part's preset number nnn: the name
 * is then the list's everywhere and PRESETS turns on from it. Any other file: its own name (upper case), shown while the
 * part's engine and preset number stay as they were at the load */
static char sdm_pname[NPART][14];
static uint16_t sdm_pkey[NPART];
static uint32_t sdm_pkey_of(const track_t *t) { return (uint32_t)t->eng_req | (uint32_t)t->preset << 8; }
static void sdm_named(uint32_t part, const char *file)
{
    track_t *t = &trk[part % NPART];
    const char *f = sdb_base(sdm.path);              /* (file is sdm.path's last part: ".../Factory/Acid/B/03 - x.ops") */
    uint32_t nn = sdb_bank_no(file), n;
    uint8_t bk = f - sdm.path >= 3 ? sdl_uc((uint8_t)f[-2]) : 0u;
    if (part >= NPART)
        return;
    sdm_pname[part][0] = 0;
    if (nn && sdl_has(sdm.path, "/Presets/" SDL_FACTORY "/") && f[-1] == '/' && f[-3] == '/' && bk >= 'A' && bk <= 'Z') {
        n = (uint32_t)(bk - 'A') * SDB_KEYS + nn - 1u;   /* (bank B's 03: the table's preset 18) */
        if (n < ENG_NPRE(t->eng_req % NENGINES)) {
            t->preset = (uint8_t)n;
            return;
        }
    }
    sdm_strip(sdm_pname[part], sizeof sdm_pname[part], file, ".ops");
    sdl_upper(sdm_pname[part]);
    sdm_pkey[part] = (uint16_t)sdm_pkey_of(t);
}
static const char *sdm_part_name(uint32_t k)            /* the card preset part k plays, 0: its engine's or user's name */
{
    return k < NPART && sdm_pname[k][0] && sdm_pkey[k] == (uint16_t)sdm_pkey_of(&trk[k]) ? sdm_pname[k] : 0;
}

/* ---- the actions on slot k of the list shown */
static void sdm_do(uint32_t op, uint32_t k)
{
    const char *nm;
    uint32_t n, part = song.sel;
    FRESULT r = FR_OK;
#if SDM_DX7
    if (sdm.bank) {                                  /* (a bank's voices: LOAD only) */
        if (op == SDO_LOAD)
            sdm_dx7_load(k);
        else
            ui_message("A DX7 BANK: LOAD ONLY");
        return;
    }
#endif
    if (!sdm_ready() || !sdb_path(&sdm.b, k, sdm.path, sizeof sdm.path)) {
        ui_message("NOT ON THE CARD");
        return;
    }
    nm = sdb_base(sdm.path);
    if (op == SDO_DELETE) {
        if (sdm.list == SDC_PROJ && sdm.view != SDV_VERSIONS) {
            (void)sdp_dedupe(sdm_proj_rel(sdm.path));   /* (a cut rename's halves first: one chain, one name) */
            (void)sdb_path(&sdm.b, k, sdm.path, sizeof sdm.path);
            r = sdm_rmdir(sdm.path, sdl_len(sdm.path), 1u);
        } else {
            if (sdm.view == SDV_VERSIONS)
                (void)sdp_dedupe(sdm.vname);
            (void)sdb_path(&sdm.b, k, sdm.path, sizeof sdm.path);
            r = f_unlink(sdm.path);
        }
        ui_message(sdm_was(r) == FR_OK ? "DELETED" : r == FR_DENIED ? "FOLDER NOT EMPTY" : "NOT DELETED");
    } else if (sdm.list == SDC_PROJ) {
        if (sdm.view == SDV_VERSIONS)
            sdm_proj_load_file(sdm.path, sdm.vname);
        else
            sdm_load_name(sdm_proj_rel(sdm.path));
        return;
    } else if (sdm.list == SDC_PRE) {
        uint64_t id = 0;
        n = sdm_small_get_id(sdm.path, "OPPR", sd_blk, sizeof sd_blk, &id);
        if (is_drum(TSEL))
            ui_message("DRUM TRACK: NO SOUND");
        else if (!n || up_card_give(sd_blk, n) < 0)
            ui_message("NOT A PRESET");
        else {                                       /* (refused: up_load said why) */
            sdm_named(song.sel, nm);
            sdm_ref_set(song.sel, sdm.path, id);     /* (the part's preset: found again by its id) */
            snd_mark(song.sel);
            ui_say("LOADED ", sdm_disp(nm));
        }
        return;
    } else if (sdm.list == SDC_KIT) {
        uint64_t id = 0;
        n = sdm_small_get_id(sdm.path, "OPKT", sd_blk, sizeof sd_blk, &id);
#if FELUCCA_SD_LANES
        n = sdm_kit_lanes_in(sd_blk, n);             /* (its lanes' card samples into the project's: the kit's own n) */
#endif
        if (!n || ukit_card_give(sd_blk, n))
            ui_message("NOT A KIT");
        else {
            sdm_ref_set(NPART, sdm.path, id);        /* (the project's kit: found again by its id) */
            snd_mark(TRK_DRUM);
            ui_say("LOADED ", sdm_disp(nm));
        }
        return;
    } else {                                         /* SAMPLES: USE, the selected part's sample by its hash */
        FILINFO fi;
        char t[8];
        int32_t no = -1;
#if FELUCCA_SD_LANES
        if (is_drum(TSEL) && lane_picked())          /* a drum lane picked: its sample (drums/lane_card.c) */
            part = NPART + lane_selected() % DRUM_LANES;
#endif
        if (part >= SDM_NSMP) {
            ui_message(FELUCCA_SD_LANES ? "PICK A PART OR A LANE" : "PICK T1 T2 OR T3");
            return;
        }
        if (sdx_open() == FR_OK && f_stat(sdm.path, &fi) == FR_OK &&
            (no = sdx_find_path(sdm.path + SDX_SKIP, &sdx.rec)) >= 0 &&
            (sdx.rec.size != (uint32_t)fi.fsize || sdx.rec.stamp != ((uint32_t)fi.fdate << 16 | fi.ftime)))
            no = -1;                                 /* (changed since the last pass: hashed again now) */
        if (no < 0) {                                /* (not indexed yet: hashed now) */
            r = sdm_was(sdx_note_written(sdm.path, sd_blk, sizeof sd_blk));
            no = r == FR_OK ? sdx_find_path(sdm.path + SDX_SKIP, &sdx.rec) : -1;
        }
        if (no < 0) {
            ui_message(r == FR_DENIED ? "SAMPLE INDEX FULL" : "NOT A SAMPLE");
            return;
        }
        memset(&sdm_smp[part], 0, sizeof sdm_smp[part]);
        sdm_smp[part].hash = sdx.rec.hash;
        sdm_smp[part].size = sdx.rec.size;
        sdl_cat(sdm_smp[part].hint, 0, SDM_HINT, sdx.rec.path + sizeof "SAMPLES");   /* (from SAMPLES/, cut) */
        (void)sdx_touch((uint32_t)no);
        sdm_smp_dirty = 1;                           /* (the project's record: sdm_poll writes it, quiet) */
#if FELUCCA_SD_LANES
        if (part >= NPART) {                         /* the lane on CARD, its sample in the plan (sdm_lanes_sync) */
            fm1_irq_off();
            dl.src[part - NPART] = (uint8_t)DL_CARD;
            fm1_irq_on();
            snd_mark(TRK_DRUM);
            sdm_smp_gen++;
            ui_say("LANE: ", sdm_disp(nm));
            return;
        }
#endif
#if FELUCCA_SD_STREAM
        sst_part_set(part, sdm_smp[part].hash);      /* (the part's CARD sample: placed by sd_stream_plan.c) */
#if FELUCCA_SLICE
        if (trk[part].eng_req == (uint8_t)ENG_SLOT_SLICE) {   /* a part on SLICE stays on it: its SRC on CARD (the
                                                               * sample whole, sd_slice.c) */
            trk[part].p[P_E0] = (int16_t)SLC_SRC_CARD;
        } else
#endif
        {
            if (trk[part].eng_req != (uint8_t)ENG_SLOT_SAMPLE) {   /* USE puts the part on SAMPLE (the user, 2026-10-26) */
                set_engine_of(&trk[part], ENG_SLOT_SAMPLE);
                if (trk[part].eng_req != (uint8_t)ENG_SLOT_SAMPLE)
                    return;                          /* (refused: memory short, the drop is proposed: model.c) */
            }
            trk[part].p[P_E0] = (int16_t)SMP_CARD;   /* .. its SET on CARD (eng_sample.c) */
        }
        snd_mark(part);
#endif
        sdm_smp_gen++;
        str_cpy(t, trk_tag(part), sizeof t);
        str_app(t, ": ", sizeof t);
        ui_say(t, sdm_disp(nm));
        return;
    }
}

/* NAME's done (op_name.c) for list `list`: 1 = stay on NAME (a name the card cannot take, another file's) */
static int sdm_save(uint32_t list, const char *name, const char *was)
{
    uint32_t n, dn = 0, same_kit = 0;
    uint64_t id = 0;
    FRESULT r;
    if (!sdm_ready()) {
        ui_message("NO SD CARD");
        return 0;
    }
    if (!sdl_name_ok(name)) {
        ui_message("NAME NOT USABLE");
        return 1;
    }
    if (list == SDC_PROJ) {
        r = sdm_proj_save(name);
        return r == FR_OK ? 0 : r == FR_EXIST || r == FR_INVALID_NAME ? 1 : -1;   /* (BANK FULL: NAME stays) */
    } else if (list == SDC_PRE || list == SDC_KIT) {
        if (list == SDC_PRE ? is_drum(TSEL) : 0) {
            ui_message("DRUM TRACK: NO SOUND");
            return 0;
        }
        {   /* Presets/User/<Engine>/<bank>/<nn> - <name>.ops, Kits/User/<bank>/<nn> - <name>.opk */
            char lc[SDL_NAME_MAX + 1u];
            uint32_t nn, m;
            int same, rc = sdm_save_place(list, name, sdm.path, sizeof sdm.path, &nn, &same);
            if (rc > 0 || (same && sdl_cmp(name, was))) {
                ui_message(rc > 0 ? "BANK FULL" : "NAME TAKEN");   /* (a preset or a kit has no versions: only the one
                                                                    * asked over, its own name) */
                return 1;
            }
            str_cpy(lc, name, sizeof lc);
            m = rc ? sizeof sdm.path : sdl_len(sdm.path);
            dn = m;                                  /* (the bank's folder ends there: its '/') */
            m = m < sizeof sdm.path ? sdm_num(sdm.path, sdl_cat(sdm.path, m, sizeof sdm.path, "/"), sizeof sdm.path, nn) : m;
            m = m < sizeof sdm.path ? sdl_cat(sdm.path, m, sizeof sdm.path, sdl_lower(lc)) : m;
            n = list == SDC_PRE ? up_card_take(sd_blk, name) : ukit_card_take(sd_blk);
            if (list == SDC_KIT && n) {              /* "<nn> - <name> (909, sampled).opk": its instruments */
                char ins[64];
                ukit_card_instr(sd_blk, ins, sizeof ins);
                m = m < sizeof sdm.path ? sdl_cat(sdm.path, m, sizeof sdm.path, ins) : m;
            }
            m = m < sizeof sdm.path ? sdl_cat(sdm.path, m, sizeof sdm.path, list == SDC_PRE ? ".ops" : ".opk") : m;
            r = m < sizeof sdm.path ? FR_OK : FR_INVALID_NAME;
            same_kit = list == SDC_KIT && same ? nn : 0u;
            if (same && dn < sizeof sdm.path) {      /* saved over its own name: its id kept (the old file's) */
                sdm.path[dn] = 0;
                id = sdm_same_id(sdm.path, nn, list == SDC_PRE ? ".ops" : ".opk");
                sdm.path[dn] = '/';
            }
            if (!id)
                id = sdm_new_id();                   /* (a new one: "save as" another name) */
        }
#if FELUCCA_SD_LANES
        if (list == SDC_KIT && n)                    /* (the lanes' card samples after the kit: hash and size each) */
            n = sdm_kit_lanes_out(sd_blk, n);
#endif
        if (r == FR_OK)
            r = n ? sdm_was(sdm_small_put_id(sdm.path, list == SDC_PRE ? "OPPR" : "OPKT", sd_blk, n, id)) : FR_INT_ERR;
        if (r == FR_OK && same_kit && dn < sizeof sdm.path) {                /* (saved over its name: an old file of other instruments goes) */
            sdm.path[dn] = 0;
            sdm_kit_drop_old(sdm.path, same_kit, sdm.path + dn + 1u);
            sdm.path[dn] = '/';
        }
        if (r == FR_OK) {
            (void)sdx_note_written(sdm.path, sd_blk, sizeof sd_blk);   /* (indexed now: found by its id at once) */
            sdm_ref_set(list == SDC_PRE ? song.sel : NPART, sdm.path, id);
        }
        ui.toast_next = 1;
        ui_message(r == FR_OK ? "SAVED TO CARD" : r == FR_DENIED ? "CARD FULL" : "CARD SAVE FAILED");
        ui.toast_next = 0;
        if (r == FR_OK && list == SDC_PRE)
            snd_mark(song.sel);
        return r == FR_OK ? 0 : -1;
    }
    return 0;
}

/* ---- SYSTEM > SD CARD (op_project.c) */
static uint32_t sdm_keep(void) { return sdm_rec.keep; }
static uint32_t sdm_scale(void) { return sdm_rec.scale * 25u; }
static void sdm_set(uint32_t what, int32_t s)        /* 0 VERSIONS, 1 SLACK, 2 PRE-BUFFER: a detent, stopping at the ends;
                                                      * s 0 the default */
{
    static const uint8_t MAX[3] = {SDM_KEEP_MAX, SDM_SCALE_MAX, 4}, DEF[3] = {8, 4, 2};
    uint8_t *v = what == 2u ? &sdm_rec.pre : what ? &sdm_rec.scale : &sdm_rec.keep;
    int32_t x;
    what %= 3u;
    if (what == 2u && !*v)
        *v = 2;                                      /* (0: the default, 50 ms) */
    x = *v + (s > 0 ? 1 : s < 0 ? -1 : 0);
    *v = (uint8_t)(!s ? DEF[what] : x < (what == 2u) ? (what == 2u) : x > MAX[what] ? MAX[what] : x);
    sdm_apply();
    sdm_dirty = 2;
}
static void sdm_leave(void)                          /* SYSTEM left: the settings into the log (sdm_poll, once quiet) */
{
    if (sdm_dirty)
        sdm_dirty = 1;
}
/* SETTINGS (the user, 2026-10-26: a settings backup on the card): BACKUP writes the device's settings record (project.c
 * persist_t, as settings_save makes it now) to Optimist/Backup/settings.opd ("OPST", sdm_small_put); RESTORE reads it
 * back into flash and starts the FM-1 again (the settings are read at power-on). This FM-1's own stay: its Bluetooth
 * address and its copy of the radio's calibration always; its pairings too when the file is another FM-1's (another
 * address: the bonds and remembered devices are that unit's) */
static int sdm_settings_path(void) { return sdl_path(sdm.path, sizeof sdm.path, SDL_BACKUP, "settings.opd") != 0; }
static void sdm_backup(void)
{
    if (!sdm_ready() || !sdm_settings_path()) {
        ui_message("NO SD CARD");
        return;
    }
    settings_save();                                 /* (the record as the device is now) */
    (void)sdl_path(sdm.path, sizeof sdm.path, SDL_BACKUP, 0);
    (void)f_mkdir(sdm.path);
    (void)sdm_settings_path();
    ui_message(sdm_was(sdm_small_put(sdm.path, "OPST", (const uint8_t *)&persist_saved, sizeof persist_saved)) == FR_OK
               ? "SETTINGS SAVED TO CARD" : "SETTINGS NOT SAVED");
}
static void sdm_restore(void)
{
    persist_t r;
    if (!sdm_ready() || !sdm_settings_path()) {
        ui_message("NO SD CARD");
        return;
    }
    if (sdm_small_get(sdm.path, "OPST", (uint8_t *)&r, sizeof r) != sizeof r || r.magic != PERSIST_MAGIC4) {
        ui_message("NO SETTINGS BACKUP");
        return;
    }
    if (memcmp(r.ble_addr, persist_saved.ble_addr, sizeof r.ble_addr)) {   /* another FM-1's: its pairings are its own */
        memcpy(r.ble_bond, persist_saved.ble_bond, sizeof r.ble_bond);
        memcpy(r.ble_dev, persist_saved.ble_dev, sizeof r.ble_dev);
        memcpy(r.bt_rec, persist_saved.bt_rec, sizeof r.bt_rec);
        memcpy(r.bt_key, persist_saved.bt_key, sizeof r.bt_key);
        memcpy(r.bt_kept, persist_saved.bt_kept, sizeof r.bt_kept);
        memcpy(r.ble_kept, persist_saved.ble_kept, sizeof r.ble_kept);
    }
    memcpy(r.ble_addr, persist_saved.ble_addr, sizeof r.ble_addr);   /* (this FM-1's address, its radio's calibration) */
    memcpy(r.ble_rf, persist_saved.ble_rf, sizeof r.ble_rf);
    if (st_save(OBJ_SETTINGS, &r, sizeof r)) {
        ui_message("SETTINGS NOT RESTORED");
        return;
    }
    persist_saved = r;
    ui_message("RESTORED: RESTARTING");
    sdm_reboot_at = fm1_ms + 1u;
}

static void sdm_again(void)                          /* SD CARD's YES: mounted again (or the first time), folders, index */
{
    sdm_ix_force = 1;                                /* (the walk, whatever the signature says) */
    if (sd_state == SD_READY)
        sdm_st = SDM_MOUNTED;
    else
        sdm_mount_try();
    if (sd_state != SD_READY)
        ui_message("NO SD CARD");
}
static int32_t sdm_index_pct(void) { return sdm_st == SDM_INDEX ? (int32_t)sdm_ix_pct : -1; }   /* (-1: no pass) */
#if FELUCCA_SD_STREAM
static uint32_t sdm_pre_ms(void) { return sst_pre_ms; }
static uint32_t sdm_pre_kb(void) { return sst_pre_kb(sst_pre_ms); }
/* the card samples' memory short (sd_stream_plan.c sstp: refused): MEM FULL CARD, then a drop proposed with the one
 * confirm, one part at a time (as model.c mem_offer: only a part whose memory alone makes room, never one asked); none
 * (or all asked): the longest pre-buffer that fits said. Placed by itself once there is room (the plan's retries) */
static uint8_t sdm_asked, sdm_offering;
static void sdm_mem_tick(void)
{
    char a[30];
    uint32_t k;
    if (sstp.state != SSTP_REFUSED) {
        sdm_asked = sdm_offering = 0;
        return;
    }
    if (op_armed() || (sstp.said && !sdm_offering))
        return;
    sdm_offering = 0;
    for (k = 0; k < SST_NPART && k < NPART; k++)
        if ((sstp.drop >> k & 1u) && !(sdm_asked >> k & 1u) && !trk[k].silent) {
            sdm_asked |= (uint8_t)(1u << k);
            sdm_offering = 1;
            sstp.said = 1;
            str_cpy(a, trk_tag(k), sizeof a);
            str_app(a, " ", sizeof a);
            str_app(a, trk_eng_name(&trk[k]), sizeof a);
            ui_message("MEM FULL CARD");
            op_arm(ARM_OP, OA_SDDROP, k, "DROP", a, 1);
            return;
        }
    sstp.said = 1;
    if (sstp.fit_ms) {                               /* "MEM FULL CARD: 25 MS FITS" (SYSTEM > SD card's PRE-BUFFER) */
        str_cpy(a, "MEM FULL CARD: ", sizeof a);
        fmt_app(a, (int32_t)sstp.fit_ms);
        str_app(a, " MS FITS", sizeof a);
        ui_message(a);
    } else {
        ui_message("MEM FULL CARD");
    }
}
/* a part on a card set the card does not hold (sd_stream_plan.c sstp.bmiss): "PIANO NOT ON CARD" once, said again
 * only after it was there (or the part left it); the part silent. Only with the card mounted (none: NO SD CARD) */
static uint8_t sdm_bank_said;
static void sdm_bank_tick(void)
{
    char a[30];
    uint32_t k, m = sd_state == SD_READY && sstp.state != SSTP_GATED && !sdx_pass_on() ? sstp.bmiss : 0u;
    sdm_bank_said &= (uint8_t)m;
    for (k = 0; k < SST_NPART && k < NPART; k++)
        if ((m >> k & 1u) && !(sdm_bank_said >> k & 1u) && !op_armed()) {
            sdm_bank_said |= (uint8_t)(1u << k);
            str_cpy(a, SMP_SET_NAMES[(sst_part_bank[k] - 1u) % SMP_NSETS], sizeof a);
            str_app(a, " NOT ON CARD", sizeof a);
            ui_message(a);
            return;
        }
}
/* the drums' card sounds the card does not hold (sd_stream_plan.c kmiss, x9miss: SD card mode's sampled kits and 909):
 * "KIT SAMPLES MISSING" / "909 SAMPLES MISSING" once, said again only after they were there or no lane wanted them; those
 * hits silent */
static uint32_t sdm_kit_said;
static void sdm_kit_tick(void)
{
    uint32_t m = sd_state == SD_READY && sstp.state != SSTP_GATED ? (uint32_t)sstp.kmiss | (uint32_t)sstp.x9miss << 16 : 0u;
    sdm_kit_said &= m;
    if (!m || (m & ~sdm_kit_said) == 0u || op_armed())
        return;
    ui_message((m & ~sdm_kit_said & 0xFFFFu) ? "KIT SAMPLES MISSING" : "909 SAMPLES MISSING");
    sdm_kit_said |= (m & ~sdm_kit_said & 0xFFFFu) ? m & 0xFFFFu : m & 0xFFFF0000u;
}
/* a GRAIN or STRETCH part whose cache's reads go missing (sd_stream_rc.h: grains given up or cut, windows frozen):
 * "P1 CARD SLOW" once, when a window of SDM_SLOW_MS has SDM_SLOW_N misses or more and they are an eighth of its reads
 * or more; said again only after the part's cache went away (another set, engine, project) */
#define SDM_SLOW_MS 2000u
#define SDM_SLOW_N 4u
static uint8_t sdm_slow_said;
static uint32_t sdm_slow_t0, sdm_slow_rd[SST_NPART], sdm_slow_ms[SST_NPART];
static void sdm_slow_tick(void)
{
    char a[30];
    uint32_t k, rd, ms, dr, dm;
    if (fm1_ms - sdm_slow_t0 < SDM_SLOW_MS)
        return;
    sdm_slow_t0 = fm1_ms;
    for (k = 0; k < SST_NPART && k < NPART; k++) {
        const sst_rc_t *c = &sst_rc[k];
        if (!sst_rc_live(c)) {
            sdm_slow_said &= (uint8_t)~(1u << k);
            sdm_slow_rd[k] = sdm_slow_ms[k] = 0;
            continue;
        }
        rd = c->hit + c->late + c->miss;
        ms = c->miss;
        dr = rd - sdm_slow_rd[k];
        dm = ms - sdm_slow_ms[k];
        sdm_slow_rd[k] = rd;
        sdm_slow_ms[k] = ms;
        if (dm >= SDM_SLOW_N && dm * 8u >= dr && !(sdm_slow_said >> k & 1u) && !op_armed()) {
            sdm_slow_said |= (uint8_t)(1u << k);
            str_cpy(a, trk_tag(k), sizeof a);
            str_app(a, " CARD SLOW", sizeof a);
            ui_message(a);
            return;
        }
    }
}
#else
static uint32_t sdm_pre_ms(void) { return 0; }
static uint32_t sdm_pre_kb(void) { return 0; }
#endif
static uint32_t sdm_state(void) { return sd_state == SD_READY ? (sdm_st >= SDM_INDEX ? 3u : 2u) : sd_state == SD_NOFS ? 1u : 0u; }
