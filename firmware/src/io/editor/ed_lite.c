/* SPDX-License-Identifier: GPL-3.0-only */
/* The editor protocol for backup and restore only (builder item ED_LIVE off; included by editor.c in place of the live
 * editor; the user, 2026-10-18: "if we only do backup import / restore it would need only a few kb"). What the web page's
 * Backup needs and nothing else:
 *   1 INFO      the version, no engine, no parameter, no track, protocol 6, then the tag 0x64 (1 byte: 1): the page sees a
 *               backup-only FM-1 and offers its Backup alone (web/editor.html loadLite)
 *   25 PING     49 BUILD (the builder's items: the backup file records which build it came from)
 *   11..15      the user sample slots (ed_user.c: a restore writes the USR slots through them; no user presets here)
 *   43..48      backup and restore of every stored object (ed_backup.c)
 *   54..57      the snapshots (ed_snap.c, FELUCCA_SNAPSHOTS): a backup's SNAP area is restored through their import
 * No live sync, no WATCH, no GET / SET / DESC: the device is not edited from the page. */
#include "ed_user.c"           /* cmds 11..15: the sample slots */
#include "ed_pack7.h"          /* (backup and snapshots send pack7; the live editor has it through ed_drums.c) */
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
typedef int (*ed_cmd_lite_t)(uint32_t cmd, const uint8_t *a, uint32_t na);
#define ED_TAG_LITE 0x64u                               /* INFO's tag: this build answers backup and restore only */
static const ed_cmd_lite_t ED_CMDS[] = {ED_USER_FN ED_BACKUP_FN ED_SNAP_FN 0};
static void ed_handle(const uint8_t *f, uint32_t n)   /* f: the bytes between F0 and F7 */
{
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    ed_begin(cmd);
    switch (cmd) {
    case ED_INFO:
        ed_str("FELUCCA " FELUCCA_VERSION, 24);
        ed_b4(0, 0, 0, NSTEP); ed_b(0);                   /* (no engine, no parameter: nothing to edit) */
        ed_b(0);                                          /* v3: no track */
        ed_b(6);                                          /* v5: the protocol version (tags follow from v6) */
        ed_b3(ED_TAG_LITE, 1, 1);
        break;
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
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_HDR0 || p[1] != ED_HDR1 || p[2] != ED_HDR2)
        return;
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
