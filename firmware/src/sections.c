/* SPDX-License-Identifier: GPL-3.0-only */
/* Song sections A..H / A..P (FELUCCA_SECTIONS 8 / 16, registry.h SEC_LOGGED), included by project.c for the
 * firmware. User design (2026-10-06): fixed names A..P in banks of 4, stored compressed (sec_codec.c) in one
 * shared log (sec_log.c, the old project slots' 32 KiB), a MEM gauge, MEM FULL refuses a new section but the
 * playing one can always be saved, an empty section costs nothing. A section is what a project slot was
 * (PROJECT page SAVE / LOAD, SAVE + key in the song layer, the song's chain).
 *
 * RAM: no section is kept whole any more (FELUCCA_SECTIONS 4 kept four, 14.5 KiB of .noinit):
 *   - the pending arena (.noinit, survives a warm reset): sections stored while playing, compressed, written to
 *     the log once the transport stops and nothing sounds (a flash write stops the audio);
 *   - the stage (pool): the next section the audio ISR will apply (the song's next part, a live jump), decoded
 *     ahead by the main loop (sec_service), so a section starts exactly on its bar.
 * Old projects (FUN* in the A/B project objects at 0x97000..0x9EFFF) move into the log as A..D at the first
 * start (sec_migrate: each project's record goes into its stale copy's sector before its current copy is erased;
 * cut anywhere, the next start goes on). */
#if !FELUCCA_FLASH
#error "FELUCCA_SECTIONS 8 / 16 keep the sections in flash (FELUCCA_FLASH)"
#endif
#if FELUCCA_MOTION
#error "FELUCCA_MOTION keeps its data beside the four project slots: build it with FELUCCA_SECTIONS=4"
#endif
#define SEC_IDS FELUCCA_SECTIONS
#include "sec_codec.c"
#define SLG_BUF_ATTR __attribute__((section(".noinit")))   /* (scratch: the old slots' .noinit room) */
#include "sec_log.c"

#define SEC_ARENA 8192u                                /* (typical sections: ~0.5 KiB compressed) */
typedef struct {
    uint32_t magic, sum;
    uint16_t len[SEC_IDS], off[SEC_IDS];               /* per section: its pending record (len 0: none) */
    uint32_t used;
    uint8_t data[SEC_ARENA];
} sec_pend_t;
#define SEC_PEND_MAGIC 0x444E4550u                     /* "PEND" */
static sec_pend_t sec_pend __attribute__((section(".noinit")));
static project_t sec_stage_p __attribute__((section(".pool")));
static dlrec_t sec_stage_d;
static uint8_t sec_rbuf[SEC_REC_MAX] __attribute__((section(".pool"), aligned(4)));
static dlrec_t sec_tmp_dl;
static volatile int8_t sec_stage_id = -1;              /* the section the stage holds (ready for the ISR), -1 none */
static uint32_t sec_gen, sec_stage_gen;                /* a store changes gen: a staged copy of it is decoded again */
static uint32_t sec_misses;                            /* the ISR wanted a section not staged (kept playing) */
static uint8_t sec_bank;                               /* the bank the song layer's keys play / store (0..SEC_IDS/4-1) */

static uint32_t sec_pend_sum(void) { return proj_hash(&sec_pend.len, sizeof sec_pend - 8u); }
static int sec_pend_ok(void) { return sec_pend.magic == SEC_PEND_MAGIC && sec_pend.sum == sec_pend_sum() && sec_pend.used <= SEC_ARENA; }
static void sec_pend_seal(void) { sec_pend.magic = SEC_PEND_MAGIC; sec_pend.sum = sec_pend_sum(); }
static void sec_pend_clear(void) { memset(&sec_pend, 0, sizeof sec_pend - SEC_ARENA); sec_pend_seal(); }
static int sec_pend_has(uint32_t id) { return id < SEC_IDS && sec_pend.len[id]; }
static void sec_pend_del(uint32_t id)
{
    uint32_t i, o, n;
    if (!sec_pend_has(id))
        return;
    o = sec_pend.off[id], n = sec_pend.len[id];
    for (i = o; i + n < sec_pend.used; i++)            /* (down: the arena's later records; no memmove here) */
        sec_pend.data[i] = sec_pend.data[i + n];
    sec_pend.used -= n;
    sec_pend.len[id] = 0;
    for (i = 0; i < SEC_IDS; i++)
        if (sec_pend.len[i] && sec_pend.off[i] > o)
            sec_pend.off[i] = (uint16_t)(sec_pend.off[i] - n);
    sec_pend_seal();
}
static int sec_pend_put(uint32_t id, const uint8_t *r, uint32_t n)    /* 0 ok, 1 no room */
{
    sec_pend_del(id);
    if (sec_pend.used + n > SEC_ARENA)
        return 1;
    memcpy(sec_pend.data + sec_pend.used, r, n);
    sec_pend.off[id] = (uint16_t)sec_pend.used;
    sec_pend.len[id] = (uint16_t)n;
    sec_pend.used += n;
    sec_pend_seal();
    return 0;
}

static int project_used(uint32_t s) { s %= SEC_IDS; return sec_pend_has(s) || slg_has(s); }
static uint32_t sec_ready(void)
{
    uint32_t i, m = 0;
    for (i = 0; i < SEC_IDS; i++)
        m |= (uint32_t)project_used(i) << i;
    return m;
}
/* section s -> p and its drum record d; 0 empty or unreadable */
static int sec_read(uint32_t s, project_t *p, dlrec_t *d)
{
    int n;
    s %= SEC_IDS;
    if (sec_pend_has(s))
        return sec_decode(sec_pend.data + sec_pend.off[s], sec_pend.len[s], p, d);
    n = flash_ok ? slg_get(s, sec_rbuf) : 0;
    return n > 0 && sec_decode(sec_rbuf, (uint32_t)n, p, d);
}
/* what is playing now -> sec_rbuf, its length (the audio ISR off while the tracks are read) */
static uint32_t sec_capture(void)
{
    fm1_irq_off();
    proj_capture(&proj_tmp.cur, &sec_tmp_dl);
    fm1_irq_on();
    return sec_encode(&proj_tmp.cur, &sec_tmp_dl, sec_rbuf);
}
/* would the log take section s of n bytes (the playing one may use the reserve)? */
static int sec_room(uint32_t s, uint32_t n, int playing)
{
    uint32_t i, live = slg_live_bytes(), need = SEC_ALIGN(SEC_HEAD + n);
    if (slg.at[s] && slg.alen[s])
        live -= SEC_ALIGN(SEC_HEAD + slg.alen[s]);
    for (i = 0; i < SEC_IDS; i++)                      /* (what waits to be written counts) */
        if (i != s && sec_pend_has(i))
            live += SEC_ALIGN(SEC_HEAD + sec_pend.len[i]) - (slg.at[i] && slg.alen[i] ? SEC_ALIGN(SEC_HEAD + slg.alen[i]) : 0u);
    return live + need + (playing ? 0u : SEC_ALIGN(SEC_HEAD + SEC_REC_MAX)) <= SEC_ROOM;
}
/* the MEM gauge: % used, how many more sections of the last stored size (or 500 B) fit */
static uint32_t sec_last_n = 500;
static void sec_mem(uint32_t *pct, uint32_t *more)
{
    uint32_t i, live = slg_live_bytes(), room = SEC_ROOM - SEC_ALIGN(SEC_HEAD + SEC_REC_MAX);
    for (i = 0; i < SEC_IDS; i++)
        if (sec_pend_has(i))
            live += SEC_ALIGN(SEC_HEAD + sec_pend.len[i]) - (slg.at[i] && slg.alen[i] ? SEC_ALIGN(SEC_HEAD + slg.alen[i]) : 0u);
    *pct = live >= SEC_ROOM ? 100u : live * 100u / SEC_ROOM;
    *more = live >= room ? 0u : (room - live) / SEC_ALIGN(SEC_HEAD + sec_last_n);
}

/* ---- the project slots (PROJECT page, the song studio, the editor): stopped, written at once */
static void project_save(uint32_t slot)
{
    uint32_t s = slot % SEC_IDS, n;
    int rc;
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
    n = sec_capture();
    if (!flash_ok) {
        ui_message(sec_pend_put(s, sec_rbuf, n) ? "MEM FULL" : "SAVED (RAM)");
        sec_gen++;
        return;
    }
    rc = sec_room(s, n, s == (uint32_t)live_sec) ? slg_put(s, sec_rbuf, n, s == (uint32_t)live_sec) : 1;
    if (!rc) {
        sec_pend_del(s);
        sec_last_n = n;
        sec_gen++;
    }
    ui_message(rc == 1 ? "MEM FULL" : rc ? "SAVE ERROR" : "SAVED");
}
static void project_apply(const project_t *p, const dlrec_t *d);
static void project_load(uint32_t slot)
{
    if (song.playing || transport_req) { ui_message("STOP BEFORE LOAD"); return; }
    if (!sec_read(slot, &proj_tmp.cur, &sec_tmp_dl)) {
        ui_message("EMPTY SLOT");
        return;
    }
    project_apply(&proj_tmp.cur, &sec_tmp_dl);
    ui_message("LOADED");
}

/* ---- live sections (SAVE + key): stored at once into the pending arena (playing too), written when quiet */
static void section_store(uint32_t s)
{
    uint32_t n;
    s %= SEC_IDS;
    n = sec_capture();
    if (!sec_room(s, n, s == (uint32_t)live_sec || slg_has(s))) {
        ui_message("MEM FULL");                        /* (nothing stored: the reserve stays) */
        return;
    }
    if (sec_pend_put(s, sec_rbuf, n)) {
        ui_message("STOP TO SAVE MORE");               /* (the RAM arena is full until the next write) */
        return;
    }
    sec_last_n = n;
    sec_gen++;
    live_sec = (int8_t)s;
    sec_dirty |= (uint16_t)(1u << s);
}
static void section_load(uint32_t s)                   /* stopped: the section is the loop now */
{
    s %= SEC_IDS;
    if (!sec_read(s, &proj_tmp.cur, &sec_tmp_dl))
        return;
    project_apply(&proj_tmp.cur, &sec_tmp_dl);
    live_sec = (int8_t)s;
}
/* a live jump (song layer while playing): staged now, applied by the audio ISR on the next bar; 0 = empty */
static int section_cue(uint32_t s)
{
    s %= SEC_IDS;
    sec_stage_id = -1;                                 /* (the ISR leaves the stage alone while it is written) */
    if (!sec_read(s, &sec_stage_p, &sec_stage_d))
        return 0;
    sec_stage_gen = sec_gen;
    sec_stage_id = (int8_t)s;
    live_req = (int8_t)s;
    return 1;
}

static void sections_write(void)                       /* the pending sections and the song into flash */
{
    uint32_t i;
    if (flash_ok)
        for (i = 0; i < SEC_IDS; i++)
            if (sec_pend_has(i)) {
                int rc = slg_put(i, sec_pend.data + sec_pend.off[i], sec_pend.len[i], 1);
                if (!rc)
                    sec_pend_del(i);
                else if (rc == 1) {                    /* (the log filled meanwhile: kept in RAM, said once) */
                    ui_message("MEM FULL");
                    continue;
                }
            }
    sec_dirty = 0;
    for (i = 0; i < SEC_IDS; i++)
        sec_dirty |= (uint16_t)((uint32_t)sec_pend_has(i) << i);
    if (song_dirty) {
        song_dirty = 0;
        settings_save();
    }
}

/* ---- the audio ISR's side (arranger_scene.c) */
static uint32_t arrangement_ready(void) { return sec_ready(); }
static void arrangement_apply(uint32_t scene)
{
    uint32_t k;
    if (sec_stage_id != (int8_t)scene) {               /* (not staged in time: the section playing goes on) */
        sec_misses++;
        return;
    }
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        seq_release(t);
        trk_all_off(t);
        t->nheld = t->arp_phys = t->arp_note = t->rh_n = t->rskip_n = 0;
        t->rskip_lanes = 0;
    }
    proj_apply(&sec_stage_p, &sec_stage_d, 0);
    sec_stage_id = -1;                                 /* (used: the main loop stages the next) */
    sync_reload = 1;
    ui.force = 1;
}

/* main loop: keep the stage holding the section the ISR will want next: stopped in song mode the song's first
 * part, playing a song its next part (a live jump is staged by section_cue) */
static void sec_service(void)
{
    int want = -1;
    if (arrangement_enabled && arrangement.count) {
        if (!song.playing && !transport_req)
            want = arrangement.entry[0].scene;
        else if (arrangement_clock.running) {
            uint32_t nx = arrangement_clock.index + 1u;
            if (nx >= arrangement.count)
                nx = arrangement.loop ? 0u : 0xFFu;
            if (nx != 0xFFu)
                want = arrangement.entry[nx].scene;
        }
    }
    if (want < 0 || live_req >= 0 || (sec_stage_id == (int8_t)want && sec_stage_gen == sec_gen))
        return;
    if (song.playing && arrangement_clock.running && arrangement_clock.bar + 1u >= arrangement.entry[arrangement_clock.index].bars &&
        sec_stage_id >= 0)
        return;                                        /* (the bar the stage is due: leave it) */
    sec_stage_id = -1;
    if (sec_read((uint32_t)want, &sec_stage_p, &sec_stage_d)) {
        sec_stage_gen = sec_gen;
        sec_stage_id = (int8_t)want;
    }
}

/* ---- the first start with the log: the four old project slots (FUN*, A/B objects) become sections A..D */
static void sec_migrate(void)
{
    uint32_t i, s, any = 0;
    for (s = 0; s < SEC_LOG_SECTORS; s++) {            /* old objects still in the area? */
        st_hdr_t h;
        any |= !st_read(slg_off(s), &h, sizeof h) && h.magic == ST_MAGIC;
    }
    if (!any)
        return;
    for (i = 0; i < 4u; i++) {
        st_hdr_t h;
        int cur = st_current(OBJ_PROJECT0 + i, &h);    /* the current copy (0 A, 1 B), < 0 none */
        if (cur >= 0 && !slg_has(i) && proj_get(OBJ_PROJECT0 + i, &proj_tmp.cur, &sec_tmp_dl)) {
            uint32_t n = sec_encode(&proj_tmp.cur, &sec_tmp_dl, sec_rbuf), stale = 2u * i + (cur ? 0u : 1u);
            if (!slg.sorder[stale] || slg.head != stale) {
                if (slg_open(stale))
                    return;                            /* (flash: tried again at the next start) */
            }
            if (SEC_ALIGN(SEC_HEAD + n) > slg_free_in_head() || slg_append_raw(i, slg.seq++, sec_rbuf, n))
                return;
        }
        if (cur >= 0) {                                /* its current copy: in the log now */
            uint32_t c = 2u * i + (uint32_t)cur;
            if (c != slg.head && !st_erase(slg_off(c)))
                slg.sorder[c] = 0;
        }
    }
    for (s = 0; s < SEC_LOG_SECTORS; s++) {            /* what is left of the old objects */
        st_hdr_t h;
        if (s != slg.head && !st_read(slg_off(s), &h, sizeof h) && h.magic == ST_MAGIC && !st_erase(slg_off(s)))
            slg.sorder[s] = 0;
    }
}
static void sec_boot(void)                             /* persist_boot */
{
    uint32_t s, logged = 0;
    if (!sec_pend_ok())
        sec_pend_clear();
    if (!flash_ok)
        return;
    for (s = 0; s < SEC_LOG_SECTORS; s++) {            /* the log's own sectors (old objects are not erased here) */
        sec_shead_t sh;
        logged |= !st_read(slg_off(s), &sh, sizeof sh) && sh.magic == SEC_MAGIC;
    }
    {
        st_hdr_t h;
        int old = 0;
        for (s = 0; s < SEC_LOG_SECTORS; s++)
            old |= !st_read(slg_off(s), &h, sizeof h) && h.magic == ST_MAGIC;
        if (old) {                                     /* scan what the log has, migrate, then the log as usual */
            memset(&slg, 0, sizeof slg);
            slg.seq = slg.sseq = 1;
            for (s = 0; s < SEC_LOG_SECTORS; s++)
                slg_scan(s);
            slg.head = SEC_LOG_SECTORS;
            for (s = 0; s < SEC_LOG_SECTORS; s++)
                if (slg.sorder[s] && (slg.head >= SEC_LOG_SECTORS || slg.sorder[s] > slg.sorder[slg.head]))
                    slg.head = s;
            if (slg.head < SEC_LOG_SECTORS)
                slg.fill = slg_scan(slg.head);
            sec_migrate();
            old = 0;
            for (s = 0; s < SEC_LOG_SECTORS; s++)
                old |= !st_read(slg_off(s), &h, sizeof h) && h.magic == ST_MAGIC;
            if (old) {                                 /* (cut short: the log stays shut, read only, until the next */
                slg.up = 0;                            /* start goes on; nothing may erase what still waits) */
                return;
            }
        }
    }
    (void)logged;
    slg_boot();
    for (s = 0; s < SEC_IDS; s++)                      /* (a warm reset: what still waits for flash) */
        if (sec_pend_has(s))
            sec_dirty |= (uint16_t)(1u << s);
}
static void proj_slots_drop(void)                      /* a restore done: nothing in RAM goes back to flash */
{
    sec_pend_clear();
    sec_dirty = 0;
    song_dirty = 0;
}
