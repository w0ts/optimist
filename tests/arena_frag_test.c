/* SPDX-License-Identifier: GPL-3.0-only */
/* The arena's fragmentation (docs/SD-MODE-DESIGN.md 6b, the user 2026-10-29: "how do we avoid fragmentation?"): a
 * host stress test through the firmware's own paths (model.c set_engine_of mem_retry, fx_slots.c ins_put fxs_load, a
 * project's load, fx/fx_arena.c fxm_poll, fx/insfx/fxi.c fxi_poll, core/arena_bufs.c, core/arena.c's packing), built
 * with every engine that keeps a part's memory the build has, the five limited inserts, the three sends and an arena
 * of FELUCCA_ARENA_BYTES (run_tests.sh: the jam profile's 96 KB, its sends: the delay's line 16384 samples, FDN8 in
 * the pool; XDLY_LEN 8192). Random steps: a part's engine (any, or one that keeps nothing), a track's insert slot (any
 * limited type, or none), a master slot's send in or out, now and then a project's load; refusals given up (MEM FULL)
 * or a load's list cancelled (placeholders), as a user who does not drop anything. After every step:
 *   - the free room, every gap (the idle blocks free: they are taken back without asking), and the largest gap: their
 *     difference is the room fragmentation hides; its worst and its mean are printed
 *   - every refusal the user sees (a part's change, a load's part, a send line, an insert line, another buffer) and
 *     how many of them a packed layout would have placed (what it asks for no more than the free room, with its own
 *     block for a part's change): the refusals fragmentation causes
 * The checks: the arena's blocks never overlap, nothing moved is lost (a moved part's block holds what it held), and
 * no refusal a packed layout would place in more than FRAG_MAX of the steps. The numbers (and the moves: how many, the
 * longest in host time, which is no FM-1 time) are printed for docs/SD-MODE-DESIGN.md 6b. The same source builds
 * against fm1-next before the packing (no core/arena.c moves) for the comparison */
#include <time.h>
#include <stdint.h>
static uint32_t frag_us(void)                           /* (the probe's clock: the host's, no FM-1 time) */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}
#define ARN_US() frag_us()
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
static struct { char msg[40]; uint32_t msg_t; uint8_t layer, hold_kind; int force; } ui;
static uint8_t settings_later;
static void ui_message(const char *m) { str_cpy(ui.msg, m, sizeof ui.msg); ui.msg_t = 40; }
static void ui_say(const char *a, const char *b) { (void)b; ui_message(a); }
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
#if !FXM_NEED || !INS_LIM
#error "arena_frag_test: built with FELUCCA_FX_ARENA, FELUCCA_ARENA_BYTES and the limited inserts (run_tests.sh)"
#endif
#ifdef ARN_NO
#define FRAG_PACK 1                                     /* (core/arena.c's packing: 2026-10-29) */
#define FRAG_LIM fxm_lim()
#define FRAG_MOVING(k) arn_moving(k)          /* (a block mid-move: its source being overwritten, not checked) */
#else
#define FRAG_PACK 0                                     /* (fm1-next before it: the comparison) */
#define FRAG_LIM ARN_BYTES
#define FRAG_MOVING(k) 0
#endif
#ifndef FRAG_STEPS
#define FRAG_STEPS 20000u
#endif
#ifndef FRAG_SEED
#define FRAG_SEED 1u
#endif
#ifndef FRAG_MAX
#define FRAG_MAX 0.015                                  /* (the share of steps with a refusal a packed layout would place) */
#endif

static int bad, checks;
static uint32_t frag_rng;                               /* (xorshift32: the same steps on every host, unlike rand) */
static uint32_t frag_rand(void)
{
    frag_rng ^= frag_rng << 13;
    frag_rng ^= frag_rng >> 17;
    frag_rng ^= frag_rng << 5;
    return frag_rng;
}
#define CHECK(c, ...) do { checks++; if (!(c)) { bad++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } else printf("ok   " __VA_ARGS__), printf("\n"); } while (0)

static void blocks(uint32_t n)
{
    static int32_t o[CTL * 2];
    while (n--) {
        events_block(CTL);
        mix_block(o, CTL);
    }
}
static void pass(void)                                  /* a main loop pass (main.c): the arena, the instances, WAVE */
{
    fxm_poll();
    arb_poll();
    fxi_poll();
#if FELUCCA_ENG_WAVE
    wt_service();
#endif
}
static void load_like(const uint8_t *eng)               /* a project's load (storage/project.c proj_apply: its engines) */
{
    uint32_t k;
    fm1_irq_off();
    part_mem_load_begin();
    for (k = 0; k < NPART; k++) {
        uint32_t no = part_mem_load(k, eng[k]) != 0;
        trk[k].silent = (uint8_t)no;
        trk[k].eng_req = (uint8_t)(no ? eng_silent_slot() : eng[k]);
    }
    fm1_irq_on();
}

/* ---- the layout's measures: the blocks in use (a reservation, or a block not idle), sorted by place */
typedef struct { uint32_t off, len; } fb_t;
static uint32_t frag_blocks(fb_t *b)
{
    uint32_t k, n = 0, i, j;
    for (k = 0; k < ARN_PARTS; k++) {
        if (arn_cur[k].len && !arn_cur[k].idle)
            b[n].off = arn_cur[k].off, b[n++].len = arn_cur[k].len;
        if (arn_has_next[k] && arn_next[k].len && !(arn_next[k].off == arn_cur[k].off && arn_cur[k].len && !arn_cur[k].idle))
            b[n].off = arn_next[k].off, b[n++].len = arn_next[k].len;
    }
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && b[j - 1u].off > b[j].off; j--) {
            fb_t t = b[j];
            b[j] = b[j - 1u], b[j - 1u] = t;
        }
    return n;
}
static void frag_room(uint32_t *free_, uint32_t *gap)  /* every gap, the largest */
{
    fb_t b[2u * ARN_PARTS];
    uint32_t n = frag_blocks(b), i, at = 0, f = 0, g = 0;
    for (i = 0; i <= n; i++) {
        uint32_t e = i < n ? b[i].off : ARN_BYTES, d = e > at ? e - at : 0u;
        f += d;
        g = d > g ? d : g;
        if (i < n && b[i].off + b[i].len > at)
            at = b[i].off + b[i].len;
    }
    *free_ = f, *gap = g;
}
static int frag_apart(void)                             /* the blocks inside the arena, none over another */
{
    fb_t b[2u * ARN_PARTS];
    uint32_t n = frag_blocks(b), i;
    for (i = 0; i < n; i++)
        if (b[i].off + b[i].len > ARN_BYTES || (i + 1u < n && b[i].off + b[i].len > b[i + 1u].off))
            return 0;
    return 1;
}
/* a part's block's contents (a sum of its words), to tell a move lost nothing: the same before and after a pass in
 * which the audio did not run (the parts' states change only in it) */
static uint32_t frag_sum(uint32_t k)
{
    const uint32_t *w = (const uint32_t *)arn_ptr(k);
    uint32_t i, s = 0;
    for (i = 0; w && i < arn_cur[k].len / 4u; i++)
        s = s * 31u + w[i];
    return s;
}

/* the refusals seen, and those a packed layout would have placed (by kind: a part's change, a load's part, a send line,
 * an insert line, another buffer) */
enum { RF_PART, RF_LOAD, RF_SEND, RF_INS, RF_BUF, RF_N };
static const char *const RF_NAME[RF_N] = {"a part's change", "a load's part", "a send line", "an insert line",
                                          "another buffer"};
static uint32_t rf_all[RF_N], rf_packed[RF_N], rf_rule[RF_N], rf_saved;
static uint64_t st_held, st_full;                       /* owner-steps holding a limited type, without its line */
/* the free bytes under the send lines' places (FRAG_PACK: fx_arena.c fxm_lim; before, none: the arena's end) */
static uint32_t frag_low(void)
{
    fb_t b[2u * ARN_PARTS];
    uint32_t n = frag_blocks(b), i, at = 0, f = 0, lim = FRAG_PACK ? FRAG_LIM : ARN_BYTES;
    for (i = 0; i <= n; i++) {
        uint32_t e = i < n ? b[i].off : ARN_BYTES, d;
        e = e < lim ? e : lim;
        d = e > at ? e - at : 0u;
        f += d;
        if (i < n && b[i].off + b[i].len > at)
            at = b[i].off + b[i].len;
    }
    return f;
}
/* a refusal of need bytes (tab of them a table, under the places), the free room then: raw, a packed layout of
 * everything would place it; by the rules, a packed layout in which only the parts and the inserts' room move and
 * the buffers, the room and the tables stay under the send lines' places (top: the kind is one of those) */
static void refused(uint32_t kind, uint32_t need, uint32_t room, uint32_t tab, uint32_t top)
{
    uint32_t low = frag_low();
    rf_all[kind]++;
    rf_packed[kind] += need <= room;
    rf_rule[kind] += need <= room && tab <= low && (!top || need <= low);
}
static uint32_t ins_need(void)                          /* the smallest line an owner left FULL asks for */
{
    uint32_t j, n = 0xFFFFFFFFu;
    for (j = 0; j < FXI_NL; j++)
        if (fxi_full[j] && FXA_SZ(j) * FXA_U * 2u < n)
            n = FXA_SZ(j) * FXA_U * 2u;
    return n;
}

int main(void)
{
    uint8_t eng[NENGINES], lim[8];
    uint32_t ne = 0, nl = 0, e, j, n, k, steps_packed = 0, worst = 0, wf = 0, wg = 0, okall = 1, lines = 0, seen = 0;
    uint32_t lost = 0;
    uint64_t sum_hid = 0, sum_free = 0;
    host_tracks_init();
    fxi_init();
    for (e = 0; e < NENGINES; e++)
        if (part_mem_bytes(e))
            eng[ne++] = (uint8_t)e;
    eng[ne++] = (uint8_t)eng_silent_slot();
    lim[nl++] = FXT_NONE;
    for (j = 0; j < FXI_NL; j++)
        if (FXI_TN[j])
            lim[nl++] = (uint8_t)FXI_TY(j);
    for (j = 0; j < 3u; j++)
        lines += FXM_BYTES[j];
    printf("arena %u B: %u engines keep a part's memory (the largest %u B), %u limited insert types, the send lines %u B "
           "(%s)\n", (unsigned)ARN_BYTES, (unsigned)(ne - 1u), (unsigned)ARN_SLOT, (unsigned)(nl - 1u), (unsigned)lines,
           FRAG_PACK ? "packed: core/arena.c" : "fm1-next: never moved");
    frag_rng = 2463534242u ^ (FRAG_SEED * 0x9E3779B9u);
    for (n = 0; n < FRAG_STEPS && okall; n++) {
        uint32_t r = frag_rand(), op = r % 20u, a = (r >> 8) % 4u, f0, g0, prev;
        frag_room(&f0, &g0);
        if (op < 6u) {                                  /* a part's engine */
            track_t *t = &trk[a % NPART];
            uint32_t want = eng[(r >> 12) % ne], own = arn_cur[a % NPART].len && !arn_cur[a % NPART].idle ?
                                                         arn_cur[a % NPART].len : 0u;
            prev = part_mem_refused.on;
            set_engine_of(t, want);
            if (part_mem_refused.on && (!prev || part_mem_refused.part == a % NPART)) {
                uint32_t fr;
                int rr = 0;
                for (fr = 0; fr < 40u && part_mem_refused.on; fr++) {   /* (the UI's frames: op_input.c mem_tick, its
                                                         * waits; no drop accepted) */
                    pass();
                    blocks(1);
                    rr = mem_retry();
                    if (rr < 0 || (rr == 0 && part_mem_refused.on && !part_mem_refused.waiting))
                        break;                          /* (no room: the drops proposed, all declined) */
                }
                if (part_mem_refused.on) {
                    refused(RF_PART, part_mem_bytes(want) + part_tab_bytes(want), f0 + own, part_tab_bytes(want), 0);
#ifdef FRAG_DBG
                    if (part_mem_bytes(want) + part_tab_bytes(want) <= f0 + own && rf_packed[RF_PART] < 8u) {
                        uint32_t c;
                        printf("dump: part %u wants %u (+%u tab) own %u free %u rr %d w %x:", a % NPART,
                               part_mem_bytes(want), part_tab_bytes(want), own, f0, rr, part_mem_refused.waiting);
                        for (c = 0; c < ARN_PARTS; c++)
                            if (arn_cur[c].len || arn_has_next[c])
                                printf(" [c%u %u+%u%s%s n%u+%u]", c, arn_cur[c].off, arn_cur[c].len,
                                       arn_cur[c].idle ? "i" : "", arn_has_next[c] ? "R" : "", arn_next[c].off,
                                       arn_next[c].len);
                        printf("\n");
                    }
#endif
                } else
                    rf_saved++;                         /* (placed once the blocks were moved together: mem_retry) */
            }
            part_mem_refused.on = 0;                    /* (given up: MEM FULL) */
        } else if (op < 12u) {                          /* a track's insert slot */
            uint8_t s[2];
            s[0] = ins_slot[a][0], s[1] = ins_slot[a][1];
            s[(r >> 12) & 1u] = lim[(r >> 13) % nl];
            if (s[0] == s[1] && s[0] != FXT_NONE)
                s[1] = FXT_NONE;                        /* (a type once an owner) */
            ins_put(a, s);
        } else if (op < 15u) {                          /* a master slot's send */
            fxs_load(1u + a % 3u, (r >> 12) % 3u ? FXS_LT[a % 3u] : FXT_NONE);
        } else if (op == 15u && !((r >> 12) & 7u)) {    /* a project's load */
            uint8_t le[NPART];
            for (k = 0; k < NPART; k++)
                le[k] = eng[(r >> (14u + 3u * k)) % ne];
            load_like(le);
            for (k = 0; k < NPART; k++)
                if (part_mem_refused.lmask >> k & 1u)
                    refused(RF_LOAD, part_mem_bytes(le[k]) + part_tab_bytes(le[k]), f0, part_tab_bytes(le[k]), 0);
            mem_load_cancel();                          /* (HOME: placeholders) */
        }
        {   /* (a pass alone, the audio not run: whatever it moves keeps its contents) */
            uint32_t sm[NPART], pk[NPART];
            for (k = 0; k < NPART; k++)
                sm[k] = frag_sum(k), pk[k] = arn_ptr(k) && !arn_cur[k].idle && !FRAG_MOVING(k) ? 1u : 0u;
            pass();
            for (k = 0; FRAG_PACK && k < NPART; k++)
                if (pk[k] && arn_ptr(k) && !arn_cur[k].idle && !FRAG_MOVING(k) && frag_sum(k) != sm[k])
                    lost++;
        }
        blocks(1u + (r >> 28) % 3u);
        pass();
        frag_room(&f0, &g0);
        for (j = 0; j < 3u; j++)
            if (fxm_news >> j & 1u)
            {
                refused(RF_SEND, FXM_BYTES[j], f0, 0, 0);
#ifdef FRAG_DBG
                if (FXM_BYTES[j] <= f0 && rf_rule[RF_SEND] < 6u) {
                    uint32_t c;
                    printf("sdump: send %u wants %u at %u free %u busy %d:", j, FXM_BYTES[j], FXM_AT[j], f0, arn_busy());
                    for (c = 0; c < ARN_PARTS; c++)
                        if (arn_cur[c].len || arn_has_next[c])
                            printf(" [c%u %u+%u%s%s n%u+%u]", c, arn_cur[c].off, arn_cur[c].len,
                                   arn_cur[c].idle ? "i" : "", arn_has_next[c] ? "R" : "", arn_next[c].off,
                                   arn_next[c].len);
                    printf("\n");
                }
#endif
            }
        if (fxm_news >> FXM_INS & 1u)
            refused(RF_INS, ins_need(), f0, 0, 1);
        for (j = 0; j < ARB_NC; j++)
            if (arb_news >> j & 1u)
                refused(RF_BUF, arb_bytes(j), f0, 0, 1);
        fxm_news = 0, arb_news = 0;
        {
            uint32_t p = 0, i;
            for (i = 0; i < RF_N; i++)
                p += rf_rule[i];
            steps_packed += p != seen;                  /* (a step with a new refusal a packed layout would place, by
                                                         * the rules) */
            seen = p;
        }
        sum_hid += f0 - g0, sum_free += f0;
        for (j = 0; j < FXI_NL; j++)
            for (k = 0; k < FXI_NK; k++)
                if (FXI_TN[j] && fxi_holds(k, FXI_TY(j))) {
                    st_held++;
                    st_full += fxi_at[k][j] == FXI_NONE;
                }
        if (f0 - g0 > worst)
            worst = f0 - g0, wf = f0, wg = g0;
        okall = frag_apart();
    }
    {
        uint32_t i, all = 0, packed = 0, rule = 0, mv = 0, mus = 0, gated = 0;
#if FRAG_PACK
        mv = arn_probe.moves, mus = arn_probe.max_us, gated = arn_probe.gated;
#endif
        for (i = 0; i < RF_N; i++) {
            printf("  refused: %-16s %6u, a packed layout would have placed %u (by the rules %u)\n", RF_NAME[i],
                   (unsigned)rf_all[i], (unsigned)rf_packed[i], (unsigned)rf_rule[i]);
            all += rf_all[i], packed += rf_packed[i], rule += rf_rule[i];
        }
        printf("  a part's change placed once the blocks were moved together (core/arena.c arn_pack): %u\n",
               (unsigned)rf_saved);
        printf("  insert owner-steps: %llu holding a limited type, %llu of them without its line (FULL or waiting: "
               "%.1f %%)\n", (unsigned long long)st_held, (unsigned long long)st_full,
               st_held ? 100.0 * (double)st_full / (double)st_held : 0.0);
        printf("FRAG rule_fit=%u ", (unsigned)rule);
        printf("FRAG arena=%u steps=%u seed=%u refused=%u packed_fit=%u steps_packed=%u worst_hidden=%u (free %u, "
               "largest gap %u) mean_hidden=%u mean_free=%u ins_unserved=%.1f%% part=%u/%u load=%u/%u send=%u/%u ins=%u/%u "
               "buf=%u/%u saved=%u moves=%u move_max_us=%u gated=%u lost=%u\n", (unsigned)ARN_BYTES, (unsigned)n,
               (unsigned)FRAG_SEED, (unsigned)all, (unsigned)packed, (unsigned)steps_packed, (unsigned)worst,
               (unsigned)wf, (unsigned)wg, (unsigned)(sum_hid / (n ? n : 1u)), (unsigned)(sum_free / (n ? n : 1u)),
               st_held ? 100.0 * (double)st_full / (double)st_held : 0.0, (unsigned)rf_all[RF_PART],
               (unsigned)rf_packed[RF_PART], (unsigned)rf_all[RF_LOAD], (unsigned)rf_packed[RF_LOAD],
               (unsigned)rf_all[RF_SEND], (unsigned)rf_packed[RF_SEND], (unsigned)rf_all[RF_INS],
               (unsigned)rf_packed[RF_INS], (unsigned)rf_all[RF_BUF], (unsigned)rf_packed[RF_BUF], (unsigned)rf_saved,
               (unsigned)mv, (unsigned)mus, (unsigned)gated, (unsigned)lost);
        CHECK(okall && n == FRAG_STEPS, "%u random steps: the arena's blocks never overlap", (unsigned)n);
        CHECK(all > 100u, "... the arena is short often enough to measure: %u refusals", (unsigned)all);
        CHECK(!lost, "... every part's block moved holds what it held: %u moves, none lost (%u)", (unsigned)mv,
              (unsigned)lost);
#ifdef ARN_MV_STEP
        CHECK(gated > 0u, "... a window a pass (ARN_MV_STEP): moves across audio blocks, the moving part skipped in %u "
              "part-blocks (voice.c PART_MOVING), the blocks still apart", (unsigned)gated);
#endif
        CHECK(!FRAG_PACK || steps_packed <= (uint32_t)(FRAG_MAX * FRAG_STEPS), "... a refusal a packed layout would "
              "have placed in %u steps (%.2f %%, at most %.2f %%): fragmentation hides at worst %u B (of %u free), %u B on "
              "average", (unsigned)steps_packed, 100.0 * steps_packed / FRAG_STEPS, 100.0 * FRAG_MAX, (unsigned)worst,
              (unsigned)wf, (unsigned)(sum_hid / (n ? n : 1u)));
    }
    printf("arena frag test: %d checks, %d failed\n", checks, bad);
    return bad ? 1 : 0;
}
