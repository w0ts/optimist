/* SPDX-License-Identifier: GPL-3.0-only */
/* The console's run-control parser (firmware/src/io/console_parse.h): play, pause, stop, rec, proj load, heavy,
 * wdtreset; lines that are not its own fall through (CR_NONE); malformed ones are CR_BAD, never half-run. Host only. */
#include <stdio.h>
#include <string.h>

#include "../firmware/src/io/console_parse.h"

static int fails;
static void chk(const char *line, int op, int arg)
{
    con_run_t r = con_run_parse(line);
    if (r.op != op || (op != CR_BAD && op != CR_NONE && r.arg != arg)) {
        printf("FAIL '%s': op %d arg %d, want %d %d\n", line, r.op, r.arg, op, arg);
        fails++;
    }
}

int main(void)
{
    chk("play", CR_PLAY, 0);
    chk("  play  ", CR_PLAY, 0);
    chk("pause", CR_STOP, 0);
    chk("stop", CR_STOP, 0);
    chk("rec on", CR_REC_ON, 0);
    chk("rec off", CR_REC_OFF, 0);
    chk("rec", CR_BAD, 0);
    chk("rec maybe", CR_BAD, 0);
    chk("proj load 2", CR_PROJ_LOAD, 2);
    chk("song load 0", CR_PROJ_LOAD, 0);
    chk("project load 0x3", CR_PROJ_LOAD, 3);
    chk("proj load 4", CR_BAD, 0);
    chk("proj load", CR_BAD, 0);
    chk("proj 2", CR_BAD, 0);
    chk("heavy", CR_HEAVY, 1);
    chk("heavy 3", CR_HEAVY, 3);
    chk("heavy 0", CR_BAD, 0);
    chk("heavy 5", CR_BAD, 0);
    chk("heavy x", CR_BAD, 0);
    chk("wdtreset", CR_WDTRESET, 0);
    chk("play now", CR_BAD, 0);
    chk("status", CR_NONE, 0);       /* (stop is a whole word: not status, not stopwatch) */
    chk("stopwatch", CR_NONE, 0);
    chk("reboot", CR_NONE, 0);
    chk("playing", CR_NONE, 0);
    chk("", CR_NONE, 0);
    if (fails)
        return 1;
    puts("console_parse_test: ok");
    return 0;
}
