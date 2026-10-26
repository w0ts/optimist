/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Bluetooth audio's remembered devices (the user, 2026-10-24: "remember up to 4 BT audio devices"): bta.mem, the last one
 * first, each with its link key; AUTO pages them in that order (bta.page_i, bta_next) and the one that answers goes first
 * (bta_settle); the devices list's rows. Pure: the bta state only (bt_audio.h). Included by bt_audio.c, and by the host
 * UI tests' stand-in (tests/bta_ui_stubs.h). */
static struct bta_mem *bta_mem_of(const uint8_t a[6])  /* device a remembered: its entry (its key), else 0 */
{
    uint32_t i;
    for (i = 0; i < bta.nmem; i++)
        if (ble_eq(bta.mem[i].addr, a, 6))
            return &bta.mem[i];
    return 0;
}
static void bta_front(uint32_t i)                       /* remembered device i becomes the last one, the others in order */
{
    struct bta_mem t = bta.mem[i];
    for (; i; i--)
        bta.mem[i] = bta.mem[i - 1u];
    bta.mem[0] = t;
    bta.page_i = 0;
    bta.key_new = 1;                                    /* (op_bt.c bt_poll: the settings record saved) */
}
/* device a (its name nm) becomes the last one: its key kept when it was remembered; full, the oldest one goes */
static void bta_remember(const uint8_t a[6], const char *nm)
{
    struct bta_mem *m = bta_mem_of(a);
    if (!m) {
        m = &bta.mem[bta.nmem < BTA_MEMS ? bta.nmem++ : BTA_MEMS - 1u];
        memset(m, 0, sizeof *m);
        ble_cpy(m->addr, a, 6);
    }
    str_cpy(m->name, nm[0] ? nm : "DEVICE", sizeof m->name);
    bta_front((uint32_t)(m - bta.mem));
}
static void bta_forget(uint32_t i)                      /* remembered device i forgotten, its key with it */
{
    if (i >= bta.nmem)
        return;
    for (bta.nmem--; i < bta.nmem; i++)
        bta.mem[i] = bta.mem[i + 1u];
    memset(&bta.mem[bta.nmem], 0, sizeof bta.mem[0]);
    bta.page_i = 0;
    bta.key_new = 1;
}
static int bta_next(void)                               /* AUTO, no answer: the next remembered device to page -> 0: no other */
{
    if (bta.nmem < 2u)
        return 0;
    bta.page_i = (uint8_t)((bta.page_i + 1u) % bta.nmem);
    return 1;
}
static void bta_settle(int up)                          /* a connect over: up, the device it reached goes first */
{
    if (up && bta.page_i && bta.page_i < bta.nmem)
        bta_front(bta.page_i);
    bta.page_i = 0;
}
/* the devices list's rows: the remembered devices, then those the scan found and not remembered */
static uint32_t bta_rows(void)
{
    uint32_t i, n = bta.nmem;
    for (i = 0; i < bta.n; i++)
        n += !bta_mem_of(bta.d[i].addr);
    return n;
}
/* row i: a remembered device -> its index, else -1 - the found device's index (-1 - BTA_DEVS: none) */
static int bta_row(uint32_t i)
{
    uint32_t k;
    if (i < bta.nmem)
        return (int)i;
    i -= bta.nmem;
    for (k = 0; k < bta.n; k++)
        if (!bta_mem_of(bta.d[k].addr) && !i--)
            return -1 - (int)k;
    return -1 - (int)BTA_DEVS;
}
