/* SPDX-License-Identifier: GPL-3.0-only */
/* Every sound source of this build has something to pick (a modular build must never leave one empty):
 *   - every synth engine built has at least one loadable entry on the PRESETS list: a factory preset (model.c BANK as
 *     this build resolves it: the engine built, the preset in its table, its sample set built: preset_playable), or
 *     else its INIT (the engine's defaults); each entry loaded through the list gives that engine (INIT: its
 *     default values). COVER_STRICT=1 (the full build): no engine falls back to INIT;
 *   - every drum source built (the sampled kits, the synthesised kits, each X0X kit and style kit) has at least one
 *     kit on the kit list (drum_kit_built), and the power-on kit is one of them (tests/kits_sound_test.c: each
 *     heard).
 * Built by tests/run_tests.sh on several configurations (the defaults, every engine and kit, sample headers with
 * sets left out) and by tests/builder_test.py on random builder configurations (tools/builder/verify.py
 * preset_cover). One line per source:  cover: <NAME> <built> <in its table> <loadable> [INIT]
 *   - INIT and every preset of every engine: ENV 1 MOD's PIT and moves at 0, ENV2's DEST A the engine's default (the
 *     filter, or its nearest: env1_cards.h) when ENV2 is not used (2026-10-16)
 * The list is the model's (core/model.c preset_pos / preset_at), loaded as the Optimist UI's PRESETS does. */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
static struct { int force; } ui;                    /* (what the UI keeps that the model and the stores touch) */
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

/* list index n into the selected track, as the Optimist UI's PRESETS (op_screens.c op_preset_go) */
static void preset_go(uint32_t n)
{
    uint32_t k, e = preset_at(n, &k);
#if FELUCCA_NB_LIST
    if (e == NB_LIST) {
        nb_load(k);
        return;
    }
#endif
    if (e == NENGINES) {
        (void)up_load(k);
        return;
    }
    if (e != TSEL->eng_req)
        set_engine(e);
    apply_preset(k);
}

static int bad;

#ifndef COVER_STRICT
#define COVER_STRICT 0          /* 1: every engine has a factory preset on the list (no INIT): the full build */
#endif

/* the PRESETS list entries of engine slot e: its factory presets (ui.c BANK as this build resolves it) and its INIT
 * (an engine with none: its defaults). Each one loaded on track 1 through the list (preset_go) must give that engine
 * and, for INIT, its defaults; *init: the INIT entries */
static uint32_t loadable(uint32_t e, uint32_t *init)
{
    uint32_t n, k, i, total, hits = 0;
    track_t *t = &trk[0];
    *init = 0;
    preset_pos(&total);
    for (n = 0; n < total; n++) {
        if (preset_at(n, &k) != e)
            continue;
        preset_go(n);
        if (t->eng_req != e || t->user) {
            printf("cover: %s: list entry %u does not load engine %u\n", preset_name(e, k), n, e);
            bad++;
            continue;
        }
        if (k == PRESET_INIT) {
            *init += 1;
            for (i = 0; i < 8u; i++)
                if (t->p[P_E0 + i] != ENGINES[e]->edit[i].def) {
                    printf("cover: INIT of engine %u: EDIT value %u not its default\n", e, i);
                    bad++;
                }
            hits++;
        } else
            hits += k < ENGINES[e]->npresets && preset_playable(ENGINES[e], k);
    }
    return hits;
}

/* the kits of kit UIDs lo..hi-1 on this build's kit list */
static uint32_t kits_built(uint32_t lo, uint32_t hi)
{
    uint32_t k, n = 0;
    for (k = lo; k < hi && k < DRUM_KITS; k++)
        n += (uint32_t)drum_kit_built(k);
    return n;
}

static void source(const char *name, int built, uint32_t have, uint32_t reach, uint32_t init)
{
    int ok = (!built || reach >= 1u) && !(COVER_STRICT && init);
    printf("cover: %-10s %-5s %3u %3u%s %s\n", name, built ? "built" : "-", have, reach, init ? " INIT" : "",
           ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(void)
{
    pl_whole = 1;                                       /* (the PRESETS knob's list: every engine's sounds in a row; EDIT's card keeps to one engine) */
    uint32_t e, k, init;
    host_tracks_init();
    song.sel = 0;
    bank_resolve();
    for (e = 0; e < NENGINES; e++) {
        uint32_t n = loadable(e, &init);
        source(ENG_UID_NAME[eng_uid(e)], 1, ENGINES[e]->npresets, n, init);
    }
    /* the drum sources (registry.h): kit UIDs 0..4 sampled, 5.. synthesised, 37 / 38 the X0X kits */
    source("KITS SMPL", DRUM_SMASK != 0, 5u, kits_built(0, DRUM_SAMPLED), 0);
    source("KITS SYNTH", FELUCCA_DRUM_SYNTH, DS_NKITS, kits_built(DRUM_SAMPLED, DRUM_SYNTH_END), 0);
    source("X0X 909", FELUCCA_DRUM_X909, 1u, kits_built(DRUM_UID_X909, DRUM_UID_X909 + 1u), 0);
    source("X0X 808", FELUCCA_DRUM_X808, 1u, kits_built(DRUM_UID_X808, DRUM_UID_X808 + 1u), 0);
    for (k = DRUM_UID_X808 + 1u; k < DRUM_KITS; k++)          /* (a kit UID added later: its own source) */
        source(DRUM_KIT_NAMES[k], drum_kit_built(k), 1u, (uint32_t)drum_kit_built(k), 0);
    source("KIT LIST", 1, DRUM_KITS, kits_built(0, DRUM_KITS), 0);
    source("POWER-ON", 1, 1u, (uint32_t)drum_kit_built(DRUM_DEFAULT_KIT), 0);
    for (e = 0; e < NENGINES; e++) {                  /* ENV 1 MOD and ENV2 on INIT and every preset (model.c apply_preset_to) */
        uint32_t pi, ok = 1;
        for (pi = 0; pi <= ENGINES[e]->npresets; pi++) {
            track_t *t = &trk[0];
            t->eng_req = (uint8_t)e;
            apply_preset_to(t, pi < ENGINES[e]->npresets ? pi : PRESET_INIT);
            ok &= !t->p[P_E1PIT] && !t->p[P_E1X] && !t->p[P_E1Y] && !t->p[P_E1Z];
#if FELUCCA_ANALOG2
            ok &= t->p[P_N2AA] != 0 || t->p[P_N2DA] == (int16_t)env2_def_dest(e);   /* (ENV2 unused: DEST A the engine's) */
            ok &= pi < ENGINES[e]->npresets || (!t->p[P_N2AA] && !t->p[P_N2AB]);   /* (INIT: ENV2 at 0) */
#endif
        }
        if (!ok) {
            printf("env: %s: ENV 1's PIT / moves not 0, or ENV2's DEST A not the engine's default (INIT, a preset)\n", ENG_UID_NAME[eng_uid(e)]);
            bad++;
        }
    }
    printf(bad ? "PRESET COVER FAILED (%d)\n" : "preset cover: every source of this build has a preset (or INIT) / kit\n", bad);
    return bad != 0;
}
