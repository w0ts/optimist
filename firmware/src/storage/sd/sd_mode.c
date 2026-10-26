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
_Static_assert(SDC_KEYS == SDB_KEYS && SDC_NAME == SDL_NAME_MAX + 1u && SDC_PNAME == SEC_NAME_LEN + 1u,
               "sd_mode.c: the UI's sizes (op_sdcard.c) are the card's");
#define SDM_ID SEC_ID_SDCARD
#define SDM_HINT 52u
enum { SDM_OFF = 0, SDM_NONE, SDM_MOUNTED, SDM_INDEX, SDM_READY };   /* the card's state for the UI */

typedef struct {
    uint8_t magic[2];                                /* "SD" */
    uint8_t keep, scale;                             /* VERSIONS 0..SDM_KEEP_MAX; SLACK in steps of 25 % (0..16) */
    uint8_t pend;                                    /* a card save pending: p */
    uint8_t pre;                                     /* the pre-buffer (SD_STREAM): SST_PRE_MS[pre - 1], 0 its default */
    uint8_t rsv[2];
    sdp_pending p;
} sdm_rec_t;

typedef struct {
    uint64_t hash;                                   /* the sample's (sd_index.c), 0 none */
    uint32_t size;
    char hint[SDM_HINT];                             /* its path under SAMPLES/ when picked (cut), 0 ended */
} sdm_smp_t;
_Static_assert(sizeof(sdm_smp_t) == 64u && sizeof(sdm_smp_t) * NPART <= SEC_REC_MAX, "sd_mode.c: a part's sample record");

#define SDM_DX7 FELUCCA_ENG_FM6                      /* DX7 banks in PRESETS (below: the FM6 collections on the card) */
#define SDM_KEEP_MAX 16u
#define SDM_SCALE_MAX 16u                            /* (x 25 %: 400 %, sd_alloc.c's cap) */
static sdm_rec_t sdm_rec;
static uint8_t sdm_st, sdm_smp_dirty;               /* (the samples' record written once the audio is quiet: sdm_poll) */
static uint8_t sdm_dirty;                            /* the settings changed: 2 on SYSTEM, 1 SYSTEM left (written, quiet) */
static uint8_t sdm_dropped;                          /* the boot's pending save was another project's: dropped */
static sdm_smp_t sdm_smp[NPART];                     /* the open project's samples (the player's) */
static uint32_t sdm_smp_key;                         /* the record read (its sequence + 1; 0 none read) */
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
#if FELUCCA_SD_STREAM
static void sdm_mem_tick(void);
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
    char nm[SEC_NAME_LEN + 1u];
    if (!sdm_rec.pend)
        return 0;
    sec_name(0, nm);
    if (sdl_cmp(nm, sdm_rec.p.name)) {
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
    if (slg_get(SEC_ID_SDSMP, sec_rbuf) == (int)sizeof sdm_smp)
        memcpy(sdm_smp, sec_rbuf, sizeof sdm_smp);
    else
        memset(sdm_smp, 0, sizeof sdm_smp);
#if FELUCCA_SD_STREAM
    for (key = 0; key < NPART; key++)
        sst_part_set(key, sdm_smp[key].hash);        /* (the project's CARD samples: the plan places them) */
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
        if (sdm_smp_dirty && !slg_put(SEC_ID_SDSMP, (const uint8_t *)sdm_smp, sizeof sdm_smp, 1))
            sdm_smp_dirty = 0;
    }
    if (!sdm_smp_dirty)
        sdm_smp_sync();
#if FELUCCA_SD_STREAM
    sdm_mem_tick();
#endif
    if (sdm_st >= SDM_MOUNTED && sd_state != SD_READY) {
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
        sdm_st = SDM_INDEX;
        if (!ui.msg_t)
            ui_message("SD CARD: SAMPLES");          /* (said before the pass, which holds the screen) */
        ui.force = 1;
        return;
    }
    if (sdm_st == SDM_INDEX) {
        (void)sdm_was(sdx_update(sd_blk, sizeof sd_blk, 0, 0));
        sdm_st = SDM_READY;
        ui.force = 1;
    }
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
    fm6_unpack(fm6_ed[p], v);
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
        ui_message("NOT A DX7 BANK");
        return 0;
    }
#endif
    return sdm_ready() && sdm_was(sdb_into(&sdm.b, k)) == FR_OK;
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
    if (!sdm_ready() || sdm.b.view != SDB_FOLDER || !n || !sdl_cmp(sdm.b.root, sdm.path))
        return 0;
    return sdm_was(sdb_up(&sdm.b)) == FR_OK;
}
static int sdm_at_top(void)
{
    uint32_t n = sdl_path(sdm.path, sizeof sdm.path, SDM_FOLDER[sdm.list % SDC_N], 0);
#if SDM_DX7
    if (sdm.bank)
        return 0;
#endif
    return sdm.b.view != SDB_FOLDER || sdm.view == SDV_VERSIONS || !n || !sdl_cmp(sdm.b.root, sdm.path);
}
static void sdm_folder(char *b, uint32_t cap)        /* the folder shown: its last part ("SAMPLES", "DRUMS"); VERSIONS: whose */
{
#if SDM_DX7
    if (sdm.bank) {
        sdl_cat(b, 0, cap, sdb_base(sdm.bpath));
        return;
    }
#endif
    sdl_cat(b, 0, cap, sdm.view == SDV_VERSIONS ? sdm.vname : sdb_base(sdm.b.root));
}
static uint32_t sdm_len_is(const char *s, const char *ext)   /* s ends in ext (".OPS"): s's length without it, else 0 */
{
    uint32_t n = sdl_len(s), e = sdl_len(ext);
    return n > e && !sdl_cmp(s + n - e, ext) ? n - e : 0u;
}
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
    static const char *const EXT[SDC_N] = {".OPP", ".OPS", ".OPK", ""};
    const sdb_item *it = &sdm.b.slot[k % SDB_KEYS];
    uint32_t n, a;
#if SDM_DX7
    if (sdm.bank) {                                  /* a bank's voice: its name */
        *dir = *cut = 0;
        return k < SDB_KEYS ? sdm.bvn[k] : 0;
    }
#endif
    if (k >= sdm.b.n)
        return 0;
    *dir = it->dir;
    *cut = it->cut;
#if SDM_DX7
    if (sdm.list == SDC_PRE && !it->dir && !it->cut && (n = sdm_syx(it->name)) != 0) {
        *dir = 1;                                    /* (a DX7 bank: a folder of its voices) */
        sdl_cat(sdm.shown, 0, sizeof sdm.shown, it->name);
        sdm.shown[n] = 0;
        return sdm.shown;
    }
#endif
    sdl_cat(sdm.shown, 0, sizeof sdm.shown, it->name);
    if (sdm.view == SDV_VERSIONS) {                  /* "<name>-0007.OPP": "VERSION 7" */
        a = sdl_len(sdm.vname) + 1u;
        if (!it->cut && (n = sdm_len_is(it->name, ".OPP")) > a) {
            while (a + 1u < n && it->name[a] == '0')
                a++;
            sdl_cat(sdm.shown, sdl_cat(sdm.shown, 0, sizeof sdm.shown, "VERSION "), sizeof sdm.shown, it->name + a);
            sdm.shown[8u + n - a] = 0;
        }
    } else if (!it->dir && !it->cut && (n = sdm_len_is(it->name, EXT[sdm.list % SDC_N])) != 0) {
        sdm.shown[n] = 0;
    }
    return sdm.shown;
}
static const char *sdm_base_of(uint32_t k)          /* slot k's whole name (from the card: never cut), "" none */
{
    return sdb_path(&sdm.b, k, sdm.path, sizeof sdm.path) ? sdb_base(sdm.path) : "";
}
static int sdm_exists(uint32_t list, const char *name)   /* a project, a preset or a kit of that name on the card */
{
    static const char *const EXT[SDC_N] = {"", ".OPS", ".OPK", ""};
    FILINFO fi;
    uint32_t n = sdl_path(sdm.path, sizeof sdm.path, SDM_FOLDER[list % SDC_N], name);
    return n && sdl_cat(sdm.path, n, sizeof sdm.path, EXT[list % SDC_N]) < sizeof sdm.path &&
           f_stat(sdm.path, &fi) == FR_OK;
}

/* ---- presets and kits: one small file (header + payload, a sector at most: written whole) */
typedef struct {
    char magic[4];                                   /* "OPPR" a preset (upreset.c up_rec_t), "OPKT" a kit (ukit_t + sends) */
    uint16_t ver, len;                               /* 1, the payload's bytes */
    uint64_t hash;                                   /* XXH64 of the payload */
} sdm_fh;
static uint64_t sdm_hash(const uint8_t *p, uint32_t n) { sdh_t s; sdh_init(&s); sdh_add(&s, p, n); return sdh_end(&s); }
static FRESULT sdm_small_put(const char *path, const char *magic, const uint8_t *p, uint32_t n)
{
    sdm_fh h;
    UINT k;
    FRESULT r = f_open(&sdm.f, path, FA_READ | FA_WRITE);
    memcpy(h.magic, magic, 4);
    h.ver = 1;
    h.len = (uint16_t)n;
    h.hash = sdm_hash(p, n);
    if (r == FR_NO_FILE)
        r = sda_create(&sdm.f, path, SDA_EXACT, sizeof h + n, 0);
    if (r != FR_OK)
        return r;
    r = f_write(&sdm.f, &h, sizeof h, &k);
    if (r == FR_OK)
        r = f_write(&sdm.f, p, n, &k);
    if (f_close(&sdm.f) != FR_OK && r == FR_OK)
        r = FR_DISK_ERR;
    return r;
}
static uint32_t sdm_small_get(const char *path, const char *magic, uint8_t *p, uint32_t cap)   /* its payload's bytes, 0 none */
{
    sdm_fh h;
    UINT k = 0;
    uint32_t n = 0;
    if (f_open(&sdm.f, path, FA_READ) != FR_OK)
        return 0;
    if (f_read(&sdm.f, &h, sizeof h, &k) == FR_OK && k == sizeof h && !memcmp(h.magic, magic, 4) && h.ver == 1u &&
        h.len <= cap && f_read(&sdm.f, p, h.len, &k) == FR_OK && k == h.len && sdm_hash(p, h.len) == h.hash)
        n = h.len;
    (void)f_close(&sdm.f);
    return n;
}

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
        if (id >= SLG_IDS || SLG_DEV(id) || !rl || rl > SEC_REC_MAX || at + 3u + rl > len ||
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
    sdl_cat(nm, 0, sizeof nm, name);                /* (its first 12 characters: the flash project's name) */
    (void)sec_name_set(0, nm);
    prj_load_go(0);
    ui_say("LOADED ", nm);
}
static FRESULT sdm_proj_save(const char *name)
{
    sdp_head h;
    uint32_t number = 1;
    FRESULT r;
    if (prj_save_go(0))                              /* the flash copy first (the staged one): MEM FULL, an error said */
        return FR_INT_ERR;
    (void)sec_name_set(0, name);
    persist_flush_now();                            /* (whatever waits in RAM for the log: the copy whole) */
    if (sdp_pick(name, sdm.path, sizeof sdm.path, &h, sd_blk) == FR_OK)
        number = h.number + 1u;                     /* (the save counter: the card's copy's + 1) */
    r = sdm_was(sdp_save(name, number, sdm_stage_len(), sd_blk));
    ui.toast_next = 1;
    ui_message(r == FR_OK ? "SAVED TO CARD" : r == FR_DENIED ? "CARD FULL: IN FLASH" : "CARD SAVE FAILED: IN FLASH");
    ui.toast_next = 0;
    return r;
}
static void sdm_load_name(const char *name)          /* project name's whole copy (the .OPP, else the newest whole one) */
{
    sdp_head h;
    sdl_cat(sdm.shown, 0, sizeof sdm.shown, name);   /* (sdp_pick's path goes to sdm.path; name may be in it) */
    if (!sdm_ready() || sdm_was(sdp_pick(sdm.shown, sdm.path, sizeof sdm.path, &h, sd_blk)) != FR_OK)
        ui_message("NOT A PROJECT");
    else
        sdm_proj_load_file(sdm.path, sdm.shown);
}
static FRESULT sdm_rmdir(char *path, uint32_t n, uint32_t depth)   /* a folder and what it holds, two levels at most */
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
    str_cpy(sdm_dl.name, sdm_dl.ver ? sdm.vname : sdb_base(sdm_dl.path), sizeof sdm_dl.name);
    pj_defer(3, 0);
}
static void sdm_load_now(void)
{
    if (sdm_dl.ver)
        sdm_proj_load_file(sdm_dl.path, sdm_dl.name);
    else
        sdm_load_name(sdm_dl.name);
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
            (void)sdp_dedupe(nm);                    /* (a cut rename's halves first: one chain, one name) */
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
            sdm_load_name(nm);
        return;
    } else if (sdm.list == SDC_PRE) {
        n = sdm_small_get(sdm.path, "OPPR", sd_blk, sizeof sd_blk);
        if (is_drum(TSEL))
            ui_message("DRUM TRACK: NO SOUND");
        else if (!n || up_card_give(sd_blk, n) < 0)
            ui_message("NOT A PRESET");
        else {                                       /* (refused: up_load said why) */
            snd_mark(song.sel);
            ui_say("LOADED ", nm);
        }
        return;
    } else if (sdm.list == SDC_KIT) {
        n = sdm_small_get(sdm.path, "OPKT", sd_blk, sizeof sd_blk);
        if (!n || ukit_card_give(sd_blk, n))
            ui_message("NOT A KIT");
        else {
            snd_mark(TRK_DRUM);
            ui_say("LOADED ", nm);
        }
        return;
    } else {                                         /* SAMPLES: USE, the selected part's sample by its hash */
        FILINFO fi;
        char t[8];
        int32_t no = -1;
        if (part >= NPART) {
            ui_message("PICK T1 T2 OR T3");
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
#if FELUCCA_SD_STREAM
        sst_part_set(part, sdm_smp[part].hash);      /* (the part's CARD sample: placed by sd_stream_plan.c) */
        if (trk[part].eng_req != (uint8_t)ENG_SLOT_SAMPLE) {   /* USE puts the part on SAMPLE (the user, 2026-10-26) */
            set_engine_of(&trk[part], ENG_SLOT_SAMPLE);
            if (trk[part].eng_req != (uint8_t)ENG_SLOT_SAMPLE)
                return;                              /* (refused: memory short, the drop is proposed: model.c) */
        }
        trk[part].p[P_E0] = (int16_t)SMP_CARD;       /* .. its SET on CARD (eng_sample.c) */
        snd_mark(part);
#endif
        sdm_smp_gen++;
        str_cpy(t, trk_tag(part), sizeof t);
        str_app(t, ": ", sizeof t);
        ui_say(t, nm);
        return;
    }
}

/* NAME's done (op_name.c) for list `list`: 1 = stay on NAME (a name the card cannot take, another file's) */
static int sdm_save(uint32_t list, const char *name, const char *was)
{
    uint32_t n;
    FRESULT r;
    if (!sdm_ready()) {
        ui_message("NO SD CARD");
        return 0;
    }
    if (!sdl_name_ok(name)) {
        ui_message("NAME NOT USABLE");
        return 1;
    }
    if (list != SDC_PROJ && sdl_cmp(name, was) && sdm_exists(list, name)) {
        ui_message("NAME TAKEN");                    /* (a preset or a kit has no versions: only the one asked over) */
        return 1;
    }
    if (list == SDC_PROJ) {
        return sdm_proj_save(name) == FR_OK ? 0 : -1;
    } else if (list == SDC_PRE || list == SDC_KIT) {
        if (list == SDC_PRE ? is_drum(TSEL) : 0) {
            ui_message("DRUM TRACK: NO SOUND");
            return 0;
        }
        n = list == SDC_PRE ? up_card_take(sd_blk, name) : ukit_card_take(sd_blk);
        sdl_path(sdm.path, sizeof sdm.path, SDM_FOLDER[list], name);
        r = n && sdl_cat(sdm.path, sdl_len(sdm.path), sizeof sdm.path, list == SDC_PRE ? ".OPS" : ".OPK") < sizeof sdm.path
                ? sdm_was(sdm_small_put(sdm.path, list == SDC_PRE ? "OPPR" : "OPKT", sd_blk, n)) : FR_INT_ERR;
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
static void sdm_again(void)                          /* SD CARD's YES: mounted again (or the first time), folders, index */
{
    if (sd_state == SD_READY)
        sdm_st = SDM_MOUNTED;
    else
        sdm_mount_try();
    if (sd_state != SD_READY)
        ui_message("NO SD CARD");
}
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
#else
static uint32_t sdm_pre_ms(void) { return 0; }
static uint32_t sdm_pre_kb(void) { return 0; }
#endif
static uint32_t sdm_state(void) { return sd_state == SD_READY ? (sdm_st >= SDM_INDEX ? 3u : 2u) : sd_state == SD_NOFS ? 1u : 0u; }
