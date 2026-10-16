/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequencer mutes (firmware/src/seq/seq_mute.c; the user, 2026-10-13: "for midi, not audio"):
 *   keys     smute_key: the 16 white keys the drum lanes play are the 16 lanes, the black keys F# G# A# C# are T1 T2 T3 DR,
 *            the other black keys nothing
 *   seq      a muted track plays no note, for its sound or MIDI out, and the notes sounding end; a muted drum lane does
 *            not hit (sound or MIDI) while the others do; the keys played live still sound; unmuted: as before
 *   record   nothing muted: no TLV; muted: a TLV round trip through the FX record and PROJECT SAVE / LOAD; a project
 *            without it: nothing muted
 * Run by tests/run_tests.sh. Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#if FELUCCA_BLE
static uint8_t ble_on;                          /* (firmware/src/io/midi/midi_ble.c: HOME > BLUETOOTH, the radio's address) */
static uint8_t ble_addr_kept[8];
static void ble_midi_out(uint32_t pkt) { (void)pkt; }
#endif
static uint32_t trk_def_engine(uint32_t i)
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/storage/project.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("smute: %-86s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static dlrec_t tdl;
#include "../firmware/src/storage/settings_word.c"
static uint32_t out_pk[8192], out_n;
static int8_t bal[16][128];                     /* note ons - offs per (channel, note) since the last clear */
static uint32_t ons[16];                        /* note ons per channel */

static void collect(void)                       /* what went to MIDI OUT since the last call */
{
    while (mo_r != mo_w) {
        uint32_t p = midi_out_q[mo_r % MQ], st = (p >> 8) & 0xF0u, ch = (p >> 8) & 15u, n = (p >> 16) & 0x7Fu;
        if (out_n < 8192u)
            out_pk[out_n++] = p;
        if (st == 0x90u && ((p >> 24) & 0x7Fu)) {
            bal[ch][n]++;
            ons[ch]++;
        } else if (st == 0x80u || st == 0x90u) {
            bal[ch][n]--;
        }
        mo_r++;
    }
}
static void clear_out(void)
{
    collect();
    memset(bal, 0, sizeof bal);
    memset(ons, 0, sizeof ons);
    out_n = 0;
}
static uint32_t total_ons(void)
{
    uint32_t c, n = 0;
    for (c = 0; c < 16u; c++)
        n += ons[c];
    return n;
}
static int all_ended(void)                      /* every note on was ended, none twice */
{
    uint32_t c, n;
    for (c = 0; c < 16u; c++)
        for (n = 0; n < 128u; n++)
            if (bal[c][n] != 0)
                return 0;
    return 1;
}
static uint32_t blk;
static void blocks(uint32_t n)
{
    int32_t out[CTL * 2];
    while (n--) {
        mix_block(out, CTL);
        collect();
        blk++;
    }
}
static void send(uint32_t status, uint32_t note, uint32_t vel)
{
    midi_in_q[mi_w++ % MQ] = (status >> 4) | status << 8 | note << 16 | vel << 24;
    blocks(1);
}
static int gated(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static uint32_t ngated(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate;
    return n;
}
static void reset(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_held, 0, sizeof midi_held);
    memset(kb_kind, 0, sizeof kb_kind);
    memset(&um, 0, sizeof um);
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
    midi_nheld = 0;
    host_tracks_init();
    bps_defaults();
    bp_set[BPS_MOUT] = 0;
    bp_set[BPS_MIN] = 0;
    fm1_in.notes = fm1_in.buttons = kb_prev = 0;
    mi_w = mi_r = 0;
    mo_w = mo_r = 0;
    usb.config = 1;
    panic_req = transport_req = 0;
    song.playing = 0;
    song.sel = 0;
    mo_any = 0;
    memset(mo_set, 0, sizeof mo_set);
    for (i = 0; i < NPART; i++) {
        host_preset(&trk[i], 0, 1);
        trk[i].p[P_VOICE] = V_POLY;
        trk[i].p[P_AMODE] = trk[i].p[P_AHOLD] = 0;
        trk[i].p[P_SUS] = 127;
        trk[i].p[P_TRANS] = 0;
    }
    song.g[G_BPM] = 120;
    ly_bit[LY_ROLL] = 1u << 8;
    clear_out();
}
static void step_note(track_t *t, uint32_t k, uint32_t note)
{
    t->step[k].n = 1;
    t->step[k].note[0] = (uint8_t)note;
    t->step[k].time = ST_NOTE;
    t->step[k].vel = 100;
}
static void play_bars(uint32_t bars)
{
    transport_req = 1;
    blocks((uint32_t)(FS * 60.0 / song.g[G_BPM] * 4 * bars / CTL));
}
static void stop(void)
{
    transport_req = 2;
    blocks(4);
}


static void t_keys(void)
{
    uint32_t k, l, nb = 0, ok = 1;
    static const uint8_t BLK[4] = {1, 3, 5, 8};
    for (l = 0; l < DRUM_LANES; l++)
        ok &= smute_key(key_of_lane(l)) == (int32_t)l;
    check(ok, "the white keys the lanes play (key_of_lane) mute the lanes in order");
    ok = 1;
    for (k = 0; k < 4u; k++)
        ok &= smute_key(BLK[k]) == 16 + (int32_t)k;
    check(ok, "the black keys F# G# A# C# mute T1 T2 T3 DR");
    for (k = 0; k < 27u; k++)
        nb += smute_key(k) < 0;
    check(nb == 27u - DRUM_LANES - 4u, "the other keys mute nothing (27 keys: the lanes + 4 tracks)");
    check(smute_key(27) < 0 && smute_key(200) < 0, "a key past the keyboard: nothing");
    ok = 1;
    for (k = 0; k < 27u; k++)
        if (smute_key(k) >= 0 && smute_key(k) < (int32_t)DRUM_LANES)
            ok &= lane_of_key(k) == (uint32_t)smute_key(k);
    check(ok, "a lane key is the lane drums.c lane_of_key says (the key plays it)");
    smute_trk = 0, smute_lane = 0;
    check(smute_toggle(2) == 1 && smute_lane_on(2) && smute_keys_lit() == 1u << key_of_lane(2), "toggle a lane: on, its key lit");
    check(smute_toggle(2) == 0 && !smute_lane_on(2) && smute_keys_lit() == 0u, "... again: off");
    check(smute_toggle(17) == 1 && smute_trk_on(1) && smute_keys_lit() == 1u << 3, "toggle T2 (black key 2, G#): on, lit");
    check(smute_toggle(-1) == 0 && smute_toggle(20) == 0 && smute_trk == 2u && smute_lane == 0u, "a key that mutes nothing: nothing changes");
    smute_trk = 0;
}

static void t_track(void)
{
    uint32_t i, on0, on1, hear = 0;
    reset();
    bp_set[BPS_MOUT] = 1;
    bp_set[BPS_CH1] = 8;
    for (i = 0; i < 16u; i += 4u) {
        step_note(&trk[0], i, 60 + i);
        step_note(&trk[1], i + 2u, 48);
    }
    smute_trk = smute_lane = 0;
    play_bars(2);
    on0 = ons[0];
    on1 = ons[7];
    check(on0 >= 8u && on1 >= 8u, "unmuted: both tracks' steps go out on their channels");
    stop();
    clear_out();
    smute_set_trk(0, 1);
    for (i = 0; i < 400u && hear == 0u; i++) {
        if (i == 0)
            transport_req = 1;
        blocks(1);
        hear += ngated(&trk[0]);
    }
    play_bars(2);
    check(ons[0] == 0u && hear == 0u, "T1's sequencer muted: no MIDI note and no voice of the sound");
    check(ons[7] >= on1, "... T2 plays on");
    stop();
    check(all_ended(), "STOP: every note ended");
    smute_trk = 0;
    reset();
    bp_set[BPS_MOUT] = 1;
    for (i = 0; i < 16u; i++)
        step_note(&trk[0], i, 60);
    play_bars(1);
    check(ons[0] > 4u, "(a note on every step: it sounds)");
    smute_set_trk(0, 1);
    blocks(1);
    check(ngated(&trk[0]) <= 1u, "muted while it plays: nothing new starts");
    {
        uint32_t o = ons[0];
        blocks((uint32_t)(FS * 60.0 / song.g[G_BPM] * 4 / CTL));
        check(ons[0] == o, "... a bar later: still no new note");
    }
    stop();
    check(all_ended(), "the notes that sounded ended with their gates / STOP: none hangs");
    smute_trk = 0;
    reset();
    smute_set_trk(0, 1);
    fm1_in.notes = 1u << 12;
    blocks(2);
    check(ngated(&trk[0]) > 0u, "a key played live on the muted track still sounds");
    fm1_in.notes = 0;
    blocks(2);
    smute_trk = 0;
}

static void t_lane(void)
{
    uint32_t i, kick = 0, hat = 0, n;
    reset();
    bp_set[BPS_MOUT] = 1;
    for (i = 0; i < 16u; i += 4u) {
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    }
    smute_trk = smute_lane = 0;
    drums.hits = 0;
    play_bars(1);
    n = (uint32_t)0;
    check((drums.hits & 1u) && (drums.hits & 16u), "unmuted: lanes 0 and 4 hit (the sound)");
    stop();
    clear_out();
    smute_set_lane(0, 1);
    drums.hits = 0;
    play_bars(2);
    for (i = 0; i < out_n; i++)
        if (((out_pk[i] >> 24) & 0x7Fu) && (out_pk[i] & 0xFu) == 9u) {
            uint32_t note = (out_pk[i] >> 16) & 0x7Fu;
            kick += note == LANE_NOTE_OF(0);
            hat += note == LANE_NOTE_OF(4);
        }
    check(kick == 0u && hat >= 8u, "lane 0 muted: no kick note out, the lane 4's go on");
    check(!(drums.hits & 1u) && (drums.hits & 16u), "... and the sound: lane 0 never hits, lane 4 does");
    stop();
    check(all_ended(), "STOP: every drum note ended");
    smute_lane = 0;
    smute_set_trk(3, 1);
    clear_out();
    drums.hits = 0;
    play_bars(1);
    check(total_ons() == 0u && drums.hits == 0u, "the drum track muted: no lane hits, no MIDI");
    stop();
    smute_trk = 0;
    (void)n;
}

static void t_record(void)
{
    static project_t q;
    uint8_t b[8];
    reset();
    smute_trk = smute_lane = 0;
    check(smute_tlv(b) == 0u, "nothing muted: no TLV (a project as before writes nothing new)");
    smute_trk = 5u, smute_lane = 0x201u;
    check(smute_tlv(b) == 3u && b[0] == 5u && b[1] == 1u && b[2] == 0x02u, "muted: the 3-byte TLV (tracks, lanes)");
    smute_trk = smute_lane = 0;
    smute_untlv(b, 3);
    check(smute_trk == 5u && smute_lane == 0x201u, "... read back");
    smute_untlv(b, 2);
    check(smute_trk == 0u && smute_lane == 0u, "a short one: nothing muted");
    smute_trk = 3u, smute_lane = 0x0102u;
    proj_capture(&q, &tdl);
    smute_trk = smute_lane = 0;
    proj_apply(&q, &tdl, 1);
    check(smute_trk == 3u && smute_lane == 0x0102u, "PROJECT capture / apply keeps them (the FX record)");
    smute_trk = smute_lane = 0;
    proj_capture(&q, &tdl);
    smute_trk = 1u, smute_lane = 1u;
    proj_apply(&q, &tdl, 1);
    check(smute_trk == 0u && smute_lane == 0u, "a project with none: nothing muted after the load");
}

int main(void)
{
    t_keys();
    t_track();
    t_lane();
    t_record();
    printf("smute: %d failed\n", fails);
    return fails;
}
