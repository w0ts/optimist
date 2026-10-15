/* SPDX-License-Identifier: GPL-3.0-only */
/* The master DUST and DJ filter as they were before fx-rework (2026-10-14, fx.c at 416f5c2a, verbatim but the names and
 * the RAM attributes): tests/fx_slots_test.c t_master checks that the DUST and FILTER insert types on the master slots
 * give these samples exactly. */
/* ---- DUST: the master through an old sampler and a record. G_DUST 0..127 turns up together: drive
 * into a soft clip, a lower sample rate (held samples, 44.1 -> 11 kHz), fewer bits (15 -> 8), a
 * one-pole low-pass (open -> ~3 kHz), a little hiss and crackle. The hiss and the crackle are the
 * record turning: they fade in with PLAY and out (~0.1 s) at STOP, so a stopped SLOOP is silent.
 * Stereo, ~25 ops a sample. */
static struct {
    int32_t hl, hr, hn;                                 /* held samples, samples left to hold */
    int32_t ll, lr;                                     /* low-pass states */
    int32_t rnd, click;                                 /* noise state, a crackle decaying */
    int32_t bed;                                        /* hiss / crackle level, Q15: 0 stopped, 32767 playing */
} ref_dust = {0, 0, 0, 0, 0, 0x2545F491, 0, 0};


static void ref_dust_process(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t d = song.g[G_DUST], hold, shift, a, drive, hiss, i, bed0, bed1;
    uint32_t pc;
    if (!d) {
        ref_dust.bed = 0;
        return;
    }
    hold = 1 + d * 3 / 127;
    shift = d / 18;
    a = 32767 - d * 165;                                /* one-pole coefficient, Q15 */
    drive = 4096 + d * 24;                              /* Q12: 1x .. 1.75x */
    hiss = d * 2;
    pc = (uint32_t)d * 7u;                              /* crackle: chance per sample, x 2^-22 */
    bed0 = ref_dust.bed;                                    /* ~0.1 s from 0 to full (238 a block of 32) */
    bed1 = ref_dust.bed = clamp(ref_dust.bed + (song.playing ? 238 : -238), 0, 32767);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = l[i], y = r[i];
        int32_t bed = bed0 + (((bed1 - bed0) * i) >> CTL_LOG2);
        uint32_t nz = noise32(&ref_dust.rnd);
        if (--ref_dust.hn <= 0) {                           /* sample and hold, then the bits */
            ref_dust.hn = hold;
            ref_dust.hl = softclip(((x >> 2) * drive) >> 10);
            ref_dust.hr = softclip(((y >> 2) * drive) >> 10);
            if (shift) {
                ref_dust.hl = crush_tz(ref_dust.hl, shift);     /* fewer bits, toward 0: no DC from tails */
                ref_dust.hr = crush_tz(ref_dust.hr, shift);
            }
        }
        ref_dust.ll += mulq15(ref_dust.hl - ref_dust.ll, a);
        ref_dust.lr += mulq15(ref_dust.hr - ref_dust.lr, a);
        if ((nz >> 10) < pc)                            /* a speck of dust */
            ref_dust.click = mulq15(((int32_t)(nz & 0x3FFu) - 512) * d / 8, bed);
        x = ref_dust.ll + ref_dust.click + mulq15(((int32_t)(nz >> 16) - 32768) * hiss >> 15, bed);
        y = ref_dust.lr + ref_dust.click + mulq15(((int32_t)(nz & 0xFFFFu) - 32768) * hiss >> 15, bed);
        ref_dust.click -= ref_dust.click >> 2;
        l[i] = x;
        r[i] = y;
    }
}

/* ---- the DJ filter on the master: G_FILT < 0 a low-pass closing, > 0 a high-pass opening, 0 off.
 * The cutoff glides to the knob (no zipper); at 0 it opens fully, then the filter is bypassed. */
static struct {
    int32_t cut;                                        /* now, 0..127 << 8 (CUTOFF_HZ index) */
    int8_t mode;                                        /* -1 LP, 1 HP, 0 off */
    int32_t l1, l2, r1, r2;
} ref_djf;

/* a side starts (from open, or the other side): in flash, a block now and then (FAR from ref_djf_process, RAM code) */
static void ref_djf_start(int32_t v, int32_t l0, int32_t r0)
{
    ref_djf.l1 = ref_djf.r1 = 0;
    if (v < 0) {
        ref_djf.mode = -1;
        ref_djf.cut = 127 << 8;
        ref_djf.l2 = clamp(l0, -140000, 140000);            /* (SLOOP 2.6 d30d655 fx.c djf_run, GPL-3.0-only) the LP starts */
        ref_djf.r2 = clamp(r0, -140000, 140000);            /* at rest on its input: its output is the input (DC: exact), */
    } else {                                            /* not 0 under a sounding mix (a click) */
        ref_djf.mode = 1;
        ref_djf.cut = 0;
        ref_djf.l2 = ref_djf.r2 = 0;
    }
}
static void ref_djf_process(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t v = song.g[G_FILT], to, i;
    tsvf_t c;
    if ((v < 0 && ref_djf.mode >= 0) || (v > 0 && ref_djf.mode <= 0))   /* (switching side: from open) */
        ref_djf_start(v, l[0], r[0]);                  /* (n > 0: a block) */
    if (!ref_djf.mode)
        return;
    to = ref_djf.mode < 0 ? (v < 0 ? (127 << 8) + v * 90 * 4 : 127 << 8) : (v > 0 ? v * 90 * 4 : 0);
    ref_djf.cut += clamp(to - ref_djf.cut, -384, 384);          /* ~1.5 index a block */
    if (!v && ref_djf.cut == to) {
        ref_djf.mode = 0;                                   /* fully open again: off */
        return;
    }
    tsvf_coef(&c, ref_djf.cut, 40);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = clamp(l[i], -140000, 140000), y = clamp(r[i], -140000, 140000);
        int32_t fl = tsvf_lp(&c, x, &ref_djf.l1, &ref_djf.l2), fr = tsvf_lp(&c, y, &ref_djf.r1, &ref_djf.r2);
        l[i] = ref_djf.mode < 0 ? fl : x - fl;
        r[i] = ref_djf.mode < 0 ? fr : y - fr;
    }
}

