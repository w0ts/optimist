/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c: a knob turning a slot's TYPE onto the type the other slot holds skips it, never takes it
 * (the user, 2026-10-18: "when s1 is on effect A, and i scroll thru effects for s2, it should not STEAL FX from S1, it
 * should skip it. same for all inserts"): the master's slots (the master FX screen, the mixer's MASTER row), a drum
 * lane's (its pages, the mixer's lane rows, drum_sends.c dsend_set); a track's: ins_skip (ui_optimist_inslots.h). */
static int ns_ix(const uint8_t *list, uint32_t n, uint32_t t)   /* t's entry in a list, -1 none */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (list[i] == t)
            return (int)i;
    return -1;
}
/* the other built type of the list but t: the first one past FXT_NONE */
static uint32_t ns_other(const uint8_t *list, uint32_t n, uint32_t t)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (list[i] != FXT_NONE && list[i] != t)
            return list[i];
    return FXT_NONE;
}
static void noswap_tests(void)
{
    int16_t *p;
    uint32_t a, b, n;
    int ia, ib;
    reset_ui();
    /* the master: S1 a, S2 b; S2 turned onto a's entry, from either side */
    (void)mst_desc(0, &p);
    n = (uint32_t)mst_d[0].max + 1u;
    a = ns_other(mst_list, n, FXT_NONE);
    b = ns_other(mst_list, n, a);
    {
        uint8_t s[INS_N] = {(uint8_t)a, (uint8_t)b};
        mst_put(s);
    }
    (void)mst_desc(1, &p);
    ia = ns_ix(mst_list, n, a), ib = ns_ix(mst_list, n, b);
    check(a != FXT_NONE && b != FXT_NONE && ia >= 0 && ib >= 0, "(the master: two built insert types)");
    fxs_type_set(FXS_ID_MST + 1u, ia);
    check(mst_slot[0] == a && mst_slot[1] != a && mst_slot[1] != b,
          "master S2 turned onto S1's type: S1 keeps it, S2 skips to the next (no swap, 2026-10-18)");
    {
        uint8_t s[INS_N] = {(uint8_t)a, (uint8_t)b};
        mst_put(s);
    }
    check(mst_type_turn(1, ia) != ia && mst_slot[0] == a, "... the mixer's MASTER row the same (mst_type_turn)");
    /* a drum lane: S1 a, S2 b; S2's TYPE (dsend id 6) turned onto a's entry */
    (void)lin_desc(0, 3, &p);
    n = (uint32_t)lin_td[1].max + 1u;
    a = ns_other(lin_list, n, FXT_NONE);
    b = ns_other(lin_list, n, a);
    {
        uint8_t s[INS_N] = {(uint8_t)a, (uint8_t)b};
        lin_put(0, s);
    }
    (void)lin_desc(0, 3, &p);
    ia = ns_ix(lin_list, n, a);
    check(a != FXT_NONE && b != FXT_NONE && ia >= 0, "(a drum lane: two built insert types)");
    dsend_set(0, 6, ia);
    check(lin_slot[0][0] == a && lin_slot[0][1] != a, "drum lane S2 turned onto S1's type: S1 keeps it, S2 skips it");
    {
        uint8_t s[INS_N] = {FXT_NONE, FXT_NONE};
        lin_put(0, s);
    }
    reset_ui();
}
