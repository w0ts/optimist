/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, what the screen costs the bus a card shares (docs/SD-CARD-DESIGN.md 7): the LCD's bytes a frame.
 * The user, 2026-11-04: "when hold record and the ring to clear track fills in, audio is dropping", "switching to arp
 * also drop sample voices". BUS_COST=1 prints the table (alone, as CANVAS_BENCH); the frames' figures go into
 * tests/sd_stream_bus_test.c's pages. */
static uint32_t bc_px, bc_blits, bc_fill_px, bc_fills;
static void bc_reset(void) { bc_px = blit_px, bc_blits = blit_n, bc_fill_px = fill_px, bc_fills = fill_n; }
static uint32_t bc_bytes(void) { return (blit_px - bc_px + fill_px - bc_fill_px) * 2u; }
static void bc_row(const char *what, uint32_t frames_n)
{
    printf("  bus cost %-34s %7u B  %3u blits %3u fills, %u frame(s)\n", what, bc_bytes(), blit_n - bc_blits, fill_n - bc_fills,
           frames_n);
}
static void bc_page(const char *name, uint32_t b)
{
    uint32_t i, worst = 0, b0;
    bc_reset();
    tap(b);                                             /* (press and release: two frames) */
    bc_row(name, 2u);
    for (i = 0; i < 4u; i++) {
        b0 = bc_bytes();
        frame();
        if (bc_bytes() - b0 > worst)
            worst = bc_bytes() - b0;
    }
    printf("    then %u B a frame at most\n", worst);
}
static void bus_cost_bench(void)
{
    uint32_t t0, n, worst = 0, b0;
    static const struct {
        const char *name;
        uint32_t b;
    } PG[] = {{"to ENV", B_ENV}, {"to LFO", B_LFO}, {"to FX", B_FX}, {"to SCL", B_SCL}, {"to EDIT", B_EDIT}, {"to ARP", B_ARP},
              {"to SEQ", B_SEQ}, {"to GLO", B_GLO}, {"to HOME", B_HOME}, {"to ARP again", B_ARP}, {"to EDIT again", B_EDIT}};
    reset_ui();
    frames(8);
    bc_reset();
    frames(60);
    bc_row("idle, the home page", 60u);
    for (n = 0; n < sizeof PG / sizeof PG[0]; n++)
        bc_page(PG[n].name, PG[n].b);
    reset_ui();
    frames(8);
    press(B_REC);
    t0 = fm1_ms;
    while (!rh.ring && fm1_ms - t0 < 2000u)
        frame();
    frames(3);
    t0 = fm1_ms;
    bc_reset();
    n = 0;
    while (fm1_ms - t0 < 1000u) {
        b0 = bc_bytes();
        frame();
        n++;
        if (bc_bytes() - b0 > worst)
            worst = bc_bytes() - b0;
    }
    bc_row("the REC ring", n);
    printf("    %u B a frame at most\n", worst);
    release(B_REC);
    frames(3);
}

/* the ring's frames: after its first, the LCD gets the box round the arc that moved, and the screen is what a whole repaint
 * draws (the look kept) */
static void ring_bus_tests(void)
{
    static uint16_t now[240 * 240];
    uint32_t t0, n = 0, worst = 0, diff = 0, first, b0;
    reset_ui();
    frames(8);
    press(B_REC);
    t0 = fm1_ms;
    while (!rh.ring && fm1_ms - t0 < 2000u) {
        bc_reset();
        frame();
    }
    first = bc_bytes();
    check(first >= 50000u, "the ring: its first frame the whole panel");
    t0 = fm1_ms;
    while (rh.ring && fm1_ms - t0 < 500u) {
        b0 = bc_bytes();
        frame();
        n++;
        if (bc_bytes() - b0 > worst)
            worst = bc_bytes() - b0;
    }
    while (rh.ring && fm1_ms - t0 < 1200u) {
        b0 = bc_bytes();
        frame();
        n++;
        if (bc_bytes() == b0)                           /* (its 64 steps: a frame may draw nothing) */
            continue;
        memcpy(now, screen, sizeof now);
        op_repaint();                                   /* (the same frame whole; the ring's clock stands: frame()'s 16 ms taken back) */
        fm1_ms -= 16u;
        frame();
        diff += memcmp(now + OP_PY * 240, screen + OP_PY * 240, (uint32_t)MODAL_H * 240u * sizeof now[0]) != 0;
    }
    check(n >= 20u, "the ring: its frames seen");
    check(worst <= 4096u, "the ring: a step costs the LCD the arc's box, 4 KB at most");
    check(diff == 0u, "the ring: a step's pixels are a whole repaint's");
    printf("  the ring: first frame %u B, a step %u B at most, %u steps, %u differ from a whole repaint\n", first, worst, n, diff);
    release(B_REC);
    frames(3);
    check(!rh.ring && op_overlay() != 3u, "the ring let go: gone");
    press(B_REC);                                       /* (a second hold: whole again, not the last ring's box) */
    t0 = fm1_ms;
    while (!rh.ring && fm1_ms - t0 < 2000u) {
        bc_reset();
        frame();
    }
    check(bc_bytes() >= 50000u, "the ring held again: whole again");
    release(B_REC);
    frames(3);
}
