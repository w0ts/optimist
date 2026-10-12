/* SPDX-License-Identifier: GPL-3.0-only */
/* An LFO's destination slots, shared by the LFOs that use them: the drum lanes' LFOs now (drums/lane_play.c), the synth
 * parts' next (the user, 2026-10-12: two slots replace PIT FLT SHP AMP there too). Header only: the type, the active
 * test, the summed amount per destination, the scale of the LFO onto a value's range, the waves.
 *
 *   slot        a destination and a signed amount (-64..63, as P_LD_PIT). The destination is the owner's own list:
 *               0 OFF, 1.. its destinations (the lanes: drum_edit.c DE_* + 1). Two slots on one destination add.
 *   on          an LFO whose every slot is OFF or at amount 0 is OFF: its owner computes nothing for it (no tick, no
 *               value, no work at a hit). lfo_slots_on is the test.
 *   the effect  lfo_slot_mod: the LFO (Q15) x the amount onto a value whose range is span (max - min): +-span / 2 at
 *               amount 64 with the LFO at its peak, so a full LFO on a value at its middle sweeps its whole range. The
 *               owner clamps the sum into the value's range. span x 128 x 32768 must fit an int32: span <= 255.
 *   the waves   lfo_shape: P_LWAVE's SIN TRI SAW SQR S&H (as voice.c lfo_wave, which takes them from a track), Q15;
 *               S&H holds rnd (a value drawn once a cycle by the owner). */
#ifndef FELUCCA_LFO_SLOT_H
#define FELUCCA_LFO_SLOT_H

#define LFO_NSLOT 2u
#define LFO_AMT_MIN (-64)
#define LFO_AMT_MAX 63
#define LFO_DEST_OFF 0u
#define LFO_WAVES 5u                     /* SIN TRI SAW SQR S&H (params.c N_LWAVE) */
#define LFO_WAVE_SH 4u
typedef struct {
    uint8_t dest;                        /* 0 OFF, 1.. the owner's destination + 1 */
    int8_t amt;                          /* LFO_AMT_MIN..LFO_AMT_MAX, 0: none */
} lfo_slot_t;
_Static_assert(sizeof(lfo_slot_t) == 2u, "a slot: two bytes");

/* slot s acts (a destination and an amount) */
static inline int lfo_slot_live(const lfo_slot_t *s) { return s->dest != LFO_DEST_OFF && s->amt != 0; }
/* the LFO of these LFO_NSLOT slots is computed at all */
static inline int lfo_slots_on(const lfo_slot_t *s)
{
    uint32_t i;
    for (i = 0; i < LFO_NSLOT; i++)
        if (lfo_slot_live(&s[i]))
            return 1;
    return 0;
}
/* the destinations the live slots act on: bit d (1..31) */
static inline uint32_t lfo_slots_mask(const lfo_slot_t *s)
{
    uint32_t i, m = 0;
    for (i = 0; i < LFO_NSLOT; i++)
        if (lfo_slot_live(&s[i]) && s[i].dest < 32u)
            m |= 1u << s[i].dest;
    return m;
}
/* the summed amount of the live slots on destination d (1..): -128..126 */
static inline int32_t lfo_slots_amt(const lfo_slot_t *s, uint32_t d)
{
    uint32_t i;
    int32_t a = 0;
    for (i = 0; i < LFO_NSLOT; i++)
        if (s[i].dest == d)
            a += s[i].amt;
    return a;
}
/* the live slots as (destination, amount) pairs to apply, one pair a destination (two slots on one: their amounts summed,
 * so the owner adds then clamps once) -> how many (0..LFO_NSLOT), d[] the destinations (1..), a[] the amounts */
static inline uint32_t lfo_slots_pairs(const lfo_slot_t *s, uint32_t *d, int32_t *a)
{
    uint32_t i, j, n = 0;
    for (i = 0; i < LFO_NSLOT; i++) {
        if (!lfo_slot_live(&s[i]))
            continue;
        for (j = 0; j < n && d[j] != s[i].dest; j++)
            ;
        if (j < n) {
            a[j] += s[i].amt;
        } else {
            d[n] = s[i].dest, a[n] = s[i].amt;
            n++;
        }
    }
    return n;
}
/* a slot read from outside (a stored record, the editor): inside its ranges; ndest: the owner's destinations (a
 * destination past them: OFF) */
static inline lfo_slot_t lfo_slot_fix(uint32_t dest, int32_t amt, uint32_t ndest)
{
    lfo_slot_t s;
    s.dest = (uint8_t)(dest <= ndest ? dest : LFO_DEST_OFF);
    s.amt = (int8_t)(amt < LFO_AMT_MIN ? LFO_AMT_MIN : amt > LFO_AMT_MAX ? LFO_AMT_MAX : amt);
    return s;
}
/* the LFO's value lfo (Q15) at summed amount amt onto a value whose range is span (<= 255): what to add to it */
static inline int32_t lfo_slot_mod(int32_t lfo, int32_t amt, int32_t span) { return (lfo * amt * span) >> 22; }

/* wave w at phase ph (Q15); rnd: S&H's value this cycle (the top 16 bits used) */
static inline int32_t lfo_shape(uint32_t w, uint32_t ph, uint32_t rnd)
{
    switch (w) {
    case 1:
        return osc_tri(ph);
    case 2:
        return (int32_t)(ph >> 16) - 32768;
    case 3:
        return ph < 0x80000000u ? 32767 : -32767;
    case LFO_WAVE_SH:
        return (int32_t)(rnd >> 16) - 32768;
    default:
        return osc_sine(ph);
    }
}
/* S&H's value for cycle c of LFO k: a hash, the same on every run and on either core (no rng() draw at a hit) */
static inline uint32_t lfo_rnd_of(uint32_t c, uint32_t k)
{
    uint32_t x = c * 2654435761u ^ (k + 1u) * 0x9E3779B9u;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    x *= 0x297A2D39u;
    return x ^ (x >> 15);
}

#endif
