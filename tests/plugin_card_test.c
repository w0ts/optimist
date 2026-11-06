/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of a plugin loaded from the SD card into the flash (firmware/src/core/plugin_card.c over core/plugin.c,
 * docs/SD-MODE-DESIGN.md section 10) on the card model (tests/sd_card_model.h, full FatFs) and a simulated NOR flash:
 *   - Optimist/Plugins/SID.opl written on the card (the loader test's synthetic file with SID's UID): asked for UID 23,
 *     placed in the region's first sectors, slot 0 holds UID 23, its header whole; asked again: the slot, nothing read
 *     or written;
 *   - an engine with no file on the card (PROPHET): READ, nothing placed; a UID past the registry: BAD;
 *   - a file named for one engine holding another's UID (CS80.opl with UID 23 inside): BAD and not kept;
 *   - the card gone (not ready): READ.
 *   build/host/plugin_card_test FAT32.img   Exit status: the number of failed checks. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FELUCCA_SD_FATFS 1
#define FELUCCA_PLUGINS 1
#define FELUCCA_SDCARD 1
#define FELUCCA_PLUGIN_KB 64
#define FELUCCA_PLUGIN_SLOTS 2
#define FELUCCA_LAYOUT 0
#define PLUG_ABI 0x12345678u
#define FELUCCA_ANALOG2 1
#define FELUCCA_SLICE 0                               /* (core.h sets it from ENG_SLICE before registry.h) */
#include "sd_card_model.h"
#include "storage/sd/sd_mmc.c"
#include "storage/sd/sd_diskio.c"
#include "fm1_xip.h"
#include "core/registry.h"                           /* ENG_UID_NAME, plug_uid, NBUILT, NENGINES */

typedef struct engine_s engine_t;
static const engine_t *ENGINES[NENGINES];
static int plug_none_;
#define PLUG_NONE ((const engine_t *)&plug_none_)
#define PLUG_RAM
#define SDL_ROOT "Optimist"

/* ---- the card as sdcard.c wraps it: one open file */
static FATFS fs __attribute__((aligned(4)));
static FIL sd_fil;
enum { SD_NOCARD, SD_READY };
static int sd_state = SD_READY;
static FRESULT sd_open(const char *path)
{
    if (sd_state != SD_READY)
        return FR_NOT_ENABLED;
    (void)f_close(&sd_fil);
    return f_open(&sd_fil, path, FA_READ);
}
static FRESULT sd_read(void *p, uint32_t n, uint32_t *got)
{
    UINT g = 0;
    FRESULT r = sd_state == SD_READY ? f_read(&sd_fil, p, (UINT)n, &g) : FR_NOT_ENABLED;
    *got = g;
    return r;
}
static FRESULT sd_seek(uint32_t ofs) { return sd_state == SD_READY ? f_lseek(&sd_fil, ofs) : FR_NOT_ENABLED; }

/* ---- the simulated flash (tests/plugin_test.c's) */
static uint8_t flash[1u << 20];
static uint32_t n_erase, n_prog;
#define PLUG_XIP(off) ((const uint8_t *)flash + (off))
static int in_region(uint32_t off, uint32_t n) { return off >= FL_PLUG_LO && n <= FL_PLUG_HI - off; }
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    *took = 0;
    if (!in_region(off, 4096u) || (off & 0xFFFu))
        return -1;
    memset(flash + off, 0xFF, 4096u);
    n_erase++;
    return 0;
}
static void plug_sfc(uint8_t *b, uint32_t off, uint32_t n);
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n)
{
    static uint8_t plain[PLUG_SECTOR];
    uint32_t i;
    if (!in_region(off, n) || n > sizeof plain)
        return -1;
    memcpy(plain, src, n);
    plug_sfc(plain, off, n);
    for (i = 0; i < n; i++)
        flash[off + i] &= plain[i];
    n_prog++;
    return 0;
}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static uint32_t st_crc_upd(uint32_t c, const void *p, uint32_t n)
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
#include "core/plugin.c"
#include "core/plugin_card.c"

/* ---- a plugin file (tests/plugin_test.c's vector, this UID) */
static void st32(uint8_t *p, uint32_t v) { plug_st32(p, v); }
#define IMG_LEN 10000u
static const uint32_t REL[3] = {0x10u, (1u << 28) | 0x6Eu, 0x1FCEu};
static uint8_t filebuf[PLUG_HDR_LEN + IMG_LEN + 3u * 4u];
static uint32_t mk_file(uint8_t uid)
{
    plug_hdr_t h;
    uint32_t i;
    uint8_t *img = filebuf + PLUG_HDR_LEN;
    for (i = 0; i < IMG_LEN; i += 4u)
        st32(img + i, i * 0x01010101u + 7u);
    st32(img + 0x6E, 0x3456FFC8u);
    st32(img + 0x72, 0x18BA0012u);
    memset(&h, 0, sizeof h);
    h.magic = PLUG_MAGIC;
    h.api = PLUG_API;
    h.kind = PLUG_KIND_ENGINE;
    h.uid = uid;
    memcpy(h.name, "TEST", 4);
    h.len = IMG_LEN;
    h.desc = 0x100u;
    h.nrel = 3;
    h.abi = PLUG_ABI;
    h.crc = plug_crc(img, IMG_LEN);
    h.hcrc = plug_crc(&h, PLUG_HDR_LEN - 4u);
    memcpy(filebuf, &h, PLUG_HDR_LEN);
    for (i = 0; i < 3u; i++)
        st32(img + IMG_LEN + i * 4u, REL[i]);
    return PLUG_HDR_LEN + IMG_LEN + 12u;
}
static FRESULT put_file(const char *path, const uint8_t *d, uint32_t n)
{
    FIL f;
    UINT put;
    FRESULT r = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (r == FR_OK)
        r = f_write(&f, d, n, &put);
    (void)f_close(&f);
    return r;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv)
{
    uint32_t n32, n;
    uint8_t *img;
    int rc;
    plug_hdr_t h;
    if (argc < 2) { printf("usage: plugin_card_test FAT32.img\n"); return 2; }
    img = load(argv[1], &n32);
    card(img, n32, 2048, 1, 1, 0);                        /* SDHC, FAT32 in a partition */
    CHECK(f_mount(&fs, "", 1) == FR_OK, "mount");
    CHECK(f_mkdir("Optimist") == FR_OK || 1, "the root folder");
    CHECK(f_mkdir("Optimist/Plugins") == FR_OK, "Optimist/Plugins made");
    memset(flash, 0xFF, sizeof flash);
    plug_init();
    CHECK(ENG_UID_N > 23u && strcmp(ENG_UID_NAME[23], "SID") == 0, "UID 23 is SID in the registry");

    n = mk_file(23);
    CHECK(put_file("Optimist/Plugins/SID.opl", filebuf, n) == FR_OK, "SID.opl written on the card");
    rc = plug_card_install(23);
    CHECK(rc == 0 && plug_uid[0] == 23u, "SID from the card: slot 0, UID 23 (%d)", rc);
    memcpy(&h, flash + FL_PLUG_LO, PLUG_HDR_LEN);
    CHECK(h.magic == PLUG_MAGIC && h.uid == 23u && h.base == FM1_XIP(FL_PLUG_LO) + PLUG_HDR_LEN && h.nrel == 0u, "placed whole at the region's start");
    CHECK(plug_le32(flash + FL_PLUG_LO + PLUG_HDR_LEN + 0x10u) == 0x10u * 0x01010101u + 7u + h.base, "patched");
    n_prog = n_erase = 0;
    rc = plug_card_install(23);
    CHECK(rc == 0 && n_prog == 0u && n_erase == 0u, "asked again: its slot, nothing written (%d)", rc);
    CHECK(eng_built(23) && eng_slot(23) == NBUILT, "the registry sees SID built, in the first plugin slot");

    rc = plug_card_install(24);                            /* PROPHET: no file */
    CHECK(rc == PLUG_E_READ && plug_uid[1] == 0xFFu, "no file for PROPHET: READ (%d), nothing placed", rc);
    CHECK(plug_card_install(ENG_UID_N + 5u) == PLUG_E_BAD, "a UID past the registry: BAD");

    n = mk_file(23);                                       /* CS80.opl holding SID's UID */
    CHECK(put_file("Optimist/Plugins/CS80.opl", filebuf, n) == FR_OK, "CS80.opl (SID inside) written");
    plug_forget(0);
    rc = plug_card_install(26);
    CHECK(rc == PLUG_E_BAD && plug_uid[0] == 0xFFu && plug_uid[1] == 0xFFu, "another engine inside: BAD (%d), not kept", rc);
    plug_init();
    CHECK(plug_uid[0] == 0xFFu, "a boot finds nothing placed");

    sd_state = SD_NOCARD;
    rc = plug_card_install(23);
    CHECK(rc == PLUG_E_READ, "the card gone: READ (%d)", rc);
    printf("%d failed\n", fails);
    return fails;
}
