/* SPDX-License-Identifier: GPL-3.0-only */
/* The arp follows TRN, ROOT, SCALE and QNT on held and latched notes (SLOOP 2.6 d30d655, after Felucca 1.5 #191;
 * seq.c arp_add / arp_next / kb_pitch). Before, the arp kept the notes mapped when the keys went down:
 *   latched     C4 + E4 latched (WHITE, MAJ): TRN +2, ROOT D, SCALE MIN each move what it plays now
 *   QNT         a key under SNAP, then WHITE: a black key falls silent; every key silent: the arp plays nothing
 *   octave      OCT+ when the key went down stays with that key; OCT moved after does not move it
 *   chord       chord mode, one key latched: its three notes all follow TRN
 *   release     not latched: a key let go after TRN changed leaves the arp (held[] keeps the notes as they went down)
 *   MIDI in     a note not from a key (MIDI in) plays as it came, whatever TRN does
 * Exit status: the failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("arp follow: %-90s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void block(void)
{
    int32_t out[CTL * 2];
    mix_block(out, CTL);
}
static void keys(uint32_t mask)                          /* the keys down now; a block runs */
{
    fm1_in.notes = mask;
    block();
}
static uint32_t K(uint32_t note) { return 1u << (note - 53u); }   /* the key of note 53 + k */
static void reset(void)
{
    track_t *t = &trk[0];
    seq_stop();
    transport_req = 0;
    host_tracks_init();
    host_preset(t, 0, 0);
    t->p[P_VOICE] = V_POLY;
    t->p[P_TRANS] = 0;
    t->p[P_ROOT] = 0;
    t->p[P_SCALE] = 1;                                   /* MAJ */
    t->p[P_QUANT] = Q_WHITE;
    t->p[P_CHORD] = 0;
    t->p[P_AMODE] = 1;                                   /* UP */
    t->p[P_AOCT] = 1;
    t->p[P_AORDER] = 0;
    t->p[P_AHOLD] = 1;
    t->p[P_APROB] = 127;
    t->nheld = 0;
    t->arp_phys = 0;
    song.sel = 0;
    song.octave = 0;
    song.rec = 0;
    rec_wait = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    kb_prev = 0;
    block();
}
/* what the arp plays now, once round, low to high, as "60 64" ("-" for nothing) */
static const char *arp_plays(void)
{
    static char b[96];
    track_t *t = &trk[0];
    uint32_t idx = t->arp_idx, i, j, n = 0, c = 0, got[16];
    t->arp_idx = 0xFFFFFFFFu;
    for (i = 0; i < t->nheld; i++) {
        uint32_t x = arp_next(t);
        if (x == KB_SILENT)
            break;
        for (j = 0; j < n && got[j] != x; j++)
            ;
        if (j == n)
            got[n++] = x;
    }
    t->arp_idx = idx;
    b[0] = 0;
    if (!n)
        return "-";
    for (i = 0; i < n; i++)
        c += (uint32_t)snprintf(b + c, sizeof b - c, i ? " %u" : "%u", got[i]);
    return b;
}
static void latch(uint32_t mask)
{
    keys(mask);
    keys(0);
}

int main(void)
{
    track_t *t = &trk[0];

    reset();
    latch(K(60) | K(64));
    check(t->nheld == 2u && !strcmp(arp_plays(), "60 64"), "latched C4 + E4 (WHITE, MAJ): the arp plays 60 64");
    t->p[P_TRANS] = 2;
    check(!strcmp(arp_plays(), "62 66"), "TRN +2 after the latch: 62 66 (it played 60 64 before #191)");
    t->p[P_TRANS] = 0;
    t->p[P_ROOT] = 2;
    check(!strcmp(arp_plays(), "62 66"), "ROOT D after the latch: the keys' degrees in D, 62 66");
    t->p[P_ROOT] = 0;
    t->p[P_SCALE] = 2;                                   /* MIN */
    check(!strcmp(arp_plays(), "60 63"), "SCALE MIN after the latch: the third is minor, 60 63");
    t->p[P_SCALE] = 1;
    check(!strcmp(arp_plays(), "60 64"), "back to MAJ: 60 64 again");

    reset();
    t->p[P_QUANT] = Q_SNAP;
    latch(K(60) | K(61));                                 /* C4 and the black key C#4: SNAP rounds it down to C */
    check(!strcmp(arp_plays(), "60"), "SNAP: C4 and C#4 latched both play C (each note once)");
    t->p[P_QUANT] = Q_WHITE;
    check(!strcmp(arp_plays(), "60"), "QNT WHITE after the latch: the black key falls silent, C4 plays");
    reset();
    t->p[P_QUANT] = Q_SNAP;
    latch(K(61));
    t->p[P_QUANT] = Q_WHITE;
    check(!strcmp(arp_plays(), "-"), "a black key alone, then WHITE: the arp plays nothing");
    {
        int32_t out[CTL * 2];
        uint32_t i;
        for (i = 0; i < 400u; i++)
            mix_block(out, CTL);
        check(!t->arp_note, "and the running arp starts no note (KB_SILENT is never played)");
    }

    reset();
    song.octave = 1;
    latch(K(60));
    song.octave = 0;
    check(!strcmp(arp_plays(), "72"), "OCT+ when the key went down: the key keeps its octave (72), OCT back does not move it");
    t->p[P_TRANS] = -1;
    check(!strcmp(arp_plays(), "71"), "and TRN still applies in that octave (71)");

    reset();
    t->p[P_CHORD] = 1;                                   /* TRIAD */
    latch(K(60));
    check(t->nheld == 3u && !strcmp(arp_plays(), "60 64 67"), "chord mode, C4 latched: C E G");
    t->p[P_TRANS] = 5;
    check(!strcmp(arp_plays(), "65 69 72"), "TRN +5 after the latch: every note of the chord follows (65 69 72)");

    reset();
    t->p[P_AHOLD] = 0;
    keys(K(60) | K(64));
    t->p[P_TRANS] = 3;
    check(!strcmp(arp_plays(), "63 67"), "not latched, keys held: TRN +3 applies at once (63 67)");
    keys(K(64));
    check(t->nheld == 1u && !strcmp(arp_plays(), "67"), "C4 let go after TRN moved: it leaves the arp, E4 stays");
    keys(0);
    check(t->nheld == 0u, "all let go: nothing held");

    reset();
    in_key = 0;
    input_on(t, 50, 100);                                /* a note from MIDI in: no key */
    input_off(t, 50);
    t->p[P_TRANS] = 4;
    check(t->nheld == 1u && !strcmp(arp_plays(), "50"), "a MIDI note latched: it plays as it came, TRN does not move it");

    printf("arp follow: %s (%d failed)\n", fails ? "FAILED" : "PASS", fails);
    return fails;
}
