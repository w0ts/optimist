/* SPDX-License-Identifier: GPL-3.0-only */
/* The console's text helpers and the parser of the run-control commands (play, pause, rec, proj load, heavy, wdtreset).
 * Pure: no firmware state, so tests/console_parse_test.c compiles it on the host. console.c includes it; the actions
 * are console_run.c's (main loop). */
#ifndef CONSOLE_PARSE_H
#define CONSOLE_PARSE_H
#include <stdint.h>

static uint32_t con_num(const char **p, int *ok)        /* decimal or 0x hex */
{
    const char *s = *p;
    uint32_t v = 0, base = 10, d, n = 0;
    while (*s == ' ')
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2, base = 16;
    for (;; s++, n++) {
        char c = *s;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else
            break;
        v = v * base + d;
    }
    *ok = n > 0;
    *p = s;
    return v;
}

static int con_word(const char **p, const char *w)       /* match a whole word */
{
    const char *s = *p;
    while (*s == ' ')
        s++;
    while (*w && *s == *w)
        s++, w++;
    if (*w || (*s && *s != ' '))
        return 0;
    *p = s;
    return 1;
}

/* the run-control commands: CR_NONE is "not one of these" (con_exec goes on), CR_BAD a usage error */
enum { CR_NONE, CR_BAD, CR_PLAY, CR_STOP, CR_REC_ON, CR_REC_OFF, CR_PROJ_LOAD, CR_HEAVY, CR_WDTRESET };
#define CR_SLOTS 4u                                     /* the project slots (project.c proj_slot) */
#define CR_NOTES_MAX 4u                                 /* a step's notes (step_t.note) */
typedef struct {
    uint8_t op;
    uint8_t arg;                                        /* PROJ_LOAD: the slot 0..3; HEAVY: notes a step 1..4 */
} con_run_t;

static int con_end(const char *p)                       /* nothing but blanks left */
{
    while (*p == ' ')
        p++;
    return *p == 0;
}

static con_run_t con_run_parse(const char *p)
{
    con_run_t r = {CR_NONE, 0};
    int ok;
    uint32_t v;
    if (con_word(&p, "play"))
        r.op = CR_PLAY;
    else if (con_word(&p, "pause") || con_word(&p, "stop"))
        r.op = CR_STOP;                                 /* (the sequencer has no pause: STOP, the next PLAY from the top) */
    else if (con_word(&p, "wdtreset"))
        r.op = CR_WDTRESET;
    else if (con_word(&p, "rec")) {
        r.op = con_word(&p, "on") ? CR_REC_ON : con_word(&p, "off") ? CR_REC_OFF : CR_BAD;
    } else if (con_word(&p, "proj") || con_word(&p, "project") || con_word(&p, "song")) {
        if (!con_word(&p, "load"))
            return (con_run_t){CR_BAD, 0};
        v = con_num(&p, &ok);
        if (!ok || v >= CR_SLOTS)
            return (con_run_t){CR_BAD, 0};
        r.op = CR_PROJ_LOAD, r.arg = (uint8_t)v;
    } else if (con_word(&p, "heavy")) {
        r.op = CR_HEAVY, r.arg = 1;
        if (!con_end(p)) {
            v = con_num(&p, &ok);
            if (!ok || v < 1u || v > CR_NOTES_MAX)
                return (con_run_t){CR_BAD, 0};
            r.arg = (uint8_t)v;
        }
    } else
        return r;
    if (r.op != CR_BAD && !con_end(p))
        r.op = CR_BAD;                                  /* (trailing words: refused, never half-run) */
    return r;
}
#endif
