/* SPDX-License-Identifier: GPL-3.0-only */
/* USB SERIAL (SLOOP 2.4, firmware/src/io/usb/usb.c usb_serial / usb_cdc_on): a build with the CDC console presents it only
 * when the setting is on, from the start after the setting changed. Built twice (tests/run_tests.sh):
 *   -DFELUCCA_CDC=0 US_DUMP=1  writes the descriptors of a build without the console to argv[1];
 *   -DFELUCCA_CDC=1            checks: the setting off (the default) presents exactly those bytes; on presents the
 *                              composite (the IAD, the CDC interfaces, EP2 / EP3); the setting changed while running
 *                              changes nothing until the next start (usb_start).
 * Exit status: the number of failed checks. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
static uint32_t host_now;
#define SYNC_NOW() host_now
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#define __attribute__(x)
#include "../firmware/src/io/usb/usb.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("usb_serial: %-90s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t desc(uint32_t type, uint8_t *out)
{
    const uint8_t *d;
    uint16_t l = 0;
    if (!get_desc(type << 8, &d, &l))
        return 0;
    memcpy(out, d, l);
    return l;
}

int main(int argc, char **argv)
{
    static uint8_t dev[64], cfg[1024], ref[2048];
    uint32_t nd, nc;
    FILE *f;
    if (argc < 2) {
        printf("usage: %s <descriptor file>\n", argv[0]);
        return 1;
    }
#if !FELUCCA_CDC
    nd = desc(1, dev);
    nc = desc(2, cfg);
    f = fopen(argv[1], "wb");
    if (!f)
        return 1;
    fputc((int)nd, f);
    fwrite(dev, 1, nd, f);
    fputc((int)(nc & 255u), f);
    fputc((int)(nc >> 8), f);
    fwrite(cfg, 1, nc, f);
    fclose(f);
    printf("usb_serial: the descriptors without the console: %u + %u bytes\n", nd, nc);
    return 0;
#else
    {
        uint32_t rn, rd, rc;
        f = fopen(argv[1], "rb");
        if (!f) {
            printf("usb_serial: %s missing (the FELUCCA_CDC=0 build writes it)\n", argv[1]);
            return 1;
        }
        rn = (uint32_t)fread(ref, 1, sizeof ref, f);
        fclose(f);
        rd = ref[0];
        rc = ref[1 + rd] | (uint32_t)ref[2 + rd] << 8;
        check("the setting is off at power-on (no settings record)", !usb_serial);
        usb_cdc_on = usb_serial;                         /* (usb_start's first line: the SIE is not modelled) */
        nd = desc(1, dev);
        nc = desc(2, cfg);
        check("off: the device descriptor is a no-console build's, byte for byte",
              rn >= 3u + rd + rc && nd == rd && !memcmp(dev, ref + 1, nd));
        check("off: the configuration (MIDI only, 2 interfaces) is a no-console build's, byte for byte",
              nc == rc && !memcmp(cfg, ref + 3 + rd, nc) && cfg[4] == 2u);
        usb_serial = 1;                                  /* the menu: ON, while running */
        nd = desc(1, dev);
        check("the setting changed while running: the same device until the next start", nd == rd && !memcmp(dev, ref + 1, nd));
        usb_cdc_on = usb_serial;                         /* the next start */
        nd = desc(1, dev);
        nc = desc(2, cfg);
        check("on: the composite device (misc / IAD class), the console's 4 interfaces",
              nd == 18u && dev[4] == 0xEFu && nc == sizeof CFG_DESC && cfg[4] == 4u && !memcmp(cfg, CFG_DESC, nc));
    }
    printf("usb_serial: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
#endif
}
