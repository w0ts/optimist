/* The mirror's workload (MIRROR_CHECK, MIRROR_TRACE=FILE MIRROR_WORKLOAD=1 MIRROR_BT=1; tests/mirror_aimd_test.c replays it): the
 * user's scenarios drawn by the real UI, every write the mirror is given recorded with its pixels (mc_trace), a marker 'M' after
 * each UI frame (16 ms) and 'K' before a frame that a knob, a page or a track changed (the latency's start). Each scenario's
 * bytes a second at the source (the link unlimited) printed: what the link would have to carry to keep up */
#ifdef MIRROR_CHECK
static void ml_mark(uint16_t k) { mc_trace(k, 0, 0, 0, 0, 0, 0); }
static void ml_fr(void) { frame(); ml_mark('M'); }
static void ml_frs(uint32_t n) { while (n--) ml_fr(); }
static uint32_t ml_b0, ml_f0;
static void ml_begin(void) { ml_b0 = mc_bytes, ml_f0 = mc_frames; }
static void ml_end(const char *what, uint32_t nframes)
{
    double s = nframes * 16 / 1000.0;
    printf("  mirror workload: %-38s %6.0f B/s %5.1f frames/s\n", what, (mc_bytes - ml_b0) / s, (mc_frames - ml_f0) / s);
}
static void ml_knob(uint32_t role, int32_t s) { ml_mark('K'); encs[panel.enc[role]] += s; ml_fr(); }
static void ml_drags(uint32_t role)     /* a detent each frame for 1 s, then 1.5 s still; three times, back and forth */
{
    uint32_t i, k;
    for (k = 0; k < 3u; k++) {
        for (i = 0; i < 60u; i++)
            ml_knob(role, (k & 1u) ? -1 : 1);
        ml_frs(94);
    }
}
static void mirror_workload(void)
{
    static const uint8_t PG[] = {SCR_SOUND, SCR_FX, SCR_STEP, SCR_HOME, SCR_SOUND, SCR_GLOBAL, SCR_HOME, SCR_FX};
    uint32_t k;
    ml_mark('M');
    ml_begin(); ml_frs(188); ml_end("HOME idle", 188);
    op_enter(SCR_SOUND); ml_frs(30);
    ml_begin(); ml_drags(EN_K1); ml_end("SOUND knob drags (1 s on, 1.5 s off)", 3u * 154u);
    ml_begin();
    for (k = 0; k < 12u; k++) {
        ml_knob(EN_K2, 1);
        ml_frs(30);
    }
    ml_end("SOUND single detents 2/s", 12u * 31u);
    op_enter(SCR_HOME); ml_frs(30);
    tap(B_PLAY); ml_frs(60);
    ml_begin(); ml_frs(250); ml_end("HOME playing", 250);
    ml_begin(); ml_drags(EN_K1); ml_end("HOME playing + knob drags", 3u * 154u);
    ml_begin();
    for (k = 0; k < 8u; k++) {
        ml_knob(EN_ALGO, (k & 2u) ? -1 : 1);
        ml_frs(30);
    }
    ml_end("HOME playing + ALGORITHM every 0.5 s", 8u * 31u);
    tap(B_PLAY); ml_frs(30);
    ml_begin();
    for (k = 0; k < sizeof PG; k++) {
        ml_mark('K');
        op_enter(PG[k]);
        ml_frs(47);
    }
    ml_end("page changes every 0.75 s", (uint32_t)sizeof PG * 47u);
    ml_frs(60);
}
#endif
