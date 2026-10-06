/* SPDX-License-Identifier: GPL-3.0-only */
/* What a load uses and this build lacks (firmware/src/miss.c, FELUCCA_MISSING_WARN): a project, a song section
 * and a user kit made on a full build, loaded on a reduced one. Built twice by tests/run_tests.sh:
 *   full:    missing_test write A.bin B.bin K.bin
 *            A: part 1 FM6 (DLY send), part 2 SAMPLE on PIANO, part 3 ANALOG with SLICER and a step's chance;
 *            the drum track on 909 with a lane on the 606, a lane on USR2, a lane edited, a lane's own DLY
 *            send; DUST up. B: A with part 2 PHYS (a song section). K: the user kit bank (kit 1: these lanes).
 *            On the full build (USR2 holds a sample) nothing is missing.
 *   reduced: missing_test reduce A.bin B.bin K.bin DIR   (FM6, the drum synth, DELAY, DUST, SLICER, the drum
 *            editor and lane sends, chance, PHYS left out; PIANO left out of the sample sets: a header made
 *            with FELUCCA_SAMPLES_SKIP=PIANO; USR2 empty)
 *            the scan names each item once, in order, by the build's own names; the top bar line; once per
 *            item and power-on; the real UI: the message after a load, nothing again on a reload, only PHYS
 *            when the song changes to B while playing (proj_apply from the audio side), SAVE > TOOLS > MISS
 *            (the count, the items one by one), a user kit's load; screens DIR/miss-*.ppm */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint16_t screen[240*240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ uint32_t i,j; assert(x+w<=240 && y+h<=240); for(j=0;j<h;j++) for(i=0;i<w;i++) screen[(y+j)*240+x+i]=p[j*w+i]; }
#include "../firmware/src/gfx.c"
static void lcd_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c)
{ uint32_t i,j; assert(x+w<=240&&y+h<=240); for(j=0;j<h;j++)for(i=0;i<w;i++)screen[(y+j)*240+x+i]=swap16(c); }
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
static uint8_t fm1_led[16], fm1_led_dim[16];
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
static void fm1_wdt_feed(void) {}
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#define FELUCCA_ICONS 1
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/ui_drums.c"
static int project_used(uint32_t i) { return i < 2; }
static void project_save(uint32_t i) { (void)i; }
static void project_load(uint32_t i) { (void)i; }
static void arrangement_save(void) {}
static uint32_t arrangement_ready(void) { return 3; }
static void arrangement_apply(uint32_t s) { (void)s; }
static void song_backup(void) {}
static void song_restore(void) {}
static void section_store(uint32_t s) { live_sec = (int8_t)s; }
static void section_load(uint32_t s) { live_sec = (int8_t)s; }
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return 1; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { (void)k; b[0] = 0; }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; }
static void settings_save(void) {}
#include "../firmware/src/ui_song.c"
#include "../firmware/src/ui_studio.c"
#include "../firmware/src/ui_fm6.c"
#include "../firmware/src/icons.c"
#define PROJ_HOST 1
#include "../firmware/src/project.c"
#include "../firmware/src/miss.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_overview.c"
#include "../firmware/src/ui_layers.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/fm6_store.c"
/* the user kit bank (drum_kits.c) on a RAM image of its two sectors, through storage.c */
static uint8_t kit_nor[0x2000];
static int st_read(uint32_t off, void *dst, uint32_t n)
{ if (off < 0xDA000u || off + n > 0xDC000u) return -1; memcpy(dst, kit_nor + off - 0xDA000u, n); return 0; }
static int st_erase(uint32_t off) { if (off < 0xDA000u || off >= 0xDC000u) return -1; memset(kit_nor + off - 0xDA000u, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{ uint32_t i; if (off < 0xDA000u || off + n > 0xDC000u) return -1; for (i = 0; i < n; i++) kit_nor[off - 0xDA000u + i] &= ((const uint8_t *)src)[i]; return 0; }
#include "../firmware/src/storage.c"
static uint32_t kit_tmp[4096 / 4];
#define UK_HOST 1
#define UK_TMP ((ukit_bank_t *)(void *)kit_tmp)
#include "../firmware/src/drum_kits.c"
#include "../firmware/src/splash.c"

static const char *outdir;
static void ppm(const char *name)
{
    char path[512];
    unsigned i;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t p = swap16(screen[i]);
        uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void frame(void)
{
    uint32_t q;
    static int32_t o[CTL * 2];
    for (q = 0; q < 22u; q++)
        mix_block(o, CTL);
    ui_input();
    ui_leds();
    ui_draw();
    fm1_ms += 16;
}
static void frames(uint32_t n) { while (n--) frame(); }

static int bad;
static void check(const char *what, int ok)
{
    printf("missing: %-74s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
typedef struct { project_t p; dlrec_t d; } pfile_t;   /* a project and its drum record (proj_capture) */
static int rd(const char *f, void *p, size_t n)
{
    FILE *h = fopen(f, "rb");
    size_t r = h ? fread(p, 1, n, h) : 0;
    if (h)
        fclose(h);
    return r == n;
}
static void wr(const char *f, const void *p, size_t n)
{
    FILE *h = fopen(f, "wb");
    assert(h);
    fwrite(p, 1, n, h);
    fclose(h);
}
/* a user slot in the host image: one zone (drum_edit_test.c host_usr_slot) */
static void usr_slot(uint32_t k)
{
    uint8_t *s = (uint8_t *)host_slots + k * SMP_USER_SIZE;
    smp_user_hdr_t *h = (smp_user_hdr_t *)s;
    memset(s, 0, SMP_USER_SIZE);
    h->magic = SMP_USER_MAGIC;
    h->version = 1;
    h->nz = 1;
    h->data_len = 512;
    h->zone[0].n = 1000;
    h->zone[0].le = 999;
    h->zone[0].rate = 65536u;
    h->zone[0].root16 = 60 * 16;
    h->zone[0].hi = 127;
    smp_user_scan(k);
}
static uint32_t set_of(const char *name)
{
    uint32_t i;
    for (i = 0; i < SMP_NALL; i++)
        if (str_eq(SMP_ALL_NAMES[i], name))
            return i;
    return 0;
}
static uint32_t kit_of(const char *name)
{
    uint32_t i;
    for (i = 0; i < DRUM_KITS; i++)
        if (str_eq(DRUM_KIT_NAMES[i], name))
            return i;
    return 0;
}
static void ui_start(void)
{
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
}
static void load(const pfile_t *f, int all) { proj_apply(&f->p, &f->d, all); }   /* (all 0: a song section) */

#if !FELUCCA_ENG_FM6                                    /* ---- the reduced build */
static void lines(const char *what, uint32_t lim, const char *want)
{
    char b[30];
    miss_line(b, miss_scan(), lim);
    printf("missing:   \"%s\"\n", b);
    check(what, !strcmp(b, want));
}
static int reduce(char **argv)
{
    static pfile_t a, b;
    static uint8_t kits[sizeof kit_nor];
    uint32_t n, i;
    char t[16], *e;
    check("reduced: FM6, PHYS, the drum synth, DELAY, DUST, SLICER, drum EDIT and SENDS, CHANCE out; PIANO empty",
          !ENG_HAS(FM6) && !ENG_HAS(PHYS) && !FELUCCA_DRUM_SYNTH && !FELUCCA_FX_DELAY && !FELUCCA_FX_DUST &&
          !FELUCCA_FX_SLICER && !FELUCCA_DRUM_EDIT && !FELUCCA_DRUM_SENDS && !FELUCCA_CHANCE &&
          !SMP_SETS[set_of("PIANO")].nz && SMP_SETS[set_of("BASS")].nz);
    check("reduced: A, B and the kit bank read", rd(argv[2], &a, sizeof a) && proj_ok(&a.p) && rd(argv[3], &b, sizeof b) &&
          proj_ok(&b.p) && rd(argv[4], kits, sizeof kits));
    outdir = argv[5];
    ui_start();
    /* the scan, by itself */
    {
        static const struct { uint16_t it; const char *name; } WANT[] = {
            {MS_ITEM(MS_ENG, 0, ENG_UID_FM6), "FM6 T1"}, {MS_ITEM(MS_FX, 0, MF_DELAY), "DELAY"},
            {MS_ITEM(MS_SET, 1, 0), "PIANO T2"}, {MS_ITEM(MS_FX, 2, MF_CHANCE), "CHANCE"},
            {MS_ITEM(MS_KIT, 3, 0), "KIT 909"}, {MS_ITEM(MS_KIT, 3, 0), "KIT 606"},
            {MS_ITEM(MS_USR, 3, 1), "USR2 EMPTY"}, {MS_ITEM(MS_FX, 3, MF_SNDED), "SOUND EDIT"},
            {MS_ITEM(MS_FX, 3, MF_LFX), "LANE FX"}, {MS_ITEM(MS_FX, 2, MF_SLICER), "SLICER"},
            {MS_ITEM(MS_FX, 3, MF_DUST), "DUST"}};
        uint32_t ok = 1;
        load(&a, 1);
        n = miss_scan();
        for (i = 0; i < n; i++) {
            *miss_name(t, miss_m[i]) = 0;
            printf("missing:   %2u %04X %s\n", i, miss_m[i], t);
        }
        for (i = 0; i < n && i < sizeof WANT / sizeof WANT[0]; i++) {
            miss_name(t, miss_m[i]);
            ok &= !strcmp(t, WANT[i].name) && (WANT[i].it >> 12 == MS_KIT || miss_m[i] == WANT[i].it);
        }
        check("A on the reduced build: 11 items in order, each once (DELAY: from 3 tracks)",
              n == sizeof WANT / sizeof WANT[0] && ok);
    }
    lines("the top bar: what fits 25 characters, then the count of the others", 25u, "MISSING: FM6 T1, DELAY +9");
    lines("a layer screen's title (less room): the first item, the count", 14u, "MISSING: FM6 T1 +10");
    check("the parts play their stand-ins (FM6 -> DIGITAL), the kit is kept (909 UID)",
          ENGINES[trk[0].eng_req] == &ENG_DIGITAL && TDRUM->p[P_E0] == (int16_t)kit_of("909"));
    n = miss_fresh(miss_scan());
    check("once per item: the first load's 11 are new ...", n == 11u);
    check("... and none of them a second time", miss_fresh(miss_scan()) == 0u);
    memset(miss_said, 0, sizeof miss_said);                 /* (power-on again, for the UI below) */

    /* the UI: the message after a load; not again; a song section while playing: only what is new */
    go_home();
    ui.force = 1;
    frame();
    load(&a, 1);                                /* (project_apply: a load) */
    frame();
    check("after the load: the top bar says it", ui.msg_t > 40u && !strcmp(ui.msg, "MISSING: FM6 T1, DELAY +9"));
    ui.force = 1;
    frame();
    ppm("miss-load");
    frames(160);
    check("the message goes after ~2.5 s", ui.msg_t == 0u);
    load(&a, 1);
    frames(2);
    check("the same project again: nothing said", ui.msg_t == 0u);
    song.playing = 1;
    edges_btn |= 1u << panel.btn[B_SAVE];                   /* SAVE held: the song layer (SAVE + a key: a section) */
    fm1_in.buttons |= 1u << panel.btn[B_SAVE];
    frames(12);
    load(&b, 0);                                /* (sections.c / arranger_scene.c: from the audio ISR) */
    frame();
    check("playing, the song changes to B: only PHYS T2", !strcmp(ui.msg, "MISSING: PHYS T2"));
    ui.force = 1;
    frame();
    ppm("miss-section-layer");
    check("... said on the song layer too (SAVE held)", ui.layer != LY_PLAY && ui.msg_t);
    ui.layer_used = 1;                                      /* (a key was used: no tap on release) */
    fm1_in.buttons &= ~(1u << panel.btn[B_SAVE]);
    frames(2);
    ui.force = 1;
    frame();
    ppm("miss-section");
    check("... its part plays ANALOG meanwhile, the audio goes on", ENGINES[trk[1].eng_req] == &ENG_ANALOG && song.playing);
    frames(160);
    load(&a, 0);
    load(&b, 0);
    frames(2);
    check("A, B, A, B: nothing more", ui.msg_t == 0u);
    song.playing = 0;

    /* SAVE > TOOLS > MISS: the count, the items one by one */
    for (i = 0; i < NPAGES && !(PAGES[i].scope == SC_GLOBAL && PAGES[i].id[2] == G_MISS); i++)
        ;
    check("SAVE > TOOLS has MISS on KNOB 3", i < NPAGES && PAGES[i].fam == FAM_SAVE && str_eq(PAGES[i].title, "TOOLS"));
    open_family(FAM_SAVE);
    ui.page = (uint8_t)i;
    page_entered();
    frames(2);
    check("TOOLS: MISS shows the count (B: 11)", miss_n == 11);
    ui.force = 1;
    frame();
    ppm("miss-tools");
    encs[panel.enc[EN_K3]] = 1;
    frame();
    e = strstr(ui.msg, "MISSING 2/11: ");
    check("MISS turned: the next item, 2/11 DELAY", e == ui.msg && !strcmp(ui.msg + 14, "DELAY"));
    ui.force = 1;
    frame();
    ppm("miss-tools-2");
    encs[panel.enc[EN_K3]] = -1;
    frame();
    encs[panel.enc[EN_K3]] = -3;                            /* (a fast turn: still one item) */
    frame();
    check("... back past the first: 11/11 DUST", !strcmp(ui.msg, "MISSING 11/11: DUST"));
    for (i = 0; i < 3u; i++) {
        encs[panel.enc[EN_K3]] = 1;
        frame();
    }
    frame();
    check("... 3/11 PHYS T2", !strcmp(ui.msg, "MISSING 3/11: PHYS T2"));
    ui.force = 1;
    frame();
    ppm("miss-tools-3");

    /* the user replaces what is missing: it leaves the list */
    trk[1].eng_req = (uint8_t)ENG_SLOT_ANALOG;             /* (the orphan: another sound now) */
    trk[1].p[P_E0] = 3;
    frames(2);
    check("part 2 given another sound: PHYS leaves the count (10)", miss_n == 10);

    /* a user kit made on the full build */
    memcpy(kit_nor, kits, sizeof kit_nor);
    uk_read = 0;
    memset(&dl, 0, sizeof dl);
    memset(dsend, 0, sizeof dsend);
    TDRUM->p[P_E0] = (int16_t)DRUM_DEFAULT_KIT;
    memset(miss_said, 0, sizeof miss_said);                 /* (power-on again: A said them all already) */
    miss_fresh(miss_scan());                                /* (the parts' items: said) */
    go_home();
    frames(2);
    ui.msg_t = 0;
    check("the full build's kit 1 loads", ukit_load(0) == 1);
    frame();
    printf("missing:   \"%s\"\n", ui.msg);
    check("the user kit: its 909 and 606, its sample on USR2, its edit, its lane send are named",
          !strcmp(ui.msg, "MISSING: KIT 909, KIT 606 +3") && ui.msg_t);
    for (n = miss_scan(), i = 0; i < n; i++)
        ;
    check("... all of them in the list", n >= 5u);
    ui.force = 1;
    frame();
    ppm("miss-kit");

    /* the X0X kits (UIDs 37 / 38, off here): the track on the 909, a lane on the 808, both named */
    {
        uint32_t k909 = 0, k808 = 0;
        TDRUM->p[P_E0] = (int16_t)kit_of("X0X 909");
        dl.src[2] = (uint8_t)(DL_KIT0 + kit_of("X0X 808"));
        for (n = miss_scan(), i = 0; i < n; i++) {
            *miss_name(t, miss_m[i]) = 0;
            k909 |= !strcmp(t, "KIT X0X 909");
            k808 |= !strcmp(t, "KIT X0X 808");
        }
        check("the X0X kits: UIDs 37 / 38, not built here", kit_of("X0X 909") == 37u && kit_of("X0X 808") == 38u &&
              !FELUCCA_DRUM_X909 && !FELUCCA_DRUM_X808);
        check("the X0X kits: the scan names KIT X0X 909 (the track) and KIT X0X 808 (a lane)", k909 && k808);
        check("the X0X 909: a built stand-in plays, the UID is kept",
              drum_kit_built(drum_kit()) && TDRUM->p[P_E0] == 37);
        /* an X0X voice on a lane of a built kit (DL_X808 + CB): its machine named, a built stand-in plays it */
        TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
        dl.src[2] = DL_KIT;
        dl.src[15] = (uint8_t)(DL_X808 + 12u);
        for (k808 = 0, n = miss_scan(), i = 0; i < n; i++) {
            *miss_name(t, miss_m[i]) = 0;
            k808 |= !strcmp(t, "KIT X0X 808");
        }
        check("an X0X voice on a lane (the 808's CB), not built: the scan names KIT X0X 808; a built kit plays it",
              k808 && drum_kit_built(dl_kit_of(15, drum_kit())) && dl.src[15] == DL_X808 + 12u);
        dl.src[15] = DL_KIT;
        /* a style kit (UID 43, X8 TRAP) on the track: named as a kit, a built stand-in plays, the UID kept */
        TDRUM->p[P_E0] = (int16_t)(DRUM_UID_XSTYLE + 4u);
        for (k808 = 0, n = miss_scan(), i = 0; i < n; i++) {
            *miss_name(t, miss_m[i]) = 0;
            k808 |= !strcmp(t, "KIT X8 TRAP");
        }
        check("a style kit (X8 TRAP, UID 43), not built: the scan names KIT X8 TRAP, a built stand-in plays it",
              k808 && drum_kit_built(drum_kit()) && TDRUM->p[P_E0] == 43);
        TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    }
    return bad;
}
#endif

int main(int argc, char **argv)
{
    static pfile_t a, b;
    if (argc == 5 && !strcmp(argv[1], "write") && ENG_HAS(FM6) && ENG_HAS(PHYS)) {   /* ---- the full build */
        uint32_t i;
        ui_start();
        usr_slot(1);                                         /* (USR2 holds a sample here) */
        host_preset(&trk[0], ENG_SLOT_FM6, 3);               /* BELLS: DLY 29 */
        host_preset(&trk[1], ENG_SLOT_SAMPLE, 0);
        trk[1].p[P_E0] = (int16_t)set_of("PIANO");
        host_preset(&trk[2], ENG_SLOT_ANALOG, 2);
        trk[2].p[P_SLCR] = 1;
        trk[2].step[0].flags = 4u << 2;                      /* (chance.c: 80 %) */
        TDRUM->p[P_E0] = (int16_t)kit_of("909");
        memset(&dl, 0, sizeof dl);
        memset(dsend, 0, sizeof dsend);
        dl.src[2] = (uint8_t)(DL_KIT0 + kit_of("606"));
        dl.src[3] = DL_USR + 1u;
        dl.ofs[4][DE_TUNE] = 3;
        dsend[5] = dsend_word(-1, 10, 0);
        song.g[G_DUST] = 30;
        proj_capture(&a.p, &a.d);
        load(&a, 1);
        check("full: nothing is missing (the scan is empty)", miss_scan() == 0u);
        check("full: kit 1 stored from these lanes", ukit_store(0) == 0);
        host_preset(&trk[1], ENG_SLOT_PHYS, 0);
        proj_capture(&b.p, &b.d);
        for (i = 0; i < 2u; i++)
            wr(argv[2 + i], i ? (void *)&b : (void *)&a, sizeof a);
        wr(argv[4], kit_nor, sizeof kit_nor);
        check("full: A (FM6, PIANO, 909 ...) and B (PHYS on part 2) written",
              a.p.t[0].engine == ENG_UID_FM6 && b.p.t[1].engine == ENG_UID_PHYS);
        return bad;
    }
#if !FELUCCA_ENG_FM6
    if (argc == 6 && !strcmp(argv[1], "reduce"))
        return reduce(argv);
#endif
    fprintf(stderr, "usage: missing_test write A B K (full build) | reduce A B K DIR (reduced build)\n");
    return 2;
}
