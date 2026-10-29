/* SPDX-License-Identifier: GPL-3.0-only */
/* A step's GLIDE (the step's SLIDE, SF_SLIDE; the user, 2026-10-29, issue #104 "Where is the slide"): the STEP card turns it
 * on, the sequencer holds the step's note into the next one, which glides there (seq.c seq_step; MONO / LEGATO: voice.c
 * mono_play, POLY: seq_slide_poly + voice.c trk_note_glide; GLIDE 0: SLIDE_T, ~50 ms).
 *   heard      two steps C4 -> G4 at 120 BPM: with GLIDE on step 1 the voice that played C4 goes on and passes the pitches
 *              between (portamento), its envelope going on; without it G4 starts at its pitch, a new strike. POLY and MONO.
 *              The output of the second step differs between the two
 *   stored     SF_SLIDE is a bit of the stored step (pstep_t, 10 bytes, unchanged since SLOOP): a step from before this
 *              (flags 0) loads with no glide, a glide step keeps it through the stored form (ngate.h steps_store / load)
 * Exit status: the failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("seqglide: %-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
typedef struct {
    uint32_t between;                                   /* blocks of step 2 with the G4 voice between C4 and G4 */
    uint32_t restart;                                   /* a voice (re)started at step 2 (its envelope from the start) */
    uint32_t c4_gone;                                   /* the C4 voice still on as C4 at step 2's end: 0 */
    double energy[2];                                   /* the output's sum of squares at step 1, step 2 */
    int32_t out[64][CTL * 2];                           /* step 2's first blocks */
    uint32_t nout;
} run_t;
static void run(uint32_t mode, uint32_t slide, run_t *r)
{
    track_t *t = &trk[0];
    uint32_t i, guard, ages = 0, age0 = 0, st, seen = 0;
    static const uint8_t C4[1] = {60}, G4[1] = {67};
    memset(r, 0, sizeof *r);
    seq_stop();
    transport_req = 0;
    host_tracks_init();
    host_preset(t, 0, 0);
    t->p[P_VOICE] = (int16_t)mode;
    t->p[P_GLIDE] = 0;
    t->p[P_TRANS] = 0;
    t->p[P_SLEN] = 4;
    t->p[P_SGATE] = 64;                                 /* (half a step: no slide, the notes part) */
    put_step(t, 0, 1, C4, ST_NOTE, slide ? SF_SLIDE : 0u);
    put_step(t, 1, 1, G4, ST_NOTE, 0u);
    song.sel = 0;
    clk_beat = clk_pos = 0;
    transport_req = 1;
    for (guard = 0; guard < 20000u; guard++) {
        int32_t out[CTL * 2];
        uint32_t k;
        double e = 0;
        mix_block(out, CTL);
        for (k = 0; k < CTL * 2u; k++)
            e += (double)out[k] * (double)out[k];
        st = t->seq_idx % 4u;                           /* (the step playing) */
        seen |= st == 1u;
        if (seen && st != 1u)
            break;                                      /* (step 2 over) */
        if (st > 1u)
            continue;
        r->energy[st] += e;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            if (!v->active || !v->gate)
                continue;
            if (st == 0u && v->note == 60u)
                age0 = v->age;
            if (st == 1u && v->note == 67u) {
                r->between += v->pitch_cur > 60 * 16 && v->pitch_cur < 67 * 16;
                ages |= v->age != age0;
            }
            if (st == 1u && v->note == 60u)
                r->c4_gone = 1;
        }
        if (st == 1u && r->nout < 64u)
            memcpy(r->out[r->nout++], out, sizeof out);
    }
    r->restart = ages;
    seq_stop();
}
static double out_diff(const run_t *a, const run_t *b)
{
    uint32_t i, k, n = a->nout < b->nout ? a->nout : b->nout;
    double d = 0;
    for (i = 0; i < n; i++)
        for (k = 0; k < CTL * 2u; k++)
            d += fabs((double)a->out[i][k] - (double)b->out[i][k]);
    return d;
}
static run_t ra, rb;
int main(void)
{
    static const uint32_t MODES[3] = {V_POLY, V_LEGATO, V_MONO};
    static const char *const MN[3] = {"POLY", "LEGATO", "MONO"};
    uint32_t m;
    char w[160];
    for (m = 0; m < 3u; m++) {
        run(MODES[m], 1, &ra);
        run(MODES[m], 0, &rb);
        printf("seqglide: %s glide: %u blocks between, restart %u; none: %u blocks between, restart %u; diff %.0f\n", MN[m],
               ra.between, ra.restart, rb.between, rb.restart, out_diff(&ra, &rb));
        snprintf(w, sizeof w, "%s: GLIDE on step 1: G4 glides from C4 (pitches between)%s", MN[m],
                 MODES[m] == V_MONO ? " (MONO strikes every note: its envelope again)" : ", the same voice going on");
        check(ra.between >= 2u && (MODES[m] == V_MONO || !ra.restart) && !ra.c4_gone, w);
        snprintf(w, sizeof w, "%s: GLIDE off: G4 starts at its pitch, a new strike", MN[m]);
        check(rb.between == 0u && rb.restart, w);
        snprintf(w, sizeof w, "%s: the second step's output differs with and without it", MN[m]);
        check(ra.nout > 4u && out_diff(&ra, &rb) > 1000.0 && ra.energy[1] > 0 && rb.energy[1] > 0, w);
    }
    {   /* stored: SF_SLIDE in the stored step; a step from before (flags 0) has none */
        static pstep_t ps[NSTEP];
        track_t *t = &trk[0];
        host_tracks_init();
        memset(ps, 0, sizeof ps);
        ps[0].time = ST_NOTE, ps[0].n = 1, ps[0].note[0] = 60, ps[0].vel = 100;   /* (an old project's step: no flags) */
        steps_load(t, 0, ps);
        check(t->step[0].time == ST_NOTE && !(t->step[0].flags & SF_SLIDE), "an old project's step (flags 0) loads with no glide");
        t->step[0].flags |= SF_SLIDE;
        steps_store(ps, t, 0);
        memset(t->step, 0, sizeof t->step);
        steps_load(t, 0, ps);
        check((t->step[0].flags & SF_SLIDE) && sizeof(pstep_t) == 10u, "a glide step keeps it through the stored step (10 bytes, as before)");
    }
    return fails;
}
