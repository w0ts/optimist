/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol, version 2 of the drum lane and user kit commands: the lanes with their sends (included by
 * ed_drums.c; web/EDITOR_PROTOCOL.md "Drum lanes and user kits"). Each v2 form is one an older firmware does
 * not answer (36: no reply to the lone version byte; 37, 39: lane / slot >= 16, no reply; 40: rc 1), so an
 * editor asks 36 v2 first and keeps to version 1 without a reply. Sends per lane, 3 bytes: REV, DLY, CHO (0..31;
 * REV is signed: -1 was TRK, the drum track's REV, until 2026-10: never sent now, read as 4, a lane as it is).
 *   36 v2  2 (get) | 2, pack7 lanes + sends (204 + 48 B, set)   -> 2, pack7 lanes + sends (252 B)
 *   37 v2  0x40 + lane [, pack7 lane + sends (12 + 3 B)]         -> 0x40 + lane, pack7 (15 B)
 *   39 v2  0x40 + slot                                           -> 0x40 + slot, used, pack7 kit + sends (196 + 48 B)
 *   40 v2  0x40 + slot, pack7 kit + sends (196 + 48 B; used 0: erase) -> 0x40 + slot, rc (as 40) */
#define ED_V2 0x40u
#define ED_DL2_N (sizeof(dlanes_t) + 3u * DRUM_LANES)          /* 252 */

static void ed_snd_get(const uint16_t *w, uint8_t *b, uint32_t n)   /* n words -> 3n bytes */
{
    uint32_t l;
    for (l = 0; l < n; l++, b += 3) {
        b[0] = (uint8_t)dsend_rev(w[l]);
        b[1] = (uint8_t)dsend_dly(w[l]);
        b[2] = (uint8_t)dsend_cho(w[l]);
    }
}
static void ed_snd_set(uint16_t *w, const uint8_t *b, uint32_t n)   /* 3n bytes -> n words (clamped) */
{
    uint32_t l;
    for (l = 0; l < n; l++, b += 3)
        w[l] = dsend_word((int8_t)b[0] < 0 ? DSEND_DEF : (uint32_t)clamp(b[0], 0, DSEND_MAX),
                          (uint32_t)clamp(b[1], 0, DSEND_MAX), (uint32_t)clamp(b[2], 0, DSEND_MAX));
}

/* a v2 drum command: 1 = handled (replied), 0 = not a v2 form (ed_drums.c goes on) */
static __attribute__((noinline)) int ed_dsend(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint8_t b[ED_DL2_N];
    if (cmd == ED_DRUM_LANES && na && a[0] == 2u && (na == 1u || na == 1u + ED_DL2_N + (ED_DL2_N + 6u) / 7u)) {
        if (na > 1u) {
            dlrec_t n;
            if (ed_unpack7(a + 1, na - 1u, b, sizeof b) != sizeof b)
                return 0;
            memcpy(&n.l, b, sizeof n.l);
            ed_snd_set(n.snd, b + sizeof n.l, DRUM_LANES);
            fm1_irq_off();
            dlrec_apply(&n, DRREV_MOVED);
            fm1_irq_on();
            ui.force = 1;
        }
        memcpy(b, &dl, sizeof dl);
        ed_snd_get(dsend, b + sizeof dl, DRUM_LANES);
        ed_b(2);
        ed_pack7(b, sizeof b);
        return 1;
    }
    if (cmd == ED_DRUM_LANE && na && (a[0] & ~15u) == ED_V2) {
        uint32_t l = a[0] & 15u;
        if (na > 1u) {
            dlanes_t n = dl;
            uint16_t w;
            if (ed_unpack7(a + 1, na - 1u, b, 15u) != 15u)
                return 0;
            memcpy(n.ofs[l], b, DE_N);
            n.src[l] = b[8];
            memcpy(n.ref[l], b + 9, 3);
            dl_fix(&n);
            ed_snd_set(&w, b + 12, 1);
            fm1_irq_off();
            dl = n;
            dsend[l] = w;
            fm1_irq_on();
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_lane_get(l, b);
        ed_snd_get(&dsend[l], b + 12, 1);
        ed_pack7(b, 15u);
        return 1;
    }
#if FELUCCA_DRUM_KITS
#define ED_KIT2 ((uint32_t)sizeof(ukit_t) + 3u * DRUM_LANES)   /* a kit (196 B) and its sends (48 B) */
    if (cmd == ED_UKIT_GET && na && (a[0] & ~15u) == ED_V2) {
        ukit_t k;
        uint16_t w[DRUM_LANES];
        uint32_t u = a[0] & 15u, used;
        memset(&k, 0, sizeof k);
        memset(w, 0, sizeof w);
        used = (uint32_t)(ukit_get(u, &k) && ukit_sends(u, w));
        memcpy(b, &k, sizeof k);
        ed_snd_get(w, b + sizeof k, DRUM_LANES);
        ed_b(a[0]);
        ed_b(used);
        ed_pack7(b, ED_KIT2);
        return 1;
    }
    if (cmd == ED_UKIT_PUT && na && (a[0] & ~15u) == ED_V2) {
        ukit_t k;
        uint16_t w[DRUM_LANES];
        uint32_t rc = 1;
        if (ed_unpack7(a + 1, na - 1u, b, ED_KIT2) == ED_KIT2) {
            memcpy(&k, b, sizeof k);
            ed_snd_set(w, b + sizeof k, DRUM_LANES);
            if (!k.used || uk_valid(&k))
                rc = song.playing || transport_req ? 3u : ukit_put_snd(a[0] & 15u, k.used ? &k : 0, w) ? 2u : 0u;
        }
        ed_b(a[0]);
        ed_b(rc);
        return 1;
    }
#endif
    (void)a, (void)na, (void)cmd, (void)b;
    return 0;
}
