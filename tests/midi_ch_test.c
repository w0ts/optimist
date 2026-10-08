/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4 phase 3, per-track MIDI channels (firmware/src/seq_midi.c; always built,
 * tests/run_tests.sh): defaults as before (parts 1 2 3, drums 10); a note plays the track that has its channel, a
 * channel nobody has plays the selected track, an OFF track is silent in and out; the keys send on the track's
 * channel; the channels round-trip through a project (older projects: the defaults); NEW resets them.
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i)
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/storage/project.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("midi_ch: %-86s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static dlrec_t tdl;
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
    drums.set = -2;
    midi_nheld = 0;
    host_tracks_init();
    bps_defaults();
    fm1_in.notes = fm1_in.buttons = kb_prev = 0;
    mi_w = mi_r = 0;
    mo_w = mo_r = 0;
    usb.config = 1;
    panic_req = transport_req = 0;
    song.playing = 0;
    song.sel = 0;
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

static void t_defaults(void)
{
    reset();
    check(trk_midi_ch(0) == 0u && trk_midi_ch(1) == 1u && trk_midi_ch(2) == 2u && trk_midi_ch(TRK_DRUM) == 9u,
          "defaults: parts on channels 1 2 3, the drums on 10");
    song.g[G_DRCH] = 0;
    check(trk_midi_ch(TRK_DRUM) == MCH_OFF, "the drum channel 0 is OFF (nothing out)");
}

static void t_in(void)
{
    uint32_t d0;
    reset();
    bp_set[BPS_CH0] = 5;                                  /* track 1 on channel 5 */
    song.sel = 2;
    send(0x94, 60, 100);
    check(gated(&trk[0], 60) && !gated(&trk[2], 60), "a note on a track's channel plays that track");
    send(0x84, 60, 0);
    check(!gated(&trk[0], 60), "its note off ends it");
    send(0x90, 62, 100);
    check(gated(&trk[2], 62) && !gated(&trk[0], 62), "channel 1, no track's now: the selected track plays it (as before)");
    send(0x80, 62, 0);
    bp_set[BPS_CH2] = 0;                                  /* the selected track is OFF */
    send(0x90, 64, 100);
    check(ngated(&trk[0]) + ngated(&trk[1]) + ngated(&trk[2]) == 0u, "a channel nobody has, the selected track OFF: silent");
    send(0x92, 64, 100);
    check(ngated(&trk[2]) == 0u, "an OFF track's old channel plays nothing");
    bp_set[BPS_CH1] = 5;                                  /* two tracks on channel 5: the first one plays */
    send(0x94, 67, 100);
    check(gated(&trk[0], 67) && !gated(&trk[1], 67), "two tracks on one channel: the first plays");
    send(0x84, 67, 0);
    d0 = drums.age;
    song.g[G_DRCH] = 12;
    send(0x9B, 36, 100);                                  /* channel 12 */
    check(drums.age != d0, "the drum channel is G_DRCH (12)");
    d0 = drums.age;
    song.g[G_DRCH] = 0;
    send(0x99, 36, 100);
    check(drums.age == d0, "drums OFF: channel 10 plays nothing");
}

static void t_keys(void)
{
    reset();
    song.sel = 1;
    bp_set[BPS_CH1] = 7;
    fm1_in.notes = 1u << 7;                               /* a key */
    blocks(2);
    check(ons[6] == 1u && total_ons() == 1u, "a key of track 2 sends on its channel (7)");
    fm1_in.notes = 0;
    blocks(2);
    check(all_ended(), "the key up ends it on the same channel");
    bp_set[BPS_CH1] = 0;
    clear_out();
    fm1_in.notes = 1u << 7;
    blocks(2);
    fm1_in.notes = 0;
    blocks(2);
    check(total_ons() == 0u && out_n == 0u, "a track OFF: its keys send nothing");
    song.sel = TRK_DRUM;
    song.g[G_DRCH] = 4;
    fm1_in.notes = 1u << 7;
    blocks(2);
    fm1_in.notes = 0;
    blocks(2);
    check(ons[3] >= 1u && all_ended(), "the drum keys send on the drum channel (4)");
}

static void t_project(void)
{
    static project_t q;
    reset();
    bp_set[BPS_CH0] = 5;
    bp_set[BPS_CH1] = 0;
    bp_set[BPS_CH2] = 3;                                  /* (its default: no bits) */
    song.g[G_DRCH] = 12;
    proj_capture(&q, &tdl);
    check(q.g[G_MIDI] == (5 | 17 << 5), "capture: five bits a part (5, OFF = 17, default = 0) in the G_MIDI slot");
    reset();
    proj_apply(&q, &tdl, 1);
    check(bp_set[BPS_CH0] == 5 && bp_set[BPS_CH1] == 0 && bp_set[BPS_CH2] == 3 && song.g[G_DRCH] == 12,
          "apply: the channels are back, the drum channel with them");
    q.g[G_MIDI] = 0;
    bp_set[BPS_CH0] = 9;
    proj_apply(&q, &tdl, 1);
    check(bp_set[BPS_CH0] == 1 && bp_set[BPS_CH1] == 2 && bp_set[BPS_CH2] == 3, "an older project (0 there): the defaults");
    q.g[G_MIDI] = 1;                                      /* SLOOP 2.4's MIDI OUT flag on: part 1 on channel 1 */
    proj_apply(&q, &tdl, 1);
    check(bp_set[BPS_CH0] == 1 && bp_set[BPS_CH1] == 2 && bp_set[BPS_CH2] == 3, "a 2.4 project (1 there): still the defaults");
    q.g[G_MIDI] = (int16_t)(31 | 30 << 5 | 18 << 10);     /* out of range: the defaults */
    proj_apply(&q, &tdl, 1);
    check(bp_set[BPS_CH0] == 1 && bp_set[BPS_CH1] == 2 && bp_set[BPS_CH2] == 3, "out-of-range codes: the defaults");
    bp_set[BPS_CH0] = 8;
    q.g[G_MIDI] = 0;
    proj_apply(&q, &tdl, 0);
    check(bp_set[BPS_CH0] == 8, "a song section (not all) leaves the channels alone");
    bps_defaults();
    check(bp_set[BPS_CH0] == 1 && song.g[G_DRCH] == 10, "NEW: the channels default");
}

int main(void)
{
    t_defaults();
    t_in();
    t_keys();
    t_project();
    printf("midi_ch: %d failed\n", fails);
    return fails;
}
