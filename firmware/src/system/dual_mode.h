/* SPDX-License-Identifier: GPL-3.0-only */
/* The second core's setting and why it is down (FELUCCA_DUAL, dual.c). Pure: the host tests include it
 * (tests/dual_guard_test.c), and so does the settings word (storage/settings_word.c: bits 27..28).
 *
 * 2ND CORE, a setting of the FM-1 (after Jangada 0.9.6's AUTO / OFF / BOOST, zednaked/jangada @ 07cce38, GPL-3.0-only):
 *   AUTO (0, the default, and what a settings record without it reads): CPU1 starts at power-on. On its first misread
 *        (a request or argument nobody wrote, fm1_dual_guard.h), a wrong boot canary, a job that does not come back or a
 *        fault, it is held for the rest of the session and the FM-1 goes on with one core; with DUAL_MODE_MENU the
 *        setting then turns itself OFF (saved), so the next boots stay on one core until it is set back to AUTO.
 *   OFF (1): one core.
 * A boot that follows one that crashed in its first 30 s (bootguard.failed) stays on one core too, whatever the setting.
 * The supply BOOST is a builder item (FELUCCA_DUAL_BOOST, hal/fm1_supply.h), not a value here: the user's ruling, a fix
 * stays in the builder until it is verified on hardware. */
#ifndef DUAL_MODE_H
#define DUAL_MODE_H
#include <stdint.h>

#ifndef DUAL_MODE_MENU
#if defined(FELUCCA_UI) && FELUCCA_UI == 1
#define DUAL_MODE_MENU 1       /* the Optimist UI's SYSTEM > CPU > 2ND CORE sets it back (ui/optimist/op_project.c) */
#else
#define DUAL_MODE_MENU 0       /* 1 once a menu can set 2ND CORE back to AUTO: only then is the self-disable saved */
#endif
#endif

enum { DUALM_AUTO, DUALM_OFF, DUALM_N };
#define DUAL_MODE_BITS 27u     /* in the settings word (settings_word.c), 2 bits; 2 and 3 read as AUTO */

/* why CPU1 is not taking jobs (dual.up 0); DUAL_UP while it runs */
enum { DUAL_UP, DUAL_NO_HELLO, DUAL_TIMEOUT, DUAL_FAULT, DUAL_FLASH, DUAL_MISREAD, DUAL_CANARY, DUAL_EARLY, DUAL_SETOFF,
       DUAL_WHY_N };

/* the reason's name, as the screens show it ("" for DUAL_UP; a value out of range reads as DUAL_UP's) */
static inline const char *dual_why_name(uint32_t why)
{
    static const char *const DUAL_WHY_TXT[DUAL_WHY_N] = {"", "NO START", "TIMEOUT", "FAULT", "FLASH", "MISREAD", "CANARY",
                                                         "AFTER CRASH", "SETTING"};
    return DUAL_WHY_TXT[why % DUAL_WHY_N];
}

static inline uint32_t dual_mode_in(uint32_t w) { return ((w >> DUAL_MODE_BITS) & 3u) == DUALM_OFF ? DUALM_OFF : DUALM_AUTO; }
static inline uint32_t dual_mode_out(uint32_t w, uint32_t mode)
{
    return (w & ~(3u << DUAL_MODE_BITS)) | (uint32_t)(mode == DUALM_OFF) << DUAL_MODE_BITS;
}

/* at boot: DUAL_UP = start CPU1; else why not. EARLY_CRASHES: bootguard.failed (boots that ended in their first 30 s) */
static inline uint32_t dual_boot_why(uint32_t mode, uint32_t early_crashes)
{
    if (mode == DUALM_OFF)
        return DUAL_SETOFF;
    if (early_crashes)
        return DUAL_EARLY;
    return DUAL_UP;
}

/* a reason that says this FM-1 cannot run CPU1 at this supply (AUTO turns itself OFF); not a start that never answered
 * (nothing ran), not a boot kept on one core by a crash or by the setting */
static inline int dual_why_parks(uint32_t why)
{
    return why == DUAL_TIMEOUT || why == DUAL_FAULT || why == DUAL_FLASH || why == DUAL_MISREAD || why == DUAL_CANARY;
}

/* the setting after CPU1 went down for WHY on a boot that started with BOOT_MODE */
static inline uint32_t dual_mode_after(uint32_t mode, uint32_t boot_mode, uint32_t why)
{
    if (DUAL_MODE_MENU && mode == DUALM_AUTO && boot_mode == DUALM_AUTO && dual_why_parks(why))
        return DUALM_OFF;
    return mode;
}
#endif
