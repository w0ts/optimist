/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/editor_sync_test.c with FELUCCA_LAUNCHPAD=1: a Launchpad X on the USB host plays and edits the FM-1 (firmware/src/io/launchpad/:
 * jank's app, third_party/jank, with the FM-1 as its unit, lp_unit.c). The test is the Launchpad: it takes the packets the app sends
 * (lp_out) and gives it what a Launchpad X sends (lp_in): Programmer mode's answer, pad presses. Included after editor_v14.h (its
 * v14_blocks: the audio interrupt's events and render). */
static uint8_t lpt_sel_calls;
static void track_select(uint32_t i) { song.sel = (uint8_t)i; lpt_sel_calls++; }   /* (op_input.c's: what it does to the model) */
static char lpt_said[32];
static void ui_message(const char *m) { str_cpy(lpt_said, m, sizeof lpt_said); }
#include "../firmware/src/io/launchpad/launchpad.c"

static uint32_t lpt_sysex, lpt_lights, lpt_other;  /* what the app sent since lpt_take's last reset */
static uint8_t lpt_first[12];                       /* the first SysEx's first bytes */
static void lpt_take(void)                         /* the Launchpad reads what the app sent */
{
    uint32_t p, k = 0;
    lpt_sysex = lpt_lights = lpt_other = 0;
    while (lp_out(&p)) {
        uint32_t cin = p & 15u;
        if (cin >= 4u && cin <= 7u) {
            if (lpt_sysex < 4u)
                for (uint32_t j = 1; j < 4u && k < sizeof lpt_first; j++)
                    lpt_first[k++] = (uint8_t)(p >> (8u * j));
            lpt_sysex++;
        } else if (cin == 9u || cin == 0xBu) {
            lpt_lights++;
        } else {
            lpt_other++;
        }
    }
}
static void lpt_steps(uint32_t ms)                 /* the main loop for ms: the app's step every 10 ms, the audio blocks between */
{
    for (uint32_t t = 0; t < ms; t += 10u) {
        fm1_ms += 10u;
        lp_step(fm1_ms);
        v14_blocks(2);
    }
}
static void lpt_pad(uint32_t row, uint32_t col, uint32_t vel)   /* a pad (row 0 the bottom) as the X sends it in Programmer mode */
{
    uint32_t note = 10u * (row + 1u) + col + 1u;
    lp_in(0x19u | (vel ? 0x90u : 0x80u) << 8 | note << 16 | vel << 24);   /* (cable 1, CIN 9 / 8) */
}

static void lpt_tests(void)
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        host_preset(&trk[i], 0, 1);
        trk[i].p[P_VOICE] = V_POLY;
        trk[i].p[P_MUTE] = 0;
        memset(trk[i].step, 0, sizeof trk[i].step);
        trk[i].p[P_SLEN] = 16, trk[i].p[P_SDIV] = DUR_DEF_16, trk[i].p[P_SGATE] = 64;
    }
    memset(trk[TRK_DRUM].dstep, 0, sizeof trk[TRK_DRUM].dstep);
    trk[TRK_DRUM].p[P_SLEN] = 16, trk[TRK_DRUM].p[P_SDIV] = DUR_DEF_16;
    song.g[G_BPM] = 120;
    song.solo = 0;
    song.sel = 0;
    transport_req = 2;
    v14_blocks(4);
    v14_stable();
    usb.config = 1;
    fm1_ms += 10000;

    check(lp_name_ok("Launchpad X") && lp_name_ok("Launchpad Mini MK3") && !lp_name_ok("Launchkey Mini MK3") && !lp_name_ok("") && !lp_name_ok(NULL),
          "launchpad: a Launchpad X or Mini MK3 is one; a Launchkey, no name: not");

    /* attach: Programmer mode after the wait, the setup after its answer, the lights after the settle */
    lp_attach("Launchpad X");
    lpt_steps(100);
    lpt_take();
    check(lpt_sysex == 0u && lpt_lights == 0u, "launchpad: nothing sent in its first 500 ms (it settles after plugging in)");
    lpt_steps(500);
    lpt_take();
    check(lpt_sysex == 3u && lpt_first[0] == 0xF0 && lpt_first[1] == 0x00 && lpt_first[2] == 0x20 && lpt_first[3] == 0x29 && lpt_first[5] == 0x0C &&
          lpt_first[6] == 0x0E && lpt_first[7] == 0x01 && lpt_lights == 0u,
          "launchpad: Programmer mode (F0 00 20 29 02 0C 0E 01 F7: three SysEx packets), no light before it answers");
    lp_in(0x04u | 0xF0u << 8 | 0x00u << 16 | 0x20u << 24);   /* its answer: F0 00 20 29 02 0C 0E 01 F7 */
    lp_in(0x04u | 0x29u << 8 | 0x02u << 16 | 0x0Cu << 24);
    lp_in(0x07u | 0x0Eu << 8 | 0x01u << 16 | 0xF7u << 24);
    lpt_steps(20);
    lpt_take();
    check(lpt_sysex >= 1u && lpt_lights == 0u, "launchpad: its answer: the setup SysEx, still no light (the settle)");
    lpt_steps(600);
    lpt_take();
    check(lpt_lights >= 16u, "launchpad: after the settle, the lights (Note mode: the pads in the scale's colours)");

    /* a pad plays the selected track (Note mode, the app's track 0 = T1), its release ends the note */
    lpt_pad(0, 0, 100);
    v14_blocks(2);
    check(lu.nheld == 1u && lu.held[0].trk == 0u && v14_gates() >= 1u, "launchpad: a pad down: a note on T1, held, sounding");
    lpt_pad(0, 0, 0);
    v14_blocks(40);
    check(lu.nheld == 0u && v14_gates() == 0u, "launchpad: the pad up: the note ends");

    /* a lane track: a pad is that lane's hit whatever its pitch */
    lp.app.n.track = LP_LANE0 + 2u;
    lpt_pad(1, 3, 90);
    check(lu.nheld == 1u && lu.held[0].trk == TRK_DRUM && lu.held[0].note == 2u, "launchpad: on jank track 6 (lane 2) a pad hits the drum track's lane 2");
    lpt_pad(1, 3, 0);
    lp.app.n.track = TRK_DRUM;                       /* the drum track as a whole: a note plays the lane whose instrument it is */
    lp_act(&(struct jlapp_act){JLAPP_ACT_NOTE_ON, TRK_DRUM, (uint8_t)LANE_NOTE_OF(1), 100, 0, 0, 0});
    check(lu.nheld == 1u && lu.held[0].note == 1u, "launchpad: on jank track 3 a note hits the lane of its instrument (lane 1's note)");
    lp_act(&(struct jlapp_act){JLAPP_ACT_NOTE_OFF, TRK_DRUM, (uint8_t)LANE_NOTE_OF(1), 0, 0, 0, 0});
    lp.app.n.track = 0;

    /* the lights follow what the FM-1 plays: a step of T2 played comes back as a pushed note */
    trk[1].step[2] = (step_t){{64}, 1, ST_NOTE, 0, 90, 0, 0, {0}};
    transport_req = 1;
    for (i = 0; i < 2u * FS / CTL && !lp.app.recent[0].used; i++) {
        v14_blocks(1);
        if (!(i & 15u)) {
            fm1_ms += 16u * CTL * 1000u / FS;
            lp_step(fm1_ms);
        }
    }
    {
        int seen = 0;
        for (i = 0; i < JLAPP_RECENT; i++)
            seen |= lp.app.recent[i].used && lp.app.recent[i].track == 1u && lp.app.recent[i].note == 64u;
        check(seen && lp.app.transport, "launchpad: T2's step played (note 64) reaches the app's lights; the transport's light is on");
    }
    transport_req = 2;
    v14_blocks(4);
    memset(&trk[1].step[2], 0, sizeof trk[1].step[2]);

    /* the mixer's acts: a lane's pan and VOL, the mutes over the 4 tracks and the lanes */
    lp_act(&(struct jlapp_act){JLAPP_ACT_MIX, LP_LANE0 + 1u, 0, 100, 0, LU_PAN, 0});
    check(dlm_pan[1] == 36, "launchpad: a lane track's PAN fader: the lane's pan (100 is +36)");
#if FELUCCA_DRUM_EDIT
    lp_act(&(struct jlapp_act){JLAPP_ACT_MIX, LP_LANE0 + 1u, 0, 127, 0, LU_VOL, 0});
    check(dl.ofs[1][DE_LEVEL] == LU_DB_MAX && lu_lane_vol(1) == 127u, "launchpad: a lane track's VOL: its LEVEL edit (127 is +6 dB)");
    dl.ofs[1][DE_LEVEL] = 0;
#endif
    lp_act(&(struct jlapp_act){JLAPP_ACT_MUTE, 0, (uint8_t)(1u << 1 | 1u << (LP_LANE0 + 3u)), 0, 0, 0, 0});
    check(trk[1].p[P_MUTE] && !trk[0].p[P_MUTE] && dlm_muted(3) && !dlm_muted(2), "launchpad: MUTE's mask: T2 and lane 3 muted, the rest not");
    lp_act(&(struct jlapp_act){JLAPP_ACT_MUTE, 0, 0, 0, 0, 0, 0});
    dlm_pan[1] = 0;

    /* detach: what the pads held ends, nothing more goes out */
    lpt_pad(2, 2, 100);
    lp_detach();
    v14_blocks(40);
    lpt_steps(50);
    lpt_take();
    check(lu.nheld == 0u && v14_gates() == 0u && lpt_sysex + lpt_lights == 0u && !lv_tap_q.on,
          "launchpad: unplugged: its notes end, the tap stops, nothing is sent");
}
