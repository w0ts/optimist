/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The backup's objects and the user sample slots as files of fm1-base's companion services (FELUCCA_ED_COMPANION: Optimist
 * as an fm1-base app, the trial; fm1-base's docs/COMPANION.md, web/EDITOR_PROTOCOL.md "fm1-base's companion services"), in place of the
 * editor's cmds 43..48 and 11..15: the same objects, read and written through the same paths, the platform speaking the
 * protocol (list, read by offset, a write checked whole by its CRC-32 before it counts). Included by ed_backup.c in place
 * of its command handler; the app's hooks (app_file_*) call ed_cp_* (the trial's ui/opt_editor.h).
 *   0 .. BK_N-1  each object of BK_OBJS by its tag (a tag's trailing space dropped: "S01"), in BK_OBJS' order; read as
 *                BK_READ reads it, written as BK_BEGIN / DATA / COMMIT write it (a storage object received whole into
 *                proj_tmp, checked, then st_save; a project stream's records written as they come); USR1..USR3 as the
 *                sample upload writes them (the header sector erased at the start, the data as it comes, the header,
 *                held until then, last; delete: the slot erased)
 *   BKINFO       (read only) what BK_LIST adds to a name and a size: BK_VERSION, the count, the switches, the project
 *                magic, the chunk; each object's kind and flags (bit 0 in this build, 2 written by BEGIN..COMMIT, 3 a
 *                slot holding the FM6 bank: BK_LIST's, without bit 1, the size says it); then bk_caps. 7-bit bytes,
 *                as BK_LIST has them
 *   BKCTL        (one byte written) the session: 0 end it (BK_END 0), 1 end it and restart to load what came (BK_END 1),
 *                2 the commits from now on only check (BK_COMMIT i 1), 3 they write again. A session opens at the first
 *                write (what is only in RAM saved first) and ends with 0 / 1 or 10 s without a request
 * Refused while the transport plays (app_file_busy), BKCTL apart. */
#define ED_CP_INFO BK_N
#define ED_CP_CTL (BK_N + 1u)
#define ED_CP_N (BK_N + 2u)
_Static_assert(BK_N * 2u + 12u + BK_CAPS_N <= ED_PAYLOAD_N, "BKINFO: one reply's room (made in ed_out)");
static uint8_t ed_cp_check, ed_cp_ctl;               /* BKCTL 2: check only; the byte a BKCTL write brought */

static uint32_t ed_cp_usr(uint32_t i) { return BK_OBJS[i].kind == BK_USR; }
#ifdef FM6_MAGIC                                     /* (BK_LIST's flag 8: the slot holds the FM6 user bank) */
#define ED_CP_FM6(o) ((o)->kind == BK_USR && ((const smp_user_hdr_t *)smp_user_xip((o)->id))->magic == FM6_MAGIC ? 8u : 0u)
#else
#define ED_CP_FM6(o) 0u
#endif
static void ed_cp_drop(void)                         /* an object begun and not committed: dropped (the open project's index back) */
{
#if SEC_LOGGED && FELUCCA_PATTERNS
    if (bk.obj >= 0 && BK_OBJS[bk.obj].kind == BK_PJS)
        bk_scope_back();
#endif
    bk.obj = -1;
}
/* BKINFO's bytes into ed_out (from 5, ed_n their end), as BK_LIST's head, flags and caps */
static void ed_cp_info(void)
{
    static const uint8_t K[] = {BK7(PROJ_MAGIC >> 24, 0), BK7(PROJ_MAGIC >> 16, 0), BK7(PROJ_MAGIC >> 8, 0),
                                BK7(PROJ_MAGIC, 0), BK7X2(BK_CHUNK)};
    uint32_t i;
    ed_begin(0);
    ed_b2(BK_VERSION, BK_N);
    ed_b32(bk_switches(), 2);
    ed_bs(K, sizeof K);
    ed_b2(0, BK_N);
    for (i = 0; i < BK_N; i++) {
        const bk_obj_t *o = &BK_OBJS[i];
        ed_b2(o->kind, (o->on ? 1u : 0u) | (o->kind != BK_USR && o->kind != BK_SNP ? 4u : 0u) | ED_CP_FM6(o));
    }
    bk_caps();
}

/* file i: its name (a tag's 4 letters, a trailing space dropped), kind (fm1-base's PLAT_FK_*), flags (PLAT_FF_*: 1 read,
 * 2 write, 4 delete, 8 also while playing), size and room; 0, or -1 past the last */
#define ED_CP_INFO_N (12u + 2u * BK_N + BK_CAPS_N)   /* (BKINFO's length: ed_cp_info's bytes) */
#if SEC_LOGGED && FELUCCA_PATTERNS
#define ED_CP_PJS_ROOM (SEC_LOG_SECTORS * SEC_SECT)
#else
#define ED_CP_PJS_ROOM 0u
#endif
/* by BK kind (BK_ST, USR, FM6, SEC, PRJ, LOG, SNP, PJS): fm1-base's PLAT_FK_* (bank, sample, bank, project x 2, other x 2,
 * project; the settings apart) and the most a write takes (USR: its slot's room; FM6, SNP: never written) */
static const uint8_t ED_CP_KIND[] = {4, 3, 4, 1, 1, 0, 0, 1};
static const uint32_t ED_CP_ROOM[] = {ST_PAYLOAD_MAX, 0, 0, sizeof proj_tmp, sizeof proj_tmp, sizeof proj_tmp, 0,
                                      ED_CP_PJS_ROOM};
static int ed_cp_file(uint32_t i, char *name, uint32_t *kind, uint32_t *flags, uint32_t *size, uint32_t *room)
{
    const bk_obj_t *o;
    uint32_t crc;
    if (i >= ED_CP_N)
        return -1;
    if (i >= BK_N) {
        memcpy(name, i == ED_CP_INFO ? "BKINFO" : "BKCTL\0", 7);   /* (7 bytes each: "BKCTL" alone is 6, read past) */
        *kind = 0;
        *flags = i == ED_CP_INFO ? 1u : 2u | 8u;
        *size = i == ED_CP_INFO ? ED_CP_INFO_N : 0u;
        *room = i == ED_CP_INFO ? ED_CP_INFO_N : 1u;
        return 0;
    }
    o = &BK_OBJS[i];
    memcpy(name, o->tag, 4);
    name[o->tag[3] == ' ' ? 3 : 4] = 0;
    *kind = o->kind == BK_ST && o->id == OBJ_SETTINGS ? 6u : ED_CP_KIND[o->kind];
    *flags = 1u | (o->on && ED_CP_ROOM[o->kind] ? 2u : 0u) | (o->kind == BK_USR ? 2u | 4u : 0u);
#if SEC_LOGGED && FELUCCA_PATTERNS
    if (o->kind == BK_PJS && bk.obj >= 0 && BK_OBJS[bk.obj].kind == BK_PJS)
        *size = 0;                                   /* (a stream being written holds the index: a walk would move it) */
    else
#endif
        *size = bk_info(i, &crc);
    *room = o->kind == BK_USR ? SMP_USER_CAP(o->id) : ED_CP_ROOM[o->kind];
    return 0;
}

/* n bytes of file i from off (the platform bounds them by the size: 256 at most) -> dst; 0 read */
static int ed_cp_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n)
{
    uint32_t crc, len;
    const uint8_t *p;
    if (i == ED_CP_INFO) {
        ed_cp_info();
        if (off + n > ED_CP_INFO_N)
            return -1;
        memcpy(dst, ed_out + 5u + off, n);
        return 0;
    }
    if (i >= BK_N)
        return -1;
    len = bk_info(i, &crc);
    if (off + n > len)
        return -1;
    if (!n)
        return 0;                                    /* (no object's pointer: a slot with no room's is NULL on fm1-base) */
    p = BK_OBJS[i].kind == BK_ST ? st_buf + off : BK_PJ_BUF(i, off, n) BK_SEC_BUF(i, off) BK_SNP_BUF(i, off, n)
        BK_OBJS[i].kind == BK_FM6 ? fm6_bank_xip + off : smp_user_xip(BK_OBJS[i].id) + off;
    memcpy(dst, p, n);
    return 0;
}

static int ed_cp_busy(void) { return song.playing || transport_req; }
static void ed_cp_session(void)                      /* a write: the session goes on, or opens (RAM-only work saved first) */
{
    if (!proj_tmp_lent) {
        BK_FLUSH();
        proj_tmp_lent = 1;
    }
    proj_tmp_t0 = fm1_ms;
}

/* a new content of file i, size bytes, comes (BK_BEGIN; a sample slot's SMP_BEGIN): 0 ready */
static int ed_cp_begin(uint32_t i, uint32_t size)
{
    ed_cp_drop();
    ed_cp_ctl = 0xFFu;
    if (i == ED_CP_CTL || i >= BK_N)
        return i == ED_CP_CTL ? 0 : -1;
    if (!flash_ok || !size)
        return -1;                                   /* (not written, too long, playing: the platform refused it, flags, room) */
    if (ed_cp_usr(i)) {                             /* the slot is empty from now on; the header comes into proj_tmp */
        uint32_t k = BK_OBJS[i].id;
        if (size < SMP_USER_DATA || st_blank || ed_smp_erase(k, 0))
            return -1;
        ed_smp_open[k] = 1;
    }
    ed_cp_session();
    bk.obj = (int8_t)i;
    bk.len = size;
    bk.got = 0;
#if SEC_LOGGED && FELUCCA_PATTERNS
    if (BK_OBJS[i].kind == BK_PJS) {                 /* the stream's records go into that project as they come */
        bk_ps.hn = 0;
        bk_pj.ok = 0;
        if (bk_scope(BK_OBJS[i].id)) {
            bk.obj = -1;
            return -1;
        }
    }
#endif
    return 0;
}

/* a sample slot's bytes from off: the header's first 512 into proj_tmp, the data into the slot (each sector erased as the
 * data reaches its start), as SMP_WRITE writes them */
static int ed_cp_usr_write(uint32_t k, uint32_t off, const uint8_t *src, uint32_t n)
{
    uint32_t took, t;
    if (off < SMP_USER_DATA) {
        t = SMP_USER_DATA - off < n ? SMP_USER_DATA - off : n;
        memcpy(BK_BUF + off, src, t);
        off += t, src += t, n -= t;
    }
    while (n) {
        t = 0x1000u - (off & 0xFFFu) < n ? 0x1000u - (off & 0xFFFu) : n;
        if ((!(off & 0xFFFu) && fl_erase4k_quiet(ed_smp_slot(k) + off, &took)) || fl_write(ed_smp_slot(k) + off, src, t))
            return -1;
        off += t, src += t, n -= t;
    }
    return 0;
}
/* n bytes of file i from off, in order (BK_DATA; SMP_WRITE): 0 written */
static int ed_cp_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n)
{
    if (i == ED_CP_CTL) {
        ed_cp_ctl = n ? src[0] : 0xFFu;
        return 0;
    }
    if (!proj_tmp_lent || bk.obj != (int8_t)i || n > bk.len - off)
        return -1;                                   /* (in order: the platform's) */
    proj_tmp_t0 = fm1_ms;
    bk.got += n;
    if (ed_cp_usr(i))
        return ed_cp_usr_write(BK_OBJS[i].id, off, src, n);
#if SEC_LOGGED && FELUCCA_PATTERNS
    if (BK_OBJS[i].kind == BK_PJS)
        return bk_pj_feed(src, n) ? -1 : 0;
#endif
    memcpy(BK_BUF + off, src, n);
    return 0;
}

/* a sample slot's header (proj_tmp's first bytes), once its data is in: checked as SMP_END checks it, written last (its
 * page whole, the 32 bytes after the header too: the file reads back as it was written) */
static int ed_cp_usr_end(uint32_t k)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)(const void *)BK_BUF;
    uint32_t z;
    if (!ed_smp_open[k] || h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u ||
        h->data_len != bk.len - SMP_USER_DATA)
        return -1;
    for (z = 0; z < h->nz; z++)
        if (!smp_zone_ok(&h->zone[z], h->data_len))
            return -1;
    ed_smp_open[k] = 0;
    ed_smp_inval(k);
    if (st_crc32(smp_user_xip(k) + SMP_USER_DATA, h->data_len) != h->crc ||
        fl_write(ed_smp_slot(k), BK_BUF, SMP_USER_DATA))
        return -1;
    ed_smp_inval(k);
    smp_user_scan(k);
    return usr_nz[k] ? 0 : -1;
}
/* every byte of file i came, its CRC-32 checked by the platform, not playing (BK_COMMIT; SMP_END; BKCTL's byte acted
 * on): 0 done */
static int ed_cp_commit(uint32_t i)
{
    uint32_t rc;
    if (i == ED_CP_CTL) {
        if (ed_cp_ctl >= 2u) {
            ed_cp_check = ed_cp_ctl == 2u;
            return ed_cp_ctl > 3u ? -1 : 0;
        }
        if (ed_cp_ctl == 1u && proj_tmp_lent) {
            proj_slots_drop();                           /* (no RAM copy goes back over what was restored) */
            bk.reboot = 1;
            bk.reboot_ms = fm1_ms;
        }
        ed_cp_check = 0;
        bk_close();
        return 0;
    }
    if (!proj_tmp_lent || bk.obj != (int8_t)i || bk.got != bk.len) {
        ed_cp_drop();
        return -1;
    }
    if (ed_cp_usr(i))
        rc = (uint32_t)ed_cp_usr_end(BK_OBJS[i].id);
#if SEC_LOGGED && FELUCCA_PATTERNS
    else if (BK_OBJS[i].kind == BK_PJS) {           /* written as it came; whole, its CRC */
        rc = bk_ps.hn;                               /* (a record cut short) */
        bk_scope_back();
        bk_pj.ok = 0;
    }
#endif
    else if (!BK_ST_OK(i))
        rc = 8;                                          /* the firmware would not load it: not written */
    else if (ed_cp_check)
        rc = 0;                                          /* a check only: the object stays unwritten */
    else if (BK_OBJS[i].kind >= BK_SEC)
        rc = bk_commit_sec(i);
    else
        rc = st_save(BK_OBJS[i].id, BK_BUF, bk.len) != 0;
    bk.obj = -1;
    return rc ? -1 : 0;
}

/* file i deleted: a sample slot erased (SMP_ERASE) */
static int ed_cp_delete(uint32_t i)
{
    if (i >= BK_N || !ed_cp_usr(i) || !SMP_USER_CAP(BK_OBJS[i].id) || !flash_ok || st_blank)
        return -1;
    ed_smp_open[BK_OBJS[i].id] = 0;
    return ed_smp_erase(BK_OBJS[i].id, 1) ? -1 : 0;
}
