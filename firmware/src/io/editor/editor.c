/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol: SysEx for the web editor (web/EDITOR_PROTOCOL.md; v2 = user presets + live sync,
 * v3 = four tracks: the v1 / v2 commands act on the selected track, cmds 27-30 reach any track;
 * v4 = TRACK_PARAM (31) and the TRACK_CHANGED push (32), enabled by WATCH bit 1;
 * v5 = SLOOP 2.0: INFO ends with the protocol version (5), steps carry level / ratchet bytes,
 * DRUM_STEP (33) reads / writes the drum track's 16 lanes, TRACK ends with the solo mask;
 * v6 = the builder: INFO adds each engine slot's UID, BUILD (49) the build's profile, hash and items;
 * v7 = Optimist: DRUM_SRCS (50), DRUM_SHOW (51), PAGES (52), STATUS (53), the sends in TRACK_CHANGED; INFO unchanged,
 *      asked; v8 = snapshots (54..57);
 * v9 = only what changed, every track (WATCH bit 2: PARAMS 59, STEPS 60, LANE 61, TRACKS 62, SONG 63 pushes, ed_sync9.c)
 *      and the status stream with the meters (WATCH bit 3: STREAM 58, ed_status.c); asked with WATCH, INFO unchanged);
 * v10 = the FX slots: INFO tag 0x56, FX (86) and its v9 push FX_PUSH (87), every slot's amount in TRACK_CHANGED (ed_fxs.c);
 * v11 = v10 (the FX slots) plus the automation store (phase 3): AUTO_GET (92), AUTO_SET (93) read and write a track's list of events (ed_stepx.c);
 *      asked (a firmware before it does not answer), INFO unchanged.
 *   F0 7D 46 4C cmd args.. F7     (7D = non-commercial ID, "FL")
 * Values are 14 bit, two 7-bit bytes LSB first, offset by 8192 (so -8192..8191).
 * Every request gets a reply with the same cmd; 23/24/26 are also pushed
 * while watched. Frames arrive through sx_frame (usb.c), replies leave
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
_Static_assert(10u + 4u * (P_COUNT + G_COUNT) + 6u <= ED_PAYLOAD_N, "MACRO (every value changed by the macros) fits one reply");

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
#include "ed_desc.h"           /* DESC's body (DESC, FXP, DLAN, STEPLANE; SCRN and SCRS its first four) */

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
#include "ed_user.c"           /* cmds 11..21: the sample slots and the user presets */
#include "ed_drums.c"          /* cmds 36..42: drum lanes, user kits, a slot read back */
#include "ed_dsrc.c"           /* cmds 50, 51: the drum sources and what a lane's SOUND pages show */
#include "ed_pages.c"          /* cmd 52: the pages (the editor lays out a sound as the device does) */
#include "ed_status.c"         /* cmd 53: the transport, the steps playing, the meters */
#if FELUCCA_MACROS
#include "ed_macro.c"          /* cmd 65: what the performance macros make of the values (macro.c) */
#else
#define ED_MACRO_FN
#endif
#include "ed_undo.c"           /* v14: cmd 102 undo and redo, INFO tag 0x5F; ed_undo_edit marks the editor's own step writes */
#if FELUCCA_AUTO
#include "ed_stepx.c"          /* cmds 92, 93 (v11): the automation store; 72..77: SLOOP 2.4's step extras on it */
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
#if (FELUCCA_SL24_EXPORT || FELUCCA_SL24_EDIMPORT) && FELUCCA_FLASH
#include "ed_sl24.c"           /* cmd 78: the working project and the settings for SLOOP 2.4 (its backup file); 90, 91 its import */
#else
#define ED_SL24_FN
#endif

/* the engine byte of DUMP / RELOAD / TRACK: NENGINES = the drum track (no engine) */
static uint32_t ed_eng(const track_t *t) { return is_drum(t) ? NENGINES : t->eng_req % NENGINES; }

/* ---- live sync (v2): while the editor WATCHes, device-side changes are pushed.
 * Shadows of the selected track's p[] + song.g[] and of its steps are kept in step
 * with what the editor knows (its own SET / STEP_SET update them); the main loop
 * compares and pushes CHANGED / STEP_CHANGED, or RELOAD after a load or when another
 * track was selected (v3: RELOAD and STEP_CHANGED carry the selected track). Pushes
 * go out only into a half-empty SysEx ring, so they never wait. */
#define ED_PUSH_MAX 4u                                   /* frames per pass */
#define ED_NV (P_COUNT + G_COUNT)
/* v4: the parameters of the tracks that are not selected which TRACK_CHANGED follows (the mixer) */
static const uint8_t ED_TIDS[] = {P_LEVEL, P_PAN, P_MUTE, P_DIST, P_CHOR, P_DLY, P_REV, P_FXOFF   /* (+ the FX sends: the Mix tab) */
#if SL24_TP
                                   , P_TFLT                                                     /* (v10: every FX slot's */
#endif
#if FELUCCA_MASTER_COMP
                                   , P_TCOMP                                                    /* amount: fx_slots.c) */
#endif
#if INS_FX
                                   , P_TFOLD, P_TFUZZ, P_TPHSR, P_TFLNG, P_TFRMT, P_TWIDE, P_TCHRS, P_TECHO, P_TGATE   /* (the
                                                                  * further inserts') */
#endif
#if FELUCCA_FX_DUST
                                   , P_TDUST                                                    /* (DUST's, 2026-10-14) */
#endif
#if FELUCCA_INS_JCHORUS
                                   , P_TJCHO                                                    /* (JUNO CHORUS's MIX) */
#endif
};
#define ED_NTID ((uint32_t)sizeof ED_TIDS)
#define ED_NT (NTRK * ED_NTID)
static struct {
    uint8_t on, eng, preset, sel;
    uint8_t v4;                                          /* WATCH bit 1: TRACK_CHANGED pushes too */
    uint8_t v9, stream;                                  /* WATCH bit 2: v9 pushes (ed_sync9.c); bit 3: STREAM */
    uint8_t ends;                                        /* WATCH bit 4 (v12): STEPS pushes carry the note ends */
    uint32_t pos, last_ms, run_ms, resets;
    int16_t v[ED_NV];                                    /* TSEL->p[], then song.g[] */
    uint16_t t[ED_NV];                                   /* ms (low 16 bits) of the last push */
    uint32_t st[NSTEP];                                  /* step signatures */
    int16_t tv[ED_NT];                                   /* v4: trk[k].p[ED_TIDS[j]] at k * ED_NTID + j */
    uint16_t tt[ED_NT];
    uint32_t tpos;
} ed_w;

static int16_t *ed_val(uint32_t i) { return i < P_COUNT ? &TSEL->p[i] : &song.g[i - P_COUNT]; }
static uint32_t ed_step_sig(const track_t *t, uint32_t i)   /* step i: all its bytes (a drum step's lanes, a synth
                                                          * step's own note ends) */
{
    uint32_t n, h = 0x811C9DC5u;
    const uint8_t *b = trk_step_raw(t, i, &n);
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
#include "ed_steps.c"         /* a step on the wire: STEP_GET / TRACK_STEP bytes (out and in), DRUM_STEP bytes */
static void ed_shadow_mix(void)                          /* v4: the other tracks' mix as it is (TRACK_CHANGED) */
{
    uint32_t i;
    for (i = 0; i < ED_NT; i++)
        ed_w.tv[i] = trk[i / ED_NTID].p[ED_TIDS[i % ED_NTID]];
}
static void ed_shadow_v(void)                            /* the selected track's parameters and the globals as they are */
{
    uint32_t i;
    for (i = 0; i < ED_NV; i++)
        ed_w.v[i] = *ed_val(i);
}
static void ed_shadow(void)                              /* the editor is in sync */
{
    uint32_t i;
    ed_shadow_v();
    for (i = 0; i < NSTEP; i++)
        ed_w.st[i] = ed_step_sig(TSEL, i);
    ed_shadow_mix();
    ed_w.eng = (uint8_t)ed_eng(TSEL);
    ed_w.preset = TSEL->preset;
    ed_w.sel = song.sel;
    sync_reload = 0;
}
#include "ed_steplane.c"     /* v14: cmd 105 a step in a drum lane's context and its lockable values, INFO tag 0x62 */
#include "ed_solo.c"          /* v14: cmd 104 track and lane solo and mute, INFO tag 0x61 */
#include "ed_rec.c"           /* v14: cmd 107 the REC key and its state, INFO tag 0x63 */
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
            ed_w.v[i] = e9.p[song.sel][i] = TSEL->p[i];
}
static int ed_room(void) { return so_w - so_r + 8u <= SXQ / 2u; }
static void ed_known(uint32_t k, uint32_t id)            /* the editor's own change of trk[k].p[id]: no push */
{
    uint32_t j;
    ed9_known_p(k, id);
    if (k == song.sel) {
        ed_w.v[id] = trk[k].p[id];
        return;
    }
    for (j = 0; j < ED_NTID; j++)
        if (ED_TIDS[j] == id)
            ed_w.tv[k * ED_NTID + j] = trk[k].p[id];
}

static void ed_sync(void)                                /* main loop */
{
    uint32_t i, n = 0, now = fm1_ms;
    if (!ed_w.on || now - ed_w.run_ms < 5u)
        return;
    ed_w.run_ms = now;
    if (!usb.config || usb.resets != ed_w.resets || now - ed_w.last_ms > 3000u) {
        ed_w.on = ed_w.v9 = ed_w.stream = mt.master = 0; /* no host, USB reset, or 3 s without a request */
        return;
    }
    if (ed_w.stream)
        ed_stream(now, ed9_room(ED_STREAM_N + 6u));      /* v9: the status stream, 25 Hz at most */
    if (sync_reload || ed_eng(TSEL) != ed_w.eng || TSEL->preset != ed_w.preset || song.sel != ed_w.sel) {
        if (!ed_room())
            return;
        ed_shadow();
        ed_begin(ED_RELOAD);
        ed_b3(ed_eng(TSEL), TSEL->preset, song.sel);
        ed_send();
        return;
    }
    if (ed_w.v9) {                                       /* v9: every track, coalesced (ed_sync9.c) */
        ed9_sync(now);
        return;
    }
    for (i = 0; i < NSTEP && n < ED_PUSH_MAX; i++) {
        uint32_t h = ed_step_sig(TSEL, i);
        if (h == ed_w.st[i])
            continue;
        if (!ed_room())
            return;
        ed_w.st[i] = h;
        ed_begin(ED_STEP_CHANGED);
        ed_b2(i, song.sel);
        ed_send();
        n++;
    }
    for (i = 0; i < ED_NV && n < ED_PUSH_MAX; i++) {    /* round robin: nothing starves */
        uint32_t k = (ed_w.pos + i) % ED_NV;
        int16_t v = *ed_val(k);
        if (v == ed_w.v[k] || (uint16_t)(now - ed_w.t[k]) < 20u)
            continue;                                    /* coalesced: the latest value goes out later */
        if (!ed_room())
            return;
        ed_w.v[k] = v;
        ed_w.t[k] = (uint16_t)now;
        ed_begin(ED_CHANGED);
        ed_b2(k < P_COUNT ? 0u : 1u, k < P_COUNT ? k : k - P_COUNT);
        ed_v(v);
        ed_send();
        n++;
        ed_w.pos = k + 1u;
    }
    for (i = 0; ed_w.v4 && i < ED_NT && n < ED_PUSH_MAX; i++) {   /* v4: the other tracks' mix */
        uint32_t k = (ed_w.tpos + i) % ED_NT, tr = k / ED_NTID, id = ED_TIDS[k % ED_NTID];
        int16_t v = trk[tr].p[id];
        if (tr == song.sel || v == ed_w.tv[k] || (uint16_t)(now - ed_w.tt[k]) < 20u)
            continue;                                    /* (the selected track: CHANGED above) */
        if (!ed_room())
            return;
        ed_w.tv[k] = v;
        ed_w.tt[k] = (uint16_t)now;
        ed_begin(ED_TRACK_CHANGED);
        ed_b2(tr, id);
        ed_v(v);
        ed_send();
        n++;
        ed_w.tpos = k + 1u;
    }
}

/* descriptor of parameter id of track t: the engine parameters of the engine it asked for
 * (t->engine follows in the audio ISR, after a short fade) */
static const param_desc_t *ed_tdesc(const track_t *t, uint32_t id)   /* the static ones: an engine's */
{                                                                     /* desc hook is the device display only */
    if(is_drum(t) && id==P_E0) return &DRUM_KIT_DESC;
    if (is_drum(t) && id == P_DACC)
        return &DRUM_ACC_DESC;                                        /* (the ACCENT amount) */
    if (id >= P_E0 && id <= P_E7) {
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        const param_desc_t *d = ENG_IS(e, FM6) ? fm6_ed_desc(id - P_E0) : 0;   /* (FM6's ENGINE: the modes built; */
        return d ? d : &e->edit[id - P_E0];                                      /* one built: "-", the row goes) */
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
                                      ED_SCOPE_FN ED_UNDO_FN ED_NOTE_FN ED_SOLO_FN ED_REC_FN ED_STEPLANE_FN 0};   /* 86..107 */
/* 65 the macros' effect on the values; 11..21: samples, user presets; 36..42: drum lanes, kits; 43..48: backup; 50, 51
 * drum sources; 52 pages; 53 status; 54..57 snapshots; 66 the CZ collection; 78, 90, 91 SLOOP 2.4; 79..84 patterns;
 * 72..77: step extras; 92, 93: the automation */
static const ed_cmd_t ED_CMDS[] = {ED_MACRO_FN ED_USER_FN ED_DRUMS_FN ED_BACKUP_FN ED_DSRC_FN ED_PAGES_FN ED_STATUS_FN ED_SNAP_FN
                                   ED_CZ_FN ED_SL24_FN ED_PAT_FN ED_STEPX_FN 0};
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
    if (k == song.sel)
        ed_w.st[i] = ed_step_sig(t, i);
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
        ed_b4(NENGINES, P_COUNT, G_COUNT, NSTEP); ed_b(P_E0);
        for (i = 0; i < NENGINES; i++)
            ed_str(ENGINES[i]->name, 8);
        ed_b(NTRK);                                       /* v3 */
        ed_b(6);                                          /* v5: the protocol version */
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
        ed_rec_info();                  /* tag 0x63: the REC key (v14, ed_rec.c) */
        ed_fx_info();                   /* tag 0x56: the FX slots' types (v10, ed_fxs.c) */
        ed_fxp_info();                  /* tag 0x57: the slots by owner (v13, ed_fxp.c) */
        ed_scope_info();                /* tag 0x5E: the scope (v13, ed_scope.c; the Optimist UI) */
        ed_fav_info();                  /* tag 0x5D: the favorites (v13, ed_fav.c) */
        ed_scrs_info();                 /* tag 0x5C: the screens as rows of cards (v13, ed_scrs.c; the Optimist UI) */
        ed_tools_info();                /* tag 0x5B: TOOLS and EUCLID (v13, ed_tools.c; the Optimist UI) */
        ed_songc_info();                /* tag 0x5A: the song chain (v13, ed_song.c) */
        ed_scrn_info();                 /* tag 0x59: GLOBAL and SYSTEM as lines (v13, ed_scrn.c; the Optimist UI) */
        ed_dlan_info();                 /* tag 0x58: the lanes' mix, LFOs and mutes (v13, ed_dlan.c) */
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
    case ED_SET:
        if (na < 2u || !(d = ed_desc(a[0], a[1], &vp)))
            return;
        if (cmd == ED_SET && na >= 4u) {
            if (a[0] == 1 && a[1] == G_ENGSEL) {          /* engine change: the safe path */
                ed_load_t lb;
                ed_load_before(&lb);
                set_engine((uint32_t)clamp(ed_rv(a + 2), 0, NENGINES - 1));
                ed_load_after(&lb);
            } else if (d->max > d->min) {
                *vp = (int16_t)clamp(ed_rv(a + 2), d->min, d->max);
            }
            ui.force = 1;
            if (a[0] == 1u) {                             /* the editor's own change: no push */
                ed_w.v[P_COUNT + a[1]] = *vp;
                ed9_known_g(a[1]);
            } else if (a[0] == 0u) {
                ed_w.v[a[1]] = *vp;
                ed9_known_p(song.sel, a[1]);
            }                                             /* (REVERB > TYPE: not pushed, nothing to skip) */
        }
        ed_b2(a[0], a[1]);
        ed_v(*vp);
        break;
    case ED_DUMP:
        ed_shadow_v();                                     /* the editor gets them all here */
        ed9_known_track(song.sel);
        memcpy(e9.g, song.g, sizeof e9.g);
        ed_b2(ed_eng(TSEL), TSEL->preset);
        ed_vs(TSEL->p, P_COUNT);
        ed_vs(song.g, G_COUNT);
        break;
    case ED_DESC:
        if (na < 2u || !(d = ed_desc(a[0], a[1], &vp)))
            return;
        ed_b2(a[0], a[1]);
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
        uint32_t keep, m, v4was = ed_w.v4, v9was = ed_w.v9, stwas = ed_w.stream;
        if (na < 1u)
            return;
        keep = ed_w.on && ed_w.resets == usb.resets && (a[0] & 1u);   /* already watching: the changes not yet */
        m = a[0] & 1u ? a[0] & 31u : 0u;                              /* pushed stay pending (INFO tag 53 01 bit 0, */
        ed_w.on = (uint8_t)(m & 1u);                                  /* Felucca 1.0.2 #65); bits 1..4 only while on */
        ed_w.v4 = (uint8_t)(m >> 1 & 1u);                /* v4: also TRACK_CHANGED; the reply says it is known */
        ed_w.v9 = (uint8_t)(m >> 2 & 1u);                /* v9: every track's changes, coalesced (ed_sync9.c) */
        ed_w.stream = (uint8_t)(m >> 3 & 1u);            /* v9: the status stream (ed_status.c) */
        ed_w.ends = (uint8_t)(m >> 4 & 1u);              /* v12: a synth step pushed with its notes' own ends */
        mt.master = ed_w.stream;                         /* (meters.c: the output's peak too) */
        ed_w.resets = usb.resets;
        if (ed_w.on && !keep) {
            ed_shadow();
            ed9_shadow_all();
        } else if (keep && ed_w.v4 && !v4was) {
            ed_shadow_mix();                               /* TRACK_CHANGED newly asked for: from the mix as it is */
        }
        if (keep && ed_w.v9 && !v9was)
            ed9_shadow_all();                              /* v9 newly asked for: from the device as it is */
        if (ed_w.stream && !(keep && stwas)) {             /* a new stream: its first frame goes out */
            memset(est.last, 0xFF, sizeof est.last);
            for (i = 0; i <= NTRK; i++)
                mt.ed[i] = 0;
            est.ms = fm1_ms - ED_STREAM_MS;
        }
        ed_b(m);                                           /* (on, v4, v9, stream, ends: what is on) */
        break;
    }
    case ED_PING:
        ed_b(0);
        break;
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
                ed_w.v[P_COUNT + G_DRLVL] = *lv, ed9_known_g(G_DRLVL);
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
            if (song.sel == TRK_DRUM)
                ed_w.st[a[0]] = ed_step_sig(TDRUM, a[0]);
        }
        ed9_known_step(TRK_DRUM, a[0]);
        ed_b(a[0]);
        ed_dstep_out(d);
        break;
    }
    case ED_TRACK_PARAM: {                                 /* track, id [, v14] -> track, id, v14 */
        track_t *t;
        if (na < 2u || a[0] >= NTRK || a[1] >= P_COUNT)
            return;
        t = &trk[a[0]];
        d = ed_tdesc(t, a[1]);
        if (na >= 4u) {
            if (d->max > d->min)                           /* as SET: clamped; a fixed value stays */
                t->p[a[1]] = (int16_t)clamp(ed_rv(a + 2), d->min, d->max);
            ed_known(a[0], a[1]);
            ui.force = 1;
        }
        ed_b2(a[0], a[1]);
        ed_v(t->p[a[1]]);
        break;
    }
    case ED_SYNC_STATS:                                    /* v9: what the sync costs (a measurement aid) */
        ed_b32(e9.scans, 5);                               /* (5 x 7 bits each) */
        ed_b32(e9.frames, 5);
        ed_b32(e9.bytes, 5);
        ed_b32(e9.t_max / FM1_TICKS_PER_US, 5);
        ed_b32(e9.t_sum / FM1_TICKS_PER_US, 5);
        ed_b32(est.frames, 5);
        ed_b32(mt.seen, 5);
        ed_b32(mt.missed, 5);
        if (na)
            e9.scans = e9.frames = e9.bytes = e9.t_max = e9.t_sum = est.frames = mt.seen = mt.missed = 0;
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
    ed_sync();                                             /* v2 pushes (while watched) */
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_HDR0 || p[1] != ED_HDR1 || p[2] != ED_HDR2)
        return;
    ed_w.last_ms = fm1_ms;                                 /* any request keeps WATCH alive */
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
