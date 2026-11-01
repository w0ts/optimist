/* SPDX-License-Identifier: GPL-3.0-only */
/* An update starts (or UBOOT from the host): the transport stops before the first write. system/stop_first.h with the
 * audio block mocked (it consumes transport_req == 2 like seq.c: seq_stop, clock-out STOP follows song.playing = 0), and
 * system/main.c checked for the order: stop_first() before persist_flush_now() / ota_session() / fm1_enter_uboot(). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct { uint8_t playing; } song;
static volatile uint8_t transport_req;
static uint32_t lag_ms, waited, feeds, stops_out, writes, writes_while_playing;
static void fm1_wdt_feed(void) { feeds++; }
static void fm1_delay_ms(uint32_t n)
{
    while (n--) {
        waited++;
        if (transport_req == 2u && waited >= lag_ms) {      /* (the audio block: seq_stop, then the MIDI STOP out) */
            if (song.playing) stops_out++;
            song.playing = 0;
            transport_req = 0;
        }
    }
}
#include "../firmware/src/system/stop_first.h"
static void flash_write(void) { writes++; writes_while_playing += song.playing; }

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *b = malloc(1 << 20);
    size_t n;
    assert(f && b);
    n = fread(b, 1, (1 << 20) - 1, f);
    b[n] = 0;
    fclose(f);
    return b;
}
static int before(const char *s, const char *a, const char *b)
{
    const char *pa = strstr(s, a), *pb = strstr(s, b);
    return pa && pb && pa < pb;
}

int main(void)
{
    char *m;
    song.playing = 1; lag_ms = 3;                           /* playing, the block carries the stop out 3 ms later */
    stop_first();
    flash_write();
    assert(!song.playing && stops_out == 1 && writes == 1 && writes_while_playing == 0 && waited <= 5);
    printf("playing + update start: stopped before the first write (%u ms)       ok\n", (unsigned)waited);

    waited = 0; stops_out = 0;
    stop_first();                                           /* already stopped: no wait, no second STOP */
    assert(!song.playing && waited == 0 && stops_out == 0);
    printf("already stopped: nothing asked, no wait                          ok\n");

    song.playing = 1; lag_ms = 100000; waited = 0;          /* the block never answers: bounded */
    stop_first();
    assert(waited == 100u && transport_req == 2u);
    printf("no answer from the audio block: gives up after 100 ms            ok\n");
    transport_req = 0;

    song.playing = 0; transport_req = 1; lag_ms = 0;        /* a PLAY just requested: stopped too */
    stop_first();
    assert(!song.playing && !transport_req);
    printf("a PLAY still pending: dropped, stopped                           ok\n");

    m = slurp("firmware/src/system/main.c");
    assert(before(m, "usb.ota_req = 0;\n            stop_first();", "ota_session();"));
    assert(before(m, "stop_first();", "persist_flush_now();                        /* an update ends"));
    assert(before(m, "if (usb.uboot_req) {                            /* SysEx F0 22 24 35 7D F7 from the host */\n            stop_first();",
                  "fm1_enter_uboot();\n        }\n#if FELUCCA_CDC"));
    printf("main.c: stop_first() ahead of the flush, the session and UBOOT   ok\n");
    free(m);
    printf("stop_first tests passed\n");
    return 0;
}
