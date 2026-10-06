/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: backup and restore of everything the FM-1 keeps in flash, object by object (included by
 * editor.c; FELUCCA_FLASH; web/EDITOR_PROTOCOL.md "Backup and restore"). One registry, BK_OBJS: each stored
 * area is an object with a 4-letter tag, a kind and the build switch it belongs to. A new area (the section
 * log of FELUCCA_SECTIONS, say) is one more line; a kind other than these two needs its read and its write.
 *   BK_ST   a storage.c object (A/B sector pair: projects, autosave, user presets, settings, the kit bank, the
 *           drum records). Read: the current copy's payload. Written back through st_save: the other copy,
 *           header last, read back; the bytes are those the firmware wrote, in their own format (magic,
 *           version inside), so loading them runs the usual migrations.
 *   BK_USR  a user sample slot, raw (its header and data; a slot holding the FM6 user bank too). Read here;
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
 * 5 not in this build (never written blindly), 6 too long, 7 the storage write failed (the old copy stays).
 * A restore opens a session at its first BEGIN: whatever is only in RAM goes to flash first (live sections,
 * song, the working project), then project.c's proj_tmp holds the object being received (proj_put and the
 * kit bank refuse meanwhile; a session left for 10 s ends by itself). An object is written only by its
 * COMMIT, after its CRC: a transfer cut short writes nothing. Done: no RAM copy goes back to flash (the
 * .noinit slots are dropped), the FM-1 restarts. */
enum { ED_BK_LIST = 43, ED_BK_READ, ED_BK_BEGIN, ED_BK_DATA, ED_BK_COMMIT, ED_BK_END };
enum { BK_ST, BK_USR };
#define BK_VERSION 1u
#define BK_CHUNK 256u
typedef struct {
    char tag[4];
    uint8_t kind, id, on;                     /* id: storage.c OBJ_* / USR slot; on: the build has it */
} bk_obj_t;
static const bk_obj_t BK_OBJS[] = {
    {{'S', 'E', 'T', 'T'}, BK_ST, OBJ_SETTINGS, 1},
    {{'D', 'L', 'N', 'S'}, BK_ST, OBJ_DLANES, FELUCCA_ANALOG2},   /* (before the projects: a restore writes in order) */
    {{'P', 'R', 'J', '1'}, BK_ST, OBJ_PROJECT0, 1},
    {{'P', 'R', 'J', '2'}, BK_ST, OBJ_PROJECT0 + 1, 1},
    {{'P', 'R', 'J', '3'}, BK_ST, OBJ_PROJECT0 + 2, 1},
    {{'P', 'R', 'J', '4'}, BK_ST, OBJ_PROJECT0 + 3, 1},
    {{'A', 'U', 'T', 'O'}, BK_ST, OBJ_AUTOSAVE, 1},
    {{'U', 'P', 'R', '1'}, BK_ST, OBJ_UPRESET0, 1},
    {{'U', 'P', 'R', '2'}, BK_ST, OBJ_UPRESET0 + 1, 1},
    {{'U', 'K', 'I', 'T'}, BK_ST, OBJ_UKIT, FELUCCA_DRUM_KITS},
    {{'U', 'S', 'R', '1'}, BK_USR, 0, 1},
    {{'U', 'S', 'R', '2'}, BK_USR, 1, 1},
    {{'U', 'S', 'R', '3'}, BK_USR, 2, 1},
    /* FELUCCA_SECTIONS: the section log plugs in here, e.g. {{'S', 'L', 'O', 'G'}, BK_ST, OBJ_SLOG, FELUCCA_SECTIONS} */
};
#define BK_N (sizeof BK_OBJS / sizeof BK_OBJS[0])
_Static_assert(BK_N <= 24u, "BK_LIST fits one reply");
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
 * flash, 1 ANALOG 2, 2 DRUM_EDIT, 3 DRUM_USR, 4 DRUM_KITS, 5 DRUM_SENDS, 6 ARRANGER, 7 USB_AUDIO */
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
           (uint32_t)FELUCCA_DRUM_USR << 3 | (uint32_t)FELUCCA_DRUM_KITS << 4 | (uint32_t)FELUCCA_DRUM_SENDS << 5 |
           BK_SW_ARR << 6 | BK_SW_UA << 7;
}

/* object i's length and CRC-32 (0 / 0: nothing stored); a storage object's payload is left in st_buf */
static uint32_t bk_info(uint32_t i, uint32_t *crc)
{
    const bk_obj_t *o = &BK_OBJS[i];
    *crc = 0;
    if (!flash_ok)
        return 0;
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
        else if (h->magic == FM6_MAGIC && h->data_len == FM6_BANK_N)
            n = FM6_BANK_OFF + FM6_BANK_N;                     /* the FM6 user bank (fm6_store.c) */
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
            ed_b(PROJ_MAGIC >> (24u - 8u * i));               /* "FUNA" */
        ed_b32(BK_CHUNK, 2);
        for (i = 0; i < BK_N; i++) {
            uint32_t len = bk_info(i, &crc);
            ed_b((uint32_t)BK_OBJS[i].tag[0]), ed_b((uint32_t)BK_OBJS[i].tag[1]);
            ed_b((uint32_t)BK_OBJS[i].tag[2]), ed_b((uint32_t)BK_OBJS[i].tag[3]);
            ed_b(BK_OBJS[i].kind);
            ed_b((BK_OBJS[i].on ? 1u : 0u) | (len ? 2u : 0u) | (BK_OBJS[i].kind == BK_ST ? 4u : 0u) |
                 (BK_OBJS[i].kind == BK_USR && len && ((const smp_user_hdr_t *)smp_user_xip(BK_OBJS[i].id))->magic !=
                  SMP_USER_MAGIC ? 8u : 0u));                  /* (8: the slot holds the FM6 user bank) */
            ed_b32(len, 3);
            ed_b32(crc, 5);
        }
        return 1;
    case ED_BK_READ: {
        if (na < 4u || a[0] >= BK_N)
            return 0;
        i = a[0];
        off = ed_r32(a + 1, 3);
        n = bk_info(i, &crc);
        n = off < n ? (n - off > BK_CHUNK ? BK_CHUNK : n - off) : 0u;
        {
            const uint8_t *p = BK_OBJS[i].kind == BK_ST ? st_buf + off : smp_user_xip(BK_OBJS[i].id) + off;
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
        else if (BK_OBJS[i].kind != BK_ST || !n)
            rc = 1;
        else if (!BK_OBJS[i].on)
            rc = 5;
        else if (n > ST_PAYLOAD_MAX)
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
        else if (st_save(BK_OBJS[i].id, BK_BUF, bk.len))
            rc = 7;
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
