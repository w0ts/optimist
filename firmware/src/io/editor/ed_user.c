/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol: the user sample slots and the user presets (included by editor.c; web/EDITOR_PROTOCOL.md).
 *   11 SMP_BEGIN  12 SMP_WRITE  13 SMP_END  14 SMP_ERASE  15 SMP_INFO
 *   16 UP_LIST  17 UP_GET  18 UP_PUT  19 UP_STORE  20 UP_LOAD  21 UP_ERASE
 * The ones that erase or write flash are refused while the transport plays or PLAY is queued, as the panel's own
 * saves are (a flash erase silences the audio ~50 ms): rc 3 "stop first" (UP_PUT, UP_STORE, UP_ERASE, SMP_BEGIN,
 * SMP_ERASE), rc 5 for SMP_WRITE (its 3 is a failed write). SMP_END only programs the header (no erase). After
 * SLOOP 2.4 (isod89/sloop-fm1 v2.4, 8d3823f, editor.c ed_flash_busy, GPL-3.0-only) */
static uint32_t ed_flash_busy(void) { return song.playing || transport_req; }
/* a user preset's engine on the wire: its slot, 127 when this build leaves the engine out (kept, not loadable) */
static uint32_t ed_up_eng(uint32_t uid) { return eng_built(uid) ? eng_slot_built(uid) : 127u; }

/* ---- user sample slots (eng_sample.c): flash SMP_USER_BASE + k * SMP_USER_SIZE ----
 * BEGIN erases the header sector (the slot is invalid from then on), WRITE fills the data
 * (offset >= 512, erasing each further sector when the write reaches its start), END sends
 * the header: the device checks the data CRC and writes the header last. */
static uint8_t ed_smp_buf[512] __attribute__((aligned(4)));
static uint8_t ed_smp_open[SMP_USER_SLOTS];        /* SMP_BEGIN done, END not yet: WRITE / END may act */
static uint32_t ed_smp_slot(uint32_t k) { return SMP_USER_BASE + k * SMP_USER_SIZE; }
static void ed_smp_inval(uint32_t k)
{
    fm1_irq_off();
    fl_inval(ed_smp_slot(k), SMP_USER_CAP(k));
    fm1_irq_on();
}
static int ed_smp_erase(uint32_t k, uint32_t all)  /* header sector, or the whole slot */
{
    uint32_t i, took;
    int rc = 0;
    usr_nz[k] = 0;
    for (i = 0; i < 16u; i++)
        usr_zone[k][i].n = 0;                     /* a sounding voice ends instead of reading 0xFF */
    for (i = 0; i < (all ? SMP_USER_CAP(k) / 0x1000u : 1u) && !rc; i++) {
        rc = fl_erase4k_quiet(ed_smp_slot(k) + i * 0x1000u, &took);
        fm1_wdt_feed();
    }
    ed_smp_inval(k);
    return rc;
}
static int ed_smp_end(uint32_t k, const uint8_t *a, uint32_t na)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)ed_smp_buf;
    uint32_t i;
    if (!ed_smp_open[k] || usr_nz[k])
        return 6;                                  /* no BEGIN first (a header over a header: flash ANDs them) */
    if (ed_unpack7(a, na, ed_smp_buf, sizeof(smp_user_hdr_t)) != sizeof(smp_user_hdr_t))
        return 1;
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u ||
        h->data_len > SMP_USER_CAP(k) - SMP_USER_DATA)
        return 2;
    for (i = 0; i < h->nz; i++)                    /* the zones checked before anything is written */
        if (!smp_zone_ok(&h->zone[i], h->data_len))
            return 2;
    ed_smp_open[k] = 0;
    ed_smp_inval(k);
    if (st_crc32(smp_user_xip(k) + SMP_USER_DATA, h->data_len) != h->crc)
        return 3;
    if (fl_write(ed_smp_slot(k), ed_smp_buf, sizeof(smp_user_hdr_t)))
        return 4;
    ed_smp_inval(k);
    smp_user_scan(k);
    return usr_nz[k] ? 0 : 5;
}

/* a command of 11..21; 0 = not one of them, or a frame too short (no reply) */
static int ed_user(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t i;
    switch (cmd) {
    case ED_SMP_BEGIN:                                     /* slot -> slot, rc */
    case ED_SMP_ERASE: {
        uint32_t rc;
        if (na < 1u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return 0;
        rc = ed_flash_busy() ? 3u : ed_smp_erase(a[0], cmd == ED_SMP_ERASE) ? 1u : 0u;
        ed_smp_open[a[0]] = (uint8_t)(cmd == ED_SMP_BEGIN && !rc);
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_SMP_WRITE: {                                   /* slot, off (3 x 7 bit), pack7 data -> slot, off, rc */
        uint32_t off, len, rc = 0, took;
        if (na < 5u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return 0;
        off = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        len = ed_unpack7(a + 4, na - 4u, ed_smp_buf, 256u);
        if (off < SMP_USER_DATA || (off & 0xFFu) || !len || off + len > SMP_USER_CAP(a[0]))
            rc = 1;
        else if (ed_flash_busy())
            rc = 5;                                        /* stop first */
        else if (usr_nz[a[0]] || !ed_smp_open[a[0]])
            rc = 4;                                        /* slot in use: SMP_BEGIN first (voices read it) */
        else {
            if (!(off & 0xFFFu))                           /* first write into a sector: erase it */
                rc = fl_erase4k_quiet(ed_smp_slot(a[0]) + off, &took) ? 2u : 0u;
            if (!rc && fl_write(ed_smp_slot(a[0]) + off, ed_smp_buf, len))
                rc = 3;
        }
        ed_b(a[0]);
        ed_b(off);
        ed_b(off >> 7);
        ed_b(off >> 14);
        ed_b(rc);
        break;
    }
    case ED_SMP_END:                                       /* slot, pack7 header -> slot, rc */
        if (na < 2u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return 0;
        ed_b(a[0]);
        ed_b((uint32_t)ed_smp_end(a[0], a + 1, na - 1u));
        break;
    case ED_SMP_INFO:                                      /* -> per slot: zones (0 = empty), name, data KiB */
        ed_b(SMP_USER_SLOTS);
        ed_b(SMP_USER_SIZE / 1024u);
        for (i = 0; i < SMP_USER_SLOTS; i++) {
            const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(i);
            char nm[9] = {0};
            uint32_t j;
            ed_b(usr_nz[i]);
            for (j = 0; usr_nz[i] && j < 8u; j++)
                nm[j] = h->name[j] >= 32 && h->name[j] < 127 ? h->name[j] : 0;
            ed_str(nm, 8);
            ed_b(usr_nz[i] ? (h->data_len + 1023u) / 1024u : 0u);
        }
        for (i = 0; i < SMP_USER_SLOTS; i++)               /* (appended) each slot's KiB: USR3 64 */
            ed_b(SMP_USER_CAP(i) / 1024u);
        break;
    case ED_UP_LIST: {                                     /* start, count -> start, count, total, per slot: used, engine, name */
        uint32_t s0, cnt;
        if (na < 2u)
            return 0;
        s0 = a[0];
        cnt = a[1] > 16u ? 16u : a[1];
        if (s0 >= UP_SLOTS)
            cnt = 0;
        else if (s0 + cnt > UP_SLOTS)
            cnt = UP_SLOTS - s0;
        ed_b(s0);
        ed_b(cnt);
        ed_b(UP_SLOTS);
        for (i = s0; i < s0 + cnt; i++) {
            char nm[13] = {0};
            uint32_t j, u = (uint32_t)up_used(i);
            for (j = 0; u && j < 12u; j++)
                nm[j] = up_rec(i)->name[j];
            ed_b(u);
            ed_b(u ? ed_up_eng(up_rec(i)->engine) : 0u);
            ed_str(nm, 12);
        }
        break;
    }
    case ED_UP_GET: {                                      /* slot -> slot, used, engine, name, P_COUNT x v14, 16 x (note, flags) */
        int16_t v[P_COUNT];
        char nm[13] = {0};
        const up_rec_t *r;
        uint32_t u;
        if (na < 1u || a[0] >= UP_SLOTS)
            return 0;
        r = up_rec(a[0]);
        u = (uint32_t)up_used(a[0]);
        if (u)
            up_values(r, v);                               /* today's P_* order, clamped */
        for (i = 0; u && i < 12u; i++)
            nm[i] = r->name[i];
        ed_b(a[0]);
        ed_b(u);
        ed_b(u ? ed_up_eng(r->engine) : 0u);
        ed_str(nm, 12);
        for (i = 0; i < P_COUNT; i++)
            ed_v(u ? v[i] : 0);
        for (i = 0; i < 16u; i++) {
            ed_b(u ? r->note[i] : 0u);
            ed_b(u ? r->flags[i] : 0u);
        }
        break;
    }
    case ED_UP_PUT: {                                      /* slot, engine, name, values, pattern -> slot, rc */
        static up_rec_t r;
        uint32_t slot = 0, rc;
        if (na < 1u)
            return 0;
        rc = (uint32_t)up_parse(a, na, &r, &slot);
        if (!rc) {
            int16_t v[P_COUNT];
            up_values(&r, v);                              /* each value inside its range */
            up_vals_put(&r, v);
            rc = ed_flash_busy() ? 3u : up_put(slot, &r) ? 2u : 0u;
        }
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_UP_STORE: {                                    /* slot, name -> slot, rc */
        char nm[13] = {0};
        uint32_t n0, rc = 1;
        if (na < 1u)
            return 0;
        for (n0 = 0; 1u + n0 < na && a[1 + n0] && n0 < 13u; n0++)
            ;
        if (a[0] < UP_SLOTS && (!n0 || up_name_ok(a + 1, n0))) {   /* "" = automatic name */
            for (i = 0; i < n0; i++)
                nm[i] = (char)a[1 + i];
            int r = ed_flash_busy() ? -3 : up_store(a[0], nm);
            rc = r == -3 ? 3u : r == 1 ? 1u : r ? 2u : 0u;   /* 1: the drum track is selected; 3: stop first */
        }
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_UP_LOAD:                                       /* slot -> slot, rc */
        if (na < 1u)
            return 0;
        ed_b(a[0]);
        ed_b(a[0] < UP_SLOTS && !up_load(a[0]) ? 0u : 1u);
        break;
    case ED_UP_ERASE:
        if (na < 1u)
            return 0;
        ed_b(a[0]);
        ed_b(a[0] >= UP_SLOTS ? 1u : ed_flash_busy() ? 3u : up_put(a[0], 0) ? 2u : 0u);
        break;
    default:
        return 0;
    }
    return 1;
}
