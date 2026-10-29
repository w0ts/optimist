/* SPDX-License-Identifier: GPL-3.0-only */
/* SD card mode's sample sets on the card (the user, 2026-10-29: a ticked set never grows the flash; tools/gen_samples.py
 * --card writes them, eng_sample.c smp_bank_on plays them through the streams) in the host's render (tests/hostsim.c):
 * every set's factory presets played from the card's OPTIMIST/SAMPLES/<SET>/Z<k>.WAV (read from DIR as gen_samples.py
 * wrote them, each zone a source, the rings filled between two audio blocks as the main loop's runs would) against the
 * very same presets on the flash's set (this host build has every set in flash: SMP_CARD_SET is the test's switch):
 *   - each zone's keys ("inst"), root and loop ("smpl") as the flash zone's;
 *   - a phrase on each set's presets (every zone, a semitone round each zone's root, an octave and a half up, two down, a held
 *     note through the loop, LOOP off, BITS, CUT, DRV, TUNE), bit-identical to the flash render, never dry;
 *   - a set not on the card (no zones placed): silent.
 *   sd_sets_test DIR */
#define FELUCCA_SD_STREAM 1
#define FELUCCA_SD_RING_KB 8
static unsigned card_mask;                           /* the sets the test plays from the card */
#define SMP_CARD_SET(s) ((card_mask >> (s)) & 1u)
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t *file[SST_NSRC];
static uint32_t fsize[SST_NSRC];
static sst_run_t run1[SST_NSRC];
static uint8_t ring_mem[SST_N * SST_K * 512u] __attribute__((aligned(4)));

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

/* the WAV as sd_stream.c sst_wav and sst_src_load take it: fmt, data, smpl (unity, first loop), inst (lo, hi); a
 * 50 ms head; one run */
static int load_src(uint32_t si, const char *path)
{
    FILE *f = fopen(path, "rb");
    sst_src_t *s = &sst_src[si];
    uint32_t at = 12, n, rate = 0, unity = 60;
    uint64_t hb;
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
        if (!memcmp(c, "fmt ", 4)) {
            CHECK(le16(c + 8) == 1u && le16(c + 10) == 1u && le16(c + 22) == 16u, "%s: 16-bit mono PCM", path);
            rate = le32(c + 12);
        } else if (!memcmp(c, "data", 4)) {
            s->dofs = at + 8u;
            s->dlen = n;
        } else if (!memcmp(c, "smpl", 4)) {
            unity = le32(c + 8 + 12);
            if (le32(c + 8 + 28)) {
                s->lps = le32(c + 8 + 36 + 8);
                s->lpe = le32(c + 8 + 36 + 12);
            }
        } else if (!memcmp(c, "inst", 4)) {
            s->lo = c[8 + 3];
            s->hi = c[8 + 4];
        }
        at += 8u + n + (n & 1u);
    }
    s->bits = SST_F16;
    s->chans = 1;
    s->fsh = 1;
    s->frames = s->dlen / 2u;
    s->rate = (uint32_t)(((uint64_t)rate << 16) / 44100u);
    s->root = (int8_t)((int32_t)unity - 60);
    hb = (uint64_t)50u * s->rate * 441u / (10u * 65536u);
    s->hlen = (uint32_t)((hb << 1) + 3u) & ~3u;
    if (s->hlen > s->dlen)
        s->hlen = s->dlen & ~3u;
    s->head = file[si] + s->dofs;
    run1[si].fsec = 0;
    run1[si].lba = 0;
    run1[si].n = (fsize[si] + 511u) / 512u;
    s->run = &run1[si];
    s->nrun = 1;
    s->ready = 1;
    return (s->dofs & 1u) == 0 && s->frames > 0;
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
            if (st->lpend && st->next == (s->dofs + st->lpend - 1u) >> 9)
                st->next = (s->dofs + (st->lpstart > s->hlen ? st->lpstart : s->hlen)) >> 9;
            else
                st->next++;
        }
    }
}

#define PHRASE (12u * 44100u)
static int32_t *A, *B;                               /* (shared with the forked renders: mmap) */
static uint32_t *stats;

static void render(int32_t *L, uint32_t at, uint32_t frames)
{
    uint32_t f, i;
    int32_t o[2 * CTL];
    for (f = 0; f < frames; f += CTL) {
        produce();
        mix_block(o, CTL);
        for (i = 0; i < CTL && at + f + i < PHRASE; i++)
            L[at + f + i] = o[2 * i];
    }
}

static uint32_t sample_eng(void)
{
    uint32_t e;
    for (e = 0; e < NENGINES && strcmp(ENGINES[e]->name, "SAMPLE"); e++)
        ;
    return e;
}

/* the phrase: each zone's root, a semitone round it, two octaves down and up, a chord, a note held 1.5 s */
static uint32_t phrase_notes(uint32_t set, int *notes)
{
    const smp_set_t *ss = &SMP_SETS[set];
    uint32_t z, n = 0;
    for (z = 0; z < ss->nz && n + 6u < 40u; z++) {
        int r = SMP_ZONES[ss->z0 + z].root16 / 16;
        notes[n++] = r;
        notes[n++] = r - 1 >= 0 ? r - 1 : r;
        notes[n++] = r + 1 <= 127 ? r + 1 : r;
        notes[n++] = r - 24 >= 0 ? r - 24 : r;
        notes[n++] = r + 17 <= 127 ? r + 17 : r;   /* (the card set's cap two octaves up, the flash's three) */
    }
    return n;
}

static void play(int32_t *L, uint32_t set, uint32_t pi, int card, int variant)
{
    track_t *t = &trk[0];
    int notes[40];
    uint32_t n = phrase_notes(set, notes), k, at = 0, e = sample_eng();
    memset(L, 0, PHRASE * sizeof *L);
    card_mask = card ? 1u << set : 0u;
    host_tracks_init();
    host_preset(t, e, pi);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;   /* (the voice alone: no FX's state across runs) */
    if (variant == 1) {
        t->p[P_E3] = 0;                              /* LOOP off */
        t->p[P_E2] = 40;                             /* BITS */
    } else if (variant == 2) {
        t->p[P_E1] = 5;                              /* TUNE */
        t->p[P_E4] = 60;                             /* CUT */
        t->p[P_E6] = 90;                             /* DRV */
    }
    for (k = 0; k < SST_N; k++) {
        sst[k].on = 0;
        sst[k].ring = ring_mem + k * SST_K * 512u;
    }
    sst_dry = sst_nostream = 0;
    for (k = 0; k < n && at + 13230u < PHRASE; k++) {   /* 0.2 s on, 0.1 s off each */
        trk_note_on(t, notes[k], 100);
        render(L, at, 8820u);
        trk_note_off(t, notes[k]);
        render(L, at + 8820u, 4410u);
        at += 13230u;
    }
    if (at + 3u * 44100u <= PHRASE) {                /* a chord, then a held note through the loop */
        int r = SMP_ZONES[SMP_SETS[set].z0].root16 / 16;
        trk_note_on(t, r, 90);
        trk_note_on(t, r + 4, 90);
        trk_note_on(t, r + 7, 90);
        render(L, at, 22050u);
        trk_note_off(t, r);
        trk_note_off(t, r + 4);
        trk_note_off(t, r + 7);
        at += 22050u;
        trk_note_on(t, r + 12, 100);
        render(L, at, 66150u);
        trk_note_off(t, r + 12);
        render(L, at + 66150u, 22050u);
    }
}

/* each render forked from the same state (the voices, the LFOs, every engine's and generator's state as they are
 * here): the two paths compared from one start, the parent untouched */
static void play_fork(int32_t *L, uint32_t set, uint32_t pi, int card, int variant)
{
    pid_t pid = fork();
    int st;
    if (pid == 0) {
        play(L, set, pi, card, variant);
        stats[0] = sst_dry;
        stats[1] = sst_nostream;
        _exit(0);
    }
    waitpid(pid, &st, 0);
    CHECK(pid > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0, "the render ran (set %u preset %u)", set, pi);
}

int main(int argc, char **argv)
{
    static const char *const SETS[] = {"PIANO", "BASS", "VIBES", "HORNS", "STRGS", "FLUTE", "SCRCH"};
    uint32_t e = sample_eng(), si, set, z, pi, played = 0, k;
    char path[512];
    if (argc != 2) {
        printf("usage: sd_sets_test DIR (gen_samples.py --card DIR)\n");
        return 2;
    }
    CHECK(e < NENGINES, "SAMPLE built");
    A = mmap(0, 2u * PHRASE * sizeof *A + 64u, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (A == MAP_FAILED)
        return 2;
    B = A + PHRASE;
    stats = (uint32_t *)(B + PHRASE);
    for (si = 0; si < sizeof SETS / sizeof SETS[0]; si++) {
        for (set = 0; set < SMP_NSETS && strcmp(SMP_SETS[set].name, SETS[si]); set++)
            ;
        CHECK(set < SMP_NSETS && SMP_SETS[set].nz, "%s in this host build's flash", SETS[si]);
        if (set >= SMP_NSETS || !SMP_SETS[set].nz)
            continue;
        /* the card's zones, one source each, in key order: as the flash zones */
        for (z = 0; z < SMP_SETS[set].nz; z++) {
            const smp_zone_t *fz = &SMP_ZONES[SMP_SETS[set].z0 + z];
            snprintf(path, sizeof path, "%s/OPTIMIST/SAMPLES/%s/Z%u.WAV", argv[1], SETS[si], z);
            CHECK(load_src(z, path), "%s read", path);
            CHECK(sst_src[z].lo == fz->lo && sst_src[z].hi == fz->hi && (sst_src[z].root + 60) * 16 == fz->root16 &&
                      sst_src[z].frames == fz->n && sst_src[z].rate == fz->rate &&
                      (fz->looped ? sst_src[z].lpe == fz->le - 1u && sst_src[z].lps == fz->ls - 1u : !sst_src[z].lpe),
                  "%s Z%u: keys %u..%u root %d loop %u..%u, the flash's %u..%u %d %u..%u", SETS[si], z, sst_src[z].lo,
                  sst_src[z].hi, sst_src[z].root + 60, sst_src[z].lps, sst_src[z].lpe, fz->lo, fz->hi, fz->root16 / 16,
                  fz->looped ? fz->ls : 0u, fz->looped ? fz->le : 0u);
        }
        snprintf(path, sizeof path, "%s/OPTIMIST/SAMPLES/%s/Z%u.WAV", argv[1], SETS[si], z);
        CHECK(!fopen(path, "rb"), "%s: no zone past the set's", path);
        sst_bank_src[0] = 0;
        sst_bank_n[0] = (uint8_t)SMP_SETS[set].nz;
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            uint32_t var, diff, first, sounding;
            if ((uint32_t)ENGINES[e]->presets[pi].e[0] != set)
                continue;
            for (var = 0; var < 3u; var++) {
                play_fork(A, set, pi, 0, (int)var);
                play_fork(B, set, pi, 1, (int)var);
                for (k = 0, diff = 0, first = PHRASE, sounding = 0; k < PHRASE; k++) {
                    if (A[k] != B[k] && !diff++)
                        first = k;
                    sounding += A[k] != 0;
                }
                CHECK(diff == 0 && sounding > 44100u && stats[0] == 0 && stats[1] == 0,
                      "%s %s (variant %u): the card's render the flash's: %u samples differ (first at %u: %d / %d), "
                      "%u sounding, dry %u, no ring %u", SETS[si], ENGINES[e]->presets[pi].name, var, diff, first,
                      first < PHRASE ? A[first] : 0, first < PHRASE ? B[first] : 0, sounding, stats[0], stats[1]);
            }
            played++;
        }
        /* not on the card (the plan placed no zone): silent */
        sst_bank_n[0] = 0;
        for (pi = 0; pi < ENGINES[e]->npresets && (uint32_t)ENGINES[e]->presets[pi].e[0] != set; pi++)
            ;
        play_fork(B, set, pi, 1, 0);
        for (k = 0, z = 0; k < PHRASE; k++)
            z += B[k] != 0;
        CHECK(z == 0, "%s not on the card: silent (%u samples sounding)", SETS[si], z);
    }
    CHECK(played >= 10u, "every set's presets played (%u)", played);
    printf("%s: %u presets on 7 card sets, bit-identical to the flash's\n", fails ? "FAIL" : "ok", played);
    return fails ? 1 : 0;
}
