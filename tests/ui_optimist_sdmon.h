/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SD card mode with streaming (UI_SDCARD=1 with SAMPLE, ARENA and SD_RING): SYSTEM > SD STREAM
 * (ui/optimist/op_sdmon.c, storage/sd/sd_mon.c; the user, 2026-11-05: "we should totally monitor how we are doing on the SD
 * read rate and sample playback somehow"). The monitor sampled at ticks of the test's own (the model's clock: 1000 a ms)
 * over the streams' real state, the caches' and the card driver's counters, the page's four lines read as the screen does:
 *   the page: right after SD CARD's two, READ / STREAMS / BUFFER / DRY
 *   READ: KB/s from the blocks read over a period; the bar against the kept clock (8.57 MHz: 839 KB/s); the peak held 10 s,
 *         then the period's own; the colours by the share of the clock's rate
 *   STREAMS: sounding now / the most in 10 s;  BUFFER: the least ring fill of the streams the card feeds, now / the least of 10 s ("42/0"),
 *         amber under 25, red at 0 (of the 10 s least), "-" with none
 *   DRY: a stream that ran dry counted (sst_ran_dry) and red, amber after 10 s; late / missed reads beside it; CRC FAILS and
 *         STEPS DOWN (and NO RING) taking turns on the line, the knob stepping them; the clear: YES, SAVE, a knob on another
 *         line; the counters themselves go on
 *   the real clock: ssm_poll samples once a period */
#include <stdarg.h>
static void smt_ck(int ok, const char *fmt, ...)         /* smt_ck() with a formatted message */
{
    char b[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    check(ok, b);
}
#define SMT_MS(n) ((n) * SD_TICKS_MS)
static uint32_t smt_t;                                  /* the test's tick */
static void smt_run(uint32_t ms, uint32_t blocks)       /* ms later (in periods), blocks more read */
{
    uint32_t n = ms / SSM_MS, k;
    for (k = 0; k < n; k++) {
        smt_t += SMT_MS(SSM_MS);
        sst_st.blocks += blocks / n + (k == 0u ? blocks % n : 0u);
        ssm_sample(smt_t);
    }
}
static const char *smt_lab(uint32_t r, uint32_t k, cell_t *c) { sys_cell(r, k, c); return c->label ? c->label : ""; }
static void smt_stream(uint32_t i, uint32_t on, uint32_t wr, uint32_t done)   /* stream i: sounding, wr slots filled ahead */
{
    sst[i].on = (uint8_t)on;
    sst[i].owner = 0;
    sst[i].gen = 1;
    sst[i].pgen = 1;
    sst[i].rd = 0;
    sst[i].wr = wr;
    sst[i].done = (uint8_t)done;
}
static void sdmon_tests(void)
{
    uint32_t r, i, k, seen;
    cell_t c;
    char t[16], l[5][12];
    for (r = 0; r < sys_rows(); r++) {                  /* the first SD CARD page */
        sys_name(r, t);
        if (!strcmp(t, "SD CARD"))
            break;
    }
    smt_ck(r + 2u < sys_rows(), "SD STREAM: SD CARD has its pages");
    sys_name(r + 1u, t);
    smt_ck(!strcmp(t, "SD CARD"), "SD STREAM: SD CARD's second page");
    sys_name(r + 2u, t);
    smt_ck(!strcmp(t, "SD STREAM"), "SYSTEM: a SD STREAM page right after SD CARD's two");
    r += 2u;
    memset(&ssm, 0, sizeof ssm);
    for (i = 0; i < SST_NALL; i++)
        sst[i].on = 0;
    sst_dry = sst_nostream = sst_cut = 0;
    sst_st.blocks = sst_rcs.blocks = 0;
    for (i = 0; i < SST_NPART; i++)
        sst_rc[i].late = sst_rc[i].miss = 0;
    sd_crc_fails = sd_demotions = 0;
    sd_step = 5;                                        /* 8.57 MHz: 839 KB/s */
    smt_t = SMT_MS(1000);
    ssm_sample(smt_t);
    sys_cell(r, 0, &c);
    smt_ck(!strcmp(c.label, "READ") && !strcmp(c.val, "0") && !strcmp(c.unit, "KB/S") && c.kind == CK_RO && c.col == C_OK,
          "READ: 0 KB/S, green, read-only, with nothing read");
    smt_ck(!strcmp(smt_lab(r, 1, &c), "STREAMS") && !strcmp(c.val, "0/0"), "STREAMS: 0/0");
    smt_ck(!strcmp(smt_lab(r, 2, &c), "BUFFER") && !strcmp(c.val, "-"), "BUFFER: - while no stream is fed");
    smt_ck(!strcmp(smt_lab(r, 3, &c), "DRY") && !strcmp(c.val, "0") && c.col == C_OK, "DRY: 0, green");
    /* ---- READ: the rate from a model's bytes */
    smt_run(500, 410);                                  /* 410 blocks in half a second: 209920 B / 500 ms = 419 KB/s */
    sys_cell(r, 0, &c);
    smt_ck(!strcmp(c.val, "419") && c.gv == 49 && c.gmax == 100 && c.col == C_OK && c.pkbar && c.hold[0] == 49,
          "READ: 410 blocks in 500 ms = 419 KB/S, the bar 49 %% of the clock's 839 (%s, gv %d, tick %d)", c.val, c.gv, c.hold[0]);
    smt_run(500, 100);
    sys_cell(r, 0, &c);
    smt_ck(!strcmp(c.val, "102") && c.gv == 12 && c.hold[0] == 49, "READ: 102 KB/S now, the peak's tick stays at 49 (%s, %d, %d)", c.val, c.gv, c.hold[0]);
    smt_run(8000, 0);
    sys_cell(r, 0, &c);
    smt_ck(c.hold[0] == 49 && !strcmp(c.val, "0"), "READ: the peak still held 9 s on (tick %d)", c.hold[0]);
    smt_run(2000, 0);
    sys_cell(r, 0, &c);
    smt_ck(c.hold[0] == 0, "READ: the peak let go after 10 s (tick %d)", c.hold[0]);
    smt_run(500, 800);                                  /* 409600 B / 500 ms = 819 KB/s: 97 % of the clock */
    sys_cell(r, 0, &c);
    smt_ck(!strcmp(c.val, "819") && c.col == C_ERR, "READ: 819 KB/S is red (%s)", c.val);
    smt_run(500, 450);                                  /* 460800 / 500 = 460 KB/s: 54 % */
    sys_cell(r, 0, &c);
    smt_ck(c.col == C_OK && c.hold[0] == 97, "READ: 460 KB/S green, the peak's tick at 97 (%d)", c.hold[0]);
    sd_step = 2;                                        /* 15 MHz: 1470 KB/s */
    smt_run(500, 450);
    sys_cell(r, 0, &c);
    smt_ck(c.gv == 31, "READ: the bar follows the kept clock (15 MHz: 460 of 1470 = %d %%)", c.gv);
    sd_step = 5;
    /* ---- STREAMS and BUFFER */
    smt_stream(0, 1, SST_K / 2u, 0);
    smt_stream(1, 1, SST_K, 0);
    smt_stream(2, 1, 0, 1);                             /* (all of it in the head: not fed by the card) */
    smt_run(500, 0);
    sys_cell(r, 1, &c);
    smt_ck(!strcmp(c.val, "3/3") && c.gv == 3 * 100 / (int)SST_N && c.pkbar && c.hold[0] == c.gv, "STREAMS: 3 sounding of the most 3 (%s)", c.val);
    sys_cell(r, 2, &c);
    smt_ck(!strcmp(c.val, "50/50") && !strcmp(c.unit, "%") && c.col == C_OK && c.gv == 50 && c.pkbar && c.hold[0] == 50, "BUFFER: the least ring half full: 50 now / 50 least, green (%s)", c.val);
    sst[1].wr = SST_K / 4u;                             /* two slots of eight: 25 % */
    smt_run(500, 0);
    sys_cell(r, 2, &c);
    smt_ck(!strcmp(c.val, "25/25") && c.col == C_OK, "BUFFER: 25 %% is not yet amber (%s)", c.val);
    sst[1].wr = SST_K / 4u - 1u;
    smt_run(500, 0);
    sys_cell(r, 2, &c);
    smt_ck(c.col == C_WARN && c.gv < 25, "BUFFER: under 25 %% amber (%s)", c.val);
    sst[1].wr = 0;
    smt_run(500, 0);
    sys_cell(r, 2, &c);
    smt_ck(!strcmp(c.val, "0/0") && c.col == C_ERR, "BUFFER: 0 red (%s)", c.val);
    sst[1].wr = SST_K;
    smt_stream(2, 0, 0, 0);
    smt_run(500, 0);
    sys_cell(r, 2, &c);
    smt_ck(!strcmp(c.val, "50/0") && c.col == C_ERR && c.gv == 50 && c.hold[0] == 0, "BUFFER: now 50 (stream 0 half full), the least of the last 10 s still 0 after the ring refilled (%s)", c.val);
    smt_stream(0, 0, 0, 0);
    smt_stream(1, 0, 0, 0);
    smt_run(500, 0);
    sys_cell(r, 1, &c);
    smt_ck(!strcmp(c.val, "0/3"), "STREAMS: none now, the most 3 in 10 s (%s)", c.val);
    smt_run(10000, 0);
    sys_cell(r, 1, &c);
    smt_ck(!strcmp(c.val, "0/0"), "STREAMS: the most let go after 10 s (%s)", c.val);
    sys_cell(r, 2, &c);
    smt_ck(!strcmp(c.val, "-"), "BUFFER: no stream fed for 10 s: - (%s)", c.val);
    /* ---- DRY */
    smt_stream(3, 1, 2, 0);
    sst_ran_dry(3);                                     /* the audio: sst_at found no slot */
    smt_ck(sst_dry == 1u && sst[3].dry && !sst[3].on, "a stream that runs dry is counted by the audio (sst_dry 1)");
    smt_run(500, 0);
    sys_cell(r, 3, &c);
    smt_ck(!strcmp(c.label, "DRY") && !strcmp(c.val, "1") && c.col == C_ERR, "DRY: 1, red (%s)", c.val);
    sst_rc[0].late = 2;
    sst_rc[1].miss = 1;
    smt_run(500, 0);
    sys_cell(r, 3, &c);
    smt_ck(!strcmp(c.unit, "LATE 2 MISS 1"), "DRY: GRAIN's and STRETCH's late and missed reads beside it (%s)", c.unit);
    smt_run(9000, 0);
    sys_cell(r, 3, &c);
    smt_ck(c.col == C_ERR, "DRY: still red 10 s after the dry stream");
    smt_run(2000, 0);
    sys_cell(r, 3, &c);
    smt_ck(!strcmp(c.val, "1") && c.col == C_WARN, "DRY: amber once it is not moving (%s)", c.val);
    smt_stream(3, 1, 2, 0);
    sst_ran_dry(3);
    smt_run(500, 0);
    sys_cell(r, 3, &c);
    smt_ck(!strcmp(c.val, "2") && c.col == C_ERR, "DRY: a second one: 2, red again (%s)", c.val);
    /* ---- CRC FAILS and STEPS DOWN take turns on the line, the knob steps them */
    sd_crc_fails = 3;
    sd_demotions = 1;
    sst_nostream = 4;
    sst_cut = 5;                                        /* (tails cut for a newer hit's ring: the CUT item, 2026-11-06) */
    smt_run(500, 0);
    sys_cell(r, 3, &c);
    smt_ck(!strcmp(c.label, "DRY"), "DRY: a stream just dry keeps the line on DRY though the others count");
    smt_run(11000, 0);
    seen = 0;
    for (k = 0; k < 5u; k++) {
        sys_turn(r, 3, 1, 0);                           /* the knob: the next item, held 30 s */
        sys_cell(r, 3, &c);
        strncpy(l[k], c.label, sizeof l[k] - 1u);
        l[k][sizeof l[k] - 1u] = 0;
        seen |= !strcmp(c.label, "DRY") && !strcmp(c.val, "2") ? 1u : !strcmp(c.label, "NO RING") && !strcmp(c.val, "4") ? 2u :
                !strcmp(c.label, "CRC FAILS") && !strcmp(c.val, "3") ? 4u : !strcmp(c.label, "STEPS DOWN") && !strcmp(c.val, "1") ? 8u :
                !strcmp(c.label, "CUT") && !strcmp(c.val, "5") ? 16u : 0u;
        smt_ck(c.col == C_WARN, "DRY: %s %s amber", c.label, c.val);
    }
    smt_ck(seen == 31u, "DRY: five turns of the knob show DRY 2, NO RING 4, CUT 5, CRC FAILS 3, STEPS DOWN 1 (%u)", seen);
    smt_ck(strcmp(l[0], l[1]) && strcmp(l[1], l[2]) && strcmp(l[2], l[3]) && strcmp(l[3], l[4]), "DRY: each turn another item");
    fm1_ms += 5000;
    sys_cell(r, 3, &c);
    smt_ck(!strcmp(c.label, l[4]), "DRY: held where the knob left it 5 s on");
    fm1_ms += 26000;
    sys_cell(r, 3, &c);
    smt_ck(smn_ph() == (fm1_ms >> SMN_PH_SH), "DRY: the clock's turns again 30 s after the last turn");
    /* ---- the clear: YES, SAVE (YES), a knob of another line, SHIFT + the DRY knob */
    smt_ck(sys_yes(r, 3, 0), "SD STREAM: YES on a line is taken");
    smt_run(500, 0);
    sys_cell(r, 3, &c);
    smt_ck(!strcmp(c.label, "DRY") && !strcmp(c.val, "0") && c.col == C_OK, "YES: DRY from 0 again (%s)", c.val);
    sys_cell(r, 0, &c);
    smt_ck(c.hold[0] == 0, "YES: READ's peak from the period's own (%d)", c.hold[0]);
    smt_ck(sst_dry == 2u && sd_crc_fails == 3u && sst_nostream == 4u && sst_cut == 5u, "the counters themselves go on (the console's sd stream): sst_dry %u", sst_dry);
    sst_nostream++;
    smt_run(500, 0);
    sys_cell(r, 3, &c);
    smt_ck(!strcmp(c.label, "NO RING") || !strcmp(c.label, "DRY"), "a new loss after the clear is counted");
    sys_turn(r, 0, 1, 0);
    smt_run(500, 0);
    sys_cell(r, 3, &c);
    smt_ck(!strcmp(c.val, "0") && !strcmp(c.label, "DRY"), "a knob on READ clears too");
    sst_dry++;
    smt_run(500, 0);
    sys_turn(r, 3, OP_RESET, 0);
    smt_run(500, 0);
    sys_cell(r, 3, &c);
    smt_ck(!strcmp(c.val, "0"), "SHIFT + the DRY knob clears");
    /* ---- the real clock: a sample once a period */
    {
        uint32_t t0 = clk;
        clk = t0 + SMT_MS(SSM_MS) - 1u;
        ssm.t = t0;
        ssm_poll();
        smt_ck(ssm.t == t0, "ssm_poll: not before the period ends");
        clk = t0 + SMT_MS(SSM_MS);
        ssm_poll();
        smt_ck(ssm.t == clk, "ssm_poll: a sample when it does");
    }
    /* ---- the page drawn */
    sd_step = 5;
    smt_stream(0, 1, SST_K * 3u / 4u, 0);
    smt_stream(1, 1, SST_K / 2u, 0);
    smt_stream(2, 1, SST_K / 4u, 0);
    sst_st.blocks = 0;
    ssm.blocks = 0;
    smt_run(500, 300);
    smt_run(500, 180);
    sst_dry += 2;
    sst_rc[0].late = 3;
    sd_crc_fails = 2;
    smt_run(500, 120);
    op_enter(SCR_SYSTEM);
    for (i = 0; i < gl_n(1); i++) {                     /* (the screen's rows are the lines of its pages: the first of ours) */
        uint32_t e = gl_ent(1, i);
        if (sys_item(e >> 2, e & 3u) == SI_SMRATE)
            break;
    }
    smt_ck(i < gl_n(1) && gl_first(1, i) == i, "SD STREAM: the four lines make a page of their own (line %u)", i);
    ui.row[SCR_SYSTEM] = (uint8_t)i;
    ui.force = 1;
    frame();
    ppm("opt-sdstream");
    smt_stream(0, 0, 0, 0);
    smt_stream(1, 0, 0, 0);
    smt_stream(2, 0, 0, 0);
    op_enter(SCR_HOME);
}
