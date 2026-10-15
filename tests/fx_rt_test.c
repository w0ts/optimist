/* SPDX-License-Identifier: GPL-3.0-only */
/* The returns R1 R2 R3 holding any type (fx-pool 2026-10-15; fx_slots.c fxs_fix / fxs_load / rt_tlv, fx.c rt_run): the
 * three send lines (CHO DLY REV) each owned by a return slot, a send type pinned to its own line, another type on the line
 * its slot had:
 *   layout   the default: no other type, every line live; PHSR into R1 (CHO's): on line 0, CHO no longer heard but its line
 *            is (the tracks' CHO sends feed PHSR), its amount the full-wet point, the send cards named PHSR; REV into R1
 *            swaps it with PHSR, each keeping its line; GATER refused
 *   audio    the line's send through the type, out to L and R, times the line's RET (0: nothing), its send bus fed
 *            silence; nothing sent: nothing out; removed: its tail ends, then nothing runs
 *   pool     FLNG (1 instance) on a return takes the pool's instance (owner R*)
 *   record   RET_TLV round trips (types, lines, amounts); the header holds the sends only; the default writes none
 * Run by tests/run_tests.sh (INS_PHASER INS_FLANGER on). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("fx_rt: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static int32_t any(const int32_t *b)
{
    int32_t i, o = 0;
    for (i = 0; i < CTL; i++)
        o |= b[i];
    return o;
}
/* one block of rt_run with the sine (amplitude a) sent on line j; 1: L == R */
static int blk(uint32_t j, int32_t a, uint32_t b0)
{
    int32_t *snd[3] = {send_c, send_d, send_r};
    uint32_t i, k, eq = 1;
    for (k = 0; k < 3u; k++)
        for (i = 0; i < CTL; i++)
            snd[k][i] = k == j ? (int32_t)lrint(a * sin(2 * M_PI * 330.0 * (b0 * CTL + i) / FS)) : 0;
    for (i = 0; i < CTL; i++)
        mix_l[i] = mix_r[i] = 0;
    rt_run(CTL);
    for (i = 0; i < CTL; i++)
        eq &= mix_l[i] == mix_r[i];
    return (int)eq;
}

int main(void)
{
    uint8_t b[FXR_MAX];
    uint32_t i, n, k, eq;
    int32_t peak;
    host_tracks_init();
    fxs_set(FXS_DEF);
    check("default: R1 R2 R3 = REV DLY CHO (the one order) on lines 2 1 0, no other type, every built line live",
          fxs_line[1] == 2 && fxs_line[2] == 1 && fxs_line[3] == 0 && !rt_any() && FXS_ON(FXT_CHO) == FELUCCA_FX_CHORUS &&
          FXS_ON(FXT_REV) == FELUCCA_FX_REVERB);
    check("... nothing to keep: no FX record", fxr_encode(b) == 0u);
    fxs_load(3, FXT_PHSR);
    check("PHSR into R3: on line 0 (CHO's), CHO not heard, its line is; amount 64 (full wet)",
          fxs_slot[3] == FXT_PHSR && fxs_line[3] == 0 && fxs_rty[0] == FXT_PHSR && !FXS_HAS(FXT_CHO) && FXS_ON(FXT_CHO) &&
          fxs_ramt[3] == 64 && rt_amt(0) == 64);
    check("... the tracks' CHO send card (card 3: R3's) is named PHSR; the others keep theirs",
          !strcmp(fxs_send_d(P_CHOR)->label, "PHSR") && fxs_send_d(P_DLY) == &TP[P_DLY] && fxs_send(2) == FXT_CHO &&
          fxs_card(2) == FXT_CHO && fxs_card(0) == FXT_REV);
    fxs_load(2, FXT_GATE);
    check("GATER refused on a return (a track's tempo values)", fxs_slot[2] == FXT_DLY);
    for (i = 0, eq = 1, peak = 0; i < 40u; i++) {
        eq &= (uint32_t)blk(0, 40000, i);
        for (k = 0; k < CTL; k++)
            peak = abs(mix_l[k]) > peak ? abs(mix_l[k]) : peak;
    }
    check("audio: the send through PHSR into L and R (the same: mono in), heard, bounded", eq && peak > 5000 && peak < 200000);
    check("... the line's send bus fed silence (send_c zeroed)", !any(send_c));
    blk(0, 0, 0);
    for (i = 0; i < 80u; i++)
        blk(0, 0, i);
    check("... nothing sent: nothing out (the tail ended)", !any(mix_l) && !any(mix_r));
    song.g[G_CRET] = 0;
    blk(0, 40000, 1);
    check("CHO RET (line 0's return) at 0: nothing out", !any(mix_l));
    song.g[G_CRET] = 127;
    fxs_load(3, FXT_REV);
    check("REV into R3: swaps with PHSR, each keeps its line (REV 2, PHSR 0); the cards follow (R1 PHSR's CHO line)",
          fxs_slot[3] == FXT_REV && fxs_line[3] == 2 && fxs_slot[1] == FXT_PHSR && fxs_line[1] == 0 &&
          fxs_rty[0] == FXT_PHSR && fxs_rty[2] == FXT_NONE && fxs_card(0) == FXT_CHO && fxs_card(2) == FXT_REV);
    fxs_load(2, FXT_FLNG);
    fxi_poll();
    check("FLNG into R2 (line 1): the pool's instance, owner R2's line", fxs_rty[1] == FXT_FLNG &&
          fxi_ostate(FXI_RT0 + 1u, FXT_FLNG) == FXI_OK && ifx_lim_at(FXT_FLNG, IFX_RT(1)) != FXI_NONE);
    for (i = 0; i < 20u; i++)
        blk(1, 40000, i);
    check("... heard", any(mix_l) != 0);
    fxs_ramt[1] = 77;
    n = fxr_encode(b);
    for (k = 0, eq = 1; k < FX_NSLOT; k++)
        eq &= b[2u + k] != FXT_PHSR && b[2u + k] != FXT_FLNG;
    check("record: the header holds the sends (and the tracks' inserts as before), never a return's other type; a RET_TLV",
          n > FXR_HEAD && b[5] == FXT_REV && eq);
    fxs_set(FXS_DEF);
    check("... the default again (a header alone: no other type)", !rt_any());
    fxr_decode(b, n, 1);
    check("... decoded: PHSR FLNG REV, lines 0 1 2, PHSR's amount 77", fxs_slot[1] == FXT_PHSR && fxs_slot[2] == FXT_FLNG &&
          fxs_slot[3] == FXT_REV && fxs_line[1] == 0 && fxs_line[2] == 1 && fxs_ramt[1] == 77 && fxs_rty[0] == FXT_PHSR);
    fxs_load(1, FXT_NONE);
    fxs_load(2, FXT_NONE);
    for (i = 0; i < RT_TAIL + 2u; i++)
        blk(0, 40000, i);
    check("removed: nothing out, the tails ended (no block runs)", !any(mix_l) && !rt_tail[0] && !rt_tail[1] && any(send_c));
    printf("fx_rt: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
