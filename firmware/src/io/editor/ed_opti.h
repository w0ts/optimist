/* SPDX-License-Identifier: GPL-3.0-only */
/* INFO's tag 0x66: what this build answers, by name and version ("opti_backup_4 opti_mirror_2 ..."; web/EDITOR_PROTOCOL.md
 * "INFO"). Every firmware of the Felucca family answers INFO with the same "FL" header, but each numbers its other commands
 * its own way (SLOOP's backup is 34..36, ours 43..48; 78 is Melodee's CZ and our SLOOP files), so a client sends nothing
 * else before it has read these names: a feature is offered only when its name and a version the client reads are listed.
 * A number goes up only when an older client would misread the new form (the user, 2026-10-23: "a command with a specific
 * name the indicate it is compatible with our way of doing things (and a version of the prototol, like opti_backup_1"). */
#ifndef ED_OPTI_H
#define ED_OPTI_H
#define ED_OPTI_FULL (FELUCCA_ED_LIVE || FELUCCA_LIBRARIAN)   /* (live editing, or the librarian: presets, sections) */
#if FELUCCA_FLASH && FELUCCA_BACKUP
#define ED_OPTI_BK "opti_backup_4 "     /* (ed_backup.c BK_VERSION) */
#else
#define ED_OPTI_BK ""
#endif
#if FELUCCA_ED_MIRROR
#define ED_OPTI_MIR "opti_mirror_2 "    /* (ed_mirror_enc.h MIR_VERSION) */
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
static void ed_opti_info(void)
{
    static const char S[] = ED_OPTI_BK ED_OPTI_MIR ED_OPTI_LIB ED_OPTI_PAT;
    uint32_t i;
    _Static_assert(sizeof S < 128u, "INFO tag 0x66: one 7-bit length");
#if FELUCCA_ED_MIRROR
    _Static_assert(MIR_VERSION == 2, "ed_opti.h: opti_mirror_ names MIR_VERSION");
#endif
    ed_b(0x66);
    ed_b(sizeof S - 1u);
    for (i = 0; i + 1u < sizeof S; i++)
        ed_b((uint8_t)S[i]);
}
#endif
