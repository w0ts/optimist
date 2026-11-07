/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID's running state of a part (acid_dsp.c: a bass303_t, float, its own unit) lives in the part's engine memory
 * (engines.c part_mem, the pool: a part plays one engine at a time). The integer unit cannot see bass303_t: it keeps
 * this many bytes for it, acid_dsp.c checks that they hold one */
#pragma once
#define ACID_MEM_BYTES 480u
void *acid_mem(int k);                             /* engines.c: part k's engine memory (k < NPART) */
void acid_forget(int k);                           /* acid_dsp.c: part k's state is gone, built afresh at its next use */
