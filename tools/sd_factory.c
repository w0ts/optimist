/* SPDX-License-Identifier: GPL-3.0-only */
/* The factory presets as SD card files (the user, 2026-10-26: "all presets (user or factory) go on the card"; the SD
 * card zip: tools/sd_zip.py; docs/SD-CARD-DESIGN.md 9). Built on the host from the firmware's own sources (one source
 * of truth): each built engine's each preset is applied to a part as the device applies it (model.c apply_preset_to;
 * FM6 its voice loaded, fm6_sync), then taken as the card's preset record (upreset.c up_card_take: the record, an FM6
 * sound's voice after it) and written as the card's preset file (sd_mode.c sdm_small_put's format: "OPPR", version 2,
 * the payload's length, its XXH64, the sound's factory id: storage/sd/sd_ids.h, the payload) to
 *   <out>/Optimist/Presets/Factory/<Engine>/<L>/<nn> - <name>.ops   the engine's presets in its table's order, 16 a bank:
 *                                                                preset p in bank A + p / 16, numbered p % 16 + 1,
 *                                                                <Engine> sd_names.h's, <name> the preset's, lowercase
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
static uint8_t uk_tmp[4096] __attribute__((aligned(4)));   /* (drum_kits.c's bank scratch: the host's own) */
#define UK_TMP ((ukit_bank_t *)(void *)uk_tmp)
#define UK_HOST 1                                /* (no project store here: the host's bank, as tests/defproj_test.c) */
#define MISS_BUMP() ((void)0)                    /* (the MISSING warning: the device's, not the generator's) */
#include "../firmware/src/drums/drum_kits.c"      /* ukit_card_take: a kit as the card's kit file (UK_CARD_ON) */
#include "../firmware/src/storage/sd/fatfs/ff.h"         /* (sd_hash.c's WAV reader names FatFs; unused here) */
#include "../firmware/src/storage/sd/sd_hash.c"
#include "../firmware/src/storage/sd/sd_names.h"        /* the engines' folders: the firmware's own names */
#include "../firmware/src/storage/sd/sd_ids.h"          /* the factory ids: a pure function of the sound */
#include <ctype.h>

typedef struct {                                 /* sd_mode.c sdm_fh */
    char magic[4];
    uint16_t ver, len;
    uint64_t hash;
    uint64_t id;
} card_fh;

static int put(const char *path, const char *magic, const uint8_t *p, uint32_t n, uint64_t id)
{
    card_fh h;
    sdh_t s;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, magic, 4);
    h.ver = 2;
    h.id = id;
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

static char *lower(char *s)                      /* the card's file names are lowercase (sd_layout.c; the FM-1 shows
                                                  * them upper case: sd_mode.c sdm_factory_scan) */
{
    char *p = s;
    for (; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    return s;
}

/* the sounds flash mode lists as an engine's presets beyond its table (the user, 2026-11-04: "they should just be
 * presets in sd card", ALL of them): FM6's voice packs (storage/nbank.c, eng_fm6.c FM6_PACK: KEYS, BASS, BELLS, PADS,
 * 80S, 16 each, every one built in here), numbered on after the engine's own presets in the order flash mode lists them.
 * Each is the engine's defaults with the voice (nbank.c nb_load: EDIT to the defaults, VOICE the pack voice), the rest
 * of the sound a fresh track's: the file carries the voice's own bytes, so an SD card mode build plays it whatever
 * packs it has. The CZ collection (U01..) is the user's flash store and the FM6 R bank a received one: no factory sound */
static uint32_t extras(const engine_t *E)
{
#if FELUCCA_ENG_FM6
    if (ENG_IS(E, FM6))
        return FM6_NPKV;
#endif
    (void)E;
    return 0;
}
static void extra_apply(const engine_t *E, uint32_t j, char *nm, uint32_t cap)
{
    uint32_t i;
    apply_preset_to(&trk[0], PRESET_INIT);       /* (a fresh track's sound: INIT) */
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = E->edit[i].def;
#if FELUCCA_ENG_FM6
    trk[0].p[P_E0] = (int16_t)(1u + j);          /* VOICE: pack voice j (0 is INIT VOICE) */
    fm6_cur[0] = 0;                              /* (the buffer loaded afresh from it: fm6_sync) */
    str_cpy(nm, fm6_pack_name(j), cap);
    for (i = (uint32_t)strlen(nm); i && nm[i - 1u] == ' '; i--)
        nm[i - 1u] = 0;                          /* (a DX7 name is blank-padded to 10) */
#else
    (void)j, (void)nm, (void)cap;
#endif
}

static int kits(const char *out);

int main(int argc, char **argv)
{
    static uint8_t rec[sizeof(up_rec_t) + UP_CARD_FM6];
    static char seen[256][16];                   /* (the engine's names so far: a repeated name's occurrence) */
    uint32_t e, pi, n, total = 0, nth, j;
    uint64_t id;
    char dir[400], bank[440], path[512], nm[16], en[16], rn[13];
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
        str_cpy(en, sdn_engine(eng_uid(e)), sizeof en);   /* (sd_names.h: the firmware's own table) */
        clean(en);
        snprintf(dir, sizeof dir, "%s/Optimist/Presets/Factory/%s", argv[1], en);
        mkdirs(dir);
        for (pi = 0; pi < E->npresets + extras(E); pi++) {
            trk[0].eng_req = trk[0].engine = (uint8_t)e;
            if (pi < E->npresets) {
                apply_preset_to(&trk[0], pi);
                str_cpy(nm, E->presets[pi].name, sizeof nm);
            } else {
                extra_apply(E, pi - E->npresets, nm, sizeof nm);
            }
#if FELUCCA_ENG_FM6
            if (ENG_IS(E, FM6))
                fm6_sync(&trk[0], 0);            /* (its VOICE into the voice buffer, as the first block does: the
                                                  * file carries the voice itself, up_card_take) */
#endif
            n = up_card_take(rec, nm);
            memcpy(rn, rec + 5, 12);                 /* (the name the file carries: upreset.c up_rec_t, 12 at most) */
            rn[12] = 0;
            for (j = 0, nth = 1; j < pi && j < 256u; j++)
                nth += !strcmp(seen[j], rn);
            if (pi < 256u)
                str_cpy(seen[pi], rn, sizeof seen[pi]);
            id = sdi_factory_preset(eng_uid(e), rn, nth);   /* (as the table has it, before the card's case) */
            if (!n) {
                fprintf(stderr, "sd_factory: %s %u: not taken\n", E->name, pi);
                return 1;
            }
            clean(nm);
            snprintf(bank, sizeof bank, "%s/%c", dir, (char)('A' + pi / 16u));   /* (banks of 16: B/03 is 18) */
            mkdirs(bank);
            snprintf(path, sizeof path, "%s/%02u - %s.ops", bank, pi % 16u + 1u, lower(nm));
            if (put(path, "OPPR", rec, n, id)) {
                fprintf(stderr, "sd_factory: %s: not written\n", path);
                return 1;
            }
        }
        printf("%s %u %u\n", en, (unsigned)E->npresets, (unsigned)extras(E));   /* (folder, own, the collections') */
        total += E->npresets + extras(E);
    }
    printf("total %u\n", total);
    return kits(argv[1]);
}

/* the factory kits as kit files (the user, 2026-11-04: "the factory kits go on the card like presets"): every kit UID in
 * the firmware's order (drums.c: the sampled kits, the 32 synthesised, X0X's 909 and 808 and their styles, KAWA,
 * PLAITS: every one built in here), 16 a bank, as SD card mode's kit files ("OPKT", sd_mode.c sdm_small_put):
 *   <out>/Optimist/Kits/Factory/<L>/<nn> - <name> (<instruments>).opk   (drum_kits.c ukit_card_instr: 909, 808,
 *                                  synth, kawa, plaits, sampled, the ones its lanes name, in that order)
 * Each is the kit picked on fresh lanes (dsnd_desc.c dl_reset_lanes, then the kit: what the kit list plays) taken as a
 * user kit is (drum_kits.c ukit_card_take: the lanes, their sources by reference to the built-in drum sounds, the
 * sends). A build without a kit's drum source plays its stand-in (drums.c drum_kit_of), as for a project */
static int kits(const char *out)
{
    static uint8_t rec[UK_CARD_N];
    char bank[440], path[512], nm[16], ins[64];
    uint32_t k, n;
    for (k = 0; k < DRUM_KITS; k++) {
        memset(&dl, 0, sizeof dl);
        memset(dsend, 0, sizeof dsend);
        TDRUM->p[P_E0] = (int16_t)k;
        dl_e0 = (int16_t)k;
        n = ukit_card_take(rec);
        str_cpy(nm, DRUM_KIT_NAMES[k], sizeof nm);
        clean(nm);
        snprintf(bank, sizeof bank, "%s/Optimist/Kits/Factory/%c", out, (char)('A' + k / 16u));
        mkdirs(bank);
        ukit_card_instr(rec, ins, sizeof ins);       /* (its drum instruments: " (909, sampled)", drum_kits.c) */
        snprintf(path, sizeof path, "%s/%02u - %s%s.opk", bank, k % 16u + 1u, lower(nm), ins);
        if (n != UK_CARD_N || put(path, "OPKT", rec, n, sdi_factory_kit(k))) {
            fprintf(stderr, "sd_factory: %s: not written\n", path);
            return 1;
        }
    }
    printf("kits %u\n", (unsigned)DRUM_KITS);
    return 0;
}
