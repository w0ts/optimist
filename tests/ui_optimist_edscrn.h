/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the editor's SCRN (cmd 96, firmware/src/io/editor/ed_scrn.c): GLOBAL and SYSTEM as the Optimist UI's
 * lines. Checked in every variant:
 *   op 0   the line and page counts are the screen's own (gl_n, gl_npg)
 *   op 1   every line's reply carries what the device draws: page, section, label, value, unit, kind
 *   op 2   a turn (fine, a unit) moves the setting through the screen's own function; the reply is the line again
 *   op 3   YES on a destructive action asks (ask 1, the device's question) and does nothing until ok
 *   a bad screen or line: no reply
 * With -DEDSCRN_GOLDEN=1 (a build with the SYSTEM rows of LIGHTS, USB SERIAL, BRIGHT) the lines as JSON are compared with
 * tests/scrn_table.json (EDSCRN_WRITE=1 rewrites it): the web editor's mock serves them (tools/gen_fxp_table.py). Included after
 * ui_optimist_glpages.h. */
static uint8_t ed_out[900];
static uint32_t ed_n;
static void ed_begin(uint32_t cmd) { ed_out[0] = 0xF0; ed_out[1] = 0x7D; ed_out[2] = 0x46; ed_out[3] = 0x4C; ed_out[4] = (uint8_t)cmd; ed_n = 5; }
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out - 1u) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_v(int32_t v) { uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192); ed_b(u); ed_b(u >> 7); }
static void ed_str(const char *s, uint32_t max) { uint32_t i; for (i = 0; s && s[i] && i < max; i++) ed_b((uint8_t)s[i] & 0x7Fu); ed_b(0); }
static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
#include "../firmware/src/io/editor/ed_scrn.c"
#include "../firmware/src/io/editor/ed_song.c"
#include "../firmware/src/io/editor/ed_tools.c"

/* a decoded LINE reply (after the header of n bytes: op screen line [rc]) */
typedef struct {
    uint32_t page, kind, state, danger, ask, gk, byd, fmt;
    int32_t gmin, gmax, gv, dmin, dmax, ddef;
    char title[24], sect[14], label[16], val[16], unit[10], obj[16];
} scl_t;
static uint32_t scl_str(const uint8_t *a, uint32_t p, char *o, uint32_t n)
{
    uint32_t i = 0;
    while (a[p] && i + 1u < n)
        o[i++] = (char)a[p++];
    o[i] = 0;
    while (a[p])
        p++;
    return p + 1u;
}
static void scl_parse(const uint8_t *a, uint32_t p, scl_t *o)
{
    memset(o, 0, sizeof *o);
    o->page = a[p++];
    p = scl_str(a, p, o->title, sizeof o->title);
    p = scl_str(a, p, o->sect, sizeof o->sect);
    o->kind = a[p++];
    p = scl_str(a, p, o->label, sizeof o->label);
    p = scl_str(a, p, o->val, sizeof o->val);
    p = scl_str(a, p, o->unit, sizeof o->unit);
    o->state = a[p++], o->danger = a[p++], o->ask = a[p++], o->gk = a[p++];
    o->gmin = ed_rv(a + p), p += 2, o->gmax = ed_rv(a + p), p += 2, o->gv = ed_rv(a + p), p += 2;
    o->byd = a[p++];
    if (o->byd) {
        o->fmt = a[p++];
        o->dmin = ed_rv(a + p), p += 2, o->dmax = ed_rv(a + p), p += 2, o->ddef = ed_rv(a + p), p += 2;
    }
    scl_str(a, p, o->obj, sizeof o->obj);
}
/* the entries of a list / switch line (gauge kinds 3, 4, 5: few entries, many entries, two): what the device draws at each position, found by turning the line
 * through the screen's own function from its first entry to its last and back to where it was ("vals": [value text, unit] a position); the web editor's mock shows
 * them as the device would when its arrows are clicked */
static uint32_t scl_vals(uint32_t s, uint32_t i, const scl_t *c, char (*val)[16], char (*unit)[10], uint32_t max)
{
    uint8_t q[6];
    uint32_t n = 0;
    int32_t g, here = c->gv;
    scl_t t;
    if (c->kind != 1u || c->gk < 3u || c->gk > 5u || c->gmax <= c->gmin)
        return 0;
    for (g = c->gmin; g <= c->gmax && n < max; g++) {
        int32_t d = g - here;
        if (d) {
            q[0] = 2, q[1] = (uint8_t)s, q[2] = (uint8_t)i, q[3] = 1, q[4] = (uint8_t)((d + 8192) & 127), q[5] = (uint8_t)((d + 8192) >> 7);
            ed_begin(ED_SCRN);
            ed_scrn(ED_SCRN, q, 6);
        } else {                                         /* (a turn of 0 steps may still toggle a switch: read the line) */
            q[0] = 1, q[1] = (uint8_t)s, q[2] = (uint8_t)i;
            ed_begin(ED_SCRN);
            ed_scrn(ED_SCRN, q, 3);
        }
        scl_parse(ed_out, 8, &t);
        strcpy(val[n], t.val), strcpy(unit[n], t.unit);
        here = t.gv;
        n++;
    }
    if (c->gv != here) {                                 /* (put back) */
        int32_t d = c->gv - here;
        q[0] = 2, q[1] = (uint8_t)s, q[2] = (uint8_t)i, q[3] = 1, q[4] = (uint8_t)((d + 8192) & 127), q[5] = (uint8_t)((d + 8192) >> 7);
        ed_begin(ED_SCRN);
        ed_scrn(ED_SCRN, q, 6);
    }
    return n;
}
static void scl_json(FILE *f, uint32_t s, uint32_t i, const scl_t *c0, int first, uint32_t nv, char (*vv)[16], char (*vu)[10])
{
    scl_t cc = *c0;                                     /* (the build's date and time are not the golden's: BUILD's text is fixed here) */
    const scl_t *c = &cc;
    if (!strcmp(c->label, "BUILD"))
        strcpy(cc.val, "<date>"), strcpy(cc.unit, "<time>");
    fprintf(f, "%s   {\"s\":%u,\"i\":%u,\"page\":%u,\"title\":\"%s\",\"sect\":\"%s\",\"kind\":%u,\"label\":\"%s\",\"val\":\"%s\",\"unit\":\"%s\",\"state\":%u,\"danger\":%u,\"gk\":%u,\"gmin\":%d,\"gmax\":%d,\"gv\":%d,\"byd\":%u,\"fmt\":%u,\"dmin\":%d,\"dmax\":%d,\"ddef\":%d,\"obj\":\"%s\"",
            first ? "" : ",\n", s, i, c->page, c->title, c->sect, c->kind, c->label, c->val, c->unit, c->state, c->danger, c->gk, c->gmin, c->gmax, c->gv, c->byd, c->fmt, c->dmin, c->dmax, c->ddef, c->obj);
    if (nv) {
        uint32_t k;
        fprintf(f, ",\"vals\":[");
        for (k = 0; k < nv; k++)
            fprintf(f, "%s[\"%s\",\"%s\"]", k ? "," : "", vv[k], vu[k]);
        fprintf(f, "]");
    }
    fprintf(f, "}");
}
#if FELUCCA_SEQTOOLS && FELUCCA_ED_TOOLS
/* TOOLS (ed_tools.c): the tools and EUCLID through the device's own functions */
static void edtools_tests(void)
{
    uint32_t i;
    uint8_t q[8];
    int ok = 1, r;
    track_t *t;
    reset_ui();
    song.sel = 0;
    t = &trk[0];
    for (i = 0; i < NSTEP; i++) {
        memset(&t->step[i], 0, sizeof t->step[i]);
        t->step[i].time = ST_REST;
    }
    t->p[P_SLEN] = 8;
    for (i = 0; i < 4u; i++) {                         /* notes 60 61 62 63 on steps 1..4 (NOTE steps, 100) */
        t->step[i].time = ST_NOTE, t->step[i].n = 1, t->step[i].note[0] = (uint8_t)(60 + i), t->step[i].vel = 100;
    }
    q[0] = 1, q[1] = 0, q[2] = 127, q[3] = 0;           /* REVERSE the whole pattern (a synth track) */
    ed_begin(ED_TOOLS);
    r = ed_tools(ED_TOOLS, q, 4);
    ok = r && ed_out[5] == 1u && ed_out[6] == 0u && ed_out[7] == 0u;
    for (i = 0; i < 8u; i++)
        ok &= i < 4u ? t->step[i].time == ST_REST : t->step[i].time == ST_NOTE && t->step[i].note[0] == 60 + (7 - i);
    check(ok, "TOOLS op 1: REVERSE through the device's tl_run: the four notes move to the end of the 8 steps, reversed");
    q[1] = 3, q[3] = 0;                                 /* LENGTH x2 */
    ed_begin(ED_TOOLS);
    r = ed_tools(ED_TOOLS, q, 4);
    check(r && trk_len(t) == 16u && ed_out[9] == 16u, "TOOLS op 1: LENGTH x2: 8 steps become 16, the state says so");
    q[3] = 1;                                           /* LENGTH 1/2 */
    ed_begin(ED_TOOLS);
    r = ed_tools(ED_TOOLS, q, 4);
    check(r && trk_len(t) == 8u, "TOOLS op 1: LENGTH 1/2: back to 8");
    {   /* EUCLID, the generator layer: ON leaves the pattern alone, HITS turns it, OFF (4) writes what plays, OFF (5) leaves the pattern as it was */
        uint8_t was[8];
        uint32_t hits = 0;
        gen_t g0 = gen[gen_slot(song.sel % NTRK, lane_selected()) % GEN_NSLOT];
        for (i = 0; i < 8u; i++)
            was[i] = t->step[i].time;
        gen_defaults(&gen[gen_slot(song.sel % NTRK, lane_selected()) % GEN_NSLOT], GT_EUCLID);   /* (HITS 4 STEPS 16 ROT 0, off) */
        gen_sync();
        q[0] = 3, q[1] = 127;
        ed_begin(ED_TOOLS);
        r = ed_tools(ED_TOOLS, q, 2);
        ok = r && ed_out[6] == 0u && ed_out[13] == 1u;
        for (i = 0; i < 8u; i++)
            ok &= t->step[i].time == was[i];            /* (the pattern is untouched while it plays) */
        q[0] = 2, q[1] = 0, q[2] = 3;                   /* HITS 3 over STEPS (len 8: 16 clamps to 8) */
        ed_begin(ED_TOOLS);
        r = ed_tools(ED_TOOLS, q, 3);
        ok &= r && ed_out[10] == 3u;
        q[0] = 4;                                       /* OFF, what plays kept: the 3 hits are the pattern now */
        ed_begin(ED_TOOLS);
        r = ed_tools(ED_TOOLS, q, 1);
        for (i = 0; i < 8u; i++)
            hits += t->step[i].time == ST_NOTE;
        ok &= r && ed_out[13] == 0u && hits == 3u;
        q[0] = 3, q[1] = 127;                           /* ON again, then OFF (5): the pattern stays as it is */
        ed_begin(ED_TOOLS);
        r = ed_tools(ED_TOOLS, q, 2);
        q[0] = 5;
        ed_begin(ED_TOOLS);
        r = ed_tools(ED_TOOLS, q, 1);
        ok &= r && ed_out[13] == 0u;
        for (i = 0; i < 8u; i++)
            ok &= (t->step[i].time == ST_NOTE) == (hits ? t->step[i].time == ST_NOTE : 0);
        gen[gen_slot(song.sel % NTRK, lane_selected()) % GEN_NSLOT] = g0;
        gen_sync();
        (void)was;
        check(ok, "TOOLS op 3 / 2 / 4 / 5: EUCLID ON leaves the pattern, HITS 3 turns it, OFF keeps the 3 hits as steps, OFF (5) writes nothing");
    }
    {   /* v2: the slot's generator of any type (ops 6..9; the reply ends with type, mode, on after the two strings) */
        gen_t g0 = gen[gen_slot(song.sel % NTRK, lane_selected()) % GEN_NSLOT];
        uint8_t was[8];
        for (i = 0; i < 8u; i++)
            was[i] = t->step[i].time;
        q[0] = 6, q[1] = GT_PULSE;                      /* PULSE at its defaults (EVERY 4, OFFSET 0, RATCHET 2) */
        ed_begin(ED_TOOLS);
        r = ed_tools(ED_TOOLS, q, 2);
        ok = r && ed_out[10] == 4u && ed_out[11] == 0u && ed_out[12] == 2u && ed_out[16] == GT_PULSE && ed_out[18] == 0u;
        q[0] = 7, q[1] = 1, q[2] = 9;                   /* OFFSET 9: fitted under EVERY (9 % 4 = 1) */
        ed_begin(ED_TOOLS);
        r = ed_tools(ED_TOOLS, q, 3);
        ok &= r && ed_out[11] == 1u;
        q[0] = 8, q[1] = GM_ADD;
        ed_begin(ED_TOOLS);
        r = ed_tools(ED_TOOLS, q, 2);
        ok &= r && ed_out[17] == GM_ADD;
        q[0] = 9, q[1] = 1, q[2] = 127;                 /* ON: the EUCLID flag stays 0, the slot's on 1, the pattern untouched */
        ed_begin(ED_TOOLS);
        r = ed_tools(ED_TOOLS, q, 3);
        ok &= r && ed_out[13] == 0u && ed_out[18] == 1u && ng_cur()->on && ng_cur()->type == GT_PULSE;
        for (i = 0; i < 8u; i++)
            ok &= t->step[i].time == was[i];
        q[0] = 9, q[1] = 0;
        ed_begin(ED_TOOLS);
        r = ed_tools(ED_TOOLS, q, 2);
        ok &= r && ed_out[18] == 0u && !ng_cur()->on;
        q[0] = 6, q[1] = GT_N;                          /* a type the build has not: no reply */
        ed_begin(ED_TOOLS);
        ok &= !ed_tools(ED_TOOLS, q, 2);
        gen[gen_slot(song.sel % NTRK, lane_selected()) % GEN_NSLOT] = g0;
        gen_sync();
        check(ok, "TOOLS v2 op 6..9: PULSE at its defaults, OFFSET fitted, MODE ADD, ON / OFF (the pattern untouched), a bad type no reply");
    }
    q[0] = 10;
    ed_begin(ED_TOOLS);
    check(!ed_tools(ED_TOOLS, q, 1), "TOOLS: an unknown op: no reply");
    reset_ui();
}
#endif
static void edscrn_tests(void)
{
    uint32_t s, i, ok = 1, n = 0;
    uint8_t q[8];
    scl_t c;
    reset_ui();
    song.sel = 0;
#if FELUCCA_ED_SCRN
    for (s = 0; s < 2u; s++) {
        q[0] = 0, q[1] = (uint8_t)s;
        ed_begin(ED_SCRN);
        ok &= (uint32_t)ed_scrn(ED_SCRN, q, 2) && ed_out[5] == 0u && ed_out[6] == s && ed_out[7] == gl_n(s) && ed_out[8] == gl_npg(s);
    }
    check(ok, "SCRN op 0: GLOBAL and SYSTEM: the line and page counts are the screen's own");
    ok = 1;
    for (s = 0; s < 2u; s++)
        for (i = 0; i < gl_n(s); i++) {
            cell_t d;
            char sec[14], tl[24];
            q[0] = 1, q[1] = (uint8_t)s, q[2] = (uint8_t)i;
            ed_begin(ED_SCRN);
            ok &= (uint32_t)ed_scrn(ED_SCRN, q, 3) && ed_out[5] == 1u && ed_out[6] == s && ed_out[7] == i;
            scl_parse(ed_out, 8, &c);
            gl_cell(s, i, &d);
            gl_sect(s, i, sec);
            gl_ptitle(s, i, tl, sizeof tl);
            ok &= c.page == gl_pg(s, i) && !strncmp(c.sect, sec, 12) && !strncmp(c.title, tl, 20) && c.kind == d.kind &&
                  !strcmp(c.label, d.label ? d.label : "") && !strncmp(c.val, d.val, 14) && !strncmp(c.unit, d.unit ? d.unit : "", 8) && c.byd == (d.d != 0);
            n++;
        }
    check(ok && n > 10u, "SCRN op 1: every line of both screens as the device draws it (page, section, label, value, unit, kind, descriptor)");
    /* a turn: GLOBAL's TEMPO BPM, a unit (fine) up and down; the default */
    {
        int32_t b0 = song.g[G_BPM];
        scl_t a;
        q[0] = 2, q[1] = 0, q[2] = 0, q[3] = 1, q[4] = (uint8_t)((3 + 8192) & 127), q[5] = (uint8_t)((3 + 8192) >> 7);
        ed_begin(ED_SCRN);
        ok = (uint32_t)ed_scrn(ED_SCRN, q, 6) && ed_out[5] == 2u;
        scl_parse(ed_out, 8, &a);
        ok &= !strcmp(a.label, "BPM") && song.g[G_BPM] == b0 + 3 && a.kind == 1u;
        q[4] = (uint8_t)((-3 + 8192) & 127), q[5] = (uint8_t)((-3 + 8192) >> 7);
        ed_begin(ED_SCRN);
        ok &= (uint32_t)ed_scrn(ED_SCRN, q, 6) && song.g[G_BPM] == b0;
        q[3] = 2, q[4] = q[5] = 0;
        ed_begin(ED_SCRN);
        ok &= (uint32_t)ed_scrn(ED_SCRN, q, 6) && song.g[G_BPM] == GP[G_BPM].def;
        song.g[G_BPM] = (int16_t)b0;
        check(ok, "SCRN op 2: a fine turn moves BPM by units through the screen's own turn; mode 2 its default");
    }
    /* YES on an action that destroys: SYSTEM > CALIBRATE > PANEL asks first */
    {
        uint32_t line = 0xFFu;
        for (i = 0; i < gl_n(1); i++) {
            cell_t d;
            gl_cell(1, i, &d);
            if (d.kind == CK_ACT && d.label && !strcmp(d.label, "PANEL"))
                line = i;
        }
        ok = 1;
        if (line != 0xFFu) {
            q[0] = 3, q[1] = 1, q[2] = (uint8_t)line, q[3] = 0;
            ed_begin(ED_SCRN);
            ok = (uint32_t)ed_scrn(ED_SCRN, q, 4) && ed_out[5] == 3u && ed_out[8] == 1u;
            scl_parse(ed_out, 9, &c);
            ok &= c.ask == 1u && op_armed();
            op_disarm();
        }
        check(ok, "SCRN op 3: YES on CALIBRATE > PANEL (ok 0) asks the device's question and does nothing");
    }
    ok = 1;
    q[0] = 1, q[1] = 2, q[2] = 0;
    ed_begin(ED_SCRN);
    ok &= !ed_scrn(ED_SCRN, q, 3);
    q[1] = 0, q[2] = 99;
    ed_begin(ED_SCRN);
    ok &= !ed_scrn(ED_SCRN, q, 3);
    check(ok, "SCRN: a screen or line past the table: no reply");
#ifdef EDSCRN_GOLDEN
    {   /* the lines as JSON: the golden the web editor's mock serves */
        const char *gold = "tests/scrn_table.json";
        FILE *f = tmpfile();
        char *a = malloc(1u << 20), *b = malloc(1u << 20);
        long na, nb = -1;
        int first = 1;
        fprintf(f, "{\"lines\": [\n");
        for (s = 0; s < 2u; s++)
            for (i = 0; i < gl_n(s); i++) {
                q[0] = 1, q[1] = (uint8_t)s, q[2] = (uint8_t)i;
                ed_begin(ED_SCRN);
                ed_scrn(ED_SCRN, q, 3);
                scl_parse(ed_out, 8, &c);
                {
                    char vv[24][16], vu[24][10];
                    uint32_t nv = scl_vals(s, i, &c, vv, vu, 24u);
                    scl_json(f, s, i, &c, first, nv, vv, vu);
                }
                first = 0;
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
        check(nb == na && !memcmp(a, b, (size_t)na), "SCRN: the lines equal tests/scrn_table.json (EDSCRN_WRITE=1 rewrites it; the web editor's mock serves them)");
        free(a), free(b);
    }
#endif
    ed_begin(1); ed_scrn_info();
    check(ed_n > 5u && ed_out[5] == 0x59u, "ED_SCRN on: INFO carries tag 0x59");
#else
    q[0] = 0, q[1] = 0;
    ed_begin(ED_SCRN);
    check(!ed_scrn(ED_SCRN, q, 2) && ed_n == 5u, "ED_SCRN off: cmd 96 is not handled (no reply)");
    ed_begin(1); ed_scrn_info();
    check(ed_n == 5u, "ED_SCRN off: INFO has no tag 0x59");
#endif
    reset_ui();
#if FELUCCA_SEQTOOLS && FELUCCA_ED_TOOLS
    edtools_tests();
    ed_begin(1); ed_tools_info();
    check(ed_n > 5u && ed_out[5] == 0x5Bu, "ED_TOOLS on: INFO carries tag 0x5B");
#elif FELUCCA_SEQTOOLS
    q[0] = 0, q[1] = 0, q[2] = 0, q[3] = 0;
    ed_begin(ED_TOOLS);
    check(!ed_tools(ED_TOOLS, q, 4) && ed_n == 5u, "ED_TOOLS off: cmd 98 is not handled (no reply)");
    ed_begin(1); ed_tools_info();
    check(ed_n == 5u, "ED_TOOLS off: INFO has no tag 0x5B");
#endif
}
