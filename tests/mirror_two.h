/* SPDX-License-Identifier: GPL-3.0-only */
/* Two clients at once (2026-11-01, on the user's FM-1: the Mirror app over Bluetooth kept SYSTEM > INFO's last line, "GUARD OFF",
 * at the foot of HOME while the FM-1's own screen was clean, during a USB script that mirrored, navigated and turned its mirror
 * off). Included by tests/mirror_test.c after its stand-ins: the Bluetooth client (the app: checked frames, its lost seqs asked
 * again, the keep-alive each second, SUMS compared and the bands that differ asked, "SUMS wanted" after 3 s without) mirrors
 * all along; a USB client (tools/fm1_drive.py, a measurement script) turns its mirror on with a catch-up and STATE, keeps it
 * alive, the page changes (INFO -> HOME), lets go and turns its mirror off. The Bluetooth client's picture is the panel's
 * within the self-check's time, whatever the USB client did, and USB's 106 0 leaves the Bluetooth client's mirror on (it
 * switched it off before: its keys let go, the self-check reset, the whole panel again at its next keep-alive). The mirror is
 * still one (the link of the last 106): the two take turns, each turn a catch-up whole.
 * Then a page's clear the Bluetooth client never got (lost on the way, or dropped while behind by the lag rule): the model's
 * HOME draws nothing under its rows in a repaint (as the UI's HOME drew neither the help band nor the load lines' strip, the
 * rows INFO's GUARD line sat in), so only the panel sent whole (MIR_WHY_PAGE) brings them right. Each client its own decoder
 * (mirror_dec.h's state swapped in while its link's packets are read) */
typedef struct {
    uint16_t fb[240 * 240];
    __typeof__(md) d;
    uint8_t hf[sizeof hf];
    uint32_t hn;
    uint32_t sum_ms, opt_ms, alive_ms, sums, compared, bands, asks, fixes;
    uint32_t mask;                      /* bands that differ, to ask */
} mt_client;
static mt_client mtc_b, mtc_u, *mtc_cur;
static void mtc_use(mt_client *c)
{
    if (c == mtc_cur)
        return;
    if (mtc_cur) {
        memcpy(mtc_cur->fb, md_fb, sizeof md_fb);
        mtc_cur->d = md;
        memcpy(mtc_cur->hf, hf, sizeof hf);
        mtc_cur->hn = hn;
    }
    memcpy(md_fb, c->fb, sizeof md_fb);
    md = c->d;
    memcpy(hf, c->hf, sizeof hf);
    hn = c->hn;
    mtc_cur = c;
}
static mt_client *mtc_was;
static void mtc_usb_side(int in)
{
    if (in)
        mtc_was = mtc_cur, mtc_use(&mtc_u);
    else if (mtc_was)
        mtc_use(mtc_was);
}
static void mtc_sums(const uint8_t *f, uint32_t n)   /* (the app's MirrorLink: compared when it has every frame before) */
{
    uint32_t ref = 0;
    int32_t d;
    if (mtc_cur != &mtc_b)
        return;
    mtc_b.sum_ms = fm1_ms, mtc_b.sums++;
    if (!md.have_seq || md.broken || md.fix_have || f[6] != md.seq)
        return;
    d = msum_client(md_fb, f, n, &ref);
    mtc_b.compared++;
    if (d > 0)
        mtc_b.mask |= (uint32_t)d;
}
static void mtc_bt_req(uint32_t bits, const uint8_t *x, uint32_t nx)   /* 106 over Bluetooth */
{
    uint8_t f[16] = {0xF0, 0x7D, 0x46, 0x4C, 106, (uint8_t)bits};
    uint32_t n = 6, i;
    for (i = 0; i < nx; i++)
        f[n++] = x[i];
    f[n++] = 0xF7;
    bt_send(f, n);
}
static void mtc_bands(uint32_t m)
{
    uint8_t x[5];
    uint32_t k;
    for (k = 0; k < 5u; k++)
        x[k] = (uint8_t)(m >> (7u * k) & 127u);
    mtc_bt_req(MSUM_ASK, x, 5);
}
/* the Bluetooth client after the radio: lost seqs asked by run, the bands that differ, the keep-alive, SUMS wanted */
static void mtc_b_tick(void)
{
    uint32_t q;
    mtc_use(&mtc_b);
    for (q = 0; q < md.fix_have; q++) {
        uint8_t x[2] = {(uint8_t)md.fix_lo[q], (uint8_t)md.fix_hi[q]};
        mtc_bt_req(1 | MIR_CHK_BIT | MIR_FIX_BIT, x, 2);
        mtc_b.fixes++;
    }
    md.fix_have = 0;
    if (md.broken && !md.have_seq)
        md.broken = 0;
    if (mtc_b.mask) {
        mtc_bands(mtc_b.mask);
        mtc_b.bands += (uint32_t)__builtin_popcount(mtc_b.mask);
        mtc_b.mask = 0, mtc_b.opt_ms = fm1_ms;
    }
    if (fm1_ms - mtc_b.alive_ms >= 1000u) {
        mtc_b.alive_ms = fm1_ms;
        mtc_bt_req(1 | MIR_CHK_BIT, 0, 0);
        mtc_b.asks++;
        if (fm1_ms - mtc_b.sum_ms > 3000u && fm1_ms - mtc_b.opt_ms > 3000u)
            mtc_bands(0), mtc_b.opt_ms = fm1_ms;
    }
}
/* the USB client: 106 as tools/fm1_drive.py sends it (one byte: no checked frames, no SUMS) */
static void mtc_usb_ask(uint32_t bits)
{
    uint8_t a[1] = {(uint8_t)bits};
    nrep = 0;
    ed_bt = 0;
    ed_mirror(a, 1);
}
/* two pages the UI draws: INFO, a list to the screen's foot (its last line low), and HOME, rows over black */
static uint32_t mtc_page;
static uint16_t mtc_clock;              /* (the header's clock, drawn with every page and repaint: every row a page owns drawn again) */
static void mtc_draw(uint32_t page, uint32_t whole)
{
    uint32_t y, s = rnd_s;
    rnd_s = 77u + page;
    if (whole)
        lcd_fill(0, 0, 240, 240, 0);    /* (a page drawn whole: ui.force's clear) */
    lcd_fill(0, 0, 240, 25, 0);         /* (the header, its clock) */
    lcd_fill(200, 3, 30, 10, mtc_clock);
    for (y = 25; y + 19u <= (page ? 200u : 237u); y += 19u) {
        canvas(240, 18, 3);
        lcd_blit(0, y, 240, 18, cv);
    }
    rnd_s = s;                          /* (HOME: nothing under its rows, the help band's black from the page's clear only:
                                         * op_draw.c with PARAM_HELP, the band drawn by the help line alone) */
}
static void mtc_ui_frame(void)          /* (op_input.c: the frame the self-check hashes, between mir_draw_begin and end) */
{
    mir_draw_begin();
    if (mt_due) {
        mt_due = 0;
        repaints++;
        mtc_draw(mtc_page, 0);
    }
    mir_draw_end();
}
static uint32_t mtc_wrong_ms, mtc_wrong_max, mtc_offs;
static int mtc_b_same(void)
{
    mtc_use(&mtc_b);
    return !memcmp(panel, md_fb, sizeof panel);
}
/* one 16 ms pass: the main loop's mir_poll, a UI frame, both links read, the Bluetooth client's answers */
static void mtc_step(void)
{
    fm1_ms += 16u;
    mir_poll();
    mtc_ui_frame();
    host_drain();
    mtc_use(&mtc_b);
    radio(~0u);
    mtc_b_tick();
    if (!mtc_b_same()) {
        mtc_wrong_ms += 16u;
        mtc_wrong_max = mtc_wrong_ms > mtc_wrong_max ? mtc_wrong_ms : mtc_wrong_max;
    } else
        mtc_wrong_ms = 0;
}
static void mtc_run(uint32_t ms)
{
    uint32_t t = fm1_ms + ms;
    while ((int32_t)(t - fm1_ms) > 0)
        mtc_step();
}
static void two_tests(void)
{
    uint32_t round, lossy;
    mt_sums = mtc_sums;
    mt_usb_side = mtc_usb_side;
    mt_defer = 1;
    memset(&mtc_b, 0, sizeof mtc_b);
    memset(&mtc_u, 0, sizeof mtc_u);
    mtc_cur = 0;
    mtc_use(&mtc_b);
    ble_out_on = 1;
    mtc_u.d.have_seq = 0;
    for (lossy = 0; lossy < 2u; lossy++)
        for (round = 0; round < 6u; round++) {
            uint32_t t;
            mir_ask(0, MIR_USB);        /* (from nothing: both clients off) */
            mir_poll();
            mtc_page = 0;
            mtc_draw(0, 1);
            mtc_b.alive_ms = fm1_ms - 1000u, mtc_b.sum_ms = mtc_b.opt_ms = fm1_ms - 4000u;
            mtc_use(&mtc_b);
            md.have_seq = 0, md.broken = 0, md.fix_have = 0;
            mtc_bt_req(1 | 2 | MIR_CHK_BIT, 0, 0);   /* (the app's first ask: the whole panel, checked frames) */
            mtc_run(3000);
            check(mtc_b_same(), "two clients (%u, %u): the Bluetooth client alone, SYSTEM INFO: the panel", lossy, round);
            mt_lossy = lossy;
            mtc_wrong_ms = mtc_wrong_max = 0;
            mtc_usb_ask(1 | 2 | 4);     /* (the USB script: on, the whole panel, STATE) */
            for (t = 0; t < 6000u; t += 16u) {
                if (t % 1000u == 500u)
                    mtc_usb_ask(1);     /* (its keep-alive) */
                if (t == 3952u && (round & 1u))
                    mtc_b.alive_ms = fm1_ms - 1000u;   /* (the app's keep-alive just before USB's end: the mirror on Bluetooth's link) */
                if (t == 1504u + 160u * round) {   /* (109 NAV / 108 PANEL from USB: the page changes, drawn whole) */
                    mtc_page = 1;
                    mtc_draw(1, 1);
                }
                if (t % 96u == 0u)
                    mtc_clock = (uint16_t)rnd(0x10000u), lcd_fill(200, 3, 30, 10, mtc_clock);   /* (the header's clock: something moves) */
                if (t == 4000u) {       /* (its end: let go, the mirror off) */
                    uint8_t a[3] = {4, 0, 0};
                    uint32_t bt = mir.on && mir.link == MIR_BT;
                    nrep = 0;
                    ed_panel(a, 3);
                    mtc_usb_ask(0);
                    if (bt) {           /* (the mirror on Bluetooth's link: USB's off is not its) */
                        mtc_offs++;
                        check(mir.on && mir.link == MIR_BT && !mir.behind,
                              "two clients (%u, %u): USB's 106 0 leaves the Bluetooth client's mirror on", lossy, round);
                    } else
                        check(!mir.on, "two clients (%u, %u): USB's 106 0 ends the mirror on its own link", lossy, round);
                }
                mtc_step();
            }
            mt_lossy = 0;
            mtc_run(4000);
            check(mtc_b_same(), "two clients (%u, %u): the Bluetooth client's picture is the panel after the USB client left", lossy, round);
            check(mtc_wrong_max <= 4000u, "two clients (%u, %u): the Bluetooth client's picture wrong at most 4 s (%u ms)", lossy, round,
                  mtc_wrong_max);
        }
    printf("two clients: the Bluetooth client: %u SUMS, %u compared, %u bands asked, %u lost runs asked; wrong for %u ms at most; "
           "USB's 106 0 while Bluetooth mirrored %u times\n", mtc_b.sums, mtc_b.compared, mtc_b.bands, mtc_b.fixes, mtc_wrong_max, mtc_offs);
    check(mtc_offs >= 3u, "two clients: USB's 106 0 came while the mirror was on Bluetooth's link (%u)", mtc_offs);
    /* a page's clear the Bluetooth client never got (lost on the way; dropped while behind, the lag rule): HOME's repaint draws only
     * its rows, so the rows INFO drew under them come again only with the panel whole; the client's picture the panel's */
    for (round = 0; round < 4u; round++) {
        uint32_t w0 = mst.whole[MIR_WHY_PAGE] + mfix_whole;
        mtc_page = 0;
        mtc_draw(0, 1);
        mtc_run(2000);
        check(mtc_b_same(), "a page's clear missed (%u): the Bluetooth client on SYSTEM INFO, the panel", round);
        if (round & 1u)
            mir_behind(MIR_WHY_LAG);    /* (the lag rule refused a frame: behind) */
        else
            mt_lose_whole = 1;
        mtc_page = 1;
        mtc_draw(1, 1);
        mtc_run(3000);
        check(mtc_b_same(), "a page's clear missed (%s): the Bluetooth client's picture the panel's within 3 s",
              round & 1u ? "drawn while behind" : "lost on the way");
        check(mst.whole[MIR_WHY_PAGE] + mfix_whole > w0, "a page's clear missed (%u): the panel sent whole", round);
        mt_lose_whole = 0;
    }
    mtc_use(&mtc_b);
    mt_sums = 0;
    mt_usb_side = 0;
    mt_defer = 0;
    mtc_cur = 0;
    mtc_bt_req(0, 0, 0);
    mir_poll();
    radio(~0u);
    ble_out_on = 0;
    me.chk = 0;
}
