/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The model operations both UIs and the core call (docs/UI-FEASIBILITY.md section 4.1, step 2): what loading a
 * sound, an engine or the power-on defaults does to a track, the curated preset list (BANK) the PRESETS knob
 * browses, and the core's forward declarations the UIs need. Moved verbatim from ui/sloop/ui.c (2026-10-08) so
 * that a build with the other UI (FELUCCA_UI, ui/optimist) has them too; felucca.c includes it before the UI.
 * Nothing here touches a UI's state: what must redraw says so through sync_reload (the editor) or its caller. */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "OPTIMIST 0.1"  /* the FM-1 firmware Optimist (based on SLOOP and Felucca) */
#endif
static void project_save(uint32_t slot);
static void arrangement_save(void);
static void panel_setup(void);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
#if FELUCCA_SL24_SAFE
static uint32_t project_state(uint32_t s);   /* project.c sl24_guard.c */
#if SL24_AUTO
static void sl24_auto_import(void);
#endif
#endif
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
#if FELUCCA_NATIVE_BANKS                     /* the engine's own collection after the user presets: nbank.c */
#define NB_LIST (NENGINES + 1u)               /* preset_at: an entry of it */
static uint32_t nb_list(uint32_t *rank, uint32_t n, uint32_t *at);
static void nb_row(uint32_t k, char *tag, char *nm);
static void nb_load(uint32_t k);
#endif
#if FELUCCA_SNAPSHOTS                        /* SAVE > SNAPSHOT: snapshots.c */
static uint32_t sn_ui_n(void);
static void sn_ui_label(char *b, uint32_t i);
static uint32_t sn_ui_row(uint32_t i, char *name, uint32_t *bytes);
static uint32_t sn_ui_free(void);
static int sn_ui_fits(void);
static uint32_t sn_ui_sig(void);
static void sn_ui(uint32_t op, uint32_t i);
#endif
static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, UP_SLOTS = none */
{
    return t->user && up_used(t->user - 1u) ? t->user - 1u : UP_SLOTS;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

/* ------------------------------------------------------- track setup --- */
/* LIVE: no factory sequence patterns. Loading a sound (factory or user preset) never
 * writes the sequencer: every pattern is the one the player records or enters. */

/* the parts' sounds at power-on (engine, preset): bass, pad, lead */
static const uint8_t TRK_DEF[NPART][2] = {{0, 0}, {1, 0}, {4, 5}};   /* ANALOG 808 BOOM, DIGITAL RHODES, SAMPLE LOFI FLUTE */
/* part i's default engine (slot; TRK_DEF holds UIDs: a fallback when not built) and preset */
static uint32_t trk_def_engine(uint32_t i)
{
    return i >= NPART ? 0u : ENG_ALL ? TRK_DEF[i][0] : eng_slot(TRK_DEF[i][0]);
}
static uint32_t trk_def_preset(uint32_t i) { return ENG_ALL || eng_built(TRK_DEF[i][0]) ? TRK_DEF[i][1] : 0u; }   /* (i < NPART) */

static int seq_is_empty(const track_t *t) { return track_empty(t); }

static void track_defaults_steps(track_t *t) { steps_clear(t); }

/* what loading a sound (factory or user preset) leaves alone: the mix (LEVEL, PAN, MUTE, the FX bypass:
 * the TRACKS faders, GLO + key), the pattern parameters (LEN, DIV, SWING, GATE) and the key the part plays
 * in (ROOT, SCALE, QNT, CHORD: the song's; SCL + key sets the root of every part). The SLICER is
 * part of the sound: a factory preset turns it OFF (its defaults), a user preset brings its own */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || i == P_FXOFF || (i >= P_SLEN && i <= P_SGATE) ||
           (i >= P_ROOT && i <= P_QUANT) || i == P_CHORD
#if P_TAIL
           || i >= P_ENG_END                         /* (SLOOP 2.4's FILT, STRUM, VLEAD: the track's, as 2.4 keeps them;
                                                      * the COMP insert's amount: the mix, D8) */
#endif
        ;
}

/* INIT: an engine with no factory preset this build can play (none in its table, or none whose sample set is
 * built) has this one entry on the PRESETS list, and in the web editor's list: the engine's defaults, as a fresh
 * track on it. No data: made from the engine's and the track's defaults */
#define PRESET_INIT 0xFFu
static uint32_t eng_first_playable(const engine_t *e)    /* its first playable preset, else PRESET_INIT */
{
    uint32_t k;
    for (k = 0; k < e->npresets; k++)
        if (preset_playable(e, k))
            return k;
    return PRESET_INIT;
}

/* preset pi of the engine the track asked for: the whole sound (not the pattern parameters); PRESET_INIT, or any pi
 * on an engine without a playable preset: INIT */
static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i;
    if (is_drum(t))
        return;
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0;
    if (t == TSEL)
        sync_reload = 1;
    if (pi == PRESET_INIT || eng_first_playable(e) == PRESET_INIT) {
        t->preset = 0;
        for (i = 0; i < P_E0; i++)                    /* INIT: the track's sound and the engine's values to their */
            if (!param_kept(i))                       /* defaults */
                t->p[i] = TP[i].def;
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = e->edit[i].def;
        return;
    }
    pi %= e->npresets;
    t->preset = (uint8_t)pi;
    if (ENG_IS(e, FM6))
        fm6_cur[trk_index(t) % NPART] = 0;           /* its VOICE afresh: edits of the buffer go */
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i))
            t->p[i] = TP[i].def;                     /* sounds the same after any edit (not the pattern, not the mix) */
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)e->presets[pi].e[i];
    t->p[P_ATK] = e->presets[pi].env[0];
    t->p[P_DEC] = e->presets[pi].env[1];
    t->p[P_SUS] = e->presets[pi].env[2];
    t->p[P_REL] = e->presets[pi].env[3];
    t->p[P_ED_FLT] = e->presets[pi].fenv;
    t->p[P_ED_FX] = preset_trim(eng_uid(t->eng_req % NENGINES), pi);  /* level-matched (tools/level_presets.py) */
    t->p[P_VOICE] = e->presets[pi].mono ? V_LEGATO : V_POLY;   /* mono presets keep the legato feel */
    {   /* the rest of the patch: sends, arpeggiator (never a pattern: LIVE) */
        static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
        const preset_t *pr = &e->presets[pi];
        for (i = 0; i < 4u; i++) {
            t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
            t->p[P_AMODE + i] = (int16_t)(pr->arp[i] ? pr->arp[i] - 1 : TP[P_AMODE + i].def);
        }
        preset_extras(t->p, pr);                     /* glide, pitch / LFO modulation, voice mode */
        analog2_extras(t->p, e, pi);                 /* ANALOG 2's own values */
    }
}

/* the engine's defaults and its first preset. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    if (is_drum(t))
        return;
    fm1_irq_off();
#if FELUCCA_PLOCK
    locks_restore(t);                                 /* (a lock in force: its base first, then the new sound) */
#endif
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_to(t, eng_first_playable(e));      /* (its first preset; none: INIT) */
    fm1_irq_on();
}

static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
#if P_TAIL
    for (i = P_ENG_END; i < P_COUNT; i++)            /* (SLOOP 2.4's FILT, STRUM, VLEAD; the COMP insert's amount) */
        t->p[i] = TP[i].def;
#endif
    track_defaults_steps(t);
}

/* the factory presets as one list by kind (basses, keys, organs, pads, leads, plucks and bells, stabs,
 * the rest), then the used user presets: the PRESETS knob and the PRESETS page browse it. By name: an
 * engine's preset table may change order; tests/ui_pages_test.c checks every preset is here once */
enum { BK_BASS, BK_KEYS, BK_ORGAN, BK_PAD, BK_LEAD, BK_PLUCK, BK_STAB, BK_FX };
static const char *const BANK_KIND[] = {"BASS", "KEYS", "ORGN", "PAD", "LEAD", "PLCK", "STAB", "FX"};
static const struct { uint8_t kind, e; const char *name; } BANK[] = {
    {BK_BASS, 0, "808 BOOM"}, {BK_BASS, 0, "808 DIRTY"}, {BK_BASS, 0, "808 SLIDE"}, {BK_BASS, 0, "SUB BASS"},
    {BK_BASS, 0, "PLUGG BASS"}, {BK_BASS, 0, "REESE"}, {BK_BASS, 0, "WOBBLE"}, {BK_BASS, 0, "ACID 303"},
    {BK_BASS, 1, "FM BASS"}, {BK_BASS, 2, "CZ BASS"}, {BK_BASS, 6, "FAT BASS"}, {BK_BASS, 0, "FUNK BASS"},
    {BK_BASS, 5, "WOW BASS"}, {BK_BASS, 3, "GB BASS"}, {BK_BASS, 4, "UP BASS"}, {BK_BASS, 4, "DEEP BASS"},
#if FELUCCA_ANALOG2
    {BK_BASS, 0, "LP24 BASS"},
#endif
    {BK_BASS, ENG_UID_FM6, "SOLID BASS"}, {BK_BASS, ENG_UID_FM6, "SAW BASS"},
    {BK_KEYS, 1, "RHODES"}, {BK_KEYS, 1, "DX RHODES"}, {BK_KEYS, 1, "WURLI"}, {BK_KEYS, 1, "M1 PIANO"},
    {BK_KEYS, 1, "AFRO KEYS"}, {BK_KEYS, 4, "GRAND PNO"}, {BK_KEYS, 4, "DUSTY PNO"}, {BK_KEYS, 4, "LOFI KEYS"}, {BK_KEYS, 2, "SOFT KEYS"},
    {BK_KEYS, 1, "CLAV"},
    {BK_KEYS, ENG_UID_FM6, "TINE EP"}, {BK_KEYS, ENG_UID_FM6, "CLAVINET"},
    {BK_ORGAN, 7, "SOUL ORGAN"}, {BK_ORGAN, 7, "GOSPEL"}, {BK_ORGAN, 7, "JAZZ ORGAN"}, {BK_ORGAN, 7, "DIRTY B3"},
    {BK_ORGAN, 7, "HOUSE ORGN"}, {BK_ORGAN, ENG_UID_FM6, "DRAWBARS"},
    {BK_PAD, 0, "WARM PAD"}, {BK_PAD, 6, "SAW PAD"}, {BK_PAD, 1, "GLASS PAD"}, {BK_PAD, 0, "DARK STR"},
    {BK_PAD, 2, "CZ STRING"}, {BK_PAD, 0, "ATMOS PAD"}, {BK_PAD, 8, "LOFI CLOUD"}, {BK_PAD, 8, "VIBE HAZE"},
    {BK_PAD, 5, "CHOIR AAH"}, {BK_PAD, 5, "SOUL OOH"}, {BK_PAD, ENG_UID_SUPER, "SUPER PAD"},
    {BK_PAD, ENG_UID_FM6, "STRINGS"}, {BK_PAD, ENG_UID_FM6, "FM GLASS"},
    {BK_LEAD, 0, "SUPERSAW"}, {BK_LEAD, ENG_UID_SUPER, "SUPER LEAD"}, {BK_LEAD, 0, "G-FUNK LD"}, {BK_LEAD, 6, "SYNC LEAD"},
    {BK_LEAD, 6, "HOOVER"}, {BK_LEAD, ENG_UID_SUPER, "HOOVER SAW"},
#if FELUCCA_ANALOG2
    {BK_LEAD, 0, "SYNC SWEEP"}, {BK_LEAD, 0, "FIFTH LEAD"},
#endif
    {BK_LEAD, 5, "TALKBOX"}, {BK_LEAD, 3, "GAME LEAD"}, {BK_LEAD, 4, "LOFI FLUTE"}, {BK_LEAD, 8, "FLUTE DUST"},
    {BK_LEAD, ENG_UID_FM6, "FM SYNC LD"}, {BK_LEAD, ENG_UID_FM6, "FLUTE"},
    {BK_PLUCK, 0, "TRAP PLUCK"}, {BK_PLUCK, ENG_UID_SUPER, "SUPER PLCK"}, {BK_PLUCK, 2, "RESO PLUCK"}, {BK_PLUCK, 1, "PLUGG BELL"}, {BK_PLUCK, 1, "TRAP BELL"},
    {BK_PLUCK, 1, "MUSIC BOX"}, {BK_PLUCK, 1, "KALIMBA"}, {BK_PLUCK, 1, "MARIMBA"}, {BK_PLUCK, 4, "VIBES"},
    {BK_PLUCK, 3, "8BIT ARP"},
    {BK_PLUCK, ENG_UID_FM6, "BELLS"}, {BK_PLUCK, ENG_UID_FM6, "FM MARIMBA"}, {BK_PLUCK, ENG_UID_FM6, "FM KALIMBA"},
    {BK_PLUCK, ENG_UID_FM6, "STEEL DRUM"}, {BK_PLUCK, ENG_UID_FM6, "TUBULAR"}, {BK_PLUCK, ENG_UID_FM6, "HARP"},
    {BK_STAB, 6, "MIN STAB"}, {BK_STAB, 6, "MIN7 STAB"}, {BK_STAB, 6, "RAVE STAB"}, {BK_STAB, 6, "DUB CHORD"}, {BK_STAB, ENG_UID_SUPER, "SUPER CHRD"},
    {BK_STAB, 0, "SYN BRASS"}, {BK_STAB, 2, "CZ BRASS"}, {BK_STAB, 4, "HORN STAB"}, {BK_STAB, 4, "STRING STB"},
    {BK_STAB, ENG_UID_FM6, "BRASS SECT"},
    {BK_FX, 4, "SCRATCH"}, {BK_FX, 4, "GM KIT"},
#if FELUCCA_ENG_PHYS
    {BK_PLUCK, ENG_UID_PHYS, "BELL TREE"}, {BK_PLUCK, ENG_UID_PHYS, "WOOD MRMBA"}, {BK_PLUCK, ENG_UID_PHYS, "PLUCK"},
    {BK_PLUCK, ENG_UID_PHYS, "THUMB PNO"}, {BK_PLUCK, ENG_UID_PHYS, "SYMP HARP"}, {BK_PAD, ENG_UID_PHYS, "BOWED METAL"},
    {BK_PAD, ENG_UID_PHYS, "DRONE STRING"}, {BK_FX, ENG_UID_PHYS, "HAND DRUM"}, {BK_FX, ENG_UID_PHYS, "TOMS"},
#endif
#if FELUCCA_ENG_ACID
    {BK_BASS, ENG_UID_ACID, "ACID LINE"}, {BK_BASS, ENG_UID_ACID, "ACID SQR"}, {BK_BASS, ENG_UID_ACID, "ACID RAGE"},
    {BK_BASS, ENG_UID_ACID, "ACID DUB"},
#endif
#if FELUCCA_ENG_SLICE
    {BK_FX, ENG_UID_SLICE, "BREAK 16"}, {BK_FX, ENG_UID_SLICE, "CHOP 8"}, {BK_FX, ENG_UID_SLICE, "REVERSE"},
    {BK_FX, ENG_UID_SLICE, "USR SLICE"},
#endif
#if FELUCCA_ENG_CZ
    {BK_BASS, ENG_UID_CZ, "PD BASS"}, {BK_LEAD, ENG_UID_CZ, "RESO SWEEP"}, {BK_PLUCK, ENG_UID_CZ, "GLASS BELL"},
    {BK_STAB, ENG_UID_CZ, "WIRE BRASS"}, {BK_PAD, ENG_UID_CZ, "SOFT PAD"}, {BK_FX, ENG_UID_CZ, "NOISE BREATH"},
    {BK_KEYS, ENG_UID_CZ, "PULSE KEYS"}, {BK_LEAD, ENG_UID_CZ, "CZ INIT"},
#endif
#if GR_FALLBACK                                     /* (GRAIN without its presets' sets: eng_grain.c) */
    {BK_PAD, 8, "GRAIN PAD"},
#endif
};
#define NBANK_ALL (sizeof BANK / sizeof BANK[0])
/* the list as this build has it: the entries whose engine is built and whose preset exists (a reduced build:
 * registry.h; a sample set left out takes its presets along). bank_ix: list position -> BANK entry */
static uint8_t bank_pi[NBANK_ALL];                   /* the preset index of each entry in its engine */
static uint8_t bank_ix[NBANK_ALL], bank_n;
static uint8_t bank_init[NENGINES], bank_ni;          /* after them: INIT of each engine with no entry (slots) */
static uint8_t bank_ready;
static void bank_resolve(void)
{
    uint32_t i, k;
    bank_n = bank_ni = 0;
    for (i = 0; i < NBANK_ALL; i++) {
        const engine_t *e;
        bank_pi[i] = 0xFF;
        if (!eng_built(BANK[i].e))
            continue;
        e = ENGINES[eng_slot_built(BANK[i].e)];
        for (k = 0; k < e->npresets; k++)
            if (str_eq(e->presets[k].name, BANK[i].name))
                bank_pi[i] = (uint8_t)k;
        if (bank_pi[i] != 0xFF && preset_playable(e, bank_pi[i]))
            bank_ix[bank_n++] = (uint8_t)i;
    }
    for (i = 0; i < NENGINES; i++) {
        for (k = 0; k < bank_n && eng_slot_built(BANK[bank_ix[k]].e) != i; k++)
            ;
        if (k == bank_n)
            bank_init[bank_ni++] = (uint8_t)i;
    }
    bank_ready = 1;
}
#define NBANK ((uint32_t)bank_n)                     /* the factory entries */
#define NLIST (NBANK + bank_ni)                      /* and the INIT ones: then the user presets */
static uint32_t preset_pos(uint32_t *total)          /* list index of the selected track's preset */
{
    uint32_t i, cur = 0;
    if (!bank_ready)
        bank_resolve();
    for (i = 0; i < NBANK; i++)
        if (eng_slot_built(BANK[bank_ix[i]].e) == TSEL->eng_req && bank_pi[bank_ix[i]] == TSEL->preset)
            cur = i;
    for (i = 0; i < bank_ni; i++)
        if (bank_init[i] == TSEL->eng_req)
            cur = NBANK + i;
    if (user_of(TSEL) < UP_SLOTS)
        cur = NLIST + up_rank(user_of(TSEL));
    *total = NLIST + up_count();
#if FELUCCA_NATIVE_BANKS
    {
        uint32_t at, n = nb_list(&i, ~0u, &at);           /* (the engine's own collection: nbank.c) */
        if (i < n)
            cur = *total + i;
        *total += n;
    }
#endif
    return cur;
}

/* list index n (< total) -> engine slot, *k its preset (PRESET_INIT: INIT); NENGINES = user preset, *k its slot */
static uint32_t preset_at(uint32_t n, uint32_t *k)
{
    if (!bank_ready)
        bank_resolve();
#if FELUCCA_NATIVE_BANKS
    if (n >= NLIST + up_count()) {
        uint32_t r;
        nb_list(&r, n - NLIST - up_count(), k);
        return NB_LIST;
    }
#endif
    if (n >= NLIST) {
        *k = up_nth(n - NLIST);
        return NENGINES;
    }
    if (n >= NBANK) {
        *k = PRESET_INIT;
        return bank_init[n - NBANK];
    }
    *k = bank_pi[bank_ix[n]];
    return eng_slot_built(BANK[bank_ix[n]].e);
}
static const char *preset_kind(uint32_t n) { return n < NBANK ? BANK_KIND[BANK[bank_ix[n]].kind] : n < NLIST ? "INIT" : "USER"; }
/* the name of preset k of engine slot e as the lists show it */
static const char *preset_name(uint32_t e, uint32_t k)
{
    return k == PRESET_INIT || !ENGINES[e]->npresets ? "INIT" : ENGINES[e]->presets[k % ENGINES[e]->npresets].name;
}
