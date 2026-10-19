/* SPDX-License-Identifier: GPL-3.0-only */
/* Bluetooth audio as the screen drives it (builder item BT_AUDIO; SYSTEM > Bluetooth's AUDIO MAN / AUDIO AUTO, op_project.c).
 * Included by bt_inq.c after bt_page.c (it calls the inquiry, the page and the ACL stage of the Classic stack). Not the
 * console's: nothing here prints.
 *   bta_start(m)  an AUDIO mode on: BLE MIDI off (one engine on the radio at a time, docs/BT-CLASSIC.md); what connects
 *                 is the screen's (ui/optimist/op_bt.c: SAVE, AUTO at boot)
 *   bta_scan(on)  the devices list: an inquiry (BTA_SCAN_S), what answers listed
 *   bta_page(a)   page device a (the last one)
 *   bta_pick(i)   page found device i (it becomes the last one)
 *   bta_stop()    the run or the link ends (an AUDIO mode off)
 *   bta_state()   what the link does, for the STATE line (BTA_S_*)
 * The devices come from the inquiry's FHS and EIR (bt_inq_con.c bti_print_rx calls bta_saw / bta_named). The last
 * device is kept in the settings record (op_bt.c bt_rec_out). Hardware-untested:
 * the radio handed from BLE to BR/EDR and back is not measured, and the security step (SSP, E0) is not written, so a
 * real sink refuses the audio channel (BTA_S_REFUSED) */
/* (the states, the device list and the calls are in bt_audio.h: the screen, included before this, reads them) */
static int bta_find(const uint8_t a[6])                 /* its index in the list, -1 */
{
    uint32_t i;
    for (i = 0; i < bta.n; i++)
        if (ble_eq(bta.d[i].addr, a, 6))
            return (int)i;
    return -1;
}
static void bta_saw(const uint8_t a[6], uint32_t cod)   /* an FHS heard (bt_inq_con.c): a device answered the scan */
{
    struct bta_dev *d;
    if (bta.mode == BTA_OFF || bta_find(a) >= 0 || bta.n >= BTA_DEVS)
        return;
    d = &bta.d[bta.n++];
    ble_cpy(d->addr, a, 6);
    d->audio = (uint8_t)(((cod >> 8) & 0x1Fu) == 0x04u);   /* (major class 4: Audio / Video) */
    d->name[0] = 0;
}
static void bta_named(const uint8_t a[6], const char *nm)   /* its EIR's name (bt_inq_con.c) */
{
    int i = bta_find(a);
    if (i >= 0)
        str_cpy(bta.d[i].name, nm, sizeof bta.d[i].name);
}
static void bta_stop(void)
{
    if (bti_busy())
        bti_run_stop();
    bta.mode = BTA_OFF;
}
static void bta_hangup(void)                            /* the link (or the page) ends, the mode stays: SAVE connects again */
{
    if (bti_busy())
        bti_run_stop();
    bta.tried = 0;
}
static int bta_page(const uint8_t a[6])
{
    if (bti_busy())
        bti_run_stop();
    bta.tried = 1;
    return bp_start(a, BTA_PAGE_SLOTS);
}
static void bta_start(uint32_t m)
{
#if FELUCCA_BLE
    ble_midi_set(0);                                    /* (the radio is BR/EDR's while an AUDIO mode runs) */
#endif
    bta_stop();
    bta.mode = (uint8_t)m;
    bta.tried = 0;
}
static void bta_scan(int on)                            /* the devices list open (an inquiry, BTA_SCAN_S) or closed */
{
    if (on) {
        if (bti_busy())
            bti_run_stop();
        bta.n = 0, bta.cur = 0;
        (void)bti_run_start(BTI_M_INQ, BTA_SCAN_S * 1600u);
    } else if (bti.mode == BTI_M_INQ && bti_busy())
        bti_run_stop();
}
static void bta_pick(uint32_t i)                        /* SAVE on the DEVICE line in AUDIO MAN */
{
    if (i >= bta.n)
        return;
    ble_cpy(bta.last, bta.d[i].addr, 6);
    str_cpy(bta.last_name, bta.d[i].name[0] ? bta.d[i].name : "DEVICE", sizeof bta.last_name);
    bta.have_last = 1;
    (void)bta_page(bta.d[i].addr);
}
static uint32_t bta_state(void)
{
    if (bta.mode == BTA_OFF)
        return BTA_S_OFF;
    if (bti.mode == BTI_M_INQ && bti_st.phase == BTI_INQ_RUN)
        return BTA_S_SCANNING;
    if (bti.mode == BTI_M_CONN && bti_st.phase == BTI_INQ_RUN)
        return ba.stage == BA_AV_READY ? BTA_S_STREAMING : ba.stage == BA_FAILED ? BTA_S_REFUSED
             : ba.stage >= BA_AV_SIG ? BTA_S_SETUP : BTA_S_LINKED;
    if (bti.mode == BTI_M_PAGE && bti_st.phase == BTI_INQ_RUN)
        return BTA_S_PAGING;
    if (bta.mode == BTA_AUTO)
        return !bta.have_last ? BTA_S_NO_LAST : bta.tried ? BTA_S_FAILED : BTA_S_READY;
    if (bta.tried)
        return BTA_S_FAILED;                            /* (paged, no answer; or the link closed) */
    return bta.n ? BTA_S_FOUND : BTA_S_NONE;
}
static const char *bta_dev_name(uint32_t i)             /* found device i as shown: its name, else its address's last bytes */
{
    static char b[17];
    const struct bta_dev *d;
    if (i >= bta.n)
        return "NONE";
    d = &bta.d[i];
    if (d->name[0])
        return d->name;
    str_cpy(b, "DEVICE ", sizeof b);
    {
        static const char HX[] = "0123456789ABCDEF";
        uint32_t k = str_len(b);
        b[k++] = HX[d->addr[1] >> 4], b[k++] = HX[d->addr[1] & 15u], b[k++] = HX[d->addr[0] >> 4], b[k++] = HX[d->addr[0] & 15u];
        b[k] = 0;
    }
    return b;
}
