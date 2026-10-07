/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4 phase 5: the visualiser (FELUCCA_VIS), included by ui_pages_test.c; the block runs only with the switch on.
 * HOME on TRACKS opens it; the audio tap copies the blocks; SELECT steps the 12 styles (round); the style is in the
 * settings word; KNOB 1 does nothing; a layer shows its screen and the visualiser comes back; HOME closes it. */
static void sl24p5_vis_tests(void)
{
#if FELUCCA_VIS
    uint32_t st, wr0, i, lit;
    int16_t b0;
    song.sel = 0; song.playing = 0; go_home(); frames(20);
    check(!vis_on && cur_page()->scope == SC_TRK, "vis: on TRACKS, closed");
    wr0 = vis_wr;
    trk_note_on(&trk[0], 57, 120);
    tap(B_HOME); frames(6);
    check(vis_on && vis_shown(), "vis: HOME tapped on TRACKS: the visualiser opens");
    check(vis_wr != wr0, "vis: the audio tap copied blocks (vis_wr moved)");
    for (i = lit = 0; i < VIS_FFT; i++) lit |= vis_l[i] != 0;
    check(lit != 0, "vis: the scope sees the note sounding");
    ui.force = 1; frames(2); ppm("vis-style-1-oscilloscope");
    b0 = song.g[G_BPM];
    for (st = 1; st < 12u; st++) {
        encs[panel.enc[EN_SELECT]] = 1; frames(4);
        if (st == 11u) { trk_note_on(&trk[1], 62, 120); frames(3); ppm("vis-style-12-sloop"); }
    }
    check(vis_style == 11 && song.g[G_BPM] == b0, "vis: SELECT x11: style 12 of 12, the tempo untouched");
    encs[panel.enc[EN_SELECT]] = 1; frames(4);
    check(vis_style == 0, "vis: SELECT once more: round to style 1");
    encs[panel.enc[EN_SELECT]] = -1; frames(4);
    check(vis_style == 11, "vis: SELECT left: round to style 12");
    for (st = 0; st < 12u; st++) { vis_style = (uint8_t)st; ui.force = 1; trk_note_on(&trk[0], 50u + st, 120); frames(3); }
    check(vis_shown(), "vis: every one of the 12 styles drew (the host blit asserts the screen)");
    encs[panel.enc[EN_K1]] = 3; frames(2);
    check(vis_shown(), "vis: KNOB 1 does nothing to it");
    press(B_FX); frames(30);
    check(ui.layer == LY_FX && vis_on, "vis: a layer held shows its screen, the visualiser waits");
    release(B_FX); frames(6);
    check(vis_shown(), "vis: ... and is back when the layer is let go");
    song.master_q12 = 0; trk_note_on(&trk[0], 57, 120); frames(4);
    for (i = lit = 0; i < VIS_FFT; i++) lit |= vis_l[i] != 0;
    check(lit != 0, "vis: MASTER at 0: the picture still shows the sound (as if MASTER were up)");
    song.master_q12 = 2048;
    tap(B_HOME); frames(3);
    check(!vis_on && !vis_shown() && cur_page()->scope == SC_TRK, "vis: HOME again: closed, the TRACKS screen");
    open_family(FAM_ENV); frames(2); tap(B_HOME); frames(2);
    check(!vis_on && cur_page()->scope == SC_TRK, "vis: HOME on another page: TRACKS (not the visualiser)");
    encs[panel.enc[EN_SELECT]] = 1; frames(2);
    check(song.g[G_BPM] != b0, "vis: closed: SELECT is the tempo again");
    song.g[G_BPM] = 120; vis_style = 0; go_home(); frames(4);
#endif
}
