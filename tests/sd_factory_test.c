/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card zip's factory presets (tools/sd_factory.c, tools/sd_zip.py; SD card mode reads them in place of the
 * tables: sd_mode.c sdm_factory_*): every engine's every preset applied as the device applies it (apply_preset_to),
 * taken as the card's record (up_card_take), then loaded back into a part on another sound (up_card_give, as
 * sdm_factory_poll does): every value of the sound the same, an FM6 voice too; the FM6 packs' voices too (numbered on
 * after FM6's own presets, sd_factory.c extras). With FILEs (a build without the packs: the zip's FM6 files): each
 * loads with its own voice bytes, never a pack the build does not have. With --kits and the zip's factory kits (every
 * drum source built): one a kit UID, named as the kit list does, each loaded as SD card mode does playing as the
 * flash's kit, every lane's hit sample for sample.
 *   sd_factory_test [FILE.ops ...]   sd_factory_test --kits FILE.opk ...   (FELUCCA_DRUM_X909 / X808 on) */
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
static uint8_t uk_tmp[4096] __attribute__((aligned(4)));   /* (drum_kits.c's bank scratch: the host's own) */
#define UK_TMP ((ukit_bank_t *)(void *)uk_tmp)
#define UK_HOST 1
#define MISS_BUMP() ((void)0)
#include "../firmware/src/drums/drum_kits.c"      /* ukit_card_take / ukit_card_give (UK_CARD_ON) */
#include <ctype.h>
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

/* KITS mode (the zip's factory kits, Optimist/Kits/Factory/<L>/*.opk, in order; every drum source built): one file a
 * kit UID, named as the kit list names it, and each kit loaded from its file (sd_mode.c: ukit_card_give, as SD card mode
 * loads a kit) plays as the kit picked in flash mode (fresh lanes, the kit: dsnd_desc.c dl_reset_lanes, dl_kit_set):
 * every lane's hit rendered, sample for sample */
#define KT_N (FS / 4u)                                 /* a hit: 250 ms */
static int32_t kt_a[DRUM_LANES][KT_N], kt_b[DRUM_LANES][KT_N];
static void kit_render(int32_t (*o)[KT_N])
{
    uint32_t l, j;
    int32_t r[CTL], rv[CTL];
    for (l = 0; l < DRUM_LANES; l++) {
        memset(&drums, 0, sizeof drums);
        drums_set = -2;
        song.g[G_DRLVL] = 100;
        drum_on(DLANE_NOTE(l), 110);
        for (j = 0; j + CTL <= KT_N; j += CTL) {
            memset(&o[l][j], 0, CTL * sizeof o[l][0]), memset(r, 0, sizeof r), memset(rv, 0, sizeof rv);
            drums_render(&o[l][j], r, rv, CTL);
        }
    }
}
/* kit k's hits into o, rendered in a child process of this one (so both ways start from the same state of the drum
 * machines, X0X's and KAWA's filters among it): way 0 the kit picked on fresh lanes as flash mode does (dsnd_desc.c
 * dl_reset_lanes, dl_kit_set); else the payload rec loaded over another kit as SD card mode does (ukit_card_give).
 * 0 rendered */
static int kit_way(int32_t (*o)[KT_N], uint32_t k, const uint8_t *rec)
{
    int fd[2], st = 0;
    size_t got = 0;
    ssize_t r;
    pid_t pid;
    if (pipe(fd))
        return -1;
    if ((pid = fork()) == 0) {
        close(fd[0]);
        memset(&dl, 0, sizeof dl);
        memset(dsend, 0, sizeof dsend);
        TDRUM->p[P_E0] = (int16_t)(rec ? (k ? 0u : 1u) : k);
        dl_e0 = TDRUM->p[P_E0];
        if (rec && ukit_card_give(rec, UK_CARD_N))
            _exit(1);
        kit_render(o);
        for (got = 0; got < sizeof kt_a; got += (size_t)r)
            if ((r = write(fd[1], (const char *)o + got, sizeof kt_a - got)) <= 0)
                _exit(1);
        _exit(0);
    }
    close(fd[1]);
    while (pid > 0 && got < sizeof kt_a && (r = read(fd[0], (char *)o + got, sizeof kt_a - got)) > 0)
        got += (size_t)r;
    close(fd[0]);
    if (pid > 0)
        waitpid(pid, &st, 0);
    return pid > 0 && got == sizeof kt_a && WIFEXITED(st) && !WEXITSTATUS(st) ? 0 : -1;
}
/* a kit file's instruments as its name must say them (the user, 2026-11-04: "<nn> - <name> (909, sampled).opk"), from
 * its lanes' sources by the names the kit list gives them (not drum_kits.c's UID ranges): a sampled kit (the first five)
 * "sampled", an X0X 909 kit or voice (X0X 909, X9 ...) "909", the 808's "808", KAWA, PLAITS, any other kit "synth"; in
 * the order 909, 808, synth, kawa, plaits, sampled */
static const char *kit_instr_of(const uint8_t *rec)
{
    static char out[64];
    static const char *const F[6] = {"909", "808", "synth", "kawa", "plaits", "sampled"};
    ukit_t kt;
    uint32_t l, m = 0, i, src;
    memcpy(&kt, rec, sizeof kt);
    for (l = 0; l < DRUM_LANES; l++) {
        const char *nm;
        src = kt.src[l];
        if (src >= DL_USR && src <= DL_CARD) {
            m |= 32u;
            continue;
        }
        nm = src >= DL_X909 && src < DL_X909 + DL_X909_N ? "X9 voice" : src >= DL_X808 && src < DL_X808 + DL_X808_N
             ? "X8 voice" : src >= DL_XKIT0 ? DRUM_KIT_NAMES[DRUM_UID_XKIT + src - DL_XKIT0]
             : src >= DL_KIT0 ? DRUM_KIT_NAMES[src - DL_KIT0] : 0;
        if (!nm)
            continue;
        if (src >= DL_KIT0 && src < DL_KIT0 + DRUM_SAMPLED)
            m |= 32u;
        else if (!strncmp(nm, "X0X 909", 7) || !strncmp(nm, "X9 ", 3))
            m |= 1u;
        else if (!strncmp(nm, "X0X 808", 7) || !strncmp(nm, "X8 ", 3))
            m |= 2u;
        else if (!strcmp(nm, "KAWA"))
            m |= 8u;
        else if (!strcmp(nm, "PLAITS"))
            m |= 16u;
        else
            m |= 4u;
    }
    out[0] = 0;
    for (i = 0; i < 6u; i++)
        if (m >> i & 1u)
            snprintf(out + strlen(out), sizeof out - strlen(out), "%s%s", out[0] ? ", " : " (", F[i]);
    if (out[0])
        snprintf(out + strlen(out), sizeof out - strlen(out), ")");
    return out;
}
static int kits_mode(int argc, char **argv)
{
    static uint8_t f[512];
    uint32_t k, n = 0, silent = 0;
    char want[128], nm[16];
    for (k = 0; (int)k + 2 < argc && k < 255u; k++) {
        const char *p = argv[k + 2], *b = strrchr(p, '/') ? strrchr(p, '/') + 1 : p;
        FILE *fp = fopen(p, "rb");
        size_t got = fp ? fread(f, 1, sizeof f, fp) : 0;
        uint32_t i;
        int64_t e = 0;
        if (fp)
            fclose(fp);
        str_cpy(nm, k < DRUM_KITS ? DRUM_KIT_NAMES[k] : "?", sizeof nm);
        for (i = 0; nm[i]; i++)
            nm[i] = (char)tolower((unsigned char)nm[i]);
        snprintf(want, sizeof want, "%02u - %s%s.opk", (unsigned)(k % 16u + 1u), nm,
                 got == 16u + UK_CARD_N ? kit_instr_of(f + 16u) : "");
        if (k >= DRUM_KITS || strcmp(b, want) || p[strlen(p) - strlen(b) - 2] != (char)('A' + k / 16u)) {
            printf("FAIL %s: kit %u is %c/%s\n", p, k, 'A' + k / 16u, want);
            bad++;
            continue;
        }
        if (got != 16u + UK_CARD_N || memcmp(f, "OPKT", 4)) {
            printf("FAIL %s: not a kit file (%zu B)\n", p, got);
            bad++;
            continue;
        }
        if (kit_way(kt_a, k, 0) || kit_way(kt_b, k, f + 16u)) {   /* flash mode, then SD card mode */
            printf("FAIL %s: not loaded\n", p);
            bad++;
            continue;
        }
        for (i = 0; i < DRUM_LANES; i++) {
            uint32_t j;
            for (j = 0; j < KT_N; j++)
                e += kt_a[i][j] < 0 ? -kt_a[i][j] : kt_a[i][j];
        }
        silent += !e;
        if (memcmp(kt_a, kt_b, sizeof kt_a)) {
            printf("FAIL %s: kit %u (%s) from the card does not play as the flash's\n", p, k, DRUM_KIT_NAMES[k]);
            bad++;
        }
        n++;
    }
    if (n != DRUM_KITS || silent) {
        printf("FAIL kits: %u files of %u kits, %u silent\n", n, (unsigned)DRUM_KITS, silent);
        bad++;
    }
    printf("sd factory test: %u factory kits (of %u) from the card, every lane's hit as the flash's, %u failed\n", n,
           (unsigned)DRUM_KITS, bad);
    return bad != 0;
}

int main(int argc, char **argv)
{
    static uint8_t rec[sizeof(up_rec_t) + UP_CARD_FM6];
    static int16_t ed[sizeof fm6_ed[0] / sizeof fm6_ed[0][0]];
    uint32_t e, pi, i, n, total = 0, fm6 = 0;
    char nm[16];
    host_tracks_init();
    song.sel = 0;
    if (argc > 1 && !strcmp(argv[1], "--kits"))
        return kits_mode(argc, argv);
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
