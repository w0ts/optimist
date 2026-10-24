/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the user preset record (firmware/src/storage/upreset.c, -DUP_HOST part):
 * UP_PUT parsing, a bank round trip through storage.c on a simulated NOR (UPB4),
 * bank / record version checks, map-by-count, a byte a value, pattern <-> steps. */
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
/* LFO DEST's four ids: their stored form is the depths of before or the slots' tagged layout (lfo_store.h; tests/lfo_slots_test.c) */
#if FELUCCA_ANALOG2
#define ENV_ID(i) ((i) == P_A2FENV || ((i) >= P_A2EPIT && (i) <= P_A2ESDT) || ((i) >= P_E1PIT && (i) <= P_NLAST))
#else
#define ENV_ID(i) ((i) >= P_E1PIT && (i) <= P_NLAST)
#endif
/* the values a record keeps in another form: LFO DEST's slots (lfo_store.h), ENV DEST's / ENV2 DEST's (env_store.h: their
 * depths, the slots) */
#define LFO_ID(i) (((i) >= P_L1DA && (i) <= P_L1AB) || ENV_ID(i))

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
        def[i] = (int16_t)(100 + i % 20u);

    n = put_frame(a, 5, 2, "Bass One", -40);
    bad += check("UP_PUT frame < 640 bytes", 5u + n + 1u < 640u);
    bad += check("UP_PUT parses", up_parse(a, n, &r, &slot) == 0 && slot == 5u && r.engine == 2u &&
                                      up_valid(&r) && !memcmp(r.name, "Bass One", 8) && !r.name[8]);
    ok = r.ver == UP_VER && UP_VER == 5u && r.ne0 == P_E0 && r.nv == P_ENG_END + UP_NTAIL;
    up_params(&r, v, def);                                  /* (the values after P_E7: a sound's kept, the others' defaults) */
    for (i = 0; i < P_COUNT; i++)
        ok &= LFO_ID(i) || v[i] == (i < P_ENG_END || (i >= P_E1PIT && i <= P_SLAST) ? (int16_t)(-40 + (int32_t)i) : def[i]);
    bad += check("UP_PUT values: a byte each, negative ones too, the sound's after P_E7 too", ok);
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
    bad += check("bank fits one object; record 216 B, UPB4", sizeof(up_bank_t) <= ST_PAYLOAD_MAX && sizeof(up_rec_t) == 216u &&
                                                             UP_BANK_MAGIC == 0x34425055u);
    bad += check("bank save", st_save(OBJ_UPRESET0 + 1, &up_bank[1], sizeof up_bank[1]) == 0);
    memset(up_bank, 0, sizeof up_bank);
    len = st_load(OBJ_UPRESET0 + 1, &up_bank[1], sizeof up_bank[1]);
    up_bank_check(1, &up_bank[1], len);
    got = *up_rec(17);
    bad += check("bank load: the record is back", up_used(17) && !memcmp(&got, &r, sizeof r));
    bad += check("other slots empty", !up_used(16) && !up_used(18) && !up_used(0));
    len = st_load(OBJ_UPRESET0, &up_bank[0], sizeof up_bank[0]);
    up_bank_check(0, &up_bank[0], len);
    bad += check("bank 0 never written -> empty", len < 0 && !up_used(0) && up_bank[0].magic == 0);
    bad += check("banks in 0xDC000..0xDFFFF", st_sector(OBJ_UPRESET0, 0) == 0xDC000u &&
                                                   st_sector(OBJ_UPRESET0 + 1, 1) == 0xDF000u &&
                                                   st_sector(OBJ_PROJECT0 + 3, 1) + 4096u <= 0xA0000u);
    up_bank[1].magic = 0x33425055u;                         /* UPB3 (before 2026-10-24): not read */
    up_bank_check(1, &up_bank[1], (int)sizeof up_bank[1]);
    bad += check("a UPB3 bank (the format before) -> empty", !up_used(17));
    up_bank[1].magic = UP_BANK_MAGIC;
    up_bank[1].rsize = sizeof(up_rec_t);
    up_bank[1].nslot = UP_PER_BANK;
    *up_rec(17) = r;
    up_rec(17)->ver = 4;
    bad += check("record of another version -> empty", !up_used(17));
    *up_rec(17) = r;
    up_rec(17)->nv = UP_NV + 1u;
    bad += check("record of more values than it holds -> empty", !up_used(17));

    {   /* a record of a build with fewer values: P_E0 two earlier, no values after P_E7 -> by count, the rest defaults */
        uint32_t e0 = P_E0 - 2u;
        memset(&r, 0, sizeof r);
        r.used = UP_USED, r.ver = UP_VER, r.name[0] = 'Z', r.ne0 = (uint8_t)e0, r.nv = (uint8_t)(e0 + 8u);
        for (i = 0; i < e0 + 8u; i++)
            r.v[i] = (int8_t)(i % 100u);
        up_params(&r, v, def);
        ok = up_valid(&r);
        for (i = 0; i < P_E0; i++)
            ok &= LFO_ID(i) || v[i] == (i < e0 ? (int16_t)(i % 100u) : def[i]);
        for (i = 0; i < 8u; i++)
            ok &= v[P_E0 + i] == (int16_t)((e0 + i) % 100u);
        for (i = P_ENG_END; i < P_COUNT; i++)
            ok &= LFO_ID(i) || v[i] == def[i];
        bad += check("fewer values: by count (P_E0 .. from ne0), the rest the defaults", ok);
    }
    {   /* every value a signed byte: -128 and 127 kept, beyond clamped (a range never reaches it: auto_int8_check) */
        int16_t w[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            w[i] = ENV_ID(i) ? 0 : (int16_t)(i & 1u ? -128 : 127);
        w[P_LEVEL] = 300, w[P_PAN] = -300;
        memset(&r, 0, sizeof r);
        r.used = UP_USED, r.name[0] = 'Y';
        up_vals_put(&r, w);
        up_params(&r, v, def);
        ok = up_valid(&r) && v[P_LEVEL] == 127 && v[P_PAN] == -128;
        for (i = 0; i < P_COUNT; i++)
            if (i != P_LEVEL && i != P_PAN && !LFO_ID(i))
                ok &= i < P_ENG_END || (i >= P_E1PIT && i <= P_SLAST) ? v[i] == w[i] : v[i] == def[i];
        bad += check("-128 and 127 kept, 300 / -300 clamped; values after P_E7 not a sound's: defaults", ok);
    }
    {   /* ENV 1 MOD's values and ENV2 DEST's slots (env_store.h), FLT B: back as given; ENV's FLT SHP depths as they are */
        static const int16_t S[3][8] = {{0, 0, 0, 0, LT_FLT, 0, LT_OFF, 0},
                                        {-64, 63, 1, -1, LT_PIT, 33, LT_SDTN, -7},
                                        {20, -30, 0, 7, LT_FLT, 88, LT_OSC2, -64}};
        int16_t w[P_COUNT];
        uint32_t k, n8 = FELUCCA_ANALOG2 ? 8u : 4u;
        ok = 1;
        for (k = 0; k < 3u; k++) {
            for (i = 0; i < P_COUNT; i++)
                w[i] = ENV_ID(i) ? 0 : def[i];               /* (the words of the depths of before: 0 at run time) */
            memcpy(&w[P_E1PIT], S[k], n8 * sizeof w[0]);
            w[P_ED_FLT] = (int16_t)(10 * k), w[P_ED_SHP] = (int16_t)(-5 * (int)k);
#if FELUCCA_ANALOG2
            w[P_A2BTYP] = (int16_t)(5 + k), w[P_A2BCUT] = (int16_t)(127 - k), w[P_A2BRES] = (int16_t)(60 * k);
#endif
            up_vals_put(&r, w);
            up_params(&r, v, def);
            ok &= !memcmp(&v[P_E1PIT], S[k], n8 * sizeof v[0]) && v[P_ED_FLT] == (int16_t)(10 * k) && v[P_ED_SHP] == (int16_t)(-5 * (int)k);
#if FELUCCA_ANALOG2
            ok &= v[P_A2BTYP] == (int16_t)(5 + k) && v[P_A2BCUT] == (int16_t)(127 - k) && v[P_A2BRES] == (int16_t)(60 * k);
#endif
        }
        r.v[P_ENG_END + 4u] = (int8_t)LT_N;                 /* (a DEST past the list: OFF, its AMT 0) */
        up_params(&r, v, def);
        ok &= !FELUCCA_ANALOG2 || (v[P_N2DA] == LT_OFF && v[P_N2AA] == 0);
    }
    bad += check("ENV 1 MOD / ENV2 DEST / FLT B: round trip, a DEST past the list OFF", ok);

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
