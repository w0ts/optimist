/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the button LEDs against what the buttons do (the user, 2026-10-13, the mixer on DR: "the
 * buttons that are lit up are wrong"): for every screen x track kind (a synth track, the drum track with no lane, a drum
 * lane, MASTER) and every panel button but PLAY, REC and OCT (always on), the LED is lit (half or full) exactly when a
 * tap changes what the screen shows, or a hold works a layer (ly_bit: the layer's keys and knobs). A button that does
 * neither is dark. The measured table is docs/UI-OPTIMIST-DESIGN.md section 14. Included after led_lvl (the LED test). */
enum { BK_SYN, BK_DR, BK_LANE, BK_MASTER, BK_N };
static const char *const BK_NAME[BK_N] = {"synth", "DR", "lane", "MASTER"};
static const uint8_t BK_SCR[] = {SCR_HOME, SCR_SOUND, SCR_STEP, SCR_FX, SCR_GLOBAL, SCR_PROJECT, SCR_SYSTEM, SCR_SONG, SCR_TEMPO,
                                 SCR_SCOPE};
static void bk_set(uint32_t kind, uint32_t scr)
{
    reset_ui();
    song.sel = kind == BK_SYN || kind == BK_MASTER ? 0 : TRK_DRUM;
    ui.lane = 0;
    if (kind == BK_LANE) {
        lane_select(4);
        ui.lane = 1;
    }
    ui.row[SCR_HOME] = (uint8_t)(kind == BK_MASTER ? MXR_MASTER : kind == BK_SYN ? MXR_T1 : kind == BK_DR ? MXR_DR : MXR_LANE0 + 4);
    ui.force = 1;
    frames(2);
    if (scr == SCR_HOME)
        return;
    op_enter(scr);
    if (scr == SCR_SOUND)
        snd_fam = SND_ALL;
    if (scr == SCR_FX && kind != BK_MASTER)
        ui.master = 1;
    frames(2);
}
static uint32_t bk_sig(void)
{
    return ui.scr | (uint32_t)snd_fam << 4 | (uint32_t)ui.row[ui.scr] << 12 | (uint32_t)ui.arm_scr << 20 | (uint32_t)(st.held != 0) << 28;
}
static int bk_step_one(uint32_t scr) { return scr == SCR_STEP && SCR->rows() < 2u; }
static void btnled_tests(void)
{
    static const char *const BN[] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ"};
    uint32_t k, s, b, bad, bad_b = 0, n = 0;
    for (k = 0; k < BK_N; k++)
        for (s = 0; s < sizeof BK_SCR; s++) {
            if (k == BK_MASTER && BK_SCR[s] != SCR_HOME && BK_SCR[s] != SCR_FX)
                continue;                               /* (MASTER is the mixer's row and the master FX only) */
            bad = 0;
            for (b = 0; b < NB; b++) {
                uint32_t lv, act, l, sig0;
                if (b == B_PLAY || b == B_REC || b == B_OCTDN || b == B_OCTUP)
                    continue;
                bk_set(k, BK_SCR[s]);
                lv = led_lvl(b);
                sig0 = bk_sig();
                tap(b);
                frames(2);
                act = sig0 != bk_sig();
                bk_set(k, BK_SCR[s]);
                press(b);
                frames(HOLD_FRAMES);
                l = lay_btn_layer(b);
                if (b != B_LFO)                         /* (LFO's held layer, the patterns, is not counted: section 14) */
                    act |= l != LY_PLAY && ly_bit[l] != 0u && lay.shown != LY_PLAY;
                release(b);
                n++;
                if (b == B_SEQ && bk_step_one(BK_SCR[s]))
                    continue;                   /* (STEP on its one page, a build without the tools: SEQ marks the screen, there is no next page) */
                if ((lv != 0u) != (act != 0u) && !bad++)
                    bad_b = b;
            }
            if (bad) {
                char w[96];
                snprintf(w, sizeof w, "button LEDs, %s on screen %u: %s's LED does not match what it does", BK_NAME[k], BK_SCR[s],
                         BN[bad_b]);
                check(0, w);
            }
        }
    {   /* OCT- / OCT+ (the user, 2026-10-14): lit only where a tap or a hold + something does something */
        bk_set(BK_SYN, SCR_HOME);
        check(led_lvl(B_OCTDN) == 1 && led_lvl(B_OCTUP) == 1, "OCT LEDs, the mixer on a synth track: both dim (SHIFT, SHIFT2 combos)");
        bk_set(BK_MASTER, SCR_HOME);
        check(led_lvl(B_OCTDN) == 0 && led_lvl(B_OCTUP) == 0, "OCT LEDs, the mixer on MASTER: both dark");
        bk_set(BK_SYN, SCR_SOUND);
        check(led_lvl(B_OCTDN) == 1 && led_lvl(B_OCTUP) == 1, "OCT LEDs, SOUND on a synth: both dim (the octave)");
        bk_set(BK_SYN, SCR_STEP);
        check(led_lvl(B_OCTDN) == 1 && led_lvl(B_OCTUP) == 1, "OCT LEDs, STEP on a synth: both dim");
        bk_set(BK_DR, SCR_STEP);
        check(led_lvl(B_OCTDN) == 1 && led_lvl(B_OCTUP) == 1, "OCT LEDs, STEP on DR: both dim (ghost / hard)");
        bk_set(BK_DR, SCR_SOUND);
        check(led_lvl(B_OCTDN) == 1 && led_lvl(B_OCTUP) == 0, "OCT LEDs, SOUND on DR: OCT- dim (SHIFT), OCT+ dark (no octave on drums)");
        bk_set(BK_SYN, SCR_PROJECT);
        check(led_lvl(B_OCTDN) == 0 && led_lvl(B_OCTUP) == 0, "OCT LEDs, PROJECT: both dark");
        bk_set(BK_SYN, SCR_SYSTEM);
        check(led_lvl(B_OCTDN) == 0 && led_lvl(B_OCTUP) == 0, "OCT LEDs, SYSTEM: both dark");
        bk_set(BK_SYN, SCR_GLOBAL);
        check(led_lvl(B_OCTUP) == 0, "OCT LEDs, GLOBAL (off the tempo line): OCT+ dark");
        bk_set(BK_SYN, SCR_FX);
        check(led_lvl(B_OCTDN) == 0 && led_lvl(B_OCTUP) == 0, "OCT LEDs, the master FX: both dark");
        bk_set(BK_SYN, SCR_SCOPE);
        check(led_lvl(B_OCTDN) == 0 && led_lvl(B_OCTUP) == 0, "OCT LEDs, SCOPE: both dark");
    }
    check(n > 150u, "button LEDs: every screen x track kind x button was walked");
    bk_set(BK_DR, SCR_HOME);
    check(led_lvl(B_LFO) == 0 && led_lvl(B_ENV) == 0 && led_lvl(B_EDIT) == 1, "button LEDs, the mixer on DR (no lane): LFO and ENV dark, EDIT half");
    bk_set(BK_LANE, SCR_HOME);
    check(led_lvl(B_LFO) == 1 && led_lvl(B_ENV) == (DSND_ENV ? 1u : 0u), "button LEDs, the mixer on a lane: LFO and ENV half");
    bk_set(BK_MASTER, SCR_HOME);
    check(led_lvl(B_ENV) + led_lvl(B_LFO) + led_lvl(B_EDIT) + led_lvl(B_ARP) + led_lvl(B_SCL) + led_lvl(B_SEQ) == 0u && led_lvl(B_FX) == 1,
          "button LEDs, the mixer on MASTER: the track buttons dark, FX half");
    bk_set(BK_MASTER, SCR_HOME);
    press(B_SCL);
    frames(HOLD_FRAMES);
    check(lay.shown == LY_PLAY, "button LEDs, MASTER: SCL held shows no layer map");
    release(B_SCL);
    bk_set(BK_SYN, SCR_HOME);
}
