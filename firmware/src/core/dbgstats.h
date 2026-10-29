/* SPDX-License-Identifier: GPL-3.0-only */
/* DEBUG STATS (SYSTEM > DEBUG): fm1-base keeps a snapshot of the load, the late halves, the stacks and the radios in
 * flash every 5 minutes while it is on (its device/dbgstats.c, plat.h plat_stats_enable; the last 60 read over USB with
 * fm1-base's tools/fm1_remote.py stats), for a unit that misbehaves only after hours of play. OFF by default; kept in
 * the settings record, one byte (storage/persist_rec.c rsv_pj[1]: a record from before reads 0, OFF). Only where the
 * platform keeps them (the release: fm1-base's plat.h, PLAT_STATS_APP): Optimist's own build has no line (DBG_STATS 0).
 * Included by ui/panel.c (before the storage and the Optimist UI) and so by the host tests. */
#ifndef FELUCCA_DBGSTATS_H
#define FELUCCA_DBGSTATS_H
#include <stdint.h>
#ifdef PLAT_STATS_APP
#define DBG_STATS 1
#else
#define DBG_STATS 0
#endif
static uint8_t dbg_stats;                           /* 0 OFF, 1 ON */
static void dbg_stats_set(uint32_t on)              /* the setting, and the platform told (at boot: after the record) */
{
    dbg_stats = (uint8_t)(on != 0u);
#if DBG_STATS
    plat_stats_enable(dbg_stats);
#endif
}
#endif
