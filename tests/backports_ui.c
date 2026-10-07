/* SPDX-License-Identifier: GPL-3.0-only */
/* The backported features' UI (firmware/src/backports.h), included by ui_pages_test.c: each block only with
 * its switch on (tests/run_tests.sh builds ui_pages_test with the switches of the run).
 *   chance   SEQ > STEP 2 on a synth track, KNOB 2 sets the cursor step's chance; not on the drum track
 *   spring   FX > REVERB: TYPE ROOM / SPRING switches the bus
 *   bass+    MENU > LOWCUT: OFF / LOWCUT / BASS+ (fx_lowcut 2), ZOOM leaves it
 *   bright   MENU > BRIGHT: 8 (full) .. 1, the PWM duty (never below 4/16); a boot starts at full
 *   motion   SEQ > MOTION: PLAY on, CLEAR twice
 *   acid     ACID GEN on an ACID track (GO twice writes a line), not on other engines
 *   keylit   the keys of the notes the selected synth track plays (sequencer, ARP, held voices) light up:
 *            the lowest key of a note, the octave, nothing on a layer or with nothing playing */
static void backport_ui_tests(void)
{
#if FELUCCA_CHANCE
    {
        uint32_t guard = 0;
        song.sel = 0; song.playing = 0; go_home(); frame();
        steps_clear(&trk[0]);
        trk[0].p[P_SLEN] = 16;
        trk[0].step[2].n = 1; trk[0].step[2].note[0] = 60; trk[0].step[2].time = ST_NOTE;
        open_family(FAM_SEQ); frame();
        while (cur_page()->id[1] != STEP_ID_CHANCE && guard++ < 8u)
            tap(B_SEQ);
        check(cur_page()->id[1] == STEP_ID_CHANCE, "chance: SEQ again on a synth track: STEP 2");
        cursor_set(2); frame();
        encs[panel.enc[EN_K2]] = -10; frames(2);
        check(step_chance(&trk[0].step[2]) == 50u, "chance: STEP 2 KNOB 2 -10 detents: the step at 50 %");
        ui.force = 1; frame(); ppm("page-step2-chance");
        cursor_set(3); frame();
        encs[panel.enc[EN_K2]] = -4; frames(2);
        check(step_chance(&trk[0].step[3]) == 100u, "chance: an empty step takes no chance");
        song.sel = TRK_DRUM; go_home(); frame();
        open_family(FAM_SEQ); frame();
        for (guard = 0; guard < 6u; guard++) {
            tap(B_SEQ);
            if (cur_page()->id[1] == STEP_ID_CHANCE)
                break;
        }
        check(cur_page()->id[1] != STEP_ID_CHANCE, "chance: the drum track's SEQ pages have no STEP 2");
        song.sel = 0; go_home(); frame();
        steps_clear(&trk[0]);
    }
#endif
#if FELUCCA_SPRING
    {
        uint32_t guard = 0;
        song.sel = 0; go_home(); frame();
        open_family(FAM_FX); frame();
        while (cur_page()->scope != SC_BPSET && guard++ < 8u)
            tap(B_FX);
        check(cur_page()->scope == SC_BPSET && cur_page()->id[0] == BPS_RTYPE, "spring: FX pages: REVERB (TYPE)");
        encs[panel.enc[EN_K1]] = 1; frames(2);
        check(bp_set[BPS_RTYPE] == 1 && sp.type == 1, "spring: KNOB 1 right: SPRING, the bus switched");
        ui.force = 1; frame(); ppm("page-reverb-spring");
        encs[panel.enc[EN_K1]] = -1; frames(2);
        check(bp_set[BPS_RTYPE] == 0 && sp.type == 0, "spring: KNOB 1 left: ROOM again");
        go_home(); frame();
    }
#endif
#if FELUCCA_BASSPLUS
    {
        uint32_t keep = settings.lowcut;
        ui.menu = 1; ui.menu_sel = MI_LOWCUT; ui.force = 1; frame();
        settings.lowcut = 0; fx_lowcut = 0;
        encs[panel.enc[EN_K1]] = 1; frames(2);
        check(settings.lowcut == 1u && fx_lowcut == 1u, "bass+: MENU LOWCUT, KNOB 1 right: LOWCUT");
        encs[panel.enc[EN_K1]] = 1; frames(2);
        check(settings.lowcut == 2u && fx_lowcut == 2u, "bass+: once more: BASS+");
        ui.force = 1; frame(); ppm("menu-bassplus");
        encs[panel.enc[EN_K1]] = 1; frames(2);
        check(settings.lowcut == 2u, "bass+: BASS+ is the last value");
        ui.menu_sel = MI_ZOOM; encs[panel.enc[EN_K1]] = 1; frames(2);
        check(settings.zoom == 1u && fx_lowcut == 2u, "bass+: ZOOM on leaves BASS+ alone");
        encs[panel.enc[EN_K1]] = -1; frames(2);
        settings.lowcut = keep; fx_lowcut = (uint8_t)keep;
        ui.menu = 0; ui.force = 1; frame();
    }
#endif
#if FELUCCA_BRIGHT
    {
        ui.menu = 1; ui.menu_sel = MI_BRIGHT; ui.force = 1; frame();
        check(bright_level() == 8u && BL_DUTY[bl_dim] == 16u, "bright: full by default (duty 16 of 16)");
        encs[panel.enc[EN_K1]] = -3; frames(2);
        check(bright_level() == 5u && BL_DUTY[bl_dim] == 10u, "bright: MENU BRIGHT, KNOB 1 left 3: level 5, duty 10");
        ui.force = 1; frame(); ppm("menu-bright");
        encs[panel.enc[EN_K1]] = -9; frames(2);
        check(bright_level() == 1u && BL_DUTY[bl_dim] == 4u, "bright: level 1 is duty 4 of 16, never X0X's frozen 1/16");
        {
            uint32_t k, lo = 16;
            for (k = 0; k < 8u; k++)
                lo = BL_DUTY[k] < lo ? BL_DUTY[k] : lo;
            check(lo >= 4u, "bright: no step below 4/16 (X0X issue #2 froze at 1/16)");
        }
        bright_boot();                               /* a boot with level 1 saved: persist_boot calls this */
        check(bright_level() == 8u && BL_DUTY[bl_dim] == 16u, "bright: boot with a saved level 1: full (duty 16)");
        bright_set(1);
        encs[panel.enc[EN_K1]] = 9; frames(2);
        check(bright_level() == 8u, "bright: back to 8");
        ui.menu = 0; ui.force = 1; frame();
    }
#endif
#if FELUCCA_MOTION
    {
        uint32_t guard = 0;
        song.sel = 0; song.playing = 0; go_home(); frame();
        memset(&motion, 0, sizeof motion);
        open_family(FAM_SEQ); frame();
        while (cur_page()->scope != SC_MOTION && guard++ < 8u)
            tap(B_SEQ);
        check(cur_page()->scope == SC_MOTION, "motion: SEQ pages: MOTION");
        motion_set_event(&trk[0], 3, P_CHOR, 50);
        motion_set_enabled(&trk[0], 0);
        encs[panel.enc[EN_K1]] = 1; frames(2);
        check(motion.on & 1u, "motion: KNOB 1 right: PLAY on");
        ui.force = 1; frame(); ppm("page-motion");
        encs[panel.enc[EN_K4]] = 1; frames(2);
        check(motion.count == 1u, "motion: CLEAR: one detent only arms");
        encs[panel.enc[EN_K4]] = 1; frames(2);
        check(motion.count == 0u && !(motion.on & 1u), "motion: CLEAR again: the track's events gone");
        go_home(); frame();
    }
#endif
#if FELUCCA_ENG_ACID
    {
        uint32_t guard = 0, notes = 0, i;
        song.sel = 0; song.playing = 0; go_home(); frame();
        set_engine_of(&trk[0], ENG_IX_ACID); frames(2);
        trk[0].p[P_SLEN] = 16;
        steps_clear(&trk[0]);
        open_family(FAM_EDIT); frame();
        while (cur_page()->scope != SC_BPSET && guard++ < 10u)
            tap(B_EDIT);
        check(cur_page()->scope == SC_BPSET && cur_page()->id[0] == BPS_GDENS, "acid: EDIT pages of an ACID track: ACID GEN");
        encs[panel.enc[EN_K4]] = 1; frames(2);
        for (i = 0; i < 16u; i++)
            notes += trk[0].step[i].time == ST_NOTE;
        check(notes == 0u, "acid: GEN: one detent only arms");
        encs[panel.enc[EN_K4]] = 1; frames(2);
        for (i = 0; i < 16u; i++)
            notes += trk[0].step[i].time == ST_NOTE;
        check(notes > 0u, "acid: GEN again: a new line in the pattern");
        ui.force = 1; frame(); ppm("page-acid-gen");
        set_engine_of(&trk[0], 0); frames(2);
        open_family(FAM_EDIT); frame();
        for (guard = 0; guard < 10u; guard++) {
            tap(B_EDIT);
            if (cur_page()->scope == SC_BPSET && cur_page()->id[0] == BPS_GDENS)
                break;
        }
        check(guard == 10u, "acid: not on an ANALOG track");
        steps_clear(&trk[0]);
        go_home(); frame();
    }
#endif
#if FELUCCA_KEYLIT
    {
        track_t *t = &trk[0];
        uint32_t i;
        song.sel = 0; song.playing = 0; go_home(); frame();
        t->p[P_CHORD] = 0; t->p[P_QUANT] = Q_OFF; t->p[P_ROOT] = 0; t->p[P_TRANS] = 0;
        song.octave = 0;
        for (i = 0; i < NVOICE; i++)
            t->v[i].active = 0;
        t->seq_n = 0; t->arp_note = 0;
        check(keys_lit() == 0u, "keylit: nothing plays: no key lit");
        t->seq_n = 1; t->seq_notes[0] = 60;                    /* C4 = key 7 (53 + 7) */
        check(keys_lit() == 1u << 7, "keylit: a sequenced C4 lights key 7");
        t->seq_n = 0; t->arp_note = 62;
        check(keys_lit() == 1u << 9, "keylit: the ARP's D4 lights key 9");
        t->arp_note = 0;
        t->v[0].active = 1; t->v[0].gate = 1; t->v[0].stage = 2; t->v[0].note = 64;
        check(keys_lit() == 1u << 11, "keylit: a held voice (MIDI in) E4 lights key 11");
        t->v[0].gate = 0; t->v[0].stage = 3;
        check(keys_lit() == 0u, "keylit: released (its tail) is dark");
        t->v[0].active = 0;
        song.octave = -1;
        t->seq_n = 1; t->seq_notes[0] = 60;
        check(keys_lit() == 1u << 19, "keylit: OCT-: C4 on key 19");
        song.octave = 0;
        t->p[P_QUANT] = Q_SNAP; t->p[P_SCALE] = 1;             /* SNAP: C# rounds down onto C: key 7 only */
        check(keys_lit() == 1u << 7, "keylit: SNAP: the lowest key of a note only");
        t->p[P_QUANT] = Q_OFF; t->p[P_SCALE] = 0;
        song.sel = TRK_DRUM;
        check((keys_lit() & 1u << 7) == 0u, "keylit: the drum track: its own hits, not a part's notes");
        song.sel = 0;
        t->seq_n = 0;
        frame();
    }
#endif
}
