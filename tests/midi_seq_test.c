/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4 phase 3, SEQ -> MIDI OUT and IN = CLOCK (firmware/src/seq_midi.c; build with FELUCCA_MIDI_CH=1
 * FELUCCA_MIDI_OUT=1 FELUCCA_MIDI_INCLK=1, tests/run_tests.sh):
 *   MIDI OUT  SEQ: the sequencer, the drums, the arp and the rolls go out on the track's channel, every note is
 *             ended, STOP ends what is on, KEYS (the default) sends none of it, a channel changed under a note ends
 *             it where it began, notes in from MIDI are never sent back; OUT / IN are no project's
 *   MIDI IN   CLOCK: note-ons are dropped, a held note still ends
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
#include "../firmware/src/project.c"

#if !(FELUCCA_MIDI_CH && FELUCCA_MIDI_OUT && FELUCCA_MIDI_INCLK)
#error "build with -DFELUCCA_MIDI_CH=1 -DFELUCCA_MIDI_OUT=1 -DFELUCCA_MIDI_INCLK=1"
#endif

static int fails;
static void check(int ok, const char *what)
{
    printf("midi_seq: %-86s %s\n", what, ok ? "ok" : "FAIL");
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

static void t_settings(void)
{
    static project_t q;
    reset();
    bp_set[BPS_MOUT] = 1;
    bp_set[BPS_MIN] = 1;
    proj_capture(&q, &tdl);
    reset();
    bp_set[BPS_MOUT] = 1;
    proj_apply(&q, &tdl, 1);
    check(bp_set[BPS_MOUT] == 1 && bp_set[BPS_MIN] == 0, "OUT / IN are the FM-1's: a project neither saves nor loads them");
    bps_defaults();
    check(bp_set[BPS_MOUT] == 1, "NEW leaves OUT as it was");
}

static void t_seq_out(void)
{
    uint32_t i;
    reset();
    for (i = 0; i < 16u; i += 4u)
        step_note(&trk[0], i, 60 + i);
    for (i = 0; i < 16u; i += 4u)
        step_note(&trk[1], i + 2u, 48);
    dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[4], 4, LV_NORM, 0);
    bp_set[BPS_CH1] = 8;
    play_bars(2);
    check(total_ons() == 0u, "OUT = KEYS: the sequencer sends nothing");
    stop();
    bp_set[BPS_MOUT] = 1;
    clear_out();
    play_bars(2);
    check(ons[0] >= 8u, "OUT = SEQ: track 1's steps go out on channel 1");
    check(ons[7] >= 8u, "track 2's on its own channel (8)");
    check(ons[9] >= 4u, "the drum hits on channel 10");
    {
        uint32_t k, bad = 0;
        for (k = 0; k < out_n; k++)
            if ((out_pk[k] & 0xFu) != 9u && (out_pk[k] & 0xFu) != 8u)
                bad++;
        check(bad == 0u, "only note on / off packets (CIN 9 / 8)");
    }
    stop();
    check(all_ended(), "STOP: every note on was ended, none twice");
    {
        uint32_t o = total_ons();
        stop();
        check(total_ons() == o, "(STOP again: nothing more)");
    }
    clear_out();
    play_bars(1);
    bp_set[BPS_MOUT] = 0;
    blocks(2);
    stop();
    check(all_ended(), "OUT back to KEYS while playing: what was sent ends");
    bp_set[BPS_MOUT] = 1;
    clear_out();
    play_bars(1);
    bp_set[BPS_CH0] = 0;
    blocks(1);
    check(ons[0] > 0u && bal[0][60] == 0 && bal[0][64] == 0 && bal[0][68] == 0 && bal[0][72] == 0, "a track set to OFF ends its notes");
    stop();
    check(ons[0] == ons[0] && all_ended(), "(and the rest ends at STOP)");
}

static void t_chan_move(void)
{
    uint32_t k;
    reset();
    bp_set[BPS_MOUT] = 1;
    trk[0].p[P_AMODE] = 1;
    trk[0].p[P_AGATE] = 127;
    send(0x90, 60, 100);
    blocks(800);
    check(ons[0] >= 2u, "the arp's notes go out on the track's channel");
    bp_set[BPS_CH0] = 4;
    blocks(1);
    for (k = 0; k < 128u; k++)
        if (bal[0][k] != 0)
            break;
    check(k == 128u, "channel changed under the arp's note: the off went on the channel it began on");
    blocks(800);
    check(ons[3] >= 2u, "the arp goes on on the new channel (4)");
    send(0x80, 60, 0);
    blocks(20);
    check(all_ended(), "the arp's last note is ended once the key is up");
}

static void t_no_echo(void)
{
    reset();
    bp_set[BPS_MOUT] = 1;
    send(0x90, 60, 100);
    send(0x90, 64, 100);
    send(0x99, 36, 100);
    blocks(4);
    send(0x80, 60, 0);
    send(0x80, 64, 0);
    blocks(4);
    check(total_ons() == 0u && out_n == 0u, "notes in from MIDI are never sent back (a note, a chord, a drum hit)");
}

static void t_roll(void)
{
    reset();
    bp_set[BPS_MOUT] = 1;
    song.sel = 0;
    fm1_in.buttons = ly_bit[LY_ROLL];                     /* ARP held: the key repeats */
    fm1_in.notes = 1u << 7;
    blocks(800);
    check(ons[0] >= 3u && ons[0] == total_ons(), "a roll goes out on its track's channel");
    fm1_in.notes = 0;
    blocks(100);
    fm1_in.buttons = 0;
    blocks(4);
    check(all_ended(), "every roll hit is ended");
    clear_out();
    song.sel = TRK_DRUM;
    fm1_in.buttons = ly_bit[LY_ROLL];
    fm1_in.notes = 1u << 7;
    blocks(800);
    check(ons[9] >= 3u, "a drum roll goes out on the drum channel");
    fm1_in.notes = 0;
    fm1_in.buttons = 0;
    blocks(100);
    check(all_ended(), "the drum roll's hits end with it");
}

static void t_in_clock(void)
{
    reset();
    send(0x90, 60, 100);
    check(gated(&trk[0], 60), "IN = NOTES: a note plays");
    bp_set[BPS_MIN] = 1;
    send(0x90, 62, 100);
    check(!gated(&trk[0], 62), "IN = CLOCK: a note on is dropped");
    send(0x80, 60, 0);
    check(!gated(&trk[0], 60), "its note off still ends a note held before");
    send(0x99, 36, 100);
    send(0x80, 62, 0);
    check(ngated(&trk[0]) == 0u, "no hanging note");
    {
        uint32_t d0 = drums.age;
        send(0x99, 38, 100);
        check(drums.age == d0, "no drum hit either");
    }
}

int main(void)
{
    t_settings();
    t_seq_out();
    t_chan_move();
    t_no_echo();
    t_roll();
    t_in_clock();
    printf("midi_seq: %d failed\n", fails);
    return fails;
}
