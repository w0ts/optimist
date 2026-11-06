/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM > SD STREAM (SD card mode with streaming; the user, 2026-11-05: "we should totally monitor how we are doing on
 * the SD read rate and sample playback somehow"): four lines, the numbers storage/sd/sd_mon.c keeps in the main loop.
 *   READ     KB/s the card gave in the last half second; the bar against what the kept clock can carry (SMN_PM: 840 KB/s
 *            at 8.57 MHz), the tick the peak of the last 10 s; green, amber from 60 %, red from 85 % of it (INFO's CPU colours)
 *   STREAMS  voices streaming now / the most in the last 10 s (the bar: of the SST_N rings, the tick the most)
 *   BUFFER   the least a ring holds of the streams the card feeds, percent of the ring, now / the least of the last 10 s
 *            ("42/0", STREAMS' style; the tick the 10 s least): the early warning before a dropout; the colour follows the
 *            10 s least (amber under 25, red at 0: a drum kit's rings start empty, so its "now" is the live reading);
 *            "-" while no stream is fed
 *   DRY      streams that ran dry (green 0, red for 10 s after one, amber after), GRAIN's and STRETCH's late and missed
 *            reads beside it; the line takes turns, as WI-FI's STATE, with NO RING (notes with no ring free), CRC FAILS and
 *            STEPS DOWN (the clock walk's run-time steps) once any of them is not 0; a dry stream keeps the line on DRY
 * The knob of DRY steps the line's items and holds it 30 s (op_wifi.c wf_ph); a turn of any other knob, YES and SAVE clear the
 * counts (the peaks too; the counters themselves go on: the console's sd stream reads them since boot). */
#include "../../storage/sd/sd_mon.h"

#define SMN_PM 98u                                      /* KB/s the card gives a kHz of clock, in thousandths: 8571 kHz -> 840 KB/s (measured) */
#define SMN_PH_SH 10                                    /* the DRY line's phase: 2^10 ms, about 1 s (WI-FI's STATE) */
#define SMN_PH_HOLD_MS 30000u
static struct { uint32_t at, ph; uint8_t on; } smn_p;
static int smn_item(uint32_t it) { return it >= SI_SMRATE && it <= SI_SMDRY; }
static uint32_t smn_cap(void)                           /* what the kept clock carries, KB/s (the floor's when unknown) */
{
    uint32_t k = sd_clock_khz();
    return (k < 1000u ? 6000u : k) * SMN_PM / 1000u;
}
static uint32_t smn_pct(uint32_t v, uint32_t cap) { return v >= cap ? 100u : v * 100u / cap; }   /* cap is not 0: 588 and up */
static uint32_t smn_held(void) { return smn_p.on && fm1_ms - smn_p.at < SMN_PH_HOLD_MS; }
static uint32_t smn_ph(void)                            /* the DRY line's phase: the clock's, or the one the knob stepped to */
{
    if (smn_held())
        return smn_p.ph;
    smn_p.on = 0;
    return fm1_ms >> SMN_PH_SH;
}
static uint32_t smn_items(const ssm_view_t *v, uint8_t *it)   /* the DRY line's items: 0 DRY, 1 NO RING, 4 CUT, 2 CRC FAILS, 3 STEPS DOWN */
{
    uint32_t n = 0;
    it[n++] = 0;
    if (v->nos)
        it[n++] = 1;
    if (v->cut)
        it[n++] = 4;
    if (v->crc)
        it[n++] = 2;
    if (v->demo)
        it[n++] = 3;
    return n;
}
static void smn_dry(const ssm_view_t *v, cell_t *c)
{
    static char u[16];
    static const char *const NM[5] = {"DRY", "NO RING", "CRC FAILS", "STEPS DOWN", "CUT"};
    uint8_t it[5];
    uint32_t n = smn_items(v, it), k = it[v->dry_hot && !smn_held() ? 0u : smn_ph() % n], x = k == 0u ? v->dry : k == 1u ? v->nos : k == 2u ? v->crc : k == 3u ? v->demo : v->cut;
    sys_ro(c, NM[k]);
    fmt_int(c->val, (int32_t)(x < 99999u ? x : 99999u));
    c->col = !x ? C_OK : k == 0u && v->dry_hot ? C_ERR : C_WARN;
    c->unit = "";
    if (k == 0u && (v->late || v->miss)) {              /* (GRAIN, STRETCH: reads that came late / were given up, beside it) */
        str_cpy(u, "LATE ", sizeof u);
        fmt_app(u, (int32_t)(v->late < 99u ? v->late : 99u));
        str_app(u, " MISS ", sizeof u);
        fmt_app(u, (int32_t)(v->miss < 99u ? v->miss : 99u));
        c->unit = u;
    }
}
static void smn_cell(uint32_t it, cell_t *c)
{
    ssm_view_t v;
    uint32_t cap = smn_cap(), p;
    ssm_view(&v);
    switch (it) {
    case SI_SMRATE:
        sys_ro(c, "READ");
        fmt_int(c->val, (int32_t)v.kbs);
        c->unit = "KB/S";
        p = smn_pct(v.kbs, cap);
        c->col = load_col((int32_t)p);
        cell_gauge(c, 0, 0, 100, (int32_t)p);
        c->pkbar = 1;
        c->hold[0] = (int16_t)smn_pct(v.kbs_pk, cap);
        break;
    case SI_SMSTRM:
        sys_ro(c, "STREAMS");
        fmt_int(c->val, (int32_t)v.n);
        str_app(c->val, "/", sizeof c->val);
        fmt_app(c->val, (int32_t)v.n_pk);
        cell_gauge(c, 0, 0, 100, (int32_t)(v.n * 100u / SST_N));
        c->pkbar = 1;
        c->hold[0] = (int16_t)(v.n_pk * 100u / SST_N);
        break;
    case SI_SMBUF:
        sys_ro(c, "BUFFER");
        p = v.fill_lo;
        if (p == SSM_NO) {
            cell_val(c, "-");
            c->col = C_DIM;
            cell_gauge(c, 0, 0, 100, 0);
            break;
        }
        fmt_int(c->val, (int32_t)(v.fill == SSM_NO ? p : v.fill));
        str_app(c->val, "/", sizeof c->val);
        fmt_app(c->val, (int32_t)p);
        c->unit = "%";
        c->col = !p ? C_ERR : p < 25u ? C_WARN : C_OK;
        cell_gauge(c, 0, 0, 100, (int32_t)(v.fill == SSM_NO ? p : v.fill));
        c->pkbar = 1;
        c->hold[0] = (int16_t)p;
        break;
    default:
        smn_dry(&v, c);
        break;
    }
}
static void smn_clear(void)
{
    ssm_clear();
    ui_message("COUNTERS CLEARED");
}
static void smn_turn(uint32_t it, int32_t s)            /* a knob: DRY's steps its items; the others, and SHIFT + any, clear */
{
    if (it != SI_SMDRY || s == OP_RESET) {
        smn_clear();
        return;
    }
    smn_p.ph = smn_ph() + (uint32_t)s;
    smn_p.at = fm1_ms;
    smn_p.on = 1;
}
