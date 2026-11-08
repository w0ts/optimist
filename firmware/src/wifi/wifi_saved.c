/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The saved Wi-Fi networks, live: the one wn_store the firmware keeps, decoded from the settings record at start-up and
 * written back with it (storage/project.c persist_boot / settings_save, as bt_kept_in / bt_kept_out). Included from
 * felucca.c before project.c, for a build with FELUCCA_WIFI. wifi_nets.c is the logic (host-tested). */
#include "wifi_nets.c"

static struct wn_store wn_st IN_POOL;                     /* the saved networks and the AUTO / OFF mode */
static void wifi_nets_in(const uint8_t r[WN_STORE_SIZE])
{
    wn_load(&wn_st, r);
    if (wn_st.mode == WN_MODE_JANK || wn_st.mode == WN_MODE_JANK_AUTO)
        wn_st.mode = WN_MODE_AUTO;                      /* (MODE JANK is the trial's Wi-Fi stack's; this one joins saved networks only) */
    if (wn_st.mode == WN_MODE_JANK_HOST)
        wn_st.mode = WN_MODE_AP;                        /* (JANK HOST too: here the access point, AP AUTO) */
#if !FELUCCA_WIFI_AP
    if (wn_st.mode == WN_MODE_AP || wn_st.mode == WN_MODE_AP_MAN)
        wn_st.mode = WN_MODE_OFF;                       /* (a record from a build with MODE AP: this one has none, it would sit doing nothing) */
#endif
}
static void wifi_nets_out(uint8_t r[WN_STORE_SIZE]) { wn_save(&wn_st, r); }
