/* SPDX-License-Identifier: GPL-3.0-only */
/* The silent placeholder (docs/SD-MODE-DESIGN.md 6b; the user, 2026-10-25): a part dropped to make memory room keeps
 * its sequence, plays nothing, shows NONE in grey, its arena memory idle (taken back when another part needs it), and
 * stays so through a save and a load (the engine byte ENG_UID_SILENT) until an engine is picked.
 *   placeholder_test */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
/* what a UI keeps that miss.c's main-loop side reads and writes (the Optimist UI's op_state.c ui): the message, its
 * frames left, the layer held; the page shown (cur_page: TOOLS when its count is asked) */
static struct { char msg[40]; uint32_t msg_t; uint8_t layer, hold_kind; int force; } ui;
static uint8_t settings_later;
static void ui_message(const char *m) { str_cpy(ui.msg, m, sizeof ui.msg); ui.msg_t = 40; }
static void ui_say(const char *a, const char *b) { (void)b; ui_message(a); }
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
#include "../firmware/src/core/model.c"          /* the model operations (apply_preset_to, BANK) */
#include "../firmware/src/drums/dsnd_desc.c"      /* the drum lanes' SOUND values and the kit list */
static const page_t *page_now = &PAGES[0];
static const page_t *cur_page(void) { return page_now; }
#define PROJ_HOST 1
#include "../firmware/src/storage/project.c"
#include "../firmware/src/storage/miss.c"
#if FELUCCA_NB_LIST
#include "../firmware/src/storage/nbank.c"       /* the FM6 / CZ collections in PRESETS */
#endif
static int bad, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { bad++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } else printf("ok   " __VA_ARGS__), printf("\n"); } while (0)

static void blocks(uint32_t n)
{
    static int32_t o[CTL * 2];
    while (n--) {
        events_block(CTL);
        mix_block(o, CTL);
    }
}

static uint32_t active(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}

int main(void)
{
    static project_t pj, back;
    static dlrec_t dl;
    track_t *t = &trk[0];
    host_tracks_init();
    set_engine_of(t, ENG_IX_FM6);
    blocks(64);
    CHECK(t->engine == ENG_IX_FM6 && !t->silent && arn_ptr(0) && !arn_cur[0].idle, "part 1 on FM6: its arena memory in use");
    trk_note_on(t, 60, 100);
    blocks(4);
    CHECK(active(t) > 0, "FM6 plays a note");
    trk_note_off(t, 60);
    blocks(4u * FS / CTL);
    part_drop(t);
    blocks(64);
    CHECK(t->silent && t->engine == eng_silent_slot() && part_mem_bytes(t->engine) == 0 && arn_cur[0].idle,
          "dropped: silent, on an engine that keeps no memory, FM6's block idle (free for another part)");
    trk_note_on(t, 64, 100);
    blocks(8);
    CHECK(active(t) == 0, "a note into the placeholder: no voice");
    trk_note_off(t, 64);
    CHECK(!strcmp(trk_eng_name(t), "NONE"), "shown as NONE (the UI draws it grey: ui/colors.c trk_col)");
    proj_capture(&pj, &dl);
    CHECK(pj.t[0].engine == ENG_UID_SILENT && pj.t[1].engine != ENG_UID_SILENT, "a save records it (ENG_UID_SILENT), the others as they are");
    back = pj;
    set_engine_of(t, ENG_IX_FM6);
    blocks(64);
    CHECK(!t->silent && t->engine == ENG_IX_FM6 && strcmp(trk_eng_name(t), "NONE"), "an engine picked: it plays again");
    trk_note_on(t, 60, 100);
    blocks(4);
    CHECK(active(t) > 0, "... and sounds");
    trk_note_off(t, 60);
    blocks(4u * FS / CTL);
    proj_apply(&back, &dl, 1);
    blocks(64);
    CHECK(t->silent && t->engine == eng_silent_slot(), "a load of the saved project: the placeholder again");
#ifdef FELUCCA_ARENA_BYTES                               /* (built with an arena of one part's memory: the drop flow) */
    {
        track_t *u = &trk[1];
        uint32_t k = 0, f;
        set_engine_of(t, ENG_IX_FM6);
        blocks(64);
        CHECK(t->engine == ENG_IX_FM6 && !t->silent, "flow: part 1 on FM6, the arena full");
        set_engine_of(u, ENG_IX_FM6);
        CHECK(u->eng_req != ENG_IX_FM6 && part_mem_refused.on && part_mem_refused.part == 1u && !part_mem_refused.from_load,
              "flow: part 2 asks FM6: refused, the refusal recorded (an edit)");
        CHECK(part_mem_refused.drop == 1 && mem_offer(&k) && k == 0u, "flow: proposed: drop part 1 (what arn_drop_for names)");
        CHECK(!mem_offer(&k), "flow: nothing else to propose (part 3 keeps no memory, part 2 is the one asking)");
        mem_drop(0);
        CHECK(mem_retry() == 0, "flow: part 1 still fading: wait");
        for (f = 0; f < 200u && mem_retry() == 0 && part_mem_refused.on; f++)
            blocks(8);
        blocks(64);
        CHECK(!part_mem_refused.on && t->silent && u->engine == ENG_IX_FM6 && !u->silent,
              "flow: once part 1 switched: part 2 on FM6, part 1 the placeholder");
        trk_note_on(u, 60, 100);
        blocks(4);
        CHECK(active(u) > 0, "flow: part 2's FM6 plays");
        trk_note_off(u, 60);
        blocks(4u * FS / CTL);
        proj_capture(&pj, &dl);                         /* a project with part 3 on FM6 too: its load cannot fit */
        pj.t[2].engine = (uint8_t)eng_uid(ENG_IX_FM6);
        proj_apply(&pj, &dl, 1);
        blocks(64);
        CHECK(part_mem_refused.on && part_mem_refused.from_load && part_mem_refused.part == 2u && trk[2].engine != ENG_IX_FM6,
              "flow: a load with one FM6 too many: that part refused (a load), its old engine kept");
        CHECK(mem_offer(&k) && k == 1u, "flow: proposed: drop part 2");
        trk[2].p[P_E0] = 77;                            /* (the loaded sound's own value: a load's retry keeps it) */
        mem_drop(1);
        for (f = 0; f < 200u && mem_retry() == 0 && part_mem_refused.on; f++)
            blocks(8);
        blocks(64);
        CHECK(!part_mem_refused.on && trk[2].engine == ENG_IX_FM6 && trk[2].p[P_E0] == 77 && trk[1].silent,
              "flow: part 2 dropped: part 3 on FM6 with the project's sound (not the engine's first preset)");
    }
#endif
    printf("placeholder test: %d checks, %d failed\n", checks, bad);
    return bad ? 1 : 0;
}
