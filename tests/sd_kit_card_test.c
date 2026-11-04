/* SPDX-License-Identifier: GPL-3.0-only */
/* SD card mode's drums from the card (registry.h KITS_CARD, X909_CARD; the user, 2026-11-04: "the drum kits samples as
 * well", the 909's hats and cymbals; drums/kit_card.c, drums/x0x/x0x_card.c) in the host's render (tests/hostsim.c),
 * the card's files read from DIR as tools/sd_samples.py wrote them (each a source, the rings filled between two audio
 * blocks as the main loop's runs would):
 *   - the factory kit ACOUSTIC loaded from its card file (Kits/Factory/A/01 - acoustic.opk, as sd_mode.c loads a kit:
 *     ukit_card_give): the files its lanes want (kc_want) the lanes' sounds; every lane's hit, then every lane at once,
 *     streamed through the rings bit-identical to the same hits on each whole file in RAM (its head: the files' own
 *     samples, no card read), sounding, never dry; DEEP, TIGHT, BRIGHT and DUST the same files treated (not ACOUSTIC's
 *     render); at its pitch (kick, note 36 on its root) the voice reads the file's samples one by one;
 *   - a sound not on the card (its source none: MISSING, sd_mode.c says it): that lane silent, the others not;
 *   - the X0X 909: its hats, crash and ride from X0X/909/ (x9c_want: the files its lanes play), streamed bit-identical to
 *     the whole files, sounding; not on the card: those lanes silent.
 *   sd_kit_card_test DIR (the zip's content: Optimist/...) */
#define FELUCCA_SD_STREAM 1
#define FELUCCA_SD_RING_KB 4
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
static uint8_t nor[0x100000];                        /* (drum_kits.c's flash bank: storage.c on RAM) */
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
static void ui_message(const char *m) { (void)m; }
static void ui_say(const char *a, const char *b) { (void)a; (void)b; }
static struct { int force, redraw; } ui;
#define UK_CARD_ON 1
static uint8_t uk_tmp[4096] __attribute__((aligned(4)));   /* (drum_kits.c's bank scratch: the host's own) */
#define UK_TMP ((ukit_bank_t *)(void *)uk_tmp)
#define UK_HOST 1
#define MISS_BUMP() ((void)0)
#include "../firmware/src/drums/drum_kits.c"      /* ukit_card_give: a kit file's payload into the lanes */

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t *file[SST_NSRC];
static uint32_t fsize[SST_NSRC];
static sst_run_t run1[SST_NSRC];
static uint8_t ring_mem[SST_N * SST_K * 512u] __attribute__((aligned(4)));
static char path[SST_NSRC][600];

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

/* source si from a card WAV (16-bit mono, as sd_stream.c sst_wav takes it), one run; whole: its head the whole file */
static int load_src(uint32_t si, const char *p, int whole)
{
    FILE *f = fopen(p, "rb");
    sst_src_t *s = &sst_src[si];
    uint32_t at = 12, n, rate = 0;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    fsize[si] = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    free(file[si]);
    file[si] = calloc(1, fsize[si] + 512u);
    if (fread(file[si], 1, fsize[si], f) != fsize[si]) {
        fclose(f);
        return 0;
    }
    fclose(f);
    memset(s, 0, sizeof *s);
    s->hi = 127;
    while (at + 8u <= fsize[si]) {
        const uint8_t *c = file[si] + at;
        n = le32(c + 4);
        if (!memcmp(c, "fmt ", 4))
            rate = le32(c + 12);
        else if (!memcmp(c, "data", 4))
            s->dofs = at + 8u, s->dlen = n;
        at += 8u + n + (n & 1u);
    }
    s->bits = SST_F16;
    s->chans = 1;
    s->fsh = 1;
    s->frames = s->dlen / 2u;
    s->rate = (uint32_t)(((uint64_t)rate << 16) / 44100u);
    s->hlen = whole ? s->dlen & ~3u : (uint32_t)((25u * rate / 1000u * 2u + 3u) & ~3u);   /* (a drum sound's 25 ms) */
    s->head = file[si] + s->dofs;
    run1[si].n = (fsize[si] + 511u) / 512u;
    s->run = &run1[si];
    s->nrun = 1;
    s->ready = 1;
    return s->frames > 0 && rate == 44100u;
}

static void produce(void)                            /* the main loop's runs, from the files' images */
{
    uint32_t i, last, slot;
    for (i = 0; i < SST_N; i++) {
        sst_t *st = &sst[i];
        const sst_src_t *s = &sst_src[st->src];
        if (!st->on || (st->owner && !*st->owner))
            continue;
        last = (s->dofs + s->dlen - 1u) >> 9;
        if (st->pgen != st->gen) {
            st->pgen = st->gen;
            st->wr = 0;
            st->next = (s->dofs + s->hlen) >> 9;
        }
        while (st->wr - st->rd < SST_K && st->next <= last) {
            slot = st->wr % SST_K;
            st->tag[slot] = 0;
            memcpy(st->ring + slot * 512u, file[st->src] + st->next * 512u, 512);
            st->tag[slot] = sst_tag(st->gen, st->next);
            st->wr++;
            st->next++;
        }
    }
}

#define HIT (FS / 3u)                                /* a hit's render: a third of a second */
#define NOUT (DRUM_LANES + 1u)                       /* every lane alone, then every lane at once */
static int32_t (*OUT)[HIT];
static uint32_t *ST;                                 /* (the child's sst_dry, sst_nostream) */

static void render_hits(void)
{
    uint32_t l, j, k;
    int32_t r[CTL], rv[CTL];
    for (l = 0; l < NOUT; l++) {
        drums_off();                                 /* (each hit from silence: drums_off, as All Sound Off) */
        drums.tail = 0;
        song.g[G_DRLVL] = 100;
        for (k = 0; k < DRUM_LANES; k++)
            if (l == DRUM_LANES || k == l)
                drum_on(DLANE_NOTE(k), 110);
        for (j = 0; j + CTL <= HIT; j += CTL) {
            produce();
            memset(&OUT[l][j], 0, CTL * sizeof OUT[l][0]), memset(r, 0, sizeof r), memset(rv, 0, sizeof rv);
            drums_render(&OUT[l][j], r, rv, CTL);
        }
        drums_off();
        for (j = 0; j < 8u; j++) {                   /* (the streams given back, the tail out) */
            memset(r, 0, sizeof r), memset(rv, 0, sizeof rv);
            drums_render(r, r, rv, CTL);
        }
    }
}

static uint8_t kit_rec[512];
static uint32_t nsrc;
enum { W_STREAM = 0, W_WHOLE = 1 };
/* kit k's hits (the card's kit file loaded, then kit k; 0xFF: as loaded) into o, in a child: from one state */
static void play(int32_t (*o)[HIT], uint32_t k, int whole, int32_t drop_kit, int x9_none)
{
    pid_t pid = fork();
    int st;
    uint32_t i;
    if (pid == 0) {
        OUT = o;
        for (i = 0; i < nsrc; i++)
            load_src(i, path[i], whole);
        for (i = 0; i < SST_N; i++)
            sst[i].on = 0, sst[i].ring = ring_mem + i * SST_K * 512u;
        sst_dry = sst_nostream = 0;
        memset(&dl, 0, sizeof dl);
        memset(dsend, 0, sizeof dsend);
        if (ukit_card_give(kit_rec + 16u, UK_CARD_N))
            _exit(3);
        if (k != 0xFFu) {
            TDRUM->p[P_E0] = (int16_t)k;
            dl_e0 = TDRUM->p[P_E0];
            memset(dl.src, 0, sizeof dl.src);        /* (the lanes on the kit) */
        }
        if (drop_kit >= 0)
            sst_kit_src[drop_kit] = 0xFF;
        if (x9_none)
            for (i = 0; i < SST_NX9; i++)
                sst_x9_src[i] = 0xFF;
        render_hits();
        ST[0] = sst_dry;
        ST[1] = sst_nostream;
        _exit(0);
    }
    waitpid(pid, &st, 0);
    CHECK(pid > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0, "the render ran (kit %u)", k);
}

static uint64_t energy(const int32_t *x)
{
    uint64_t e = 0;
    uint32_t j;
    for (j = 0; j < HIT; j++)
        e += (uint64_t)((int64_t)x[j] * x[j]) >> 8;
    return e;
}

int main(int argc, char **argv)
{
    static const char *const X9[SST_NX9] = {"hh.wav", "crash.wav", "ride.wav"};
    int32_t (*A)[HIT], (*B)[HIT];
    uint32_t k, l, i, d, nlane_snd = 0;
    char kp[700];
    FILE *f;
    size_t got;
    if (argc != 2) {
        printf("usage: sd_kit_card_test DIR\n");
        return 2;
    }
    A = mmap(0, 2u * sizeof(int32_t) * NOUT * HIT + 64u, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (A == MAP_FAILED)
        return 2;
    B = A + NOUT;
    ST = (uint32_t *)(void *)(B + NOUT);
    host_tracks_init();
    for (k = 0; k < SST_NKIT; k++) {                 /* the kit's sounds: sources 0..15, the 909's 16..18 */
        snprintf(path[k], sizeof path[k], "%s/Optimist/Samples/Kits/Acoustic/%s", argv[1], KC_FILE[k]);
        CHECK(load_src(k, path[k], 0), "%s: a 44.1 kHz 16-bit mono file", path[k]);
        sst_kit_src[k] = (uint8_t)k;
    }
    for (k = 0; k < SST_NX9; k++) {
        snprintf(path[SST_NKIT + k], sizeof path[0], "%s/Optimist/Samples/X0X/909/%s", argv[1], X9[k]);
        CHECK(load_src(SST_NKIT + k, path[SST_NKIT + k], 0), "%s read", path[SST_NKIT + k]);
        sst_x9_src[k] = (uint8_t)(SST_NKIT + k);
    }
    nsrc = SST_NKIT + SST_NX9;
    snprintf(kp, sizeof kp, "%s/Optimist/Kits/Factory/A/01 - acoustic (sampled).opk", argv[1]);
    f = fopen(kp, "rb");
    got = f ? fread(kit_rec, 1, sizeof kit_rec, f) : 0;
    if (f)
        fclose(f);
    CHECK(got == 16u + UK_CARD_N && !memcmp(kit_rec, "OPKT", 4), "%s: a kit file (%zu B)", kp, got);
    memset(&dl, 0, sizeof dl);
    CHECK(!ukit_card_give(kit_rec + 16u, UK_CARD_N) && drum_kit() == 0u, "ACOUSTIC loaded from its file (kit %u)",
          drum_kit());
    for (l = 0; l < DRUM_LANES; l++)
        nlane_snd |= 1u << KC_MAP[kc_of(LANE_NOTE_OF(l))][3];
    CHECK(kc_want() == nlane_snd && __builtin_popcount(kc_want()) >= 8, "the files its lanes want: 0x%x (the lanes' "
          "sounds 0x%x)", kc_want(), nlane_snd);
    CHECK(kc_want() == 0x3FFu, "the default lanes: kick rim snare clap hats toms crash ride, files 01 .. 10 (0x%x)",
          kc_want());

    /* ACOUSTIC from its file, then its four treatments: streamed = the whole files, sounding, never dry */
    for (k = 0; k < 5u; k++) {
        play(A, k ? k : 0xFFu, W_WHOLE, -1, 0);
        play(B, k ? k : 0xFFu, W_STREAM, -1, 0);
        for (l = 0, d = 0; l < NOUT; l++)
            d += memcmp(A[l], B[l], sizeof A[l]) != 0;
        for (l = 0, i = 0; l < DRUM_LANES; l++) {
            i += energy(B[l]) > 1000u;
            if (energy(B[l]) <= 1000u)
                printf("  kit %u lane %u (%s): %llu\n", k, l, LANE_NAME_OF(l), (unsigned long long)energy(B[l]));
        }
        CHECK(d == 0 && i == DRUM_LANES && ST[0] == 0 && ST[1] == 0, "kit %s: every lane's hit and all at once streamed "
              "as the whole files (%u renders differ), %u of %u lanes sounding, %u dry, %u with no ring",
              DRUM_KIT_NAMES[k], d, i, DRUM_LANES, ST[0], ST[1]);
        if (k == 0)
            memcpy(A[NOUT - 1u], B[0], sizeof A[0]);   /* (ACOUSTIC's kick, kept) */
        else
            CHECK(memcmp(B[0], A[NOUT - 1u], sizeof B[0]) != 0, "kit %s: the files treated, not ACOUSTIC's kick",
                  DRUM_KIT_NAMES[k]);
    }
    printf("  ACOUSTIC, DEEP, TIGHT, BRIGHT, DUST: every lane from the card, as the whole files\n");
    {   /* at its pitch the kick voice reads the file sample by sample (its source's frames, one an output sample) */
        voice_t *v;
        int32_t x;
        uint32_t ok = 0, n = 0, j;
        for (i = 0; i < SST_N; i++)
            sst[i].on = 0, sst[i].ring = ring_mem + i * SST_K * 512u;
        load_src(0, path[0], 1);
        memset(&drums, 0, sizeof drums);
        drum_on(DLANE_NOTE(0), 110);
        v = &drums.v[0];
        CHECK(v->active && dlc_is(v) && (uint32_t)v->s[5] == sst_src[0].rate, "the kick: a card voice at the file's rate "
              "(step %d)", v->s[5]);
        for (j = 0; j < 2000u && v->active; j++)     /* (the hit's first call reads frame 0: ph[0] starts at -1) */
            if (dlc_next(v)) {
                x = (int16_t)le16(file[0] + sst_src[0].dofs + 2u * j);
                ok += v->s[3] == x;
                n++;
            }
        CHECK(n > 1900u && ok == n, "the kick: frames 0 .. %u the file's (%u of them)", n - 1u, ok);
        drums_off();
    }

    /* a sound not on the card: its lane silent, the others sound */
    play(B, 0xFFu, W_STREAM, KC_MAP[kc_of(LANE_NOTE_OF(1))][3], 0);
    for (l = 0, i = 0; l < DRUM_LANES; l++)
        i += energy(B[l]) > 1000u;
    CHECK(energy(B[1]) == 0 && i == DRUM_LANES - 1u, "the snare not on the card: its lane silent (%llu), %u others sound",
          (unsigned long long)energy(B[1]), i);

    /* the X0X 909: its hats and cymbals from the card */
    {
        uint32_t x9 = DRUM_UID_X909, hats = 0, si;
        memset(&dl, 0, sizeof dl);
        TDRUM->p[P_E0] = (int16_t)x9;
        dl_e0 = TDRUM->p[P_E0];
        CHECK(x9c_want() == 7u, "909: its lanes want hh, crash, ride (0x%x)", x9c_want());
        play(A, x9, W_WHOLE, -1, 0);
        play(B, x9, W_STREAM, -1, 0);
        for (l = 0, d = 0; l < NOUT; l++)
            d += memcmp(A[l], B[l], sizeof A[l]) != 0;
        for (l = 0; l < DRUM_LANES; l++) {
            uint32_t c = x0x_code(x9, l, LANE_NOTE_OF(l)) & 31u;
            if (c >= 7u && c <= 10u)
                hats |= 1u << l;
        }
        for (l = 0, i = 0; l < DRUM_LANES; l++)
            i += (hats >> l & 1u) && energy(B[l]) > 1000u;
        CHECK(d == 0 && i == (uint32_t)__builtin_popcount(hats) && i >= 4u && ST[0] == 0, "909: its hats, crash, ride "
              "(%u lanes) streamed as the whole files (%u renders differ), %u sounding, %u dry", __builtin_popcount(hats), d,
              i, ST[0]);
        play(B, x9, W_STREAM, -1, 1);
        for (l = 0, i = 0, si = 0; l < DRUM_LANES; l++) {
            i += (hats >> l & 1u) && energy(B[l]) != 0;
            si += !(hats >> l & 1u) && energy(B[l]) > 1000u;
        }
        CHECK(i == 0 && si >= 4u, "909 not on the card: its sample voices silent (%u sound), its circuit voices not (%u)",
              i, si);
    }
    printf("sd_kit_card_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
