/* SPDX-License-Identifier: GPL-3.0-only */
/* Bench 18 (bench.c): the PLAITS costs, each Plaits algorithm Optimist has (the PLAITS engine's four models through its
 * presets, the PLAITS kit's KPUNC and SCRAK sounds) and two references (ANALOG's first preset, a 909-style kit sound).
 * Measured once from the main loop (bench_frame, after 64 blocks), interrupts off, in timer ticks (24 a us; in the
 * emulator the instructions at FM1_CPU_MHZ): for each subject, each of BPL_REP times from rest, the note start
 * (trk_note_on: what the block's events cost on CPU0, PRE) and its first block (track_render / ds_render: the part's or
 * the drums' job), their least over the repeats; then BPL_BLK blocks after it (their sum and largest). bench_pl[k]: {note
 * start, first block, later blocks' sum, their count, their largest, the FNV-1a of their samples}: drums
 * until the hit ends; two builds' hashes the same: the same samples on the target. A chord (BPL_CHORD notes on one
 * block) as a subject of its own: the worst a part's note start can be. */
#define BPL_REP 4u
#define BPL_BLK 400u
#define BPL_CHORD 4u
#define BPL_N 40u
volatile uint32_t bench_pl[BPL_N][6] __attribute__((used));
volatile uint32_t bench_pl_n __attribute__((used));
volatile uint32_t bench_pl_done __attribute__((used));
static int32_t bpl_buf[CTL];
static uint32_t bpl_hash(uint32_t h)                        /* FNV-1a of the block in bpl_buf: two builds' samples compared */
{
    uint32_t i;
    for (i = 0; i < CTL; i++)
        h = (h ^ (uint32_t)bpl_buf[i]) * 16777619u;
    return h;
}

static void bpl_quiet_part(track_t *t)                      /* every voice of part t off, at rest */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        v->active = 0, v->gate = 0, v->env = 0, v->env_out = 0, v->stage = 0;
    }
    t->nmono = 0, t->mono_note = 0;
}
static void bpl_quiet_drums(void)
{
    uint32_t i;
    for (i = 0; i < NDRUM; i++)
        drums.v[i].active = 0, drums.v[i].age = 0;
    drums.tail = 0;
}

/* part 0 on engine slot ei, preset pi; nn notes from rest */
static void bpl_part(uint32_t k, uint32_t ei, uint32_t pi, uint32_t nn)
{
    track_t *t = &trk[0];
    uint32_t r, b, i, ts, t0, best_on = ~0u, best_b1 = ~0u, sum = 0, mx = 0, h = 0x811C9DC5u;
    fm1_irq_off();
    t->xf_on = 0, t->xf = 0, t->pold = 0;                 /* (switched by the firmware: bpl_run waits for it) */
    t->p[P_VOICE] = V_POLY;
    for (r = 0; r < BPL_REP; r++) {
        bpl_quiet_part(t);
        t0 = fm1_ticks();
        for (i = 0; i < nn; i++)
            FAST_CALL(seq, trk_note_on)(t, 48u + 7u * i, 100u);
        ts = fm1_ticks() - t0;
        best_on = ts < best_on ? ts : best_on;
        t0 = fm1_ticks();
        FAR(track_render)(t, bpl_buf, CTL);
        ts = fm1_ticks() - t0;
        best_b1 = ts < best_b1 ? ts : best_b1;
    }
    for (b = 0; b < BPL_BLK; b++) {                         /* the last repeat's note on */
        t0 = fm1_ticks();
        FAR(track_render)(t, bpl_buf, CTL);
        ts = fm1_ticks() - t0;
        sum += ts, mx = ts > mx ? ts : mx;
        h = bpl_hash(h);
    }
    bpl_quiet_part(t);
    fm1_irq_on();
    bench_pl[k][0] = best_on, bench_pl[k][1] = best_b1, bench_pl[k][2] = sum, bench_pl[k][3] = BPL_BLK;
    bench_pl[k][4] = mx, bench_pl[k][5] = h;
}

/* the drum track on kit uid, GM note */
static void bpl_drum(uint32_t k, uint32_t uid, uint32_t note)
{
    uint32_t r, b, ts, t0, best_on = ~0u, best_b1 = ~0u, sum = 0, mx = 0, alive = 1, h = 0x811C9DC5u;
    TDRUM->p[P_E0] = (int16_t)uid;
    fm1_irq_off();
    for (r = 0; r < BPL_REP; r++) {
        bpl_quiet_drums();
        t0 = fm1_ticks();
        FAST_CALL(seq, trk_note_on)(TDRUM, note, 100u);
        ts = fm1_ticks() - t0;
        best_on = ts < best_on ? ts : best_on;
        t0 = fm1_ticks();
        alive = (uint32_t)FAR(ds_render)(&drums.ds[0], bpl_buf, CTL);
        ts = fm1_ticks() - t0;
        best_b1 = ts < best_b1 ? ts : best_b1;
    }
    for (b = 0; b < 20000u && alive; b++) {                 /* until it ends (a PLAITS hit: at most 6 s) */
        t0 = fm1_ticks();
        alive = (uint32_t)FAR(ds_render)(&drums.ds[0], bpl_buf, CTL);
        ts = fm1_ticks() - t0;
        sum += ts, mx = ts > mx ? ts : mx;
        h = bpl_hash(h);
    }
    bpl_quiet_drums();
    fm1_irq_on();
    bench_pl[k][0] = best_on, bench_pl[k][1] = best_b1, bench_pl[k][2] = sum, bench_pl[k][3] = b;
    bench_pl[k][4] = mx, bench_pl[k][5] = h;
}

/* the subjects in order: 0..11 the PLAITS presets with one note, 12..23 with a chord, then ANALOG preset 0 (a note, a
 * chord), then the PLAITS kit's four sounds and its hat. Called every main-loop frame: a part's engine is asked for,
 * then measured once the firmware switched it (its memory, the fade) and 64 blocks went by */
static uint32_t bpl_k, bpl_t0;
static uint8_t bpl_asked;
static void bpl_run(void)
{
    static const uint8_t DN[5] = {36, 35, 38, 40, 42};    /* PLAITS: KICK (KPUNC), KICK 2, SNARE (SCRAK), SNARE 2; its hat */
    uint32_t k = bpl_k, np = 0, ei, pi, nn;
#if FELUCCA_ENG_PLAITS
    np = 2u * ENG_PLAITS.npresets;
#endif
    if (k < np + 2u) {
        track_t *t = &trk[0];
#if FELUCCA_ENG_PLAITS
        ei = k < np ? ENG_IX_PLAITS : 0u;
        pi = k < np ? (k < np / 2u ? k : k - np / 2u) : 0u;
#else
        ei = 0u, pi = 0u;
#endif
        nn = (k < np ? k >= np / 2u : k - np) ? BPL_CHORD : 1u;
        if (!bpl_asked) {
            set_engine_of(t, ei);
            apply_preset_to(t, pi);
            bpl_asked = 1, bpl_t0 = bench.blk;
            return;
        }
        if (t->engine != t->eng_req || t->xf_on || PART_MOVING(t) || bench.blk - bpl_t0 < 64u)
            return;
        bpl_part(k, ei, pi, nn);
    } else if (k < np + 2u + 5u) {
#if FELUCCA_DS_PLAITS
        bpl_drum(k, DRUM_UID_XKIT + 1u, DN[k - np - 2u]);
#endif
    } else {
        bench_pl_n = k;
        bench_pl_done = 1;
        return;
    }
    bpl_k = k + 1u, bpl_asked = 0;
}
