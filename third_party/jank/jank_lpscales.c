/* SPDX-License-Identifier: GPL-3.0-only */
/* OPTIMIST'S OWN (not copied from jank: tools/sync_jank.sh leaves it): the FM-1's scales for the Launchpad (jank_lpscales.h). Built in
 * Optimist's unit after seq/seq.c and core/params.c: the masks are SCALE_MASK's and the names N_SCALE's, one list for both. */
#include "jank_lpscales.h"
_Static_assert(sizeof SCALE_MASK / sizeof SCALE_MASK[0] == JLS_COUNT && sizeof N_SCALE / sizeof N_SCALE[0] == JLS_COUNT,
               "the Launchpad's scales are SCL's");
static struct jln_scale jls_list[JLS_COUNT];
static uint8_t jls_built;
static void jls_build(void)
{
    unsigned i, k;
    for (i = 0; i < JLS_COUNT; i++) {
        struct jln_scale *s = &jls_list[i];
        s->name = N_SCALE[i];
        s->n = 0;
        for (k = 0; k < 12u && s->n < JLN_SCALE_MAX; k++)
            if ((SCALE_MASK[i] >> k) & 1u)
                s->iv[s->n++] = (uint8_t)k;
    }
    jls_built = 1;
}
unsigned jls_count(void) { return JLS_COUNT; }
const struct jln_scale *jls_get(unsigned i)
{
    if (!jls_built)
        jls_build();
    return i < JLS_COUNT ? &jls_list[i] : (const struct jln_scale *)0;
}
int jls_index_of(const struct jln_scale *s)
{
    if (!jls_built)
        jls_build();
    return s >= jls_list && s < jls_list + JLS_COUNT ? (int)(s - jls_list) : -1;
}
unsigned jls_mask(unsigned i) { return i < JLS_COUNT ? SCALE_MASK[i] : 0u; }
