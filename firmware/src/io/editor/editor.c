/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol: SysEx for the web editor (web/EDITOR_PROTOCOL.md: the frames, every command, which builder item
 * answers it). The core here: INFO (1), BUILD (49), PING (25), WATCH and RELOAD (24); the live sync is ed_sync9.c (the
 * PARAMS 59, STEPS 60, LANE 61, TRACKS 62, SONG 63 pushes while watched); each other command set is its own ed_*.c.
 *   F0 7D 46 4C cmd args.. F7     (7D = non-commercial ID, "FL")
 * Values are 14 bit, two 7-bit bytes LSB first, offset by 8192 (so -8192..8191).
 * Every request gets a reply with the same cmd; RELOAD (24) and the v9 frames are pushed while watched. Frames arrive through sx_frame (usb.c), replies leave
 * through ota_wire_send(). */
#define ED_HDR0 0x7D
#define ED_HDR1 0x46
#define ED_HDR2 0x4C
enum { ED_INFO = 1, ED_GET, ED_SET, ED_DUMP, ED_DESC, ED_STEP_GET, ED_STEP_SET, ED_PRESET, ED_PROJECT, ED_NAMES,
       ED_SMP_BEGIN, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO,
       ED_UP_LIST, ED_UP_GET, ED_UP_PUT, ED_UP_STORE, ED_UP_LOAD, ED_UP_ERASE,   /* v2: user presets */
       ED_WATCH, ED_CHANGED, ED_RELOAD, ED_PING, ED_STEP_CHANGED,              /* v2: live sync */
       ED_TRACK, ED_TRACK_MIX, ED_TRACK_DUMP, ED_TRACK_STEP,                    /* v3: tracks */
       ED_TRACK_PARAM, ED_TRACK_CHANGED,                                        /* v4: any track's parameters */
       ED_DRUM_STEP,                                                            /* v5: the 16 drum lanes */
       ED_BUILD = 49 };            /* v6: the build's contents (33, 34 avoided: Melodee's; 36..42 ed_drums.c; 43..48 backup) */
#include "ed_out.h"
#ifndef ED_OUT_HERE                         /* (felucca.c defines it: ota.c shares it) */
static uint8_t ed_out[ED_OUT_N] __attribute__((aligned(4)));
#endif   /* (ed_out.h: RAM is the limit; BK_LIST pages; aligned: ed_user.c ed_smp_buf) */
static uint32_t ed_n;
/* the replies that grow with the build (ed_b drops what does not fit): the longest of each fits (2026-10, 634 B of room:
 * DESC 603, MACRO 484 at most, DUMP 236, UP_GET 206 in the largest builds; BK_LIST pages, ed_backup.c) */
_Static_assert(3u + 6u + 9u + 9u + 64u * 9u <= ED_PAYLOAD_N, "DESC (64 enum names of 8 at most) fits one reply");
_Static_assert(2u + 2u * (P_COUNT + G_COUNT) <= ED_PAYLOAD_N, "DUMP fits one reply");
_Static_assert(3u + 13u + 2u * P_COUNT + 32u <= ED_PAYLOAD_N, "UP_GET fits one reply");
/* (MACRO's: ed_macro.c, by the macros' rows) */

static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0;
    ed_out[1] = ED_HDR0;
    ed_out[2] = ED_HDR1;
    ed_out[3] = ED_HDR2;
    ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
}
static void ed_b(uint32_t v)
{
    if (ed_n < sizeof ed_out - 1u)
        ed_out[ed_n++] = (uint8_t)(v & 0x7Fu);
}
#include "ed_reply.h"          /* ed_b2..ed_b4: two to four bytes a call; ed_b32: 7-bit groups */
static void ed_v(int32_t v)
{
    uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192);
    ed_b2(u, u >> 7);
}
static void ed_str(const char *s, uint32_t max)   /* ASCII, 0-terminated */
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i]);
    ed_b(0);
}
static void ed_send(void)
{
    ed_out[ed_n++] = 0xF7;
    ota_wire_send(ed_out, ed_n);
}
static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
#if FELUCCA_ED_LIVE
#include "ed_desc.h"           /* DESC's body (DESC, FXP, DLAN, STEPLANE; SCRN and SCRS its first four) */
#endif

/* pack7 bytes (a byte of high bits, then up to 7 bytes) back into bytes: the sample slots, kits, backup, snapshots */
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;                                 /* groups: msb byte, then up to 7 bytes */
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
#if FELUCCA_ED_MIRROR
#include "ed_mirror.c"         /* cmd 106 the screen mirrored (the iPhone app), INFO tag 0x65; with live editing or without */
#else
static void ed_mirror_info(void) {}
#define MIR_SEEN() ((void)0)
#define mir_poll() ((void)0)
#endif
#include "ed_opti.h"           /* INFO tag 0x66: what this build answers, by name and version */
#if !FELUCCA_ED_LIVE
#include "ed_lite.c"           /* ED_LIVE=0: backup and restore only (the frames, INFO, BUILD, PING, 11..15, 43..48, 54..57);
                                * the rest of this file is the live editor */
#else
#include "ed_user.c"           /* cmds 11..21: the sample slots and the user presets */
#include "ed_drums.c"          /* cmds 36..42: drum lanes, user kits, a slot read back */
#include "ed_dsrc.c"           /* cmds 50, 51: the drum sources and what a lane's SOUND pages show */
#include "ed_pages.c"          /* cmd 52: the pages (the editor lays out a sound as the device does) */
#if FELUCCA_MACROS
#include "ed_macro.c"          /* cmd 65: what the performance macros make of the values (macro.c) */
#else
#define ED_MACRO_FN
#endif
#include "ed_undo.c"           /* v14: cmd 102 undo and redo, INFO tag 0x5F; ed_undo_edit marks the editor's own step writes */
#if FELUCCA_AUTO
#include "ed_stepx.c"          /* cmds 92, 93 (v11): the automation store */
#else
#define ED_STEPX_FN
#endif
#if FELUCCA_PATTERNS
#include "ed_pat.c"            /* cmds 79..82: the per-track patterns and scenes (storage/sections/pat.c) */
#else
#define ED_PAT_FN              /* (each file ED_*_FN: its handler in the tables of ed_handle) */
#endif
#if CZ_NUSER
#include "ed_cz.c"             /* cmd 66: the CZ collection (nbank.c) */
#else
#define ED_CZ_FN
#endif
#if FELUCCA_SNAPSHOTS
#include "ed_snap.c"           /* cmds 54..57: snapshots (list, save / load / clear / rename, export, import) */
#else
#define ED_SNAP_FN
#endif
#if FELUCCA_FLASH && FELUCCA_BACKUP
#include "ed_backup.c"         /* cmds 43..48: backup / restore of every stored object */
#else
#define ED_BACKUP_FN
#endif
#if FELUCCA_FLASH && FELUCCA_BACKUP && SEC_LOGGED && FELUCCA_PATTERNS
#include "ed_proj.c"           /* cmd 110: the projects (open, save, clear, name; their streams are the backup's PJ01..) */
#else
#define ED_PROJ_FN
#endif
#if (FELUCCA_SL24_EXPORT || FELUCCA_SL24_EDIMPORT) && FELUCCA_FLASH
#include "ed_sl24.c"           /* cmd 78: the working project and the settings for SLOOP 2.4 (its backup file); 90, 91 its import */
#else
#define ED_SL24_FN
#endif

/* the engine byte of DUMP / RELOAD / TRACK: NENGINES = the drum track (no engine) */
static uint32_t ed_eng(const track_t *t) { return is_drum(t) ? NENGINES : t->eng_req % NENGINES; }

/* ---- live sync: while the editor WATCHes, device-side changes are pushed (v9, ed_sync9.c: every track, coalesced), and
 * RELOAD after a load or when another track was selected. What the selected track plays as (engine, preset) and which
 * track it is are kept here; the values and steps are ed_sync9.c's shadows. Pushes go out only into a half-empty SysEx
 * ring, so they never wait. */
static struct {
    uint8_t on, eng, preset, sel;
    uint8_t ends;                                        /* WATCH bit 4 (v12): STEPS pushes carry the note ends */
    uint32_t last_ms, run_ms, resets;
} ed_w;

#include "ed_steps.c"         /* a step on the wire: STEP_GET / TRACK_STEP bytes (out and in), DRUM_STEP bytes */
static void ed_shadow(void)                              /* the editor is in sync */
{
    ed_w.eng = (uint8_t)ed_eng(TSEL);
    ed_w.preset = TSEL->preset;
    ed_w.sel = song.sel;
    sync_reload = 0;
}
#include "ed_steplane.c"     /* v14: cmd 105 a step in a drum lane's context and its lockable values, INFO tag 0x62 */
#include "ed_solo.c"          /* v14: cmd 104 track and lane solo and mute, INFO tag 0x61 */
#include "ed_note.c"          /* v14: cmd 103 notes played from the editor, INFO tag 0x60 */
#include "ed_scope.c"        /* v13: cmd 101 the oscilloscope's trace, INFO tag 0x5E */
#include "ed_fav.c"           /* v13: cmd 100 the favorites, INFO tag 0x5D */
#include "ed_scrs.c"          /* v13: cmd 99 the Optimist UI's screens as rows of cards, INFO tag 0x5C */
#include "ed_tools.c"         /* v13: cmd 98 the STEP TOOLS and EUCLID, INFO tag 0x5B */
#include "ed_song.c"          /* v13: cmd 97 the song chain, INFO tag 0x5A */
#include "ed_scrn.c"          /* v13: cmd 96 GLOBAL and SYSTEM as the Optimist UI's lines, INFO tag 0x59 */
#include "ed_dlan.c"          /* v13: cmd 95 the drum lanes' mix, LFOs and the sequencer mutes, INFO tag 0x58 */
#include "ed_fxp.c"           /* v13: cmd 94 every effect slot and its parameters by owner, INFO tag 0x57 */
#include "ed_fxs.c"           /* v10: cmd 86 the FX slots, 87 their push, INFO tag 0x56 */
#include "ed_sync9.c"         /* v9: every track's changes, coalesced (PARAMS, STEPS, LANE, TRACKS, SONG pushes) */
#include "ed_reads.h"          /* which ops of cmds 86..107 only read (no ui.force for them) */
/* the editor's own sound load on the selected track (PRESET, SET of G_ENGSEL): the editor re-reads DUMP after
 * the reply, so the shadow takes the load (engine, preset, the parameters it changed) and no RELOAD echoes back
 * (INFO tag 53 01 bit 1). A RELOAD already due before it (a load or selection on the device) still goes out.
 * After Felucca 1.0.2 (hugelton/Felucca db70550, #65, editor.c ed_load_before / after, by Leo Kuroshita,
 * GPL-3.0-only). */
typedef struct { int16_t p[P_COUNT]; uint8_t eng, preset, due; } ed_load_t;
static void ed_load_before(ed_load_t *b)
{
    memcpy(b->p, TSEL->p, sizeof b->p);
    b->eng = (uint8_t)ed_eng(TSEL);
    b->preset = TSEL->preset;
    b->due = sync_reload || b->eng != ed_w.eng || b->preset != ed_w.preset || song.sel != ed_w.sel;
}
static void ed_load_after(const ed_load_t *b)
{
    uint32_t i;
    if (!ed_w.on || b->due)
        return;
    sync_reload = 0;
    ed_w.eng = (uint8_t)ed_eng(TSEL);
    ed_w.preset = TSEL->preset;
    for (i = 0; i < P_COUNT; i++)                        /* the load's values; a pending push of another stays */
        if (TSEL->p[i] != b->p[i])
            e9.p[song.sel][i] = TSEL->p[i];
}
static int ed_room(void) { return so_w - so_r + 8u <= SXQ / 2u; }
static void ed_known(uint32_t k, uint32_t id) { ed9_known_p(k, id); }   /* the editor's own change of trk[k].p[id]: no push */

static void ed_sync(void)                                /* main loop */
{
    uint32_t now = fm1_ms;
    if (!ed_w.on || now - ed_w.run_ms < 5u)
        return;
    ed_w.run_ms = now;
    if (!usb.config || usb.resets != ed_w.resets || now - ed_w.last_ms > 3000u) {
        ed_w.on = 0;                                     /* no host, USB reset, or 3 s without a request */
        return;
    }
    if (sync_reload || ed_eng(TSEL) != ed_w.eng || TSEL->preset != ed_w.preset || song.sel != ed_w.sel) {
        if (!ed_room())
            return;
        ed_shadow();
        ed_begin(ED_RELOAD);
        ed_b3(ed_eng(TSEL), TSEL->preset, song.sel);
        ed_send();
        return;
    }
    ed9_sync(now);                                       /* every track, coalesced (ed_sync9.c) */
}

/* descriptor of parameter id of track t: the engine parameters of the engine it asked for
 * (t->engine follows in the audio ISR, after a short fade) */
static const param_desc_t *ed_tdesc(const track_t *t, uint32_t id)   /* the static ones: an engine's */
{                                                                     /* desc hook is the device display only */
    if(is_drum(t) && id==P_E0) return &DRUM_KIT_DESC;
    if (is_drum(t) && id == P_DACC)
        return &DRUM_ACC_DESC;                                        /* (the ACCENT amount) */
    if (is_drum(t) && DRUM_GR_OF(id))
        return DRUM_GR_OF(id);                                        /* (the kit's GRIDS: params.c) */
    if (id >= P_E0 && id <= P_E7) {
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        return &e->edit[id - P_E0];
    }
    return &TP[id];
}
/* descriptor and value slot of (scope, id): scope 0 = the selected track, 1 = global; with two or more reverb
 * algorithms (rev_type.c REV_MULTI) REVERB > TYPE at (SC_BPSET, BPS_RTYPE) too, as INFO's tag 0x52 names it: its
 * names are the algorithms built */
#define ED_SC_RTYPE 9u                                  /* (params.c SC_BPSET) */
static const param_desc_t *ed_desc(uint32_t scope, uint32_t id, int16_t **vp)
{
#if BP_SET_ANY
    _Static_assert(SC_BPSET == ED_SC_RTYPE, "the editor's scope of REVERB > TYPE and the MIDI settings");
#endif
#if REV_MULTI
    if (scope == ED_SC_RTYPE && id == BPS_RTYPE)
        return bps_desc(id, vp);
#endif
#if FELUCCA_MIDI_CH || FELUCCA_MIDI_OUT || FELUCCA_MIDI_INCLK
    if (scope == ED_SC_RTYPE && id >= BPS_CH0 && id <= BPS_MIN && ((id <= BPS_CHD ? FELUCCA_MIDI_CH : id == BPS_MOUT ? FELUCCA_MIDI_OUT : FELUCCA_MIDI_INCLK)))
        return bps_desc(id, vp);   /* the tracks' MIDI channels, MIDI OUT and IN (SLOOP 2.4; INFO tag 0x54) */
#endif
    if (scope == 0 && id < P_COUNT) {
        *vp = &TSEL->p[id];
        return ed_tdesc(TSEL, id);
    }
    if (scope == 1 && id < G_COUNT) {
        *vp = &song.g[id];
        return &GP[id];
    }
    return 0;
}

/* the commands of the files, each tried in turn until one answers (its ED_*_FN: the handler, nothing when the build
 * leaves the file out; one loop for them all, not a call each in ed_handle: 2026-10, the slim pass) */
typedef int (*ed_cmd_t)(uint32_t cmd, const uint8_t *a, uint32_t na);
static const ed_cmd_t ED_FX_CMDS[] = {ED_FXS_FN ED_FXP_FN ED_DLAN_FN ED_SCRN_FN ED_SONGC_FN ED_TOOLS_FN ED_SCRS_FN ED_FAV_FN
                                      ED_SCOPE_FN ED_UNDO_FN ED_NOTE_FN ED_SOLO_FN ED_STEPLANE_FN 0};   /* 86..105 */
/* 65 the macros' effect on the values; 11..21: samples, user presets; 36..42: drum lanes, kits; 43..48: backup; 50, 51
 * drum sources; 52 pages; 54..57 snapshots; 66 the CZ collection; 78, 90, 91 SLOOP 2.4; 79..84 patterns; 92, 93: the
 * automation; 110 the projects */
static const ed_cmd_t ED_CMDS[] = {ED_MACRO_FN ED_USER_FN ED_DRUMS_FN ED_BACKUP_FN ED_DSRC_FN ED_PAGES_FN ED_SNAP_FN
                                   ED_CZ_FN ED_SL24_FN ED_PAT_FN ED_STEPX_FN ED_PROJ_FN 0};
static int ed_cmds(const ed_cmd_t *t, uint32_t cmd, const uint8_t *a, uint32_t na)   /* 1: one of t answered */
{
    for (; *t; t++)
        if ((*t)(cmd, a, na))
            return 1;
    return 0;
}

static void ed_vs(const int16_t *p, uint32_t n) { while (n--) ed_v(*p++); }   /* n values, v14 each */
/* STEP_SET / TRACK_STEP: step i of track k written from the n bytes at a (8 or more: n, notes, time, flags, vel ..; fewer:
 * only read), the editor's own (an undo level; no push of it) */
static void ed_step_w(uint32_t k, uint32_t i, const uint8_t *a, uint32_t n)
{
    track_t *t = &trk[k];
    if (n >= 8u) {
        ed_undo_edit(t);
        fm1_irq_off();
        ed_step_in(t, i, a, n);
        fm1_irq_on();
        ui.force = 1;
    }
    ed9_known_step(k, i);
}

static void ed_handle(const uint8_t *f, uint32_t n)   /* f: the bytes between F0 and F7 */
{
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    int16_t *vp;
    const param_desc_t *d;
    ed_begin(cmd);
    switch (cmd) {
    case ED_INFO:
        ed_str("FELUCCA " FELUCCA_VERSION, 24);
        ed_b(NENGINES); ed_b32(P_COUNT, 2); ed_b32(G_COUNT, 2); ed_b(NSTEP); ed_b32(P_E0, 2);   /* (v7: 2 x 7 bit each) */
        for (i = 0; i < NENGINES; i++)
            ed_str(ENGINES[i]->name, 8);
        ed_b(NTRK);                                       /* v3 */
        ed_b(7);                                          /* v5: the protocol version (7: every id 2 x 7 bit) */
        for (i = 0; i < NENGINES; i++)                    /* v6: each slot's engine UID (registry.h) */
            ed_b(eng_uid(i));
        ed_b3(0x53, 1, 3);              /* tag: live sync, bit 0 WATCH while on keeps the shadow, bit 1 no RELOAD
                                         * after the editor's own PRESET / G_ENGSEL (Felucca 1.0.2 #65) */
#if FELUCCA_FX_REVERB
        ed_b(0x52); ed_b(3);            /* tag: the reverb's algorithms (rev_type.c): the mask of those built (bit
                                         * RT_x: 0 ROOM, 1 SPRING, 2 PLATE, 3 FDN8, 4 AIRWIN), then TYPE's scope and
                                         * id for DESC / GET / SET (127 127: one built, no TYPE) */
        ed_b3(REV_MASK, REV_MULTI ? ED_SC_RTYPE : 127u, REV_MULTI ? 0u : 127u);   /* (bp_set.c BPS_RTYPE) */
#endif
#if FELUCCA_MIDI_CH || FELUCCA_MIDI_OUT || FELUCCA_MIDI_INCLK
        ed_b(0x54); ed_b(3);            /* tag: the MIDI settings at (scope ED_SC_RTYPE, id first + n): n 0..3 the channels of
                                         * tracks 1..3 and the drums (0 OFF, 1..16; the project's), 4 MIDI OUT (0 KEYS,
                                         * 1 SEQ), 5 MIDI IN (0 NOTES, 1 CLOCK); the mask says which are built (bit 0
                                         * channels, 1 OUT, 2 IN); the editor lists them with DESC / GET / SET */
        ed_b2((FELUCCA_MIDI_CH ? 1u : 0u) | (FELUCCA_MIDI_OUT ? 2u : 0u) | (FELUCCA_MIDI_INCLK ? 4u : 0u), ED_SC_RTYPE);
        ed_b(BPS_CH0);
#endif
#if FELUCCA_PATTERNS
        ed_b(0x55); ed_b(2);            /* tag: the per-track patterns (ed_pat.c, cmds 79..82): slots a track, scenes */
        ed_b2(PAT_N, SEC_IDS);
#endif
        ed_undo_info();                 /* tag 0x5F: undo and redo (v14, ed_undo.c) */
        ed_note_info();                 /* tag 0x60: notes (v14, ed_note.c) */
        ed_solo_info();                 /* tag 0x61: solo and mute (v14, ed_solo.c) */
        ed_steplane_info();             /* tag 0x62: a step in a lane's context (v14, ed_steplane.c) */
        ed_fx_info();                   /* tag 0x56: the FX slots' types (v10, ed_fxs.c) */
        ed_fxp_info();                  /* tag 0x57: the slots by owner (v13, ed_fxp.c) */
        ed_scope_info();                /* tag 0x5E: the scope (v13, ed_scope.c; the Optimist UI) */
        ed_fav_info();                  /* tag 0x5D: the favorites (v13, ed_fav.c) */
        ed_scrs_info();                 /* tag 0x5C: the screens as rows of cards (v13, ed_scrs.c; the Optimist UI) */
        ed_tools_info();                /* tag 0x5B: TOOLS and EUCLID (v13, ed_tools.c; the Optimist UI) */
        ed_songc_info();                /* tag 0x5A: the song chain (v13, ed_song.c) */
        ed_scrn_info();                 /* tag 0x59: GLOBAL and SYSTEM as lines (v13, ed_scrn.c; the Optimist UI) */
        ed_dlan_info();                 /* tag 0x58: the lanes' mix, LFOs and mutes (v13, ed_dlan.c) */
        ed_mirror_info();               /* tag 0x65: the screen mirrored (ed_mirror.c) */
        ed_opti_info();                 /* tag 0x66: the features by name and version (ed_opti.h) */
        break;
    case ED_BUILD:                                        /* v6: what this build contains (tools/builder) */
        ed_str(FELUCCA_CFG_NAME, 16);
        ed_b32((uint32_t)FELUCCA_CFG_HASH, 5);            /* the .config's hash, 5 x 7 bits */
        {
            static const uint8_t bits[] = FELUCCA_CFG_BITS;   /* bit n = registry item n built, 7 a byte */
            ed_b(sizeof bits);
            ed_bs(bits, sizeof bits);
        }
        break;
    case ED_GET:
    case ED_SET: {                                         /* scope, id (2 x 7 bit) [, v14] */
        uint32_t id = na >= 3u ? ed_r32(a + 1, 2) : 0xFFFFu;
        if (na < 3u || !(d = ed_desc(a[0], id, &vp)))
            return;
        if (cmd == ED_SET && na >= 5u) {
            if (a[0] == 1 && id == G_ENGSEL) {            /* engine change: the safe path */
                ed_load_t lb;
                ed_load_before(&lb);
                set_engine((uint32_t)clamp(ed_rv(a + 3), 0, NENGINES - 1));
                ed_load_after(&lb);
            } else if (d->max > d->min) {
                *vp = (int16_t)clamp(ed_rv(a + 3), d->min, d->max);
            }
            ui.force = 1;
            if (a[0] == 1u)                               /* the editor's own change: no push */
                ed9_known_g(id);
            else if (a[0] == 0u)
                ed9_known_p(song.sel, id);                /* (REVERB > TYPE: not pushed, nothing to skip) */
        }
        ed_b(a[0]);
        ed_b32(id, 2);
        ed_v(*vp);
        break;
    }
    case ED_DUMP:
        ed9_known_track(song.sel);                         /* the editor gets them all here */
        memcpy(e9.g, song.g, sizeof e9.g);
        ed_b2(ed_eng(TSEL), TSEL->preset);
        ed_vs(TSEL->p, P_COUNT);
        ed_vs(song.g, G_COUNT);
        break;
    case ED_DESC:                                          /* scope, id (2 x 7 bit) */
        if (na < 3u || !(d = ed_desc(a[0], ed_r32(a + 1, 2), &vp)))
            return;
        ed_b(a[0]);
        ed_bs(a + 1, 2);
        ed_desc_body(d);                                   /* (ed_desc.h) */
        break;
    case ED_STEP_GET:
    case ED_STEP_SET:
        if (na < 1u || a[0] >= NSTEP)
            return;
        ed_step_w(song.sel, a[0], a + 1, cmd == ED_STEP_SET ? na - 1u : 0u);
        ed_b(a[0]);
        ed_step_out(TSEL, a[0], 1);
        break;
    case ED_PRESET: {                                      /* engine, preset */
        ed_load_t lb;
        if (na < 2u || a[0] >= NENGINES)
            return;
        ed_load_before(&lb);
        if (a[0] != TSEL->eng_req)
            set_engine(a[0]);
        apply_preset(a[1]);
        ed_load_after(&lb);
        ui.force = 1;
        ed_b2(ed_eng(TSEL), TSEL->preset);
        break;
    }
    case ED_PROJECT:                                       /* 0 = load, 1 = save, 2 = query; slot 0..FELUCCA_SECTIONS-1 (A..) */
        if (na < 2u || a[0] > 2u)
            return;
#if FELUCCA_ARRANGER
        if (a[0] < 2u && (song.playing || transport_req)) {
            ed_b4(a[0], a[1] % FELUCCA_SECTIONS, project_used(a[1] % FELUCCA_SECTIONS), 1);                                  /* optional status: transport busy */
            break;
        }
#endif
        if (a[0] == 1u)
            project_save(a[1] % FELUCCA_SECTIONS);
        else if (a[0] == 0u)
            project_load(a[1] % FELUCCA_SECTIONS);
        ed_b2(a[0], a[1] % FELUCCA_SECTIONS);
#if FELUCCA_SL24_SAFE
        ed_b((uint8_t)project_state(a[1] % FELUCCA_SECTIONS));   /* 0 empty, 1 used, 2 SLOOP 2.4's, 3 another's (kept) */
#else
        ed_b(project_used(a[1] % FELUCCA_SECTIONS));
#endif
        break;
    case ED_NAMES:                                         /* preset names of an engine */
        if (na < 1u || a[0] >= NENGINES)
            return;
        ed_b(a[0]);
        if (eng_first_playable(ENGINES[a[0]]) == PRESET_INIT) {   /* no playable preset: INIT alone (ui.c) */
            ed_b(1);
            ed_str("INIT", 12);
        } else {
            ed_b(ENGINES[a[0]]->npresets);
            for (i = 0; i < ENGINES[a[0]]->npresets; i++)
                ed_str(ENGINES[a[0]]->presets[i].name, 12);
        }
        for (i = 0; i < 2u; i++)                           /* then the two edit-page titles */
            ed_str(ENGINES[a[0]]->page_title[i], 8);
        break;
    case ED_WATCH: {                                       /* on -> on */
        uint32_t keep, m;
        if (na < 1u)
            return;
        keep = ed_w.on && ed_w.resets == usb.resets && (a[0] & 1u);   /* already watching: the changes not yet */
        m = a[0] & 1u ? 1u | 4u | (a[0] & 16u) : 0u;                  /* pushed stay pending (INFO tag 53 01 bit 0, */
        ed_w.on = (uint8_t)(m & 1u);                                  /* Felucca 1.0.2 #65); on is v9 (bit 2), bit 4 */
        ed_w.ends = (uint8_t)(m >> 4 & 1u);              /* v12: a synth step pushed with its notes' own ends */
        ed_w.resets = usb.resets;
        if (ed_w.on && !keep) {
            ed_shadow();
            ed9_shadow_all();
        }
        ed_b(m);                                           /* (on, v9, ends: what is on; v4 and the stream: never) */
        break;
    }
    case ED_PING:
        ed_b(0);
        break;
#if FELUCCA_ED_MIRROR
    case ED_MIRROR:                                        /* the screen mirrored: on / off (ed_mirror.c) */
        ed_mirror(a, na);
        break;
    case ED_PANEL:                                         /* the panel pressed by the mirror's client (ed_mirror.c) */
        ed_panel(a, na);
        break;
    case ED_NAV:                                           /* the client's touch navigation (ed_mirror.c, ui/optimist/op_nav.c) */
        ed_nav(a, na);
        break;
#endif
    case ED_TRACK:                                         /* [track] -> selected, NTRK, per track: engine, preset, level, mute, armed */
        if (na >= 1u && a[0] < NTRK && a[0] != song.sel) {
            track_select(a[0]);
            if (ed_w.on)
                ed_shadow();                               /* the editor re-reads it: no RELOAD for this */
            e9.trk_sig = ed9_trk_sig();                    /* (v9: nor a TRACKS push) */
        }
        ed9_tracks_out();                                  /* (as the v9 TRACKS push; v5: the solo mask last) */
        break;
    case ED_TRACK_MIX: {                                   /* track [, level v14, mute] -> track, level, mute */
        track_t *t;
        int16_t *lv;
        if (na < 1u || a[0] >= NTRK)
            return;
        t = &trk[a[0]];
        lv = a[0] == TRK_DRUM ? &song.g[G_DRLVL] : &t->p[P_LEVEL];
        if (na >= 4u) {
            *lv = (int16_t)clamp(ed_rv(a + 1), 0, 127);
            t->p[P_MUTE] = (int16_t)(a[3] ? 1 : 0);
            if (a[0] == TRK_DRUM)                          /* the editor's own change: no push */
                ed9_known_g(G_DRLVL);
            else
                ed_known(a[0], P_LEVEL);
            ed_known(a[0], P_MUTE);
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_v(*lv);
        ed_b(t->p[P_MUTE] != 0);
        break;
    }
    case ED_TRACK_DUMP:                                    /* track -> track, engine, preset, P_COUNT x v14 */
        if (na < 1u || a[0] >= NTRK)
            return;
        ed9_known_track(a[0]);                             /* (v9: the editor has them now) */
        ed_b3(a[0], ed_eng(&trk[a[0]]), trk[a[0]].preset);
        ed_vs(trk[a[0]].p, P_COUNT);
        break;
    case ED_TRACK_STEP:                                    /* track, index [, step] -> track, index, step (as STEP_GET) */
        if (na < 2u || a[0] >= NTRK || a[1] >= NSTEP)
            return;
        ed_step_w(a[0], a[1], a + 2, na - 2u);
        ed_b2(a[0], a[1]);
        ed_step_out(&trk[a[0]], a[1], 1);
        break;
    case ED_DRUM_STEP: {                                   /* index [, on 16 bits, lvl 32 bits, rat 32 bits as 7-bit groups]
                                                            * -> index, on (3), lvl (5), rat (5): the drum track's 16 lanes
                                                            * and the ACCENT lane (on bit 16, the fifth groups' bits 4..5) */
        dstep_t *d;
        if (na < 1u || a[0] >= NSTEP)
            return;
        d = &TDRUM->dstep[a[0]];
        if (na >= 14u) {
            ed_undo_edit(TDRUM);
            fm1_irq_off();
            ed_dstep_in(d, a);                              /* (ed_steps.c: the 16 lanes and the ACCENT lane) */
            fm1_irq_on();
            ui.force = 1;
        }
        ed9_known_step(TRK_DRUM, a[0]);
        ed_b(a[0]);
        ed_dstep_out(d);
        break;
    }
    case ED_TRACK_PARAM: {                                 /* track, id (2 x 7 bit) [, v14] -> track, id, v14 */
        track_t *t;
        uint32_t id = na >= 3u ? ed_r32(a + 1, 2) : 0xFFFFu;
        if (na < 3u || a[0] >= NTRK || id >= P_COUNT)
            return;
        t = &trk[a[0]];
        d = ed_tdesc(t, id);
        if (na >= 5u) {
            if (d->max > d->min)                           /* as SET: clamped; a fixed value stays */
                t->p[id] = (int16_t)clamp(ed_rv(a + 3), d->min, d->max);
            ed_known(a[0], id);
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_b32(id, 2);
        ed_v(t->p[id]);
        break;
    }
    case ED_SYNC_STATS:                                    /* v9: what the sync costs (a measurement aid) */
        ed_b32(e9.scans, 5);                               /* (5 x 7 bits each) */
        ed_b32(e9.frames, 5);
        ed_b32(e9.bytes, 5);
        ed_b32(e9.t_max / FM1_TICKS_PER_US, 5);
        ed_b32(e9.t_sum / FM1_TICKS_PER_US, 5);
        if (na)
            e9.scans = e9.frames = e9.bytes = e9.t_max = e9.t_sum = 0;
        break;
    default:
        if (cmd == ED_DRUM_LANES || cmd == ED_DRUM_LANE || cmd == ED_UKIT_OP) {
            if (!ed_drums(cmd, a, na))
                return;
            ed9_known_lanes();                             /* (v9: the editor's own lane writes: no LANE push) */
            break;
        }
        if (ed_cmds(ED_FX_CMDS, cmd, a, na)) {            /* 86: the FX slots (v10); 94: the owners' slots, 95: the lanes' mix (v13) */
            ed9_known_fx();                                /* (the editor's own writes: no FX push) */
            if (!ed_reads(cmd, a, na))                     /* (a read changes nothing: ui.force clears the screen to black, */
                ui.force = 1;                              /*  and the editor reads often: the panel flashed) */
            break;
        }
        if (!ed_cmds(ED_CMDS, cmd, a, na))
            return;
        break;
    }
    ed_send();
}

/* main loop: editor frames first; anything else stays for ota_service() */
static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    ed_note_tick();                                        /* v14: the editor's notes due, the quiet host's end (ed_note.c) */
    mir_poll();                                            /* the screen mirrored: a Bluetooth request, a catch-up (ed_mirror.c) */
    ed_sync();                                             /* the pushes (while watched) */
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_HDR0 || p[1] != ED_HDR1 || p[2] != ED_HDR2)
        return;
    ed_w.last_ms = fm1_ms;                                 /* any request keeps WATCH alive */
    MIR_SEEN();                                            /* (and the mirror: ed_mirror.c) */
    if (p[3] >= ED_SMP_BEGIN) {                            /* large frames: handled in place, then freed */
        ed_handle(p, n);
        ota_frame_done();
        return;
    }
    {
        static uint8_t f[64];
        uint32_t k = n > sizeof f ? sizeof f : n, i;
        for (i = 0; i < k; i++)
            f[i] = p[i];
        ota_frame_done();                                  /* free the frame buffer before replying */
        ed_handle(f, k);
    }
}
#endif   /* FELUCCA_ED_LIVE */
