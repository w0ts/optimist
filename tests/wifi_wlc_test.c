/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The coexistence controller's packed table (firmware/hal/fm1_wifi_wlc.h): every word comes back as it was recorded. */
#include <stdio.h>
#include "../firmware/hal/fm1_wifi_wlc.h"
#include "wifi_wlc_ref.h"

int main(void)
{
    unsigned i, bad = 0, n = (unsigned)(sizeof wlc_ref / sizeof *wlc_ref);
    if (n != FM1_WLC_N) {
        printf("FAIL count %u vs %u\n", n, (unsigned)FM1_WLC_N);
        return 1;
    }
    for (i = 0; i < n; i++)
        if (FM1_WLC_WORD(i) != wlc_ref[i]) {
            bad++;
            printf("FAIL word %u: %08X, was %08X\n", i, (unsigned)FM1_WLC_WORD(i), (unsigned)wlc_ref[i]);
        }
    printf("wifi_wlc_test: %u words, %u differ\n", n, bad);
    return (int)bad;
}
