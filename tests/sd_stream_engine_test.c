/* SPDX-License-Identifier: GPL-3.0-only */
/* SAMPLE's CARD set (engines/sample/eng_sample.c smp_card_*; the streams: storage/sd/sd_stream.h) through the host's
 * render (tests/hostsim.c: the engines, the voices, the mix), the card stood in for by a producer that fills the rings
 * from a RAM image of a WAV between two audio blocks, as the main loop's runs would (the scheduler itself:
 * tests/sd_stream_test.c on the card model):
 *   - a note plays the sample at its pitch (the root C4), an octave up at most (each octave doubles the reads);
 *   - the stream run dry (the producer stopped): the voice fades out at once, no click (its largest sample step no
 *     more than the sine's own), counted once;
 *   - LOOP: a held note loops through the head and the ring;
 *   - the source taken away (a project change): the voice fades out, not counted dry;
 *   - fewer rings than notes: the extra note silent, counted; a released voice's ring free for the next note. */
#define FELUCCA_SD_STREAM 1
#define FELUCCA_SD_RING_KB 8
#define main hostsim_main
#include "hostsim.c"
#undef main

#define DOFS 44u
#define FRAMES 88200u                                /* 2 s, 44.1 kHz mono 16-bit: a sine of 441 Hz */
static uint8_t file[DOFS + FRAMES * 2u + 512u];
static uint8_t ring_mem[SST_N * SST_K * 512u] __attribute__((aligned(4)));
static sst_run_t run1;
static int feed = 1;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void make_file(void)
{
    uint32_t i;
    for (i = 0; i < FRAMES; i++) {
        int16_t v = (int16_t)(20000.0 * sin(2.0 * M_PI * 441.0 * i / 44100.0));
        file[DOFS + 2u * i] = (uint8_t)v;
        file[DOFS + 2u * i + 1u] = (uint8_t)((uint16_t)v >> 8);
    }
    sst_src[0].dofs = DOFS;
    sst_src[0].dlen = FRAMES * 2u;
    sst_src[0].frames = FRAMES;
    sst_src[0].rate = 65536u;
    sst_src[0].bits = SST_F16;
    sst_src[0].chans = 1;
    sst_src[0].fsh = 1;
    sst_src[0].hlen = 4412u;                         /* 50 ms */
    sst_src[0].head = file + DOFS;
    run1.fsec = 0;
    run1.lba = 0;
    run1.n = (DOFS + FRAMES * 2u + 511u) / 512u;
    sst_src[0].run = &run1;
    sst_src[0].nrun = 1;
    sst_src[0].ready = 1;
}

static void produce(void)                            /* the main loop's runs, from the RAM image */
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
        while (feed && st->wr - st->rd < SST_K && st->next <= last) {
            slot = st->wr % SST_K;
            st->tag[slot] = 0;
            memcpy(st->ring + slot * 512u, file + st->next * 512u, 512);
            st->tag[slot] = sst_tag(st->gen, st->next);
            st->wr++;
            if (st->lpend && st->next == (s->dofs + st->lpend - 1u) >> 9)
                st->next = (s->dofs + (st->lpstart > s->hlen ? st->lpstart : s->hlen)) >> 9;
            else
                st->next++;
        }
    }
}

static int32_t L[8u * 44100u];
static uint32_t render(uint32_t frames, uint32_t at)  /* frames from L[at] on; the producer between blocks */
{
    uint32_t f, i;
    int32_t o[2 * CTL];
    for (f = 0; f < frames; f += CTL) {
        produce();
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            L[at + f + i] = o[2 * i];
    }
    return at + frames;
}

static int32_t step_max(uint32_t a, uint32_t b)
{
    int32_t m = 0;
    uint32_t i;
    for (i = a + 1u; i < b; i++)
        if (abs(L[i] - L[i - 1u]) > m)
            m = abs(L[i] - L[i - 1u]);
    return m;
}

static voice_t *card_voice(const track_t *t)
{
    uint32_t v;
    for (v = 0; v < NVOICE; v++)
        if (t->v[v].active && (uint32_t)(t->v[v].s[4] & 0xFF000000) == SMP_STREAM)
            return (voice_t *)&t->v[v];
    return 0;
}

static void sst_rings(uint8_t *mem, uint32_t n)      /* (sd_stream.c's: not in this build) */
{
    uint32_t i;
    for (i = 0; i < SST_N; i++)
        sst[i].on = 0, sst[i].ring = mem && i < n ? mem + i * SST_K * 512u : 0;
}

static void setup(track_t *t)
{
    uint32_t e;
    host_tracks_init();
    for (e = 0; e < NENGINES && strcmp(ENGINES[e]->name, "SAMPLE"); e++)
        ;
    host_preset(t, e, 0);
    t->p[P_E0] = SMP_CARD;
    t->p[P_E1] = t->p[P_E2] = t->p[P_E3] = t->p[P_E6] = 0;
    t->p[P_E4] = 127;
    t->p[P_ATK] = 0;
    t->p[P_SUS] = 127;
    t->p[P_REL] = 20;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_LEVEL] = 100;
    sst_rings(0, 0);
    for (e = 0; e < SST_N; e++)
        sst[e].ring = ring_mem + e * SST_K * 512u;
    sst_part_src[0] = 0;
    sst_dry = sst_nostream = 0;
    feed = 1;
    make_file();
}

#if FELUCCA_SLICE
/* SLICE on CARD (eng_slice.c slc_enc_*, storage/sd/sd_slice.c reads the file into them): four quarter-second tones,
 * 220 / 441 / 882 / 1764 Hz, as one sample in RAM as ADPCM; DIV 4 plays each by its key, forwards and REV; no
 * material placed: silent, never BREAK */
#define SLC_Q 11025u
static uint64_t slc_mem_buf[(SLC_TAB_BYTES + 4u * SLC_Q / 2u + 64u) / 8u];
static int16_t slc_pcm[4u * SLC_Q];

static uint32_t crossings(uint32_t a, uint32_t b)
{
    uint32_t i, n = 0;
    for (i = a + 1u; i < b; i++)
        n += (L[i - 1u] < 0) != (L[i] < 0);
    return n;
}

static void slice_card_tests(track_t *t)
{
    static const uint32_t hz[4] = {220, 441, 882, 1764};
    slc_enc_t e;
    const slc_src_t *s;
    slc_dec_t d;
    uint32_t i, e_n, j, at, eng;
    double err = 0, sig = 0;
    for (i = 0; i < 4u * SLC_Q; i++)
        slc_pcm[i] = (int16_t)(16000.0 * sin(2.0 * M_PI * hz[i / SLC_Q] * (i % SLC_Q) / 44100.0));
    CHECK(slc_enc_bytes(4u * SLC_Q) <= sizeof slc_mem_buf, "the block's size (%u)", slc_enc_bytes(4u * SLC_Q));
    slc_enc_begin(&e, slc_mem_buf, 4u * SLC_Q, 65536u);
    for (i = 0; i < 4u * SLC_Q; i++)
        slc_enc_put(&e, slc_pcm[i]);
    s = slc_enc_end(&e);
    CHECK(s && s->len == 4u * SLC_Q && s->nseg == 1u, "encoded: %u samples", s ? s->len : 0);
    if (!s)
        return;
    d.dat = slc_dat(s);
    slc_dec_at(&d, 0, 0);
    for (i = 0; i < s->len; i++) {
        double x = slc_dec_next(s, &d), y = slc_pcm[i];
        err += (x - y) * (x - y);
        sig += y * y;
    }
    e_n = (uint32_t)(10.0 * log10(sig / (err + 1.0)));
    CHECK(e_n >= 25u, "ADPCM as the voice decodes it: %u dB SNR", e_n);

    host_tracks_init();
    for (eng = 0; eng < NENGINES && strcmp(ENGINES[eng]->name, "SLICE"); eng++)
        ;
    CHECK(eng < NENGINES, "SLICE built");
    for (j = 0; j < 5u; j++) {                        /* 4 slices forward, then slice 3 REV */
        uint32_t k = j < 4u ? j : 3u, want = hz[k] * 2u / 5u, got;   /* (crossings in 0.2 s: two a cycle) */
        host_preset(t, eng, 0);
        t->p[P_E0] = (int16_t)SLC_SRC_CARD;
        t->p[P_E1] = 0;                               /* DIV 4 */
        t->p[P_E2] = t->p[P_E3] = 0;
        t->p[P_E4] = 0;                               /* ONE */
        t->p[P_E5] = j == 4u;
        t->p[P_E6] = t->p[P_E7] = 127;
        t->p[P_ROOT] = 0;
        t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
        slc_crd[0] = s;
        trk_note_on(t, j < 4u ? 60 + (int)j : 59, 100);   /* (59: slice -1, the last) */
        at = render(8820u, 0);
        got = crossings(441u, at);
        CHECK(got + got / 10u >= want && got <= want + want / 10u, "slice %u%s: %u crossings in 0.2 s, %u wanted", k,
              j == 4u ? " REV" : "", got, want);
        trk_note_off(t, j < 4u ? 60 + (int)j : 59);
        render(44100u, 0);
    }
    /* no material placed (the plan refused, the sample not on the card): silent */
    slc_crd[0] = 0;
    trk_note_on(t, 60, 100);
    at = render(8820u, 0);
    for (i = 0, j = 0; i < at; i++)
        j += L[i] != 0;
    CHECK(j == 0u, "CARD with nothing placed: silent, not BREAK (%u frames sounding)", j);
    trk_note_off(t, 60);
}
#endif

int main(void)
{
    track_t *t = &trk[0];
    voice_t *v;
    uint32_t at, ph;
    int32_t calm, after;

    /* a note plays the sample: a second of it at its pitch */
    setup(t);
    trk_note_on(t, 60, 100);
    at = render(44100u, 0);
    v = card_voice(t);
    CHECK(v && v->ph[0] > 44000u && v->ph[0] < 44200u, "a second at the root: %u frames", v ? v->ph[0] : 0);
    calm = step_max(4410u, at);
    CHECK(calm > 100, "it sounds (largest step %d)", calm);
    CHECK(sst_dry == 0, "never dry (%u)", sst_dry);

    /* the card stalls: the ring runs out, the voice fades out at once, no click */
    feed = 0;
    ph = v->ph[0];
    at = render(44100u / 2u, at);
    CHECK(sst_dry == 1, "dry once (%u)", sst_dry);
    CHECK(!card_voice(t), "the voice ended");
    after = step_max(44100u, at);
    CHECK(after <= calm + calm / 10, "no click: the largest step %d, the sine's own %d", after, calm);
    v = &t->v[0];
    (void)ph;

    /* an octave up: twice the frames; two octaves asked: still one (the reads bounded) */
    setup(t);
    t->p[P_E1] = 12;
    trk_note_on(t, 60, 100);
    render(22050u, 0);
    v = card_voice(t);
    CHECK(v && v->ph[0] > 44000u && v->ph[0] < 44200u, "an octave up: %u frames in 0.5 s", v ? v->ph[0] : 0);
    setup(t);
    t->p[P_E1] = 24;
    trk_note_on(t, 60, 100);
    render(11025u, 0);
    v = card_voice(t);
    CHECK(v && v->ph[0] > 21900u && v->ph[0] < 22200u, "two octaves asked: one (%u frames in 0.25 s)", v ? v->ph[0] : 0);

    /* LOOP: held past its end */
    setup(t);
    t->p[P_E3] = 1;
    trk_note_on(t, 60, 100);
    at = render(3u * 44100u, 0);
    v = card_voice(t);
    CHECK(v && v->ph[0] > 44000u && v->ph[0] < 44300u && sst_dry == 0, "looped: 3 s, at frame %u, dry %u",
          v ? v->ph[0] : 0, sst_dry);
    calm = step_max(4410u, at);
    CHECK(calm < 2600, "the loop's seam smooth (largest step %d)", calm);

    /* the source taken away (a project change): fades out, not dry */
    sst_src[0].ready = 0;
    render(4410u, 0);
    CHECK(!card_voice(t) && sst_dry == 0, "let go: faded, not dry (%u)", sst_dry);
    CHECK(!sst[0].on, "its stream given back");

    /* two rings, three notes: the third silent, counted; a released voice's ring free again */
    setup(t);
    sst_rings(ring_mem, 2);
    trk_note_on(t, 60, 100);
    trk_note_on(t, 64, 100);
    trk_note_on(t, 67, 100);
    render(4410u, 0);
    CHECK(sst_nostream == 1, "a third note with no ring: silent (%u)", sst_nostream);
    trk_note_off(t, 60);
    trk_note_off(t, 64);
    trk_note_off(t, 67);
    render(44100u, 0);
    CHECK(!card_voice(t), "released: over");
    trk_note_on(t, 62, 100);
    render(4410u, 0);
    CHECK(card_voice(t) && sst_nostream == 1, "a ring free again after the release (%u)", sst_nostream);

#if FELUCCA_SD_LANES
    {   /* a drum lane on the card sample (drums/lane_card.c): a hit plays it as a one-shot, a second hit cuts the first */
        uint32_t l = 2, i, at, nz = 0;
        int32_t peak = 0;
        trk_note_off(t, 62);
        render(44100u, 0);
        sst_part_src[SST_NPART + l] = 0;
        dl.src[l] = DL_CARD;
        drum_on(DNOTE_LANE | l, 100);
        at = render(22050u, 0);
        for (i = 0; i < at; i++) {
            nz += L[i] != 0;
            peak = abs(L[i]) > peak ? abs(L[i]) : peak;
        }
        CHECK(nz > 20000u && peak > 1000, "a lane on CARD: the hit plays the card sample (%u frames sounding, peak %d)", nz, peak);
        CHECK(step_max(0, at) < 6000, "... no click (the largest step %d)", step_max(0, at));
        drum_on(DNOTE_LANE | l, 100);
        render(4410u, 0);
        CHECK(sst_nostream == 1u, "a second hit: the lane's own stream back for it, no ring short (%u)", sst_nostream);
        render(44100u * 3u, 0);
        for (nz = 0, i = 0; i < 4410u; i++)
            nz += L[i] != 0;
        dl.src[l] = 0;
    }
#endif
#if FELUCCA_SLICE
    slice_card_tests(t);
#endif
    printf("sd_stream_engine_test: %s\n", fails ? "FAIL" : "ok");
    return fails != 0;
}
