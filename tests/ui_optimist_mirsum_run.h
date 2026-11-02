/* MIRROR_SUMS=1 (MIRROR_CHECK): the mirror's self-check over a lossy link. The real UI (the mirror's workload, then random use)
 * drawn through the encoder as ed_mirror.c sends it over Bluetooth (no meters), the frames on a link of MS_RATE bytes a second
 * that loses ~2 frames and changes a byte of ~1 (a bad sum) each MS_RATE bytes (the app's log, 2026-11-01: 1-4 gaps and ~1 bad
 * sum a second at 7.4-9.7 KB/s), the lag rule (MS_LAG waiting: frames refused, the panel repainted in place once clear), the
 * client's repairs by seq (its rectangles forgotten, the panel repainted), the marks, and SUMS: a check about once a second
 * (soon after a page, a repair or a catch-up), the client's differing bands asked again. Each pixel the client holds is
 * compared with the device's (mt_fb) every frame: how long any stays wrong. MIRROR_SUMS=off: the same without SUMS */
#ifdef MIRROR_CHECK
static uint32_t ms_since[240 * 240];                    /* fm1_ms + 1 when the client's pixel went wrong, 0: right */
static uint32_t ms_hist[6], ms_max, ms_frames, ms_inject;
static const uint32_t MS_EDGE[5] = {500, 1000, 1500, 2000, 3000};   /* the histogram's edges (ms): <=0.5 s .. >3 s */
static void ms_episode(uint32_t age)
{
    uint32_t k = 0;
    while (k < 5u && age > MS_EDGE[k])
        k++;
    ms_hist[k]++;
    ms_max = age > ms_max ? age : ms_max;
}
static void ms_compare(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        if (md_fb[i] != mt_fb[i]) {
            if (!ms_since[i])
                ms_since[i] = fm1_ms + 1u;
        } else if (ms_since[i]) {
            ms_episode(fm1_ms + 1u - ms_since[i]);
            ms_since[i] = 0;
        }
    }
}
static void ms_bands(uint32_t diff)                     /* the client's bands request (106 bits 112): those rows forgotten, repainted */
{
    uint32_t b;
    for (b = 0; b < MSUM_BANDS; b++)
        if (diff >> b & 1u)
            mir_enc_forget(0, b * MSUM_ROWS, 240, MSUM_ROWS), ms.ask_bands++;
    ms.asks++;
    ms.fix_due = 1;
}
static void ms_fix(void)                                /* the client's lost runs (106 bit 4): ed_mirror.c mir_fix */
{
    uint32_t r, k, n, whole = 0;
    for (r = 0; r < md.fix_have && !whole; r++) {
        n = ((md.fix_hi[r] - md.fix_lo[r]) & 127u) + 1u;
        for (k = 0; k < n && !whole; k++) {
            uint32_t sq = (md.fix_lo[r] + k) & 127u, e = sq & (MIR_RR - 1u);
            if (n > MIR_RR || me_rr[e].seq != (sq | 0x80u))
                whole = 1;
            else
                mir_enc_forget(me_rr[e].x, me_rr[e].y, me_rr[e].w, me_rr[e].h);
        }
    }
    md.fix_have = 0;
    if (whole) {                                        /* (a seq no longer known: the whole panel, black to the client first) */
        mir_enc_forget_all();
        mir_enc_fill_skip(0, 0, 240, 240, 0);
        op_repaint();
        ms.catchups++;
        return;
    }
    ms.fix_due = 1;
}
static void ms_client(uint8_t *f, uint32_t n)           /* a frame that got there */
{
    if (n == MSUM_LEN && f[5] == MSUM_OP) {
        uint32_t ref = 0;
        int32_t diff;
        if (n < 8u || f[6] != md.seq || !md.have_seq || md.broken || md.fix_have)
            return;                                     /* (frames missing before it: its repair comes first) */
        diff = msum_client(md_fb, f, n, &ref);
        if (diff > 0)
            ms_bands((uint32_t)diff);
        return;
    }
    md_frame(f, n);
    if (md.fix_have)
        ms_fix();
}
static void ms_link_tick(void)                          /* the link carries 16 ms of MS_RATE */
{
    static uint8_t f[600];
    ms.credit += MS_RATE * 16u / 1000u;
    while (ms.qr != ms.qw) {
        uint32_t n = ms.q[ms.qr % MS_QMAX] | (uint32_t)ms.q[(ms.qr + 1u) % MS_QMAX] << 8, flag = ms.q[(ms.qr + 2u) % MS_QMAX], k;
        if (n > ms.credit)
            break;
        ms.credit -= n;
        for (k = 0; k < n && k < sizeof f; k++)
            f[k] = ms.q[(ms.qr + 3u + k) % MS_QMAX];
        ms.qr += 3u + n;
        ms.qbytes -= n;
        if (flag == 1u)
            continue;                                   /* (lost on the way) */
        if (flag == 2u)
            f[n / 2u] ^= 1u;                            /* (a byte changed: the sum fails) */
        ms_client(f, n);
    }
    if (ms.qr == ms.qw)
        ms.credit = 0;                                  /* (an idle link saves nothing up) */
}
static void ms_tick(void)                               /* each frame: the device's side (ed_mirror.c mir_poll), the link, the client */
{
    static uint32_t scr = 0xFFu;
    ms_frames++;
    if (ui.scr != scr)
        scr = ui.scr, ms.soon = 1, ms.soon_ms = fm1_ms;  /* (a page: checked soon) */
    if (ms.behind && !ms.qbytes && fm1_ms - ms.catch_ms >= 250u) {   /* the catch-up: in place, what the client holds skipped */
        ms.behind = 0, ms.catch_ms = fm1_ms, ms.catchups++;
        op_repaint();
        ms.soon = 1, ms.soon_ms = fm1_ms;
    }
    if (ms.fix_due && !ms.behind && fm1_ms - ms.fix_ms >= 60u) {
        ms.fix_due = 0, ms.fix_ms = fm1_ms, ms.fixes++;
        op_repaint();
        ms.soon = 1, ms.soon_ms = fm1_ms;
    }
    if (!ms.behind && fm1_ms - ms.mark_ms >= (me.seq == ms.mark_seq ? 1000u : 300u)) {   /* the mark: a seq alone (mir_mark) */
        me.ok = 1, me.rw = 0;
        me_open_px(2);
        me_close();
        ms.mark_ms = fm1_ms, ms.mark_seq = me.seq;
    }
    if (!ms.nosum && !ms.behind && !ms.arm && !ms.cap && !ms.fix_due &&
        (fm1_ms - ms.sum_ms >= 1000u || (ms.soon && fm1_ms - ms.soon_ms >= 300u && fm1_ms - ms.sum_ms >= 500u))) {
        ms.sum_ms = fm1_ms, ms.soon = 0, ms.retry = 0, ms.arm = 1;
        op_repaint();                                   /* (the next frame drawn whole, hashed: mir_draw_begin) */
    }
    if (ms_frames % 311u == 0u) {                       /* (a wrong put in the client's picture: a cause no seq shows) */
        uint32_t x = ms_rand(200), y = ms_rand(230), w = 1u + ms_rand(40), h = 1u + ms_rand(10), i, j;
        for (j = y; j < y + h && j < 240u; j++)
            for (i = x; i < x + w; i++)
                md_fb[j * 240u + i] ^= 0x1234u;
        ms_inject++;
    }
    ms_link_tick();
    ms_compare();
}
static int mirsum_run(void)
{
    uint32_t i, diff = 0, b0, s, open = 0;
    const char *e = getenv("MIRROR_SUMS");
    mc_bt = 1;                                          /* (Bluetooth: no meters) */
    ms.nosum = e && !strcmp(e, "off");
    ms.seed = 20261101u;
    op_repaint();
    frames(8);
    memcpy(md_fb, mt_fb, sizeof md_fb);                 /* (in step at the start) */
    md.seq = me.seq, md.have_seq = 1, md.broken = 0, md.fix_have = 0;
    ms.on = 1;
    b0 = ms.bytes;
    mirror_workload();
    fuzz(6000, 4242);
    for (i = 0; i < 250u; i++)                          /* (4 s still: whatever is left is repaired) */
        frame();
    for (i = 0; i < 240u * 240u; i++) {
        diff += md_fb[i] != mt_fb[i] && fm1_ms + 1u - ms_since[i] > 1000u;   /* (in flight: not yet) */
        if (ms_since[i])
            ms_episode(fm1_ms + 1u - ms_since[i]), open++;
    }
    s = ms_frames * 16u / 1000u;
    printf("  mirror SUMS%s over a lossy link (%u B/s, lag %u B): %u s, %u B (%u B/s), %u frames lost, %u bad sums, %u refused, %u catch-ups, %u repairs\n",
           ms.nosum ? " off" : "", MS_RATE, MS_LAG, s, ms.bytes - b0, (ms.bytes - b0) / (s ? s : 1u), ms.gaps, ms.bads, ms.refused,
           ms.catchups, ms.fixes);
    printf("  mirror SUMS: %u sent (%u B, %u B/s), %.1f of %u bands checkable each, %u frames not the last check's again; %u asks for %u bands\n",
           ms.sums, ms.sum_bytes, ms.sum_bytes / (s ? s : 1u), ms.sums ? (double)ms.sum_valid / ms.sums : 0.0, MSUM_BANDS, ms.voids,
           ms.asks, ms.ask_bands);
    printf("  mirror: a pixel wrong on the client for <=0.5 s %u, <=1 s %u, <=1.5 s %u, <=2 s %u, <=3 s %u, longer %u (longest %u ms); %u px wrong at the end for over 1 s; %u wrongs put in the client's picture\n",
           ms_hist[0], ms_hist[1], ms_hist[2], ms_hist[3], ms_hist[4], ms_hist[5], ms_max, diff, ms_inject);
    ms.on = 0;
    if (ms.nosum)
        return 0;
    check(!diff, "the mirror over a lossy link with SUMS: the client's picture the device's at the end");
    check(!ms_hist[5], "the mirror over a lossy link with SUMS: no pixel wrong on the client longer than 3 s");
    (void)open;
    printf(fails ? "optimist ui mirror SUMS test FAILED (%d)\n" : "optimist ui mirror SUMS test passed\n", fails);
    return fails != 0;
}
#endif
