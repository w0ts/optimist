/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' PAN, MUTE and SOLO (drums/drum_mix.c) and their place in the FX record (fx/fx_rec.c):
 *   heard    the mask a mute and a solo make (solo wins over the rest, a muted soloed lane stays muted, the click
 *            always heard)
 *   mix      a muted lane: the mix without its hits, sample for sample; a soloed lane: the mix of it alone; every lane
 *            panned p: the mix of the drum track panned p (a lane at the centre is the track's pan, bit for bit),
 *            the sampled and the synthesised kits; an X0X kit panned hard left: nothing on the right; a voice of a
 *            lane muted while it sounds stops; the lanes' meters see their own lane only
 *   record   nothing set: no TLV (a project as before); set: a TLV round trip; a record without it (an older
 *            project): every lane at the centre, heard; PROJECT SAVE / LOAD keeps them
 * Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static uint8_t nor[0x100000];
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
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"

static struct { int force; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/storage/sections/sections.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("drum lanes mix: %-90s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---- the heard mask */
static void t_heard(void)
{
    check("nothing muted or soloed: every lane and the click heard", dlm_heard_mask(0, 0) == 0x1FFFFu);
    check("lanes 1 and 3 muted: the others heard", dlm_heard_mask(0x5u, 0) == (0x1FFFFu & ~0x5u));
    check("lane 3 soloed: lane 3 alone (and the click)", dlm_heard_mask(0, 0x4u) == (0x4u | 0x10000u));
    check("lanes 3 and 5 soloed, 5 muted: lane 3 alone", dlm_heard_mask(0x10u, 0x14u) == (0x4u | 0x10000u));
    check("every lane muted: only the click", dlm_heard_mask(0xFFFFu, 0) == 0x10000u);
}

/* ---- the mix: a bar of drums (kick 1 and 9, snare 5 and 13, closed hat on the eighths), rendered in a child */
#define L_KICK 0u
#define L_SNARE 2u
#define L_CHAT 4u
typedef struct {
    uint32_t kit, drop;                                 /* the kit; lanes whose hits are left out of the pattern */
    int32_t tpan, lpan;                                 /* the drum track's PAN, every lane's PAN */
    uint16_t mute, solo;
    int mute_at;                                        /* > 0: the kick muted after that many blocks (else -1) */
} rq_t;
typedef struct {
    uint64_t h;
    uint64_t el, er;                                    /* |L| and |R| summed */
    int32_t pk[DRUM_LANES];                             /* the lanes' meters at the end */
    uint32_t kick_before, kick_after;                   /* kick voices sounding before / after the block of mute_at */
} rs_t;
static rs_t render(const rq_t *q)
{
    int fd[2];
    rs_t res;
    memset(&res, 0, sizeof res);
    if (pipe(fd))
        return res;
    if (!fork()) {
        int32_t o[CTL * 2];
        uint32_t b, i, l;
        close(fd[0]);
        host_tracks_init();
        TDRUM->p[P_SLEN] = 16;
        TDRUM->p[P_E0] = (int16_t)q->kit;
        TDRUM->p[P_PAN] = (int16_t)q->tpan;
        for (i = 0; i < 16u; i++) {
            dstep_t *s = &TDRUM->dstep[i];
            memset(s, 0, sizeof *s);
            if (i % 8u == 0u && !((q->drop >> L_KICK) & 1u))
                dstep_set(s, L_KICK, LV_NORM, 0);
            if (i % 8u == 4u && !((q->drop >> L_SNARE) & 1u))
                dstep_set(s, L_SNARE, LV_NORM, 0);
            if (i % 2u == 0u && !((q->drop >> L_CHAT) & 1u))
                dstep_set(s, L_CHAT, LV_NORM, 0);
        }
        for (l = 0; l < DRUM_LANES; l++)
            dsend[l] = dsend_word(0, 0, 0);             /* (no reverb: the sides stay apart) */
        for (l = 0; l < DRUM_LANES; l++)
            dlm_set_pan(l, q->lpan);
        dlm_mute = q->mute, dlm_solo = q->solo;
        dlm_update();
        transport_req = 1;
        for (b = 0; b < 2u * FS / CTL; b++) {
            if (q->mute_at > 0 && b == (uint32_t)q->mute_at) {
                for (i = 0; i < NDRUM; i++)
                    res.kick_before += drums.v[i].active && dlm_lane(drums.v[i].note) == L_KICK;
                dlm_set_mute(L_KICK, 1);
            }
            mix_block(o, CTL);
            if (q->mute_at > 0 && b == (uint32_t)q->mute_at)
                for (i = 0; i < NDRUM; i++)
                    res.kick_after += drums.v[i].active && dlm_lane(drums.v[i].note) == L_KICK;
            for (i = 0; i < 2u * CTL; i++) {
                res.h = (res.h ^ (uint32_t)o[i]) * 1099511628211ull;
                if (i & 1u)
                    res.er += (uint64_t)(o[i] < 0 ? -o[i] : o[i]);
                else
                    res.el += (uint64_t)(o[i] < 0 ? -o[i] : o[i]);
            }
        }
        memcpy(res.pk, dlm_pk, sizeof res.pk);
        if (write(fd[1], &res, sizeof res) != sizeof res)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &res, sizeof res) != sizeof res)
        res.h = 2;
    close(fd[0]);
    wait(0);
    return res;
}
static rq_t rq(uint32_t kit)
{
    rq_t q;
    memset(&q, 0, sizeof q);
    q.kit = kit;
    q.mute_at = -1;
    return q;
}
static void t_mix_kit(uint32_t kit, const char *name)
{
    char w[128];
    rq_t q = rq(kit), a;
    rs_t full = render(&q), r1, r2;
    snprintf(w, sizeof w, "%s: the bar sounds (the test hears the drums)", name);
    check(w, full.el > 0u && full.er > 0u);
    a = q, a.mute = 1u << L_KICK;
    r1 = render(&a);
    a = q, a.drop = 1u << L_KICK;
    r2 = render(&a);
    snprintf(w, sizeof w, "%s: the kick muted = the bar without its kicks, sample for sample", name);
    check(w, r1.h == r2.h && r1.h != full.h);
    a = q, a.solo = 1u << L_SNARE;
    r1 = render(&a);
    a = q, a.drop = (1u << L_KICK) | (1u << L_CHAT);
    r2 = render(&a);
    snprintf(w, sizeof w, "%s: the snare soloed = the snare alone, sample for sample", name);
    check(w, r1.h == r2.h);
    snprintf(w, sizeof w, "%s: ... the meters: the snare's moved, the kick's and the hat's did not", name);
    check(w, r1.pk[L_SNARE] > 0 && r1.pk[L_KICK] == 0 && r1.pk[L_CHAT] == 0);
    a = q, a.solo = 1u << L_SNARE, a.mute = 1u << L_SNARE;
    r1 = render(&a);
    a = q, a.drop = 0xFFFFu;
    r2 = render(&a);
    snprintf(w, sizeof w, "%s: the snare soloed and muted: no drums at all", name);
    check(w, r1.h == r2.h);
    a = q, a.lpan = -40;
    r1 = render(&a);
    a = q, a.tpan = -40;
    r2 = render(&a);
    snprintf(w, sizeof w, "%s: every lane panned -40 = the drum track panned -40, sample for sample", name);
    check(w, r1.h == r2.h && r1.h != full.h);
    a = q, a.lpan = 63;
    r1 = render(&a);
    snprintf(w, sizeof w, "%s: every lane hard right: the left side all but empty", name);
    check(w, r1.el * 50u < r1.er);
    a = q, a.mute_at = 3;
    r1 = render(&a);
    snprintf(w, sizeof w, "%s: the kick muted while it sounds: its voice stops within the block", name);
    check(w, r1.kick_before > 0u && r1.kick_after == 0u);
}
static void t_mix(void)
{
    if (drum_kit_built(0))
        t_mix_kit(0, "sampled kit");
    if (FELUCCA_DRUM_SYNTH)
        t_mix_kit(DRUM_SAMPLED, "synthesised kit");
#if DRUM_X0X
    if (drum_kit_built(DRUM_UID_X909)) {
        rq_t q = rq(DRUM_UID_X909), a;
        rs_t full = render(&q), r1, r2;
        a = q, a.mute = 1u << L_KICK;
        r1 = render(&a);
        a = q, a.drop = 1u << L_KICK;
        r2 = render(&a);
        check("X0X 909: the kick muted = the bar without its kicks", r1.h == r2.h && r1.h != full.h);
        a = q, a.lpan = -64;
        r1 = render(&a);
        check("X0X 909: every lane hard left: the right side all but empty", r1.er * 50u < r1.el && r1.el > 0u);
        a = q, a.lpan = 0;
        a.solo = 1u << L_CHAT;
        r1 = render(&a);
        check("X0X 909: the hat soloed: its meter alone moved", r1.pk[L_CHAT] > 0 && r1.pk[L_KICK] == 0 && r1.pk[L_SNARE] == 0);
    }
#endif
}

/* ---- the record */
static void lanes_set(void)
{
    dlm_none();
    dlm_set_pan(0, -64);
    dlm_set_pan(7, 63);
    dlm_set_pan(15, -5);
    dlm_set_mute(3, 1);
    dlm_set_mute(12, 1);
    dlm_set_solo(9, 1);
}
static int lanes_are_set(void)
{
    return dlm_pan[0] == -64 && dlm_pan[7] == 63 && dlm_pan[15] == -5 && dlm_pan[1] == 0 && dlm_mute == ((1u << 3) | (1u << 12)) &&
           dlm_solo == (1u << 9) && dlm_heard == ((1u << 9) | 0x10000u);
}
static int lanes_default(void)
{
    uint32_t l, ok = dlm_mute == 0 && dlm_solo == 0 && dlm_heard == 0x1FFFFu;
    for (l = 0; l < DRUM_LANES; l++)
        ok &= dlm_pan[l] == 0;
    return (int)ok;
}
static void t_record(void)
{
    uint8_t b[FXR_MAX];
    uint32_t n;
    fxs_set(FXS_DEF);
    dlm_none();
    check("every lane as before: no record (a project as before writes nothing new)", fxr_encode(b) == 0u);
    lanes_set();
    n = fxr_encode(b);
    check("pans, mutes, a solo: a record with their TLV", n == FXR_HEAD + 2u + DLM_TLV_N && b[FXR_HEAD] == DLM_TLV);
    dlm_none();
    check("... decoded back the same", fxr_decode(b, n, 1) && lanes_are_set());
    check("... a song section (all 0) takes them too", (dlm_none(), fxr_decode(b, n, 0)) && lanes_are_set());
    b[FXR_HEAD + 1u] = 3u;
    n = FXR_HEAD + 5u;
    check("a TLV too short for them (a damaged one): every lane at the centre, heard", fxr_decode(b, n, 1) && lanes_default());
    lanes_set();
    check("no record (a project from before): every lane at the centre, heard", !fxr_decode(0, 0, 1) && lanes_default());
}
static void t_stores(void)
{
    int ok;
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    song.playing = 0, transport_req = 0;
    host_tracks_init();
    fxs_set(FXS_DEF);
    lanes_set();
    project_save(2);
    ok = !strcmp(last_msg, "SAVED");
    host_tracks_init();
    dlm_none();
    project_load(2);
    check("PROJECT SAVE C with lanes panned, muted, soloed; LOAD C: the same", ok && lanes_are_set());
    dlm_none();
    project_save(1);
    lanes_set();
    project_load(1);
    check("a project saved with every lane as before: loads at the centre, heard", lanes_default());
    lanes_set();
    project_load(2);
    slg_boot();
    dlm_none();
    project_load(2);
    check("after a restart: C loads its lanes", lanes_are_set());
}

int main(void)
{
    t_heard();
    t_mix();
    t_record();
    t_stores();
    printf(bad ? "drum lanes mix: %d FAILED\n" : "drum lanes mix: all ok\n", bad);
    return bad != 0;
}
