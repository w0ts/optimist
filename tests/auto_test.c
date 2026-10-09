/* SPDX-License-Identifier: GPL-3.0-only */
/* The automation store (firmware/src/seq/auto.h, auto.c; storage/auto_proj.c, sections/pat.c, stepx_log.c,
 * motion_flash.c; docs/UI-OPTIMIST-DESIGN.md section 6, phase 3) on a simulated NOR with the section log and the
 * patterns, every switch of the store on (MOTION, PLOCK, MICRO, FILLS, CHANCE):
 *   list       128 events a list, (place, param) once, the pseudo-parameters' ranges; full: no lock, no motion
 *   play       a hold event holds until the next of its parameter (the pattern restart: the patch), a step-only one
 *              its step; both on one parameter: the step's event wins, the base it returns to is the hold value;
 *              a fill-skipped step: its hold events apply, its step-only ones do not; chance on drum steps, an event
 *              winning over a synth step's chance bits
 *   forms      an old MOTN record, an old extras record (2.4's stepx form), an old pattern record (V1: motion chunk
 *              and PF_SX) read into the lists, stored again in the same old form (byte for byte where the form is
 *              written from them), read equal; what today's forms cannot hold (a chance, 65 hold events, 25 locks, a
 *              hold of a lockable-only value) stored in the new forms (a V2 pattern record, the extras record's
 *              AUTO_ENC_TAG form) and read equal, refused by an older reader; a scene saved and loaded, the
 *              autosave written and read back
 *   2.4        the store exported as SLOOP 2.4's extras and imported back: nudges, fills and the first 24 locks a
 *              track survive, motion and chance are reported; a lock's value past a signed byte clamped, reported
 *   undo       a step edit of the list undone and redone, byte for byte
 *   editor     AUTO_GET / AUTO_SET (v11), and the older LOCK_SET on the list
 *   toggle     a lock made a hold event and back (YES on STEP: auto_kind_toggle); not for a value motion refuses
 * Exit status: the number of failed checks. Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_PATTERNS 1
#define FELUCCA_MOTION 1
#define FELUCCA_PLOCK 1
#define FELUCCA_MICRO 1
#define FELUCCA_FILLS 1
#define FELUCCA_CHANCE 1
#define FELUCCA_SL24_EXPORT 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"
#include "../firmware/src/storage/motion_flash.c"

static struct { int force; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/storage/sections/sections.c"
#define AUTO_VIEW_PROJ 1
#include "auto_view.h"

#include "../firmware/src/io/editor/ed_out.h"
/* the reply builder as editor.c has it (the host build has no editor.c) */
static uint8_t ed_out[ED_OUT_N];
static uint32_t ed_n;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out - 1u) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_v(int32_t v) { uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192); ed_b(u); ed_b(u >> 7); }
static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
#include "../firmware/src/io/editor/ed_stepx.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("auto: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---- the sequencer: drum hits and their steps */
typedef struct { uint32_t pos; uint8_t note; } hit_t;
static hit_t hits[20000];
static uint32_t nhits;
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k, beat = clk_beat, pos = clk_pos;
    mix_block(out, CTL);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && nhits < 20000u) {
                hits[nhits].pos = beat * BEAT_U + pos;
                hits[nhits++].note = drums.v[k].note;
            }
}
static void clear_lists(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        AUTO_L(k)->n = 0;
    auto_w.on = 0;
    auto_touch_all();
}
static void reset(void)
{
    uint32_t i;
    seq_stop();
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        steps_clear(&trk[i]);
        trk[i].seq_abs = SEQ_NONE;
        trk[i].seq_idx = 0;
        trk[i].p[P_SLEN] = 8;
    }
    clear_lists();
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    nhits = 0;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    song.rec = 0;
    rec_wait = 0;
    fill_held = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    clk_beat = clk_pos = 0;
    song.playing = 0;
}
/* the blocks until track t enters step `step` (from another step) */
static void wait_step(track_t *t, uint32_t step)
{
    uint32_t g = 0;
    while (t->seq_abs != SEQ_NONE && t->seq_idx == step && g++ < 100000u)
        run_block();
    while (!(t->seq_abs != SEQ_NONE && t->seq_idx == step) && g++ < 100000u)
        run_block();
}

static void t_list(void)
{
    track_t *t = &trk[0];
    uint32_t i, n = 0;
    int32_t v;
    reset();
    for (i = 0; i < 64u; i++)
        n += lock_set(t, i, P_PAN, (int32_t)i - 32);
    for (i = 0; i < 64u; i++)
        n += lock_set(t, i, P_REV, (int32_t)i);
    check("128 step-only events on one track: all taken", n == 128u && AL(t)->n == AUTO_MAX);
    check("... the 129th: no lock, no motion (LIST FULL), no nudge; the other tracks unaffected",
          !lock_set(t, 0, P_DLY, 5) && motion_set_event(t, 0, P_CHOR, 5) == 2 && motion_full &&
          !step_micro_set(t, 3, 4) && lock_set(&trk[1], 0, P_DLY, 5));
    check("... a lock there already: its value changes", lock_set(t, 5, P_PAN, 9) && lock_get(t, 5, P_PAN, &v) && v == 9);
    motion_full = 0;
    clear_lists();
    check("(place, param) once: a hold and a step-only event of one parameter on one step are two",
          motion_set_event(t, 2, P_PAN, 10) == 0 && lock_set(t, 2, P_PAN, 20) && motion_set_event(t, 2, P_PAN, 11) == 0 &&
          AL(t)->n == 2u && hold_get(t, 2, P_PAN, &v) && v == 11);
    check("the pseudo-parameters: NUDGE -32..31, FILL 1..2, CHANCE 0..99; their default drops the event",
          step_micro_set(t, 1, -40) && step_micro(t, 1) == MICRO_MIN && step_micro_set(t, 1, 0) && !step_micro(t, 1) &&
          step_fill_set(t, 1, FC_NOFILL) && step_fill(t, 1) == FC_NOFILL && step_fill_set(t, 1, FC_NORM) &&
          step_chance_set(t, 1, 30) && step_chance_ev(t, 1) == 30u && step_chance_set(t, 1, 100) && AL(t)->n == 2u);
    {
        auto_ev_t e = {AUTO_NUDGE, AUTO_NUDGE, 4}, f = {0x80u | 3u, P_PAN, 1};
        e.place = 3;                                   /* (a NUDGE event without STEP-ONLY) */
        check("an event a list never holds: a pseudo-parameter as a hold event, place bit 7 set", !auto_ev_ok(&e) && !auto_ev_ok(&f));
    }
}

/* both kinds on one parameter: PAN, a hold of 20 on step 1, a lock of -30 on step 3, on step 5 a hold of 40 and a
 * lock of 10 */
static void t_both(void)
{
    track_t *t = &trk[0];
    int16_t b, v[8], w0;
    uint32_t i;
    reset();
    t->p[P_PAN] = 0;
    b = t->p[P_PAN];
    (void)motion_set_event(t, 1, P_PAN, 20);
    (void)lock_set(t, 3, P_PAN, -30);
    (void)motion_set_event(t, 5, P_PAN, 40);
    (void)lock_set(t, 5, P_PAN, 10);
    transport_req = 1;
    for (i = 0; i < 8u; i++) {
        wait_step(t, i);
        v[i] = t->p[P_PAN];
    }
    wait_step(t, 0);
    w0 = t->p[P_PAN];
    printf("auto: PAN by step: %d %d %d %d %d %d %d %d, the next pass %d\n", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], w0);
    check("hold 20 from step 2, lock -30 on step 4 only, back to the hold's 20", v[0] == b && v[1] == 20 && v[2] == 20 &&
          v[3] == -30 && v[4] == 20);
    check("a hold and a lock on step 6: the lock wins there (10), then the hold's 40 until the pass ends", v[5] == 10 &&
          v[6] == 40 && v[7] == 40);
    check("the pattern restart: the patch (motion: each pass from its base)", w0 == b);
    seq_stop();
    check("STOP: the patch back", t->p[P_PAN] == b);
}

/* fills: step 2 FILL ONLY with a hold (CHORUS 70) and a lock (PAN -50) on it */
static void t_fills(void)
{
    track_t *t = &trk[0];
    int16_t pan, cho;
    reset();
    t->step[2].note[0] = 60, t->step[2].n = 1, t->step[2].time = ST_NOTE, t->step[2].vel = 100;
    t->p[P_PAN] = 0, t->p[P_CHOR] = 0;
    (void)step_fill_set(t, 2, FC_FILL);
    (void)motion_set_event(t, 2, P_CHOR, 70);
    (void)lock_set(t, 2, P_PAN, -50);
    transport_req = 1;
    wait_step(t, 2);
    pan = t->p[P_PAN], cho = t->p[P_CHOR];
    check("a fill-skipped step: its hold event applies (CHORUS 70), its lock does not (PAN 0), no note",
          cho == 70 && pan == 0 && seq_skip[0] && !t->seq_n);
    fill_held = 1;
    wait_step(t, 2);
    check("... in a fill: the step plays, its lock too (PAN -50)", t->p[P_PAN] == -50 && t->p[P_CHOR] == 70 && !seq_skip[0]);
    fill_held = 0;
    seq_stop();
}

/* chance: drum lane 0 on every step of 8; step 3 chance 0 (never), step 5 chance 50; a synth step's bits and events */
static uint32_t hits_on(uint32_t step, uint32_t slen)
{
    uint32_t i, n = 0;
    for (i = 0; i < nhits; i++)
        n += hits[i].note == LANE_NOTE[0] && (hits[i].pos + slen / 2u) / slen % 8u == step;
    return n;
}
static void t_chance(void)
{
    uint32_t i, slen = BEAT_U / 4u, n0, n3, n5;
    reset();
    TDRUM->p[P_SLEN] = 8;
    for (i = 0; i < 8u; i++)
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
    (void)step_chance_set(TDRUM, 3, 0);
    (void)step_chance_set(TDRUM, 5, 50);
    transport_req = 1;
    while (clk_beat < 128u)                           /* 64 passes */
        run_block();
    n0 = hits_on(0, slen), n3 = hits_on(3, slen), n5 = hits_on(5, slen);
    printf("auto: drum chance: step 1 %u hits, step 4 (0 %%) %u, step 6 (50 %%) %u of 64\n", n0, n3, n5);
    check("chance on drum steps: 0 % never, 50 % about half, none: every pass", n0 == 64u && n3 == 0u && n5 > 16u && n5 < 48u);
    seq_stop();
    {   /* a synth step: its bits say never, its event 99 %; the other way, the event 0 % */
        track_t *t = &trk[0];
        reset();
        for (i = 0; i < 2u; i++)
            t->step[i].note[0] = 60, t->step[i].n = 1, t->step[i].time = ST_NOTE, t->step[i].vel = 100;
        step_set_chance(&t->step[0], 0);
        (void)step_chance_set(t, 0, 99);
        (void)step_chance_set(t, 1, 0);
        transport_req = 1;
        wait_step(t, 0);
        n0 = auto_ch[0] == 99u && !chance_drop(t, &t->step[0]) ? 1u : 0u;   /* (99 %: one in a hundred drops) */
        wait_step(t, 1);
        check("a synth step: an event wins over its chance bits (bits never, event 99 %; event 0 %: never)",
              n0 && auto_ch[0] == 0u && chance_drop(t, &t->step[1]));
        seq_stop();
    }
}

/* ---- the stored forms */
static motion_store_t old_m;
static void old_motion(void)                           /* an old store: 10 events, the tracks interleaved */
{
    static const uint8_t PL[10] = {0x02, 0x41, 0x83, 0x05, 0xC0, 0x47, 0x09, 0x8A, 0x0C, 0x4E};
    uint32_t i;
    memset(&old_m, 0, sizeof old_m);
    for (i = 0; i < 10u; i++)
        old_m.ev[i].place = PL[i], old_m.ev[i].param = (uint8_t)(i & 1u ? P_CHOR : P_DLY), old_m.ev[i].value = (int8_t)(5 * i);
    old_m.count = 10;
    old_m.on = 0x0F;
}
static void make_tracks(uint32_t s)
{
    uint32_t i, k;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = 16;
        for (k = 0; k < 4u; k++)
            if (i == TRK_DRUM)
                dstep_set(&trk[i].dstep[k * 2u], (k + s) % 16u, LV_NORM, 0);
            else
                trk[i].step[k * 2u].note[0] = (uint8_t)(40u + s + k + i), trk[i].step[k * 2u].n = 1,
                trk[i].step[k * 2u].time = ST_NOTE;
    }
}
static project_t gp;                                  /* (every capture's buffer: a store is bound once, as */
static dlrec_t gd;                                     /*  persist_boot binds the firmware's) */
static int lists_same(const auto_list_t *a)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (!auto_list_same(&a[k], AUTO_L(k)))
            return 0;
    return 1;
}
static void t_motn(void)
{
    static motion_rec_t r;
    static auto_list_t got[NTRK];
    uint32_t i;
    int ok;
    reset();
    make_tracks(1);
    proj_capture(&gp, &gd);
    memset(nor, 0xFF, sizeof nor);
    ok = st_save(OBJ_AUTOSAVE, &gp, sizeof gp) == 0;
    old_motion();
    old_m.psum = gp.sum;
    r.magic = MF_MAGIC, r.m = old_m, r.crc = st_crc32(&r, sizeof r - 4u);   /* (as motion_flash.c wrote it before) */
    {
        st_hdr_t h;
        int c = st_current(OBJ_AUTOSAVE, &h);
        ok &= c >= 0 && st_prog(st_sector(OBJ_AUTOSAVE, (uint32_t)c) + MF_OFF, &r, sizeof r) == 0;
    }
    motion_flash_read(OBJ_AUTOSAVE, &gp);
    proj_apply(&gp, &gd, 1);
    for (i = 0, ok &= mview()->count == 10u && auto_w.on == 0x0F; i < 10u; i++) {
        int32_t v;
        ok &= auto_get(AUTO_L(old_m.ev[i].place >> 6), old_m.ev[i].place & 63u, old_m.ev[i].param, &v) && v == old_m.ev[i].value;
    }
    check("an old MOTN record (64 events for the four tracks) read: each its track's hold event", ok);
    memcpy(got, auto_w.l, sizeof got);
    proj_capture(&gp, &gd);
    ok = st_save(OBJ_AUTOSAVE, &gp, sizeof gp) == 0;
    motion_flash_write(OBJ_AUTOSAVE, &gp);
    clear_lists();
    motion_flash_read(OBJ_AUTOSAVE, &gp);
    proj_apply(&gp, &gd, 1);
    check("... stored again (the MOTN record, today's form) and read: equal", ok && lists_same(got) && auto_w.on == 0x0F);
}
/* an old extras record: 2.4's stepx form of four tracks (nudges, locks, fills) */
static stepx_t old_x[NTRK];
static uint32_t old_extras(uint8_t *o)
{
    uint32_t k, n = 0;
    for (k = 0; k < NTRK; k++) {
        stepx_clear(&old_x[k]);
        old_x[k].micro[(3u + k) % 64u] = (int8_t)(-4 - (int)k);
        old_x[k].micro[40] = MICRO_MAX;
        (void)stepx_lock_set(&old_x[k], 2u + k, P_PAN, (int16_t)(10 + (int)k));
        (void)stepx_lock_set(&old_x[k], 9, P_LD_FLT, -20);
        old_x[k].lock[5].step = LOCK_FREE;            /* (a free slot in the middle: packed when stored) */
        (void)stepx_lock_set(&old_x[k], 11, P_REV, 77);
        stepx_fill_set(&old_x[k], 6u + k, k & 1u ? FC_FILL : FC_NOFILL);
        n += stepx_encode_trk(&old_x[k], o + n);
    }
    return n;
}
static void t_extras(void)
{
    static uint8_t a[AX_ENC_MAX], b[AX_ENC_MAX];
    static auto_store_t m, m2;
    stepx_t x;
    uint32_t na, nb, k;
    int ok = 1;
    na = old_extras(a);
    memset(&m, 0, sizeof m);
    ok &= ax_decode(&m, a, na);
    for (k = 0; k < NTRK; k++)
        ok &= auto_to_stepx(&x, &m.l[k]) == 0u && !memcmp(x.micro, old_x[k].micro, 64) && !memcmp(x.fill, old_x[k].fill, 16);
    check("an old extras record (2.4's form) read: its nudges, locks, fills as step-only events", ok && auto_count(&m.l[0], AUTO_ONLYS) == 6u);
    nb = ax_encode(&m, b);
    check("... stored again: today's form, byte for byte", nb == na && !memcmp(a, b, na));
    memset(&m2, 0, sizeof m2);
    ok = ax_decode(&m2, b, nb);
    check("... read: equal", ok && auto_store_same(&m, &m2));
    (void)auto_put(&m.l[1], 4u | AUTO_ONLY, AUTO_CHANCE, 25);   /* (a chance: not 2.4's form) */
    nb = ax_encode(&m, b);
    memset(&m2, 0, sizeof m2);
    check("a store with a chance: the new form (AUTO_ENC_TAG), read equal", b[0] == AUTO_ENC_TAG && ax_decode(&m2, b, nb) &&
          auto_store_same(&m, &m2));
    {
        const uint8_t *p = b;
        check("... an older build reads it as a bad stepx record: refused (nothing taken)", !stepx_decode_trk(&x, &p, b + nb));
    }
    memset(&m2, 0, sizeof m2);
    b[nb - 1u] ^= 0x40u;                               /* (a malformed event: a step past 63) */
    b[nb - 3u] |= 0x80u;
    check("... a malformed one refused, the store left empty", !ax_decode(&m2, b, nb) && !m2.l[0].n && !m2.l[3].n);
}
/* an old pattern record of track 0: V1, a motion chunk (3 events), the steps (codec A), PF_SX (a nudge, a lock, a fill) */
static uint32_t old_pattern(uint8_t *o, const project_t *p)
{
    uint8_t *q = o + 5;
    uint32_t i, b = sec_b_gain(&p->t[0], 0) > 0;      /* (the codec the firmware picks: B when shorter) */
    stepx_t x;
    o[0] = (uint8_t)(PF_V1 | PF_MOT | PF_ON | PF_SX | (b ? PF_B : 0u));
    for (i = 0; i < 4u; i++)
        o[1u + i] = (uint8_t)p->t[0].p[P_SLEN + i];
    *q++ = 3;
    for (i = 0; i < 3u; i++)
        *q++ = (uint8_t)(1u + 4u * i), *q++ = (uint8_t)P_CHOR, *q++ = (uint8_t)(20u * i);
    q = sec_steps_put(&p->t[0], 0, q, (int)b, 0);
    stepx_clear(&x);
    x.micro[2] = 9;
    (void)stepx_lock_set(&x, 4, P_PAN, -12);
    stepx_fill_set(&x, 6, FC_NOFILL);
    q += stepx_encode_trk(&x, q);
    return (uint32_t)(q - o);
}
static void t_pattern(void)
{
    static uint8_t a[SEC_REC_MAX], b[SEC_REC_MAX];
    static auto_list_t l, l2;
    auto_store_t *m;
    uint32_t na, nb, i;
    uint8_t on = 0;
    int ok;
    reset();
    make_tracks(2);
    proj_capture(&gp, &gd);
    na = old_pattern(a, &gp);
    ok = pat_decode(a, na, 0, &gp.t[0], &l, &on) && on == 1u;
    check("an old pattern record (V1: 3 motion events, PF_SX: a nudge, a lock, a fill) read: 6 events", ok && l.n == 6u &&
          auto_count(&l, AUTO_HOLDS) == 3u && auto_pseudo(&l, 2, AUTO_NUDGE, 0) == 9 && auto_pseudo(&l, 6, AUTO_FILL, 0) == FC_NOFILL);
    m = auto_for(&gp, 1);
    auto_store_clear(m);
    m->psum = gp.sum;
    m->l[0] = l;
    m->on = 1;
    nb = pat_encode(&gp, 0, b);
    check("... stored again: V1, byte for byte", nb == na && !memcmp(a, b, na));
    check("... read: equal", pat_decode(b, nb, 0, &gp.t[0], &l2, &on) && auto_list_same(&l, &l2));
    {   /* what V1 cannot hold: V2 */
        static const char *const WHY[4] = {"a chance", "65 hold events", "25 locks", "a hold event of a lockable-only value (GATE)"};
        uint32_t w;
        for (w = 0; w < 4u; w++) {
            m->l[0] = l;
            if (w == 0u)
                (void)auto_put(&m->l[0], 3u | AUTO_ONLY, AUTO_CHANCE, 40);
            for (i = 0; w == 1u && i < 62u; i++)
                (void)auto_put(&m->l[0], i, P_DLY, (int32_t)i);
            for (i = 0; w == 2u && i < 24u; i++)
                (void)auto_put(&m->l[0], i | AUTO_ONLY, P_REV, (int32_t)i);
            if (w == 3u)
                (void)auto_put(&m->l[0], 7, P_SGATE, 30);
            nb = pat_encode(&gp, 0, b);
            ok = (b[0] & PF_VER) == PF_V2 && !(b[0] & PF_SX) && (b[0] & PF_MOT) && pat_decode(b, nb, 0, &gp.t[0], &l2, &on) &&
                 auto_list_same(&m->l[0], &l2) && on == 1u;
            ok &= (b[0] & PF_VER) != PF_V1;            /* (an older build: "not a pattern this build reads") */
            printf("auto: pattern with %s: %u B (V2)\n", WHY[w], nb);
            check(w == 0u ? "a pattern V1 cannot hold (a chance): V2, its chunk the list, read equal; an older build refuses it"
                          : w == 1u ? "... 65 hold events: V2, read equal"
                          : w == 2u ? "... 25 locks: V2, read equal" : "... a hold of GATE (not recordable): V2, read equal", ok);
        }
    }
    {   /* 128 events: the longest V2 record fits a log record */
        m->l[0].n = 0;
        for (i = 0; i < 64u; i++)
            (void)auto_put(&m->l[0], i, P_DLY, 1), (void)auto_put(&m->l[0], i | AUTO_ONLY, P_PAN, 2);
        nb = pat_encode(&gp, 0, b);
        check("128 events: V2, n = 128, read equal, within PAT_REC_MAX", (b[0] & PF_VER) == PF_V2 && b[5] == 128u &&
              nb <= PAT_REC_MAX && pat_decode(b, nb, 0, &gp.t[0], &l2, &on) && auto_list_same(&m->l[0], &l2));
    }
}
/* a project with every kind, saved (a scene and its patterns) and loaded; the autosave */
static void dense_auto(uint32_t s)
{
    uint32_t k, i;
    clear_lists();
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < 20u; i++)
            (void)motion_set_event(&trk[k], (i * 3u + s) % 16u, k == TRK_DRUM ? P_REV : P_CHOR, (int32_t)(i + s));
        for (i = 0; i < 30u; i++)
            (void)auto_put(AUTO_L(k), ((i * 5u + k) % 16u) | AUTO_ONLY, i & 1u ? P_PAN : P_DLY, (int32_t)i - 15);
        (void)step_micro_set(&trk[k], 1, -7 - (int32_t)k);
        (void)step_fill_set(&trk[k], 2, FC_FILL);
        (void)step_chance_set(&trk[k], 3, 60u + k);
    }
    auto_w.on = 0x0B;
}
static void t_save(void)
{
    static auto_store_t want;
    reset();
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    memset(pat_cur, PAT_NONE, NTRK);
    live_sec = -1;
    make_tracks(3);
    dense_auto(3);
    want = auto_w;
    project_save(2);
    make_tracks(4);
    clear_lists();
    project_load(2);
    check("a scene saved with 20 holds, 30 locks, a nudge, a fill and a chance a track: loaded, every event (V2 patterns)",
          !strcmp(last_msg, "LOADED") && auto_store_same(&want, &auto_w));
    {   /* the autosave: MOTN (the first 64 hold events) + the extras record in the new form, read as autosave_resume */
        proj_capture(&gp, &gd);
        (void)st_save(OBJ_AUTOSAVE, &gp, sizeof gp);
        motion_flash_write(OBJ_AUTOSAVE, &gp);
        (void)sx_log_put(SX_ID_AUTO, gp.sum, &gp, 0);
        clear_lists();
        memset(auto_for(&gp, 1), 0, sizeof(auto_store_t));
        motion_flash_read(OBJ_AUTOSAVE, &gp);
        sx_log_get(SX_ID_AUTO, gp.sum, &gp);
        proj_apply(&gp, &gd, 1);
        check("the autosave: MOTN and the extras record (new form) read back: every event", auto_store_same(&want, &auto_w));
    }
}

/* ---- SLOOP 2.4 */
static void t_sl24(void)
{
    static uint8_t fun5[SL24_SIZE];
    static project_t q;
    static stepx_t x[NTRK], y[NTRK];
    static auto_store_t back;
    const stepx_t *xp[NTRK] = {&x[0], &x[1], &x[2], &x[3]};
    uint32_t k, i, lost = 0, cl;
    int ok = 1;
    reset();
    make_tracks(5);
    clear_lists();
    for (k = 0; k < NPART; k++) {
        (void)step_micro_set(&trk[k], 2, 5);
        (void)step_fill_set(&trk[k], 4, FC_NOFILL);
        for (i = 0; i < 26u; i++)                     /* (26 locks: the last two past 2.4's 24) */
            (void)lock_set(&trk[k], i, P_PAN, (int32_t)i - 13);
        (void)motion_set_event(&trk[k], 9, P_CHOR, 33);
        (void)step_chance_set(&trk[k], 5, 50);
    }
    (void)lock_set(&trk[0], 30, P_FXOFF, 1);          /* (2.4 has no FX OFF) */
    proj_capture(&gp, &gd);
    for (k = 0; k < NTRK; k++)
        lost |= sl24_auto_out(&x[k], AUTO_L(k));
    lost |= proj_to_sl24(&gp, xp, fun5, 0);
    printf("auto: 2.4 export lost 0x%X\n", lost);
    check("exported for 2.4: motion and chance reported (MOTION NOT IN 2.4), locks past 24 reported",
          (lost & SX24_MOTION) && (lost & SX24_CHANCE) && (lost & SX24_LOCK));
    ok = proj_from_sl24(&q, fun5, SL24_SIZE, y, 0);
    cl = sl24_auto_in(&back, y);
    for (k = 0; k < NPART; k++) {
        int32_t v;
        ok &= auto_pseudo(&back.l[k], 2, AUTO_NUDGE, 0) == 5 && auto_pseudo(&back.l[k], 4, AUTO_FILL, 0) == FC_NOFILL;
        ok &= auto_count(&back.l[k], AUTO_HOLDS) == 0u && auto_pseudo(&back.l[k], 5, AUTO_CHANCE, 100) == 100;
        for (i = 0; i < 26u; i++)
            ok &= auto_get(&back.l[k], i | AUTO_ONLY, P_PAN, &v) == (i < 24u) && (i >= 24u || v == (int32_t)i - 13);
    }
    ok &= !auto_get(&back.l[0], 30u | AUTO_ONLY, P_FXOFF, (int32_t[]){0});
    check("... imported back: the nudges, fills, the first 24 locks a track; no motion, no chance, no FX OFF lock",
          ok && !cl);
    stepx_clear(&y[0]);
    (void)stepx_lock_set(&y[0], 3, P_LEVEL, 300);      /* (a 2.4 lock past a signed byte) */
    cl = sl24_auto_in(&back, y);
    {
        int32_t v;
        check("a 2.4 lock of 300: clamped to 127, the import says so (AUTO_CLAMP)", cl == AUTO_CLAMP &&
              auto_get(&back.l[0], 3u | AUTO_ONLY, P_LEVEL, &v) && v == 127);
    }
}

/* ---- undo, the editor, the kind toggle */
static void t_undo(void)
{
    track_t *t = &trk[0];
    auto_list_t before, after;
    reset();
    (void)lock_set(t, 1, P_PAN, 4);
    before = *AL(t);
    undo_clear();
    undo_mark(t, (undo_sess += 4u) | 3u);
    (void)lock_set(t, 2, P_PAN, 8);
    (void)step_micro_set(t, 2, 6);
    (void)motion_set_event(t, 3, P_CHOR, 9);
    after = *AL(t);
    undo_close();
    check("undo: the list as it was (a lock, a nudge, a hold event gone)", undo_apply(0) && !memcmp(AL(t), &before, sizeof before) &&
          !step_micro(t, 2));
    check("redo: the three back, byte for byte, the nudge playing again", undo_apply(1) && !memcmp(AL(t), &after, sizeof after) &&
          auto_nudge[0][2] == 6);
}
static int ed_call(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    ed_n = 0;
    return ed_stepx(cmd, a, na);
}
static void t_editor(void)
{
    uint8_t a[8];
    int32_t v;
    reset();
    a[0] = 1, a[1] = 0, a[2] = 3, a[3] = P_CHOR, a[4] = 0, a[5] = (uint8_t)((50 + 8192) & 127), a[6] = (uint8_t)((50 + 8192) >> 7);
    check("AUTO_SET SET: a hold event, track 2, step 4, CHORUS 50 (its PLAY bit on)", ed_call(ED_AUTO_SET, a, 7) &&
          ed_out[2] == 0 && hold_get(&trk[1], 3, P_CHOR, &v) && v == 50 && (auto_w.on & 2u));
    a[2] = 2 | AUTO_ONLY, a[3] = AUTO_CHANCE & 127u, a[4] = AUTO_CHANCE >> 7, a[5] = (uint8_t)((40 + 8192) & 127);
    a[6] = (uint8_t)((40 + 8192) >> 7);
    check("AUTO_SET SET: a chance of 40 % on step 3 (a pseudo-parameter: 2 x 7 bit)", ed_call(ED_AUTO_SET, a, 7) && ed_out[2] == 0 &&
          step_chance_ev(&trk[1], 2) == 40u);
    a[2] = 2;                                          /* (CHANCE without STEP-ONLY) */
    check("AUTO_SET: a pseudo-parameter as a hold event: refused (rc 1)", ed_call(ED_AUTO_SET, a, 7) && ed_out[2] == 1);
    a[0] = 1;
    check("AUTO_GET: n 2, the PLAY bit, both events as stored", ed_call(ED_AUTO_GET, a, 1) && ed_out[1] == 2 && ed_out[2] == 0 &&
          ed_out[3] == 1 && ed_out[4] == 0 && ed_out[5] == 2 && ed_out[6] == 3 && ed_out[7] == P_CHOR && ed_out[8] == 0 &&
          ed_out[11] == (2 | AUTO_ONLY) && ed_out[12] == (AUTO_CHANCE & 127u) && ed_out[13] == 1);
    a[1] = 1, a[2] = 3, a[3] = P_CHOR, a[4] = 0;
    check("AUTO_SET DEL: the hold event gone", ed_call(ED_AUTO_SET, a, 5) && ed_out[2] == 0 && !hold_get(&trk[1], 3, P_CHOR, &v));
    a[1] = 2, a[2] = 3;
    check("AUTO_SET CLEAR (all): the list empty", ed_call(ED_AUTO_SET, a, 3) && !AL(&trk[1])->n);
    a[1] = 3, a[2] = 0;
    check("AUTO_SET PLAY 0: the track's motion off", ed_call(ED_AUTO_SET, a, 3) && !(auto_w.on & 2u));
    {
        static const uint8_t lset[5] = {1, 5, P_PAN, (uint8_t)((20 + 8192) & 127), (uint8_t)((20 + 8192) >> 7)};
        check("the older LOCK_SET (73) writes a step-only event of the list", ed_call(ED_LOCK_SET, lset, 5) && ed_out[3] == 1 &&
              lock_get(&trk[1], 5, P_PAN, &v) && v == 20 && AL(&trk[1])->n == 1u);
    }
}
static void t_toggle(void)
{
    track_t *t = &trk[0];
    int32_t v;
    reset();
    (void)lock_set(t, 4, P_PAN, -9);
    check("YES (HOLD): a lock becomes a hold event, its value kept, PLAY on", auto_kind_toggle(t, 4, P_PAN) == 1u &&
          hold_get(t, 4, P_PAN, &v) && v == -9 && !lock_get(t, 4, P_PAN, &v) && (auto_w.on & 1u));
    check("... again: step-only", auto_kind_toggle(t, 4, P_PAN) == 2u && lock_get(t, 4, P_PAN, &v) && v == -9);
    (void)lock_set(t, 5, P_SGATE, 40);
    check("a lock of GATE (lockable, not recordable): no HOLD", auto_kind_toggle(t, 5, P_SGATE) == 0u && lock_get(t, 5, P_SGATE, &v));
}

int main(void)
{
    (void)auto_for(&proj_tmp.cur, 1), (void)auto_for(&sec_stage_p, 1);   /* (as persist_boot binds them) */
    auto_init();
    t_list();
    t_both();
    t_fills();
    t_chance();
    t_motn();
    t_extras();
    t_pattern();
    t_save();
    t_sl24();
    t_undo();
    t_editor();
    t_toggle();
    printf("auto test %s (%d failed)\n", bad ? "FAILED" : "passed", bad);
    return bad;
}
