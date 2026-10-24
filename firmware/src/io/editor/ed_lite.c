/* SPDX-License-Identifier: GPL-3.0-only */
/* The editor protocol without live editing (builder item ED_LIVE off; included by editor.c in place of the live editor;
 * the user, 2026-10-18: "if we only do backup import / restore it would need only a few kb"; 2026-10-19: "base should
 * just be the communication layer, the common part, and we should move AS MUCH as possible in sub features"). The
 * base, EDITOR alone, is the frames and:
 *   1 INFO      the version, no engine, no parameter, no track, protocol 6, then the tag 0x64 (1 byte: 1): the page sees an
 *               FM-1 without live editing and offers what the sub-items below answer (web/editor.html loadLite)
 *   25 PING     49 BUILD (the builder's items: a backup file records which build it came from)
 * With BACKUP:
 *   11..15      the user sample slots (ed_user.c: a restore writes the USR slots through them; no user presets here)
 *   43..48      backup and restore of every stored object (ed_backup.c)
 *   54..57      the snapshots (ed_snap.c, FELUCCA_SNAPSHOTS): a backup's SNAP area is restored through their import
 *   110         the projects (ed_proj.c, PATTERNS): open, save, clear, name (the simulator moves one project)
 * With the librarian (builder item LIBRARIAN, 2026-10-19: "one entry in builder that allows me to select this, and not the
 * rest"), every piece of user data one at a time, still nothing edited live:
 *   1 INFO      as the live editor's header (engines, P_COUNT, tracks, UIDs; no G_* values), tag 0x64 = 3, tag 0x55
 *   9 PROJECT   a section loaded, saved, asked (as editor.c)
 *   16..21      the user presets (ed_user.c)   38..42 the user kits, a sample slot read back (ed_drums.c)
 *   79..84      the patterns and scenes (ed_pat.c, FELUCCA_PATTERNS)
 * No live sync, no WATCH, no GET / SET / DESC: the device is not edited from the page. */
#define ED_LITE_BK (FELUCCA_FLASH && FELUCCA_BACKUP)   /* (BACKUP: its restore writes the sample slots and the snapshots) */
#if ED_LITE_BK || FELUCCA_LIBRARIAN
#include "ed_user.c"           /* cmds 11..15: the sample slots (and the librarian's 16..21: the user presets) */
#include "ed_pack7.h"          /* (backup, snapshots and kits send pack7; the live editor has it through ed_drums.c) */
#else
#define ED_USER_FN
#endif
#if FELUCCA_SNAPSHOTS && ED_LITE_BK
#include "ed_snap.c"           /* cmds 54..57: snapshots (list, save / load / clear / rename, export, import) */
#else
#define ED_SNAP_FN
#endif
#if ED_LITE_BK
#include "ed_backup.c"         /* cmds 43..48: backup / restore of every stored object */
#else
#define ED_BACKUP_FN
#endif
#if ED_LITE_BK && SEC_LOGGED && FELUCCA_PATTERNS
#include "ed_proj.c"           /* cmd 110: the projects (open, save, clear, name; their streams are the backup's PJ01..) */
#else
#define ED_PROJ_FN
#endif
#if FELUCCA_LIBRARIAN
#include "ed_drums.c"          /* cmds 38..42: the user kits, a slot read back (the lanes: the live editor's) */
#if FELUCCA_PATTERNS
#include "ed_pat.c"            /* cmds 79..84: the per-track patterns and scenes */
#else
#define ED_PAT_FN
#endif
#else
#define ED_DRUMS_FN
#define ED_PAT_FN
#endif
typedef int (*ed_cmd_lite_t)(uint32_t cmd, const uint8_t *a, uint32_t na);
#define ED_TAG_LITE 0x64u                               /* INFO's tag: this build answers backup and restore only */
static const ed_cmd_lite_t ED_CMDS[] = {ED_USER_FN ED_DRUMS_FN ED_PAT_FN ED_BACKUP_FN ED_SNAP_FN ED_PROJ_FN 0};
#define ED_LITE_LIB (FELUCCA_LIBRARIAN ? 2u : 0u)        /* (tag 0x64's bit 1: the librarian's commands answer) */
static void ed_handle(const uint8_t *f, uint32_t n)   /* f: the bytes between F0 and F7 */
{
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    ed_begin(cmd);
    switch (cmd) {
    case ED_INFO:
        ed_str("FELUCCA " FELUCCA_VERSION, 24);
#if FELUCCA_LIBRARIAN                                     /* (the presets' engines and values: a record's layout) */
        ed_b(NENGINES); ed_b32(P_COUNT, 2); ed_b32(0, 2); ed_b(NSTEP); ed_b32(P_E0, 2);   /* (v7: 2 x 7 bit each) */
        for (i = 0; i < NENGINES; i++)
            ed_str(ENGINES[i]->name, 8);
        ed_b(NTRK);                                       /* v3 */
        ed_b(7);                                          /* v5: the protocol version (7: every id 2 x 7 bit) */
        for (i = 0; i < NENGINES; i++)                    /* v6: each slot's engine UID (registry.h) */
            ed_b(eng_uid(i));
#if FELUCCA_PATTERNS
        ed_b(0x55); ed_b(2);                              /* tag: the per-track patterns (as editor.c) */
        ed_b2(PAT_N, SEC_IDS);
#endif
#else
        ed_b(0); ed_b32(0, 2); ed_b32(0, 2); ed_b(NSTEP); ed_b32(0, 2);   /* (no engine, no parameter: nothing to edit) */
        ed_b(0);                                          /* v3: no track */
        ed_b(7);                                          /* v5: the protocol version (tags follow from v6; 7: ids 2 x 7 bit) */
#endif
        ed_b3(ED_TAG_LITE, 1, 1u | ED_LITE_LIB);
        ed_mirror_info();                                 /* tag 0x65: the screen mirrored (ed_mirror.c) */
        ed_opti_info();                                   /* tag 0x66: the features by name and version (ed_opti.h) */
        break;
#if FELUCCA_LIBRARIAN
    case ED_PROJECT:                                      /* 0 = load, 1 = save, 2 = query (as editor.c) */
        if (na < 2u || a[0] > 2u)
            return;
        i = a[1] % FELUCCA_SECTIONS;
#if FELUCCA_ARRANGER
        if (a[0] < 2u && (song.playing || transport_req)) {
            ed_b4(a[0], i, project_used(i), 1);           /* transport busy */
            break;
        }
#endif
        if (a[0] == 1u)
            project_save(i);
        else if (a[0] == 0u)
            project_load(i);
        ed_b2(a[0], i);
#if FELUCCA_SL24_SAFE
        ed_b((uint8_t)project_state(i));
#else
        ed_b(project_used(i));
#endif
        break;
#endif
    case ED_BUILD:                                        /* v6: what this build contains (tools/builder) */
        ed_str(FELUCCA_CFG_NAME, 16);
        ed_b32((uint32_t)FELUCCA_CFG_HASH, 5);
        {
            static const uint8_t bits[] = FELUCCA_CFG_BITS;
            ed_b(sizeof bits);
            ed_bs(bits, sizeof bits);
        }
        break;
    case ED_PING:
        ed_b(0);
        break;
#if FELUCCA_ED_MIRROR
    case ED_MIRROR:                                       /* the screen mirrored: on / off (ed_mirror.c) */
        ed_mirror(a, na);
        break;
    case ED_PANEL:                                         /* the panel pressed by the mirror's client (ed_mirror.c) */
        ed_panel(a, na);
        break;
    case ED_NAV:                                           /* the client's touch navigation (ed_mirror.c, ui/optimist/op_nav.c) */
        ed_nav(a, na);
        break;
#endif
    default:
        for (i = 0; ED_CMDS[i]; i++)
            if (ED_CMDS[i](cmd, a, na))
                break;
        if (!ED_CMDS[i])
            return;                                       /* (not one of them: no reply, as a build without a command) */
        break;
    }
    ed_send();
}
/* main loop: editor frames first; anything else stays for ota_service() */
static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    mir_poll();                                            /* the screen mirrored: a Bluetooth request, a catch-up (ed_mirror.c) */
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_HDR0 || p[1] != ED_HDR1 || p[2] != ED_HDR2)
        return;
    MIR_SEEN();                                            /* (any request keeps the mirror on: ed_mirror.c) */
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
