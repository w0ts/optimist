/* SPDX-License-Identifier: GPL-3.0-only */
/* The USB serial console (firmware/src/io/usb/usb.c usb_cdc_on): a build with it (FELUCCA_CDC, the builder's USB_CONSOLE)
 * presents the composite device from the first start (no device switch since 2026-11-05; SLOOP 2.4's USB SERIAL setting
 * chose it until then). Built twice (tests/run_tests.sh):
 *   -DFELUCCA_CDC=0 US_DUMP=1  writes the descriptors of a build without the console to argv[1];
 *   -DFELUCCA_CDC=1            checks: the composite (the IAD, the CDC interfaces, EP2 / EP3) from the start, and that the
 *                              no-console build's descriptors are the same MIDI device without the IAD and the CDC block.
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
static const char *dev_name(void) { return "FM1-0000"; }   /* (system/devname.c: the product string, usb.c) */
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
        usb_cdc_on = 1;                                  /* (usb_start's: the SIE is not modelled) */
        nd = desc(1, dev);
        nc = desc(2, cfg);
        check("the composite device (misc / IAD class), the console's 4 interfaces, from the first start",
              nd == 18u && dev[4] == 0xEFu && nc == sizeof CFG_DESC && cfg[4] == 4u && !memcmp(cfg, CFG_DESC, nc));
        check("a no-console build: class 0, 2 interfaces, 8 + 66 bytes fewer (no IAD, no CDC block)",
              rn >= 3u + rd + rc && rd == 18u && ref[1 + 4] == 0u && rc + 8u + 66u == sizeof CFG_DESC && ref[3 + rd + 4] == 2u);
        check("its MIDI interfaces are this build's, byte for byte (after the IAD)",
              rc >= 9u && !memcmp(ref + 3 + rd + 9, CFG_DESC + 17, rc - 9u));
    }
    printf("usb_serial: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
#endif
}
