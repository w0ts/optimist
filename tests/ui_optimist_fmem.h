/* The switchable features on the screen (core/feat_mem.c; tests/fmem_fake.h's stand-in radio; built with UI_FMEM): the
 * memory line (op_draw.c memline_draw) follows BT AUDIO's block on the frame right after the main loop's pass that
 * placed or gave it back, and in every frame of a switch-on that asks first (PAUSE n MS?), the question drawn, the
 * pack, the placement: the line drawn is always the arena's fill of that moment. INFO's RAM USE line splits it */
#define FMCHK(c, ...) do { char fm_m[200]; snprintf(fm_m, sizeof fm_m, __VA_ARGS__); check((c), fm_m); } while (0)
static uint32_t fm_ml_drawn(void)                       /* the memory line's lit width on the screen, px */
{
    uint32_t x = 0;
    while (x < 240u && !px_at(x, 0, C_LINE))
        x++;
    return x;
}
static int fm_ml_now(void) { return fm_ml_drawn() == memline_px(info_ram_pct()); }
static uint32_t fm_bad_frames;
static void fm_step(void)                               /* a main loop pass (the arena, the buffers, the radio), then a frame */
{
    fxm_poll();
    arb_poll();
    radio();
    frame();
    fm_bad_frames += !fm_ml_now();
}
static void fm_no_idle(void)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (arn_cur[k].len && arn_cur[k].idle) {
            arn_give(k);
            part_mem_p[k] = 0;
        }
}
static void fmem_ui_tests(void)
{
    uint32_t w0, w1, i, FM = arn_round(part_mem_bytes(ENG_IX_FM6)), B = arn_round(RB_BYTES), K = FXM_CP_K0 + FXM_CPN - 1u;
    cell_t c;
    reset_ui();
    for (i = 0; i < NPART; i++)
        set_engine_of(&trk[i], eng_silent_slot());
    frames(20);
    fm_no_idle();
    op_enter(SCR_HOME);
    ui.force = 1;
    for (i = 0; i < 8u; i++)
        fm_step();                                      /* (the effects' lines placed first: the main loop's passes) */
    w0 = fm_ml_drawn();
    FMCHK(fm_ml_now(), "features: the memory line drawn as the arena's fill (%u px)", (unsigned)w0);
    fm_bad_frames = 0;
    bta_set(1);
    fm_step();
    w1 = fm_ml_drawn();
    FMCHK(arn_ptr(ARB_K(ARB_BTA)) && w1 > w0 && fm_ml_now(),
          "features: BT AUDIO on: its block placed, the line longer on the frame right after (%u -> %u px)", (unsigned)w0,
          (unsigned)w1);
#if FELUCCA_SYS_DEBUG || 1
    sys_it_cell(SI_RAMUSE, &c);
    FMCHK(c.label && !strcmp(c.label, "ARENA") && strstr(c.val, "/18/") != 0,
          "features: INFO's ARENA line splits it: \"%s\" KB (parts / radios / free)", c.val);
#endif
    bta_set(0);
    for (i = 0; i < 12u && arn_ptr(ARB_K(ARB_BTA)); i++)
        fm_step();
    fm_step();
    FMCHK(!arn_ptr(ARB_K(ARB_BTA)) && fm_ml_drawn() == w0, "features: off: given back, the line back to %u px",
          (unsigned)fm_ml_drawn());
    /* the switch-on that asks first: two FM6 parts side by side, the lower one idle, the upper one sounding, the room
     * above them a unit short of the block */
    set_engine_of(&trk[0], ENG_IX_FM6);
    set_engine_of(&trk[1], ENG_IX_FM6);
    for (i = 0; i < 4u; i++)
        fm_step();
    {
        uint32_t lo = arn_cur[0].off < arn_cur[1].off ? 0u : 1u, hi = 1u - lo, n = arn_free_bytes() - (B - 256u);
        if (!arn_reserve(K, n))
            (void)arn_take(K);
        set_engine_of(&trk[lo], eng_silent_slot());
        for (i = 0; i < 8u; i++)
            fm_step();
        trk_note_on(&trk[hi], 60, 110);
        fm_step();
        w0 = fm_ml_drawn();
        bta_set(1);
        for (i = 0; i < 8u && !op_armed(); i++)
            fm_step();
        FMCHK(op_armed() && !strncmp(ui.arm_verb, "PAUSE ", 6) && !strcmp(ui.arm_arg, "BT AUDIO 18 KB") &&
              fm_ml_drawn() == w0, "features: no room but after a pack: asked \"%s\" / \"%s\", the line as it was",
              ui.arm_verb, ui.arm_arg);
        op_yes();
        for (i = 0; i < 64u && !arn_ptr(ARB_K(ARB_BTA)); i++)
            fm_step();
        fm_step();
        FMCHK(arn_ptr(ARB_K(ARB_BTA)) && fm_ml_drawn() > w0 && arn_cur[hi].off < FM,
              "features: YES: packed and placed, the line longer (%u -> %u px)", (unsigned)w0, (unsigned)fm_ml_drawn());
        trk_note_off(&trk[hi], 60);
        bta_set(0);
        for (i = 0; i < 16u; i++)
            fm_step();
        arn_give(K);
    }
    FMCHK(!fm_bad_frames, "features: every frame of it drew the fill of its moment (%u frames off)", (unsigned)fm_bad_frames);
    reset_ui();
}
