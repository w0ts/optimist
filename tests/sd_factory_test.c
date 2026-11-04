/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card zip's factory presets (tools/sd_factory.c, tools/sd_zip.py; SD card mode reads them in place of the
 * tables: sd_mode.c sdm_factory_*): every engine's every preset applied as the device applies it (apply_preset_to),
 * taken as the card's record (up_card_take), then loaded back into a part on another sound (up_card_give, as
 * sdm_factory_poll does): every value of the sound the same, an FM6 voice too; the FM6 packs' voices too (numbered on
 * after FM6's own presets, sd_factory.c extras). With FILEs (a build without the packs: the zip's FM6 files): each
 * loads with its own voice bytes, never a pack the build does not have.
 *   sd_factory_test [FILE.ops ...] */
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
static struct { int force, redraw; } ui;   /* (redraw: sections.c since LCD_DIRTY went, 26c6b7ac) */
static void ui_message(const char *m) { (void)m; }
static void ui_say(const char *a, const char *b) { (void)a; (void)b; }
static uint32_t up_gen;
static uint8_t settings_later;
static uint8_t sync_reload;
#include "../firmware/src/core/model.c"           /* apply_preset_to: a preset as the device applies it */
#include "../firmware/src/storage/upreset.c"
#include "../firmware/src/storage/sd/fatfs/ff.h"         /* (sd_hash.c's WAV reader names FatFs; unused here) */
#include "../firmware/src/storage/sd/sd_hash.c"


static int16_t want[P_COUNT];
static uint32_t bad;

/* engine slot e's sound k as tools/sd_factory.c makes it: its table's presets, then (FM6) the packs' voices, each a
 * fresh track's sound with the voice (sd_factory.c extra_apply); its name into nm. 0: no sound k */
static int sound_of(uint32_t e, uint32_t k, char *nm, uint32_t cap)
{
    const engine_t *E = ENGINES[e];
    uint32_t i;
    trk[0].eng_req = trk[0].engine = (uint8_t)e;
    if (k < E->npresets) {
        apply_preset_to(&trk[0], k);
        str_cpy(nm, E->presets[k].name, cap);
    } else {
#if FELUCCA_ENG_FM6
        if (!ENG_IS(E, FM6) || k - E->npresets >= FM6_NPKV)
            return 0;
        apply_preset_to(&trk[0], PRESET_INIT);
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = E->edit[i].def;
        trk[0].p[P_E0] = (int16_t)(1u + k - E->npresets);
        fm6_cur[0] = 0;
        str_cpy(nm, fm6_pack_name(k - E->npresets), cap);
#else
        (void)i;
        return 0;
#endif
    }
#if FELUCCA_ENG_FM6
    if (ENG_IS(E, FM6))
        fm6_sync(&trk[0], 0);
#endif
    return 1;
}

/* FILES mode (a build without FM6's packs; the zip's FM6 files, Optimist/Presets/Factory/FM6/<L>/*.ops): each loaded
 * as SD card mode loads it: its voice the file's own bytes, never a pack this build does not have */
static uint32_t files_mode(int argc, char **argv)
{
#if FELUCCA_ENG_FM6
    static uint8_t f[512];
    static int16_t ed[sizeof fm6_ed[0] / sizeof fm6_ed[0][0]];
    uint32_t n = 0;
    int a;
    for (a = 1; a < argc; a++) {
        FILE *fp = fopen(argv[a], "rb");
        size_t got = fp ? fread(f, 1, sizeof f, fp) : 0;
        if (fp)
            fclose(fp);
        if (got < 16u + sizeof(up_rec_t) + UP_CARD_FM6) {
            printf("FAIL %s: no FM6 voice in it (%zu B)\n", argv[a], got);
            bad++;
            continue;
        }
        trk[0].eng_req = trk[0].engine = (uint8_t)ENG_SLOT_FM6;
        apply_preset_to(&trk[0], 0);
        memset(fm6_ed[0], 0, sizeof fm6_ed[0]);
        fm6_unpack(ed, f + 16u + sizeof(up_rec_t));      /* (the file's own voice) */
        if (up_card_give(f + 16u, (uint32_t)got - 16u) || memcmp(ed, fm6_ed[0], sizeof ed) ||
            fm6_cur[0] != (int16_t)(trk[0].p[P_E0] + 1)) {
            printf("FAIL %s: not loaded with its own voice (a build without the packs)\n", argv[a]);
            bad++;
        }
        n++;
    }
    printf("sd factory test (%u FM6 packs in this build): %u FM6 files of the zip loaded with their own voices, %u failed\n",
           (unsigned)FM6_NPACKS, n, bad);
    return n;
#else
    (void)argc, (void)argv;
    return 0;
#endif
}

int main(int argc, char **argv)
{
    static uint8_t rec[sizeof(up_rec_t) + UP_CARD_FM6];
    static int16_t ed[sizeof fm6_ed[0] / sizeof fm6_ed[0][0]];
    uint32_t e, pi, i, n, total = 0, fm6 = 0;
    char nm[16];
    host_tracks_init();
    song.sel = 0;
    if (argc > 1)
        return files_mode(argc, argv) < 80u || bad != 0;   /* (the 3 own and the 80 packs' at least) */
    for (e = 0; e < NENGINES; e++)
        for (pi = 0; sound_of(e, pi, nm, sizeof nm); pi++) {
            int16_t was = 0;
            memcpy(want, trk[0].p, sizeof want);
#if FELUCCA_ENG_FM6
            memcpy(ed, fm6_ed[0], sizeof ed);
#endif
            n = up_card_take(rec, nm);
            trk[0].eng_req = trk[0].engine = (uint8_t)((e + 1u) % NENGINES);   /* (another sound first) */
            apply_preset_to(&trk[0], 0);
            if (!n || up_card_give(rec, n)) {
                printf("FAIL %s %u: not taken or not loaded\n", ENGINES[e]->name, pi);
                bad++;
                continue;
            }
            for (i = 0; i < P_COUNT; i++)
                if (!param_kept(i) && trk[0].p[i] != want[i] && !was++) {
                    printf("FAIL %s %u (%s): parameter %u is %d, the preset's %d\n", ENGINES[e]->name, pi, nm, i,
                           trk[0].p[i], want[i]);
                    bad++;
                }
#if FELUCCA_ENG_FM6
            if (ENG_IS(ENGINES[e], FM6)) {
                fm6++;
                if (memcmp(ed, fm6_ed[0], sizeof ed)) {
                    printf("FAIL FM6 %u (%s): its voice not the preset's\n", pi, nm);
                    bad++;
                }
            }
#endif
            total++;
        }
    printf("sd factory test: %u presets (%u FM6: its own and the packs' voices) through the card's record, %u failed\n",
           total, fm6, bad);
    return bad != 0;
}
