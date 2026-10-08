/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the user preset record (firmware/src/storage/upreset.c, -DUP_HOST part):
 * UP_PUT parsing, a bank round trip through storage.c on a simulated NOR,
 * bank / record version checks, map-by-count, pattern <-> steps. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)
#define UP_HOST 1
#include "../firmware/src/core/core.h"

static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
#include "../firmware/src/storage/upreset.c"

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static uint32_t put_frame(uint8_t *a, uint32_t slot, uint32_t eng, const char *name, int32_t base)
{
    uint32_t n = 0, i;
    a[n++] = (uint8_t)slot;
    a[n++] = (uint8_t)eng;
    for (i = 0; name[i]; i++)
        a[n++] = (uint8_t)name[i];
    a[n++] = 0;
    for (i = 0; i < P_COUNT; i++) {
        uint32_t u = (uint32_t)(base + (int32_t)i + 8192);
        a[n++] = u & 127u;
        a[n++] = (u >> 7) & 127u;
    }
    for (i = 0; i < 16u; i++) {
        a[n++] = (uint8_t)(i % 3u ? 40u + i : 0u);  /* rests on 0, 3, 6 .. */
        a[n++] = (uint8_t)(i == 3u ? 4u : i % 3u ? 1u : 2u);   /* step 3: tie; slide on a rest drops */
    }
    return n;
}

int main(void)
{
    uint8_t a[640];
    up_rec_t r, got;
    uint32_t n, slot = 99, i;
    int bad = 0, len, ok;
    int16_t v[P_COUNT], def[P_COUNT];
    memset(nor, 0xFF, sizeof nor);
    for (i = 0; i < P_COUNT; i++)
        def[i] = (int16_t)(1000 + i);

    n = put_frame(a, 5, 2, "Bass One", -40);
    bad += check("UP_PUT frame < 640 bytes", 5u + n + 1u < 640u);
    bad += check("UP_PUT parses", up_parse(a, n, &r, &slot) == 0 && slot == 5u && r.engine == 2u &&
                                      up_valid(&r) && !memcmp(r.name, "Bass One", 8) && !r.name[8]);
    ok = r.ver == UP_VER && r.np == P_ENG_END;              /* (the values after P_E7, if built: none in a record) */
    up_params(&r, v, def);                                  /* (UP_VER 2: ENV2's extras packed, up_vals_put) */
    for (i = 0; i < P_COUNT; i++)
        ok &= v[i] == (i < P_ENG_END ? (int16_t)(-40 + (int32_t)i) : def[i]);
    bad += check("UP_PUT values (negative v14 too)", ok);
    bad += check("pattern: rest drops flags, tie has no note",
                 r.note[0] == 0 && r.flags[0] == 0 && r.note[1] == 41 && r.flags[1] == 1 && r.note[3] == 0 &&
                     r.flags[3] == 4);
    bad += check("UP_PUT short frame -> args", up_parse(a, n - 1u, &r, &slot) == 1);
    n = put_frame(a, 32, 0, "X", 0);
    bad += check("UP_PUT slot 32 -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, NENGINES, "X", 0);
    bad += check("UP_PUT bad engine -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, 0, "", 0);
    bad += check("UP_PUT empty name -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, 0, "THIRTEEN CHRS", 0);
    bad += check("UP_PUT 13-char name -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, 0, "TWELVE CHARS", 0);
    bad += check("UP_PUT 12-char name ok", up_parse(a, n, &r, &slot) == 0 && !memcmp(r.name, "TWELVE CHARS", 12));
    {
        char nm[13];
        *up_rec(31) = r;
        memcpy(up_rec(31)->name, "Low case", 9);
        up_name(31, nm);
        bad += check("name shown upper case", !strcmp(nm, "LOW CASE"));
    }

    /* bank round trip through storage.c */
    n = put_frame(a, 17, 3, "Keys", 7);
    up_parse(a, n, &r, &slot);
    up_bank[1].magic = UP_BANK_MAGIC;
    up_bank[1].rsize = sizeof(up_rec_t);
    up_bank[1].nslot = UP_PER_BANK;
    *up_rec(17) = r;
    bad += check("bank fits one object", sizeof(up_bank_t) <= ST_PAYLOAD_MAX);
    bad += check("bank save", st_save(OBJ_UPRESET0 + 1, &up_bank[1], sizeof up_bank[1]) == 0);
    memset(up_bank, 0, sizeof up_bank);
    len = st_load(OBJ_UPRESET0 + 1, &up_bank[1], sizeof up_bank[1]);
    up_bank_check(1, len);
    got = *up_rec(17);
    bad += check("bank load: the record is back", up_used(17) && !memcmp(&got, &r, sizeof r));
    bad += check("other slots empty", !up_used(16) && !up_used(18) && !up_used(0));
    len = st_load(OBJ_UPRESET0, &up_bank[0], sizeof up_bank[0]);
    up_bank_check(0, len);
    bad += check("bank 0 never written -> empty", len < 0 && !up_used(0) && up_bank[0].magic == 0);
    bad += check("banks in 0xDC000..0xDFFFF", st_sector(OBJ_UPRESET0, 0) == 0xDC000u &&
                                                   st_sector(OBJ_UPRESET0 + 1, 1) == 0xDF000u &&
                                                   st_sector(OBJ_PROJECT0 + 3, 1) + 4096u <= 0xA0000u);
    up_bank[1].rsize = 190;                                 /* another record layout */
    up_bank_check(1, (int)sizeof up_bank[1]);
    bad += check("bank with another record size -> empty", !up_used(17));
    up_bank[1].magic = UP_BANK_MAGIC;
    up_bank[1].rsize = sizeof(up_rec_t);
    up_bank[1].nslot = UP_PER_BANK;
    *up_rec(17) = r;
    up_rec(17)->ver = UP_VER + 1u;
    bad += check("record with another version -> empty", !up_used(17));

    /* map by count (UP_VER 1 records, one value a word): a record from a build with fewer parameters (ANALOG 2:
     * FUN8's 69, before ENV2's extras; else 2 fewer) */
    r.ver = 1;
    {
        uint32_t np = FELUCCA_ANALOG2 ? 69u : P_COUNT - 2u, nc = np - 8u;
        r.np = (uint8_t)np;
        for (i = 0; i < np; i++)
            r.p[i] = (int16_t)i;
        up_params(&r, v, def);
        ok = up_valid(&r);
        for (i = 0; i < P_E0; i++)
            ok &= v[i] == (i < nc ? (int16_t)i : def[i]);
        for (i = 0; i < 8u; i++)
            ok &= v[P_E0 + i] == (int16_t)(nc + i);
    }
    bad += check("np < P_COUNT: mapped by count", ok);
    /* a record saved before the SLICER (P_COUNT 53, P_E0 45): the four SLICER parameters (just
     * before P_E0) take their defaults, everything else keeps its id */
    r.np = 53;
    for (i = 0; i < 53u; i++)
        r.p[i] = (int16_t)(2000 + i);
    up_params(&r, v, def);
    ok = P_SLCR == 45 && P_SLDEPTH + 1 == P_CHORD && P_CHORD == 49 && P_FXOFF == 50 && P_E0 == 51 + 16 * FELUCCA_ANALOG2;   /* (ANALOG 2: its ten, ENV2's six) */
    for (i = 0; i < 45u; i++)
        ok &= v[i] == (int16_t)(2000 + i);
    for (i = P_SLCR; i < P_E0; i++)
        ok &= v[i] == def[i];
    for (i = 0; i < 8u; i++)
        ok &= v[P_E0 + i] == (int16_t)(2000 + 45 + i);
    bad += check("old record (np 53): SLICER and CHORD defaults, E0..E7 kept", ok);
    /* a record of SLOOP 1.0 (P_COUNT 57, P_E0 49): CHORD (SLOOP 2.0) takes its default */
    r.np = 57;
    for (i = 0; i < 57u; i++)
        r.p[i] = (int16_t)(3000 + i);
    up_params(&r, v, def);
    ok = 1;
    for (i = 0; i < P_CHORD; i++)
        ok &= v[i] == (int16_t)(3000 + i);
    ok &= v[P_CHORD] == def[P_CHORD] && v[P_FXOFF] == def[P_FXOFF];
    for (i = 0; i < 8u; i++)
        ok &= v[P_E0 + i] == (int16_t)(3000 + 49 + i);
    bad += check("SLOOP 1.0 record (np 57): CHORD, FX defaults, the rest kept", ok);
    /* a record of SLOOP 2.0 .. 2.2 (P_COUNT 58, P_E0 50): the FX bypass takes its default (the effects on) */
    r.np = 58;
    for (i = 0; i < 58u; i++)
        r.p[i] = (int16_t)(4000 + i);
    up_params(&r, v, def);
    ok = v[P_FXOFF] == def[P_FXOFF];
    for (i = 0; i < P_FXOFF; i++)
        ok &= v[i] == (int16_t)(4000 + i);
    for (i = 0; i < 8u; i++)
        ok &= v[P_E0 + i] == (int16_t)(4000 + 50 + i);
    bad += check("SLOOP 2.x record (np 58): FX on, the rest kept", ok);
    {   /* today's record (up_vals_put): every value back as given */
        int16_t w[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            w[i] = (int16_t)(i * 5u % 128u) - 64;
        up_vals_put(&r, w);
        up_params(&r, v, def);
        ok = r.np == P_ENG_END && r.ver == UP_VER && up_valid(&r) && !memcmp(v, w, P_ENG_END * sizeof w[0]) &&
             UP_NS(P_ENG_END) <= UP_PMAX;
        for (i = P_ENG_END; i < P_COUNT; i++)               /* (the values after P_E7: the defaults) */
            ok &= v[i] == def[i];
    }
    bad += check("np == P_ENG_END: as stored (the values after P_E7 are not)", ok);

    {   /* steps -> pattern (UP_STORE) */
        step_t st[NSTEP];
        memset(st, 0, sizeof st);
        for (i = 0; i < 16u; i++)
            st[i].time = ST_REST;
        st[0] = (step_t){{60, 64, 67, 0}, 3, ST_NOTE, SF_ACCENT | SF_SLIDE, 100};
        st[1] = (step_t){{0}, 0, ST_TIE, 0, 0};
        st[2] = (step_t){{50}, 0, ST_NOTE, SF_ACCENT, 0};   /* n = 0: empty */
        st[20] = (step_t){{70}, 1, ST_NOTE, 0, 90};         /* beyond 16: not stored */
        up_pat_from(&r, st);
        bad += check("steps -> pattern", r.note[0] == 60 && r.flags[0] == 3 && r.note[1] == 0 && r.flags[1] == 4 &&
                                             r.note[2] == 0 && r.flags[2] == 0 && !up_pat_empty(&r));
        memset(st, 0, sizeof st);
        up_pat_from(&r, st);
        bad += check("empty sequencer -> empty pattern", up_pat_empty(&r));
    }
    printf("%s\n", bad ? "USER PRESET TEST FAILED" : "user preset test passed");
    return bad != 0;
}
