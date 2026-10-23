/* SPDX-License-Identifier: GPL-3.0-only */
/* The delta-coded lookup tables (tools/gen_tables.py carr, felucca_tables.h TAB_CODED): the decoder the target runs at
 * boot (tab_expand) fills every coded table with exactly the plain array's values, bit for bit, the stream read to its
 * end. Built with TAB_CODED_FORCE (the coded form on the host) and TAB_CODED_CHECK (the plain arrays beside it). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TAB_CODED_FORCE 1
#define TAB_CODED_CHECK 1
#define FELUCCA_TAB_CODED_ALL 1
#ifndef FELUCCA_ENG_FM6
#define FELUCCA_ENG_FM6 1
#endif
#include "felucca_tables.h"

static int fails;

#define CHECK(T) \
    if (sizeof(T) != sizeof(T##_RAW) || memcmp(T, T##_RAW, sizeof(T)) != 0) { \
        printf("FAIL %s: the decoded table differs from the plain one\n", #T); \
        fails++; \
    } else \
        n++;

int main(void)
{
    int n = 0;
    uint32_t i;
    tab_expand();
    TAB_CHECK_core(CHECK)
    TAB_CHECK_flash(CHECK)
    TAB_CHECK_fm6(CHECK)
    /* the streams end where the descriptors say: every byte decoded, none left (a trailing byte would be a table
     * entry the generator and the decoder disagree on) */
    for (i = 0; i < sizeof(TAB_DESC_core) / sizeof(TAB_DESC_core[0]); i++)
        if (!TAB_DESC_core[i].n || (TAB_DESC_core[i].w != 2u && TAB_DESC_core[i].w != 4u && TAB_DESC_core[i].w != 8u)) {
            printf("FAIL descriptor %u: n %u width %u\n", (unsigned)i, TAB_DESC_core[i].n, TAB_DESC_core[i].w);
            fails++;
        }
    if (SINE[256] != 32767 || SINE[768] != -32767 || SINE[0] != 0 || SINE[512] != 0) {
        printf("FAIL SINE's quarters: %d %d %d %d\n", SINE[0], SINE[256], SINE[512], SINE[768]);
        fails++;
    }
    printf("tab_coded: %d tables decoded as their plain arrays, %d failures\n", n, fails);
    return fails ? 1 : 0;
}
