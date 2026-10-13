/* SPDX-License-Identifier: GPL-3.0-only */
/* The CPU guard (firmware/src/system/cpuguard.c, FELUCCA_CPU_GUARD; docs/CPU-GUARD.md): its cost model, its prediction,
 * the hysteresis (8 halves over 85 % or one late half: a step; 2 s under 80 % with what the level saved: a level
 * off), what each level eases (ACID's oversampling, ANALOG 2's swarm, UNISON) and what it never sheds (MONO /
 * LEGATO / UNISON parts, a monophonic engine, the only held voice). Built with FELUCCA_CPU_GUARD=1 and
 * FELUCCA_ENG_ACID=1 (tests/run_tests.sh); the halves are fed with chosen times, as the audio ISR would. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "../firmware/src/system/cpuguard.c"

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
#if DRUM_X0X
    x0x_all_off();                                    /* (no X0X channel sounding) */
#endif
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
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
#if FELUCCA_MASTER_COMP
    {   /* a part's COMP insert: counted while it runs on a part that sounds (on, or letting go), not in no slot */
        uint32_t e1 = e0 + CG_COST_DRUMS;
        CHECK(CG_COST_TCOMP > 0u, "the COMP insert's cost is measured (cpu/fx/comp_on, comp_off): %u", CG_COST_TCOMP);
        trk[0].p[P_TCOMP] = 100, trk[2].p[P_TCOMP] = 100;
        CHECK(cg_est() == e1, "COMP in no FX slot: not counted (%u, want %u)", cg_est(), e1);
        fxs_load(3, FXT_COMP);
        CHECK(cg_est() == e1 + CG_COST_TCOMP, "COMP in a slot on a sounding part: + %u (%u), a silent part's: none",
              CG_COST_TCOMP, cg_est());
        trk[0].p[P_TCOMP] = 0;
        tcomp[0].gr16 = 1000;
        CHECK(cg_est() == e1 + CG_COST_TCOMP, "at 0 while it lets go: still counted (%u)", cg_est());
        tcomp[0].gr16 = 0;
        CHECK(cg_est() == e1, "at 0, at rest: not counted (%u)", cg_est());
        trk[2].p[P_TCOMP] = 0;
        CHECK(CG_COST_DBCOMP > 0u && CG_COST_DBDIST > 0u, "the drum bus's inserts' costs are measured (cpu/fx/dbus_*): "
              "COMP %u, DIST %u", CG_COST_DBCOMP, CG_COST_DBDIST);
        TDRUM->p[P_TCOMP] = 90;
        CHECK(cg_est() == e1 + CG_COST_DBCOMP, "the drum bus's COMP set, COMP in a slot: + %u (%u)", CG_COST_DBCOMP, cg_est());
        TDRUM->p[P_TCOMP] = 0;
        dbus_comp.gr16 = 500;
        CHECK(cg_est() == e1 + CG_COST_DBCOMP, "... at 0 while it lets go: still counted (%u)", cg_est());
        dbus_comp.gr16 = 0;
        CHECK(cg_est() == e1, "... at rest: not counted (%u)", cg_est());
        dbus_live = 1;
        CHECK(cg_est() == e1 + CG_COST_DBDIST, "the drum bus's DIST running on a signal: + %u (%u)", CG_COST_DBDIST, cg_est());
        dbus_live = 0;
        fxs_set(FXS_DEF);
    }
#endif
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

/* 4. a late half after one over the ceiling steps at once (a lone late half does not: a flash write, another
 * interrupt's burst); a half predicted late steps before it renders; the saving counts at the release, from the load
 * of the halves before the step (never a spike's own) */
static void test_late_and_gain(void)
{
    uint32_t i;
    busy_setup();
    half(US(101));
    CHECK(cg.level == CG_L_OFF, "a lone late half: no step (%u)", cg.level);
    half(US(50));
    half(US(90));
    half(US(101));
    CHECK(cg.level == CG_L_QUALITY, "a late half after one over the ceiling: a step at once (%u)", cg.level);
    /* the step's saving: ~88 % before (84 % for a while, then 8 halves at 90 %), 60 % after: ~28 % */
    busy_setup();
    for (i = 0; i < 50u; i++)
        half(US(84));
    for (i = 0; i < 8u; i++)
        half(US(90));
    CHECK(cg.level == CG_L_QUALITY, "level %u", cg.level);
    for (i = 0; i < CG_HOLD; i++)
        half(US(60));
    CHECK(cg.gain[CG_L_QUALITY] >= 66u && cg.gain[CG_L_QUALITY] <= 77u, "the saving measured: %u",
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
        CHECK(!((bass303_t *)acid_mem(0))->lite, "ACID oversamples off the guard");
        cg.level = CG_L_QUALITY;
        acid_render_v(&trk[0], &trk[0].v[0], b, CTL, &m);
        CHECK(((bass303_t *)acid_mem(0))->lite, "ACID without oversampling from quality on");
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

/* 7. the drums: X0X channels in the model (each its cost, their mix once), their tails and the synthesised kits'
 * shorter from quality on, never shed (built with the X0X kits: tests/run_tests.sh) */
static void test_drums(void)
{
    uint32_t i, e0;
    dsv_t *s;
    reset();
    TDRUM->p[P_E0] = DRUM_SAMPLED;                    /* a synthesised kit: a quiet tail ends sooner from quality on */
    drum_on(36, 100);
    for (i = 0; i < NDRUM && !drums.v[i].active; i++)
        ;
    CHECK(i < NDRUM && drums.synth[i], "a synthesised kick voice");
    s = &drums.ds[i < NDRUM ? i : 0];
    s->hold = 0;
    s->amp = s->amp_to = 100;                         /* -50 dBFS */
    s->nz = s->nz_to = s->ck = 0;
    CHECK(ds_alive(s), "off: a voice at -50 dBFS sounds on");
    CHECK(cg_tails() && cg_useful(CG_L_QUALITY), "a synthesised drum voice: quality has something to ease");
    cg.level = CG_L_QUALITY;
    CHECK(!ds_alive(s), "quality: a voice at -50 dBFS ends (under -40 dBFS)");
    s->amp = s->amp_to = 400;
    CHECK(ds_alive(s), "quality: a voice at -38 dBFS sounds on");
#if FELUCCA_DRUM_X808
    reset();
    TDRUM->p[P_E0] = DRUM_UID_X808;
    CHECK(cg_est() == CG_COST_BASE && !cg_tails(), "an X0X kit, nothing hit: idle");
    drum_on(36, 100);                                 /* BD: channel 11 */
    CHECK(x0x_sounding() == 1u << 11, "the 808's BD on its channel (%#x)", x0x_sounding());
    e0 = CG_COST_BASE + CG_X0X_SHARED + CG_X0X[11];
    CHECK(cg_est() == e0, "an X0X channel: %u, want the idle mix + their mix + its cost %u", cg_est(), e0);
    drum_on(38, 100);                                 /* SD: channel 12 */
    CHECK(cg_est() == e0 + CG_X0X[12], "a second channel adds its own cost only: %u", cg_est());
    CHECK(CG_X0X[11] > 0u && CG_X0X[23] > CG_X0X[11], "the measured order: the 808's cymbal costs most");
    /* nothing but the X0X drums and an FM6 part: quality first (the 808's tails), then the top; never a drum shed */
    host_preset(&trk[0], ENG_IX_FM6, 0);
    trk[0].p[P_VOICE] = V_POLY;
    hold(&trk[0], 4);
    half(US(101));
    half(US(101));
    CHECK(cg.level == CG_L_QUALITY, "an X0X 808 channel sounds: two late halves step to quality (level %u)", cg.level);
    half(US(50));
    CHECK(d8_tail_k > 5.0e-3f, "quality: the 808's tails end at -40 dB (%g)", (double)d8_tail_k);
    for (i = 0; i < 60u; i++) {
        if (!(x0x_sounding() >> 11 & 1u))
            drum_on(36, 100);                         /* (the kick kept sounding) */
        half(US(99));
    }
    CHECK(cg.level == CG_TOP && sounding(&trk[0], 1) == 1u, "the top: the FM6 voices shed but the last (level %u, "
          "%u held)", cg.level, sounding(&trk[0], 1));
    CHECK(x0x_sounding() >> 11 & 1u, "the X0X kick never shed");
    for (i = 0; i < 4u * CG_RELEASE; i++)
        half(US(30));
    CHECK(cg.level == CG_L_OFF && d8_tail_k < 5.0e-3f, "off again: the 808's tails at -60 dB (level %u, %g)", cg.level,
          (double)d8_tail_k);
#endif
    (void)e0;
}


/* 8. the reverb (fx.c rev_guard, the REVERB level): a tail with nothing else to ease. Before it the guard went straight
 * to the top with nothing to shed and the reverb kept every half late (the user's mots build on the FM-1: crackles
 * whenever the reverb was on; the emulator at 24 MHz: every half late, a click every block). Now one step to REVERB:
 * the wet fades out over one half with no step bigger than the tail's own, then exactly 0, the bus off (the line
 * cleared a chunk a block); when the load it saved fits again the send is heard again from a silent tank */
#define RV_BLOCKS(h) ((h) * (HALF_FRAMES / CTL))       /* the blocks of h halves */
static int32_t rv_send[CTL], rv_zero[CTL], rv_wl[CTL], rv_wr[CTL];
static uint32_t rv_ph = 1;
static int32_t rv_run(uint32_t blocks, int send, int32_t *maxstep)   /* -> the largest |wet| of those blocks */
{
    static int32_t last;
    uint32_t b, i;
    int32_t pk = 0;
    for (b = 0; b < blocks; b++) {
        for (i = 0; i < CTL; i++) {                   /* (a noise burst: the tank full) */
            rv_ph = rv_ph * 1103515245u + 12345u;
            rv_send[i] = send ? (int32_t)(rv_ph >> 17) - 16384 : 0;
        }
        if (!fx_buses(rv_zero, rv_zero, rv_send, rv_wl, rv_wr, CTL))
            for (i = 0; i < CTL; i++)
                rv_wl[i] = rv_wr[i] = 0;              /* (every bus idle: nothing added) */
        for (i = 0; i < CTL; i++) {
            int32_t a = rv_wl[i] < 0 ? -rv_wl[i] : rv_wl[i], d = rv_wl[i] - last;
            d = d < 0 ? -d : d;
            pk = a > pk ? a : pk;
            if (maxstep && d > *maxstep)
                *maxstep = d;
            last = rv_wl[i];
        }
    }
    return pk;
}
static void test_reverb(void)
{
    uint32_t i;
    int32_t pk, step0 = 0, step1 = 0;
    reset();
    song.g[G_RSIZE] = 124, song.g[G_RDAMP] = 30;      /* (a long tail: ~12 s) */
    rv_run(RV_BLOCKS(40), 1, 0);                      /* the tank full */
    pk = rv_run(RV_BLOCKS(20), 0, &step0);            /* the tail alone */
    CHECK(pk > 1000, "the reverb's tail sounds (%d)", pk);
    half(US(101));                                    /* late halves: nothing sounds but the tail */
    half(US(101));
    CHECK(cg.level < CG_TOP, "a reverb tail and nothing else: late halves ease the reverb, not the top with nothing "
          "to shed (level %u)", cg.level);
    rv_run(RV_BLOCKS(1), 0, &step1);                  /* (the half after the step: the fade) */
    for (i = 0; i < 40u; i++) {                       /* the reverb off: the load as the rest of the mix */
        half(US(60));
        rv_run(RV_BLOCKS(1), 0, 0);
    }
    CHECK(cg.level < CG_TOP, "60 %% once the reverb is off: no further step (level %u)", cg.level);
    pk = rv_run(RV_BLOCKS(1), 0, 0);
    CHECK(pk == 0, "the reverb's wet faded out to exactly 0 (%d)", pk);
    CHECK(step1 <= step0 + 64, "the fade-out: no step bigger than the tail's own (%d, the tail %d)", step1, step0);
    pk = rv_run(RV_BLOCKS(8), 1, 0);
    CHECK(pk == 0, "at the level the send is not heard (%d)", pk);
    for (i = 0; i < sizeof rev_line / 2u && !rev_line[i]; i++)
        ;
    CHECK(i == sizeof rev_line / 2u, "the line cleared while off (a cell %u of %u not 0)", i, (unsigned)(sizeof rev_line / 2u));
    for (i = 0; i < 4u * CG_RELEASE; i++) {           /* a light load: the reverb's saving fits again */
        half(US(20));
        if (i % 64u == 0u)
            rv_run(RV_BLOCKS(1), 1, 0);
    }
    CHECK(cg.level == CG_L_OFF, "the load fits again: the guard off (level %u)", cg.level);
    pk = rv_run(RV_BLOCKS(8), 1, 0);
    CHECK(pk > 1000, "the reverb heard again (%d)", pk);
}


/* 9. the reverb comes back (the user's FM-1, 2026-10-12: "once after some time the reverb stops producing any sound",
 * at a load of ~41 % with a worst half of 63 %). Ten minutes of halves around 41 % with the reverb sent all along,
 * a lone spike now and then (one half over its time: a flash write, a burst of another interrupt) and twice two in a
 * row: after each the reverb must sound again within 3 s, the guard off again; at no time may it stay silent. */
static uint32_t rr_seed = 7;
static uint32_t rr_rand(uint32_t n)
{
    rr_seed = rr_seed * 1664525u + 1013904223u;
    return (rr_seed >> 8) % n;
}
static void test_reverb_recover(void)
{
    uint32_t h, last_spike = 0, spikes = 0, worst = 0, silent = 0, longest = 0;
    reset();
    song.g[G_RSIZE] = 90, song.g[G_RDAMP] = 60;      /* (the default room) */
    rv_run(RV_BLOCKS(40), 1, 0);
    for (h = 0; h < 600u * 172u; h++) {               /* 10 minutes */
        uint32_t pct = 30u + rr_rand(34u);            /* 30 .. 63 % */
        int spike = h > 1000u && rr_rand(4000u) == 0u, pair = h == 30000u || h == 70000u;
        int32_t pk;
        if (spike || pair) {
            pct = 150u;
            spikes++;
            last_spike = h;
        }
        half(US(pct));
        if (pair) {
            rv_run(RV_BLOCKS(1), 1, 0);
            half(US(120));
            h++;
        }
        pk = rv_run(RV_BLOCKS(1), 1, 0);
        if (pk < 200) {
            silent++;
            longest = silent > longest ? silent : longest;
        } else {
            silent = 0;
        }
        if (h - last_spike == 3u * 172u && last_spike) {   /* 3 s after a spike: back */
            worst = cg.level > worst ? cg.level : worst;
            CHECK(cg.level == CG_L_OFF && pk >= 200, "3 s after the spike at half %u: the guard off (level %u) and "
                  "the reverb heard (%d)", last_spike, cg.level, pk);
        }
    }
    CHECK(spikes >= 10u, "the spikes came (%u)", spikes);
    CHECK(longest < 3u * 172u, "the reverb never silent for 3 s or more (longest %u halves)", longest);
    CHECK(cg.level == CG_L_OFF, "the end: the guard off (level %u)", cg.level);
}

int main(void)
{
    test_quiet();
    test_model();
    test_prediction();
    test_levels();
    test_late_and_gain();
    test_keep();
    test_drums();
    test_reverb();
    test_reverb_recover();
    printf("cpuguard_test: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
