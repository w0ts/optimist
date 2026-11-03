/* SPDX-License-Identifier: GPL-3.0-only */
/* The default project's sounds (model.c TRK_DEF, trk_def_engine / trk_def_preset / trk_def_sound; the user, 2026-11-01):
 * T1 ANALOG ACID 303, T2 FM6 GLASS EP (the KEYS pack's 4th voice), T3 DIGITAL GLASS PAD. Each is resolved as the
 * firmware does at power-on and checked by NAME, so a reordered preset table or pack fails here. A build without
 * the engine or the pack falls back (T2: DIGITAL RHODES; T1 / T3: the first engine built, or the lead): never an
 * engine or preset this build lacks. One line per part. Built by tests/run_tests.sh on several configurations. */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
static struct { int force, redraw; } ui;   /* (redraw: sections.c since LCD_DIRTY went, 26c6b7ac) */                    /* (what the UI keeps that the model and the stores touch) */
static uint8_t settings_later;
static void ui_message(const char *m) { (void)m; }
static void ui_say(const char *a, const char *b) { (void)a, (void)b; }
static int project_used(uint32_t i) { return i < 2; }
static void arrangement_apply(uint32_t s) { (void)s; }
static void arrangement_save(void) {}                     /* (the arranger's side the sequencer calls: gcc keeps seq_start) */
static uint32_t arrangement_ready(void) { return 3; }
static void section_store(uint32_t s) { live_sec = (int8_t)s; }
static void section_load(uint32_t s) { live_sec = (int8_t)s; }
#if FELUCCA_QCHAIN
static uint32_t section_bars(uint32_t s) { (void)s; return 1u; }
#endif
static void song_backup(void) {}
static void song_restore(void) {}
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return 1; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { (void)k; b[0] = 0; }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static uint32_t up_engine_slot(uint32_t k) { (void)k; return NENGINES; }   /* (no user presets here: the browsing filter, model.c pl_keep) */   /* (nbank.c names the user slots) */
#include "../firmware/src/core/model.c"          /* the model: apply_preset_to, BANK, the preset list (preset_pos / at) */
#include "../firmware/src/drums/dsnd_desc.c"      /* the drum lanes' SOUND values and the kit list */
#if FELUCCA_NB_LIST
#include "../firmware/src/storage/nbank.c"       /* the FM6 / CZ collections in PRESETS */
#endif
#if FELUCCA_DRUM_KITS                             /* the user kit bank (felucca.c builds drum_kits.c only with it) */
static uint8_t kit_nor[0x2000];
static int st_read(uint32_t off, void *dst, uint32_t n)
{ if (off < 0xDA000u || off + n > 0xDC000u) return -1; memcpy(dst, kit_nor + off - 0xDA000u, n); return 0; }
static int st_erase(uint32_t off) { if (off < 0xDA000u || off >= 0xDC000u) return -1; memset(kit_nor + off - 0xDA000u, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{ uint32_t i; if (off < 0xDA000u || off + n > 0xDC000u) return -1; for (i = 0; i < n; i++) kit_nor[off - 0xDA000u + i] &= ((const uint8_t *)src)[i]; return 0; }
#include "../firmware/src/storage/storage.c"
static uint32_t kit_tmp[4096 / 4];
#define UK_HOST 1
#define UK_TMP ((ukit_bank_t *)(void *)kit_tmp)
#include "../firmware/src/drums/drum_kits.c"
#endif

static int bad;
static void want(const char *what, int ok) { printf("defproj: %-44s %s\n", what, ok ? "ok" : "FAILED"); bad += !ok; }
static int same(const char *a, const char *b) { return !strncmp(a, b, strlen(b)); }

int main(void)
{
    uint32_t i;
    host_tracks_init();
    song.sel = 0;
    bank_resolve();
    for (i = 0; i < NPART; i++) {                       /* power-on, as main.c felucca_init */
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, trk_def_engine(i));
        trk_def_sound(t, i);
        t->engine = t->eng_req;
    }
    {
        const engine_t *e1 = ENGINES[trk[0].eng_req % NENGINES], *e2 = ENGINES[trk[1].eng_req % NENGINES], *e3 = ENGINES[trk[2].eng_req % NENGINES];
        char b[64];
        want("T1 plays an engine this build has", trk[0].eng_req < NENGINES && trk[1].eng_req < NENGINES && trk[2].eng_req < NENGINES);
#if FELUCCA_ENG_ANALOG
        want("T1 ANALOG ACID 303", ENG_IS(e1, ANALOG) && trk[0].preset == 4 && same(e1->presets[trk[0].preset].name, "ACID 303"));
        want("T1 ACID 303 level trim applied", trk[0].p[P_ED_FX] == preset_trim(eng_uid(trk[0].eng_req), 4));
#else
        want("T1 the first engine built (no ANALOG)", trk[0].eng_req == 0 && !trk[0].preset);
#endif
#if FELUCCA_ENG_FM6 && FELUCCA_FM6_PK_KEYS
        snprintf(b, sizeof b, "%s", fm6_pack_name(trk[1].p[P_E0] - 1));
        want("T2 FM6 GLASS EP", ENG_IS(e2, FM6) && same(b, "GLASS EP") && fm6_cur[1] == 0);
#else
        want("T2 DIGITAL RHODES (no FM6 KEYS pack)", ENG_IS(e2, DIGITAL) && same(e2->presets[trk[1].preset].name, "RHODES"));
#endif
#if FELUCCA_ENG_DIGITAL
        want("T3 DIGITAL GLASS PAD", ENG_IS(e3, DIGITAL) && trk[2].preset == 11 && same(e3->presets[trk[2].preset].name, "GLASS PAD"));
        want("T3 GLASS PAD level trim applied", trk[2].p[P_ED_FX] == preset_trim(eng_uid(trk[2].eng_req), 11));
#else
        want("T3 the lead (no DIGITAL)", e3->npresets == 0 || trk[2].preset < e3->npresets);
#endif
        (void)b; (void)e1; (void)e2; (void)e3;
    }
    printf(bad ? "DEFPROJ FAILED (%d)\n" : "default project: its sounds resolve by name on this build\n", bad);
    return bad != 0;
}
