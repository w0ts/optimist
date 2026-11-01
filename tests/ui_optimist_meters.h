/* SPDX-License-Identifier: GPL-3.0-only */
/* The load meters (SYSTEM > METERS: OFF / CPUS / MEMORY / BOTH; firmware/src/ui/optimist/op_meters.c, core/meters.h), included by
 * ui_optimist_test.c:
 *   pure      the width of a line (240 px = 100 %) and its colour bands (green under 60 %, amber from 60, red over 85)
 *   setting   the line's knob cycles OFF CPUS MEMORY BOTH and stops at the ends; the record's byte (mode + 1): 0, a record from
 *             before, and a byte out of range read the default CPUS
 *   lines     each mode's rows: CPU0 on row 238, CPU1 on 239 (a single core: black), the memory line on 237 (BOTH) or 238-239
 *             (MEMORY), the length for the load given, the colour of its band, the rest of the line black; the held peak falls
 *   rate      at most one redraw per MTR_PERIOD frames however the load moves, none at rest
 *   overlap   every screen drawn with METERS OFF has nothing on the strip's rows (the content ends above them), and drawn again with
 *             BOTH only the strip's rows differ: no line over content, no content moved by a line */
static void mtr_load(uint32_t q8_0, uint32_t q8_1, uint32_t pct_mem)   /* the loads the lines read: CPU0 (song.cpu_q8), CPU1, the arena's fill */
{
    song.cpu_q8 = q8_0;
#if DUAL_LOAD
    dual_c1_q8 = q8_1;
#else
    (void)q8_1;
#endif
    memline_set(pct_mem);
}
static void mtr_look(uint32_t n)                        /* n looks: the loads as set, each look a period of frames */
{
    frames(n * MTR_PERIOD);
}
static uint32_t mtr_run(uint32_t y, uint16_t col)       /* the pixels of row y from the left in col (the line's length) */
{
    uint32_t x = 0;
    while (x < 240u && px_at(x, y, col))
        x++;
    return x;
}
static int mtr_rest_black(uint32_t y, uint32_t from)    /* row y black from x on */
{
    uint32_t x;
    for (x = from; x < 240u; x++)
        if (!px_at(x, y, C_BLACK))
            return 0;
    return 1;
}
static void mtr_start(uint32_t mode)                    /* a screen with the mode, the loads at rest (nothing held), drawn whole */
{
    uint32_t i;
    reset_ui();
    stk_boot();
    meters_mode = (uint8_t)mode;
    op_enter(SCR_SOUND);
    mtr_load(0, 0, 0);
    for (i = 0; i < ML_N; i++)
        mtr.hold[i] = 0;
    ui.force = 1;
    frame();
}
static void mtr_pure_tests(void)
{
    check(mtr_px(0) == 0 && mtr_px(128) == 120 && mtr_px(256) == 240 && mtr_px(900) == 240, "meters: 0 / 50 / 100 % (and over) are 0 / 120 / 240 px");
    check(mtr_col(0) == C_OK && mtr_col(150) == C_OK && mtr_col(154) == C_WARN && mtr_col(218) == C_WARN && mtr_col(222) == C_ERR && mtr_col(256) == C_ERR,
          "meters: green under 60 %, amber from 60 to 85, red over 85");
    meters_from_rec(0);
    check(meters_mode == MTR_CPUS, "meters: a settings record from before (the byte 0) reads the default, CPUS");
    meters_from_rec(1);
    check(meters_mode == MTR_OFF, "meters: the record's byte 1 is OFF");
    meters_from_rec(4);
    check(meters_mode == MTR_BOTH && meters_to_rec() == 4u, "meters: the byte 4 is BOTH, written back as it was read");
    meters_from_rec(77);
    check(meters_mode == MTR_CPUS, "meters: a byte out of range reads the default");
    meters_mode = MTR_MEM;
    meters_from_rec(meters_to_rec());
    check(meters_mode == MTR_MEM, "meters: MEMORY survives the record");
    meters_mode = MTR_DEFAULT;
}
static void mtr_setting_tests(void)
{
    static const char *const WANT[MTR_N] = {"OFF", "CPUS", "MEMORY", "BOTH"};
    uint32_t i, n, row = 0xFFFFu, ok = 1, k;
    cell_t c;
    char v[16];
    reset_ui();
    meters_mode = MTR_DEFAULT;
    op_enter(SCR_SYSTEM);
    n = SCREENS[SCR_SYSTEM].rows();
    for (i = 0; i < n && row == 0xFFFFu; i++) {
        line_at(SCR_SYSTEM, i);
        glw_line(SCR_SYSTEM, i, &c, v);
        if (!strcmp(c.label, "METERS"))
            row = i;
    }
    check(row != 0xFFFFu && !strcmp(v, "CPUS"), "meters: SYSTEM has a METERS line, CPUS from the start");
    if (row == 0xFFFFu)
        return;
    line_at(SCR_SYSTEM, row);
    for (k = 2; k < MTR_N; k++) {                       /* up: MEMORY, BOTH */
        turn(LINE_KNOB(row), 1);
        glw_line(SCR_SYSTEM, row, &c, v);
        ok &= !strcmp(v, WANT[k]) && meters_mode == k;
    }
    turn(LINE_KNOB(row), 1);
    ok &= meters_mode == MTR_BOTH;                      /* (the knob stops at the end) */
    for (k = MTR_N - 1u; k > 0; k--) {                  /* down to OFF */
        turn(LINE_KNOB(row), -1);
        glw_line(SCR_SYSTEM, row, &c, v);
        ok &= !strcmp(v, WANT[k - 1u]) && meters_mode == k - 1u;
    }
    turn(LINE_KNOB(row), -1);
    ok &= meters_mode == MTR_OFF;
    check(ok, "meters: the METERS knob cycles OFF CPUS MEMORY BOTH and stops at both ends");
    meters_mode = MTR_DEFAULT;
}
static void mtr_lines_tests(void)
{
    uint32_t n;
    mtr_start(MTR_CPUS);
    check(mtr_rest_black(237, 0) && mtr_rest_black(238, 0) && mtr_rest_black(239, 0), "meters: at 0 % every line is black");
    mtr_load(128, 0, 0);
    mtr_look(1);
    n = mtr_run(238, C_OK);
    check(n == 120 && mtr_rest_black(238, 120) && mtr_rest_black(237, 0), "meters: CPUS, CPU0 at 50 %: row 238 green over 120 px, black after, no memory line");
#if DUAL_LOAD
    mtr_load(128, 64, 0);
    mtr_look(1);
    check(mtr_run(239, C_OK) == 60 && mtr_rest_black(239, 60) && mtr_run(238, C_OK) == 120, "meters: CPU1 at 25 %: row 239 green over 60 px, CPU0 as it was");
    mtr_load(128, 0xFFFFFFFFu, 0);                       /* (CPU1 down: DUAL_C1_OFF) */
    mtr_look(30);
    check(mtr_rest_black(239, 0), "meters: CPU1 down: its line black");
#else
    check(mtr_rest_black(239, 0), "meters: a single core: the CPU1 line stays black");
#endif
    mtr_start(MTR_CPUS);
    mtr_load(200, 0, 0);
    mtr_look(1);
    check(mtr_run(238, C_WARN) == mtr_px(200), "meters: 78 % is amber");
    mtr_load(240, 0, 0);
    mtr_look(1);
    check(mtr_run(238, C_ERR) == mtr_px(240), "meters: 94 % is red");
    mtr_load(0, 0, 0);                                   /* the peak held: it falls, never at once */
    mtr_look(1);
    n = mtr_run(238, C_ERR) + mtr_run(238, C_WARN) + mtr_run(238, C_OK);
    check(n > 0u && n < (uint32_t)mtr_px(240), "meters: the load gone: the line falls a step, the peak is held a little");
    mtr_look(40);
    check(mtr_rest_black(238, 0), "meters: ... and is gone in the end");
    mtr_start(MTR_MEM);
    mtr_load(256, 256, 50);
    mtr_look(1);
    check(mtr_run(238, C_OK) == 120 && mtr_run(239, C_OK) == 120 && mtr_rest_black(238, 120) && mtr_rest_black(239, 120) && mtr_rest_black(237, 0),
          "meters: MEMORY at 50 %: one line 2 px thick (rows 238-239), no CPU lines");
    mtr_load(0, 0, 90);
    mtr_look(1);
    check(mtr_run(239, C_ERR) == mtr_px(90 * 256 / 100) , "meters: memory at 90 % is red");
    mtr_start(MTR_BOTH);
    mtr_load(128, 0, 25);
    mtr_look(1);
    check(mtr_run(237, C_OK) == mtr_px(25 * 256 / 100) && mtr_run(238, C_OK) == 120 && mtr_rest_black(239, 0),
          "meters: BOTH: memory on 237 (1 px), CPU0 on 238, CPU1 on 239");
    mtr_start(MTR_OFF);
    mtr_load(256, 256, 100);
    mtr_look(4);
    check(mtr_rest_black(237, 0) && mtr_rest_black(238, 0) && mtr_rest_black(239, 0), "meters: OFF: nothing drawn");
    meters_mode = MTR_CPUS;                              /* (turned on again: drawn at the next frame, without a force) */
    mtr_look(1);
    check(mtr_run(238, C_ERR) > 0u, "meters: turned on: the lines appear");
    meters_mode = MTR_MEM;                               /* (to MEMORY: the CPU lines' rows black first) */
    mtr_look(1);
    check(mtr_run(238, C_ERR) == 240 && mtr_run(239, C_ERR) == 240, "meters: CPUS -> MEMORY: the strip redrawn as MEMORY's");
    meters_mode = MTR_DEFAULT;
}
static void mtr_rate_tests(void)
{
    uint32_t i, was;
    mtr_start(MTR_BOTH);
    mtr_load(100, 0, 10);
    mtr_look(2);
    was = mtr.draws;
    frames(10u * MTR_PERIOD);                            /* at rest: nothing again */
    check(mtr.draws == was, "meters: at rest the lines are not redrawn");
    was = mtr.draws;
    for (i = 0; i < 120u; i++) {                         /* 2 s at 60 frames a second, the load moving every frame */
        mtr_load(20u + (i * 37u) % 200u, 0, 10u + (i * 7u) % 80u);
        frame();
    }
    check(mtr.draws - was <= 120u / MTR_PERIOD + 1u && mtr.draws - was >= 2u && 60u / MTR_PERIOD <= 5u,
          "meters: a load moving every frame is drawn at most 5 times a second");
    mtr_start(MTR_BOTH);
    mtr_load(128, 0, 0);
    mtr_look(1);
    {   /* a stray pixel on the line stays while its width and colour do not change: no redraw of what did not move */
        screen[238 * 240 + 200] = swap16(C_WHITE);
        mtr_look(3);
        check(px_at(200, 238, C_WHITE), "meters: a line unchanged in width and colour is not redrawn");
        mtr_load(160, 0, 0);
        mtr_look(1);
        check(!px_at(200, 238, C_WHITE) || mtr_px(160) > 200, "meters: ... and is, the pixels that moved, when it changes");
    }
    meters_mode = MTR_DEFAULT;
}
static void mtr_enter_screen(uint32_t k)
{
    static const uint32_t SCRS[] = {SCR_HOME, SCR_SOUND, SCR_FX, SCR_PROJECT, SCR_SYSTEM, SCR_STEP, SCR_SONG, SCR_TEMPO, SCR_SCOPE, SCR_GLOBAL};
    reset_ui();
    if (k < sizeof SCRS / sizeof SCRS[0]) {
        op_enter(SCRS[k]);
        if (SCRS[k] == SCR_SYSTEM || SCRS[k] == SCR_GLOBAL)
            line_at(SCRS[k], 0);                         /* (the first page: the METERS line shows its own value, which the test turns) */
        return;
    }
    op_enter(SCR_HOME);
    switch (k - (uint32_t)(sizeof SCRS / sizeof SCRS[0])) {
    case 0:                                              /* the mixer on MASTER */
        turn(EN_ALGO, -1);
        break;
    case 1:                                              /* the mixer on DR */
        while (mx_row() != MXR_DR && mx_row() < MXR_LANE0)
            turn(EN_ALGO, 1);
        break;
    case 2:                                              /* STEP's drum grid */
        op_enter(SCR_STEP);
        song.sel = TRK_DRUM;
        break;
#if FELUCCA_PATTERNS
    case 3:                                              /* the patterns screen (LFO held on SONG) */
        op_enter(SCR_SONG);
        press(B_LFO);
        frames(HOLD_FRAMES);
        break;
#endif
    default:
        break;
    }
}
#if FELUCCA_PATTERNS
#define MTR_SCREENS (10u + 4u)
#else
#define MTR_SCREENS (10u + 3u)
#endif
static uint16_t mtr_off_img[240 * 240];
static void mtr_overlap_tests(void)
{
    uint32_t k, x, y, bad_off = 0, bad_on = 0, bad_strip = 0;
    for (k = 0; k < MTR_SCREENS; k++) {
        mtr_enter_screen(k);
        meters_mode = MTR_OFF;
        mtr_load(256, 256, 100);                         /* (the same loads in both drawings: the header's memory line, INFO's CPU lines are not the strip's) */
        ui.force = 1;
        mtr_look(2);                                     /* (as long as the BOTH drawing below: INFO's peaks move with time) */
        ui.force = 1;
        frames(2);
        for (y = (uint32_t)(240 - OP_MTR); y < 240u; y++)
            for (x = 0; x < 240u; x++)
                bad_off += screen[y * 240u + x] != swap16(C_BLACK);   /* (content on the strip's rows) */
        memcpy(mtr_off_img, screen, sizeof screen);
        meters_mode = MTR_BOTH;
        mtr_load(256, 256, 100);                         /* (every line full: red) */
        ui.force = 1;
        mtr_look(2);
        for (y = 0; y < (uint32_t)(240 - OP_MTR); y++)
            for (x = 0; x < 240u; x++)
                if (screen[y * 240u + x] != mtr_off_img[y * 240u + x]) {   /* (the rest of the screen as it was) */
                    bad_on++;
                }
        for (x = 0; x < 240u; x++)
            bad_strip += !px_at(x, 237, C_ERR) || !px_at(x, 238, C_ERR);
        if (bad_off || bad_on || bad_strip)
            printf("  meters: screen %u: %u pixels of content on the strip, %u changed above it, %u strip pixels wrong\n", (unsigned)k,
                   (unsigned)bad_off, (unsigned)bad_on, (unsigned)bad_strip);
        meters_mode = MTR_DEFAULT;
        if (bad_off || bad_on || bad_strip)
            break;
    }
    check(!bad_off && !bad_on && !bad_strip,
          "meters: HOME / mixer rows / SOUND / FX / PROJECT / SYSTEM / STEP / SONG / TEMPO / SCOPE / GLOBAL / patterns: nothing on the strip's rows without the lines, only those rows change with BOTH");
}
static void mtr_shots(void)                             /* two screens with BOTH, for the eye (DIR/opt-meters-both-*.ppm) */
{
    static const struct { uint32_t k; const char *name; } S[2] = {{5u, "opt-meters-both-step"}, {0u, "opt-meters-both-mixer"}};
    uint32_t i;
    for (i = 0; i < 2u; i++) {
        mtr_enter_screen(S[i].k);
        meters_mode = MTR_BOTH;
        mtr_load(190, 90, 35);                           /* CPU0 74 % (amber), CPU1 35 % (green), the arena 35 % (green) */
        ui.force = 1;
        mtr_look(2);
        ppm(S[i].name);
    }
    meters_mode = MTR_DEFAULT;
}
#if PART_DROPS
static arn_blk_t mtr_save_cur[ARN_PARTS];
static uint8_t mtr_save_next[ARN_PARTS];
#endif
static void meters_tests(void)
{
#if PART_DROPS
    memcpy(mtr_save_cur, arn_cur, sizeof arn_cur);
    memcpy(mtr_save_next, arn_has_next, sizeof arn_has_next);
#endif
    mtr_pure_tests();
    mtr_setting_tests();
    mtr_lines_tests();
    mtr_rate_tests();
    mtr_overlap_tests();
    mtr_shots();
    reset_ui();
    meters_mode = MTR_DEFAULT;
#if PART_DROPS
    memcpy(arn_cur, mtr_save_cur, sizeof arn_cur);
    memcpy(arn_has_next, mtr_save_next, sizeof arn_has_next);
#endif
    stk_host_free = 0;
}
