/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > INFO's CPU bars and RAM line (the user, 2026-10-28: "progress bars with 1-minute and 10-second
 * peaks", "add a RAM line"): the overload rings' 10 s and 1 min windows (system/overload.h ovl_win_pct: 64 slots of 1.024 s,
 * their wrap, a stopped audio), the stacks' paint and read-back (system/stackmark.h), the RAM line, and the page as drawn: each
 * CPU line's bar under its text, filled to the load now, the ticks at its peaks. Screen opt-info-bars.ppm. */
static uint32_t ovw_pct(uint32_t pct) { return OVL_HALF_US * pct / 100u; }
static void ovl_window_tests(void)
{
    uint32_t s, ok = 1;
    memset((void *)&ovl, 0, sizeof ovl);
    ovl_ring_put(&ovl.r[OVL_CPU0], ovw_pct(80u), 0u);   /* (slot 0: 80 %; then 30 % every slot) */
    for (s = 1; s < 70u; s++) {
        uint32_t ms = s * 1024u, w10, w60;
        ovl_ring_put(&ovl.r[OVL_CPU0], ovw_pct(30u), ms);
        w10 = ovl_win_pct(OVL_CPU0, ms, OVL_W10);
        w60 = ovl_win_pct(OVL_CPU0, ms, OVL_W60);
        ok &= w10 == (s < OVL_W10 ? 80u : 30u) && w60 == (s < OVL_W60 ? 80u : 30u);
        if (w10 != (s < OVL_W10 ? 80u : 30u) || w60 != (s < OVL_W60 ? 80u : 30u))
            printf("  ovl: slot %u: 10 s %u %%, 1 min %u %%\n", (unsigned)s, (unsigned)w10, (unsigned)w60);
    }
    check(ok, "overload rings: a peak held 10 slots (MAX 10S) and 60 slots (MAX 1M), then forgotten, over the ring's wrap (64)");
    ok = ovl_win_pct(OVL_CPU0, 69u * 1024u + 5u * 1024u, OVL_W10) == 30u && ovl_win_pct(OVL_CPU0, 69u * 1024u + 10u * 1024u, OVL_W10) == 0u;
    check(ok, "overload rings: the audio stopped: its last slots age out of the window, then 0");
    ovl_ring_put(&ovl.r[OVL_CPU0], ovw_pct(20u), 300u * 1024u);   /* (200 slots later: every old slot cleared) */
    check(ovl_win_pct(OVL_CPU0, 300u * 1024u, OVL_W60) == 20u && ovl_win_pct(OVL_CPU0, 300u * 1024u, OVL_SLOTS) == 20u,
          "overload rings: after a gap longer than the ring, only the new slot counts");
    check(ovl_max5_pct(OVL_CPU0, 300u * 1024u) == 20u && ovl_maxall_pct(OVL_CPU0) == 80u, "overload rings: the 5 s record and MAX since boot as before");
    memset((void *)&ovl, 0, sizeof ovl);
}
static uint32_t stk_t_main[256], stk_t_irq[128];
static void stk_t_junk(void)
{
    uint32_t i;
    for (i = 0; i < 256u; i++)
        stk_t_main[i] = 0x11111111u * (i & 7u);
    for (i = 0; i < 128u; i++)
        stk_t_irq[i] = 0x22222222u;
}
static void stackmark_tests(void)
{
    uint32_t size, k, i, ok;
    int32_t l;
    cell_t c;
    stk_t_junk();                                       /* sp in the main stack, 200 words up: both painted */
    stk_paint(stk_t_main, stk_t_main + 256, stk_t_irq, stk_t_irq + 128, (uintptr_t)(stk_t_main + 200));
    for (i = 0, ok = 1; i < 256u; i++)
        ok &= (stk_t_main[i] == STK_MARK) == (i < 200u - STK_SLACK);
    for (i = 0; i < 128u; i++)
        ok &= (stk_t_irq[i] == STK_MARK) == (i < 128u - STK_SLACK);
    check(ok, "stacks: painted below sp less the slack, the interrupts' whole less its top; nothing live touched");
    l = stk_left(STK_MAIN, &size);
    check(l == (int32_t)(200u - STK_SLACK) * 4 && size == 1024u, "stacks: the main stack's room left read back (bytes), its size");
    stk_t_main[100] = 0;                                /* (a call went 156 words deep) */
    check(stk_left(STK_MAIN, &size) == 400 && stk_left(STK_IRQ, &size) == (int32_t)(128u - STK_SLACK) * 4 && size == 512u,
          "stacks: the deepest word written is the high-water mark (400 B left); the interrupts' its own");
    check(stk_least(&k) == 256 && k == STK_IRQ, "stacks: the least left of all, and which stack");
    stk_t_junk();                                       /* sp in the interrupts' stack: that one alone */
    stk_paint(stk_t_main, stk_t_main + 256, stk_t_irq, stk_t_irq + 128, (uintptr_t)(stk_t_irq + 100));
    check(stk_t_main[0] != STK_MARK && stk_t_irq[100u - STK_SLACK - 1u] == STK_MARK && stk_t_irq[100u - STK_SLACK] != STK_MARK &&
              stk_left(STK_MAIN, &size) < 0 && stk_left(STK_IRQ, &size) == (int32_t)(100u - STK_SLACK) * 4,
          "stacks: sp in the interrupts' stack: only it painted, under sp; the main stack not measured");
    stk_t_junk();                                       /* sp in neither (a task's stack): nothing */
    stk_paint(stk_t_main, stk_t_main + 256, stk_t_irq, stk_t_irq + 128, (uintptr_t)(stk_t_main + 300));
    check(stk_t_main[0] != STK_MARK && stk_t_irq[0] != STK_MARK && stk_least(&k) < 0, "stacks: sp in neither: nothing painted, none measured");
    check(stk_left(STK_C1, &size) < 0 || DUAL_LOAD, "stacks: CPU1's: not measured on the host");
    reset_ui();
    info_ram(&c);
    check(c.kind == CK_RO && !strcmp(c.label, "RAM") && !strcmp(c.val, "?") && !strcmp(c.unit, " STACK LEFT"), "RAM line: no stack measured: ?");
    stk_boot();                                         /* (the host's stand-ins, the device's sizes) */
    stk_host_free = 41u * 1024u + 300u;
    info_ram(&c);
    check(!strcmp(c.label, "RAM 41K FREE") && !strcmp(c.val, "7.5K") && c.col == C_OK,
          "RAM line: the SRAM free at link in its name, the least stack room left (the interrupts' 7.5K), green");
    stk_host_irq[200] = 0;                              /* (an interrupt nested 1.7 KB from the bottom: 800 B left) */
    info_ram(&c);
    check(!strcmp(c.val, "0.7K") && c.col == C_WARN, "RAM line: under 1 KB of stack left: 0.7K, amber");
    stk_host_irq[200] = STK_MARK;
    check(info_line("RAM 41K FREE") < INFO_N, "RAM line: on INFO");
    stk_host_free = 0;
    {   /* the page: the CPU line's bar under its text (never under it, never text on it), its fill to the load now, the ticks */
        uint32_t ci = info_line(DUAL_LOAD ? "CPU0" : FELUCCA_SYS_DEBUG ? "LOAD" : "CPU LOAD"), n = info_lines(INFO_PG_INFO);
        int32_t ph = (GL_END - gl_y0()) / (int32_t)n, cap = ph >= GL_INFO_BIG ? gl_pitch() : GL_INFO_PH, y0, y, by = -1, x10, x60;
        memset((void *)&ovl, 0, sizeof ovl);
        info_mode = OVL_NOW;
        ovl_ring_put(&ovl.r[OVL_CPU0], ovw_pct(90u), fm1_ms - 30u * 1024u);   /* (90 % half a minute ago: the dim tick, red) */
        ovl_ring_put(&ovl.r[OVL_CPU0], ovw_pct(70u), fm1_ms);                 /* (70 % now: the bright tick, amber) */
        song.cpu_q8 = 40u * 256u / 100u + 1u;
        op_enter(SCR_SYSTEM);
        ui.row[SCR_SYSTEM] = 0;
        ui.force = 1;
        frame();
        ppm("opt-info-bars");
        ph = ph > cap ? cap : ph;
        y0 = OH_HEAD + gl_y0() + (int32_t)ci * ph;
        for (y = y0; y < y0 + ph && by < 0; y++)
            if (px_in(GL_BX, (uint32_t)y, 1, 1, C_OK) && px_in(GL_BX + 80, (uint32_t)y, 1, 1, C_OK))
                by = y;
        x10 = GL_BX + 70 * (GL_BW - GL_INFO_TK) / 100;
        x60 = GL_BX + 90 * (GL_BW - GL_INFO_TK) / 100;
        check(ci < INFO_N && by >= 0 && !px_in(GL_BX + GL_BW * 40 / 100 + 3, (uint32_t)by, 4, 1, C_OK),
              "INFO page: the CPU line's bar under its text, green, filled to the load now (40 %)");
        check(by >= 0 && px_in((uint32_t)x10, (uint32_t)by, GL_INFO_TK, 1, C_WARN) &&
                  px_in((uint32_t)x60, (uint32_t)by, GL_INFO_TK, 1, col_shade(C_ERR, 4u)),
              "INFO page: the bright tick at the 10 s peak (70 %, amber), the dim one at the 1 min peak (90 %, red dimmed)");
        {   /* (no text drawn in the bar's rows: its rows hold the bar's colours, its foot line and black only) */
            int32_t x, r;
            for (ok = 1, r = by - GL_INFO_TK; by >= 0 && r < by + GL_BH + GL_INFO_TK; r++)
                for (x = 0; x < 240; x++) {
                    uint16_t p = swap16(screen[r * 240 + x]);
                    ok &= p == 0 || p == C_OK || p == C_WARN || p == C_LINE || p == col_shade(C_ERR, 4u);
                }
            check(ok, "INFO page: nothing but the bar in its rows (no text on it)");
        }
        song.cpu_q8 = 0;
        memset((void *)&ovl, 0, sizeof ovl);
        reset_ui();
    }
}
