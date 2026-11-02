/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen mirrored on 10 pages captured from the FM-1 (2026-11-01, fm1-base's UI: its text anti-aliased; the fixture
 * ios/OptimistMirror/Tests/Fixtures/mirror_pages.sysex, each page's catch-up as it came over USB, the pages in name order): the
 * firmware's encoder (io/editor/ed_mirror_enc.h) over the same writes
 *   without the AA token: the very bytes the FM-1 sent (the encoder is the device's)
 *   with it (opti_miraa_1): decoded (mirror_dec.h) to the same pixels, page by page; the bytes per page old -> new, and the
 *   encoder's host time a pixel
 * The AA stream is the clients' fixture beside it (mirror_pages_aa.sysex: tools/fm1_mirror.py, the editor and the app decode it),
 * checked to be this encoder's; MIRROR_PAGES_UPDATE=1 writes it after an intended change of the encoder.
 *   mirror_pages_test [FIXTURE_DIR]  (default ios/OptimistMirror/Tests/Fixtures) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails, checks;
#define check(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define IN_POOL
static volatile uint32_t fm1_ms;
enum { NPAGE = 10, MAXR = 512, OUTN = 1u << 20 };
static const char *const PAGE[NPAGE] = {"global", "home_mix", "master_fx", "project", "scope", "song", "sound_edit", "sound_fx",
                                        "steps", "system"};
static uint8_t out[OUTN];               /* the encoder's frames */
static uint32_t on;
static int mir_wire(uint32_t b)
{
    if (on < OUTN)
        out[on++] = (uint8_t)b;
    return 1;
}
static void mir_wire_end(void) {}
#include "../firmware/src/io/editor/ed_mirror_enc.h"
#include "mirror_dec.h"

static struct { uint8_t fill; uint16_t x, y, w, h, c; uint32_t at; uint32_t page; } rc[MAXR];   /* every write; at: its pixels */
static uint16_t rpx[1u << 20];          /* the BLITs' pixels, byte-swapped as the panel takes them */
static uint32_t nr, npx, page_of_byte[NPAGE + 1];
static uint16_t shot[NPAGE][240 * 240]; /* each page as its own frames decode */

static uint16_t sw16(uint32_t c) { return (uint16_t)((c >> 8 | c << 8) & 0xFFFFu); }
static void take_blit(void)             /* the BLIT just decoded: its pixels out of the screen */
{
    uint32_t j, i;
    if (!nr || rc[nr - 1u].fill)
        return;
    for (j = 0; j < rc[nr - 1u].h; j++)
        for (i = 0; i < rc[nr - 1u].w; i++)
            rpx[rc[nr - 1u].at + j * rc[nr - 1u].w + i] = sw16(md_fb[(rc[nr - 1u].y + j) * 240u + rc[nr - 1u].x + i]);
}
/* each frame of buf (F0 .. F7) to fn */
static uint32_t frames_of(const uint8_t *buf, uint32_t n, void (*fn)(const uint8_t *, uint32_t))
{
    uint32_t i, s = 0, k = 0;
    for (i = 0; i < n; i++) {
        if (buf[i] == 0xF0u)
            s = i;
        if (buf[i] == 0xF7u)
            fn(buf + s, i - s + 1u), k++;
    }
    return k;
}
static uint32_t page;
static uint32_t bad;
static void read_frame(const uint8_t *f, uint32_t n)
{
    uint32_t op = f[5];
    if (op <= 1u) {
        take_blit();
        if (op == 0u && md_2(f + 6) == 0u && md_2(f + 8) == 0u && md_2(f + 10) == 240u && md_2(f + 12) == 240u && nr) {
            memcpy(shot[page], md_fb, sizeof md_fb);
            page++;
        }
        check(nr < MAXR, "writes");
        rc[nr].fill = op == 0u, rc[nr].x = (uint16_t)md_2(f + 6), rc[nr].y = (uint16_t)md_2(f + 8);
        rc[nr].w = (uint16_t)md_2(f + 10), rc[nr].h = (uint16_t)md_2(f + 12), rc[nr].at = npx, rc[nr].page = page;
        if (op == 0u)
            rc[nr].c = (uint16_t)(md_2(f + 14) | (uint32_t)f[16] << 14);
        else
            npx += (uint32_t)rc[nr].w * rc[nr].h;
        nr++;
    }
    bad += !md_frame(f, n);
}
static void dec_frame(const uint8_t *f, uint32_t n)
{
    if (f[5] == 0u && md_2(f + 6) == 0u && md_2(f + 10) == 240u && md_2(f + 12) == 240u && page_of_byte[NPAGE]) {
        memcpy(shot[page], md_fb, sizeof md_fb);
        page++;
    }
    page_of_byte[NPAGE] = 1;
    bad += !md_frame(f, n);
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
/* every write through the encoder (aa: with the token); the bytes per page in by[] */
static void encode(int aa, uint32_t *by)
{
    uint32_t k, p0;
    on = 0;
    me.aa = (uint8_t)aa;
    for (k = 0; k < nr; k++) {
        p0 = on;
        if (rc[k].fill)
            mir_enc_fill(rc[k].x, rc[k].y, rc[k].w, rc[k].h, rc[k].c);
        else
            mir_enc_blit(rc[k].x, rc[k].y, rc[k].w, rc[k].h, rpx + rc[k].at);
        by[rc[k].page] += on - p0;
    }
}
int main(int argc, char **argv)
{
    static uint8_t in[1u << 20];
    static uint16_t want[NPAGE][240 * 240];
    const char *dir = argc > 1 ? argv[1] : "ios/OptimistMirror/Tests/Fixtures";
    char path[512], apath[512];
    FILE *fp;
    snprintf(path, sizeof path, "%s/mirror_pages.sysex", dir);
    snprintf(apath, sizeof apath, "%s/mirror_pages_aa.sysex", dir);
    fp = fopen(path, "rb");
    uint32_t n, k, p, old[NPAGE] = {0}, aa[NPAGE] = {0}, to = 0, ta = 0, diff, nf, max_frame = 0, s;
    double t0, t1, t2;
    if (!fp) {
        printf("FAIL no fixture %s\n", path);
        return 1;
    }
    n = (uint32_t)fread(in, 1, sizeof in, fp);
    fclose(fp);
    /* the captured frames: the writes, and each page as decoded */
    nf = frames_of(in, n, read_frame);
    take_blit();
    memcpy(shot[page], md_fb, sizeof md_fb);
    check(page == NPAGE - 1u && !bad, "the fixture: %u pages, %u frames bad", page + 1u, bad);
    memcpy(want, shot, sizeof want);
    /* without the token: the bytes the FM-1 sent */
    encode(0, old);
    check(on == n && !memcmp(out, in, n), "the encoder without the AA token gives the FM-1's bytes (%u frames, %u B, got %u B)", nf, n, on);
    /* with it: the same pixels, page by page */
    encode(1, aa);
    memset(md_fb, 0, sizeof md_fb), memset(&md, 0, sizeof md), memset(shot, 0, sizeof shot);
    page = 0, bad = 0, page_of_byte[NPAGE] = 0;
    nf = frames_of(out, on, dec_frame);
    memcpy(shot[page], md_fb, sizeof md_fb);
    for (k = s = 0; k < on; k++) {
        if (out[k] == 0xF0u)
            s = k;
        if (out[k] == 0xF7u && k - s + 1u > max_frame)
            max_frame = k - s + 1u;
    }
    check(page == NPAGE - 1u && !bad, "the AA stream: %u pages, %u frames bad", page + 1u, bad);
    check(max_frame <= MIR_CUT + 24u, "a frame of %u bytes", max_frame);
    for (p = 0; p < NPAGE; p++) {
        for (diff = 0, k = 0; k < 240u * 240u; k++)
            diff += shot[p][k] != want[p][k];
        check(!diff, "%s: %u pixels differ", PAGE[p], diff);
        printf("  %-10s %6u B -> %6u B  %5.1f %%\n", PAGE[p], old[p], aa[p], 100.0 * aa[p] / old[p]);
        to += old[p], ta += aa[p];
    }
    printf("  %-10s %6.2f KB -> %5.2f KB  %5.1f %% (mean a page)\n", "mean", to / 1000.0 / NPAGE, ta / 1000.0 / NPAGE, 100.0 * ta / to);
    if (getenv("MIRROR_PAGES_UPDATE")) {
        FILE *o = fopen(apath, "wb");
        check(o && fwrite(out, 1, on, o) == on, "%s written", apath);
        if (o)
            fclose(o);
    } else {                            /* (the clients' fixture: this encoder's bytes) */
        FILE *o = fopen(apath, "rb");
        uint32_t m = o ? (uint32_t)fread(in, 1, sizeof in, o) : 0u;
        if (o)
            fclose(o);
        check(m == on && !memcmp(in, out, on), "%s: this encoder's AA stream (MIRROR_PAGES_UPDATE=1 after an intended change)", apath);
    }
    {   /* the encoder's time on the host, a pixel (both ways) */
        uint32_t px = 0, r, sink[NPAGE], reps = getenv("MIRROR_PAGES_REPS") ? (uint32_t)atoi(getenv("MIRROR_PAGES_REPS")) : 20u;
        for (k = 0; k < nr; k++)
            px += rc[k].fill ? 0u : (uint32_t)rc[k].w * rc[k].h;
        t0 = now();
        for (r = 0; r < reps; r++)
            memset(sink, 0, sizeof sink), encode(0, sink);
        t1 = now();
        for (r = 0; r < reps; r++)
            memset(sink, 0, sizeof sink), encode(1, sink);
        t2 = now();
        printf("  encoder on the host: %.2f ns a pixel without the AA token, %.2f with (%u px)\n", (t1 - t0) * 1e9 / (px * (double)reps),
               (t2 - t1) * 1e9 / (px * (double)reps), px);
    }
    printf("mirror_pages_test: %d of %d checks failed\n", fails, checks);
    return fails != 0;
}
