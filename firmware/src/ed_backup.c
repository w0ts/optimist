/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: backup and restore of everything the FM-1 keeps in flash, object by object (included by
 * editor.c; FELUCCA_FLASH; web/EDITOR_PROTOCOL.md "Backup and restore"). One registry, BK_OBJS: each stored
 * area is an object with a 4-letter tag, a kind and the build switch it belongs to. A new area (the section
 * log of FELUCCA_SECTIONS, say) is one more line; a kind other than these two needs its read and its write.
 *   BK_ST   a storage.c object (A/B sector pair: projects, autosave, user presets, settings, the kit bank, the
 *           drum records). Read: the current copy's payload. Written back through st_save: the other copy,
 *           header last, read back; the bytes are those the firmware wrote, in their own format (magic,
 *           version inside), so loading them runs the usual migrations.
 *   BK_FM6  the FM6 user bank (fm6_store.c, 0xD8000 / 0xD9000): its 4096 VMEM bytes; written back through
 *           fm6_bank_save (erase, data, header last).
 *   BK_USR  a user sample slot, raw (its header and data; an older slot holding the FM6 user bank too). Read here;
 *           written with the sample upload commands (11..13, SMP_BEGIN / WRITE / END: erase, data, header
 *           last), the FM6 bank with its DX7 bank SysEx (fm6_store.c).
 * Commands (a firmware without them does not answer 43):
 *   43 BK_LIST    1 (version)      -> 1, n, switches (2 x 7 bit), project magic (4 ASCII), chunk (2 x 7 bit), then
 *                                     per object: tag (4 ASCII), kind, flags (bit 0 in this build, 1 has data,
 *                                     2 written by BK_BEGIN..COMMIT, 3 a slot holding the FM6 bank), length
 *                                     (3 x 7 bit), CRC-32 (5 x 7 bit)
 *   44 BK_READ    i, off (3 x 7)   -> i, off, CRC-32 of the chunk (5 x 7), pack7 bytes (<= 256; fewer at the end)
 *   45 BK_BEGIN   i, len (3 x 7), CRC-32 (5 x 7) -> i, rc
 *   46 BK_DATA    i, off (3 x 7), CRC-32 (5 x 7), pack7 bytes (<= 256, in order) -> i, off (3 x 7), rc
 *   47 BK_COMMIT  i                -> i, rc (the object written: power-safe, the old copy stays until done)
 *   48 BK_END     0 abort / 1 done -> rc; done: the FM-1 restarts once the reply is out, and loads it all
 * rc: 0 ok, 1 arguments, 2 CRC (a chunk, or the whole object at COMMIT), 3 the transport plays, 4 no flash,
 * 5 not in this build (never written blindly), 6 too long, 7 the storage write failed (the old copy stays),
 * 8 not a valid object: the firmware would not load it (FELUCCA_BK_CHECK; nothing written).
 * A restore opens a session at its first BEGIN: whatever is only in RAM goes to flash first (live sections,
 * song, the working project), then project.c's proj_tmp holds the object being received (proj_put and the
 * kit bank refuse meanwhile; a session left for 10 s ends by itself). An object is written only by its
 * COMMIT, after its CRC: a transfer cut short writes nothing. Done: no RAM copy goes back to flash (the
 * .noinit slots are dropped), the FM-1 restarts. */
enum { ED_BK_LIST = 43, ED_BK_READ, ED_BK_BEGIN, ED_BK_DATA, ED_BK_COMMIT, ED_BK_END };
enum { BK_ST, BK_USR, BK_FM6, BK_SEC, BK_PRJ, BK_LOG, BK_SNP };   /* BK_SEC: a section's record (sections.c); BK_PRJ:
                                                    * an older backup's project slot, written as that section; BK_LOG:
                                                    * another record of the log (id: sec_log.c SEC_ID_SONG..); BK_SNP:
                                                    * the snapshot area, raw (snap_store.c), written with cmds 54..57 */
#if defined(SN_MAGIC) && FELUCCA_SNAPSHOTS                  /* (snap_store.c: the firmware; host tests without it: no SNAP) */
#define BK_SNAP_ON 1
#else
#define BK_SNAP_ON 0
#endif
#define BK_VERSION 2u                         /* 2: BK_LIST ends with what the build holds (bk_caps) */
#define BK_CHUNK 256u
#ifdef FM6_BANK_N                             /* (fm6_store.c: the firmware; host tests may leave it out) */
#define BK_FM6_ON (FELUCCA_ENG_FM6 && FELUCCA_FM6_STORE)
#define BK_FM6_N FM6_BANK_N
#else
#define BK_FM6_ON 0
#define BK_FM6_N 4096u
#define fm6_bank_xip ((const uint8_t *)0)
#define fm6_bank_save(b) ((void)(b), -1)
#define fm6_bank_find() ((void)0)
#endif
#define BK_XID 0xFFu                          /* BK_LOG id of XSTP: not a log id, the extras' pack (bk_xs_pack) */
typedef struct {
    char tag[4];
    uint8_t kind, id, on;                     /* id: storage.c OBJ_* / USR slot; on: the build has it */
} bk_obj_t;
static const bk_obj_t BK_OBJS[] = {
    {{'S', 'E', 'T', 'T'}, BK_ST, OBJ_SETTINGS, 1},
    {{'D', 'L', 'N', 'S'}, BK_ST, OBJ_DLANES, FELUCCA_ANALOG2},   /* (before the projects: a restore writes in order) */
#if SEC_LOGGED
#define BK_S(n) {{'S', (char)('0' + (n) / 10), (char)('0' + (n) % 10), ' '}, BK_SEC, (n) - 1, (n) <= FELUCCA_SECTIONS}
    BK_S(1), BK_S(2), BK_S(3), BK_S(4), BK_S(5), BK_S(6), BK_S(7), BK_S(8),
    BK_S(9), BK_S(10), BK_S(11), BK_S(12), BK_S(13), BK_S(14), BK_S(15), BK_S(16),
    {{'P', 'R', 'J', '1'}, BK_PRJ, 0, 1},                 /* (older backups: written into A..D, never read) */
    {{'P', 'R', 'J', '2'}, BK_PRJ, 1, 1},
    {{'P', 'R', 'J', '3'}, BK_PRJ, 2, 1},
    {{'P', 'R', 'J', '4'}, BK_PRJ, 3, 1},
#if FELUCCA_ARRANGER
    {{'S', 'N', 'G', '1'}, BK_LOG, SEC_ID_SONG, 1},       /* (after SETT: the chain its tag names) */
#endif
#if FELUCCA_SL24_XSTEP
    {{'X', 'S', 'T', 'P'}, BK_LOG, BK_XID, 1},            /* (SLOOP 2.4's step extras of the sections and the autosave, one raw object) */
#endif
#else
    {{'P', 'R', 'J', '1'}, BK_ST, OBJ_PROJECT0, 1},
    {{'P', 'R', 'J', '2'}, BK_ST, OBJ_PROJECT0 + 1, 1},
    {{'P', 'R', 'J', '3'}, BK_ST, OBJ_PROJECT0 + 2, 1},
    {{'P', 'R', 'J', '4'}, BK_ST, OBJ_PROJECT0 + 3, 1},
#endif
    {{'A', 'U', 'T', 'O'}, BK_ST, OBJ_AUTOSAVE, 1},
    {{'U', 'P', 'R', '1'}, BK_ST, OBJ_UPRESET0, 1},
    {{'U', 'P', 'R', '2'}, BK_ST, OBJ_UPRESET0 + 1, 1},
    {{'U', 'K', 'I', 'T'}, BK_ST, OBJ_UKIT, FELUCCA_DRUM_KITS},
#if FELUCCA_UP_FM6 && FELUCCA_ENG_FM6
    {{'U', 'P', 'F', '6'}, BK_ST, OBJ_UPFM6, 1},          /* (after UPR1 / UPR2: the voices of those records) */
#endif
    {{'F', 'M', '6', 'B'}, BK_FM6, 0, BK_FM6_ON},
#if CZ_NUSER
    {{'C', 'Z', 'B', 'K'}, BK_ST, OBJ_CZBANK, 1},         /* the CZ collection (nbank.c) */
#endif
    {{'U', 'S', 'R', '1'}, BK_USR, 0, 1},
    {{'U', 'S', 'R', '2'}, BK_USR, 1, 1},
    {{'U', 'S', 'R', '3'}, BK_USR, 2, 1},
#if BK_SNAP_ON
    {{'S', 'N', 'A', 'P'}, BK_SNP, 0, 1},                 /* (the editor restores each snapshot with SN_WRITE) */
#endif
    /* FELUCCA_SECTIONS: the section log plugs in here, e.g. {{'S', 'L', 'O', 'G'}, BK_ST, OBJ_SLOG, FELUCCA_SECTIONS} */
};
#define BK_N (sizeof BK_OBJS / sizeof BK_OBJS[0])
_Static_assert(10u + BK_N * 14u + 60u <= sizeof ed_out, "BK_LIST fits one reply");
_Static_assert(sizeof proj_tmp >= ST_PAYLOAD_MAX, "a storage object is received into proj_tmp");
#ifndef BK_FLUSH
#define BK_FLUSH() persist_flush_now()        /* (host tests: their own) */
#endif
static struct {
    uint32_t len, crc, got;                   /* the object being received: its length, CRC, bytes so far */
    int8_t obj;                               /* its index, -1 none */
    uint8_t reboot;                           /* BK_END done: main.c restarts 150 ms later (the reply out first) */
    uint32_t reboot_ms;
} bk = {0, 0, 0, -1, 0, 0};
#define BK_BUF ((uint8_t *)(void *)&proj_tmp)

/* the build switches the backup file records (the editor warns about objects a build does not have): bit 0
 * flash, 1 ANALOG 2, 2 DRUM_EDIT, 3 DRUM_USR, 4 DRUM_KITS, 5 DRUM_SENDS (the lanes' sends: always, since 2026-10),
 * 6 ARRANGER, 7 USB_AUDIO */
#if defined(FELUCCA_ARRANGER) && FELUCCA_ARRANGER
#define BK_SW_ARR 1u
#else
#define BK_SW_ARR 0u
#endif
#if defined(FELUCCA_USB_AUDIO) && FELUCCA_USB_AUDIO
#define BK_SW_UA 1u
#else
#define BK_SW_UA 0u
#endif
static uint32_t bk_switches(void)
{
    return (uint32_t)(flash_ok != 0) | (uint32_t)FELUCCA_ANALOG2 << 1 | (uint32_t)FELUCCA_DRUM_EDIT << 2 |
           (uint32_t)FELUCCA_DRUM_USR << 3 | (uint32_t)FELUCCA_DRUM_KITS << 4 | 1u << 5 |
           BK_SW_ARR << 6 | BK_SW_UA << 7;
}

#if BK_SNAP_ON
/* the snapshot area, raw: its length and CRC-32 (0: no snapshot in it); a chunk of it -> st_buf */
static uint32_t bk_snp_info(uint32_t *crc)
{
    static uint32_t key, len, kcrc;                   /* (the last answer: BK_READ asks for every chunk) */
    uint32_t k, o, c = 0xFFFFFFFFu, any = 0, now;
    if (!sn.up)
        sn_scan();
    now = sn.seq * 2654435761u ^ sn_free_count() ^ (sn.slot[SN_BAK].seq << 8);
    for (k = 0; k < SN_NSLOT; k++)
        now = now * 31u + sn.slot[k].seq + sn.slot[k].state;
    if (key && key == now) {
        *crc = kcrc;
        return len;
    }
    for (k = 0; k < SN_NSLOT; k++)
        any |= sn.slot[k].state != SN_EMPTY;
    if (!any)
        return 0;
    for (o = 0; o < SN_SECTORS * SN_SECT; o += BK_CHUNK) {
        if (st_read(SN_BASE + o, st_buf, BK_CHUNK))
            return 0;
        c = st_crc_upd(c, st_buf, BK_CHUNK);
    }
    *crc = kcrc = ~c;
    key = now;
    return len = SN_SECTORS * SN_SECT;
}
static const uint8_t *bk_snp_chunk(uint32_t off, uint32_t n) { (void)st_read(SN_BASE + off, st_buf, n); return st_buf; }
#define BK_SNP_BUF(i, off, n) BK_OBJS[i].kind == BK_SNP ? bk_snp_chunk(off, n) :
#else
#define BK_SNP_BUF(i, off, n)
#endif

#if SEC_LOGGED && FELUCCA_SL24_XSTEP
/* XSTP (stepx_log.c): SLOOP 2.4's step extras of the sections (pending in RAM, else the log's) and the autosave as one
 * object: per record u8 id (0..15 a section, 16 the autosave), u16 length, the log record as it is stored (key, the
 * stored form). Written back whole at the commit, each as the log's record of its section; the key pairs it with the
 * section record (S01..) or the autosave (AUTO) restored beside it. -> bytes in o (SEC_REC_MAX room), 0 none */
static uint32_t bk_xs_pack(uint8_t *o)
{
    uint32_t id, n = 0;
    for (id = 0; id <= 16u; id++) {
        const uint8_t *r = sx_rbuf;
        uint32_t rl = 0;
        if (id < SEC_IDS && sec_pend_has(SEC_IDS + id)) {
            r = sec_pend.data + sec_pend.off[SEC_IDS + id];
            rl = sec_pend.len[SEC_IDS + id];
        } else if (slg_has(SX_ID0 + id) && slg.alen[SX_ID0 + id] <= sizeof sx_rbuf) {
            int g = slg_get(SX_ID0 + id, sx_rbuf);
            rl = g > 0 ? (uint32_t)g : 0u;
        }
        if (rl <= 4u || n + 3u + rl > SEC_REC_MAX)
            continue;
        o[n++] = (uint8_t)id, o[n++] = (uint8_t)rl, o[n++] = (uint8_t)(rl >> 8);
        memcpy(o + n, r, rl);
        n += rl;
    }
    return n;
}
/* the pack b (n bytes) -> the log: 0 ok, 2 not one (nothing written), 7 not written (MEM FULL). The sections and the
 * autosave it does not name lose their extras (the backup had none) */
static uint32_t bk_xs_commit(const uint8_t *b, uint32_t n)
{
    sx_store_t *m;
    uint32_t at, id, rl, seen = 0;
    sec_stage_id = -1;                                 /* (the stage's store is the scratch for the check) */
    if ((m = sx_for(&sec_stage_p, 1)) == 0)
        return 2;
    m->psum = 0;
    for (at = 0; at < n; at += 3u + rl) {
        id = b[at];
        rl = at + 3u <= n ? (uint32_t)b[at + 1u] | (uint32_t)b[at + 2u] << 8 : 0u;
        if (at + 3u > n || id > 16u || ((seen >> id) & 1u) || rl <= 4u || rl > sizeof sx_rbuf || at + 3u + rl > n ||
            !sx_decode(m->x, b + at + 3u + 4u, rl - 4u))
            return 2;
        seen |= 1u << id;
    }
    for (id = 0; id <= 16u; id++) {
        if (id < SEC_IDS)
            sec_pend_del(SEC_IDS + id);
        if (!((seen >> id) & 1u) && slg_has(SX_ID0 + id) && slg_put(SX_ID0 + id, sx_rbuf, 0, 1))
            return 7;
    }
    for (at = 0; at < n; at += 3u + rl) {
        id = b[at];
        rl = (uint32_t)b[at + 1u] | (uint32_t)b[at + 2u] << 8;
        memcpy(sx_rbuf, b + at + 3u, rl);
        if (slg_put(SX_ID0 + id, sx_rbuf, rl, 1))
            return 7;
    }
    return 0;
}
#endif

/* object i's length and CRC-32 (0 / 0: nothing stored); a storage object's payload is left in st_buf */
static uint32_t bk_info(uint32_t i, uint32_t *crc)
{
    const bk_obj_t *o = &BK_OBJS[i];
    *crc = 0;
    if (!flash_ok)
        return 0;
#if BK_SNAP_ON
    if (o->kind == BK_SNP)
        return bk_snp_info(crc);
#endif
    if (o->kind == BK_FM6) {
        if (!o->on || !fm6_bank_xip)
            return 0;
        *crc = st_crc32(fm6_bank_xip, BK_FM6_N);
        return BK_FM6_N;
    }
#if SEC_LOGGED
    if (o->kind == BK_PRJ)
        return 0;                                     /* (written only: an older backup's slot) */
#if FELUCCA_SL24_XSTEP
    if (o->kind == BK_LOG && o->id == BK_XID) {
        uint32_t n = bk_xs_pack(sec_rbuf);
        if (!n)
            return 0;
        *crc = st_crc32(sec_rbuf, n);
        return n;
    }
#endif
    if (o->kind == BK_LOG) {
        int n = slg_get(o->id, sec_rbuf);
        if (n <= 0)
            return 0;
        *crc = st_crc32(sec_rbuf, (uint32_t)n);
        return (uint32_t)n;
    }
    if (o->kind == BK_SEC) {
        int n;
        if (!o->on || !project_used(o->id))
            return 0;
        if (sec_pend_has(o->id)) {
            memcpy(sec_rbuf, sec_pend.data + sec_pend.off[o->id], sec_pend.len[o->id]);
            n = sec_pend.len[o->id];
        } else
            n = slg_get(o->id, sec_rbuf);
        if (n <= 0)
            return 0;
        *crc = st_crc32(sec_rbuf, (uint32_t)n);
        return (uint32_t)n;
    }
#endif
    if (o->kind == BK_ST) {
        st_hdr_t h;
        if (st_current(o->id, &h) < 0)
            return 0;
        *crc = h.crc;
        return h.len;
    } else {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(o->id);
        uint32_t n = 0;
        if (h->magic == SMP_USER_MAGIC && h->version == 1 && h->data_len <= SMP_USER_CAP(o->id) - SMP_USER_DATA)
            n = SMP_USER_DATA + h->data_len;                    /* a sample set: header, its data */
#ifdef FM6_MAGIC
        else if (h->magic == FM6_MAGIC && h->data_len == BK_FM6_N)
            n = FM6_BANK_OFF + BK_FM6_N;                     /* the FM6 user bank (fm6_store.c) */
#endif
        if (n)
            *crc = st_crc32(smp_user_xip(o->id), n);
        return n;
    }
}
static void ed_b32(uint32_t v, uint32_t k) { while (k--) { ed_b(v); v >>= 7; } }
static uint32_t ed_r32(const uint8_t *a, uint32_t k)
{
    uint32_t v = 0;
    while (k--)
        v |= (uint32_t)a[k] << (7u * k);
    return v;
}

/* BK_LIST v2's end: what this build holds, for the editor's report before a restore (a backup from another build:
 * what plays a stand-in, what does not fit, what is skipped). The engine, kit and sample-set masks are by stable
 * UID (registry.h); a project naming an absent engine plays its fallback and keeps its settings (project.c) */
static void bk_caps(void)
{
    static const uint8_t bits[] = FELUCCA_CFG_BITS;
    uint32_t i, m = 0;
    uint64_t kits = 0;
    for (i = 0; i < ENG_UID_N; i++)
        m |= (uint32_t)eng_built(i) << i;
    ed_b32(m, 2);                                             /* engines built, bit = UID */
    for (i = 0; i < DRUM_KITS; i++)
        kits |= (uint64_t)drum_kit_built(i) << i;
    ed_b32((uint32_t)kits, 5);                                /* drum kits built, bit = kit UID (0..36) */
    ed_b32((uint32_t)(kits >> 35), 1);
    for (m = 0, i = 0; i < SMP_NSETS; i++)
        m |= (uint32_t)(SMP_SETS[i].nz != 0) << i;
    ed_b32(m, 2);                                             /* sample sets built, bit = set number */
    ed_b(FELUCCA_SECTIONS);                                   /* project slots / song sections */
    for (i = 0; i < SMP_USER_SLOTS; i++)
        ed_b32(SMP_USER_CAP(i), 3);                           /* USR1..3 capacity, bytes */
    ed_b32((uint32_t)__builtin_offsetof(project_t, t[0].engine), 2);   /* where a FUNB project names its engines, */
    ed_b32((uint32_t)sizeof(proj_trk_t), 2);                  /* a track's size, the drum kit's place (t[3].p[PJ_E0]) */
    ed_b32((uint32_t)__builtin_offsetof(project_t, t[TRK_DRUM].p[PJ_E0]), 2);
    ed_b32((uint32_t)__builtin_offsetof(project_t, t[0].p[PJ_E0]), 2);   /* a part's SET / SRC (SAMPLE, GRAIN) */
    ed_b32(PJ_NP, 1);                                         /* a section record's layout (sec_codec.c): values a */
    ed_b32(PJ_E0, 1);                                         /* track, where EDIT starts, LEN's place */
    ed_b32(P_SLEN, 1);
    ed_b(sizeof bits);                                        /* the builder's items (BUILD, 49) */
    for (i = 0; i < sizeof bits; i++)
        ed_b(bits[i]);
}

#if SEC_LOGGED
#define BK_SEC_BUF(i, off) BK_OBJS[i].kind == BK_SEC || BK_OBJS[i].kind == BK_LOG ? sec_rbuf + (off) :
/* a section's record (BK_SEC), or an older backup's project slot (BK_PRJ, any FUN* format: imported, its drum
 * record from DLNS restored before it), into the log: 0 ok, 2 not a section / project, 7 not written (MEM FULL) */
static uint32_t bk_commit_sec(uint32_t i)
{
    uint32_t id = BK_OBJS[i].id, n = bk.len;
    if (BK_OBJS[i].kind != BK_PRJ && n > SEC_REC_MAX)
        return 6;                                      /* (a log record never is: it would seal its sector) */
#if FELUCCA_SL24_XSTEP
    if (BK_OBJS[i].kind == BK_LOG && id == BK_XID)
        return bk_xs_commit(BK_BUF, n);
#endif
#if FELUCCA_ARRANGER
    if (BK_OBJS[i].kind == BK_LOG)                     /* the song chain: count, loop, 2 spare, the parts */
        return n < 4u || n != 4u + 2u * BK_BUF[0] || BK_BUF[0] > ARR_STEPS ? 2u : slg_put(id, BK_BUF, n, 1) ? 7u : 0u;
#endif
    if (BK_OBJS[i].kind == BK_SEC) {
        if (!sec_decode(BK_BUF, n, &sec_stage_p, &sec_stage_d))
            return 2;
        memcpy(sec_rbuf, BK_BUF, n);
    } else {
        sec_stage_id = -1;
        if (!proj_import(&sec_stage_p, BK_BUF, (int)n))
            return 2;
        memset(&sec_stage_d, 0, sizeof sec_stage_d);
        if (sec_stage_p.dl_hash && !dls_find(id, sec_stage_p.dl_hash, &sec_stage_d))
            sec_stage_p.dl_hash = 0, sec_stage_p.sum = proj_sum(&sec_stage_p);
#if FELUCCA_MOTION
        if (motion_for(&sec_stage_p, 0))
            motion_for(&sec_stage_p, 0)->psum = 0;     /* (an older backup's slot has no motion: not the stage's) */
#endif
        n = sec_encode(&sec_stage_p, &sec_stage_d, sec_rbuf);
    }
    sec_stage_id = -1;                                 /* (the stage held it: the ISR must not take it) */
    sec_pend_del(id);
#if FELUCCA_SL24_XSTEP
    sec_pend_del(SEC_IDS + id);                        /* (its pending extras were the old record's) */
#endif
    sec_gen++;
    return slg_put(id, sec_rbuf, n, 1) ? 7u : 0u;
}
#else
#define BK_SEC_BUF(i, off)
static uint32_t bk_commit_sec(uint32_t i) { (void)i; return 1; }
#endif

#if FELUCCA_BK_CHECK
/* FELUCCA_BK_CHECK (after SLOOP 2.3's restore, isod89 d691ba7, GPL-3.0: each object checked at its commit as a load
 * checks it): a storage object only if the firmware would load it. Today's formats in full (a project's size and sum,
 * the settings' calibration a permutation, a bank's or kit bank's shape); an older project or settings format by its
 * magic (the load converts it and checks it then). 1: it would load */
#ifdef PERSIST_MAGIC                                  /* (project.c without PROJ_HOST: the host tests have no settings record) */
static int bk_panel_ok(const uint8_t *q)              /* a panel_t as stored: ranges, a permutation (panel_init) */
{
    uint32_t i, b = 0, e = 0;
    const uint8_t *btn = q + 4, *enc = btn + NB;
    const int8_t *dir = (const int8_t *)(enc + NE);
    for (i = 0; i < NB; i++) {
        if (btn[i] >= 14u || (b >> btn[i]) & 1u)
            return 0;
        b |= 1u << btn[i];
    }
    for (i = 0; i < NE; i++) {
        if (enc[i] >= 7u || (e >> enc[i]) & 1u || (dir[i] != 1 && dir[i] != -1))
            return 0;
        e |= 1u << enc[i];
    }
    return 1;
}
#endif
static int bk_st_ok(uint32_t obj, const uint8_t *b, uint32_t n)
{
    uint32_t m = n >= 4u ? (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24 : 0u;
#ifdef PERSIST_MAGIC
    if (obj == OBJ_SETTINGS) {
        const persist_t *p = (const persist_t *)(const void *)b;
        if (m == 0x50455231u || m == 0x50455232u || m == 0x50455233u) {   /* PER1..3: this build's or an older one */
            if (m != PERSIST_MAGIC)
                return 1;
            return n <= sizeof *p && n >= __builtin_offsetof(persist_t, panel) + sizeof(panel_t) &&
                   (p->panel.magic != PANEL_MAGIC || bk_panel_ok((const uint8_t *)&p->panel));
        }
        return 0;
    }
#endif
    if (obj == OBJ_AUTOSAVE || (obj >= OBJ_PROJECT0 && obj < OBJ_PROJECT0 + 4u)) {
        if (m == PROJ_MAGIC)
            return n == sizeof(project_t) && proj_ok((const project_t *)(const void *)b);
        return m >> 8 == 0x46554Eu;                      /* "FUN1".."FUNA": converted (and checked) when loaded */
    }
#ifdef UP_BANK_MAGIC
    if (obj == OBJ_UPRESET0 || obj == OBJ_UPRESET0 + 1u) {
        const up_bank_t *k = (const up_bank_t *)(const void *)b;
        return n == sizeof *k && (k->magic == UP_BANK_MAGIC || (UP_FROM_V1 && k->magic == 0x31425055u)) &&
               k->rsize == sizeof(up_rec_t) && k->nslot == UP_PER_BANK;
    }
#endif
#if FELUCCA_UP_FM6 && FELUCCA_ENG_FM6
    if (obj == OBJ_UPFM6)
        return n == sizeof(upf_t) && m == UPF_MAGIC;
#endif
#if FELUCCA_DRUM_KITS
    if (obj == OBJ_UKIT) {
        const ukit_bank_t *k = (const ukit_bank_t *)(const void *)b;
        return k->n == UK_N && ((n == sizeof *k && m == UK_MAGIC && k->rsize == sizeof(ukit_t)) ||
                                (n == 8u + UK_N * UK_RSIZE1 && m == UK_MAGIC1 && k->rsize == UK_RSIZE1));
    }
#endif
#if FELUCCA_ANALOG2
    if (obj == OBJ_DLANES) {
        const dls_t *k = (const dls_t *)(const void *)b;
        return n == sizeof *k && m == DLS_MAGIC && k->esize == sizeof(dls_ent_t) && k->n == DLS_N;
    }
#endif
    return 1;
}
#define BK_ST_OK(i) (BK_OBJS[i].kind != BK_ST || bk_st_ok(BK_OBJS[i].id, BK_BUF, bk.len))
#else
#define BK_ST_OK(i) 1
#endif

static void bk_close(void)
{
    bk.obj = -1;
    proj_tmp_lent = 0;
}

/* a backup / restore command; 0 = not one of them: no reply */
static int ed_backup(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t i, off, n, crc, rc = 0;
    if (cmd < ED_BK_LIST || cmd > ED_BK_END)
        return 0;
    if (proj_tmp_lent)
        proj_tmp_t0 = fm1_ms;                                 /* (the session goes on) */
    switch (cmd) {
    case ED_BK_LIST:
        if (na < 1u || a[0] < 1u)
            return 0;
        ed_b(BK_VERSION);
        ed_b(BK_N);
        ed_b32(bk_switches(), 2);
        for (i = 0; i < 4u; i++)
            ed_b(PROJ_MAGIC >> (24u - 8u * i));               /* "FUNB" */
        ed_b32(BK_CHUNK, 2);
        for (i = 0; i < BK_N; i++) {
            uint32_t len = bk_info(i, &crc);
            ed_b((uint32_t)BK_OBJS[i].tag[0]), ed_b((uint32_t)BK_OBJS[i].tag[1]);
            ed_b((uint32_t)BK_OBJS[i].tag[2]), ed_b((uint32_t)BK_OBJS[i].tag[3]);
            ed_b(BK_OBJS[i].kind);
            ed_b((BK_OBJS[i].on ? 1u : 0u) | (len ? 2u : 0u) | (BK_OBJS[i].kind != BK_USR && BK_OBJS[i].kind != BK_SNP ? 4u : 0u) |
                 (BK_OBJS[i].kind == BK_USR && len && ((const smp_user_hdr_t *)smp_user_xip(BK_OBJS[i].id))->magic !=
                  SMP_USER_MAGIC ? 8u : 0u));                  /* (8: the slot holds the FM6 user bank) */
            ed_b32(len, 3);
            ed_b32(crc, 5);
        }
        bk_caps();
        return 1;
    case ED_BK_READ: {
        if (na < 4u || a[0] >= BK_N)
            return 0;
        i = a[0];
        off = ed_r32(a + 1, 3);
        n = bk_info(i, &crc);
        n = off < n ? (n - off > BK_CHUNK ? BK_CHUNK : n - off) : 0u;
        {
            const uint8_t *p = BK_OBJS[i].kind == BK_ST ? st_buf + off : BK_SEC_BUF(i, off) BK_SNP_BUF(i, off, n)
                               BK_OBJS[i].kind == BK_FM6 ? fm6_bank_xip + off : smp_user_xip(BK_OBJS[i].id) + off;
            ed_b(i);
            ed_b32(off, 3);
            ed_b32(st_crc32(p, n), 5);
            ed_pack7(p, n);
        }
        return 1;
    }
    case ED_BK_BEGIN:
        if (na < 9u || a[0] >= BK_N)
            return 0;
        i = a[0];
        n = ed_r32(a + 1, 3);
        if (!flash_ok)
            rc = 4;
        else if (BK_OBJS[i].kind == BK_USR || BK_OBJS[i].kind == BK_SNP || !n)
            rc = 1;
        else if (!BK_OBJS[i].on)
            rc = 5;
        else if (n > (BK_OBJS[i].kind == BK_FM6 ? BK_FM6_N : BK_OBJS[i].kind >= BK_SEC ? (uint32_t)sizeof proj_tmp : ST_PAYLOAD_MAX) ||
                 (BK_OBJS[i].kind == BK_FM6 && n != BK_FM6_N))
            rc = 6;
        else if (song.playing || transport_req)
            rc = 3;
        else {
            if (!proj_tmp_lent) {                              /* a new session: RAM-only work into flash first */
                BK_FLUSH();
                proj_tmp_lent = 1;
                proj_tmp_t0 = fm1_ms;
            }
            bk.obj = (int8_t)i;
            bk.len = n;
            bk.crc = ed_r32(a + 4, 5);
            bk.got = 0;
        }
        ed_b(i);
        ed_b(rc);
        return 1;
    case ED_BK_DATA:
        if (na < 10u || a[0] >= BK_N)
            return 0;
        i = a[0];
        off = ed_r32(a + 1, 3);
        if (!proj_tmp_lent || bk.obj != (int8_t)i || off != bk.got)
            rc = 1;
        else {
            n = ed_unpack7(a + 9, na - 9u, BK_BUF + off, bk.len - off < BK_CHUNK ? bk.len - off : BK_CHUNK);
            if (!n)
                rc = 1;
            else if (st_crc32(BK_BUF + off, n) != ed_r32(a + 4, 5))
                rc = 2;                                        /* (the editor sends it again) */
            else
                bk.got += n;
        }
        ed_b(i);
        ed_b32(off, 3);
        ed_b(rc);
        return 1;
    case ED_BK_COMMIT:
        if (na < 1u || a[0] >= BK_N)
            return 0;
        i = a[0];
        if (!proj_tmp_lent || bk.obj != (int8_t)i)
            rc = 1;
        else if (song.playing || transport_req)
            rc = 3;
        else if (bk.got != bk.len || st_crc32(BK_BUF, bk.len) != bk.crc)
            rc = 2;
#if FELUCCA_BK_CHECK
        else if (!BK_ST_OK(i))
            rc = 8;                                            /* the firmware would not load it: not written */
#endif
        else if (BK_OBJS[i].kind >= BK_SEC)
            rc = bk_commit_sec(i);
        else if (BK_OBJS[i].kind == BK_FM6 ? fm6_bank_save(BK_BUF) != 0 : st_save(BK_OBJS[i].id, BK_BUF, bk.len) != 0)
            rc = 7;
        if (BK_OBJS[i].kind == BK_FM6)
            fm6_bank_find();                                   /* (VOICE U01..U32: the bank as written) */
        bk.obj = -1;
        ed_b(i);
        ed_b(rc);
        return 1;
    case ED_BK_END:
        if (na < 1u)
            return 0;
        if (a[0] == 1u && proj_tmp_lent) {
            proj_slots_drop();                                 /* (no RAM copy goes back over what was restored) */
            bk.reboot = 1;
            bk.reboot_ms = fm1_ms;
        }
        bk_close();
        ed_b(0);
        return 1;
    default:
        break;
    }
    return 0;
}
