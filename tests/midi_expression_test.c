/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChanceTheMaker, Kerem Kilic (Melodee: keremimo/melodee 670193c, tests/midi_expression_test.c)
 * Adapted for SLOOP (the held-note table, our note input) */
/* MIDI expression: the real queue -> sequencer -> voices, for USB-format packets and TRS bytes.
 * Pitch bend (range, smoothing, RPN 0), mod wheel vibrato, sustain, CC120 / 121 / 123, routing. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static int via_trs;
static int32_t render_buf[CTL];

static void reset_test(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_held, 0, sizeof midi_held);
    memset(kb_kind, 0, sizeof kb_kind);
    memset(&um, 0, sizeof um);
    memset(sl, 0, sizeof sl);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    midi_nheld = 0;
    host_tracks_init();
    mi_r = mi_w = kb_prev = fm1_in.notes = panic_req = transport_req = 0;
    for (i = 0; i < NPART; i++) {
        host_preset(&trk[i], 0, 1);
        trk[i].p[P_VOICE] = V_POLY;
        trk[i].p[P_AMODE] = trk[i].p[P_AHOLD] = 0;
        trk[i].p[P_ATK] = 0;
        trk[i].p[P_SUS] = 127;
        trk[i].p[P_REL] = 100;
        trk[i].p[P_GLIDE] = trk[i].p[P_LD_PIT] = trk[i].p[P_ED_PIT] = 0;
        trk[i].p[P_E0] = 3;               /* sine, no detune / noise */
        trk[i].p[P_E1] = trk[i].p[P_E3] = 0;
    }
}

static void send_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (via_trs) {
        um_byte(st, 0); um_byte(d1, 0);
        if ((st & 0xE0u) != 0xC0u)              /* (program change, channel pressure: one data byte) */
            um_byte(d2, 0);
    } else {
        assert(mi_w - mi_r < MQ);
        midi_in_q[mi_w++ % MQ] = (st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24);
    }
    events_block(CTL);
}

static int gated(uint32_t part, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (trk[part].v[i].active && trk[part].v[i].gate && trk[part].v[i].note == note)
            return 1;
    return 0;
}

static void settle(track_t *t)
{
    uint32_t i;
    for (i = 0; i < 100; i++)
        track_render(t, render_buf, CTL);
}

static uint32_t held_on(uint32_t track)
{
    uint32_t i, n = 0;
    for (i = 0; i < midi_nheld; i++)
        n += midi_held[i].trk == track;
    return n;
}

static void pitch_test(void)
{
    track_t *t = &trk[0];
    uint32_t p0, i;
    reset_test();
    send_midi(0x90, 69, 90);
    send_midi(0xE0, 127, 127);
    assert(t->bend_target == 512 && trk[1].bend_target == 0);
    track_render(t, render_buf, CTL);
    assert(t->bend_q8 > 0 && t->bend_q8 < 512);  /* smoothed, not a jump */
    settle(t);
    p0 = t->v[0].ph[0];
    track_render(t, render_buf, CTL);
    assert(t->v[0].ph[0] - p0 == PITCH_INC[71 * 16] * CTL);
    assert(t->v[0].note == 69);                   /* its note-off identity is not bent */
    send_midi(0xE0, 0, 0);
    settle(t);
    assert(t->bend_q8 == -512);
    p0 = t->v[0].ph[0];
    track_render(t, render_buf, CTL);
    assert(t->v[0].ph[0] - p0 == PITCH_INC[67 * 16] * CTL);
    send_midi(0xE0, 0, 64);
    settle(t);
    assert(t->bend_q8 == 0);
    send_midi(0xE0, 80, 64);                     /* less than 1/16 semitone still moves the pitch */
    settle(t);
    p0 = t->v[0].ph[0];
    track_render(t, render_buf, CTL);
    assert(t->v[0].ph[0] - p0 > PITCH_INC[69 * 16] * CTL);
    assert(t->v[0].ph[0] - p0 < PITCH_INC[69 * 16 + 1] * CTL);
    send_midi(0xB0, 6, 12);                      /* data entry without RPN 0: ignored */
    assert(midi_ch[0].semis == 2);
    send_midi(0xB0, 101, 0); send_midi(0xB0, 100, 0);
    send_midi(0xB0, 6, 12); send_midi(0xB0, 38, 50);
    send_midi(0xE0, 127, 127);
    assert(t->bend_target == 3200);              /* +12.5 semitones, Q8 */
    send_midi(0xB0, 101, 127); send_midi(0xB0, 100, 127);
    send_midi(0xB0, 6, 3);
    assert(midi_ch[0].semis == 12);
    send_midi(0x80, 69, 0);
    assert(!gated(0, 69));
    /* every engine renders held notes under a fractional bend, bounded */
    for (i = 0; i < NENGINES; i++) {
        uint32_t k;
        reset_test();
        host_preset(t, i, 0);
        t->p[P_AMODE] = 0;
        send_midi(0x90, 60, 90);
        send_midi(0xE0, 33, 100);
        for (k = 0; k < 100; k++) {
            uint32_t j;
            track_render(t, render_buf, CTL);
            for (j = 0; j < CTL; j++)
                assert(render_buf[j] > -(1 << 28) && render_buf[j] < (1 << 28));
        }
        send_midi(0x80, 60, 0);
    }
    puts("expression: bend end points, smoothing, RPN 0, other parts untouched, all engines ok");
}

static void wheel_test(void)
{
    track_t *t = &trk[0];
    int16_t saved[P_COUNT];
    int32_t lo = 9999, hi = -9999;
    uint32_t i;
    reset_test();
    memcpy(saved, t->p, sizeof saved);
    send_midi(0xB0, 1, 127);
    for (i = 0; i < 1400; i++) {
        int32_t p = midi_pitch_tick(t, CTL);
        if (p < lo) lo = p;
        if (p > hi) hi = p;
    }
    assert(lo < -120 && hi > 120 && lo >= -128 && hi <= 128);
    assert(!memcmp(saved, t->p, sizeof saved));
    assert(trk[1].wheel_target == 0);
    send_midi(0xB0, 1, 0);
    settle(t);
    assert(t->wheel_q8 == 0 && midi_pitch_tick(t, CTL) == 0);
    puts("expression: +-50-cent wheel vibrato, the preset unchanged ok");
}

/* an FM6 part: the controllers reach FM6's own inputs (fm6_midi_expr), CC 5 its portamento time; the generic
 * bend stays out of its pitch (with FM6's bend range 0, a full bend changes nothing) */
static void fm6_render_run(int bend, int32_t *sum)
{
    track_t *t = &trk[0];
    uint32_t k, j;
    reset_test();
    memset(fm6_v, 0, sizeof fm6_v);              /* FM6's own running state: each run from the same start */
    memset(fm6_lfo, 0, sizeof fm6_lfo);
    memset(fm6_pt, 0, sizeof fm6_pt);
    memset(fm6_ms, 0, sizeof fm6_ms);
    memset(fm6_dc, 0, sizeof fm6_dc);
    memset(fm6_in, 0, sizeof fm6_in);
    host_preset(t, ENG_IX_FM6, 0);
    t->eng_req = t->engine = (uint8_t)ENG_IX_FM6;
    t->p[P_AMODE] = 0;
    fm6_sync(t, 0);
    fm6_ed[0][FN_PBUP] = fm6_ed[0][FN_PBDN] = 0;
    send_midi(0x90, 60, 90);
    send_midi(0xE0, bend ? 127 : 0, bend ? 127 : 64);
    *sum = 0;
    for (k = 0; k < 40; k++) {
        track_render(t, render_buf, CTL);
        for (j = 0; j < CTL; j++)
            *sum = *sum * 31 + render_buf[j];
    }
}

static void fm6_test(void)
{
    int32_t a, b;
    fm6_render_run(0, &a);
    fm6_render_run(1, &b);
    assert(fm6_in[0].bend == 8191 && trk[0].bend_target != 0);
    assert(a == b);                               /* FM6 range 0: no bend, not the generic one either */
    send_midi(0xB0, 1, 100); send_midi(0xB0, 2, 90); send_midi(0xB0, 4, 80);
    send_midi(0xD0, 70, 0); send_midi(0xB0, 65, 127);
    assert(fm6_in[0].wheel == 100 && fm6_in[0].breath == 90 && fm6_in[0].foot == 80 &&
           fm6_in[0].press == 70 && fm6_in[0].porta == 1);
    send_midi(0xB0, 5, 55);
    assert(fm6_ed[0][FN_PTIME] == 55);
    fm6_ed[1][FN_PTIME] = 3;
    send_midi(0xB1, 5, 66);                       /* channel 2 plays part 2 (ANALOG): no FM6 there */
    assert(fm6_ed[1][FN_PTIME] == 3 && fm6_ed[0][FN_PTIME] == 55);
    send_midi(0xB0, 121, 0);                      /* reset controllers: FM6's go back too */
    assert(fm6_in[0].bend == 0 && fm6_in[0].wheel == 0 && fm6_in[0].press == 0 && fm6_in[0].porta == 0);
    send_midi(0x80, 60, 0);
    puts("expression: FM6 takes bend, wheel, breath, foot, aftertouch, porta pedal and CC 5 its own way ok");
}

static void panic_test(void)
{
    uint32_t i;
    reset_test();
    send_midi(0x90, 60, 90); send_midi(0x91, 64, 90);
    settle(&trk[0]);
    send_midi(0xB0, 123, 0);
    assert(!gated(0, 60) && trk[0].v[0].active && gated(1, 64));
    send_midi(0x90, 60, 90); send_midi(0xB0, 64, 127);
    send_midi(0xB0, 123, 0);
    assert(gated(0, 60));                      /* All Notes Off: the pedal holds */
    send_midi(0xB0, 120, 0);
    track_render(&trk[0], render_buf, CTL);
    track_render(&trk[0], render_buf, CTL);
    track_render(&trk[0], render_buf, CTL);
    for (i = 0; i < NVOICE; i++) assert(!trk[0].v[i].active);
    assert(midi_find(0, 60) < 0 && gated(1, 64));
    send_midi(0x99, 36, 100);
    for (i = 0; i < NDRUM; i++) if (drums.v[i].active) break;
    assert(i < NDRUM);
    send_midi(0xB9, 120, 0);
    for (i = 0; i < NDRUM; i++) assert(!drums.v[i].active);
    trk[0].p[P_AMODE] = trk[0].p[P_AHOLD] = 1;
    send_midi(0x90, 65, 90); send_midi(0xB0, 120, 0);
    assert(!trk[0].nheld && !trk[0].arp_note && !trk[0].arp_phys);
    /* Reset All Controllers: pedal-held notes end, keys still down keep sounding */
    trk[0].p[P_AMODE] = trk[0].p[P_AHOLD] = 0;
    send_midi(0xB0, 64, 127);
    send_midi(0x90, 60, 90); send_midi(0x80, 60, 0);
    send_midi(0x90, 62, 90);
    send_midi(0xE0, 127, 127); send_midi(0xB0, 1, 127);
    send_midi(0xB0, 121, 0);
    assert(!gated(0, 60) && gated(0, 62));
    assert(!trk[0].bend_target && !trk[0].wheel_target && !midi_ch[0].pedal);
    puts("expression: CC123 releases (pedal holds), CC120 over the pedal / drums / arp, CC121 ok");
}

static void sustain_test(void)
{
    uint32_t mode;
    for (mode = V_POLY; mode <= V_UNISON; mode++) {
        reset_test();
        trk[0].p[P_VOICE] = (int16_t)mode;
        send_midi(0x90, 60, 90); send_midi(0xB0, 64, 64);
        send_midi(0x90, 64, 90); send_midi(0x80, 64, 0);
        assert(gated(0, 64));
        send_midi(0xB0, 64, 63);
        assert(gated(0, 60) && !gated(0, 64));
        send_midi(0x80, 60, 0);
        assert(!gated(0, 60));
    }
    reset_test();
    send_midi(0xB4, 64, 127); send_midi(0x94, 60, 90);   /* channel 5 plays the selected track */
    song.sel = 1;
    send_midi(0x94, 60, 0);                   /* velocity 0: its own track */
    assert(gated(0, 60));
    send_midi(0x94, 60, 90);                  /* a retrigger: the newly selected track */
    assert(!gated(0, 60) && gated(1, 60));
    send_midi(0xB4, 64, 0);                   /* its key is down: it stays */
    assert(gated(1, 60));
    send_midi(0x84, 60, 0);
    assert(!gated(1, 60));
    /* the same note on two channels: releasing one keeps the other */
    song.sel = 0;
    send_midi(0x90, 60, 90); send_midi(0x94, 60, 90);
    send_midi(0x80, 60, 0);
    assert(midi_find(4, 60) >= 0 && midi_find(0, 60) < 0);
    send_midi(0x84, 60, 0); assert(!gated(0, 60));
    trk[0].p[P_AMODE] = 1;
    send_midi(0xB0, 64, 127); send_midi(0x90, 60, 90);
    send_midi(0x80, 60, 0); assert(trk[0].nheld == 1);
    send_midi(0xB0, 64, 0); assert(!trk[0].nheld && !trk[0].arp_phys);
    send_midi(0xB0, 64, 127); send_midi(0x90, 60, 90); send_midi(0x80, 60, 0);
    panic_req = 1; events_block(CTL);         /* a preset change: its notes are forgotten */
    assert(midi_find(0, 60) < 0 && !trk[0].nheld);
    send_midi(0xB0, 64, 0); assert(!trk[0].nheld);
    puts("expression: pedal threshold, all voice modes, retriggers, track switches, arp, preset panic ok");
}

static void ownership_test(void)
{
    uint32_t i;
    reset_test();
    send_midi(0x94, 60, 90); send_midi(0x84, 60, 0);
    song.sel = 1; send_midi(0xE4, 127, 127);  /* a released track does not stay linked */
    assert(trk[0].bend_target == 0 && trk[1].bend_target == 512);
    reset_test();
    send_midi(0x94, 60, 90);                  /* a held one does */
    song.sel = 1; send_midi(0xE4, 127, 127);
    assert(trk[0].bend_target == 512 && trk[1].bend_target == 512);
    send_midi(0x84, 60, 0);
    reset_test();
    trk[0].p[P_AMODE] = 1;
    send_midi(0xB0, 64, 127);
    for (i = 0; i < 300; i++) {
        send_midi(0x90, 60, 90); send_midi(0x80, 60, 0);
        assert(held_on(0) == 1 && trk[0].arp_phys == 1);
    }
    send_midi(0xB0, 64, 0);
    assert(!held_on(0) && !trk[0].arp_phys && midi_targets(0) == 1u);
    /* the whole note range under the pedal: the table keeps the newest MIDI_HELD, the rest are released */
    reset_test();
    send_midi(0xB0, 64, 127);
    for (i = 0; i < 128; i++) {
        send_midi(0x90, i, 90); send_midi(0x80, i, 0);
        assert(midi_nheld <= MIDI_HELD);
    }
    assert(held_on(0) == MIDI_HELD && midi_find(0, 127) >= 0 && midi_find(0, 127 - MIDI_HELD) < 0);
    send_midi(0xB0, 64, 0);
    assert(!midi_nheld);
    for (i = 0; i < NVOICE; i++) assert(!trk[0].v[i].gate);
    puts("expression: routing, repeated pedal retriggers, a full table ok");
}

int main(void)
{
    for (via_trs = 0; via_trs < 2; via_trs++) {
        printf("== %s\n", via_trs ? "TRS parser" : "USB-format input queue");
        pitch_test(); wheel_test(); fm6_test(); panic_test(); sustain_test(); ownership_test();
    }
    puts("MIDI EXPRESSION TESTS PASSED");
    return 0;
}
