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
 * cut anywhere, the next start goes on). Motion (FELUCCA_MOTION): each section's record carries its motion
 * (sec_codec.c SEC_MOT, power-cut safe and counted in the gauge with it; the reserve is a record with the most
 * motion); an old slot's motion (motion_flash.c, beside it in its sector) moves into its section. */
#if !FELUCCA_FLASH
#error "FELUCCA_SECTIONS 8 / 16 keep the sections in flash (FELUCCA_FLASH)"
#endif
#define SEC_IDS FELUCCA_SECTIONS
#include "sec_codec.c"
#define SLG_BUF_ATTR __attribute__((section(".noinit")))   /* (scratch: the old slots' .noinit room) */
#if FELUCCA_SL24_SAFE
#define SLG_KEPT(s) st_kept(SEC_LOG_BASE + (s) * SEC_SECT)  /* (another firmware's project there: sl24_guard.c) */
#define SEC_OLD(s, h) (!SLG_KEPT(s) && !st_read(slg_off(s), &(h), sizeof(h)) && (h).magic == ST_MAGIC)
#else
#define SEC_OLD(s, h) (!st_read(slg_off(s), &(h), sizeof(h)) && (h).magic == ST_MAGIC)
#endif
#include "sec_log.c"

#define SEC_ARENA 8192u                                /* (typical sections: ~0.5 KiB compressed) */
#if FELUCCA_SL24_XSTEP
#define SEC_PEND_N (2u * SEC_IDS)                      /* (and each section's step extras: SEC_IDS + id, stepx_log.c) */
#else
#define SEC_PEND_N SEC_IDS
#endif
typedef struct {
    uint32_t magic, sum;
    uint16_t len[SEC_PEND_N], off[SEC_PEND_N];         /* per section: its pending record (len 0: none) */
    uint32_t used;
    uint8_t data[SEC_ARENA];
} sec_pend_t;
#define SEC_PEND_MAGIC 0x444E4550u                     /* "PEND" */
static sec_pend_t sec_pend __attribute__((section(".noinit")));
static project_t sec_stage_p __attribute__((section(".pool")));
static dlrec_t sec_stage_d;
static uint8_t sec_rbuf[SEC_REC_MAX] __attribute__((section(".pool"), aligned(4)));
_Static_assert(sizeof proj_tmp >= SEC_REC_MAX, "a section record is received into proj_tmp (ed_backup.c)");
static dlrec_t sec_tmp_dl;
static volatile int8_t sec_stage_id = -1;              /* the section the stage holds (ready for the ISR), -1 none */
static uint32_t sec_gen, sec_stage_gen;                /* a store changes gen: a staged copy of it is decoded again */
static uint32_t sec_misses;                            /* the ISR wanted a section not staged (kept playing) */
static uint8_t sec_bank;                               /* the bank the song layer's keys play / store (0..SEC_IDS/4-1) */

static uint32_t sec_pend_sum(void) { return proj_hash(&sec_pend.len, sizeof sec_pend - 8u); }
static int sec_pend_ok(void) { return sec_pend.magic == SEC_PEND_MAGIC && sec_pend.sum == sec_pend_sum() && sec_pend.used <= SEC_ARENA; }
static void sec_pend_seal(void) { sec_pend.magic = SEC_PEND_MAGIC; sec_pend.sum = sec_pend_sum(); }
static void sec_pend_clear(void) { memset(&sec_pend, 0, sizeof sec_pend - SEC_ARENA); sec_pend_seal(); }
static int sec_pend_has(uint32_t id) { return id < SEC_PEND_N && sec_pend.len[id]; }
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
    for (i = 0; i < SEC_PEND_N; i++)
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
#if FELUCCA_SL24_XSTEP
#include "stepx_log.c"         /* SLOOP 2.4's step extras: a record of their own beside each section's */
/* section s was read into p from its record (n bytes at r): its extras into p's store (the arena's, else the log's) */
static int sx_sec_read(uint32_t s, const project_t *p, const uint8_t *r, uint32_t n)
{
    uint32_t key = proj_hash(r, n);
    sx_store_t *m;
    if (sec_pend_has(SEC_IDS + s)) {
        if ((m = sx_for(p, 1)) != 0) {
            m->psum = p->sum;
            (void)sx_from_rec(sec_pend.data + sec_pend.off[SEC_IDS + s], sec_pend.len[SEC_IDS + s], key, m->x);
        }
    } else
        sx_log_get(SX_ID0 + s, key, p);
    return 1;
}
#define SX_SEC_READ(s, p, r, n) sx_sec_read(s, p, r, n)
#else
#define SX_SEC_READ(s, p, r, n) 1
#endif
/* section s -> p and its drum record d; 0 empty or unreadable */
static int sec_read(uint32_t s, project_t *p, dlrec_t *d)
{
    int n;
    s %= SEC_IDS;
    if (sec_pend_has(s))
        return sec_decode(sec_pend.data + sec_pend.off[s], sec_pend.len[s], p, d) &&
               SX_SEC_READ(s, p, sec_pend.data + sec_pend.off[s], sec_pend.len[s]);
    n = flash_ok ? slg_get(s, sec_rbuf) : 0;
    return n > 0 && sec_decode(sec_rbuf, (uint32_t)n, p, d) && SX_SEC_READ(s, p, sec_rbuf, (uint32_t)n);
}
#if FELUCCA_ARRANGER
/* the song chain past the settings record's 16 parts: the whole chain in the log (id SEC_ID_SONG: count, loop, 2
 * spare bytes, the parts), paired with the settings record by a tag (a hash of the record, never 0). Saved first,
 * then the settings record with its tag: a save cut between the two finds the old tag, so the old chain, whole. */
static uint32_t sec_song_bytes(const arr_config_t *c, uint8_t *b)
{
    uint32_t i;
    b[0] = c->count, b[1] = c->loop, b[2] = b[3] = 0;
    for (i = 0; i < c->count; i++)
        b[4u + 2u * i] = c->entry[i].scene, b[5u + 2u * i] = c->entry[i].bars;
    return 4u + 2u * c->count;
}
static uint32_t sec_song_tag(const uint8_t *b, uint32_t n)
{
    uint32_t h = proj_hash(b, n);
    h = (h ^ h >> 16) & 0xFFFFu;
    return h ? h : 1u;
}
/* -> 0 saved (*tag: the settings record's tag; 0 when the chain fits the record and the log's copy is cleared),
 * 1 MEM FULL, -1 flash */
static int sec_song_put(const arr_config_t *c, uint16_t *tag)
{
    uint8_t b[4u + 2u * ARR_STEPS];
    uint32_t n = sec_song_bytes(c, b);
    int rc;
    *tag = 0;
    if (c->count <= ARR_REC_STEPS)
        return slg_put(SEC_ID_SONG, b, 0, 0) < 0 ? -1 : 0;
    rc = slg_put(SEC_ID_SONG, b, n, 0);
    if (!rc)
        *tag = (uint16_t)sec_song_tag(b, n);
    return rc;
}
/* the settings record said tag: the log's chain replaces c when it is the one tagged and valid in this build (1) */
static int sec_song_get(arr_config_t *c, uint16_t tag)
{
    arr_config_t t;
    uint32_t i;
    int n = tag ? slg_get(SEC_ID_SONG, sec_rbuf) : 0;
    if (n < 4 || (uint32_t)n != 4u + 2u * sec_rbuf[0] || sec_rbuf[0] > ARR_STEPS || sec_song_tag(sec_rbuf, (uint32_t)n) != tag)
        return 0;
    arr_defaults(&t);
    t.count = sec_rbuf[0], t.loop = sec_rbuf[1];
    for (i = 0; i < t.count; i++)
        t.entry[i].scene = sec_rbuf[4u + 2u * i], t.entry[i].bars = sec_rbuf[5u + 2u * i];
    if (!arr_stored_ok(&t))
        return 0;
    *c = t;
    return 1;
}
#endif
#if FELUCCA_SL24_XSTEP
/* section s's record (n bytes in sec_rbuf, just put in the arena): its extras beside it (SEC_IDS + s); 0 ok */
static int sx_pend(uint32_t s, uint32_t n)
{
    const sx_store_t *m = sx_for(&proj_tmp.cur, 0);
    uint32_t r = m && m->psum == proj_tmp.cur.sum ? sx_rec(proj_hash(sec_rbuf, n), m->x) : 0u;
    sec_pend_del(SEC_IDS + s);
    return r ? sec_pend_put(SEC_IDS + s, sx_rbuf, r) : 0;
}
#define SX_PEND(s, n) sx_pend(s, n)
#else
#define SX_PEND(s, n) 0
#endif
/* what is playing now -> sec_rbuf, its length (the audio ISR off while the tracks are read) */
static uint32_t sec_capture(void)
{
    fm1_irq_off();
    proj_capture(&proj_tmp.cur, &sec_tmp_dl);
    fm1_irq_on();
    return sec_encode(&proj_tmp.cur, &sec_tmp_dl, sec_rbuf);
}
/* would the log take section s of n bytes (the playing one may use the reserve)? Counted on the model of the log
 * (sec_log.c sm_put; no log yet: an empty one), what waits in the arena first (it is written first) */
static int sec_room(uint32_t s, uint32_t n, int playing)
{
    uint32_t i;
    slg_model_t m;
    sm_init(&m);
    for (i = 0; i < SEC_IDS; i++)
        if (i != s && sec_pend_has(i) && !sm_put(&m, i, sec_pend.len[i]))
            return 0;
    return playing ? sm_put(&m, s, n) : sm_reserve(&m, s, n);
}
/* the MEM gauge: % used, how many more sections of the last stored size (or 500 B) fit */
static uint32_t sec_last_n = 500;
static void sec_mem(uint32_t *pct, uint32_t *more)
{
    uint32_t i, live = slg_live_bytes(), fits = 1;
    slg_model_t m;
    sm_init(&m);
    for (i = 0; i < SEC_IDS; i++)
        if (sec_pend_has(i)) {
            live += SEC_ALIGN(SEC_HEAD + sec_pend.len[i]) - (slg.at[i] && slg.alen[i] ? SEC_ALIGN(SEC_HEAD + slg.alen[i]) : 0u);
            fits &= sm_put(&m, i, sec_pend.len[i]);    /* (written first: sections_write) */
        }
    *pct = live >= SEC_ROOM ? 100u : live * 100u / SEC_ROOM;
    *more = fits ? sm_more(&m, sec_last_n, live) : 0u;    /* (counted as the stores will be: sec_room) */
}

/* ---- the project slots (PROJECT page, the song studio, the editor): stopped, written at once */
static void project_save(uint32_t slot)
{
    uint32_t s = slot % SEC_IDS, n;
    int rc;
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
    n = sec_capture();
    if (!flash_ok) {
        ui_message(sec_pend_put(s, sec_rbuf, n) || SX_PEND(s, n) ? "MEM FULL" : "SAVED (RAM)");
        sec_gen++;
        return;
    }
    rc = sec_room(s, n, s == (uint32_t)live_sec) ? slg_put(s, sec_rbuf, n, s == (uint32_t)live_sec) : 1;
#if FELUCCA_SL24_XSTEP
    if (!rc)                                           /* its extras beside it (keyed by the record just written) */
        rc = sx_log_put(SX_ID0 + s, proj_hash(sec_rbuf, n), &proj_tmp.cur, s == (uint32_t)live_sec);
    if (!rc)
        sec_pend_del(SEC_IDS + s);
#endif
    if (!rc) {
        sec_pend_del(s);
        sec_last_n = n;
        sec_gen++;
    }
    ui_message(rc == 1 ? "MEM FULL" : rc ? "SAVE ERROR" : "SAVED");
}
static void project_apply(const project_t *p, const dlrec_t *d);
#if FELUCCA_SL24_SAFE
static void sl24_load(uint32_t slot);                  /* sl24_guard.c: a slot of another firmware's */
#endif
static void project_load(uint32_t slot)
{
    if (song.playing || transport_req) { ui_message("STOP BEFORE LOAD"); return; }
    if (!sec_read(slot, &proj_tmp.cur, &sec_tmp_dl)) {
#if FELUCCA_SL24_SAFE
        sl24_load(slot);
#else
        ui_message("EMPTY SLOT");
#endif
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
    if (sec_pend_put(s, sec_rbuf, n) || SX_PEND(s, n)) {
        sec_pend_del(s);
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
static uint32_t section_bars(uint32_t s)
{
    project_t *p = &proj_tmp.cur;                      /* (the main loop's scratch, as section_load) */
    return sec_read(s % SEC_IDS, p, &sec_tmp_dl) ? project_bars(p) : 1u;
}
#endif
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
#if FELUCCA_SL24_XSTEP
                if (!rc)                               /* (its extras: the arena's, none: an older record cleared) */
                    rc = slg_put(SX_ID0 + i, sec_pend.data + sec_pend.off[SEC_IDS + i], sec_pend.len[SEC_IDS + i], 1);
                if (!rc)
                    sec_pend_del(SEC_IDS + i);
#endif
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
#if FELUCCA_QCHAIN
    uint32_t cn = chain_n;                              /* (once: the audio ISR's STOP may clear it meanwhile) */
    if (cn && song.playing && !arrangement_clock.running)   /* a quick chain: its next entry */
        want = chain_sec[((chain_i + 1u) % cn) % CHAIN_MAX];
#endif
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
        any |= SEC_OLD(s, h);                          /* (another firmware's, kept: not an old slot of ours) */
    }
    if (!any)
        return;
    for (i = 0; i < 4u; i++) {
        st_hdr_t h;
        int cur = st_current(OBJ_PROJECT0 + i, &h);    /* the current copy (0 A, 1 B), < 0 none */
        if (cur >= 0 && !slg_has(i) && proj_get(OBJ_PROJECT0 + i, &proj_tmp.cur, &sec_tmp_dl)) {
            uint32_t n, stale = 2u * i + (cur ? 0u : 1u);
#if FELUCCA_MOTION
            motion_flash_read(OBJ_PROJECT0 + i, &proj_tmp.cur);   /* (its motion, beside it: into the record) */
#endif
            n = sec_encode(&proj_tmp.cur, &sec_tmp_dl, sec_rbuf);
            if (!slg.sorder[stale] || slg.head != stale) {
                if (slg_open(stale))
                    return;                            /* (flash: tried again at the next start) */
            }
            if (SEC_ALIGN(SEC_HEAD + n) > slg_free_in_head() || slg_append_raw(i, slg.seq++, sec_rbuf, n))
                return;
        }
        if (cur >= 0) {                                /* its current copy: in the log now */
            uint32_t c = 2u * i + (uint32_t)cur;
#if FELUCCA_SL24_SAFE
            if (SLG_KEPT(c))
                continue;                              /* (not ours: never erased; its stale copy may go) */
#endif
            if (c != slg.head && !st_erase(slg_off(c)))
                slg.sorder[c] = 0;
        }
    }
    for (s = 0; s < SEC_LOG_SECTORS; s++) {            /* what is left of the old objects */
        st_hdr_t h;
        if (s != slg.head && SEC_OLD(s, h) && !st_erase(slg_off(s)))
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
            old |= SEC_OLD(s, h);
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
                old |= SEC_OLD(s, h);
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

#if FELUCCA_BENCH == 4
/* the emulator's measure of the stage (bench.c scenario 4): what plays now encoded once, decoded n times;
 * -> the record's length + the decodes that worked */
static uint32_t sec_bench(uint32_t n)
{
    uint32_t len = sec_capture(), ok = 0, i;
    for (i = 0; i < n; i++)
        ok += (uint32_t)sec_decode(sec_rbuf, len, &sec_stage_p, &sec_stage_d);
    return len + ok;
}
#endif
#if FELUCCA_SL24_SAFE
#include "sl24_guard.c"          /* SLOOP 2.4's projects, autosave, FM6 bank, samples kept (never erased), shown as 2.4's */
#endif
