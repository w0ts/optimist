/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the editor's STEPLANE (cmd 105, firmware/src/io/editor/ed_steplane.c) on the Optimist UI: the list of lockable values IS the
 * device's (op_step.c hp_locks / lock_param with the track and lane set as the held step has them), the call leaves the device's own track, lane and
 * cursor as it found them, a lane's lock needs the lane's hit, the lane's values are TUNE .. LEVEL. EDSTL_GOLDEN=1 (the variant of tests/run_tests.sh with
 * the automation store whole) also checks tests/steplane_table.json, the web editor's mock data (STEPLANE_WRITE=1 rewrites it). Included after
 * ui_optimist_edscrs.h (its ed_* helpers). */
#include "../firmware/src/io/editor/ed_out.h"
static struct { uint32_t last_ms; uint32_t st[NSTEP]; } ed_w;                 /* (editor.c's; the parts the files below read) */
static uint32_t ed_step_sig(const track_t *t, uint32_t i) { (void)t; return i; }
static void ed9_known_step(uint32_t t, uint32_t i) { (void)t, (void)i; }
#include "../firmware/src/io/editor/ed_undo.c"
#include "../firmware/src/io/editor/ed_steplane.c"
#if STL_ON && FELUCCA_PLOCK
static uint32_t edstl_call(uint32_t op, uint32_t track, uint32_t lane, uint32_t step, uint32_t x, int32_t v)
{
    uint8_t q[8];
    uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192);
    q[0] = (uint8_t)op, q[1] = (uint8_t)track, q[2] = (uint8_t)lane, q[3] = (uint8_t)step, q[4] = (uint8_t)x, q[5] = (uint8_t)(u & 127u), q[6] = (uint8_t)(u >> 7);
    ed_begin(ED_STEPLANE);
    return (uint32_t)ed_steplane(ED_STEPLANE, q, op == 0u ? 3u : op <= 2u ? 4u : op >= 5u && op <= 6u ? 5u : 7u);
}
/* the device's own list for (track, lane): the held step's context set, hp_locks' pages, lock_param's cells (kind, id) in order */
static uint32_t edstl_expect(uint32_t track, uint32_t lane, uint8_t (*kid)[2])
{
    uint8_t ix[OP_MAXROWS], sel = (uint8_t)song.sel, ul = ui.lane, ls = lane_sel;
    uint32_t p, k, np, n = 0, i;
    song.sel = (uint8_t)track;
    ui.lane = lane < 17u && is_drum(&trk[track]);
    if (ui.lane)
        lane_sel = (uint8_t)lane;
    np = hp_locks(ix);
    for (p = 0; p < np; p++)
        for (k = 0; k < 4u; k++) {
            const param_desc_t *d;
            int32_t id = lock_param(&PAGES[ix[p]], k, &d);
            uint8_t kd, v;
            if (id < 0 || (!ui.lane && ((uint32_t)id & LOCK_LANE)))
                continue;
            kd = ((uint32_t)id & LOCK_LANE) ? 1u : 0u, v = (uint8_t)(kd ? (uint32_t)id & 7u : (uint32_t)id);
            for (i = 0; i < n && !(kid[i][0] == kd && kid[i][1] == v); i++)
                ;
            if (i == n && n < 127u)
                kid[n][0] = kd, kid[n][1] = v, n++;
        }
    song.sel = sel, ui.lane = ul, lane_sel = ls;
    return n;
}
static uint32_t edstl_check_list(uint32_t track, uint32_t lane)
{
    uint8_t kid[128][2];
    uint32_t n = edstl_expect(track, lane, kid), i, ok, sel = song.sel, ul = ui.lane, ls = lane_sel, kinds = 0;
    ok = edstl_call(0, track, lane, 0, 0, 0) && ed_out[8] == n;
    for (i = 0; i < n; i++) {
        ok &= edstl_call(1, track, lane, i, 0, 0) && ed_out[8] == i && ed_out[9] == kid[i][0] && ed_out[10] == kid[i][1];
        kinds |= 1u << kid[i][0];
    }
    ok &= song.sel == sel && ui.lane == ul && lane_sel == ls;
    return ok ? 1u + (kinds << 1) + (n << 8) : 0u;
}
#ifdef EDSTL_GOLDEN
static void edstl_json_desc(FILE *f, const param_desc_t *d)
{
    uint32_t i;
    fprintf(f, "{\"label\":\"%s\",\"fmt\":%u,\"min\":%d,\"max\":%d,\"def\":%d,\"unit\":\"%s\"", d->label, (unsigned)d->fmt, d->min, d->max, d->def, d->unit ? d->unit : "");
    if (d->fmt == F_ENUM && d->names) {
        fprintf(f, ",\"names\":[");
        for (i = 0; i <= (uint32_t)(d->max - d->min) && i < 64u; i++)
            fprintf(f, "%s\"%s\"", i ? "," : "", d->names[i]);
        fprintf(f, "]");
    }
    fprintf(f, "}");
}
static void edstl_golden(void)
{
    static const uint8_t CX[][2] = {{0, 127}, {1, 127}, {3, 127}, {3, 0}, {3, 2}, {3, DRUM_ACC}};
    const char *gold = "tests/steplane_table.json";
    FILE *f = tmpfile();
    char *a = malloc(1u << 20), *b = malloc(1u << 20);
    long na, nb = -1;
    uint32_t x, i, n, np;
    fm1_irq_off();
    set_engine_of(&trk[0], 1);                          /* (T1 as the table was made: engine 1, its first preset; not
                                                         * whatever an earlier test left) */
    apply_preset_to(&trk[0], 0);
    fm1_irq_on();
    {                                                   /* (and the inserts as then: T2 DIST + COMP, DR - + DUST) */
        uint8_t v2[INS_N] = {FXT_DIST, FXT_COMP}, vd[INS_N] = {FXT_NONE, FXT_DUST};
        ins_put(1, v2);
        ins_put(TRK_DRUM, vd);
    }
    frames(2);
    fprintf(f, "{\"ctx\": [\n");
    for (x = 0; x < sizeof CX / sizeof CX[0]; x++) {
        uint32_t track = CX[x][0], lane = CX[x][1];
        if (lane == DRUM_ACC && !FELUCCA_DRUM_ACCENT)
            continue;
        edstl_call(0, track, lane, 0, 0, 0);
        n = ed_out[8], np = ed_out[9];
        fprintf(f, "%s {\"t\":%u,\"l\":%u,\"np\":%u,\"e\":[\n", x ? ",\n" : "", track, lane, np);
        for (i = 0; i < n; i++) {
            const param_desc_t *d;
            stl_e e;
            edstl_call(1, track, lane, i, 0, 0);
            (void)stl_walk(track, lane, i, &e, 0, 0);
            d = e.d;
            fprintf(f, "%s  {\"kind\":%u,\"id\":%u,\"page\":%u,\"cell\":%u,\"title\":\"%s\",\"d\":", i ? ",\n" : "", e.kind, e.id, e.page, e.cell, e.title);
            edstl_json_desc(f, d);
            fprintf(f, "}");
        }
        fprintf(f, "\n ]}");
    }
    fprintf(f, "\n]}\n");
    fflush(f);
    na = ftell(f);
    rewind(f);
    na = (long)fread(a, 1, (size_t)na, f);
    fclose(f);
    if (getenv("STEPLANE_WRITE") && (f = fopen(gold, "w"))) {
        fwrite(a, 1, (size_t)na, f);
        fclose(f);
    }
    if ((f = fopen(gold, "r"))) {
        nb = (long)fread(b, 1, 1u << 20, f);
        fclose(f);
    }
    check(nb == na && !memcmp(a, b, (size_t)na), "STEPLANE: the lists equal tests/steplane_table.json (STEPLANE_WRITE=1 rewrites it; the web editor's mock data)");
    free(a), free(b);
}
#endif
#endif
static void edv14_tests(void)
{
#if FELUCCA_ED_STEPLANE && STL_ON && FELUCCA_PLOCK
    uint32_t r;
    int32_t v;
    reset_ui();
    song.sel = 0;
    r = edstl_check_list(0, 127);
    check(r && (r >> 8) > 0 && !(r & 4u), "STEPLANE LIST (synth track): equal to the Optimist UI's lock pages (hp_locks / lock_param), parameters only, the device's cursor not moved");
    r = edstl_check_list(1, 127);
    check(r && (r >> 8) > 0, "STEPLANE LIST (track 2): the same rule");
    r = edstl_check_list(3, 127);
    check(r && !(r & 4u), "STEPLANE LIST (DR itself, lane 127): the drum track's own, no lane values");
#if DLA_ON
    r = edstl_check_list(3, 2);
    check(r && (r & 4u), "STEPLANE LIST (lane 2): the lane pages' values, lane values among them (kind 1)");
    {
        uint32_t n = 0, i, lane_ok = 1;
        edstl_call(0, 3, 2, 0, 0, 0);
        n = ed_out[8];
        for (i = 0; i < n; i++) {
            edstl_call(1, 3, 2, i, 0, 0);
            if (ed_out[9] == 1u)
                lane_ok &= ed_out[10] < DE_N;
        }
        check(lane_ok, "STEPLANE DESC (lane 2): a lane value's id is a DE_ index (TUNE .. LEVEL)");
    }
    dstep_set(&TDRUM->dstep[4], 2, LV_NORM, 0);
    edstl_call(0, 3, 2, 0, 0, 0);
    {
        uint32_t n = ed_out[8], i, found = 0;
        for (i = 0; i < n && !found; i++) {
            edstl_call(1, 3, 2, i, 0, 0);
            if (ed_out[9] == 1u && ed_out[10] == DE_DECAY) {
                found = 1;
                edstl_call(4, 3, 2, 4, i, 7);
                check(ed_out[6] == 0 && lane_lock_get(TDRUM, 4, 2, DE_DECAY, &v) && v == 7 && ed_out[17] == 1, "STEPLANE LOCK (lane 2, DECAY): the lane lock on the hit, as the device writes it");
                edstl_call(4, 3, 2, 5, i, 7);
                check(ed_out[6] == 3 && !lane_lock_get(TDRUM, 5, 2, DE_DECAY, &v), "STEPLANE LOCK on a step the lane does not hit: rc 3 (D4)");
                edstl_call(5, 3, 2, 4, i, 0);
                check(ed_out[6] == 0 && !lane_lock_get(TDRUM, 4, 2, DE_DECAY, &v) && ed_out[17] == 0, "STEPLANE UNLOCK (lane 2, DECAY)");
            }
        }
        check(found, "STEPLANE LIST (lane 2): DECAY is offered");
    }
    memset(&TDRUM->dstep[4], 0, sizeof TDRUM->dstep[4]);
#endif
    check(!edstl_call(0, 4, 127, 0, 0, 0) && !edstl_call(0, 0, 5, 0, 0, 0), "STEPLANE: a track past 3, a lane on a synth track: no reply");
    ed_begin(1); ed_steplane_info();
    check(ed_n == 10u && ed_out[5] == 0x62u && (ed_out[8] & 1u) && (ed_out[8] & 2u), "ED_STEPLANE on the Optimist UI: INFO carries tag 0x62 with the UI-pages flag and locks built");
#ifdef EDSTL_GOLDEN
    edstl_golden();
#endif
    reset_ui();
#endif
}
