/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/fmem_fake.h's pause hooks, after the firmware they read (the transport, the parts' quiet): included after
 * hostsim.c and model.c */
static int tf_playing(void) { return song.playing; }
static void tf_pause(void) { paused++; }            /* (the transport: ui/opt_feat.h of_pause; the voices: feat_mem.c) */
static int tf_quiet(void)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (!part_quiet(k))
            return 0;
    return 1;
}

