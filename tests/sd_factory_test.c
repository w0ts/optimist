/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card zip's factory presets (tools/sd_factory.c, tools/sd_zip.py; SD card mode reads them in place of the
 * tables: sd_mode.c sdm_factory_*): every engine's every preset applied as the device applies it (apply_preset_to),
 * taken as the card's record (up_card_take), then loaded back into a part on another sound (up_card_give, as
 * sdm_factory_poll does): every value of the sound the same, an FM6 voice too.
 *   sd_factory_test */
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


static int16_t want[P_COUNT];
int main(void)
{
    static uint8_t rec[sizeof(up_rec_t) + UP_CARD_FM6];
    static int16_t ed[sizeof fm6_ed[0] / sizeof fm6_ed[0][0]];
    uint32_t e, pi, i, n, bad = 0, total = 0, fm6 = 0;
    host_tracks_init();
    song.sel = 0;
    for (e = 0; e < NENGINES; e++)
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            int16_t was = 0;
            trk[0].eng_req = trk[0].engine = (uint8_t)e;
            apply_preset_to(&trk[0], pi);
#if FELUCCA_ENG_FM6
            if (ENG_IS(ENGINES[e], FM6))
                fm6_sync(&trk[0], 0);
#endif
            memcpy(want, trk[0].p, sizeof want);
#if FELUCCA_ENG_FM6
            memcpy(ed, fm6_ed[0], sizeof ed);
#endif
            n = up_card_take(rec, ENGINES[e]->presets[pi].name);
            trk[0].eng_req = trk[0].engine = (uint8_t)((e + 1u) % NENGINES);   /* (another sound first) */
            apply_preset_to(&trk[0], 0);
            if (!n || up_card_give(rec, n)) {
                printf("FAIL %s %u: not taken or not loaded\n", ENGINES[e]->name, pi);
                bad++;
                continue;
            }
            for (i = 0; i < P_COUNT; i++)
                if (!param_kept(i) && trk[0].p[i] != want[i] && !was++) {
                    printf("FAIL %s %u (%s): parameter %u is %d, the preset's %d\n", ENGINES[e]->name, pi,
                           ENGINES[e]->presets[pi].name, i, trk[0].p[i], want[i]);
                    bad++;
                }
#if FELUCCA_ENG_FM6
            if (ENG_IS(ENGINES[e], FM6)) {
                fm6++;
                if (memcmp(ed, fm6_ed[0], sizeof ed)) {
                    printf("FAIL FM6 %u: its voice not the preset's\n", pi);
                    bad++;
                }
            }
#endif
            total++;
        }
    printf("sd factory test: %u presets (%u FM6) through the card's record, %u failed\n", total, fm6, bad);
    return bad != 0;
}
