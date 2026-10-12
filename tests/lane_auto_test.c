/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' motion and locks (seq/auto.h AUTO_LANE, auto.c auto_step, drums/drum_edit.c dla / dl_ofs_play,
 * drums/lane_play.c, seq/auto_lane.c; the user's rulings of 2026-10-12, D1..D12), on a simulated NOR with the section
 * log and the patterns, every switch of the store on:
 *   codec      lane l's value v is the stored id 0x7C + (l << 3 | v), all 128 below the pseudo-parameters; a list with
 *              them is well formed (auto_ev_ok, auto_dec_trk), round trips byte for byte
 *   forms      lane events never go into the old forms: the MOTN form leaves them out (an older motion_valid accepts
 *              it), the stepx form too (AUTO_X_LANE), so the pattern record is V2 and the extras record the new form,
 *              each read back equal; a scene saved and loaded, the autosave (MOTN + extras) read back
 *   play       a lane lock (DECAY -40 on step 4) is heard by that hit only, a lane hold (TUNE +5 from step 6) until the
 *              pass's end; a lock beats a hold; a fill-skipped step: its lane hold applies, its lane lock does not; the
 *              patch dl.ofs is never written; STOP, PLAY, the next pass and a pattern switch let them go; the render
 *              before the lock's hit is the render without it, sample for sample, and differs after it
 *   api        lane_lock_set needs the lane's hit (D4: LANE_NOHIT), lane_step_drop, lane_kind_toggle, lane_knob
 *              (recording: a hold at the step playing, heard at once; not recording: a hold in force lets go)
 *   editor     AUTO_SET takes a lane id on the drum track (clamped to its range), refuses it on a part; AUTO_GET lists it
 *   2.4        the export leaves them out and says so: lost2 bit 2 (SX24_LAUTO); a v11 request: on bit 13 (motion)
 *   old reader built with -DDLA_ON=0 (a build that does not play them): the events kept through every form, none
 *              heard, the track's other events still played, MISS lists LANE MOTION
 * Exit status: the number of failed checks. Run by tests/run_tests.sh (both builds). */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_PATTERNS 1
#define FELUCCA_MOTION 1
#define FELUCCA_PLOCK 1
#define FELUCCA_MICRO 1
#define FELUCCA_FILLS 1
#define FELUCCA_CHANCE 1
#define FELUCCA_SL24_EXPORT 1
#include <sys/mman.h>
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
static uint8_t ed_out[ED_OUT_N];
static uint32_t ed_n;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out - 1u) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_v(int32_t v) { uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192); ed_b(u); ed_b(u >> 7); }
static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
#include "../firmware/src/io/editor/ed_stepx.c"
static void ed_pack7(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(p[j] >> 7) << j;
        ed_b(m);
        for (j = 0; j < k; j++)
            ed_b(p[j]);
        p += k, n -= k;
    }
}
typedef struct { uint8_t palette, lowcut, zoom; } b26_settings_t;
static b26_settings_t settings;
typedef struct { uint32_t magic; uint8_t btn[14], enc[7]; int8_t dir[7]; } panel_t;
static panel_t panel;
static uint32_t bp23_word(void) { return 0x100u; }
static project_t autosave_buf;
static dlrec_t autosave_dl;
#include "../firmware/src/io/editor/ed_sl24.c"
#define MISS_SCAN_ONLY 1
#include "../firmware/src/storage/miss.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("lane: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define L 4u                                           /* the lane under test: CLOSED HAT (note 42) */
#define SLEN (BEAT_U / 4u)                             /* a 16th at 120 BPM, in clock units */
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
    memset(&dl, 0, sizeof dl);
    drums.set = -2;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    song.rec = 0;
    rec_wait = 0;
    fill_held = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    clk_beat = clk_pos = 0;
    song.playing = 0;
}
/* the hits of lane L: per step of 8 (the last pass), its offsets as the hit took them */
static int8_t heard[8][DE_N];
static uint8_t heard_n[8];
static int16_t (*outbuf)[44100 * 2];                  /* (shared with the render children: mmap) */
static uint32_t outn;
static void run_block_rec(int rec)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k, at = clk_beat * BEAT_U + clk_pos;
    mix_block(out, CTL);
    if (rec >= 0)
        for (k = 0; k < CTL * 2u && outn < 44100u * 2u; k++)
            outbuf[rec][outn++] = (int16_t)clamp(out[k], -32768, 32767);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && drums.v[k].note == LANE_NOTE[L]) {
                uint32_t s = (at + SLEN / 2u) / SLEN % 8u;
                const int8_t *o = dl_ofs_play(L);
                uint32_t v;
                for (v = 0; v < DE_N; v++)
                    heard[s][v] = o ? o[v] : 0;
                heard_n[s]++;
            }
}
static void run_block(void) { run_block_rec(-1); }
static void wait_step(track_t *t, uint32_t step)
{
    uint32_t g = 0;
    while (t->seq_abs != SEQ_NONE && t->seq_idx == step && g++ < 100000u)
        run_block();
    while (!(t->seq_abs != SEQ_NONE && t->seq_idx == step) && g++ < 100000u)
        run_block();
}
static void lane_on_all(uint32_t n)                   /* lane L hits on steps 0..n-1 */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dstep_set(&TDRUM->dstep[i], L, LV_NORM, 0);
}
static void heard_clear(void) { memset(heard, 0x55, sizeof heard), memset(heard_n, 0, sizeof heard_n); }

/* ---- the codec and the stored forms */
static project_t gp;
static dlrec_t gd;
static int lists_same(const auto_list_t *a)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (!auto_list_same(&a[k], AUTO_L(k)))
            return 0;
    return 1;
}
static void t_codec(void)
{
    static uint8_t b[AUTO_TRK_ENC_MAX];
    static auto_list_t a, c;
    uint32_t l, v, ok = 1, n;
    const uint8_t *p;
    for (l = 0; l < DRUM_LANES; l++)
        for (v = 0; v < DE_N; v++) {
            uint32_t id = AUTO_LANE(l, v);
            ok &= id == 0x7Cu + l * 8u + v && AUTO_IS_LANE(id) && AUTO_LANE_L(id) == l && AUTO_LANE_V(id) == v &&
                  id < AUTO_NUDGE && id >= MOT_TAIL_END && !AUTO_PSEUDO(id);
        }
    check("lane l's value v: 0x7C + (l << 3 | v), lane and value back, between the stored track ids and the pseudo-parameters",
          ok && AUTO_LANE(15, 7) == 0xFBu && !AUTO_IS_LANE(0x7B) && !AUTO_IS_LANE(0xFC) && !AUTO_IS_LANE(AUTO_NUDGE));
    a.n = 0;
    for (l = 0, ok = 1; l < DRUM_LANES; l++) {
        auto_ev_t e = {(uint8_t)(l * 4u), (uint8_t)AUTO_LANE(l, l & 7u), (int8_t)(l - 8)}, f = e;
        f.place |= AUTO_ONLY;
        ok &= auto_ev_ok(&e) && auto_ev_ok(&f);
        (void)auto_put(&a, e.place, e.param, e.value), (void)auto_put(&a, f.place, f.param, -e.value);
    }
    n = auto_enc_trk(&a, b);
    p = b;
    check("a list of 32 lane events (both kinds, every lane): well formed, stored and read back byte for byte",
          ok && n == 1u + 3u * 32u && auto_dec_trk(&c, &p, b + n) && p == b + n && !memcmp(&a, &c, 1u + 3u * a.n));
}
static void t_forms(void)
{
    static uint8_t b[SEC_REC_MAX], x[AX_ENC_MAX];
    static motion_store_t mo;
    static auto_list_t l2;
    static auto_store_t m2, want;
    stepx_t sx;
    auto_store_t *m;
    uint32_t nb, r, i;
    uint8_t on = 0;
    int32_t v;
    reset();
    lane_on_all(8);
    (void)motion_set_event(TDRUM, 2, P_REV, 30);       /* (a track hold and a track lock beside them) */
    (void)lock_set(TDRUM, 1, P_PAN, -10);
    (void)auto_put(AUTO_L(TRK_DRUM), 5, AUTO_LANE(L, DE_TUNE), 5);
    (void)auto_put(AUTO_L(TRK_DRUM), 3u | AUTO_ONLY, AUTO_LANE(L, DE_DECAY), -40);
    auto_w.on = 1u << TRK_DRUM;
    mo.psum = 0;
    (void)auto_to_motion(&mo, auto_w.l, auto_w.on);
    check("the MOTN form leaves the lane hold out (the track hold in); an older motion_valid takes it",
          mo.count == 1u && mo.ev[0].param == P_REV && motion_valid(&mo));
    r = auto_to_stepx(&sx, AUTO_L(TRK_DRUM));
    check("the stepx form: AUTO_X_LANE, the lane lock left out, the track lock in", (r & AUTO_X_LANE) && sx.lock[0].param == P_PAN &&
          !stepx_lock_used(&sx.lock[1]));
    proj_capture(&gp, &gd);
    m = auto_for(&gp, 1);
    check("a drum list with lane events: not in today's old forms (auto_trk_old_ok 0, auto_old_ok 0)",
          !auto_trk_old_ok(m, TRK_DRUM) && !auto_old_ok(m));
    nb = pat_encode(&gp, TRK_DRUM, b);
    check("... its pattern record is V2, read back equal", (b[0] & PF_VER) == PF_V2 && pat_decode(b, nb, TRK_DRUM, &gp.t[TRK_DRUM], &l2, &on) &&
          auto_list_same(AUTO_L(TRK_DRUM), &l2));
    nb = ax_encode(m, x);
    memset(&m2, 0, sizeof m2);
    check("... its extras record the new form (AUTO_ENC_TAG), read back equal", x[0] == AUTO_ENC_TAG && ax_decode(&m2, x, nb) &&
          auto_store_same(m, &m2));
    (void)auto_put(&m->l[TRK_DRUM], 6u | AUTO_ONLY, AUTO_LANE(L, DE_LEVEL), -3);   /* (a lane lock only, no lane hold) */
    (void)auto_find(&m->l[TRK_DRUM], 5, AUTO_LANE(L, DE_TUNE));
    for (i = 0; i < m->l[TRK_DRUM].n; i++)
        if (!(m->l[TRK_DRUM].ev[i].place & AUTO_ONLY) && AUTO_IS_LANE(m->l[TRK_DRUM].ev[i].param))
            auto_del(&m->l[TRK_DRUM], i--);
    check("a lane lock alone (no lane hold): still the new forms (AUTO_X_LANE)", !auto_trk_old_ok(m, TRK_DRUM));
    /* a scene saved and loaded, the autosave */
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    memset(pat_cur, PAT_NONE, NTRK);
    live_sec = -1;
    want = auto_w;
    project_save(1);
    clear_lists();
    project_load(1);
    check("a scene saved with lane events and loaded: every event", !strcmp(last_msg, "LOADED") && auto_store_same(&want, &auto_w));
    proj_capture(&gp, &gd);
    (void)st_save(OBJ_AUTOSAVE, &gp, sizeof gp);
    motion_flash_write(OBJ_AUTOSAVE, &gp);
    (void)sx_log_put(SX_ID_AUTO, gp.sum, &gp, 0);
    clear_lists();
    memset(auto_for(&gp, 1), 0, sizeof(auto_store_t));
    motion_flash_read(OBJ_AUTOSAVE, &gp);
    sx_log_get(SX_ID_AUTO, gp.sum, &gp);
    proj_apply(&gp, &gd, 1);
    check("the autosave (MOTN without them, the extras record with them) read back: every event", auto_store_same(&want, &auto_w) &&
          auto_get(AUTO_L(TRK_DRUM), 3u | AUTO_ONLY, AUTO_LANE(L, DE_DECAY), &v) && v == -40);
}

/* ---- playback: lane L on steps 0..7; DECAY lock -40 on step 4 (idx 3), TUNE hold +5 from step 6 (idx 5), a LEVEL
 * lock -6 and a LEVEL hold -12 both on step 7 (idx 6); the patch: DECAY +10 */
static void setup_play(void)
{
    reset();
    lane_on_all(8);
    dl.ofs[L][DE_DECAY] = 10;
    (void)auto_put(AUTO_L(TRK_DRUM), 3u | AUTO_ONLY, AUTO_LANE(L, DE_DECAY), -40);
    (void)auto_put(AUTO_L(TRK_DRUM), 5, AUTO_LANE(L, DE_TUNE), 5);
    (void)auto_put(AUTO_L(TRK_DRUM), 6u | AUTO_ONLY, AUTO_LANE(L, DE_LEVEL), -6);
    (void)auto_put(AUTO_L(TRK_DRUM), 6, AUTO_LANE(L, DE_LEVEL), -12);
    auto_w.on = 1u << TRK_DRUM;
}
static void t_play(void)
{
    static dlanes_t patch;
    uint32_t s, ok = 1, pl = 1;
    setup_play();
    patch = dl;
    heard_clear();
    transport_req = 1;
    while (clk_beat < 4u)                              /* (8 passes of 8 steps of a 16th: two beats a pass) */
        run_block();
    for (s = 0; s < 8u; s++)
        printf("lane: step %u heard TUNE %d DECAY %d LEVEL %d (%u hits)\n", (unsigned)(s + 1u), heard[s][DE_TUNE],
               heard[s][DE_DECAY], heard[s][DE_LEVEL], heard_n[s]);
#if DLA_ON
    for (s = 0; s < 8u; s++)
        ok &= heard[s][DE_DECAY] == (s == 3u ? -40 : 10) && heard[s][DE_TUNE] == (s >= 5u ? 5 : 0) &&
              heard[s][DE_LEVEL] == (s == 6u ? -6 : s == 7u ? -12 : 0) && heard_n[s] >= 1u;
    check("the hits: DECAY -40 on step 4 only (the lock), TUNE +5 from step 6 to the pass's end (the hold), LEVEL: the lock "
          "-6 beats the hold -12 on step 7, the hold after", ok);
    check("... the next pass from the patch: step 1's hit DECAY 10, TUNE 0 (each pass, hold events from the start)",
          heard[0][DE_TUNE] == 0 && heard[0][DE_DECAY] == 10);
#else
    for (s = 0; s < 8u; s++)
        ok &= heard[s][DE_DECAY] == 10 && heard[s][DE_TUNE] == 0 && heard[s][DE_LEVEL] == 0 && heard_n[s] >= 1u;
    check("old reader (DLA_ON 0): no lane event heard, every hit the patch's", ok);
#endif
    check("the patch dl.ofs never written (byte for byte)", !memcmp(&dl, &patch, sizeof dl));
#if DLA_ON
    wait_step(TDRUM, 6);
    pl &= dla.hm[L] != 0 && dla.lm[L] != 0;
    seq_stop();
    check("STOP: the lanes' holds and locks let go (the overlay empty), the patch heard", pl && !dla.hm[L] && !dla.lm[L] &&
          dl_ofs(L) == dl.ofs[L]);
    transport_req = 1;
    wait_step(TDRUM, 6);
    pl = dla.hm[L] != 0;
    seq_stop();
    transport_req = 1;
    run_block();
    check("PLAY: from the patch (no hold in force before step 6's)", pl && !dla.hm[L] && !dla.lm[L]);
    wait_step(TDRUM, 6);
    proj_capture(&gp, &gd);
    pat_take(TDRUM, TRK_DRUM, &gp);
    check("a pattern switch (pat.c pat_take): the holds let go", !dla.hm[L]);
    seq_stop();
#endif
    {   /* the render: equal to the one without the events until the lock's hit, then not */
        uint32_t i, first = 0xFFFFFFFFu, n0;
        for (i = 0; i < 2u; i++) {                     /* (each in a child of this state: the same start, FX tails and all) */
            pid_t pid = fork();
            if (!pid) {
                setup_play();
                if (i)
                    clear_lists();
                outn = 0;
                transport_req = 1;
                while (outn < 44100u * 2u)
                    run_block_rec((int)i);
                _exit(0);
            }
            waitpid(pid, 0, 0);
        }
        n0 = 3u * (SLEN / 120u) * 2u;                  /* (step 4 starts 3 steps in: SLEN / BPM samples a step) */
        for (i = 0; i < 44100u * 2u && first == 0xFFFFFFFFu; i++)
            if (outbuf[0][i] != outbuf[1][i])
                first = i;
        printf("lane: the renders part at sample %u (step 4 at %u)\n", (unsigned)(first / 2u), (unsigned)(n0 / 2u));
#if DLA_ON
        check("the render with them = the one without, sample for sample, until step 4's hit (the lock); then not, within step 4",
              first != 0xFFFFFFFFu && first >= n0 && first < n0 + (SLEN / 120u) * 2u);
#else
        check("old reader: the render with them = the one without, sample for sample", first == 0xFFFFFFFFu);
#endif
    }
}
/* the track's own events among lane events: played as before (the old reader too) */
static void t_others(void)
{
    int16_t pan;
    setup_play();
    TDRUM->p[P_PAN] = 0;
    (void)lock_set(TDRUM, 2, P_PAN, -20);
    (void)motion_set_event(TDRUM, 4, P_REV, 33);
    transport_req = 1;
    wait_step(TDRUM, 2);
    pan = TDRUM->p[P_PAN];
    wait_step(TDRUM, 4);
    check("the drum track's own lock (PAN -20) and hold (REV 33) among lane events: played", pan == -20 && TDRUM->p[P_REV] == 33);
    seq_stop();
}
static void t_fill(void)
{
    reset();
    lane_on_all(8);
    (void)step_fill_set(TDRUM, 2, FC_FILL);
    (void)auto_put(AUTO_L(TRK_DRUM), 2, AUTO_LANE(L, DE_CUT), -30);
    (void)auto_put(AUTO_L(TRK_DRUM), 2u | AUTO_ONLY, AUTO_LANE(L, DE_SNAP), 20);
    auto_w.on = 1u << TRK_DRUM;
    transport_req = 1;
    wait_step(TDRUM, 2);
#if DLA_ON
    check("a fill-skipped step: its lane hold applies (CUT -30), its lane lock does not (SNAP)",
          (dla.hm[L] >> DE_CUT & 1u) && dla.hv[L][DE_CUT] == -30 && !dla.lm[L]);
    fill_held = 1;
    wait_step(TDRUM, 2);
    check("... in a fill: its lane lock too (SNAP 20)", (dla.lm[L] >> DE_SNAP & 1u) && dla.lv[L][DE_SNAP] == 20);
    fill_held = 0;
#endif
    seq_stop();
}

#if DLA_ON
/* ---- the UIs' calls */
static void t_api(void)
{
    track_t *t = TDRUM;
    int32_t v;
    reset();
    dstep_set(&t->dstep[2], L, LV_NORM, 0);
    check("lane_lock_set on a step without the lane's hit: LANE_NOHIT, nothing written (D4)",
          lane_lock_set(t, 3, L, DE_DECAY, -20) == LANE_NOHIT && !AUTO_L(TRK_DRUM)->n);
    check("... with the hit: set, clamped to the value's range (LEVEL -24..6: 40 -> 6)", lane_lock_set(t, 2, L, DE_LEVEL, 40) == 1 &&
          lane_lock_get(t, 2, L, DE_LEVEL, &v) && v == 6);
    check("... not on a part, not past LEVEL", !lane_lock_set(&trk[0], 2, L, DE_DECAY, 1) && !lane_lock_set(t, 2, L, DE_N, 1));
    (void)lane_lock_set(t, 2, L, DE_DECAY, -20);
    (void)auto_put(AUTO_L(TRK_DRUM), 2u | AUTO_ONLY, AUTO_LANE(L + 1u, DE_DECAY), 9);   /* (another lane's lock) */
    (void)auto_put(AUTO_L(TRK_DRUM), 2, AUTO_LANE(L, DE_TUNE), 3);                     /* (a hold of lane L) */
    check("the marks: lane L's values with an event on the step (locks: DECAY LEVEL; holds: TUNE)",
          lane_step_values(t, 2, L, AUTO_ONLYS) == (1u << DE_DECAY | 1u << DE_LEVEL) && lane_step_values(t, 2, L, AUTO_HOLDS) == 1u << DE_TUNE);
    check("the lane's hit removed: lane_step_drop drops its locks there (2), not its hold nor another lane's lock",
          lane_step_drop(t, 2, L) == 2u && AUTO_L(TRK_DRUM)->n == 2u && lane_hold_get(t, 2, L, DE_TUNE, &v) && v == 3);
    check("YES: the hold becomes a lock (the lane hits there), its value kept", lane_kind_toggle(t, 2, L, DE_TUNE) == 2u &&
          lane_lock_get(t, 2, L, DE_TUNE, &v) && v == 3 && !lane_hold_get(t, 2, L, DE_TUNE, &v));
    check("... and back: a hold, PLAY on", lane_kind_toggle(t, 2, L, DE_TUNE) == 1u && lane_hold_get(t, 2, L, DE_TUNE, &v) &&
          ((auto_w.on >> TRK_DRUM) & 1u));
    (void)auto_put(AUTO_L(TRK_DRUM), 4, AUTO_LANE(L, DE_CUT), 7);
    check("a hold where the lane has no hit: stays a hold (a lock there would not be heard)", lane_kind_toggle(t, 4, L, DE_CUT) == 0u);
    check("lane_hold_drop / lane_lock_drop", lane_hold_drop(t, 4, L, DE_CUT) && !lane_hold_get(t, 4, L, DE_CUT, &v));
    /* the knob: not recording, recording */
    clear_lists();
    lane_on_all(8);
    dla.hm[L] = 1u << DE_CUT, dla.hv[L][DE_CUT] = -9;
    check("lane_knob not recording: 0 (the caller sets the patch), the hold in force on that value lets go",
          lane_knob(L, DE_CUT, 12) == 0 && !dla.hm[L] && !AUTO_L(TRK_DRUM)->n);
    song.rec = 1u << TRK_DRUM;
    transport_req = 1;
    wait_step(TDRUM, 2);
    check("lane_knob recording: a hold at the step playing, heard at once (the overlay), PLAY on, dl.ofs untouched",
          lane_knob(L, DE_CUT, -50) == 1 && lane_hold_get(t, 2, L, DE_CUT, &v) && v == -50 && (dla.hm[L] >> DE_CUT & 1u) &&
          dla.hv[L][DE_CUT] == -50 && dl.ofs[L][DE_CUT] == 0 && ((auto_w.on >> TRK_DRUM) & 1u));
    check("... clamped (DECAY +100 -> 63); lane_motion_drives marks it", lane_knob(L, DE_DECAY, 100) == 1 &&
          lane_hold_get(t, 2, L, DE_DECAY, &v) && v == 63 && lane_motion_drives(L, DE_DECAY) && !lane_motion_drives(L, DE_SNAP));
    check("lane_heard: what the next hit plays (the hold's -50)", lane_heard(L, DE_CUT) == -50);
    song.rec = 0;
    seq_stop();
}
#endif

/* ---- the editor, 2.4, MISS */
static int ed_call(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    ed_n = 0;
    return ed_stepx(cmd, a, na);
}
static void t_editor(void)
{
    uint8_t a[8];
    uint32_t sid = AUTO_LANE(L, DE_DECAY);
    int32_t v;
    reset();
    a[0] = TRK_DRUM, a[1] = 0, a[2] = 3 | AUTO_ONLY, a[3] = (uint8_t)(sid & 127u), a[4] = (uint8_t)(sid >> 7);
    a[5] = (uint8_t)((-100 + 8192) & 127), a[6] = (uint8_t)((-100 + 8192) >> 7);
#if DLA_ON
    check("AUTO_SET a lane lock on the drum track (step 4, DECAY -100): taken, clamped to -64 (the value's range)",
          ed_call(ED_AUTO_SET, a, 7) && ed_out[2] == 0 && auto_get(AUTO_L(TRK_DRUM), 3u | AUTO_ONLY, sid, &v) && v == -64 &&
          ed_rv(ed_out + 5) == -64);
    a[0] = TRK_DRUM;
    check("AUTO_GET: the lane event as stored (place, the id in 2 x 7 bit, the value)", ed_call(ED_AUTO_GET, a, 1) && ed_out[1] == 1 &&
          ed_out[5] == 1 && ed_out[6] == (3 | AUTO_ONLY) && ed_out[7] == (sid & 127u) && ed_out[8] == (sid >> 7) &&
          ed_rv(ed_out + 9) == -64);
    a[0] = 0;
    check("AUTO_SET a lane id on a part: refused (rc 1)", ed_call(ED_AUTO_SET, a, 7) && ed_out[2] == 1 && !AUTO_L(0)->n);
#else
    check("old reader: AUTO_SET of a lane id refused (rc 1)", ed_call(ED_AUTO_SET, a, 7) && ed_out[2] == 1);
#endif
}
static uint32_t get78(uint32_t flag, int withflag, uint32_t *w2)
{
    uint8_t a[5] = {0, 0, 0, 0, (uint8_t)flag};
    ed_n = 0;
    (void)ed_sl24(78, a, withflag ? 5u : 4u);
    *w2 = withflag && ed_n >= 17u ? ed_g7(ed_out + ed_n - 2u, 2) : 0u;
    return ed_n >= 15u ? ed_g7(ed_out + 10, 2) : 0xFFFFu;
}
static void t_sl24(void)
{
    stepx_t x;
    uint32_t w1, w2, lost;
    reset();
    lane_on_all(8);
    (void)lock_set(TDRUM, 1, P_PAN, -10);
    (void)auto_put(AUTO_L(TRK_DRUM), 3u | AUTO_ONLY, AUTO_LANE(L, DE_DECAY), -40);
    lost = sl24_auto_out(&x, AUTO_L(TRK_DRUM));
    check("2.4 export: lane events left out, lost SX24_LAUTO (not LOCK, not MOTION); the track lock exported",
          lost == SX24_LAUTO && x.lock[0].param == P_PAN && !stepx_lock_used(&x.lock[1]));
    w1 = get78(1, 1, &w2);
    check("SL24_GET with the flag: lost2 bit 2 (lauto), bit 13 clear", w2 == 4u && !(w1 & SX24_MOTION));
    w1 = get78(0, 0, &w2);
    check("... a v11 request: on bit 13 (its motion)", (w1 & SX24_MOTION) != 0u);
}
static void t_miss(void)
{
    uint32_t i, n, has = 0;
    reset();
    (void)auto_put(AUTO_L(TRK_DRUM), 3u | AUTO_ONLY, AUTO_LANE(L, DE_DECAY), -40);
    n = miss_scan();
    for (i = 0; i < n; i++)
        has |= miss_m[i] == MS_ITEM(MS_FX, TRK_DRUM, MF_LAUTO);
#if DLA_ON
    check("MISS: a build that plays lane events lists nothing for them", !has);
#else
    check("old reader: MISS lists LANE MOTION", has);
#endif
}

int main(void)
{
    outbuf = mmap(0, 2u * sizeof *outbuf, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    (void)auto_for(&proj_tmp.cur, 1), (void)auto_for(&sec_stage_p, 1);
    auto_init();
    printf("lane: DLA_ON %d\n", (int)DLA_ON);
    t_codec();
    t_forms();
    t_play();
    t_others();
    t_fill();
#if DLA_ON
    t_api();
#endif
    t_editor();
    t_sl24();
    t_miss();
    printf("lane test %s (%d failed)\n", bad ? "FAILED" : "passed", bad);
    return bad;
}
