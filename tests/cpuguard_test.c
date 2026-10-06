/* SPDX-License-Identifier: GPL-3.0-only */
/* The CPU guard (firmware/src/cpuguard.c, FELUCCA_CPU_GUARD; docs/CPU-GUARD.md): its cost model, its prediction,
 * the hysteresis (8 halves over 85 % or one late half: a step; 2 s under 80 % with what the level saved: a level
 * off), what each level eases (ACID's oversampling, ANALOG 2's swarm, UNISON) and what it never sheds (MONO /
 * LEGATO / UNISON parts, a monophonic engine, the only held voice). Built with FELUCCA_CPU_GUARD=1 and
 * FELUCCA_ENG_ACID=1 (tests/run_tests.sh); the halves are fed with chosen times, as the audio ISR would. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "../firmware/src/cpuguard.c"

static int fails;
#define CHECK(c, ...)                                                                                   \
    do {                                                                                                \
        if (!(c)) {                                                                                     \
            printf("cpuguard_test: FAIL line %d: ", __LINE__);                                          \
            printf(__VA_ARGS__);                                                                        \
            printf("\n");                                                                               \
            fails++;                                                                                    \
        }                                                                                               \
    } while (0)

#define US(pct) ((uint32_t)(CG_HALF_US * (pct) / 100u))   /* a half that took pct % of its time */

static void reset(void)
{
    uint32_t p;
    host_tracks_init();
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    for (p = 0; p < NTRK; p++)
        memset(trk[p].v, 0, sizeof trk[p].v);
    memset(&cg, 0, sizeof cg);
    cpu_khz = 96000;                                  /* the emulator's clock in the measurements */
}

static void hold(track_t *t, uint32_t n)                /* n notes held on part t */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        trk_note_on(t, 48u + 3u * i, 100);
}

static uint32_t sounding(const track_t *t, int held)    /* voices sounding (held only: gated), not fading out */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].stage != 4u && (!held || t->v[i].gate);
    return n;
}

static void half(uint32_t us)                           /* one DMA half as the ISR runs it */
{
    cg_pre();
    cg_post(us);
}

/* 1. the model: the idle mix, a voice of each engine by UID, the drums; generated (cpuguard_costs.h) */
static void test_model(void)
{
    uint32_t e0;
    reset();
    CHECK(cg_est() == CG_COST_BASE, "idle: %u, want %u", cg_est(), CG_COST_BASE);
    host_preset(&trk[0], ENG_SLOT_ANALOG, 0);
    trk[0].p[P_VOICE] = V_POLY;
    hold(&trk[0], 3);
    CHECK(cg_est() == CG_COST_BASE + 3u * CG_VCOST[0], "3 ANALOG voices: %u", cg_est());
    host_preset(&trk[1], ENG_IX_FM6, 0);
    trk[1].p[P_VOICE] = V_POLY;
    hold(&trk[1], 2);
    e0 = CG_COST_BASE + 3u * CG_VCOST[0] + 2u * CG_VCOST[ENG_UID_FM6];
    CHECK(cg_est() == e0, "+ 2 FM6 voices: %u, want %u", cg_est(), e0);
    drum_on(36, 100);
    CHECK(cg_est() == e0 + CG_COST_DRUMS, "+ a drum voice: %u, want %u", cg_est(), e0 + CG_COST_DRUMS);
    CHECK(CG_VCOST[ENG_UID_FM6] > CG_VCOST[0] && CG_VCOST[ENG_UID_ACID] > CG_VCOST[ENG_UID_FM6],
          "the measured order (tests/cpu_baseline.txt): ANALOG < FM6 < ACID a voice");
}

/* 2. the prediction: the last half measured, plus what the model says changed since */
static void test_prediction(void)
{
    uint32_t want;
    reset();
    cpu_khz = 0;
    half(US(50));
    CHECK(cg.ips == 0 && cg.est == 0, "no clock (the host): no estimate");
    cpu_khz = 96000;
    host_preset(&trk[0], ENG_IX_FM6, 0);
    hold(&trk[0], 1);
    half(US(40));                                     /* (the model's first reading) */
    CHECK(cg.ips == 96000u * 1000u / FS, "instructions a sample at 96 MHz: %u", cg.ips);
    hold(&trk[0], 0);
    trk_note_on(&trk[0], 70, 100);
    trk_note_on(&trk[0], 74, 100);                    /* two voices more */
    cg_pre();
    want = US(40) * 256u / CG_HALF_US + 2u * CG_VCOST[ENG_UID_FM6] * 256u / cg.ips;
    CHECK(cg.est + 1u >= want && cg.est <= want + 1u, "predicted %u, want the measured + 2 voices %u", cg.est, want);
    cg_post(US(40));
    CHECK(cg.load == cg.est, "the load is the larger: %u", cg.load);
}

/* every level has something to ease: part 1 ANALOG with a swarm (POLY), part 2 a UNISON lead, part 3 a MONO bass */
static void busy_setup(void)
{
    reset();
    host_preset(&trk[0], ENG_SLOT_ANALOG, 17);         /* SUPER PAD: swarm 6 */
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_A2SWRM] = 6;
    hold(&trk[0], 3);
    host_preset(&trk[1], ENG_SLOT_ANALOG, 16);
    trk[1].p[P_VOICE] = V_UNISON;
    trk[1].p[P_A2SWRM] = 0;
    trk_note_on(&trk[1], 72, 100);
    host_preset(&trk[2], ENG_SLOT_ANALOG, 2);
    trk[2].p[P_VOICE] = V_MONO;
    trk[2].p[P_A2SWRM] = 0;
    trk_note_on(&trk[2], 36, 100);
}

/* 3. the hysteresis, the order of the levels, what each eases */
static void test_levels(void)
{
    uint32_t i, uni0;
    busy_setup();
    uni0 = sounding(&trk[1], 0);
    CHECK(uni0 >= 3u, "UNISON plays more than 2 voices before (%u)", uni0);
    for (i = 0; i < 7u; i++)
        half(US(90));
    CHECK(cg.level == CG_L_OFF, "7 halves at 90 %%: no step (level %u)", cg.level);
    half(US(90));
    CHECK(cg.level == CG_L_QUALITY, "8 halves at 90 %%: quality (level %u)", cg.level);
    CHECK(super_copies(6) == 2u, "quality: the swarm at 2 copies (%u)", super_copies(6));
    CHECK(sounding(&trk[1], 0) == uni0, "quality: UNISON untouched");
    for (i = 0; i < 8u; i++)
        half(US(90));
    CHECK(cg.level == CG_L_UNISON, "8 more: UNISON (level %u)", cg.level);
    CHECK(sounding(&trk[1], 0) <= 2u && trk[1].v[0].stage != 4u, "UNISON at 2 voices at most, its first kept (%u)",
          sounding(&trk[1], 0));
    trk_note_off(&trk[1], 72);
    trk_note_on(&trk[1], 74, 100);                    /* a new note: 2 voices */
    CHECK(sounding(&trk[1], 1) == 2u, "a new UNISON note plays 2 voices (%u)", sounding(&trk[1], 1));
    for (i = 0; i < 8u; i++)
        half(US(90));
    CHECK(cg.level == CG_L_NOTES, "8 more: notes (level %u)", cg.level);
    for (i = 0; i < 20u; i++)
        half(US(90));
    CHECK(sounding(&trk[0], 1) == 0u, "the POLY pad shed (%u held)", sounding(&trk[0], 1));
    CHECK(sounding(&trk[1], 1) == 2u && sounding(&trk[2], 1) == 1u, "the lead and the bass never shed (%u, %u)",
          sounding(&trk[1], 1), sounding(&trk[2], 1));
    CHECK(cg.steps == 3u && cg.sheds >= 2u, "steps %u, sheds %u", cg.steps, cg.sheds);

    /* release: 2 s under 80 % (gain[top] 0), one level off at a time; between 80 and 85 %: held */
    for (i = 0; i < 1000u; i++)
        half(US(82));
    CHECK(cg.level == CG_L_NOTES, "82 %% holds the level (%u)", cg.level);
    half(US(50));                                     /* (predicted from the last half: still 82 %) */
    for (i = 0; i < CG_RELEASE - 1u; i++)
        half(US(50));
    CHECK(cg.level == CG_L_NOTES, "344 halves under: still held (%u)", cg.level);
    half(US(50));
    CHECK(cg.level == CG_L_UNISON, "345: one level off (%u)", cg.level);
    CHECK(super_copies(6) == 2u, "UNISON level: the swarm still at 2");
    for (i = 0; i < 2u * CG_RELEASE + 1u; i++)
        half(US(50));
    CHECK(cg.level == CG_L_OFF, "2 x 2 s more: off (%u)", cg.level);
    CHECK(super_copies(6) == 6u, "off: the swarm whole (%u)", super_copies(6));
}

/* 4. a late half steps at once; a half predicted late steps before it renders; the saving counts at the release */
static void test_late_and_gain(void)
{
    uint32_t i;
    busy_setup();
    half(US(101));
    CHECK(cg.level == CG_L_QUALITY, "one late half: a step at once (%u)", cg.level);
    /* the step's saving: 90 % before, 60 % after: 30 % (77/256) */
    busy_setup();
    for (i = 0; i < 8u; i++)
        half(US(90));
    CHECK(cg.level == CG_L_QUALITY, "level %u", cg.level);
    for (i = 0; i < CG_HOLD; i++)
        half(US(60));
    CHECK(cg.gain[CG_L_QUALITY] >= 75u && cg.gain[CG_L_QUALITY] <= 79u, "the saving measured: %u",
          cg.gain[CG_L_QUALITY]);
    for (i = 0; i < 2u * CG_RELEASE; i++)
        half(US(60));
    CHECK(cg.level == CG_L_QUALITY, "60 %% + its 30 %% would be over 80 %%: kept (%u)", cg.level);
    for (i = 0; i < CG_RELEASE + 1u; i++)
        half(US(40));
    CHECK(cg.level == CG_L_OFF, "40 %% + 30 %%: off (%u)", cg.level);
    /* predicted late: FM6 voices more on a part measured over the ceiling; under it, a prediction alone does nothing
     * (the model counts the voices of the half that started them twice) */
    reset();
    host_preset(&trk[0], ENG_IX_FM6, 0);
    trk[0].p[P_VOICE] = V_POLY;
    half(US(70));
    for (i = 0; i < 4u; i++)
        trk_note_on(&trk[0], 60u + i, 100);
    cg_pre();
    CHECK(cg.est >= CG_FULL && cg.level == CG_L_OFF && cg.early == 0u, "predicted %u after a half at 70 %%: nothing "
          "(level %u)", cg.est, cg.level);
    cg_post(US(90));
    for (i = 0; i < 2u; i++)
        trk_note_on(&trk[0], 66u + i, 100);
    cg_pre();
    CHECK(cg.level == CG_TOP && cg.early == 1u, "predicted late after 90 %%: straight to the top before the render "
          "(FM6: nothing else to ease; level %u, early %u)", cg.level, cg.early);
    cg_post(US(95));
    for (i = 0; i < 2u; i++)
        trk_note_on(&trk[0], 70u + i, 100);
    cg_pre();
    CHECK(cg.sheds >= 1u && cg.sheds <= CG_SHED_MAX, "predicted late at the top: voices fade out before it renders "
          "(%u)", cg.sheds);
}

/* 5. never shed: MONO / LEGATO / UNISON parts, ACID, the only held voice; nothing to ease: straight to the top */
static void test_keep(void)
{
    uint32_t i;
    reset();
    host_preset(&trk[0], ENG_IX_ACID, 0);
    host_preset(&trk[1], ENG_IX_ACID, 1);
    host_preset(&trk[2], ENG_SLOT_ANALOG, 2);
    trk[0].p[P_VOICE] = V_POLY;                       /* (ACID: one voice whatever the mode) */
    trk[1].p[P_VOICE] = V_LEGATO;
    trk[2].p[P_VOICE] = V_MONO;
    trk_note_on(&trk[0], 45, 100);
    trk_note_on(&trk[1], 52, 100);
    trk_note_on(&trk[2], 36, 100);
    for (i = 0; i < 100u; i++)
        half(US(95));
    CHECK(cg.level == CG_TOP && cg.sheds == 0u, "ACID and bass lines: at the top, nothing shed (level %u, sheds %u)",
          cg.level, cg.sheds);
    CHECK(sounding(&trk[0], 1) + sounding(&trk[1], 1) + sounding(&trk[2], 1) == 3u, "all three still held");
    {
        int32_t b[CTL] = {0};
        vmod_t m;
        memset(&m, 0, sizeof m);
        cg.level = CG_L_OFF;
        acid_render_v(&trk[0], &trk[0].v[0], b, CTL, &m);
        CHECK(!acid_b[0].lite, "ACID oversamples off the guard");
        cg.level = CG_L_QUALITY;
        acid_render_v(&trk[0], &trk[0].v[0], b, CTL, &m);
        CHECK(acid_b[0].lite, "ACID without oversampling from quality on");
    }
    /* the only held voice of a POLY part is never shed */
    reset();
    host_preset(&trk[0], ENG_IX_FM6, 0);
    trk_note_on(&trk[0], 60, 100);
    cg.level = CG_TOP;
    for (i = 0; i < 50u; i++)
        half(US(99));
    CHECK(sounding(&trk[0], 1) == 1u && cg.sheds == 0u, "the only held voice stays (%u, sheds %u)",
          sounding(&trk[0], 1), cg.sheds);
    /* FM6 only: nothing for quality or UNISON: one step goes to the top */
    reset();
    host_preset(&trk[0], ENG_IX_FM6, 0);
    hold(&trk[0], 4);
    half(US(101));
    CHECK(cg.level == CG_TOP && cg.steps == 1u, "nothing to ease: straight to shedding (level %u)", cg.level);
}

/* 6. under the ceiling nothing ever changes: 20 s of halves up to 85 %, notes coming and going */
static void test_quiet(void)
{
    uint32_t i;
    busy_setup();
    for (i = 0; i < 3450u; i++) {
        if (i % 40u == 0u)
            trk_note_on(&trk[0], 60u + (i / 40u) % 12u, 100);
        if (i % 40u == 20u)
            trk_note_off(&trk[0], 60u + (i / 40u) % 12u);
        half(US(60u + (i * 7u) % 25u));               /* 60 .. 84 % */
    }
    CHECK(cg.level == CG_L_OFF && cg.steps == 0u && cg.sheds == 0u, "under the ceiling: nothing (level %u, steps %u, "
          "sheds %u)", cg.level, cg.steps, cg.sheds);
}

int main(void)
{
    test_quiet();
    test_model();
    test_prediction();
    test_levels();
    test_late_and_gain();
    test_keep();
    printf("cpuguard_test: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
