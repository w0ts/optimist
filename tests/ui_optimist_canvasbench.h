/* CANVAS_BENCH=1: what the canvas's rows cost the drawing, on the host (build with -DFELUCCA_CANVAS_STRIP=n and compare). For
 * each screen a repaint drawn whole (op_repaint, then one frame), then some updates a user makes: a knob detent, a step key,
 * a frame while playing. Per frame: the drawing's CPU (the host's, ui_draw alone: a model, not the FM-1's cycles), the blits
 * (strips) it sent and the pixels in them (the LCD's DMA time is the pixels: 240 x 62 take 5.4 ms at LCD_BAUD 0, so the same
 * pixels cost the same; what more strips add is each one's window set-up, the clear and the clipped paint run again). */
static void cb_run(const char *what, void (*act)(void), uint32_t n)
{
    uint64_t t0 = draw_ns;
    uint32_t b0 = blit_n, p0 = blit_px, i;
    for (i = 0; i < n; i++)
        act();
    printf("  canvas bench %-34s %8.1f us/action %6.2f strips/action %8.0f px/action\n", what, (double)(draw_ns - t0) / 1e3 / n,
           (double)(blit_n - b0) / n, (double)(blit_px - p0) / n);
}
static void cb_repaint(void) { op_repaint(); frame(); }
static void cb_knob(void) { turn(EN_ALGO, 1); turn(EN_ALGO, -1); }
static void cb_step_key(void) { key(3); }
static void cb_idle(void) { frame(); }
static void cb_screen(const char *name, uint32_t scr)
{
    char b[64];
    reset_ui();
    op_enter(scr);
    frames(4);
    snprintf(b, sizeof b, "repaint, %s", name);
    cb_run(b, cb_repaint, 100);
}
static void canvas_bench(void)
{
    printf("  canvas bench: %u rows (CV_MAX %u px)\n", (unsigned)CV_ROWS, (unsigned)CV_MAX);
    cb_screen("HOME", SCR_HOME);
    cb_screen("SOUND", SCR_SOUND);
    cb_screen("FX", SCR_FX);
    cb_screen("STEP", SCR_STEP);
    cb_screen("SONG", SCR_SONG);
    cb_screen("SCOPE", SCR_SCOPE);
    cb_screen("SYSTEM", SCR_SYSTEM);
    cb_screen("GLOBAL (mixer)", SCR_GLOBAL);
    reset_ui();
    op_enter(SCR_HOME);
    frames(4);
    cb_run("HOME: a knob detent there and back", cb_knob, 100);
    reset_ui();
    op_enter(SCR_STEP);
    frames(4);
    cb_run("STEP: a step key down and up", cb_step_key, 100);
    tap(B_PLAY);
    frames(8);
    cb_run("STEP playing: a frame (playhead)", cb_idle, 400);
    tap(B_PLAY);
    reset_ui();
    op_enter(SCR_GLOBAL);
    frames(4);
    tap(B_PLAY);
    frames(8);
    cb_run("mixer playing: a frame (meters, steps)", cb_idle, 400);
    tap(B_PLAY);
    printf("canvas bench done\n");
}
