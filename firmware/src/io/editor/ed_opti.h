/* SPDX-License-Identifier: GPL-3.0-only */
/* INFO's tag 0x66: what this build answers, by name and version ("opti_backup_4 opti_mirror_2 ..."; web/EDITOR_PROTOCOL.md
 * "INFO"). Every firmware of the Felucca family answers INFO with the same "FL" header, but each numbers its other commands
 * its own way (SLOOP's backup is 34..36, ours 43..48; 78 is Melodee's CZ), so a client sends nothing
 * else before it has read these names: a feature is offered only when its name and a version the client reads are listed.
 * A number goes up only when an older client would misread the new form (the user, 2026-10-23: "a command with a specific
 * name the indicate it is compatible with our way of doing things (and a version of the prototol, like opti_backup_1"). */
#ifndef ED_OPTI_H
#define ED_OPTI_H
#define ED_OPTI_FULL (FELUCCA_ED_LIVE || FELUCCA_LIBRARIAN)   /* (live editing, or the librarian: presets, sections) */
#if FELUCCA_FLASH && FELUCCA_BACKUP && FELUCCA_ED_COMPANION
#define ED_OPTI_BK "opti_cpfiles_1 "    /* (the backup's objects and the sample slots as fm1-base companion files, BKINFO and
                                       * BKCTL beside them: ed_comp.c; 43..48 and 11..15 not answered) */
#elif FELUCCA_FLASH && FELUCCA_BACKUP
#define ED_OPTI_BK "opti_backup_4 "     /* (ed_backup.c BK_VERSION) */
#else
#define ED_OPTI_BK ""
#endif
#if FELUCCA_ED_MIRROR && MIR_BLE
#define ED_OPTI_MIR "opti_mirror_2 opti_panel_1 opti_nav_1 opti_mirbt_1 "   /* (opti_mirbt_1: INFO, PANEL and NAV over Bluetooth
                                                                            * too, ed_mirror.c mir_ble_rx) */
#elif FELUCCA_ED_MIRROR
#define ED_OPTI_MIR "opti_mirror_2 opti_panel_1 opti_nav_1 "   /* (ed_mirror_enc.h MIR_VERSION; 108 PANEL, 109 NAV, ed_mirror.c) */
#else
#define ED_OPTI_MIR ""
#endif
#if ED_OPTI_FULL
#define ED_OPTI_LIB "opti_library_1 opti_projects_1 "   /* (16..21 the user presets; 9 PROJECT the sections) */
#else
#define ED_OPTI_LIB ""
#endif
#if ED_OPTI_FULL && FELUCCA_PATTERNS
#define ED_OPTI_PAT "opti_patterns_1 "  /* (79..84) */
#else
#define ED_OPTI_PAT ""
#endif
#if FELUCCA_FLASH && FELUCCA_BACKUP && SEC_LOGGED && FELUCCA_PATTERNS
#define ED_OPTI_PJ "opti_proj_1 "       /* (110: the projects, ed_proj.c) */
#else
#define ED_OPTI_PJ ""
#endif
#if FELUCCA_ED_MIRROR && MBR_PROJ
#if !defined(MIR_AIMD) || MIR_AIMD
#define ED_OPTI_AIMD "opti_miraimd_1 " /* (a second tag 0x67, the first being full: 106 bit 5 the client's loss count and next seq,
                                       * the Bluetooth stream paced to a budget, ed_mirror.c MIR_AIMD) */
#endif
#define ED_OPTI_MORE "opti_clip_1 opti_btproj_1 opti_mstate_1 opti_panelx_1 opti_mtest_1 opti_mirfix_1 "   /* (tag 0x67, 0x66 being full: 111
                                                   * the clip, ed_clip.c; opti_btproj_1: the projects' moves over Bluetooth too,
                                                   * ed_mirror.c MBR_PROJ; 106 op 4 STATE, 108 kinds 3 MASTER and 4 let go, its
                                                   * seq, 108 kind 5 the test session: ed_mirror.c; opti_mirfix_1: 106 bit 4,
                                                   * lost frames by seq, ed_mirror.c mir_fix) */
#elif FELUCCA_ED_MIRROR
#define ED_OPTI_MORE "opti_clip_1 opti_mstate_1 opti_panelx_1 opti_mtest_1 "   /* (tag 0x67, 0x66 being full: 111 the clip,
                                                                     * ed_clip.c; 106 op 4 STATE, 108 kinds 3 MASTER and 4 let go, its
                                                                     * seq, 108 kind 5 the test session: ed_mirror.c) */
#else
#define ED_OPTI_MORE ""
#endif
#ifndef ED_OPTI_AIMD
#define ED_OPTI_AIMD ""
#endif
#if FELUCCA_ED_MIRROR && (defined(MIR_BLE) && MIR_BLE)
#define ED_OPTI_SUM "opti_mirsum_1 "  /* (tag 0x67, the second: 106 op 7 SUMS, the bands request 106 bits 112; ed_mirror_sum.h, with MIR_SKIP) */
#else
#define ED_OPTI_SUM ""
#endif
#if FELUCCA_ED_MIRROR && MIR_AA
#define ED_OPTI_MAA "opti_miraa_1 "   /* (tag 0x67: 106's second byte, bit 0 the AA token, ed_mirror_enc.h MIR_AA) */
#else
#define ED_OPTI_MAA ""
#endif
#if defined(FELUCCA_WIFI) && FELUCCA_WIFI && FELUCCA_BLE && ((BLE_HW_WL82 && FELUCCA_CONSOLE) || defined(WIFI_ON_FM1B))   /* (the
                                         * release: fm1-base's own Wi-Fi stack, the trial's ui/opt_wifi.h) */
#define ED_OPTI_WIFI "opti_wifi_1 "     /* (tag 0x67: 112 the Wi-Fi networks, ed_wifi.c, included by felucca.c after console.c) */
#define ED_HAS_WIFI 1
#define ED_WIFI_FN ed_wifi,             /* (editor.c, ed_lite.c: their tables of commands; the function is defined later) */
static int ed_wifi(uint32_t cmd, const uint8_t *a, uint32_t na);
#else
#define ED_OPTI_WIFI ""
#define ED_WIFI_FN
#endif
#if defined(UPF_ON) && UPF_ON && ED_LIB
#define ED_OPTI_UPF "opti_upfm6_1 "   /* (tag 0x67: UP_GET / UP_PUT carry an FM6 slot's own voice, ed_user.c; issue #133) */
#else
#define ED_OPTI_UPF ""
#endif
#if FELUCCA_DRUMS
#define ED_OPTI_ND ""
#else
#define ED_OPTI_ND "opti_nodrums_1 "  /* (tag 0x67: no drum track, track 4 a synth part like 1..3: core.h FELUCCA_DRUMS) */
#endif
static void ed_opti_list(uint32_t tag, const char *s, uint32_t n)   /* a tag of names (n: with the 0 at its end) */
{
    uint32_t i;
    if (n < 2u)
        return;
    ed_b(tag);
    ed_b(n - 1u);
    for (i = 0; i + 1u < n; i++)
        ed_b((uint8_t)s[i]);
}
#ifdef ED_BOOT_READ                       /* (felucca.c: st_read; fm1-base's opt_editor.h: plat_boot_read) */
#include "system/bootid.h"
/* INFO tag 0x68: which bootloader the FM-1 has, "boot=mboot", "boot=stock" or "boot=unknown" (system/bootid.h): the
 * installers take the update loader with m-bootloader there, instead of planning download mode to install it. Read once
 * (a failed read again next time) */
static void ed_boot_info(void)
{
    static uint8_t id = 0xFFu;
    const char *w;
    uint32_t r = id, n = 0;
    if (r == 0xFFu) {
        r = bootid_read(ED_BOOT_READ);
        if (r != BOOTID_UNKNOWN)
            id = (uint8_t)r;
    }
    w = bootid_word(r);
    while (w[n])
        n++;
    ed_b(0x68);
    ed_b(n);
    while (*w)
        ed_b((uint8_t)*w++);
}
#else
#define ed_boot_info() ((void)0)
#endif
static void ed_opti_info(void)
{
    static const char S[] = ED_OPTI_BK ED_OPTI_MIR ED_OPTI_LIB ED_OPTI_PAT ED_OPTI_PJ;
    uint32_t i;
    _Static_assert(sizeof S < 128u, "INFO tag 0x66: one 7-bit length");
#if FELUCCA_ED_MIRROR
    _Static_assert(MIR_VERSION == 2, "ed_opti.h: opti_mirror_ names MIR_VERSION");
#endif
    static const char M[] = ED_OPTI_MORE ED_OPTI_ND ED_OPTI_WIFI ED_OPTI_UPF;
    _Static_assert(sizeof M < 128u, "INFO tag 0x67: one 7-bit length");
    ed_b(0x66);
    ed_b(sizeof S - 1u);
    for (i = 0; i + 1u < sizeof S; i++)
        ed_b((uint8_t)S[i]);
    ed_opti_list(0x67, M, sizeof M);   /* (tag 0x67: more names, the same form; a client that knows none skips it) */
    {
        static const char A[] = ED_OPTI_AIMD ED_OPTI_SUM ED_OPTI_MAA;
        ed_opti_list(0x67, A, sizeof A);   /* (0x67 again: the clients merge every 0x66 / 0x67 they read) */
    }
    ed_boot_info();                    /* (tag 0x68: the bootloader, a client that knows none skips it) */
}
#endif
