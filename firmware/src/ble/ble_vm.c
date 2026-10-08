/* SPDX-License-Identifier: GPL-3.0-only */
/* The stored RF calibration: stock V15's VM read in place, and Optimist's copy of it (ble_vm.h; docs/BLE-HW-FACTS.md
 * §14, §15.4 on feat/ble-facts d907ce3). Read only: nothing here writes or erases flash. */
#include "ble_vm.h"
#include "ble_util.h"

_Static_assert(sizeof(struct ble_rf_trims) == BLE_RF_TRIMS_SIZE, "the four RF records: 2 + 7 + 20 + 68 bytes");

#define VM_CHUNK 32u
enum { VM_MARK_A = 0xAA, VM_MARK_B = 0xAB };      /* the copy's first byte: the area it was taken from */
static const uint16_t vm_ids[4] = {106, 107, 108, 187};
static const uint8_t vm_lens[4] = {2, 7, 20, 68};

BLE_API uint32_t ble_crc16_xmodem(uint32_t crc, const uint8_t *p, uint32_t n)   /* poly 0x1021, init 0 (§14.2) */
{
    uint32_t k;
    while (n--) {
        crc ^= (uint32_t)*p++ << 8;
        for (k = 0; k < 8u; k++)
            crc = crc & 0x8000u ? (crc << 1) ^ 0x1021u : crc << 1;
        crc &= 0xFFFFu;
    }
    return crc;
}

static uint8_t *vm_slot(struct ble_rf_trims *t, uint32_t i)
{
    return i == 0 ? t->x106 : i == 1u ? t->x107 : i == 2u ? t->x108 : t->x187;
}

static int vm_187_ok(const uint8_t d[68])          /* the payload's own CRC, little-endian at 64 (§14.5) */
{
    return ble_crc16_xmodem(0, d, 64) == (uint32_t)(d[64] | d[65] << 8);
}

BLE_API uint32_t ble_vm_live(ble_vm_read_fn rd, void *ctx)   /* §14.3 step 1: A when both are marked */
{
    static const uint8_t magic[4] = {0x55, 0xAA, 0xAA, 0x55};
    uint8_t m[4];
    uint32_t a;
    for (a = BLE_VM_BASE; a <= BLE_VM_BASE + BLE_VM_AREA; a += BLE_VM_AREA)
        if (!rd(ctx, a, m, 4) && ble_eq(m, magic, 4))
            return a;
    return 0;
}

/* one record's data through the check (and into keep when wanted) -> 1 the check holds, 0 not, -1 a read failed */
static int vm_data(ble_vm_read_fn rd, void *ctx, uint32_t at, uint32_t len, uint8_t check, uint8_t *keep)
{
    uint8_t c[VM_CHUNK];
    uint32_t pos = 0, crc = 0, k;
    while (pos < len) {
        k = ble_min(VM_CHUNK, len - pos);
        if (rd(ctx, at + pos, c, k))
            return -1;
        crc = ble_crc16_xmodem(crc, c, k);
        if (keep)
            ble_cpy(keep + pos, c, k);
        pos += k;
    }
    return (crc & 0xFFu) == check;
}

BLE_API int ble_vm_scan(ble_vm_read_fn rd, void *ctx, struct ble_vm_info *info, struct ble_rf_trims *t,
                        ble_vm_visit_fn visit, void *vctx)
{
    uint8_t h[4], tmp[68];
    uint32_t area = ble_vm_live(rd, ctx), off = 4, id, len, i;
    int ok;
    ble_zero((uint8_t *)info, sizeof *info);
    info->area = area;
    if (!area)
        return 0;
    while (off + 4u <= BLE_VM_AREA) {                       /* §14.2: records back to back from area + 4 */
        if (rd(ctx, area + off, h, 4)) {
            info->read_err = 1;
            break;
        }
        id = h[1] | (h[2] & 0x0Fu) << 8;
        len = (uint32_t)(h[2] >> 4) | (uint32_t)h[3] << 4;
        if (off + 4u + len > BLE_VM_AREA)                   /* past the area: the end */
            break;
        for (i = 0; i < 4u && vm_ids[i] != id; i++)
            ;
        ok = vm_data(rd, ctx, area + off + 4u, len, h[0], i < 4u && len == vm_lens[i] ? tmp : 0);
        if (ok < 0) {
            info->read_err = 1;
            break;
        }
        if (!ok)                                             /* the first failing check ends the log (§14.3) */
            break;
        info->nrec++;
        if (visit)
            visit(vctx, area + off, id, len);
        if (i < 4u) {                                        /* the last valid record of an id wins */
            if (len == vm_lens[i]) {
                ble_cpy(vm_slot(t, i), tmp, len);
                info->have |= (uint8_t)(1u << i);
                info->wrong_len &= (uint8_t)~(1u << i);
            } else {
                info->have &= (uint8_t)~(1u << i);
                info->wrong_len |= (uint8_t)(1u << i);
            }
        }
        off += 4u + len;
    }
    info->end = off;
    info->ok187 = (uint8_t)((info->have & 8u) && vm_187_ok(t->x187));
    return info->have == BLE_VM_ALL && info->ok187 && !info->read_err;
}

/* ---------------------------------------------------------------------------------------------- the copy --- */

static uint32_t copy_crc(const uint8_t *c) { return ble_crc16_xmodem(0, c, BLE_RF_COPY_SIZE - 2u); }

BLE_API int ble_rf_copy_ok(const uint8_t copy[BLE_RF_COPY_SIZE])
{
    return (copy[0] == VM_MARK_A || copy[0] == VM_MARK_B) &&
           copy_crc(copy) == (uint32_t)(copy[BLE_RF_COPY_SIZE - 2] | copy[BLE_RF_COPY_SIZE - 1] << 8) &&
           vm_187_ok(copy + 1 + 29);                         /* (187 sits after 106, 107, 108: 2 + 7 + 20) */
}

BLE_API void ble_rf_copy_make(uint8_t copy[BLE_RF_COPY_SIZE], const struct ble_rf_trims *t, uint32_t area)
{
    uint32_t c;
    copy[0] = area == BLE_VM_BASE + BLE_VM_AREA ? VM_MARK_B : VM_MARK_A;
    ble_cpy(copy + 1, (const uint8_t *)t, BLE_RF_TRIMS_SIZE);
    c = copy_crc(copy);
    copy[BLE_RF_COPY_SIZE - 2] = (uint8_t)c;
    copy[BLE_RF_COPY_SIZE - 1] = (uint8_t)(c >> 8);
}

BLE_API void ble_rf_copy_get(const uint8_t copy[BLE_RF_COPY_SIZE], struct ble_rf_trims *t)
{
    ble_cpy((uint8_t *)t, copy + 1, BLE_RF_TRIMS_SIZE);
}

BLE_API int ble_boot_radio(int on, uint32_t boot_failed, int have_trims)
{
    return on && !boot_failed && have_trims;
}

BLE_API int ble_rf_choose(int vm_complete, const struct ble_rf_trims *vm, uint32_t vm_area,
                          uint8_t copy[BLE_RF_COPY_SIZE], struct ble_rf_trims *use, int *save)
{
    *save = 0;
    if (vm_complete) {                                       /* §15.4 step 3: the VM first */
        ble_cpy((uint8_t *)use, (const uint8_t *)vm, BLE_RF_TRIMS_SIZE);   /* (no struct copy: no memcpy here) */
        if (!ble_rf_copy_ok(copy) || !ble_eq(copy + 1, (const uint8_t *)vm, BLE_RF_TRIMS_SIZE)) {
            ble_rf_copy_make(copy, vm, vm_area);             /* step 2: kept (again) once the set is complete */
            *save = 1;
        }
        return BLE_RF_FROM_VM;
    }
    if (ble_rf_copy_ok(copy)) {                              /* then Optimist's copy */
        ble_rf_copy_get(copy, use);
        return BLE_RF_FROM_COPY;
    }
    return BLE_RF_NONE;                                      /* then none: the radio stays off */
}
