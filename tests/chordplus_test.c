/* SPDX-License-Identifier: GPL-3.0-only */
/* CHORD+ (SLOOP 2.4: firmware/src/seq.c chord_play_notes / chord_revoice, voice.c strum_*; FELUCCA_CHORDPLUS):
 *   chords     C major TRIAD + each modifier: F# minor, G# 7th, A# sus4, C# 9th, D# inversion, combinations; the
 *              7th and 9th from the scale (C major: B, D; A minor scale on A: G, B)
 *   keys       a black key held before the white one, or pressed while the chord is held: the chord changes under
 *              the finger and back when it is let go; nothing changes without a chord mode (a black key is silent)
 *   record     what is played is recorded as it sounds (the modified chord's notes on the step)
 *   strum      STRUM 20: the chord's notes 20 ms apart, low to high; -20 high to low; a chord step too; a key let
 *              go before its turn: the note never starts
 *   vlead      VLEAD ON: C then F voiced nearest (C E G -> C F A, not F A C)
 * Exit status: the failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("chordplus: %-90s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void block(void)
{
    int32_t out[CTL * 2];
    mix_block(out, CTL);
}
static void reset(void)
{
    uint32_t k;
    seq_stop();
    transport_req = 0;
    host_tracks_init();
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_STRUM] = trk[k].p[P_VLEAD] = 0;
    host_preset(&trk[0], 0, 0);
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_REL] = 0;
    trk[0].p[P_TRANS] = 0;                              /* (the preset may transpose) */
    trk[0].p[P_AMODE] = 0;
    trk[0].p[P_CHORD] = 1;                               /* TRIAD */
    for (k = 0; k < NPART; k++)
        trk[k].p[P_SCALE] = 1;                           /* MAJ (CHR: chords of the minor scale) */
    song.sel = 0;
    song.rec = 0;
    rec_wait = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    kb_prev = 0;
    memset(vl_n, 0, sizeof vl_n);
    memset(stq, 0, sizeof stq);
    block();
}
/* the notes of track 0's gated voices, sorted, as a string "60 64 67" */
static const char *sounding(void)
{
    static char b[64];
    uint8_t n[NVOICE];
    uint32_t i, k = 0, a, c;
    for (i = 0; i < NVOICE; i++)
        if (trk[0].v[i].gate)
            n[k++] = trk[0].v[i].note;
    sort_notes(n, k);
    b[0] = 0;
    for (i = 0, c = 0; i < k; i++)
        c += (uint32_t)snprintf(b + c, sizeof b - c, i ? " %u" : "%u", n[i]);
    (void)a;
    return b;
}
static void keys(uint32_t mask)                          /* the keys down now; a block runs */
{
    fm1_in.notes = mask;
    block();
}
static uint32_t key_of(uint32_t pc_note)                 /* the key (0..26) of note 53 + k */
{
    return pc_note - 53u;
}
static const char *mods_chord(uint32_t mods)
{
    static char b[64];
    uint8_t c[4];
    uint32_t i, k, n = 0;
    k = chord_play_notes(&trk[0], 60, mods, c);
    b[0] = 0;
    for (i = 0; i < k; i++)
        n += (uint32_t)snprintf(b + n, sizeof b - n, i ? " %u" : "%u", c[i]);
    return b;
}

static void t_chords(void)
{
    reset();
    check(!strcmp(mods_chord(0), "60 64 67"), "C TRIAD: C E G");
    check(!strcmp(mods_chord(CM_MINOR), "60 63 67"), "F#: minor (C Eb G)");
    check(!strcmp(mods_chord(CM_SEVEN), "60 64 67 71"), "G#: + the scale's 7th (B)");
    check(!strcmp(mods_chord(CM_SUS4), "60 65 67"), "A#: sus4 (C F G)");
    check(!strcmp(mods_chord(CM_NINE), "60 62 64 67") || !strcmp(mods_chord(CM_NINE), "60 64 67 74"), "C#: + the 9th (D)");
    check(!strcmp(mods_chord(CM_INV), "64 67 72"), "D#: inverted (E G C)");
    check(!strcmp(mods_chord(CM_MINOR | CM_SEVEN), "60 63 67 71"), "F# + G#: minor with the scale's 7th");
    check(!strcmp(mods_chord(CM_SEVEN | CM_NINE), "60 64 71 74"), "G# + C#: 7th and 9th (the 9th for the 5th)");
}

static void t_keys(void)
{
    uint32_t kc, kfs = key_of(66), kgs = key_of(68);
    reset();
    for (kc = 0; kc < 27u && kb_map(&trk[0], kc) != 60u; kc++)
        ;
    check(kc < 27u, "a white key plays C4 in chord mode");
    keys(1u << kc);
    check(!strcmp(sounding(), "60 64 67"), "the key: C E G");
    keys(1u << kc | 1u << kfs);
    check(!strcmp(sounding(), "60 63 67"), "F# pressed while the chord is held: minor under the finger");
    keys(1u << kc | 1u << kfs | 1u << kgs);
    check(!strcmp(sounding(), "60 63 67 71"), "G# too: minor 7th");
    keys(1u << kc);
    check(!strcmp(sounding(), "60 64 67"), "both let go: back to the major triad");
    keys(0);
    keys(1u << kfs);
    keys(1u << kfs | 1u << kc);
    check(!strcmp(sounding(), "60 63 67"), "F# held first, then the key: minor");
    keys(0);
    check(!strcmp(sounding(), ""), "all let go: silent");
    trk[0].p[P_CHORD] = 0;
    keys(1u << kfs);
    check(!strcmp(sounding(), "66") && kb_kind[kfs] == KS_NOTE, "no chord mode: a black key is no modifier (it plays its note)");
    keys(0);
}

static void t_record(void)
{
    uint32_t kc, i, j, has63 = 0, has64 = 0, kfs = key_of(66);
    reset();
    for (kc = 0; kc < 27u && kb_map(&trk[0], kc) != 60u; kc++)
        ;
    song.rec = 1u;
    transport_req = 1;
    block();
    keys(1u << kfs);
    keys(1u << kfs | 1u << kc);
    for (i = 0; i < 40u; i++)
        block();
    keys(0);
    for (i = 0; i < 40u; i++)
        block();
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < trk[0].step[i].n; j++)
            has63 |= trk[0].step[i].note[j] == 63u, has64 |= trk[0].step[i].note[j] == 64u;
    check(has63 && !has64, "recorded as it sounds: the minor third (Eb) on the step, not E");
    song.rec = 0;
}

/* blocks until track 0 has n gated voices (-1: never within 200) */
static int blocks_to(uint32_t n)
{
    int b;
    for (b = 0; b < 200; b++) {
        uint32_t i, k = 0;
        for (i = 0; i < NVOICE; i++)
            k += trk[0].v[i].gate != 0;
        if (k >= n)
            return b;
        block();
    }
    return -1;
}
static void t_strum(void)
{
    uint32_t kc, i;
    int b2, b3;
    uint32_t per = 20u * FS / 1000u / CTL;              /* blocks a 20 ms step */
    reset();
    for (kc = 0; kc < 27u && kb_map(&trk[0], kc) != 60u; kc++)
        ;
    trk[0].p[P_STRUM] = 20;
    fm1_in.notes = 1u << kc;
    block();
    check(!strcmp(sounding(), "60"), "STRUM 20: the lowest at once");
    b2 = blocks_to(2);
    check(b2 >= (int)per - 2 && b2 <= (int)per + 2 && !strcmp(sounding(), "60 64"), "STRUM 20: the 3rd ~20 ms later");
    b3 = blocks_to(3);
    check(b3 >= (int)per - 2 && b3 <= (int)per + 2, "STRUM 20: the 5th ~20 ms after it");
    keys(0);
    reset();
    trk[0].p[P_STRUM] = -20;
    keys(1u << kc);
    check(!strcmp(sounding(), "67"), "STRUM -20: the highest first");
    keys(0);
    for (i = 0; i < 4u * per; i++)
        block();
    check(!strcmp(sounding(), ""), "STRUM: a key let go before its notes' turn: they never start");
    reset();                                            /* a chord step */
    trk[0].p[P_STRUM] = 30;
    {
        static const uint8_t C[3] = {60, 64, 67};
        put_step(&trk[0], 0, 3, C, ST_NOTE, 0);
    }
    transport_req = 1;
    block();
    block();
    check(!strcmp(sounding(), "60"), "STRUM on a chord step: the lowest first");
    b3 = blocks_to(3);
    check(b3 > (int)per, "STRUM on a chord step: the others follow");
    transport_req = 2;
    block();
}

static void t_vlead(void)
{
    uint8_t c[4];
    uint32_t k;
    reset();
    trk[0].p[P_VLEAD] = 1;
    k = chord_play_notes(&trk[0], 60, 0, c);
    k = chord_play_notes(&trk[0], 65, 0, c);
    check(k == 3u && c[0] == 60 && c[1] == 65 && c[2] == 69, "VLEAD: C E G then F: C F A (nearest), not F A C");
    trk[0].p[P_VLEAD] = 0;
    k = chord_play_notes(&trk[0], 65, 0, c);
    check(k == 3u && c[0] == 65, "VLEAD off: F A C");
}

int main(void)
{
    t_chords();
    t_keys();
    t_record();
    t_strum();
    t_vlead();
    printf("chordplus: %d failed\n", fails);
    return fails != 0;
}
