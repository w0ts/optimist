/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID's running state of a part (acid_dsp.c: a bass303_t, float, its own unit) lives in the part's engine memory
 * (engines.c part_mem, the pool: a part plays one engine at a time). The integer unit cannot see bass303_t: it keeps
 * this many bytes for it, acid_dsp.c checks that they hold one */
#pragma once
#define ACID_MEM_BYTES 480u
/* a plugin's part memory (eng_acid.c part_bytes; tools/plugin_build.py PLUG_ENG_ACID): the bass303_t's 480 B, then the
 * shared coefficients' copy (bass303.c bass303_k, 280 B) and the part's acid_st_t (acid_dsp.c checks they fit) */
#define ACID_PLUG_BYTES 784u
void *acid_mem(int k);                             /* engines.c: part k's engine memory (k < NPART) */
void acid_forget(int k);                           /* acid_dsp.c: part k's state is gone, built afresh at its next use */
