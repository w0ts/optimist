/* SPDX-License-Identifier: GPL-3.0-only */
/* DRONE's per-part state (eng_drone.c): its shape alone */
#pragma once
enum { DR_CPL, DR_TOPO, DR_CHR, DR_DRF, DR_TONE, DR_NC };   /* the controls (eng_drone.c) */
/* each part's network: the ears' phases and last outputs, their DC blocks and low-passes; the shared drift phases,
 * smoothed increments and controls; its latched voices (HOLD) */
typedef struct {
    uint32_t ph[2][4];
    int32_t prev[2][4];
    int32_t low[2], dci[2], dco[2];
    uint32_t dph[4];
    int32_t inc[4], ctl[DR_NC];
    uint32_t ready;                       /* 0: the next render installs the pitch and the controls (no glide in) */
    uint32_t latched;                     /* HOLD: the voices whose key went up (voice.c drone_latch_off), bit i voice i */
    uint8_t lnote[NVOICE];                /* (and the note each latched: MONO may move the voice to another key since) */
} drone_pt_t;
/* the part's state: its network (pt), its side this block (BEAT: (L - R) / 2, mixed by fx.c mix_part) and whether it
 * was written this block (sd: drone_block clears it, the voice zeroes the block). Built in: one static instance a part
 * (voice.c's HOLD and fx.c's side read it through DRONE_MEM) */
typedef struct {
    drone_pt_t pt;
    int32_t side[CTL];
    uint8_t sd;
} drone_mem_t;
