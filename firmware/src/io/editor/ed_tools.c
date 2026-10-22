/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v13: STEP's TOOLS, GEN and GEN OUT pages (ui/optimist/op_tools.c; included by editor.c; web/EDITOR_PROTOCOL.md
 * "Sequence tools"; builds with ED_SEQ and FELUCCA_SEQTOOLS or the generators, FELUCCA_GENS). REVERSE, RANDOM, COOK and LENGTH (x2, 1/2) act on the selected
 * track's pattern (a drum track: the whole kit, or one lane), each one undo level, through the device's own function (tl_run: the UI's YES
 * calls the same one), so the steps and their events move exactly as on the device. EUCLID is the note generator (HITS spread over STEPS,
 * turned by ROT, repeated through LEN): ON writes it over the pattern, OFF keeps it or gives the pattern back.
 *   98 TOOLS  op [args] -> op, rc, then the state
 *       state: track, lane (the one EUCLID writes: 0..9), len, hits, steps, rot, on (the generator is on here), then op 1's message: a, b
 *       op 0  -                                 get the state
 *       op 1  verb (0 REVERSE 1 RANDOM 2 COOK 3 LENGTH 4 QUANTIZE), lane (127: the whole kit / a synth track), half (LENGTH: 1 = 1/2,
 *             0 = x2; QUANTIZE: the grid, 1 2 4 a step)
 *       op 2  k (0 HITS, 1 STEPS, 2 ROT), value            EUCLID's value, written at once while it is on
 *       op 3  lane (127: keep the lane)                   EUCLID ON: the pattern is written (rc 1: another generator is on)
 *       op 4  -                                           EUCLID OFF, the generated steps kept
 *       op 5  -                                           EUCLID OFF, the pattern as it was given back
 *   v2 (INFO version 2): the slot's generator of any type, as the GEN and GEN OUT cards; every state ends with its type, mode, on
 *       op 6  type (0 EUCLID 1 RANDOM 2 PULSE 3 GRIDS) another generator: its values at their defaults
 *       op 7  k (0..2), value                             the generator's value k (EUCLID HITS STEPS ROT, RANDOM DENSITY SEED ACCENT,
 *                                                         PULSE EVERY OFFSET RATCHET), fitted to its range
 *       op 8  mode (0 REPLACE 1 ADD 2 MASK)
 *       op 9  on (0 / 1), lane (127: keep the lane)      ON / OFF, the pattern untouched (OFF with what plays written: op 4)
 * rc 0 done. A bad op: no reply; the selected track is the device's (TRACK, 27). INFO tag 0x5B: version, then (version 3,
 * 2026-10-22) what is built: bit 0 the tools (op 1, SEQTOOLS), bit 1 the generators (ops 4..9, NOTEGEN or GEN_GRIDS),
 * bit 2 EUCLID's own ops (2, 3: NOTEGEN); then the types built, a bit each (bit 0 EUCLID .. bit 3 GRIDS). An op of a part
 * not built, a type not built: no reply; without the generators the state's generator is all 0. Version 2 had all. */
#include "ed_reply.h"         /* (ed_b2..ed_b4, ed_b32) */
enum { ED_TOOLS = 98 };
#if (FELUCCA_SEQTOOLS || FELUCCA_GENS) && FELUCCA_ED_TOOLS
static void ed_tools_state(uint32_t op, uint32_t rc, const char *sa, const char *sb)
{
    track_t *t = TSEL;
    uint8_t gs[6] = {0, 0, 0, 0, 0, 0};                 /* type mode on, the three values (no generators: all 0) */
#if FELUCCA_GENS
    gen_t g = *ng_cur();                                /* (the slot's settings; EUCLID's three values, ON only for EUCLID) */
    GEN_TYPES[g.type % GT_N].fit(&g, trk_len(t));
    gs[0] = (uint8_t)(g.type % GT_N), gs[1] = (uint8_t)(g.mode % GM_N), gs[2] = g.on != 0u;
    gs[3] = g.p[0], gs[4] = g.p[1], gs[5] = g.p[2];
#endif
    ed_b4(op, rc, song.sel % NTRK, edl_out(lane_selected())); ed_b4(trk_len(t), gs[3], gs[4], gs[5]); ed_b(gs[2] && gs[0] == 0u);   /* (EUCLID on: type 0) */
    ed_str(sa, 24);
    ed_str(sb, 24);
    ed_b3(gs[0], gs[1], gs[2]);                         /* (v2: the slot's generator, any type; GT_EUCLID is 0) */
}
static int ed_tools(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, rc = 0;
    track_t *t = TSEL;
#if FELUCCA_GENS
    gen_t g;
#endif
#if FELUCCA_SEQTOOLS
    char b[24];
#endif
    const char *sa = "", *sb = "";
    if (cmd != ED_TOOLS || op > 9u)
        return 0;
    if ((op == 1u && !FELUCCA_SEQTOOLS) || ((op == 2u || op == 3u) && !FELUCCA_NOTEGEN) || (op >= 4u && !FELUCCA_GENS))
        return 0;                                       /* (a part not built: INFO 0x5B says which) */
    (void)t;
#if FELUCCA_GENS
    if (op >= 6u) {                                     /* v2: the slot's generator as the GEN / GEN OUT cards (ng_turn) */
        uint32_t len = trk_len(t);
        if (na < (op == 7u ? 3u : 2u) || (op == 6u && !gen_has(a[1])) || (op == 7u && a[1] >= GEN_NP) || (op == 8u && a[1] >= GM_N) || (op == 9u && a[1] > 1u))
            return 0;
        if (op == 9u && na >= 3u && a[2] < DRUM_LANES && is_drum(t))
            lane_select(a[2]);
        fm1_irq_off();
        g = *ng_cur();
        if (op == 6u && a[1] != g.type)
            gen_defaults(&g, a[1]);                     /* (another generator: its values at their defaults) */
        else if (op == 7u) {
            const gen_par_t *d = &GEN_TYPES[g.type % GT_N].par[a[1]];
            GEN_TYPES[g.type % GT_N].fit(&g, len);
            g.p[a[1]] = (uint8_t)clamp((int32_t)a[2], d->min, ng_max(d, len));
        } else if (op == 8u)
            g.mode = a[1];
        else if (op == 9u)
            g.on = a[1];                                /* (OFF: the pattern as it is; WRITE first is op 4) */
        GEN_TYPES[g.type % GT_N].fit(&g, len);
        ng_put(&g);
        fm1_irq_on();
        ui.force = 1;
        ed_tools_state(op, 0, "", "");
        return 1;
    }
#endif
#if FELUCCA_SEQTOOLS
    if (op == 1u) {
        int32_t lane;
        if (na < 4u || a[1] > TL_QNT)                    /* (TL_QNT, 2026-10-19: a[3] its grid, 1 2 4 a step) */
            return 0;
        lane = a[2] < DRUM_LANES && is_drum(t) ? (int32_t)a[2] : -1;
        if (ng_on())
            rc = 2u;                                    /* (the generator is on: the pattern is its, turn it off first) */
        else
            (void)tl_run(t, a[1], lane, a[1] == TL_QNT ? a[3] : a[3] != 0u, &sa, &sb, b);
    }
#endif
#if FELUCCA_NOTEGEN
    if (op == 2u || op == 3u) {                  /* 2 EUCLID's value; 3 EUCLID ON here (the generator layer: the pattern untouched) */
        if (op == 2u && (na < 3u || a[1] > 2u))
            return 0;
        if (op == 3u && na >= 2u && a[1] < DRUM_LANES && is_drum(t))
            lane_select(a[1]);
        fm1_irq_off();                                  /* (read, changed, written: one section, as the UI's ng_turn) */
        g = *ng_cur();
        if (g.type != GT_EUCLID)
            gen_defaults(&g, GT_EUCLID);
        if (op == 2u)
            g.p[a[1]] = (uint8_t)clamp((int32_t)a[2], GEN_TYPES[GT_EUCLID].par[a[1]].min, ng_max(&GEN_TYPES[GT_EUCLID].par[a[1]], trk_len(t)));
        else
            g.on = 1;
        GEN_TYPES[GT_EUCLID].fit(&g, trk_len(t));
        ng_put(&g);
        fm1_irq_on();
    }
#endif
#if FELUCCA_GENS
    if (op == 4u || op == 5u) {                         /* OFF (any type): 4 writes what plays into the pattern first (one undo level) */
        g = *ng_cur();
        if (g.on && op == 4u && gen_write_prep(t, ng_slot())) {
            tl_mark(t);
            fm1_irq_off();
            (void)gen_write_commit(t);
            t->seq_active = 1;
            fm1_irq_on();
            sync_reload = 1;
        }
        fm1_irq_off();
        g = *ng_cur();
        g.on = 0;
        ng_put(&g);
        fm1_irq_on();
    }
#endif
    ui.force = 1;
    ed_tools_state(op, rc, sa, sb);
    return 1;
}
#define ED_TOOLS_FN ed_tools,               /* (editor.c: its table of commands) */
static void ed_tools_info(void)
{
    ed_b(0x5B);
    ed_b(3);
    ed_b(3);
    ed_b((FELUCCA_SEQTOOLS ? 1u : 0u) | (FELUCCA_GENS ? 2u : 0u) | (FELUCCA_NOTEGEN ? 4u : 0u));   /* (what is built) */
    ed_b((FELUCCA_NOTEGEN ? 7u : 0u) | (FELUCCA_GEN_GRIDS ? 8u : 0u));   /* (the types: EUCLID RANDOM PULSE, GRIDS) */
}
#else
static int ed_tools(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
#define ED_TOOLS_FN
static void ed_tools_info(void) {}
#endif
