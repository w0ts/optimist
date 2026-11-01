/* SPDX-License-Identifier: GPL-3.0-only */
/* The factory presets as SD card files (the user, 2026-10-26: "all presets (user or factory) go on the card"; the SD
 * card zip: tools/sd_zip.py; docs/SD-CARD-DESIGN.md 9). Built on the host from the firmware's own sources (one source
 * of truth): each built engine's each preset is applied to a part as the device applies it (model.c apply_preset_to;
 * FM6 its voice loaded, fm6_sync), then taken as the card's preset record (upreset.c up_card_take: the record, an FM6
 * sound's voice after it) and written as the card's preset file (sd_mode.c sdm_small_put's format: "OPPR", version 1,
 * the payload's length, its XXH64, the payload) to
 *   <out>/OPTIMIST/PRESETS/FACTORY/<ENGINE>/<nnn> <NAME>.OPS     nnn the preset's place in the engine's table (000 ..)
 * which SD card mode reads in place of the tables it no longer carries (sd_mode.c sdm_factory_*).
 *   sd_factory OUT_DIR      -> prints "<engine> <presets>" a line, exit 0 */
#define main hostsim_main
#include "../tests/hostsim.c"
#undef main
#include <sys/stat.h>

static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static uint8_t flash_ok = 1;
static struct { int force; } ui;
static void ui_message(const char *m) { (void)m; }
static void ui_say(const char *a, const char *b) { (void)a; (void)b; }
static uint32_t up_gen;
static uint8_t settings_later;
static uint8_t sync_reload;
#include "../firmware/src/core/model.c"           /* apply_preset_to: a preset as the device applies it */
#include "../firmware/src/storage/upreset.c"
#include "../firmware/src/storage/sd/fatfs/ff.h"         /* (sd_hash.c's WAV reader names FatFs; unused here) */
#include "../firmware/src/storage/sd/sd_hash.c"

typedef struct {                                 /* sd_mode.c sdm_fh */
    char magic[4];
    uint16_t ver, len;
    uint64_t hash;
} card_fh;

static int put(const char *path, const uint8_t *p, uint32_t n)
{
    card_fh h;
    sdh_t s;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    memcpy(h.magic, "OPPR", 4);
    h.ver = 1;
    h.len = (uint16_t)n;
    sdh_init(&s);
    sdh_add(&s, p, n);
    h.hash = sdh_end(&s);
    fwrite(&h, sizeof h, 1, f);
    fwrite(p, 1, n, f);
    return fclose(f);
}

static void mkdirs(const char *p)                /* every folder of path p */
{
    char b[512];
    uint32_t i;
    for (i = 0; p[i] && i < sizeof b - 1u; i++) {
        b[i] = p[i];
        if (p[i] == '/' && i) {
            b[i] = 0;
            mkdir(b, 0755);
            b[i] = '/';
        }
    }
    b[i] = 0;
    mkdir(b, 0755);
}

static void clean(char *s)                       /* a name the card takes: no / \ : * ? " < > | */
{
    for (; *s; s++)
        if (strchr("/\\:*?\"<>|", *s))
            *s = '_';
}

int main(int argc, char **argv)
{
    static uint8_t rec[sizeof(up_rec_t) + UP_CARD_FM6];
    uint32_t e, pi, n, total = 0;
    char dir[400], path[512], nm[16], en[16];
    if (argc != 2) {
        fprintf(stderr, "usage: sd_factory OUT_DIR\n");
        return 2;
    }
    host_tracks_init();
    song.sel = 0;
    for (e = 0; e < NENGINES; e++) {
        const engine_t *E = ENGINES[e];
        if (!E->npresets)
            continue;
        str_cpy(en, E->name, sizeof en);
        clean(en);
        snprintf(dir, sizeof dir, "%s/OPTIMIST/PRESETS/FACTORY/%s", argv[1], en);
        mkdirs(dir);
        for (pi = 0; pi < E->npresets; pi++) {
            trk[0].eng_req = trk[0].engine = (uint8_t)e;
            apply_preset_to(&trk[0], pi);
#if FELUCCA_ENG_FM6
            if (ENG_IS(E, FM6))
                fm6_sync(&trk[0], 0);            /* (its VOICE into the voice buffer, as the first block does) */
#endif
            str_cpy(nm, E->presets[pi].name, sizeof nm);
            n = up_card_take(rec, nm);
            if (!n) {
                fprintf(stderr, "sd_factory: %s %u: not taken\n", E->name, pi);
                return 1;
            }
            clean(nm);
            snprintf(path, sizeof path, "%s/%03u %s.OPS", dir, pi, nm);
            if (put(path, rec, n)) {
                fprintf(stderr, "sd_factory: %s: not written\n", path);
                return 1;
            }
        }
        printf("%s %u\n", E->name, (unsigned)E->npresets);
        total += E->npresets;
    }
    printf("total %u\n", total);
    return 0;
}
