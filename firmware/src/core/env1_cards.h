/* SPDX-License-Identifier: GPL-3.0-only */
/* ENV 1 is the engine's own envelope (the user, 2026-10-16; docs/ENV-MOD-DESIGN.md): the amplitude is always wired (no AMP
 * knob), and its page ENV 1 MOD has four depth knobs made in advance for each engine, no DEST / AMT pickers. Included by
 * params.c (after engines.c: the engines built, registry.h ENGINE_LIST); voice.c env_plan, the pages and the storage
 * read it. A card is one of:
 *   path   FLT / SHP / PIT: ENV on the voice's cutoff, shape, pitch (FLT SHP: P_ED_FLT P_ED_SHP, the depths of before
 *          the slots, envq15 x d >> 7; PIT: P_E1PIT, +-31.5 st at full, as an envelope's PIT slot)
 *   move   ENV moves the engine's EDIT value n for each voice as it renders (voice.c env_vmove, as an ENV2 slot on E-n),
 *          its depth in P_E1X, P_E1Y, P_E1Z (the first, second, third move card of the engine). A part move: the same
 *          for a value the engine reads once a block for the part (WHEEL's DRV CLICK TOP: e->block), moved for the block
 *          from the part's newest voice (voice.c lfo_over_in, as an ENV2 slot on PAN)
 *   alias  the engine's own EDIT value n that already shapes its envelope (ACID's ENV DEC ACC CUT, CZ's DCW W.TIM A.TIM,
 *          FM6's M.TIM C.TIM, PHASE's ENV): the same value shown a second time, nothing stored, nothing computed
 * The depths are -64..63 (AMT 2 x d on the shared arithmetic). E1C_<NAME>: the engine's four cards {kind | arg},
 * their labels (E1L_*), and ENV 2's default DEST A (the filter where the engine has one, else its nearest: core.h LT_*,
 * at AMT 0: the user raises the amount) */
#ifndef FELUCCA_ENV1_CARDS_H
#define FELUCCA_ENV1_CARDS_H
#define E1K_NONE 0x00u
#define E1K_FLT 0x10u
#define E1K_SHP 0x20u
#define E1K_PIT 0x30u
#define E1K_MV(n) (0x40u | (n))                       /* a move of EDIT value n (0..7) */
#define E1K_AL(n) (0x50u | (n))                       /* an alias of EDIT value n */
#define E1K_PM(n) (0x60u | (n))                       /* a part move of EDIT value n (read once a block) */
#define E1K_MOVES(c) (E1K_KIND(c) == 0x40u || E1K_KIND(c) == 0x60u)
#define E1K_KIND(c) ((c) & 0xF0u)
#define E1K_ARG(c) ((c) & 0x07u)
#define E1_NMV 3u                                     /* move cards an engine may have: P_E1X P_E1Y P_E1Z */
/* the cards' names: 0..7 the engine's EDIT value n's own (track_desc: its mode's), E1L_FLT .. E1L_SHP the depths' */
enum { E1L_FLT = 8, E1L_PIT, E1L_SHP };
#define E1C_ANALOG E1C(E1K_FLT, P_ED_FLT, E1L_FLT, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_SHP, P_ED_SHP, E1L_SHP, E1K_MV(5), P_E1X + 0, 5, LT_FLT)
#define E1C_SUPER E1C(E1K_FLT, P_ED_FLT, E1L_FLT, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_SHP, P_ED_SHP, E1L_SHP, E1K_MV(6), P_E1X + 0, 6, LT_FLT)
#define E1C_DIGITAL E1C(E1K_FLT, P_ED_FLT, 4, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_MV(6), P_E1X + 0, 6, E1K_MV(5), P_E1X + 1, 5, LT_FLT)
#define E1C_PHASE E1C(E1K_AL(3), P_E0 + 3, 2, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_MV(4), P_E1X + 0, 4, E1K_MV(6), P_E1X + 1, 6, LT_FLT)
#define E1C_LOFI E1C(E1K_FLT, P_ED_FLT, 7, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_MV(2), P_E1X + 0, 2, E1K_MV(4), P_E1X + 1, 4, LT_FLT)
#define E1C_SAMPLE E1C(E1K_FLT, P_ED_FLT, 4, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_MV(2), P_E1X + 0, 2, E1K_MV(6), P_E1X + 1, 6, LT_FLT)
#define E1C_FORMANT E1C(E1K_FLT, P_ED_FLT, 0, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_SHP, P_ED_SHP, 4, E1K_MV(6), P_E1X + 0, 6, LT_FLT)
#define E1C_TRIO E1C(E1K_FLT, P_ED_FLT, E1L_FLT, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_SHP, P_ED_SHP, 7, E1K_MV(6), P_E1X + 0, 6, LT_FLT)
#define E1C_DRAWBAR E1C(E1K_PIT, P_E1PIT, E1L_PIT, E1K_PM(6), P_E1X + 0, 6, \
    E1K_PM(5), P_E1X + 1, 5, E1K_PM(3), P_E1X + 2, 3, LT_E4)   /* (no filter: TOP) */
#define E1C_GRAIN E1C(E1K_FLT, P_ED_FLT, 7, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_SHP, P_ED_SHP, 1, E1K_MV(3), P_E1X + 0, 3, LT_FLT)
#define E1C_FM6 E1C(E1K_AL(2), P_E0 + 2, 2, E1K_AL(3), P_E0 + 3, 3, \
    E1K_SHP, P_ED_SHP, 1, E1K_PIT, P_E1PIT, E1L_PIT, LT_SHP)   /* (no filter: MOD) */
#define E1C_SLICE E1C(E1K_FLT, P_ED_FLT, 7, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_MV(6), P_E1X + 0, 6, E1K_MV(3), P_E1X + 1, 3, LT_FLT)   /* (PTCH: START is read at the note-on only) */
#define E1C_PHYS E1C(E1K_FLT, P_ED_FLT, 2, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_MV(3), P_E1X + 0, 3, E1K_MV(7), P_E1X + 1, 7, LT_FLT)
#define E1C_ACID E1C(E1K_AL(2), P_E0 + 2, 2, E1K_AL(3), P_E0 + 3, 3, \
    E1K_AL(4), P_E0 + 4, 4, E1K_AL(7), P_E0 + 7, 7, LT_FLT)   /* (the 303's ENV DEC ACC SLD; FLT: its CUT, eng_acid.c) */
#define E1C_CZ E1C(E1K_AL(1), P_E0 + 1, 1, E1K_AL(2), P_E0 + 2, 2, \
    E1K_AL(4), P_E0 + 4, 4, E1K_PIT, P_E1PIT, E1L_PIT, LT_E2)   /* (no filter: DCW) */
#define E1C_JUNO E1C(E1K_FLT, P_ED_FLT, E1L_FLT, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_SHP, P_ED_SHP, 1, E1K_MV(5), P_E1X + 0, 5, LT_FLT)
#define E1C_NOISE E1C(E1K_FLT, P_ED_FLT, 2, E1K_PIT, P_E1PIT, E1L_PIT, \
    E1K_SHP, P_ED_SHP, 1, E1K_MV(3), P_E1X + 0, 3, LT_FLT)
typedef struct {
    uint8_t c[4];                                     /* the cards: E1K_* */
    uint8_t id[4];                                    /* the value each keeps (an alias: its EDIT value) */
    uint8_t l[4];                                     /* their names (0..7: EDIT value n's, E1L_FLT E1L_PIT E1L_SHP) */
    uint8_t e2;                                       /* ENV 2's default DEST A (LT_*) */
} env1_cards_t;
#define E1C(c0, i0, l0, c1, i1, l1, c2, i2, l2, c3, i3, l3, e2) {{c0, c1, c2, c3}, {i0, i1, i2, i3}, {l0, l1, l2, l3}, e2}
#define E1C_(u, N, fb, s) FIF(FELUCCA_ENG_##N)(E1C_##N,)
static const env1_cards_t ENV1_CARDS[NENGINES] = {ENGINE_LIST(E1C_)};   /* by engine slot (ENGINES[]) */

/* engine slot e's card k: its kind (E1K_*) */
AINL uint32_t env1_card(uint32_t e, uint32_t k) { return ENV1_CARDS[e].c[k & 3u]; }   /* (e < NENGINES) */
/* the value card k of engine e keeps (P_ED_FLT, P_ED_SHP, P_E1PIT, P_E1X ..; an alias: its EDIT value) */
AINL uint32_t env1_id(uint32_t e, uint32_t k) { return ENV1_CARDS[e].id[k & 3u]; }
/* ENV 2's default DEST A on engine e (a new sound, INIT, a factory preset with ENV 2 unused) */
AINL uint32_t env2_def_dest(uint32_t e) { return ENV1_CARDS[e].e2; }
#endif
