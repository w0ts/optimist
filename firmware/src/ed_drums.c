/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the drum lanes and the user kits (included by editor.c; web/EDITOR_PROTOCOL.md "Drum
 * lanes and user kits"). A build without a switch does not answer its commands (no reply = absent).
 *   36 DRUM_LANES  [pack7 lanes (204 B)]            -> pack7 lanes (the working project's drum_edit.c dl)
 *   37 DRUM_LANE   lane [, pack7 (8 offsets, src, ref[3])] -> lane, pack7 of the same 12 bytes
 *   38 UKIT_LIST                                    -> 16, then per slot: used (no names: a kit is KIT n)
 *   39 UKIT_GET    slot                             -> slot, used, pack7 kit (196 B)
 *   40 UKIT_PUT    slot, pack7 kit (used 0: erase)  -> slot, rc (0 ok, 1 args, 2 flash, 3 playing)
 *   41 UKIT_OP     slot, op                         -> slot, op, rc; op 0 load into the project, 1 erase,
 *                                                      2 store the project's lanes (a name after it, from an
 *                                                      older editor, is ignored); op 3 (rename) is gone: rc 1
 *   42 SMP_READ    slot, off (3 x 7 bit), n (2 x 7 bit, <= 256) -> slot, off, pack7 bytes of the slot's flash
 * Flash writes (40, 41 ops 1..3) are refused while the transport plays (an erase stops the audio).
 * Version 2 of 36, 37, 39, 40 (ed_dsend.c) carries the lanes' sends too. */
enum { ED_DRUM_LANES = 36, ED_DRUM_LANE, ED_UKIT_LIST, ED_UKIT_GET, ED_UKIT_PUT, ED_UKIT_OP, ED_SMP_READ };

static void ed_pack7(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(p[j] >> 7) << j;
        ed_b(m);
        for (j = 0; j < k; j++)
            ed_b(p[j]);
        p += k;
        n -= k;
    }
}

/* one lane as 12 bytes: offsets, source, reference */
static void ed_lane_get(uint32_t l, uint8_t *b)
{
    memcpy(b, dl.ofs[l], DE_N);
    b[8] = dl.src[l];
    memcpy(b + 9, dl.ref[l], 3);
}

#include "ed_dsend.c"         /* version 2: the lanes with their sends */

/* a drum command; 0 = not one of them (or not in this build): no reply */
static int ed_drums(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    if (ed_dsend(cmd, a, na))
        return 1;
    switch (cmd) {
#if DL_ANY
    case ED_DRUM_LANES:
        if (na) {
            dlanes_t n;
            if (ed_unpack7(a, na, (uint8_t *)&n, sizeof n) != sizeof n)
                return 0;
            dl_fix(&n);
            fm1_irq_off();
            dl = n;
            fm1_irq_on();
            ui.force = 1;
        }
        ed_pack7((const uint8_t *)&dl, sizeof dl);
        return 1;
    case ED_DRUM_LANE: {
        uint8_t b[12];
        if (na < 1u || a[0] >= DRUM_LANES)
            return 0;
        if (na > 1u) {
            dlanes_t n = dl;
            if (ed_unpack7(a + 1, na - 1u, b, sizeof b) != sizeof b)
                return 0;
            memcpy(n.ofs[a[0]], b, DE_N);
            n.src[a[0]] = b[8];
            memcpy(n.ref[a[0]], b + 9, 3);
            dl_fix(&n);
            fm1_irq_off();
            dl = n;
            fm1_irq_on();
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_lane_get(a[0], b);
        ed_pack7(b, sizeof b);
        return 1;
    }
    case ED_SMP_READ: {                                     /* (export: a user sample with its kit) */
        uint32_t off, n;
        if (na < 6u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return 0;
        off = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        n = (uint32_t)a[4] | (uint32_t)a[5] << 7;
        if (n > 256u || off + n > SMP_USER_CAP(a[0]))
            n = off < SMP_USER_CAP(a[0]) ? (SMP_USER_CAP(a[0]) - off > 256u ? 256u : SMP_USER_CAP(a[0]) - off) : 0u;
        ed_b(a[0]);
        ed_b(off);
        ed_b(off >> 7);
        ed_b(off >> 14);
        ed_pack7(smp_user_xip(a[0]) + off, n);
        return 1;
    }
#endif
#if FELUCCA_DRUM_KITS
    case ED_UKIT_LIST: {
        uint32_t u;
        ed_b(UK_N);
        for (u = 0; u < UK_N; u++)
            ed_b((uint32_t)ukit_used(u));
        return 1;
    }
    case ED_UKIT_GET: {
        ukit_t k;
        if (na < 1u || a[0] >= UK_N)
            return 0;
        memset(&k, 0, sizeof k);
        ed_b(a[0]);
        ed_b((uint32_t)ukit_get(a[0], &k));
        ed_pack7((const uint8_t *)&k, sizeof k);
        return 1;
    }
    case ED_UKIT_PUT: {
        ukit_t k;
        uint32_t rc = 1;
        if (na < 1u)
            return 0;
        if (a[0] < UK_N && ed_unpack7(a + 1, na - 1u, (uint8_t *)&k, sizeof k) == sizeof k &&
            (!k.used || uk_valid(&k)))
            rc = song.playing || transport_req ? 3u : ukit_put(a[0], k.used ? &k : 0) ? 2u : 0u;
        ed_b(a[0]);
        ed_b(rc);
        return 1;
    }
    case ED_UKIT_OP: {
        uint32_t rc = 1;
        if (na < 2u)
            return 0;
        if (a[0] < UK_N && a[1] < 3u) {
            if (a[1] == 0u)
                rc = ukit_load(a[0]) ? 0u : 1u;
            else if (song.playing || transport_req)
                rc = 3;
            else if (a[1] == 1u)
                rc = !ukit_used(a[0]) ? 1u : ukit_put(a[0], 0) ? 2u : 0u;
            else
                rc = ukit_store(a[0]) ? 2u : 0u;
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_b(a[1]);
        ed_b(rc);
        return 1;
    }
#endif
    default:
        break;
    }
    (void)a, (void)na;
    return 0;
}
