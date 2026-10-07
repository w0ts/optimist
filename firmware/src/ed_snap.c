/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: snapshots (commands 54..57; snapshots.c, snap_store.c; web/EDITOR_PROTOCOL.md "Snapshots").
 * Included by editor.c with FELUCCA_SNAPSHOTS. A firmware without them does not answer 54.
 *   54 SN_LIST   1 (version)          -> 1, slots this build shows, slots in the format (9: 0..7 and BEFORE LOAD),
 *                                        area sectors, free sectors, payload a sector (2 x 7), the longest stream
 *                                        (3 x 7), the work fits now (0 / 1), then per slot of the format: state
 *                                        (0 empty, 1 saved, 2 damaged), shown in this build (0 / 1), size (3 x 7),
 *                                        name (string), BPM (2 x 7), sections (3 x 7, bit = A..P), song parts,
 *                                        the saving build's sections, other build (0 / 1), the work has motion (0 / 1)
 *   55 SN_OP     op, slot [, name]    -> op, slot, rc, notes. op 0 save the state now (name: "" = made from the
 *                                        work), 1 load, 2 clear, 3 rename. notes (a load): 1 sections past this
 *                                        build's, 2 the song not loaded, 4 a section not stored
 *   56 SN_READ   slot, off (3 x 7)    -> slot, off (3 x 7), CRC-32 of the chunk (5 x 7), pack7 bytes (<= 256) of
 *                                        the slot's stream (fewer at its end)
 *   57 SN_WRITE  0, slot, len (3 x 7), CRC-32 (5 x 7) -> 0, slot, rc (begin: room taken)
 *                1, slot, off (3 x 7), CRC-32 of the chunk (5 x 7), pack7 bytes (<= 256, in order) -> 1, slot, off, rc
 *                2, slot             -> 2, slot, rc (commit: the CRC, the stream parsed, written; else nothing)
 *                3, slot             -> 3, slot, 0 (abort)
 * rc: snapshots.c SNE_*: 0 ok, 1 arguments, 2 full, 3 the transport plays, 4 no flash, 5 flash write failed,
 * 6 damaged, 7 busy, 8 empty, 9 still sounding, 10 not a snapshot this firmware loads, 11 a USR3 sample in the
 * area, 12 CRC (a chunk: send it again; the stream at commit), 13 out of order / no import open. */
enum { ED_SN_LIST = 54, ED_SN_OP, ED_SN_READ, ED_SN_WRITE };
static void sn_w7(uint32_t v, uint32_t k) { while (k--) { ed_b(v); v >>= 7; } }
static uint32_t sn_r7(const uint8_t *a, uint32_t k)
{
    uint32_t v = 0;
    while (k--)
        v |= (uint32_t)a[k] << (7u * k);
    return v;
}

static void ed_sn_list(void)
{
    uint32_t k;
    if (!sn.up && flash_ok)
        sn_scan();
    ed_b(1);
    ed_b(FELUCCA_SNAPSHOTS);
    ed_b(SN_NSLOT);
    ed_b(SN_SECTORS);
    ed_b(sn.up ? sn_free_count() : 0u);
    sn_w7(SN_PAY, 2);
    sn_w7(SN_MAX, 3);
    ed_b(flash_ok && (uint32_t)sn_ui_fits());
    for (k = 0; k < SN_NSLOT; k++) {
        sn_info_t in;
        int ok = sn_info(k, &in);
        char nm[SN_NAME + 1u];
        memcpy(nm, in.name, SN_NAME);
        nm[SN_NAME] = 0;
        ed_b(sn.slot[k].state == SN_EMPTY ? 0u : ok ? 1u : 2u);
        ed_b(k < FELUCCA_SNAPSHOTS || k == SN_BAK);
        sn_w7(ok ? sn.slot[k].total : 0u, 3);
        ed_str(ok ? nm : "", SN_NAME);
        sn_w7(in.bpm, 2);
        sn_w7(in.secmask, 3);
        ed_b(in.parts);
        ed_b(in.nsec);
        ed_b(ok && (in.cfg != SN_CFG || in.pmagic != PROJ_MAGIC));
        ed_b(in.flags & 1u);
    }
}

/* a snapshot command; 0 = not one of them: no reply */
static int ed_snap(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint8_t *b = st_buf;                               /* (scratch between storage operations) */
    uint32_t k, off, n, rc;
    if (cmd < ED_SN_LIST || cmd > ED_SN_WRITE)
        return 0;
    switch (cmd) {
    case ED_SN_LIST:
        if (na < 1u || a[0] < 1u)
            return 0;
        ed_sn_list();
        return 1;
    case ED_SN_OP: {
        char nm[SN_NAME + 1u];
        if (na < 2u || a[0] > 3u)
            return 0;
        for (n = 0; n < SN_NAME && 2u + n < na && a[2u + n]; n++)
            nm[n] = (char)(a[2u + n] >= 32u && a[2u + n] < 127u ? a[2u + n] : '_');
        nm[n] = 0;
        k = a[1];
        sn_note = 0;
        rc = k >= SN_NSLOT ? SNE_ARGS : a[0] == 0u ? (uint32_t)sn_save(k, nm) : a[0] == 1u ? (uint32_t)sn_load(k) :
             a[0] == 3u ? (uint32_t)sn_rename(k, nm) :
             !flash_ok ? SNE_NOFLASH : song.playing || transport_req ? SNE_PLAYING : (ed_snap_cancel(), sn_clear(k)) ? SNE_FLASH : SNE_OK;
        ed_b(a[0]);
        ed_b(k);
        ed_b(rc);
        ed_b(sn_note);
        return 1;
    }
    case ED_SN_READ:
        if (na < 4u)
            return 0;
        k = a[0];
        off = sn_r7(a + 1, 3);
        n = sn_size(k);
        n = off < n ? (n - off > SN_CHUNK ? SN_CHUNK : n - off) : 0u;
        if (n && sn_read(k, off, b, n))
            n = 0;
        ed_b(k);
        sn_w7(off, 3);
        sn_w7(st_crc32(b, n), 5);
        ed_pack7(b, n);
        return 1;
    case ED_SN_WRITE:
        if (na < 2u || a[0] > 3u)
            return 0;
        k = a[1];
        if (a[0] == 0u) {
            if (na < 10u)
                return 0;
            rc = (uint32_t)sn_imp_begin(k, sn_r7(a + 2, 3), sn_r7(a + 5, 5));
        } else if (a[0] == 1u) {
            if (na < 11u)
                return 0;
            off = sn_r7(a + 2, 3);
            n = ed_unpack7(a + 10, na - 10u, b, SN_CHUNK);
            rc = !n ? SNE_ARGS : st_crc32(b, n) != sn_r7(a + 5, 5) ? SNE_CRC : (uint32_t)sn_imp_data(k, off, b, n);
            ed_b(1);
            ed_b(k);
            sn_w7(off, 3);
            ed_b(rc);
            return 1;
        } else if (a[0] == 2u)
            rc = (uint32_t)sn_imp_commit(k);
        else {
            ed_snap_cancel();
            rc = SNE_OK;
        }
        ed_b(a[0]);
        ed_b(k);
        ed_b(rc);
        return 1;
    default:
        break;
    }
    return 0;
}
