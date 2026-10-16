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
 * The FM-1's own supply start-up is a builder item (FELUCCA_STOCK_SUPPLY, hal/fm1_supply.h), not a value here; when its
 * read-back is wrong CPU1 is not started (SUPPLY, dual_supply_why). */
#ifndef DUAL_MODE_H
#define DUAL_MODE_H
#include <stdint.h>

#ifndef DUAL_MODE_MENU
#define DUAL_MODE_MENU 0       /* 1 once a menu can set 2ND CORE back to AUTO: only then is the self-disable saved. The Optimist UI has
                                * none (2026-10-14: the second core is always on, SYSTEM > INFO only shows its load or why it is down),
                                * so a self-disable lasts for the session; the next boot starts CPU1 again (a boot that crashed in its
                                * first 30 s still stays on one core, bootguard) */
#endif

enum { DUALM_AUTO, DUALM_OFF, DUALM_N };
#define DUAL_MODE_BITS 27u     /* in the settings word (settings_word.c), 2 bits; 2 and 3 read as AUTO */

/* why CPU1 is not taking jobs (dual.up 0); DUAL_UP while it runs */
enum { DUAL_UP, DUAL_NO_HELLO, DUAL_TIMEOUT, DUAL_FAULT, DUAL_FLASH, DUAL_MISREAD, DUAL_CANARY, DUAL_EARLY, DUAL_SETOFF,
       DUAL_PARKED, DUAL_BLE, DUAL_OVERLOAD, DUAL_SUPPLY, DUAL_WHY_N };   /* SUPPLY: the supply start-up's read-back
                                                    * was wrong (hal/fm1_supply.h): CPU1 never started */

/* the reason's name, as the screens show it ("" for DUAL_UP; a value out of range reads as DUAL_UP's) */
static inline const char *dual_why_name(uint32_t why)
{
    static const char *const DUAL_WHY_TXT[DUAL_WHY_N] = {"", "NO START", "TIMEOUT", "FAULT", "FLASH", "MISREAD", "CANARY",
                                                         "AFTER CRASH", "SETTING", "PARKED", "BLUETOOTH", "OVERLOAD",
                                                         "SUPPLY"};
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

/* at boot, after dual_boot_why: a supply start-up whose read-back was wrong (SUPPLY_ERR, hal/fm1_supply.h FM1_SUP_*)
 * keeps CPU1 off; it outranks PARKED (the user reads why the rails are suspect), not SETTING or AFTER CRASH */
static inline uint32_t dual_supply_why(uint32_t why, uint32_t supply_err)
{
    return supply_err && (why == DUAL_UP || why == DUAL_PARKED) ? DUAL_SUPPLY : why;
}

/* PARKED (2026-10-15, the user's ruling until the hardware tests settle the second core: the FM-1's power-on crashes with CPU1 rendering
 * jobs): a DUAL build never starts CPU1 unless it is built with OPTIMIST_DUAL_PARK=0 (tools/build.py, for hardware test builds),
 * so no render job ever runs on it; SYSTEM > INFO says "2ND CORE OFF PARKED" (the CPU1 diagnostics lines stay) */
#ifndef FELUCCA_DUAL_PARK
#define FELUCCA_DUAL_PARK 0       /* (1 until 2026-10-15 evening: replaced by the BLUETOOTH rule below; OPTIMIST_DUAL_PARK=1 builds
                                   * still park) */
#endif

/* BLUETOOTH (2026-10-15, the user's ruling after the hardware tests: with the BLE radio started, CPU1 running faulted on
 * every boot, at random places; with BLUETOOTH OFF it ran clean): in a build with BLE the second core runs only while
 * BLUETOOTH is OFF. It is started from the main loop after the boot (the settings read), only if BLUETOOTH is OFF then;
 * switching BLUETOOTH ON stops it first (between two audio blocks: CPU0 renders everything from the next), and it is not
 * started again until the next boot; SYSTEM > INFO says "2ND CORE OFF BLUETOOTH". Without BLE in the build: as before */
static inline uint32_t dual_ble_rule(uint32_t why, int ble_in_build, int ble_on)
{
    return why == DUAL_UP && ble_in_build && ble_on ? DUAL_BLE : why;
}
static inline uint32_t dual_park(uint32_t why, uint32_t park) { return why == DUAL_UP && park ? DUAL_PARKED : why; }

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

/* ---- SYSTEM > INFO's diagnostics of CPU1 (2026-10-15: TIMEOUT on the FM-1 with no stop record, dual.c)
 * How far CPU1 got (fm1_dual_mb.stage, CPU1 writes the furthest): the C entry, its hello, its wait loop (asleep in
 * `idle`, or spinning in RAM), its wake handler entered (asleep only), its first job picked up, its first job done */
enum { C1S_NONE, C1S_MAIN, C1S_HELLO, C1S_LOOP, C1S_WOKEN, C1S_TOOK, C1S_DONE, C1S_N };
static inline const char *dual_stage_name(uint32_t stage, int sleeps)
{
    static const char *const C1S_TXT[C1S_N] = {"NONE", "MAIN", "HELLO", "", "WOKEN", "TOOK JOB", "JOB DONE"};
    if (stage == C1S_LOOP)
        return sleeps ? "SLEEP" : "SPIN";
    return stage < C1S_N ? C1S_TXT[stage] : "?";
}

/* v appended to b, short: 0..9999 as is, then k, then M (b has room for 6 more characters) */
static inline void dual_cnum(char *b, uint32_t v)
{
    char t[8], sfx = 0;
    uint32_t k = 0;
    while (*b)
        b++;
    if (v >= 10000000u) {
        v /= 1000000u;
        sfx = 'M';
    } else if (v >= 10000u) {
        v /= 1000u;
        sfx = 'k';
    }
    do
        t[k++] = (char)('0' + v % 10u);
    while ((v /= 10u) && k < 4u);
    while (k)
        *b++ = t[--k];
    if (sfx)
        *b++ = sfx;
    *b = 0;
}
static inline void dual_chex(char *b, uint32_t v, uint32_t digits)   /* the low DIGITS hex digits of v appended */
{
    while (*b)
        b++;
    while (digits--)
        *b++ = "0123456789ABCDEF"[(v >> (4u * digits)) & 15u];
    *b = 0;
}
static inline void dual_cat(char *b, const char *s)   /* s appended to b */
{
    while (*b)
        b++;
    while ((*b++ = *s++))
        ;
}

/* the stop record (dual.c dual_down): every path that puts CPU1 down, whether it was in a job or not */
typedef struct {
    uint8_t stopped;           /* 1 once CPU1 was put down (any reason; NO START too) */
    uint8_t in_job;            /* a job was posted and not done */
    uint8_t took;              /* CPU1 had picked that job up (fm1_dual_mb.took == req) */
    uint8_t at;                /* 1 + the job it was in (1..4 a part, 5 the drums, 6 the reverb); 0: none begun */
    uint8_t no_clock;          /* fm1_ms was still 0 (TIMER5's first tick not taken yet: the first audio interrupt) */
    uint32_t c1_ms;            /* ms since CPU1 was started (TIMER4) */
    uint32_t job;              /* jobs joined before: the index of the job it stopped in (0: the first one) */
    uint32_t wait_us;          /* how long CPU0 had waited for that job */
} dual_stop_t;

/* the CPU1 STOP line: V (14 bytes) "--" | "<t>[*] W" (never picked up; t: ms under 10 s, else s) | "J<n>" (in job n) | "IDLE" (no job
 * posted: a boot check, a flash operation); U (16 bytes) " #<job>" (in a job; and " <wait>us" when CPU0 had waited
 * less than a half) or "" */
static inline void dual_stop_text(const dual_stop_t *s, char *v, char *u)
{
    v[0] = u[0] = 0;
    if (!s->stopped) {
        dual_cat(v, "--");
        return;
    }
    dual_cnum(v, s->c1_ms < 10000u ? s->c1_ms : s->c1_ms / 1000u);
    dual_cat(v, s->c1_ms < 10000u ? "ms" : "s");
    dual_cat(v, s->no_clock ? "* " : " ");
    if (!s->in_job) {
        dual_cat(v, "IDLE");
        return;
    }
    if (s->took) {
        dual_cat(v, "J");
        dual_cnum(v, s->at);
    } else {
        dual_cat(v, "W");
    }
    dual_cat(u, " #");
    dual_cnum(u, s->job);
    if (s->wait_us < 5000u) {                  /* (not a whole half: a fault or a misread came first) */
        dual_cat(u, " ");
        dual_cnum(u, s->wait_us);
        dual_cat(u, "us");
    }
}
#endif
