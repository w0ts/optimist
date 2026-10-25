/* SPDX-License-Identifier: GPL-3.0-only */
/* The RAMTEXT overlays' protocol on the host (core/overlay.c, docs/SD-MODE-DESIGN.md 6): build with
 * -DFELUCCA_OVERLAY=1. The host has no sections: each engine's hooks' table is "copied" into a slot of RAM as the
 * target copies its code, and the render reads the hooks from the slot. Checks:
 *   1. a part whose engine is not in its slot plays nothing, never calls into the slot (a trap table there), and asks
 *      for its engine; the main loop's rto_poll loads it and the part plays;
 *   2. every overlay engine renders the same samples from its slot as from its table in place (rto_host_resident);
 *   3. an engine change while notes sound: the fade, the switch, the new engine asked for, its notes after the load;
 *   4. random engine changes on the three parts, the main loop polling at random: a slot's table always names the
 *      engine the slot says it holds, no part ever calls a slot that does not hold its engine.
 *   overlay_test */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static int fails;
static void ok(const char *what, int c)
{
    printf("overlay: %-100s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

static int trapped;
static void trap_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t, (void)v, (void)out, (void)n, (void)m;
    trapped++;
}
static void trap_block(track_t *t) { (void)t; trapped++; }
static void trap_post(track_t *t, int32_t *out, uint32_t n, uint32_t nr) { (void)t, (void)out, (void)n, (void)nr; trapped++; }

static int32_t blk[2 * CTL];
static double render(uint32_t nblk)                     /* nblk blocks: their energy */
{
    double e = 0;
    uint32_t b, i;
    for (b = 0; b < nblk; b++) {
        mix_block(blk, CTL);
        for (i = 0; i < 2u * CTL; i++)
            e += (double)blk[i] * blk[i];
    }
    return e;
}

static void reset(int resident)
{
    host_tracks_init();
    rto_host_resident = (uint8_t)resident;
    (void)rto_init();
}

/* the engines with overlay code in this build (their hooks' tables) */
static uint32_t ovl_engines(uint32_t *e)
{
    uint32_t i, n = 0;
    for (i = 0; i < sizeof RTO_HOST / sizeof RTO_HOST[0]; i++)
        e[n++] = RTO_HOST[i]->tag & 0xFFu;
    return n;
}

static void test_not_loaded(void)
{
    track_t *t = &trk[0];
    rto_hdr_t *slot = (rto_hdr_t *)(void *)rto_slot0;
    reset(0);
    host_preset_req(t, ENG_SLOT_PHASE, 0);
    t->engine = (uint8_t)ENG_SLOT_PHASE;               /* (an engine set without the main loop: a project's load) */
    slot->tag = RTO_TAG(ENG_SLOT_PHASE), slot->render = trap_render, slot->block = trap_block, slot->amp = 0,
    slot->post = trap_post;                            /* (what a half-made copy could hold) */
    trk_note_on(t, 60, 100);
    trapped = 0;
    ok("1. not in its slot: the part plays nothing", render(20) == 0.0);
    ok("1. ... never calls into the slot (a trap table there)", trapped == 0);
    ok("1. ... and asks for its engine (rto_want)", rto_want[0] == ENG_SLOT_PHASE + 1u && rto_eng[0] == 0);
    rto_poll();
    ok("1. the main loop's rto_poll loads it", rto_eng[0] == ENG_SLOT_PHASE + 1u && slot->render != trap_render);
    ok("1. ... the part plays (its note waited in its voice)", render(20) > 0.0 && trapped == 0);
}

/* one engine's phrase on the three parts (k 0: from the tables in place; 1: from the slots), in a child process
 * so both start from the same state: its samples through fd */
#define SAME_BLK 200u
static void same_run(uint32_t e, uint32_t k, int fd)
{
    static int32_t a[SAME_BLK * 2 * CTL];
    uint32_t j, q;
    reset(k == 0);
    for (j = 0; j < NPART; j++)
        host_preset(&trk[j], e, j);
    for (j = 0; j < NPART; j++)
        trk_note_on(&trk[j], 48 + 7 * j, 100);
    for (j = 0; j < SAME_BLK; j++) {
        mix_block(a + j * 2u * CTL, CTL);
        if (j == SAME_BLK / 2u)
            for (q = 0; q < NPART; q++)
                trk_note_off(&trk[q], 48 + 7 * q);
    }
    if (k && rto_eng[0] != e + 1u)
        a[0] ^= 1;                                     /* (not from its slot: counts as different) */
    for (j = 0; j < sizeof a; ) {
        ssize_t w = write(fd, (const char *)a + j, sizeof a - j);
        if (w <= 0)
            _exit(2);
        j += (uint32_t)w;
    }
    _exit(0);
}
static int same_get(uint32_t e, uint32_t k, int32_t *a, uint32_t bytes)
{
    int fd[2], st;
    uint32_t got = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (pid == 0) {
        close(fd[0]);
        same_run(e, k, fd[1]);
    }
    close(fd[1]);
    while (got < bytes) {
        ssize_t r = read(fd[0], (char *)a + got, bytes - got);
        if (r <= 0)
            break;
        got += (uint32_t)r;
    }
    close(fd[0]);
    waitpid(pid, &st, 0);
    return got == bytes && WIFEXITED(st) && !WEXITSTATUS(st);
}
static void test_same(void)
{
    static int32_t a[SAME_BLK * 2 * CTL], b[SAME_BLK * 2 * CTL];
    uint32_t e[32], n = ovl_engines(e), i, j, same = 1, sounding = 1;
    for (i = 0; i < n; i++) {
        double s = 0;
        if (!same_get(e[i], 0, a, sizeof a) || !same_get(e[i], 1, b, sizeof b) || memcmp(a, b, sizeof a)) {
            printf("overlay: %s differs from its slot\n", ENGINES[e[i]]->name);
            same = 0;
        }
        for (j = 0; j < SAME_BLK * 2u * CTL; j++)
            s += (double)a[j] * a[j];
        sounding &= s > 0;
    }
    printf("overlay: the overlay engines of this build:");
    for (i = 0; i < n; i++)
        printf(" %s", ENGINES[e[i]]->name);
    printf("\n");
    ok("2. every overlay engine: the same samples from its slot as from its table in place, on the three parts", same);
    ok("2. ... and each of them sounds", sounding && n > 0);
}

static void test_switch(void)
{
    track_t *t = &trk[1];
    uint32_t i;
    reset(0);
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], ENG_SLOT_PHASE, 0);
    trk_note_on(t, 60, 100);
    (void)render(30);
    host_preset_req(t, ENG_SLOT_DIGITAL, 0);           /* (the UI's switch: eng_req, the fade, then the switch) */
    trk_note_on(t, 64, 100);                           /* (during the fade: played on the new engine) */
    for (i = 0; i < 200u && t->engine != ENG_SLOT_DIGITAL; i++)
        (void)render(1);
    ok("3. an engine change while a note sounds: the fade, then the switch", t->engine == ENG_SLOT_DIGITAL);
    (void)render(1);
    ok("3. ... the new engine asked for, the slot left alone until the main loop", rto_want[1] == ENG_SLOT_DIGITAL + 1u &&
       rto_eng[1] == ENG_SLOT_PHASE + 1u);
    (void)render(5);                                   /* (the main loop busy for five blocks) */
    ok("3. ... still asked for, nothing called in its slot meanwhile", rto_eng[1] == ENG_SLOT_PHASE + 1u);
    rto_poll();
    ok("3. ... loaded by rto_poll; its note plays after the load", rto_eng[1] == ENG_SLOT_DIGITAL + 1u && render(20) > 0.0);
    ok("3. ... the other parts' slots untouched", rto_eng[0] == ENG_SLOT_PHASE + 1u && rto_eng[2] == ENG_SLOT_PHASE + 1u);
}

static uint32_t rnd_s = 12345;
static uint32_t rnd(uint32_t n)
{
    rnd_s = rnd_s * 1103515245u + 12345u;
    return (rnd_s >> 16) % n;
}

static void test_random(void)
{
    uint32_t e[32], n = ovl_engines(e), cand[40], nc = 0, b, k, bad = 0, loads0, plays = 0;
    for (k = 0; k < n; k++)
        cand[nc++] = e[k];
    for (k = 0; k < NENGINES && nc < 40u; k++)       /* (and engines of no overlay: a slot left as it is) */
        if (ENGINES[k]->render)
            cand[nc++] = k;
    reset(0);
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], cand[rnd(nc)], 0);
    loads0 = rto_st.loads;
    for (b = 0; b < 30000u; b++) {
        k = rnd(NPART);
        if (rnd(40) == 0)
            host_preset_req(&trk[k], cand[rnd(nc)], rnd(4));
        if (rnd(6) == 0)
            trk_note_on(&trk[k], 36 + rnd(48), 100);
        if (rnd(6) == 0)
            trk_note_off(&trk[k], 36 + rnd(48));
        mix_block(blk, CTL);
        if (rnd(3) == 0)                               /* (the main loop: now and then) */
            rto_poll();
        for (k = 0; k < NPART; k++) {
            const rto_hdr_t *h = (const rto_hdr_t *)(const void *)(rto_slot0 + k * rto_slot_len);
            if (rto_eng[k] && h->tag != RTO_TAG(rto_eng[k] - 1u))
                bad++;
            if (rto_eng[k] && rto_eng[k] == trk[k].engine + 1u)
                plays++;
        }
    }
    ok("4. 30000 blocks of random engine changes: a slot's table always names the engine it holds", bad == 0);
    ok("4. ... the slots loaded again and again, the parts playing from them", rto_st.loads - loads0 > 100u && plays > 10000u);
    ok("4. ... no table refused, no copy refused", rto_st.bad == 0);
}

int main(void)
{
    test_not_loaded();
    test_same();
    test_switch();
    test_random();
    printf("overlay test %s\n", fails ? "FAILED" : "passed");
    return fails != 0;
}
