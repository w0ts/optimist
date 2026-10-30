/* SPDX-License-Identifier: GPL-3.0-only */
/* The switchable features' memory (firmware/src/core/feat_mem.c, core/arena_bufs.c ARB_BTA): Bluetooth audio's buffers
 * lent by the arena (fm1-base's plat_bta_mem, here a stand-in radio that writes into the block while it runs and for a
 * few passes after its mode goes off, as the device's run is let go; fm1base/optimist/ui/opt_bta_lend.h as the release
 * builds it). Built with FELUCCA_FX_ARENA, FM6 and the delay as arena_bufs_test, under ASan where the compiler has it:
 * a block given back is poisoned by the stand-in (ARN_TOOK unpoisons a block the arena hands out), so the radio
 * touching its block after the arena has it back fails the test.
 *   1. fits: BT AUDIO on, the block placed at the next pass, lent, nothing asked; off: held while the radio lets go
 *      (5 passes), then given back and poisoned; the output bit-exact against a run without the switches (fork)
 *   2. 50 on/off cycles under a held note: nothing leaked (the arena's room as before, every lend taken back), the
 *      output bit-exact against the run without them
 *   3. fits only after a pack (a part sounding in the way): the question "PAUSE nMS?" / "BT AUDIO 18 KB", NO: the
 *      setting off, nothing moved; YES: the transport stopped, the note faded, the arena packed, the block placed, the
 *      transport on again; the pause measured (passes and host time) and its estimate printed
 *   4. does not fit even packed: "UNLOAD?" and the free KB said; NO: off, nothing changed; YES: the unload (the
 *      includer's save and new project; here the parts freed), then placed
 *   5. a part's change refused: BT AUDIO on and idle offered with its size ("BT AUDIO 18 KB"), WI-FI (not lendable)
 *      never; streaming: not offered; YES: off, its block back, the change through; NO: nothing changed
 *   6. the radio writing into its block once given back: an ASan failure (a child process, under ASan only) */
#define FELUCCA_ARRANGER 1
#define FELUCCA_BTA_LEND 1
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include "fmem_fake.h"                                /* the stand-in radio, its lending, the features' table */
#define main hostsim_main
#include "hostsim.c"
#undef main
static struct { char msg[40]; uint32_t msg_t; uint8_t layer, hold_kind; int force; } ui;
static uint8_t settings_later;
static void ui_message(const char *m) { str_cpy(ui.msg, m, sizeof ui.msg); ui.msg_t = 40; }
static void ui_say(const char *a, const char *b) { (void)b; ui_message(a); }
static void str_app(char *d, const char *s, unsigned max)
{
    unsigned n = str_len(d);
    if (n < max)
        str_cpy(d + n, s, max - n);
}
static int project_used(uint32_t i) { return i < 2; }
static void arrangement_apply(uint32_t s) { (void)s; }
static void arrangement_save(void) {}
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
#include "../firmware/src/core/model.c"
#if !FXM_NEED || !FELUCCA_ENG_FM6 || !FMEM_ANY
#error "feat_mem_test: built with FELUCCA_FX_ARENA, FELUCCA_ARENA_BYTES and FM6"
#endif

#include "fmem_fake_post.h"                           /* (the pause's stand-ins, after the firmware) */

static int bad, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { bad++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } else printf("ok   " __VA_ARGS__), printf("\n"); } while (0)

static uint64_t out_h = 1469598103934665603ull;         /* the output's hash (FNV-1a over every sample) */
static uint32_t ms_frames;                              /* (fm1_ms as the blocks advance it: CTL frames at FS each) */
static void blocks(uint32_t n)
{
    static int32_t o[CTL * 2];
    uint32_t i;
    while (n--) {
        ms_frames += CTL;
        fm1_ms += ms_frames / (FS / 1000u);
        ms_frames %= FS / 1000u;
        events_block(CTL);
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            out_h = (out_h ^ (uint32_t)o[i]) * 1099511628211ull;
    }
}
static void pass(void)                                  /* a main loop pass: the arena, the buffers, the radio */
{
    fxm_poll();
    arb_poll();
    fxi_poll();
    radio();
}
static void settle(uint32_t n)
{
    while (n--) {
        pass();
        blocks(2);
    }
    pass();
}
static uint32_t bta_k(void) { return ARB_K(ARB_BTA); }
/* the memory line's width (op_draw.c memline_px of op_project.c info_ram_pct: ram_fill's used = ARN_BYTES - arn_room(),
 * of ARN_BYTES + ARN_UNDO_KEEP), 240 px = 100 % */
static uint32_t ml_px(void)
{
    uint32_t total = ARN_BYTES + ARN_UNDO_KEEP, used = ARN_BYTES - arn_room(), pct = used * 100u / total;
    return (pct < 100u ? pct : 100u) * 12u / 5u;
}
static void *bta_ptr(void) { return arn_ptr(bta_k()); }
/* the arena's layout (every block's place and length): nothing moved, nothing taken */
static uint32_t layout_sum(void)
{
    uint32_t k, h = 17u;
    for (k = 0; k < ARN_PARTS; k++)
        h = h * 31u + arn_cur[k].off * 7u + arn_cur[k].len;
    return h;
}
/* a filler (an inserts' copy client, unused in this build) taking n bytes from the top: the free room shaped */
static uint32_t FILL_K;
static void fill(uint32_t n)
{
    if (arn_cur[FILL_K].len) {
        arn_give(FILL_K);
    }
    if (n && !arn_reserve(FILL_K, n))
        (void)arn_take(FILL_K);
}
/* the silent parts' idle blocks given back (the main loop's arn_give, as a reservation would reclaim them): the
 * room as the free gaps alone */
static void no_idle(void)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (arn_cur[k].len && arn_cur[k].idle) {
            arn_give(k);
            part_mem_p[k] = 0;
        }
}
/* the run of n blocks under a held note on part 1 with BT AUDIO switched every `every` blocks (0: never): its hash */
static uint64_t run_hash(uint32_t n, uint32_t every)
{
    uint32_t i;
    out_h = 1469598103934665603ull;
    trk_note_on(&trk[1], 60, 110);
    for (i = 0; i < n; i++) {
        if (every && i % every == 0u)
            bta_set(!rb.mode);
        blocks(1);
        pass();
    }
    trk_note_off(&trk[1], 60);
    bta_set(0);
    for (i = 0; i < 64u; i++) {
        blocks(1);
        pass();
    }
    return out_h;
}
/* the same run in a child process (the state as it is now, both ways): equal hashes */
static int same_output(uint32_t n, uint32_t every, uint64_t *with)
{
    int fd[2], st;
    uint64_t ref = 0, h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        h = run_hash(n, 0);
        if (write(fd[1], &h, sizeof h) != (ssize_t)sizeof h)
            _exit(2);
        _exit(0);
    }
    h = run_hash(n, every);
    if (read(fd[0], &ref, sizeof ref) != (ssize_t)sizeof ref)
        ref = ~h;
    waitpid(pid, &st, 0);
    close(fd[0]), close(fd[1]);
    *with = h;
    return h == ref;
}
static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static const char *ask(char *a, uint32_t n, uint32_t *f, int *red)
{
    uint32_t i;
    const char *v = 0;
    for (i = 0; i < 4u && !v; i++) {
        settle(1);
        v = fmem_ui(0, a, n, f, red);
    }
    return v;
}

int main(void)
{
    uint32_t FM = arn_round(part_mem_bytes(ENG_IX_FM6)), B = arn_round(RB_BYTES), room0, i, f, moved, lo, hi;
    uint64_t h;
    uint8_t *blk;
    char a[24];
    int red;
    const char *v;
    track_t *t0 = &trk[0], *t1 = &trk[1];
    FILL_K = FXM_CP_K0 + FXM_CPN - 1u;
    host_tracks_init();
    fxi_init();
    for (i = 0; i < NPART; i++)
        set_engine_of(&trk[i], eng_silent_slot());
    set_engine_of(t1, ENG_IX_FM6);
    blocks(64);
    settle(8);
    CHECK(FXM_CPN && FILL_K >= NPART && arn_free_bytes() > 2u * B + 2u * FM,
          "the premise: an arena with room for the block twice (%u B free; the block %u, FM6 %u)",
          (unsigned)arn_free_bytes(), (unsigned)B, (unsigned)FM);
    if (bad)
        return 1;
    /* 1. fits */
    room0 = arn_room();
    {
        uint32_t px0 = ml_px();
        bta_set(1);
        pass();
        CHECK(bta_ptr() && rb.p == bta_ptr() && opt_bta_lent && !fmem.ask && arb_st[ARB_BTA] == FXM_ON,
              "fits: BT AUDIO on, its block placed at the next pass and lent, nothing asked");
        CHECK(ml_px() > px0 && ml_px() - px0 >= B * 240u / (ARN_BYTES + ARN_UNDO_KEEP) - 1u,
              "... the memory line longer by the block at that pass: %u -> %u px (the next frame draws it)", (unsigned)px0,
              (unsigned)ml_px());
    }
    CHECK(!fmem_ui(0, a, sizeof a, &f, &red), "... no question");
    blk = rb.p;
    bta_set(0);
    for (i = 0; i < 4u; i++)
        settle(0);
    CHECK(bta_ptr() && rb.p && rb.refused, "off: the block held while the radio lets go (%u refusals)", (unsigned)rb.refused);
    settle(8);
    CHECK(!bta_ptr() && !rb.p && !opt_bta_lent && arb_st[ARB_BTA] == FXM_NONE && arn_room() == room0 && rb.takes == 1u,
          "... then given back (the arena's room as before)");
    CHECK(fmem_held() == 0u, "... the features hold nothing (INFO's RAM USE: R0)");
    CHECK(blk && POISONED(blk) && POISONED(blk + RB_BYTES - 1u), "... and poisoned (ASan %s): the radio may not touch it",
          HAVE_ASAN ? "on" : "off: not checked");
    CHECK(same_output(256, 0, &h), "the output with nothing switched: the same in two runs (the comparison's premise)");
    CHECK(same_output(256, 64, &h), "BT AUDIO switched on and off under a held note: the output bit-exact");
    /* 2. cycles */
    {
        uint32_t l0 = rb.lends, k0 = rb.takes;
        room0 = arn_room();
        CHECK(same_output(50u * 2u * 8u, 8, &h), "50 on/off cycles under a held note: the output bit-exact");
        settle(16);
        CHECK(arn_room() == room0 && rb.lends - l0 == rb.takes - k0 && rb.lends - l0 >= 50u && !rb.p && !bta_ptr(),
              "... nothing leaked: %u lent, %u taken back, the room as before", (unsigned)(rb.lends - l0),
              (unsigned)(rb.takes - k0));
    }
    /* 3. fits only after a pack: two FM6 parts at the bottom, the lower one idle, the upper one sounding, the free room
     *    above them shaped a unit short of the block */
    for (i = 0; i < NPART; i++)
        set_engine_of(&trk[i], eng_silent_slot());
    blocks(64);
    settle(8);
    no_idle();
    set_engine_of(t0, ENG_IX_FM6);
    set_engine_of(t1, ENG_IX_FM6);
    blocks(8);
    settle(2);
    lo = arn_cur[0].off < arn_cur[1].off ? 0u : 1u, hi = 1u - lo;
    CHECK(arn_cur[lo].len && arn_cur[hi].len && arn_cur[hi].off == arn_cur[lo].off + FM,
          "two FM6 parts side by side (%u, %u)", (unsigned)arn_cur[0].off, (unsigned)arn_cur[1].off);
    fill(arn_free_bytes() - (B - 256u));                /* (the room above them: a unit short of the block) */
    set_engine_of(&trk[lo], eng_silent_slot());         /* (the lower place idle: free, below the sounding part) */
    blocks(64);
    settle(4);
    trk_note_on(&trk[hi], 60, 110);
    blocks(8);
    settle(1);
    CHECK(arn_free_bytes() < B && arn_room() >= B && fmem_how(TF_BTA, &moved) == FMEM_PACK,
          "the free room fragmented: %u B in all, %u the largest, a part sounding in the way: PACK", (unsigned)arn_room(),
          (unsigned)arn_free_bytes());
    {
        uint32_t lay = layout_sum(), px0 = ml_px();
        bta_set(1);
        v = ask(a, sizeof a, &f, &red);
        CHECK(ml_px() == px0, "... while the question waits: the memory line as it was (%u px)", (unsigned)px0);
        CHECK(v && !strncmp(v, "PAUSE ", 6) && !strcmp(a, "BT AUDIO 18 KB") && !red && f == TF_BTA,
              "... asked: \"%s?\" / \"%s\" (%u B to move)", v ? v : "-", a, (unsigned)moved);
        CHECK(!fmem_ui(1, a, sizeof a, &f, &red), "... while it is shown, nothing else");
        (void)fmem_ui(0, a, sizeof a, &f, &red);        /* (let go without YES: NO) */
        settle(4);
        CHECK(!rb.mode && !bta_ptr() && layout_sum() == lay && !paused, "NO: the setting off, nothing moved, no pause");
        bta_set(1);
        v = ask(a, sizeof a, &f, &red);
        song.playing = 1;
        {
            double t0us = now_us();
            uint32_t p = 0, ms0 = fm1_ms;
            fmem.shown = 0;
            fmem_answer(f, 1);                          /* (YES: op_layers.c OA_FMEM) */
            for (p = 0; p < 400u && !bta_ptr(); p++) {
                blocks(1);
                pass();
            }
            CHECK(v && bta_ptr() && paused == 1u && arn_cur[hi].off < FM && fm1_ms - ms0 <= fmem_pause_ms(moved) + 2u,
                  "YES: paused (the voices faded), the arena packed (the part moved down), the block placed: %u ms of "
                  "audio blocks (%u passes, %.0f us of host time); the question said %u ms (%u B moved)",
                  (unsigned)(fm1_ms - ms0), (unsigned)p, now_us() - t0us, (unsigned)fmem_pause_ms(moved),
                  (unsigned)moved);
            CHECK(ml_px() > px0 && fmem_held() == B, "... the memory line longer by the block (%u -> %u px), held %u B",
                  (unsigned)px0, (unsigned)ml_px(), (unsigned)fmem_held());
            settle(2);
            CHECK(resumed == 1u && !fmem.packing, "... the transport on again");
        }
        song.playing = 0;
        trk_note_off(&trk[hi], 60);
        bta_set(0);
        settle(16);
    }
    /* 4. does not fit even packed */
    fill(0);
    settle(2);
    fill(arn_room() > B / 2u ? arn_free_bytes() - B / 2u : 0u);
    settle(2);
    CHECK(arn_room() < B && fmem_how(TF_BTA, &moved) == FMEM_NOFIT, "the room %u B in all: NOFIT", (unsigned)arn_room());
    {
        uint32_t lay = layout_sum();
        bta_set(1);
        v = ask(a, sizeof a, &f, &red);
        CHECK(v && !strcmp(v, "UNLOAD") && red && !strcmp(a, "BT AUDIO 18 KB") && strstr(ui.msg, "KB FREE"),
              "... asked: \"%s?\" / \"%s\", the message \"%s\"", v ? v : "-", a, ui.msg);
        (void)fmem_ui(0, a, sizeof a, &f, &red);
        settle(4);
        CHECK(!rb.mode && !bta_ptr() && layout_sum() == lay && !unloads, "NO: the setting off, nothing changed");
        bta_set(1);
        v = ask(a, sizeof a, &f, &red);
        fmem.shown = 0;
        fmem_answer(f, 1);
        CHECK(unloads == 1u, "YES: the unload asked (the includer's save, then a new project)");
        fill(0);                                        /* (the new project's room: here the filler gone) */
        settle(4);
        CHECK(bta_ptr() && rb.p == bta_ptr(), "... then placed");
        bta_set(0);
        settle(16);
    }
    /* 5. a part's change refused: the idle features offered first */
    for (i = 0; i < NPART; i++)
        set_engine_of(&trk[i], eng_silent_slot());
    blocks(64);
    settle(8);
    no_idle();
    fill(0);
    settle(2);
    bta_set(1);
    settle(2);
    fill(arn_free_bytes() - FM / 2u);                   /* (no room for one FM6 part, but with BT AUDIO's block back) */
    settle(2);
    set_engine_of(t0, ENG_IX_FM6);
    blocks(8);
    settle(2);
    CHECK(part_mem_refused.on && t0->engine != ENG_IX_FM6, "FM6 on part 1: refused (no room)");
    fmem_offer_reset();
    rb.streaming = 1;
    CHECK(!fmem_offer(), "BT AUDIO streaming: not offered (in use); WI-FI idle but not lendable: never");
    fmem_offer_reset();
    rb.streaming = 0;
    {
        uint32_t m = fmem_offer(), b;
        fmem_list(a, sizeof a, m);
        CHECK(m == 1u << TF_BTA && !strcmp(a, "BT AUDIO 18 KB") && fmem_idle_mask(&b) == m && b == RB_BYTES,
              "idle: offered alone with its size: \"OFF?\" / \"%s\"", a);
        CHECK(!fmem_offer(), "... once per refusal");
        fmem_off(m);                                    /* (YES: op_layers.c OA_FMOFF) */
        part_mem_refused.waiting |= MEM_W_FX;
    }
    for (i = 0; i < 64u && t0->engine != ENG_IX_FM6; i++) {
        if (mem_retry() > 0)
            break;
        blocks(1);
        settle(0);
    }
    blocks(8);
    settle(2);
    CHECK(!rb.mode && !rb.p && t0->engine == ENG_IX_FM6 && !part_mem_refused.on, "YES: BT AUDIO off, its block back, FM6 placed");
    /* 6. the radio touching its block once the arena has it back */
#if HAVE_ASAN
    {
        int st;
        pid_t pid;
        uint8_t *gone;
        set_engine_of(t0, eng_silent_slot());
        blocks(64);
        settle(4);
        no_idle();
        fill(0);
        settle(2);
        bta_set(1);
        settle(2);
        gone = rb.p;
        bta_set(0);
        settle(16);
        fflush(stdout);
        pid = fork();
        if (pid == 0) {
            freopen("/dev/null", "w", stderr);
            gone[3]++;                                  /* (the bug this guards against: a write after the take-back) */
            _exit(0);
        }
        waitpid(pid, &st, 0);
        CHECK(gone && !rb.p && !(WIFEXITED(st) && WEXITSTATUS(st) == 0), "a write into the block given back: ASan fails it");
    }
#else
    printf("skip the use-after-give-back probe: no ASan in this compiler\n");
#endif
    printf("%d checks, %d failed\n", checks, bad);
    return bad != 0;
}
