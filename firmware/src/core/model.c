/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The model operations both UIs and the core call: what loading a
 * sound, an engine or the power-on defaults does to a track, the curated preset list (BANK) the PRESETS knob
 * browses, and the core's forward declarations the UIs need. Moved verbatim from ui/sloop/ui.c (2026-10-08) so
 * that a build with the other UI (FELUCCA_UI, ui/optimist) has them too; felucca.c includes it before the UI.
 * Nothing here touches a UI's state: what must redraw says so through sync_reload (the editor) or its caller. */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "OPTIMIST 0.2"  /* the FM-1 firmware Optimist (based on SLOOP and Felucca) */
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
#define FAV_SAVE_LATER() (settings_later = 1)       /* (panel.c: written once the audio is quiet, project.c sections_flush) */
#include "favorites.c"                      /* FELUCCA_FAVORITES: the marked sounds (fav, fav_list) */
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
#if FELUCCA_NB_LIST                     /* the engine's own collection after the user presets: nbank.c */
#define NB_LIST (NENGINES + 1u)               /* preset_at: an entry of it */
static uint32_t nb_list(uint32_t *rank, uint32_t n, uint32_t *at);
static uint32_t nb_list_of(uint32_t kind, uint32_t *rank, uint32_t n, uint32_t *at);
static uint32_t nb_kind_at(uint32_t i);
static uint32_t nb_engine_slot(uint32_t kind);
#define NB_NK 2u
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

/* the drum track of a default project (power-on with nothing stored, TOOLS > NEW): no hit, its ACCENT lane four on
 * the floor, a NORM accent on every beat (every 4th step at the default DIV 1/16, through the whole pattern; the user,
 * 2026-10-22). Nothing sounds: the project is still empty for the first take and the click (seq.c track_empty) */
static void drum_defaults_steps(track_t *t)
{
    track_defaults_steps(t);
#if FELUCCA_DRUM_ACCENT
    {
        uint32_t k;
        for (k = 0; k < NSTEP; k += 4u)
            dstep_set(&t->dstep[k], DRUM_ACC, LV_NORM, 0);
    }
#endif
}

/* what loading a sound (factory or user preset) leaves alone: the mix (LEVEL, PAN, MUTE, the FX bypass:
 * the TRACKS faders, GLO + key), the pattern parameters (LEN, DIV, SWING, GATE) and the key the part plays
 * in (ROOT, SCALE, QNT, CHORD: the song's; SCL + key sets the root of every part). The SLICER is
 * part of the sound: a factory preset turns it OFF (its defaults), a user preset brings its own */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || i == P_FXOFF || (i >= P_SLEN && i <= P_SGATE) ||
           (i >= P_ROOT && i <= P_QUANT) || i == P_CHORD
           || (i >= P_ENG_END && (i < P_E1PIT || i > P_SLAST))   /* (SLOOP 2.4's FILT, STRUM, VLEAD: the track's, as
                                                      * 2.4 keeps them; the COMP insert's amount: the mix, D8; not ENV 1
                                                      * MOD's depths nor ENV2's slots: the sound's) */
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
    sound_change(t);                                  /* (SLOOP 2.6: what sounds fades out with the old sound, no click) */
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0;
    if (t == TSEL)
        sync_reload = 1;
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults (INIT, a preset: it */
        if (!param_kept(i))                           /* sounds the same after any edit; not the pattern, not the mix) */
            t->p[i] = TP[i].def;
    if (pi == PRESET_INIT || eng_first_playable(e) == PRESET_INIT) {
        t->preset = 0;                                /* INIT: the engine's values to their defaults too */
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = e->edit[i].def;
        for (i = P_E1PIT; i <= P_SLAST; i++)          /* ENV 1 MOD's depths at 0, ENV2's slots: DEST A the filter, FLT B */
            t->p[i] = TP[i].def;
        env2_dest_def(t->p, t->eng_req);
        return;
    }
    pi %= e->npresets;
    t->preset = (uint8_t)pi;
    if (ENG_IS(e, FM6))
        fm6_cur[trk_index(t) % NPART] = 0;           /* its VOICE afresh: edits of the buffer go */
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
            t->p[P_AMODE + i] = TP[P_AMODE + i].def;    /* (a preset's ARP: pairs in its x, preset_extras) */
        }
        preset_extras(t->p, pr);                     /* glide, pitch / LFO modulation, voice mode, ARP */
        analog2_extras(t->p, e, pi);                 /* ANALOG 2's own values */
        for (i = P_E1PIT; i <= P_E1Z; i++)            /* ENV 1 MOD's PIT and moves (the tables have none) */
            t->p[i] = 0;
        for (i = P_NLAST + 1u; i <= P_SLAST; i++)     /* FLT B (ANALOG 2; the tables have none): OFF */
            t->p[i] = TP[i].def;
        (void)env_load(t->p);                        /* ENV2's depths of the tables -> its slots */
        env2_dest_def(t->p, t->eng_req);
    }
}

/* the engine's defaults and its first preset. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
/* the engine's name as shown: NONE for a silent placeholder (core.h) */
static const char *trk_eng_name(const track_t *t) { return t->silent ? "NONE" : ENGINES[t->eng_req % NENGINES]->name; }

/* a part dropped to make memory room (the user, 2026-10-25): a silent placeholder, its sequence kept, its memory
 * given back at the switch (an engine that keeps nothing in the arena), until an engine is picked (set_engine_of) */
static void part_drop(track_t *t)
{
    if (is_drum(t) || (uint32_t)(t - trk) >= NPART)
        return;
    sound_change(t);                                  /* (what sounds fades out, no click) */
    fm1_irq_off();
    t->silent = 1;
    t->eng_req = (uint8_t)eng_silent_slot();
    fm1_irq_on();
}

/* What to drop when memory is short (the user, 2026-10-25: refuse, and propose what to drop; the UI asks with its one
 * confirm, op_input.c mem_tick). mem_offer: the next part to propose, first those arn_drop_for named, then any other part
 * whose engine keeps memory, never one already asked, then (FELUCCA_FX_ARENA) the effects' lines placed, the largest
 * first (NPART + fx_arena.c FXM_*); 0 none left. mem_drop: that part a silent placeholder (its memory free once its
 * switch is done), or that effect bypassed as when refused (its line given back two blocks later). mem_retry, each frame: 0 nothing to do or still waiting for a dropped part's switch,
 * 1 the change went through, -1 still no room (offer the next) */
static int mem_offer(uint32_t *out)
{
    uint32_t k, pass;
    for (pass = 0; pass < 2u; pass++)
        for (k = 0; k < NPART; k++) {
            uint32_t named = part_mem_refused.drop > 0 && ((uint32_t)part_mem_refused.drop >> k & 1u);
            if (k == part_mem_refused.part || (part_mem_refused.asked >> k & 1u) || trk[k].silent ||
                !part_mem_bytes(trk[k].engine) || (pass == 0u && !named))
                continue;
            *out = k;
            part_mem_refused.asked |= (uint8_t)(1u << k);
            return 1;
        }
#if FXM_NEED                                          /* every part offered: then the effects' lines (the user, 2026-10-25) */
    if (fxm_offer(part_mem_refused.asked >> NPART, &k)) {
        *out = NPART + k;                             /* (NPART + fx_arena.c's FXM_*: "DROP REV 32 KB") */
        part_mem_refused.asked |= (uint8_t)(1u << (NPART + k));
        return 1;
    }
#endif
    return 0;
}

static void part_drop(track_t *t);

#define MEM_W_FX 0x80u                                /* (part_mem_refused.waiting: an effect's line going back) */
static void mem_drop(uint32_t k)
{
#if FXM_NEED
    if (k >= NPART) {                                 /* an effect's line: bypassed as when refused, its block back */
        fxm_drop(k - NPART);
        part_mem_refused.waiting |= MEM_W_FX;         /* (mem_retry waits while it goes back: fxm_pending) */
        return;
    }
#endif
    if (k >= NPART)
        return;
    part_drop(&trk[k]);
    part_mem_refused.waiting |= (uint8_t)(1u << k);
}

static void set_engine_of(track_t *t, uint32_t ei);

static int mem_retry(void)
{
    uint32_t k, p = part_mem_refused.part, e = part_mem_refused.eng;
    if (part_mem_refused.on && fxm_pending()) {        /* (an effect's line no longer used, on its way back: wait */
        part_mem_refused.waiting |= MEM_W_FX;           /* for it, then try again: fx/fx_arena.c) */
        return 0;
    }
    if (!part_mem_refused.on || !part_mem_refused.waiting)
        return 0;
    for (k = 0; k < NPART; k++)
        if ((part_mem_refused.waiting >> k & 1u) && trk[k].engine != trk[k].eng_req)
            return 0;                                 /* (a dropped part still fading: its memory not free yet) */
    part_mem_refused.waiting = 0;
    if (part_mem_reserve(p, e))
        return -1;
    part_mem_refused.on = 0;
    set_engine_of(&trk[p], e);                        /* (the edit asked for: the engine and its first preset; a
                                                       * load's refusals: mem_load_settle) */
    return 1;
}

#if !ARN_FIXED
/* A load's refusals (engines.c part_mem_refused.lmask; the user, 2026-10-25: "the list page as designed"): every part a
 * project's load could not place is a silent placeholder with the project's sound, its engine wanted (leng). The UI's
 * list (ui/optimist/op_memlist.c) shows them and what can be dropped: the parts that hold memory, then the effects' lines
 * placed (FELUCCA_FX_ARENA; the parts first, the user, 2026-10-25). An item is a part k (k < NPART) or an effect's line
 * NPART + fx_arena.c FXM_*; a set of them a bit mask by item.
 *   mem_load_settle   each frame: 0 waiting (a switch or an effect's line on its way back), 1 nothing refused (any part
 *                     that finds room now on its engine: the other parts' old blocks free once they switched), -1
 *                     still short: the list
 *   mem_load_items    the items that can be dropped, in the list's order (parts, then REV DLY CHO): how many
 *   mem_item_bytes    what dropping an item gives back
 *   mem_load_fits     1: with the items of sel dropped, every refused part placed: a trial on the arena itself (the
 *                     dropped parts' blocks idle as after their switch, the lines given back, then the reservations
 *                     the settle makes, in its order), put back as it was: FITS on the screen is the commit's outcome
 *   mem_load_commit   the items of sel dropped (parts placeholders, lines bypassed as when refused); the settle places
 *                     the refused parts once their memory is free
 *   mem_load_cancel   HOME: the refused parts stay placeholders (an engine picked later ends one) */
static uint32_t mem_load_part(uint32_t k)             /* part k can be dropped: it plays an engine that holds memory */
{
    return k < NPART && !(part_mem_refused.lmask >> k & 1u) && !trk[k].silent && part_mem_bytes(trk[k].engine) &&
           arn_ptr(k) && !arn_cur[k].idle;
}
static uint32_t mem_item_bytes(uint32_t it)
{
#if FXM_NEED
    if (it >= NPART)
        return it - NPART < 3u ? arn_cur[FXM_K(it - NPART)].len : 0u;
#endif
    return it < NPART ? arn_cur[it].len : 0u;
}
static uint32_t mem_load_items(uint8_t *it)
{
    uint32_t k, n = 0;
#if FXM_NEED
    static const uint8_t SEND_ORD[3] = {FXM_REV, FXM_DLY, FXM_CHO};   /* (one send order everywhere: REV DLY CHO) */
#endif
    for (k = 0; k < NPART; k++)
        if (mem_load_part(k))
            it[n++] = (uint8_t)k;
#if FXM_NEED
    for (k = 0; k < 3u; k++)                          /* (fxm_offer's: a send line placed, its bus' own) */
        if (FXM_BYTES[SEND_ORD[k]] && fxm_st[SEND_ORD[k]] == FXM_ON && fxm_heard(SEND_ORD[k]))
            it[n++] = (uint8_t)(NPART + SEND_ORD[k]);
#endif
    return n;
}
static uint32_t mem_load_need(void)                   /* the bytes the refused parts want */
{
    uint32_t k, n = 0;
    for (k = 0; k < NPART; k++)
        if (part_mem_refused.lmask >> k & 1u)
            n += part_mem_bytes(part_mem_refused.leng[k]);
    return n;
}
static uint32_t mem_load_free(void)                   /* the arena's room now, every gap (the idle blocks free) */
{
    uint32_t k, used = 0;
    for (k = 0; k < ARN_PARTS; k++) {
        uint32_t c = arn_cur[k].idle ? 0u : arn_cur[k].len, x = arn_has_next[k] ? arn_next[k].len : 0u;
        used += arn_has_next[k] && arn_next[k].off == arn_cur[k].off ? (c > x ? c : x) : c + x;
    }
    return used < ARN_BYTES ? ARN_BYTES - used : 0u;
}
static int mem_load_place(void)                       /* the refused parts' reservations, by part; 0 every one placed */
{
    uint32_t k;
    int r = 0;
    for (k = 0; k < NPART; k++)
        if ((part_mem_refused.lmask >> k & 1u) && arn_reserve(k, part_mem_bytes(part_mem_refused.leng[k])))
            r = -1;
    return r;
}
static int mem_load_fits(uint32_t sel)
{
    arn_blk_t cur[ARN_PARTS], nxt[ARN_PARTS];
    uint8_t hn[ARN_PARTS];
    uint32_t k;
    int ok;
    fm1_irq_off();
    memcpy(cur, arn_cur, sizeof cur), memcpy(nxt, arn_next, sizeof nxt), memcpy(hn, arn_has_next, sizeof hn);
    for (k = 0; k < ARN_PARTS; k++)
        if (sel >> k & 1u) {
            if (k < NPART)
                arn_cur[k].idle = 1, arn_has_next[k] = 0;    /* (a part dropped: arn_rest at its switch) */
            else
                arn_cur[k].len = 0, arn_has_next[k] = 0;     /* (a line dropped: arn_give two blocks later) */
        }
    ok = !mem_load_place();
    memcpy(arn_cur, cur, sizeof cur), memcpy(arn_next, nxt, sizeof nxt), memcpy(arn_has_next, hn, sizeof hn);
    fm1_irq_on();
    return ok;
}
static int mem_load_settle(void)
{
    uint32_t k;
    if (!part_mem_refused.lmask)
        return 1;
    if (fxm_pending())
        return 0;                                     /* (an effect's line on its way back: its room after) */
    for (k = 0; k < NPART; k++)
        if (trk[k].engine != trk[k].eng_req || trk[k].xf_on)
            return 0;                                 /* (a part still switching: its old block not free yet) */
    fm1_irq_off();
    for (k = 0; k < NPART; k++)                       /* (each that fits now, by part: the rest stay refused) */
        if ((part_mem_refused.lmask >> k & 1u) && !arn_reserve(k, part_mem_bytes(part_mem_refused.leng[k]))) {
            part_mem_refused.lmask &= (uint8_t)~(1u << k);
            trk[k].eng_req = part_mem_refused.leng[k];    /* (the project's sound as loaded, now on its engine) */
            trk[k].silent = 0;
        }
    fm1_irq_on();
    return part_mem_refused.lmask ? -1 : 1;
}
static void mem_load_commit(uint32_t sel)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if ((sel >> k & 1u) && mem_load_part(k))
            part_drop(&trk[k]);                       /* (a placeholder: its block idle at its switch) */
#if FXM_NEED
    for (k = 0; k < 3u; k++)
        if (sel >> (NPART + k) & 1u)
            fxm_drop(k);                              /* (bypassed as when refused, given back two blocks later) */
#endif
}
static void mem_load_cancel(void)
{
    fm1_irq_off();
    part_mem_refused.lmask = 0;
    fm1_irq_on();
}
#endif

static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    if (is_drum(t))
        return;
    if ((uint32_t)(t - trk) < NPART && part_mem_reserve((uint32_t)(t - trk), ei % NENGINES))
        return;                                       /* no room in the arena: refused (part_mem_refused: what to drop) */
    fm1_irq_off();
#if FELUCCA_MOTION || FELUCCA_PLOCK
    motion_restore(t);                                /* (the automation's values back first, then the new sound: it is
                                                       * the patch now, STOP keeps it) */
#endif
    sound_change(t);                                  /* (SLOOP 2.6: the sound as it is, before anything new is written) */
    t->eng_req = (uint8_t)(ei % NENGINES);
    t->silent = 0;                                    /* (an engine picked: the placeholder plays again) */
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
    for (i = P_ENG_END; i < P_COUNT; i++)            /* (SLOOP 2.4's FILT, STRUM, VLEAD; the COMP insert's amount) */
        t->p[i] = TP[i].def;
    if (is_drum(t))
        t->p[P_DACC] = DACC_DEF;                     /* (the ACCENT amount: core.h; the kit is the caller's) */
    for (i = 0; is_drum(t) && i < GR_KIT_N; i++)
        if (DRUM_GR_OF(P_GRX + i))
            t->p[P_GRX + i] = DRUM_GR_OF(P_GRX + i)->def;   /* (the kit's GRIDS: params.c) */
    if (is_drum(t))
        drum_defaults_steps(t);                      /* (no hit; the ACCENT lane four on the floor) */
    else
        track_defaults_steps(t);
}

/* the factory presets as one list by kind (basses, keys, organs, pads, leads, plucks and bells, stabs,
 * the rest), then the used user presets: the PRESETS knob and the PRESETS page browse it. By name: an
 * engine's preset table may change order; tests/ui_pages_test.c checks every preset is here once */
enum { BK_BASS, BK_KEYS, BK_ORGAN, BK_PAD, BK_LEAD, BK_PLUCK, BK_STAB, BK_FX };
static const char *const BANK_KIND[] = {"BASS", "KEYS", "ORGN", "PAD", "LEAD", "PLCK", "STAB", "FX"};
/* B(N, kind, uid, name): an entry of engine N (its UID): nothing when the engine is not built, so a reduced build keeps
 * neither the name nor the entry of a preset it cannot play (bank_resolve skipped them at run time). SUPER's entries:
 * ANALOG's swarm plays them (ANALOG 2), else the SUPER engine */
#define B(N, k, u, n) FIF(FELUCCA_ENG_##N)({k, u, n},)
#if FELUCCA_ANALOG2
#define FELUCCA_ENG_SUPER FELUCCA_ENG_ANALOG
#endif
static const struct __attribute__((packed)) { uint8_t kind : 3, e : 5; const char *name; } BANK[] = {   /* (5 bytes an entry: an engine UID < 32) */
    B(ANALOG, BK_BASS, 0, "808 BOOM") B(ANALOG, BK_BASS, 0, "808 DIRTY") B(ANALOG, BK_BASS, 0, "808 SLIDE") B(ANALOG, BK_BASS, 0, "SUB BASS")
    B(ANALOG, BK_BASS, 0, "PLUGG BASS") B(ANALOG, BK_BASS, 0, "REESE") B(ANALOG, BK_BASS, 0, "WOBBLE") B(ANALOG, BK_BASS, 0, "ACID 303")
    B(DIGITAL, BK_BASS, 1, "FM BASS") B(PHASE, BK_BASS, 2, "CZ BASS") B(TRIO, BK_BASS, 6, "FAT BASS") B(ANALOG, BK_BASS, 0, "FUNK BASS")
    B(FORMANT, BK_BASS, 5, "WOW BASS") B(LOFI, BK_BASS, 3, "GB BASS") B(SAMPLE, BK_BASS, 4, "UP BASS") B(SAMPLE, BK_BASS, 4, "DEEP BASS")
#if FELUCCA_ANALOG2
    B(ANALOG, BK_BASS, 0, "LP24 BASS")
#endif
    B(DIGITAL, BK_KEYS, 1, "RHODES") B(DIGITAL, BK_KEYS, 1, "DX RHODES") B(DIGITAL, BK_KEYS, 1, "WURLI") B(DIGITAL, BK_KEYS, 1, "M1 PIANO")
    B(DIGITAL, BK_KEYS, 1, "AFRO KEYS") B(SAMPLE, BK_KEYS, 4, "GRAND PNO") B(SAMPLE, BK_KEYS, 4, "DUSTY PNO") B(SAMPLE, BK_KEYS, 4, "LOFI KEYS") B(PHASE, BK_KEYS, 2, "SOFT KEYS")
    B(DIGITAL, BK_KEYS, 1, "CLAV")
    B(DRAWBAR, BK_ORGAN, 7, "SOUL ORGAN") B(DRAWBAR, BK_ORGAN, 7, "GOSPEL") B(DRAWBAR, BK_ORGAN, 7, "JAZZ ORGAN") B(DRAWBAR, BK_ORGAN, 7, "DIRTY B3")
    B(DRAWBAR, BK_ORGAN, 7, "HOUSE ORGN")
    B(ANALOG, BK_PAD, 0, "WARM PAD") B(TRIO, BK_PAD, 6, "SAW PAD") B(DIGITAL, BK_PAD, 1, "GLASS PAD") B(ANALOG, BK_PAD, 0, "DARK STR")
    B(PHASE, BK_PAD, 2, "CZ STRING") B(ANALOG, BK_PAD, 0, "ATMOS PAD") B(GRAIN, BK_PAD, 8, "LOFI CLOUD") B(GRAIN, BK_PAD, 8, "VIBE HAZE")
    B(FORMANT, BK_PAD, 5, "CHOIR AAH") B(FORMANT, BK_PAD, 5, "SOUL OOH") B(SUPER, BK_PAD, ENG_UID_SUPER, "SUPER PAD")
    B(ANALOG, BK_LEAD, 0, "SUPERSAW") B(SUPER, BK_LEAD, ENG_UID_SUPER, "SUPER LEAD") B(ANALOG, BK_LEAD, 0, "G-FUNK LD") B(TRIO, BK_LEAD, 6, "SYNC LEAD")
    B(TRIO, BK_LEAD, 6, "HOOVER") B(SUPER, BK_LEAD, ENG_UID_SUPER, "HOOVER SAW")
#if FELUCCA_ANALOG2
    B(ANALOG, BK_LEAD, 0, "SYNC SWEEP") B(ANALOG, BK_LEAD, 0, "FIFTH LEAD")
#endif
    B(FORMANT, BK_LEAD, 5, "TALKBOX") B(LOFI, BK_LEAD, 3, "GAME LEAD") B(SAMPLE, BK_LEAD, 4, "LOFI FLUTE") B(GRAIN, BK_LEAD, 8, "FLUTE DUST")
    B(ANALOG, BK_PLUCK, 0, "TRAP PLUCK") B(SUPER, BK_PLUCK, ENG_UID_SUPER, "SUPER PLCK") B(PHASE, BK_PLUCK, 2, "RESO PLUCK") B(DIGITAL, BK_PLUCK, 1, "PLUGG BELL") B(DIGITAL, BK_PLUCK, 1, "TRAP BELL")
    B(DIGITAL, BK_PLUCK, 1, "MUSIC BOX") B(DIGITAL, BK_PLUCK, 1, "KALIMBA") B(DIGITAL, BK_PLUCK, 1, "MARIMBA") B(SAMPLE, BK_PLUCK, 4, "VIBES")
    B(LOFI, BK_PLUCK, 3, "8BIT ARP")
    B(TRIO, BK_STAB, 6, "MIN STAB") B(TRIO, BK_STAB, 6, "MIN7 STAB") B(TRIO, BK_STAB, 6, "RAVE STAB") B(TRIO, BK_STAB, 6, "DUB CHORD") B(SUPER, BK_STAB, ENG_UID_SUPER, "SUPER CHRD")
    B(ANALOG, BK_STAB, 0, "SYN BRASS") B(PHASE, BK_STAB, 2, "CZ BRASS") B(SAMPLE, BK_STAB, 4, "HORN STAB") B(SAMPLE, BK_STAB, 4, "STRING STB")
    B(SAMPLE, BK_FX, 4, "SCRATCH") B(SAMPLE, BK_FX, 4, "GM KIT")
    B(FM6, BK_FX, ENG_UID_FM6, "INIT VOICE") B(FM6, BK_FX, ENG_UID_FM6, "INIT WIDE") B(FM6, BK_FX, ENG_UID_FM6, "INIT MONO")   /* (FM6: the voice packs are nbank.c's) */
    B(PHYS, BK_PLUCK, ENG_UID_PHYS, "BELL TREE") B(PHYS, BK_PLUCK, ENG_UID_PHYS, "WOOD MRMBA") B(PHYS, BK_PLUCK, ENG_UID_PHYS, "PLUCK")
    B(PHYS, BK_PLUCK, ENG_UID_PHYS, "THUMB PNO") B(PHYS, BK_PLUCK, ENG_UID_PHYS, "SYMP HARP") B(PHYS, BK_PAD, ENG_UID_PHYS, "BOWED METAL")
    B(PHYS, BK_PAD, ENG_UID_PHYS, "DRONE STRING") B(PHYS, BK_FX, ENG_UID_PHYS, "HAND DRUM") B(PHYS, BK_FX, ENG_UID_PHYS, "TOMS")
    B(ACID, BK_BASS, ENG_UID_ACID, "ACID LINE") B(ACID, BK_BASS, ENG_UID_ACID, "ACID SQR") B(ACID, BK_BASS, ENG_UID_ACID, "ACID RAGE")
    B(ACID, BK_BASS, ENG_UID_ACID, "ACID DUB")
    B(SLICE, BK_FX, ENG_UID_SLICE, "BREAK 16") B(SLICE, BK_FX, ENG_UID_SLICE, "CHOP 8") B(SLICE, BK_FX, ENG_UID_SLICE, "REVERSE")
    B(SLICE, BK_FX, ENG_UID_SLICE, "USR SLICE")
    B(NOISE, BK_FX, ENG_UID_NOISE, "WIND") B(NOISE, BK_FX, ENG_UID_NOISE, "RAIN") B(NOISE, BK_LEAD, ENG_UID_NOISE, "ARCADE")
    B(NOISE, BK_FX, ENG_UID_NOISE, "METAL")
    B(DRONE, BK_PAD, ENG_UID_DRONE, "STILL AIR") B(DRONE, BK_PAD, ENG_UID_DRONE, "OPEN FIFTH") B(DRONE, BK_PAD, ENG_UID_DRONE, "SLOW CHAIN")
    B(DRONE, BK_PAD, ENG_UID_DRONE, "DARK RING") B(DRONE, BK_PAD, ENG_UID_DRONE, "SWARM") B(DRONE, BK_FX, ENG_UID_DRONE, "THETA")
    B(DRONE, BK_FX, ENG_UID_DRONE, "ALPHA") B(DRONE, BK_FX, ENG_UID_DRONE, "DELTA") B(DRONE, BK_FX, ENG_UID_DRONE, "GAMMA")
    B(PLAITS, BK_BASS, ENG_UID_PLAITS, "FOLD BASS") B(PLAITS, BK_LEAD, ENG_UID_PLAITS, "TIDES LEAD") B(PLAITS, BK_PLUCK, ENG_UID_PLAITS, "SERGE BELL")
    B(PLAITS, BK_ORGAN, ENG_UID_PLAITS, "ADD ORGAN") B(PLAITS, BK_PAD, ENG_UID_PLAITS, "SPECTRA") B(PLAITS, BK_PLUCK, ENG_UID_PLAITS, "HARM BELL")
    B(PLAITS, BK_LEAD, ENG_UID_PLAITS, "VOSIM VOX") B(PLAITS, BK_STAB, ENG_UID_PLAITS, "FORMANT SWP") B(PLAITS, BK_LEAD, ENG_UID_PLAITS, "Z RESO")
    B(PLAITS, BK_FX, ENG_UID_PLAITS, "DROPLETS") B(PLAITS, BK_FX, ENG_UID_PLAITS, "RAIN GLASS") B(PLAITS, BK_FX, ENG_UID_PLAITS, "GRIT DUST")
    B(CZ, BK_BASS, ENG_UID_CZ, "PD BASS") B(CZ, BK_LEAD, ENG_UID_CZ, "RESO SWEEP") B(CZ, BK_PLUCK, ENG_UID_CZ, "GLASS BELL")
    B(CZ, BK_STAB, ENG_UID_CZ, "WIRE BRASS") B(CZ, BK_PAD, ENG_UID_CZ, "SOFT PAD") B(CZ, BK_FX, ENG_UID_CZ, "NOISE BREATH")
    B(CZ, BK_KEYS, ENG_UID_CZ, "PULSE KEYS") B(CZ, BK_LEAD, ENG_UID_CZ, "CZ INIT")
    B(JUNO, BK_PAD, ENG_UID_JUNO, "JUNO STRNG") B(JUNO, BK_PAD, ENG_UID_JUNO, "JUNO PWM PD") B(JUNO, BK_STAB, ENG_UID_JUNO, "JUNO BRASS")
    B(JUNO, BK_BASS, ENG_UID_JUNO, "JUNO BASS") B(JUNO, BK_LEAD, ENG_UID_JUNO, "JUNO SYNCL") B(JUNO, BK_FX, ENG_UID_JUNO, "JUNO HOLLOW")
    B(JUNO, BK_KEYS, ENG_UID_JUNO, "JUNO ORGAN") B(JUNO, BK_LEAD, ENG_UID_JUNO, "JUNO INIT")
    B(STRINGS, BK_PAD, ENG_UID_STRINGS, "SOLINA") B(STRINGS, BK_PAD, ENG_UID_STRINGS, "ENSEMBLE") B(STRINGS, BK_PAD, ENG_UID_STRINGS, "CELLOS")
    B(STRINGS, BK_STAB, ENG_UID_STRINGS, "STR STAB") B(STRINGS, BK_PAD, ENG_UID_STRINGS, "CHOIR STR")
    B(VPS, BK_LEAD, ENG_UID_VPS, "VPS LEAD") B(VPS, BK_PAD, ENG_UID_VPS, "FORMANT PAD") B(VPS, BK_BASS, ENG_UID_VPS, "VPS BASS")
    B(VPS, BK_PLUCK, ENG_UID_VPS, "VOX PLUCK") B(VPS, BK_STAB, ENG_UID_VPS, "DIGI BRASS") B(VPS, BK_PLUCK, ENG_UID_VPS, "VPS BELL")
    B(TERRAIN, BK_PAD, ENG_UID_TERRAIN, "RIPPLE PAD") B(TERRAIN, BK_PLUCK, ENG_UID_TERRAIN, "CRYSTAL") B(TERRAIN, BK_BASS, ENG_UID_TERRAIN, "BUMPY BASS")
    B(TERRAIN, BK_LEAD, ENG_UID_TERRAIN, "SADDLE LEAD") B(TERRAIN, BK_FX, ENG_UID_TERRAIN, "CHAOS FX") B(TERRAIN, BK_KEYS, ENG_UID_TERRAIN, "ORBIT KEYS")
    B(WSEQ, BK_LEAD, ENG_UID_WSEQ, "WAVE STEPS") B(WSEQ, BK_PAD, ENG_UID_WSEQ, "SWEEP PAD") B(WSEQ, BK_BASS, ENG_UID_WSEQ, "OCTAVE BASS")
    B(WSEQ, BK_FX, ENG_UID_WSEQ, "TRANCE GATE") B(WSEQ, BK_PLUCK, ENG_UID_WSEQ, "BELL STEPS") B(WSEQ, BK_PAD, ENG_UID_WSEQ, "MORPH PAD")
    B(VECTOR, BK_PAD, ENG_UID_VECTOR, "VS PAD") B(VECTOR, BK_STAB, ENG_UID_VECTOR, "VS BRASS") B(VECTOR, BK_PLUCK, ENG_UID_VECTOR, "DIGI BELLS")
    B(VECTOR, BK_BASS, ENG_UID_VECTOR, "VS BASS") B(VECTOR, BK_LEAD, ENG_UID_VECTOR, "VS LEAD") B(VECTOR, BK_PAD, ENG_UID_VECTOR, "SPACE DRONE")
    B(SID, BK_LEAD, ENG_UID_SID, "PULSE LEAD") B(SID, BK_BASS, ENG_UID_SID, "SAW BASS") B(SID, BK_LEAD, ENG_UID_SID, "SYNC LEAD")
    B(SID, BK_PLUCK, ENG_UID_SID, "RING BELL") B(SID, BK_PAD, ENG_UID_SID, "PWM STRINGS") B(SID, BK_BASS, ENG_UID_SID, "8580 ACID")
    B(SID, BK_FX, ENG_UID_SID, "NOISE ZAP") B(SID, BK_PLUCK, ENG_UID_SID, "C64 ARP")
    B(PROPHET, BK_STAB, ENG_UID_PROPHET, "P5 BRASS") B(PROPHET, BK_PAD, ENG_UID_PROPHET, "P5 STRINGS") B(PROPHET, BK_LEAD, ENG_UID_PROPHET, "P5 SYNC LEAD")
    B(PROPHET, BK_PLUCK, ENG_UID_PROPHET, "P5 PMOD BELL") B(PROPHET, BK_BASS, ENG_UID_PROPHET, "P5 BASS") B(PROPHET, BK_PAD, ENG_UID_PROPHET, "P5 PAD")
    B(PROPHET, BK_KEYS, ENG_UID_PROPHET, "P5 CLAV")
    B(WAVE, BK_PAD, ENG_UID_WAVE, "PPG PAD") B(WAVE, BK_LEAD, ENG_UID_WAVE, "WT SWEEP") B(WAVE, BK_BASS, ENG_UID_WAVE, "SYNC BASS")
    B(WAVE, BK_PLUCK, ENG_UID_WAVE, "DIGI BELL") B(WAVE, BK_PAD, ENG_UID_WAVE, "VOCAL PAD") B(WAVE, BK_LEAD, ENG_UID_WAVE, "SYNC SCREAM")
#if GR_FALLBACK                                     /* (GRAIN without its presets' sets: eng_grain.c) */
    B(GRAIN, BK_PAD, 8, "GRAIN PAD")
#endif
};
#undef B
_Static_assert(ENG_UID_N <= 32u && BK_FX < 8, "BANK: an entry's kind and engine UID in a byte");
#define NBANK_ALL (sizeof BANK / sizeof BANK[0])
static uint32_t bank_eng(uint32_t i) { return eng_slot_built(BANK[i].e); }   /* entry i's engine slot */
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
        e = ENGINES[bank_eng(i)];
        for (k = 0; k < e->npresets; k++)
            if (str_eq(e->presets[k].name, BANK[i].name))
                bank_pi[i] = (uint8_t)k;
        if (bank_pi[i] != 0xFF && preset_playable(e, bank_pi[i]))
            bank_ix[bank_n++] = (uint8_t)i;
    }
    for (i = 1; i < bank_n; i++) {                   /* the Optimist UI: by instrument, then as BANK has them (the user,
                                                      * 2026-10-12: "presets should be sorted by instrument") */
        uint8_t x = bank_ix[i];
        uint32_t ex = bank_eng(x);
        for (k = i; k > 0 && bank_eng(bank_ix[k - 1]) > ex; k--)
            bank_ix[k] = bank_ix[k - 1];
        bank_ix[k] = x;
    }
    for (i = 0; i < NENGINES; i++) {
        for (k = 0; k < bank_n && bank_eng(bank_ix[k]) != i; k++)
            ;
        if (k == bank_n)
            bank_init[bank_ni++] = (uint8_t)i;
    }
    bank_ready = 1;
}
#define NBANK ((uint32_t)bank_n)                     /* the factory entries */
#define NLIST (NBANK + bank_ni)                      /* and the INIT ones: then the user presets */
static uint32_t preset_all_pos(uint32_t *total)      /* list index of the selected track's preset, in the whole list */
{
    uint32_t i, j, cur = 0;
    if (!bank_ready)
        bank_resolve();
    for (i = 0; i < NBANK; i++)
        if (bank_eng(bank_ix[i]) == TSEL->eng_req && bank_pi[bank_ix[i]] == TSEL->preset)
            cur = i;
    for (i = 0; i < bank_ni; i++)
        if (bank_init[i] == TSEL->eng_req)
            cur = NBANK + i;
    if (user_of(TSEL) < UP_SLOTS)
        cur = NLIST + up_rank(user_of(TSEL));
    *total = NLIST + up_count();
#if FELUCCA_NB_LIST
    for (j = 0; j < NB_NK; j++) {                        /* (every engine's own collection: nbank.c) */
        uint32_t at, n = nb_list_of(nb_kind_at(j), &i, ~0u, &at);
        if (i < n)
            cur = *total + i;
        *total += n;
    }
#endif
    return cur;
}

/* list index n (< total) -> engine slot, *k its preset (PRESET_INIT: INIT); NENGINES = user preset, *k its slot */
static uint32_t preset_all_at(uint32_t n, uint32_t *k)
{
    if (!bank_ready)
        bank_resolve();
#if FELUCCA_NB_LIST
    if (n >= NLIST + up_count()) {
        uint32_t r, j, c, at = 0;
        n -= NLIST + up_count();
        for (j = 0; j < NB_NK; j++) {
            c = nb_list_of(nb_kind_at(j), &r, ~0u, &at);
            if (n < c) {
                nb_list_of(nb_kind_at(j), &r, n, &at);
                *k = (nb_kind_at(j) << 8) | at;           /* (the collection, the slot) */
                return NB_LIST;
            }
            n -= c;
        }
        *k = 0;
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
    return bank_eng(bank_ix[n]);
}
/* the whole list's name of a sound as the lists show it, below; the favorites' view of the list (core/favorites.c):
 * with LIST FAV the list is the marked sounds only. preset_pos / preset_at / preset_go's index are the list as shown;
 * preset_src maps it to the whole list's (preset_kind labels the whole list's) */
static const char *preset_kind(uint32_t n) { return n < NBANK ? BANK_KIND[BANK[bank_ix[n]].kind] : n < NLIST ? "INIT" : "USER"; }
/* the name of preset k of engine slot e as the lists show it */
static const char *preset_name(uint32_t e, uint32_t k)
{
    return k == PRESET_INIT || !ENGINES[e]->npresets ? "INIT" : ENGINES[e]->presets[k % ENGINES[e]->npresets].name;
}
#if FELUCCA_FAVORITES
/* list entry (e, k) of preset_all_at: marked? The engines' own collections (NB_LIST) have no mark */
static int fav_entry(uint32_t e, uint32_t k)
{
    if (e == NENGINES)
        return fav_user_has(k);
    return e < NENGINES && fav_has(eng_uid(e), preset_name(e, k));
}
#else
#define fav_list 0u
#endif
static uint32_t up_engine_slot(uint32_t k);                 /* storage/upreset.c: a user preset's engine slot */
/* the browsing view of the whole list: EDIT's PRESET card (the user, 2026-10-21: "I would expect to see all the presets
 * of the current engine, and not be able to switch engine") the selected track's engine's sounds only (its factory ones,
 * INIT, its user presets, its own collection); the PRESETS knob ("one big list of everything", pl_whole) every engine's;
 * with LIST FAV the marked sounds of every engine. The engine step (SHIFT2 + PRESETS, the ENGINE card) walks the whole
 * list: preset_all_* */
static uint8_t pl_whole;                             /* the browsing list: 1 every engine's sounds in a row (the PRESETS knob), 0 the track's engine's (EDIT's PRESET card): set by whoever last browsed */
static int pl_keep(uint32_t e, uint32_t k)
{
    if (e == NENGINES && up_engine_slot(k) == NENGINES)
        return 0;                                       /* (a user preset of an engine this build lacks: not listed; PRESETS
                                                         * stalled on it, up_load says MISSING and the sound stays: the FM-1,
                                                         * 2026-10-25, a synth preset under drum-machines) */
#if FELUCCA_FAVORITES
    if (fav_list)
        return fav_entry(e, k);
#endif
    if (is_drum(TSEL) || pl_whole)
        return 1;
    if (e == NENGINES)
        e = up_engine_slot(k);
#if FELUCCA_NB_LIST
    else if (e == NB_LIST)
        e = nb_engine_slot(k >> 8);                     /* (a collection's entry: its engine) */
#endif
    return e == TSEL->eng_req % NENGINES;
}
static uint32_t preset_pos(uint32_t *total)          /* list index of the selected track's preset; >= *total: not in the list */
{
    uint32_t all, cur = preset_all_pos(&all), i, n = 0, pos = 0xFFFFFFFFu;
    for (i = 0; i < all; i++) {
        uint32_t k, e = preset_all_at(i, &k);
        if (!pl_keep(e, k))
            continue;
        if (i == cur)
            pos = n;
        n++;
    }
    *total = n;
    return pos == 0xFFFFFFFFu ? n : pos;
}
static uint32_t preset_src(uint32_t n)               /* list index n -> the whole list's (its length: no such entry) */
{
    uint32_t all, i;
    preset_all_pos(&all);
    for (i = 0; i < all; i++) {
        uint32_t k, e = preset_all_at(i, &k);
        if (pl_keep(e, k) && !n--)
            return i;
    }
    return all;
}
static uint32_t preset_at(uint32_t n, uint32_t *k) { return preset_all_at(preset_src(n), k); }
#if FELUCCA_FAVORITES
/* the selected track's sound as a list entry (e = engine slot, or NENGINES: user preset k): 0 if it has none to mark
 * (the drum track, the engines' own collections, a factory sound the list does not hold) */
static int fav_cur(uint32_t *e, uint32_t *k)
{
    uint32_t all, i, u, ke, kk;
    if (is_drum(TSEL))
        return 0;
#if FELUCCA_NB_LIST
    {
        uint32_t at, r, n = nb_list(&r, ~0u, &at);
        if (r < n)
            return 0;
    }
#endif
    u = user_of(TSEL);
    ke = u < UP_SLOTS ? (uint32_t)NENGINES : TSEL->eng_req % NENGINES;
    kk = u < UP_SLOTS ? u : TSEL->preset;
    preset_all_pos(&all);
    for (i = 0; i < all; i++) {
        uint32_t k2, e2 = preset_all_at(i, &k2);
        if (e2 == ke && k2 == kk) {
            *e = ke;
            *k = kk;
            return 1;
        }
    }
    return 0;
}
#endif
