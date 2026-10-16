/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the editor's SCRS (cmd 99, firmware/src/io/editor/ed_scrs.c): the device's screens as rows of cards. Checked in every
 * variant: the rows and names are the screens' own, every card is what SCREENS[].cell says, a turn and a YES go through the screen's own functions,
 * the call leaves the device's own cursor, track, lane and page where it found them, a bad screen / track / row gets no reply.
 * EDSCRS_DUMP=1 prints every row of every screen (a review aid). Included after ui_optimist_edscrn.h (its ed_* helpers). */
#include "../firmware/src/io/editor/ed_out.h"
#include "../firmware/src/io/editor/ed_scrs.c"
#include "../firmware/src/io/editor/ed_scope.c"
typedef struct {
    uint32_t fam, rflags, kind[4], state[4], danger[4], gk[4], byd[4], fmt[4], flags[4];
    int32_t gmin[4], gmax[4], gv[4], dmin[4], dmax[4], ddef[4], val[4];
    char name[16], label[4][16], text[4][16], unit[4][10], obj[4][16], ask[28];
} scr_row_t;
static uint32_t scs_str(const uint8_t *a, uint32_t p, char *o, uint32_t n)
{
    uint32_t i = 0;
    while (a[p] && i + 1u < n)
        o[i++] = (char)a[p++];
    o[i] = 0;
    while (a[p])
        p++;
    return p + 1u;
}
static uint32_t scs_v(const uint8_t *a, uint32_t p, int32_t *o) { *o = ed_rv(a + p); return p + 2u; }
static void scs_parse(const uint8_t *a, uint32_t p, scr_row_t *o)   /* a ROW after its header (p: the family byte) */
{
    uint32_t k;
    memset(o, 0, sizeof *o);
    o->fam = a[p++];
    o->rflags = a[p++];
    p = scs_str(a, p, o->name, sizeof o->name);
    for (k = 0; k < 4u; k++) {
        o->kind[k] = a[p++];
        p = scs_str(a, p, o->label[k], 16);
        p = scs_str(a, p, o->text[k], 16);
        p = scs_str(a, p, o->unit[k], 10);
        o->state[k] = a[p++], o->danger[k] = a[p++], o->gk[k] = a[p++];
        p = scs_v(a, p, &o->gmin[k]), p = scs_v(a, p, &o->gmax[k]), p = scs_v(a, p, &o->gv[k]);
        o->byd[k] = a[p++];
        if (o->byd[k]) {
            o->fmt[k] = a[p++];
            p = scs_v(a, p, &o->dmin[k]), p = scs_v(a, p, &o->dmax[k]), p = scs_v(a, p, &o->ddef[k]), p = scs_v(a, p, &o->val[k]);
        }
        o->flags[k] = a[p++];
        p = scs_str(a, p, o->obj[k], 16);
    }
    scs_str(a, p, o->ask, sizeof o->ask);
}
static uint32_t scs_call(uint32_t op, uint32_t s, uint32_t t, uint32_t l, uint32_t f, uint32_t r, uint32_t k, uint32_t mode, int32_t v)
{
    uint8_t q[12];
    uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192), na = op == 0u ? 5u : op == 1u ? 6u : op == 3u ? 8u : 10u;
    q[0] = (uint8_t)op, q[1] = (uint8_t)s, q[2] = (uint8_t)t, q[3] = (uint8_t)l, q[4] = (uint8_t)f, q[5] = (uint8_t)r, q[6] = (uint8_t)k;
    q[7] = (uint8_t)mode, q[8] = (uint8_t)(u & 127u), q[9] = (uint8_t)(u >> 7);
    ed_begin(ED_SCRS);
    return (uint32_t)ed_scrs(ED_SCRS, q, na);
}
static const uint8_t SCS_SCR[] = {SCR_HOME, SCR_SOUND, SCR_FX, SCR_PROJECT, SCR_STEP, SCR_SONG, SCR_TEMPO, SCR_SCOPE};
static void edscrs_dump(uint32_t s, uint32_t t, uint32_t l, uint32_t f)
{
    uint32_t r, k, n;
    scr_row_t w;
    if (!scs_call(0, s, t, l, f, 0, 0, 0, 0))
        return;
    n = ed_out[10];
    printf("== %s trk %u lane %u fam %u: %u rows\n", SCR_NAME[s], t, l, f, n);
    for (r = 0; r < n; r++) {
        if (!scs_call(1, s, t, l, f, r, 0, 0, 0)) {
            printf("  row %u: NO REPLY\n", r);
            continue;
        }
        scs_parse(ed_out, 8, &w);
        printf("  %2u [%3u] %-12s", r, w.fam, w.name);
        for (k = 0; k < 4u; k++)
            printf(" | %u %s=%s%s%s", w.kind[k], w.label[k], w.text[k], w.unit[k], w.byd[k] ? "*" : "");
        printf("\n");
    }
}
static uint32_t hpn(void) { uint16_t h = st.held; uint32_t n; st.held = 1; n = hp_count(); st.held = h; return n; }
static void edscrs_tests(void)
{
    uint32_t i, s, t, ok = 1, n = 0;
    scr_row_t w;
    reset_ui();
    song.sel = 0;
#if FELUCCA_ED_SCRS
    for (i = 0; i < sizeof SCS_SCR; i++) {
        s = SCS_SCR[i];
        ok &= scs_call(0, s, 0, 127, 127, 0, 0, 0, 0) && ed_out[5] == 0u && ed_out[6] == s && ed_out[10] == (uint8_t)scrs_n(s);
    }
    check(ok, "SCRS op 0: every screen's row count is the screen's own");
    ok = 1;
    for (i = 0; i < sizeof SCS_SCR; i++)
        for (t = 0; t < NTRK; t++) {
            uint32_t r, rows;
            s = SCS_SCR[i];
            ui.scr = (uint8_t)s, song.sel = (uint8_t)t, ui.lane = 0, snd_fam = SND_ALL;
            rows = scrs_n(s);
            for (r = 0; r < rows; r++) {
                cell_t c;
                char nm[16];
                uint32_t rr = scrs_map(s, r);
                ui.row[s] = (uint8_t)rr;
                ok &= scs_call(1, s, t, 127, 127, r, 0, 0, 0) && ed_out[5] == 1u;
                scs_parse(ed_out, 8, &w);
                memset(&c, 0, sizeof c);
                ui.scr = (uint8_t)s, song.sel = (uint8_t)t, ui.lane = 0, snd_fam = SND_ALL;
                SCREENS[s].cell(rr, 0, &c);
                nm[0] = 0;
                if (s == SCR_SOUND)
                    snd_title_row(rr, nm);
                else if (s == SCR_HOME)
                    snprintf(nm, sizeof nm, "%s", mx_code(rr));   /* (the row's code, then its set: "T1 MIX") */
                else
                    SCREENS[s].name(rr, nm);
                ok &= ed_n - 5u <= ED_PAYLOAD_N;             /* (a row fits the device's reply buffer) */
                ok &= !strncmp(w.name, nm, s == SCR_HOME ? strlen(nm) : 12) && w.kind[0] == c.kind && !strcmp(w.label[0], c.label ? c.label : "") && !strncmp(w.text[0], c.val, 14);
                n++;
            }
        }
    song.sel = 0;
    reset_ui();
    check(ok && n > 20u, "SCRS op 1: every row of every screen on every track: its name and first card are what the screen's functions give");
    {   /* a turn: ENV ATK (SOUND, T1, row 0, card 0) two units up through the screen's own turn; mode 2 its default; a card that is not a value: nothing */
        cell_t c;
        int32_t v0;
        ui.scr = SCR_SOUND, song.sel = 0, ui.lane = 0, snd_fam = SND_ALL, ui.row[SCR_SOUND] = 0;
        memset(&c, 0, sizeof c);
        SCREENS[SCR_SOUND].rows();                      /* (the rows first, as the device's frame does: the cells read what it laid out) */
        SCREENS[SCR_SOUND].cell(0, 0, &c);
        v0 = *c.vp;
        ok = c.kind == CK_VAL && c.d && scs_call(2, SCR_SOUND, 0, 127, 127, 0, 0, 1, 2) && ed_out[5] == 2u;
        scs_parse(ed_out, 8, &w);
        ok &= *c.vp == clamp(v0 + 2, c.d->min, c.d->max) && w.byd[0] && w.val[0] == *c.vp && !strcmp(w.label[0], c.label);
        ok &= scs_call(2, SCR_SOUND, 0, 127, 127, 0, 0, 2, 0) && *c.vp == c.d->def;
        *c.vp = (int16_t)v0;
        check(ok, "SCRS op 2: a fine turn moves a value by units through the screen's own turn, the reply is the row with the value; mode 2 the default");
    }
    {   /* YES on an action that destroys (PROJECT > TOOLS > CLRSQ) asks first: the card says so, the question is the device's, nothing is done */
        uint32_t r, row = 0xFFu;
        ui.scr = SCR_PROJECT, ui.hot = 0;
        for (r = 0; r < SCREENS[SCR_PROJECT].rows(); r++) {
            cell_t c;
            memset(&c, 0, sizeof c);
            SCREENS[SCR_PROJECT].cell(r, 0, &c);
            if (c.kind == CK_ACT && c.label && !strcmp(c.label, "CLRSQ"))
                row = r;
        }
        ok = row != 0xFFu && scs_call(3, SCR_PROJECT, 0, 127, 127, row, 0, 0, 0) && ed_out[5] == 3u && ed_out[8] == 1u;
        scs_parse(ed_out, 9, &w);
        ok &= (w.flags[0] & 32u) && w.ask[0] && op_armed();
        op_disarm();
        check(ok, "SCRS op 3: YES on PROJECT > TOOLS > CLRSQ (ok 0) asks the device's question (its text in the row) and does nothing");
    }
    {   /* the call puts the device's own screen, track, lane, page family and cursor back */
        ui.scr = SCR_SONG, song.sel = 2, ui.lane = 1, lane_sel = 5, snd_fam = FAM_ENV, ui.row[SCR_SOUND] = 3, ui.row[SCR_FX] = 1, ui.hot = 2, ui.master = 0;
        scs_call(1, SCR_SOUND, 3, 7, 127, 2, 0, 0, 0);
        scs_call(1, SCR_FX, 0, 127, 127, 1, 0, 0, 0);
        scs_call(3, SCR_PROJECT, 1, 127, 127, 3, 0, 0, 0);          /* (TOOLS > CLRSQ asks: nothing done) */
        ok = ui.scr == SCR_SONG && song.sel == 2u && ui.lane == 1u && lane_sel == 5u && snd_fam == FAM_ENV && ui.row[SCR_SOUND] == 3u && ui.row[SCR_FX] == 1u && ui.hot == 2u && !ui.master;
        op_disarm();
        song.sel = 0, lane_sel = 0;
        reset_ui();
        check(ok, "SCRS: the device's own screen, track, lane, page and cursor are where they were after the calls");
    }
#if OP_FM6
    {   /* an FM6 track: fam 126 is the operator editor's rows (OPERATOR, the six pages of the operator picked, PIT, ALGO LFO PORTA STORE); OP turns the operator the pages edit */
        uint32_t e = fm6_slot(), keep = trk[0].eng_req;
        if (e < NENGINES) {
            reset_ui();
            song.sel = 0;
            set_engine_of(&trk[0], e);
            frames(2);
            ok = scs_call(0, SCR_SOUND, 0, 127, SCRS_FAM_FM6, 0, 0, 0, 0) && ed_out[10] == F6_ROWS;
            ok &= scs_call(1, SCR_SOUND, 0, 127, SCRS_FAM_FM6, 0, 0, 0, 0);
            scs_parse(ed_out, 8, &w);
            ok &= w.fam == 127u && !strcmp(w.name, "OPERATOR") && !strcmp(w.label[0], "OP");
            ok &= scs_call(2, SCR_SOUND, 0, 127, SCRS_FAM_FM6, 0, 0, 1, 2) && f6.target == 1u;
            ok &= scs_call(1, SCR_SOUND, 0, 127, SCRS_FAM_FM6, 1, 0, 0, 0);
            scs_parse(ed_out, 8, &w);
            ok &= !strcmp(w.name, "OP2 FREQ");
            f6.target = 0;
            set_engine_of(&trk[0], keep);
            song.sel = 0;
            reset_ui();
            check(ok, "SCRS: an FM6 track, fam 126: the operator editor's rows; OP picks the operator the pages edit (OP2 FREQ)");
        }
    }
#endif
    {   /* STEPS with a step held (fam 0..63): the held step's card pages are the rows: STEP (NOTE LEVEL RATCHET LENGTH), EXTRA, the p-lock pages; a card turned edits the step */
        track_t *t = &trk[0];
        step_t keep = t->step[2];
        uint32_t lvl0;
        reset_ui();
        song.sel = 0;
        t->step[2].time = ST_NOTE, t->step[2].n = 1, t->step[2].note[0] = 60, t->step[2].vel = 100, t->step[2].lvl = LV_NORM, t->step[2].rat = 0;
        ok = scs_call(0, SCR_STEP, 0, 127, 2, 0, 0, 0, 0) && ed_out[10] >= 1u && ed_out[10] == hpn();
        ok &= scs_call(1, SCR_STEP, 0, 127, 2, 0, 0, 0, 0);
        scs_parse(ed_out, 8, &w);
        ok &= !strcmp(w.name, "STEP") && !strcmp(w.label[0], "NOTE") && !strcmp(w.text[0], "C4") && !strcmp(w.label[1], "LEVEL") && !strcmp(w.text[1], "NORM") && !strcmp(w.label[3], "LENGTH");
        lvl0 = t->step[2].lvl;
        ok &= scs_call(2, SCR_STEP, 0, 127, 2, 0, 1, 1, 1);
        scs_parse(ed_out, 8, &w);
        ok &= t->step[2].lvl != lvl0 && !st.held && st.hp == 0u;
        if (hpn() > 1u) {                                       /* (a build with EXTRA or the p-locks: the next page) */
            ok &= scs_call(1, SCR_STEP, 0, 127, 2, 1, 0, 0, 0);
            scs_parse(ed_out, 8, &w);
            ok &= !strcmp(w.name, EX_N ? "EXTRA" : w.name) && w.name[0];
        }
        ok &= !scs_call(0, SCR_STEP, 0, 127, 63, 0, 0, 0, 0);      /* (past the pattern's length: no reply) */
        t->step[2] = keep;
        reset_ui();
        check(ok, "SCRS: STEPS with a step held (fam = the step): its card pages as rows (STEP: NOTE LEVEL RATCHET LENGTH, EXTRA, the lock pages); a turn edits the step; the held state is not left behind");
    }
#endif
#if FELUCCA_ED_SCOPE
    {   /* SCOPE (ed_scope.c, cmd 101): the trace the panel's SCOPE would draw: n heights around the zero line 64, the source set for the next 2 s */
        uint8_t q[2];
        uint32_t i, lo = 127u, hi = 0u, n;
        reset_ui();
        song.sel = 0;
        ui.scr = SCR_SOUND;                                    /* (a page whose keys play: the part sounds with a key held) */
        set_engine_of(&trk[0], trk[0].eng_req);                /* (the engine loaded, as the other tests leave it) */
        frames(2);
        trk[0].p[P_LEVEL] = 100;
        q[0] = 1;
        ed_begin(ED_SCOPE);
        ok = ed_scope(ED_SCOPE, q, 1) && ed_out[5] == 1u && scope_src == 1u && scope_ed_src == 1u;
        fm1_in.notes |= 1u << 0;                               /* (a key held: the part sounds) */
        frames(30);
        ed_begin(ED_SCOPE);
        ok &= ed_scope(ED_SCOPE, q, 1);
        n = ed_out[9] | (uint32_t)ed_out[10] << 7;
        ok &= n == SC_N && ed_n == 11u + SC_N;
        for (i = 0; i < n; i++) {
            lo = ed_out[11 + i] < lo ? ed_out[11 + i] : lo;
            hi = ed_out[11 + i] > hi ? ed_out[11 + i] : hi;
        }
        ok &= lo >= 1u && hi <= 127u && hi > lo;               /* (a note sounding: a wave, not the zero line) */
        q[0] = 0;
        ed_begin(ED_SCOPE);
        ok &= ed_scope(ED_SCOPE, q, 1) && scope_src == 0u;
        q[0] = (uint8_t)NTRK + 1u;
        ed_begin(ED_SCOPE);
        ok &= !ed_scope(ED_SCOPE, q, 1);
        scope_ed_ms = 0;
        fm1_in.notes = 0;
        frames(4);
        reset_ui();
        check(ok, "SCOPE: a source asked for (the ring follows it), n heights as the device would trace them around the zero line, a note sounding makes a wave; a bad source: no reply");
    }
    ed_begin(1); ed_scope_info();
    check(ed_n > 5u && ed_out[5] == 0x5Eu, "ED_SCOPE on: INFO carries tag 0x5E");
#else
    {
        static const uint8_t q[1] = {0};
        ed_begin(ED_SCOPE);
        check(!ed_scope(ED_SCOPE, q, 1) && ed_n == 5u, "ED_SCOPE off: cmd 101 is not handled (no reply)");
        ed_begin(1); ed_scope_info();
        check(ed_n == 5u, "ED_SCOPE off: INFO has no tag 0x5E");
    }
#endif
#if FELUCCA_ED_SCRS
    ok = !scs_call(0, SCR_SYSTEM, 0, 127, 127, 0, 0, 0, 0) && !scs_call(0, 40, 0, 127, 127, 0, 0, 0, 0) && !scs_call(0, SCR_SOUND, 9, 127, 127, 0, 0, 0, 0) &&
         !scs_call(1, SCR_SOUND, 0, 127, 127, 99, 0, 0, 0) && !scs_call(9, SCR_SOUND, 0, 127, 127, 0, 0, 0, 0);
    check(ok, "SCRS: a screen not offered, a track or row past the table, an unknown op: no reply");
#ifdef EDSCRN_GOLDEN
    {   /* the rows as JSON: the golden the web editor's mock serves (tests/scrs_table.json; EDSCRN_WRITE=1 rewrites it) */
        trk[0].p[P_LEVEL] = 100;                               /* (T1 MIX's VOLUME in the golden: -6.0 dB, whichever test ran before) */
        static const uint8_t CX[][4] = {{SCR_HOME, 0, 127, 127}, {SCR_SOUND, 0, 127, 127}, {SCR_SOUND, 1, 127, 127}, {SCR_SOUND, 3, 127, 127}, {SCR_SOUND, 3, 0, 127}, {SCR_SOUND, 3, 1, 127},
                                        {SCR_FX, 0, 127, 127}, {SCR_PROJECT, 0, 127, 127}, {SCR_STEP, 0, 127, 127}, {SCR_STEP, 3, 127, 127}, {SCR_STEP, 0, 127, 2}, {SCR_STEP, 3, 0, 2}, {SCR_SONG, 0, 127, 127}, {SCR_TEMPO, 0, 127, 127}, {SCR_SCOPE, 1, 127, 127}, {SCR_SCOPE, 0, 127, 1}
#if OP_FM6
                                        , {SCR_SOUND, 0, 127, SCRS_FAM_FM6}
#endif
                                        };
        const char *gold = "tests/scrs_table.json";
        FILE *f = tmpfile();
        char *a = malloc(1u << 20), *b = malloc(1u << 20);
        long na, nb = -1;
        uint32_t x, r, k;
        for (x = 0; x < NTRK; x++)                             /* (PATTERN's LEN DIV SWING GATE at their defaults, whichever test ran before) */
            for (k = P_SLEN; k <= P_SGATE; k++)
                trk[x].p[k] = TP[k].def;
        fprintf(f, "{\"ctx\": [\n");
        for (x = 0; x < sizeof CX / sizeof CX[0]; x++) {
            uint32_t rows, keep = trk[0].eng_req;
            step_t sv = trk[CX[x][1] % NTRK].step[2];
            dstep_t dv = TDRUM->dstep[2];
            if (CX[x][0] == SCR_STEP && CX[x][3] < 64u) {   /* (a held step: step 3 of the track holds a C4 / the lane's hit while its pages are read) */
                if (CX[x][1] == 3u)
                    dstep_set(&TDRUM->dstep[2], CX[x][2], LV_NORM, 0);
                else {
                    trk[CX[x][1]].step[2].time = ST_NOTE, trk[CX[x][1]].step[2].n = 1, trk[CX[x][1]].step[2].note[0] = 60;
                    trk[CX[x][1]].step[2].vel = 100, trk[CX[x][1]].step[2].lvl = LV_NORM, trk[CX[x][1]].step[2].rat = 0;
                }
            }
            if (CX[x][3] == SCRS_FAM_FM6)                    /* (the FM6 rows: track 1 holds the FM6 engine while they are read) */
                set_engine_of(&trk[0], fm6_slot()), frames(2);
            scs_call(0, CX[x][0], CX[x][1], CX[x][2], CX[x][3], 0, 0, 0, 0);
            rows = ed_out[10];
            fprintf(f, "%s {\"s\":%u,\"t\":%u,\"l\":%u,\"f\":%u,\"name\":\"%s\",\"rows\":[\n", x ? ",\n" : "", CX[x][0], CX[x][1], CX[x][2], CX[x][3], SCR_NAME[CX[x][0]]);
            for (r = 0; r < rows; r++) {
                scs_call(1, CX[x][0], CX[x][1], CX[x][2], CX[x][3], r, 0, 0, 0);
                scs_parse(ed_out, 8, &w);
                fprintf(f, "%s  {\"fam\":%u,\"rf\":%u,\"name\":\"%s\",\"ask\":\"%s\",\"c\":[", r ? ",\n" : "", w.fam, w.rflags, w.name, w.ask);
                for (k = 0; k < 4u; k++)
                    fprintf(f, "%s[%u,\"%s\",\"%s\",\"%s\",%u,%u,%u,%d,%d,%d,%u,%u,%d,%d,%d,%d,%u,\"%s\"]", k ? "," : "", w.kind[k], w.label[k], w.text[k], w.unit[k], w.state[k], w.danger[k],
                            w.gk[k], w.gmin[k], w.gmax[k], w.gv[k], w.byd[k], w.fmt[k], w.dmin[k], w.dmax[k], w.ddef[k], w.val[k], w.flags[k] & 31u, w.obj[k]);
                fprintf(f, "]}");
            }
            fprintf(f, "\n ]}");
            if (CX[x][3] == SCRS_FAM_FM6)
                set_engine_of(&trk[0], keep), song.sel = 0, reset_ui();
            trk[CX[x][1] % NTRK].step[2] = sv, TDRUM->dstep[2] = dv;
        }
        fprintf(f, "\n]}\n");
        fflush(f);
        na = ftell(f);
        rewind(f);
        na = (long)fread(a, 1, (size_t)na, f);
        fclose(f);
        if (getenv("EDSCRN_WRITE") && (f = fopen(gold, "w"))) {
            fwrite(a, 1, (size_t)na, f);
            fclose(f);
        }
        if ((f = fopen(gold, "r"))) {
            nb = (long)fread(b, 1, 1u << 20, f);
            fclose(f);
        }
        check(nb == na && !memcmp(a, b, (size_t)na), "SCRS: the rows equal tests/scrs_table.json (EDSCRN_WRITE=1 rewrites it; the web editor's mock serves them)");
        free(a), free(b);
    }
#endif
    if (getenv("EDSCRS_DUMP")) {
        for (i = 0; i < sizeof SCS_SCR; i++)
            for (t = 0; t < NTRK; t++)
                edscrs_dump(SCS_SCR[i], t, 127, 127);
        for (t = 0; t < 17u; t++)
            edscrs_dump(SCR_SOUND, 3, t, 127);
        trk[0].step[2].time = ST_NOTE, trk[0].step[2].n = 1, trk[0].step[2].note[0] = 60;
        edscrs_dump(SCR_STEP, 0, 127, 2);
        dstep_set(&TDRUM->dstep[2], 1, LV_NORM, 0);
        edscrs_dump(SCR_STEP, 3, 1, 2);
    }
    ed_begin(1); ed_scrs_info();
    check(ed_n > 5u && ed_out[5] == 0x5Cu, "ED_SCRS on: INFO carries tag 0x5C");
#else
    {
        static const uint8_t q[4] = {0, 0, 0, 0};
        ed_begin(ED_SCRS);
        check(!ed_scrs(ED_SCRS, q, 4) && ed_n == 5u, "ED_SCRS off: cmd 99 is not handled (no reply)");
        ed_begin(1); ed_scrs_info();
        check(ed_n == 5u, "ED_SCRS off: INFO has no tag 0x5C");
    }
#endif
    reset_ui();
}
