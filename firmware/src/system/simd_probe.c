/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 SLOOP */
/* EXPERIMENTAL (FELUCCA_SIMD_PROBE): does the FM-1's core run the packed 16-bit forms of
 * hal/fm1_simd.h, with the meaning the emulator gives them? At boot, before audio: each form on inputs
 * that tell the plausible meanings apart (wrap or saturate, round or floor, which half goes where),
 * then sine_i's asm against its C over 4096 phases. The screen shows PASS or FAIL and each form; the
 * sine kernel (simd_ok) is used only after a PASS, else the C runs: the firmware is otherwise normal.
 * ANALOG 2's packed swarm (simd_swarm_ok) needs every form to pass (it packs other halves than PACK
 * tests: h,h l,l h,l; the same field, not probed apart).
 *
 * A form the core does not implement may raise the CPU exception: the crash screen shows for 4 s and
 * the FM-1 resets. probe_state (.noinit, kept over that reset) still names the form that was running,
 * so the next boot reports TRAP for it, skips the probe (no reset loop) and runs the C. A watchdog
 * reset during the probe ends the same way. A power-on clears the record: the probe runs again. */
#ifndef FELUCCA_SIMD_PROBE_TEST
#define FELUCCA_SIMD_PROBE_TEST 0        /* 1: emulator test only, report a trap of the first form */
#endif
#define PROBE_MAGIC 0x53494D44u          /* "SIMD" */
#define PROBE_DONE 0xFFu

typedef struct {
    const char *name;
    uint32_t (*form)(uint32_t a, uint32_t b, uint32_t d);
    uint32_t a, b, d, want;              /* want: the emulator's (inferred) result */
} probe_case_t;

/* the first four cases are the kernel's two forms; the cases of a form follow each other */
static const probe_case_t PROBE_CASES[] = {
    {"QMUL16 X2", simd_qmul16x2_l, 0x00CA1234u, 0x00007FFFu, 0xAAAABBBBu, 0xAAAA00C9u},  /* 201.99: floor */
    {"QMUL16 X2", simd_qmul16x2_l, 0xFFFF0000u, 0x00003FFFu, 0x12345678u, 0x1234FFFFu},  /* -0.49: floor */
    {"QMUL16 X2", simd_qmul16x2_l, 0x80000000u, 0x00008000u, 0x00000000u, 0x00007FFFu},  /* saturates */
    {"ADD16", simd_add16_l, 0x11117FFFu, 0x22220001u, 0xAAAABBBBu, 0xAAAA8000u},         /* wraps */
    {"SUB16", simd_sub16_l, 0x00000005u, 0x00070000u, 0xAAAABBBBu, 0xAAAAFFFEu},
    {"QMUL16", simd_qmul16_l, 0x00C80000u, 0x00000003u, 0xAAAABBBBu, 0xAAAA0258u},
    {"QMUL16", simd_qmul16_l, 0x012C0000u, 0x000000C8u, 0x00000000u, 0x00007FFFu},
    {"QMUL16 W", simd_qmul16_w, 0xFFFF8000u, 0x7FFF0000u, 0x00000000u, 0xC0008000u},
    {"QMUL16 X2 W", simd_qmul16x2_w, 0x0000C000u, 0x40000000u, 0x00000000u, 0xE0000000u},
    {"QMUL16 X2 W", simd_qmul16x2_w, 0x00008000u, 0x80000000u, 0x00000000u, 0x7FFFFFFFu},
    {"PACK", simd_pack, 0x11112222u, 0x33334444u, 0x00000000u, 0x22223333u},
    {"QADD16 2", simd_qadd16_2, 0x70000001u, 0x2000FFFEu, 0x00000000u, 0x7FFFFFFFu},
    {"QADDSUB16 2", simd_qaddsub16_2, 0x70008001u, 0x20000002u, 0x00000000u, 0x7FFF8000u},
    {"QMUL16 2", simd_qmul16_2, 0x00030100u, 0xFFFE0200u, 0x00000000u, 0xFFFA7FFFu},
    {"QMUL16 X2 2", simd_qmul16x2_2, 0x40008000u, 0x40008000u, 0x00000000u, 0x20007FFFu},
};
#define PROBE_N (sizeof PROBE_CASES / sizeof PROBE_CASES[0])
#define PROBE_KERNEL 4u                  /* cases 0..3: the forms asm_sine_pk uses */
#define PROBE_SINE (PROBE_N + 1u)        /* the stage of the sine sweep */

typedef struct {
    uint32_t magic, stage, trapped;      /* stage: 1 + the case running, PROBE_SINE, PROBE_DONE */
    uint32_t got[PROBE_N];
} probe_state_t;
probe_state_t probe_state __attribute__((section(".noinit")));   /* not static: see fm1_crash */

static void probe_hex(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void probe_stage(uint32_t s)
{
    *(volatile uint32_t *)&probe_state.stage = s;   /* in RAM before the form runs */
}

/* sine_i's asm against its C: every table point, four fractions each, with low phase bits set */
static uint32_t probe_sine(void)
{
    uint32_t i, bad = 0;
    for (i = 0; i < 4096u; i++) {
        uint32_t ph = (i << 20) ^ ((i * 0x9E3779B1u) >> 12);
        bad += (uint32_t)(asm_sine_pk(ph, SINE_PK) != sine_i_c(ph));
    }
    return bad;
}

static void probe_wait(uint32_t ms)
{
    uint32_t t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < ms * 1000u * FM1_TICKS_PER_US)
        fm1_wdt_feed();
}

static void probe_line(uint32_t row, const char *a, const char *b, uint16_t c)
{
    char s[32];
    uint32_t n = 0;
    while (*a && n < 14u)
        s[n++] = *a++;
    while (n < 14u)
        s[n++] = ' ';
    while (*b && n < 31u)
        s[n++] = *b++;
    s[n] = 0;
    draw_text_box(4, 40u + 16u * row, 232, &FONT_S, s, c, 0);
}

/* one line per form: OK, the first differing result, TRAP, or - (not run).
 * trap: 1 + the case that trapped (0: none); ran: cases run; sine: differing phases (-1: not run) */
static void probe_screen(uint32_t trap, uint32_t ran, int32_t sine)
{
    const uint16_t ok = RGB(80, 220, 80), bad = RGB(255, 70, 70), dim = RGB(150, 150, 150);
    uint32_t i, j, row = 0, fails = 0;
    char h[12];
    for (i = 0; i < ran; i++)
        fails += (uint32_t)(probe_state.got[i] != PROBE_CASES[i].want);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 2, 240, &FONT_S, "SIMD PROBE  EXPERIMENTAL", C_WHITE, 1);
    draw_text_box(0, 20, 240, &FONT_S, trap ? "FAIL: TRAP" : fails || sine ? "FAIL: VALUES" : "PASS",
                  trap || fails || sine ? bad : ok, 1);
    for (i = 0; i < PROBE_N; i = j) {
        const char *st = "OK";
        uint16_t c = ok;
        for (j = i; j < PROBE_N && PROBE_CASES[j].form == PROBE_CASES[i].form; j++) {
            if (trap == j + 1u) {
                st = "TRAP";
                c = bad;
                break;
            }
            if (j >= ran) {
                st = "-";
                c = dim;
                break;
            }
            if (probe_state.got[j] != PROBE_CASES[j].want) {
                probe_hex(h, probe_state.got[j]);
                st = h;
                c = bad;
                break;
            }
        }
        while (j < PROBE_N && PROBE_CASES[j].form == PROBE_CASES[i].form)
            j++;
        probe_line(row++, PROBE_CASES[i].name, st, c);
    }
    probe_line(row, "SINE KERNEL", sine == 0 ? "ON" : trap == PROBE_SINE ? "TRAP, OFF" : "OFF", sine == 0 ? ok : bad);
    probe_wait(trap || fails || sine ? 8000u : 3000u);
}

/* after the logo, before audio: sets simd_ok */
static void simd_probe_boot(void)
{
    uint32_t i, kernel_bad = 0;
    int32_t sine = -1;
    simd_ok = 0;
    if ((fm1_boot.p3_rst & 1u) && !(fm1_boot.p3_rst & (4u | 0x40u)))
        probe_state.magic = 0;               /* power-on: probe again */
#if FELUCCA_SIMD_PROBE_TEST
    if (probe_state.magic != PROBE_MAGIC) {  /* emulator test of the report: as if case 1 had trapped */
        probe_state.magic = PROBE_MAGIC;
        probe_state.stage = 1;
        probe_state.trapped = 0;
    }
#endif
    if (probe_state.magic == PROBE_MAGIC && (probe_state.trapped ||
                                             (probe_state.stage != 0u && probe_state.stage != PROBE_DONE))) {
        uint32_t trap = probe_state.trapped ? probe_state.trapped : probe_state.stage;
        probe_state.trapped = trap;          /* kept: no probe until a power-on */
        probe_state.stage = PROBE_DONE;
        probe_screen(trap, trap - 1u < PROBE_N ? trap - 1u : PROBE_N, -1);
        return;
    }
    probe_state.magic = PROBE_MAGIC;
    probe_state.trapped = 0;
    for (i = 0; i < PROBE_N; i++) {
        const probe_case_t *c = &PROBE_CASES[i];
        probe_stage(i + 1u);
        probe_state.got[i] = c->form(c->a, c->b, c->d);
        if (i < PROBE_KERNEL)
            kernel_bad += (uint32_t)(probe_state.got[i] != c->want);
    }
    if (!kernel_bad) {
        probe_stage(PROBE_SINE);
        sine = (int32_t)probe_sine();
    }
    probe_stage(PROBE_DONE);
    simd_ok = sine == 0;
    for (i = 0; i < PROBE_N; i++)                /* ANALOG 2's swarm (pack, dual multiply): all forms */
        kernel_bad += (uint32_t)(probe_state.got[i] != PROBE_CASES[i].want);
    simd_swarm_ok = simd_ok && !kernel_bad;
    probe_screen(0, PROBE_N, sine);
}
