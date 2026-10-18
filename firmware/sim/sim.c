/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's platform: Optimist's own firmware (src/felucca.c, one compilation unit) on the host HAL of
 * firmware/sim/hal, driven from outside through the exports below (web/sim/, tests/sim_test.mjs). docs/SIMULATOR.md.
 *
 * The scheduler (sim_run): the simulated clock (hal/sim_core.h) moves from event to event: the TIMER5 tick (10 kHz:
 * the keys, the USB poll, fm1_ms), the audio DMA's half-buffer switch (every HALF_FRAMES frames at 44.1 kHz: the half
 * just played to the output, the ALNK0 handler renders the next) and, between them, the main loop: a pass every 15 ms
 * (fm1_main_frame) and the turns of its wait (fm1_main_idle_step) where the FM-1 sleeps until an interrupt.
 * Single-threaded, no second core, no Bluetooth (tools/sim.py SIM_FORCE).
 *
 * A reboot, UBOOT or an update entry (hal/fm1_sys.h) ends the run (longjmp back to the export) with its status bit;
 * the page then starts a fresh instance on the same flash bytes. */
#include <setjmp.h>
#include "felucca.c"

#define SIM_EXPORT(name) __attribute__((export_name(#name), used))

/* ------------------------------------------------ the firmware's link-time names (app.ld, fm1_isr.S) --- */
uint32_t _data_start[1], _data_end[1], _data_load[1], _bss_start[1], _bss_end[1];
uint32_t _pool_start[1], _pool_end[1], _rt_start[1], _rt_end[1], _rt_load[1], _rh_start[1], _rh_end[1], _rh_load[1];
uint32_t _rh2_start[1], _rh2_end[1], _rh2_load[1], _pi_start[1], _pi_end[1], _pi_load[1], _pz_start[1];
void isr_timer5(void) { fm1_timer5_irq(); }
void isr_timer5_ram(void) { fm1_timer5_irq(); }
void isr_alnk0(void) { fm1_alnk0_irq(); }
void isr_alnk0_ram(void) { fm1_alnk0_irq(); }

/* ------------------------------------------------------------------------------------ the scheduler --- */
#define SIM_T5_TICKS 2400u                        /* TIMER5: 10 kHz */
#define SIM_NEVER (~(uint64_t)0)
static void (*sim_h_t5)(void), (*sim_h_al)(void);
static uint8_t sim_t5_on, sim_t5_pend, sim_al_pend, sim_t5_held, sim_al_held, sim_ev_busy;
static uint64_t sim_t5_due, sim_al_due, sim_al_n; /* the next tick; the next half's end, halves so far */
static uint32_t sim_status_v;
static jmp_buf sim_jb;
static uint8_t sim_jb_on;

static uint64_t sim_frames_to_ticks(uint64_t f) { return f * 240000u / 441u; }   /* 24 MHz / 44.1 kHz */
static uint64_t sim_half_end(uint64_t n) { return sim_frames_to_ticks((n + 1u) * HALF_FRAMES); }

static void sim_next_set(void)
{
    uint64_t n = sim_al_due;
    if (sim_t5_on && sim_t5_due < n)
        n = sim_t5_due;
    sim_next = n;
}

/* ---- the audio out: interleaved int16 L, R for the page */
#define SIM_AUDIO_N 16384u                        /* frames */
static int16_t sim_aout[2u * SIM_AUDIO_N];
static uint32_t sim_aout_n, sim_aout_lost;

static int16_t sim_s16(int32_t v)                 /* 24-bit (Q15 << 7) -> 16 bit */
{
    v >>= 8;
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static void sim_audio_switch(void)                /* the DMA at a half's end */
{
    uint32_t i, n = HALF_FRAMES;
    const int32_t *h = 0;
    if (sim_alnk.on && sim_alnk.buf) {
        n = sim_alnk.half_words / 2u;
        h = sim_alnk.buf + sim_alnk.playing * sim_alnk.half_words;
        sim_alnk.playing ^= 1u;
        sim_alnk.con2 |= FM1_AUDIO_HALF;
        sim_al_pend = 1;
    }
    for (i = 0; i < n; i++) {
        if (sim_aout_n >= SIM_AUDIO_N) {
            sim_aout_lost += n - i;
            break;
        }
        sim_aout[2u * sim_aout_n] = h ? sim_s16(h[2u * i]) : 0;
        sim_aout[2u * sim_aout_n + 1u] = h ? sim_s16(h[2u * i + 1u]) : 0;
        sim_aout_n++;
    }
}

static void sim_dispatch(void)                    /* the pending handlers, if they may run */
{
    if (!sim_ie || sim_in_isr)
        return;
    sim_in_isr = 1;
    for (;;) {
        if (sim_t5_pend && sim_h_t5 && !sim_t5_held) {   /* TIMER5 (priority 4) before ALNK0 (3) */
            sim_t5_pend = 0;
            sim_h_t5();
        } else if (sim_al_pend && sim_h_al && !sim_al_held) {
            sim_al_pend = 0;
            sim_h_al();
        } else {
            break;
        }
    }
    sim_in_isr = 0;
}

static void sim_events(void)
{
    if (sim_ev_busy)
        return;
    sim_ev_busy = 1;
    sim_next = SIM_NEVER;                         /* (no re-entry from the handlers' timer reads) */
    for (;;) {
        if (sim_t5_on && sim_t5_due <= sim_t && sim_t5_due <= sim_al_due) {
            sim_t5_due += SIM_T5_TICKS;
            sim_t5_pend = 1;
        } else if (sim_al_due <= sim_t) {
            sim_audio_switch();
            sim_al_n++;
            sim_al_due = sim_half_end(sim_al_n);
        } else {
            break;
        }
        sim_dispatch();
    }
    sim_next_set();
    sim_ev_busy = 0;
}

static void sim_advance(uint64_t ticks)
{
    uint64_t end = sim_t + ticks;
    if (sim_ev_busy) {                            /* (inside a handler the events wait for its end) */
        sim_t = end;
        return;
    }
    while (sim_next <= end) {
        if (sim_t < sim_next)
            sim_t = sim_next;
        sim_events();
    }
    if (sim_t < end)
        sim_t = end;
}

static void sim_idle(void)                        /* fm1_idle: until the next event */
{
    if (sim_next > sim_t && sim_next != SIM_NEVER)
        sim_advance(sim_next - sim_t);
    else
        sim_events();
}

static void sim_irq_on(void)
{
    sim_ie = 1;
    sim_dispatch();
}

static void sim_attach(uint32_t n, void (*h)(void))
{
    if (n == FM1_IRQ_TIMER5)
        sim_h_t5 = h;
    else if (n == FM1_IRQ_ALNK0)
        sim_h_al = h;
}

static void sim_hold(uint32_t n, int hold)
{
    if (n == FM1_IRQ_TIMER5)
        sim_t5_held = (uint8_t)(hold != 0);
    else if (n == FM1_IRQ_ALNK0)
        sim_al_held = (uint8_t)(hold != 0);
    if (!hold)
        sim_dispatch();
}

static void sim_timer5_start(void)
{
    sim_t5_on = 1;
    sim_t5_due = sim_t + SIM_T5_TICKS;
    sim_next_set();
}

static void sim_stop(uint32_t why)
{
    sim_status_v |= why;
    sim_alnk.on = 0;
    if (sim_jb_on)
        longjmp(sim_jb, 1);
    __builtin_trap();
}

/* ---------------------------------------------------------------------------------------- the flash --- */
static uint8_t sim_flash_ready;
static void sim_flash_init(void)
{
    uint32_t i;
    if (sim_flash_ready)
        return;
    for (i = 0; i < SIM_FLASH_SIZE; i++)
        sim_flash_img[i] = 0xFFu;                 /* a blank part: erased */
    sim_flash_ready = 1;
}

/* ------------------------------------------------------------------------------------------ the LCD --- */
#define SIM_W 240u
static uint16_t sim_fb[SIM_W * SIM_W];
static uint32_t sim_fb_rev, sim_fb_dirty;
static struct {
    uint8_t cmd, n, hi, have_hi;
    uint8_t arg[4];
    uint16_t xs, xe, ys, ye, x, y;
} sim_lcd = {0, 0, 0, 0, {0}, 0, SIM_W - 1u, 0, SIM_W - 1u, 0, 0};

static void sim_lcd_cmd(uint8_t c)
{
    sim_lcd.cmd = c;
    sim_lcd.n = 0;
    sim_lcd.have_hi = 0;
    if (c == 0x2Cu) {                             /* RAMWR: from the window's corner */
        sim_lcd.x = sim_lcd.xs;
        sim_lcd.y = sim_lcd.ys;
    }
}

static void sim_lcd_px(uint16_t c)
{
    uint32_t x = sim_lcd.x, y = sim_lcd.y;
    if (x < SIM_W && y < SIM_W) {
        sim_fb[y * SIM_W + x] = c;
        sim_fb_dirty |= 1u << (y / 16u);
    }
    if (++sim_lcd.x > sim_lcd.xe) {
        sim_lcd.x = sim_lcd.xs;
        if (++sim_lcd.y > sim_lcd.ye)
            sim_lcd.y = sim_lcd.ys;
    }
}

static void sim_lcd_data(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (sim_lcd.cmd == 0x2Au || sim_lcd.cmd == 0x2Bu) {   /* CASET / RASET: start, end, big-endian */
        for (i = 0; i < n && sim_lcd.n < 4u; i++)
            sim_lcd.arg[sim_lcd.n++] = p[i];
        if (sim_lcd.n == 4u) {
            uint16_t a = (uint16_t)(sim_lcd.arg[0] << 8 | sim_lcd.arg[1]), b = (uint16_t)(sim_lcd.arg[2] << 8 | sim_lcd.arg[3]);
            if (sim_lcd.cmd == 0x2Au)
                sim_lcd.xs = a, sim_lcd.xe = b;
            else
                sim_lcd.ys = a, sim_lcd.ye = b;
        }
        return;
    }
    if (sim_lcd.cmd != 0x2Cu && sim_lcd.cmd != 0x3Cu)
        return;
    for (i = 0; i < n; i++) {                     /* RGB565, high byte first on the wire */
        if (!sim_lcd.have_hi) {
            sim_lcd.hi = p[i];
            sim_lcd.have_hi = 1;
        } else {
            sim_lcd_px((uint16_t)(sim_lcd.hi << 8 | p[i]));
            sim_lcd.have_hi = 0;
        }
    }
    sim_fb_rev++;
}

/* -------------------------------------------------------------------------------------- USB MIDI --- */
#define SIM_MIQ 4096u                             /* USB-MIDI event packets from the page (an 8 KiB SysEx: 2731) */
#define SIM_MIDI_N 16384u
static uint32_t sim_miq[SIM_MIQ];
static uint32_t sim_miq_w, sim_miq_r, sim_miq_lost;
static uint8_t sim_midi_inb[SIM_MIDI_N], sim_midi_outb[SIM_MIDI_N];
static uint32_t sim_midi_out_n, sim_midi_out_lost;
static uint8_t sim_sie_idx, sim_rx1_ready, sim_rx1_len;

static void sim_rx1_stage(void)                   /* the next EP1 OUT packet (<= 16 events) into usb.c's buffer */
{
    uint8_t *b = sim_usb.rxbuf[1];
    uint32_t n = 0;
    if (sim_rx1_ready || !b || sim_miq_r == sim_miq_w)
        return;
    while (sim_miq_r != sim_miq_w && n < 64u) {
        uint32_t p = sim_miq[sim_miq_r++ % SIM_MIQ];
        b[n] = (uint8_t)p;
        b[n + 1u] = (uint8_t)(p >> 8);
        b[n + 2u] = (uint8_t)(p >> 16);
        b[n + 3u] = (uint8_t)(p >> 24);
        n += 4u;
    }
    sim_rx1_len = (uint8_t)n;
    sim_rx1_ready = 1;
}

static uint32_t sim_sie_rd(uint32_t r)
{
    uint32_t frame = (uint32_t)(sim_t / 24000u);  /* SOF every ms */
    switch (r) {
    case S_INDEX:
        return sim_sie_idx;
    case S_INTRRX1:
        sim_rx1_stage();
        return sim_rx1_ready ? 0x02u : 0u;
    case S_FRAME1:
        return frame & 0xFFu;
    case S_FRAME2:
        return (frame >> 8) & 7u;
    case S_RXCSR1:
        if (sim_sie_idx != 1u)
            return 0;
        sim_rx1_stage();
        return sim_rx1_ready;
    case S_RXCOUNT1:
        return sim_sie_idx == 1u && sim_rx1_ready ? sim_rx1_len : 0u;
    default:                                      /* INTRUSB, INTRTX1, CSR0 / TXCSR1 (sent at once): nothing */
        return 0;
    }
}

static void sim_sie_wr(uint32_t r, uint32_t v)
{
    if (r == S_INDEX)
        sim_sie_idx = (uint8_t)v;
    else if (r == S_RXCSR1 && sim_sie_idx == 1u && sim_rx1_ready && (v & 0x10u)) {
        sim_rx1_ready = 0;                        /* usb.c ep1_rx done with the packet */
        sim_rx1_stage();
    }
}

static void sim_midi_put(uint8_t b)
{
    if (sim_midi_out_n < SIM_MIDI_N)
        sim_midi_outb[sim_midi_out_n++] = b;
    else
        sim_midi_out_lost++;
}

static void sim_usb_tx(uint32_t ep, const uint8_t *p, uint32_t n)   /* EP1 IN: USB-MIDI packets -> MIDI bytes */
{
    static const uint8_t LEN[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    uint32_t i, k;
    if (ep != 1u)
        return;
    for (i = 0; i + 3u < n; i += 4u)
        for (k = 0; k < LEN[p[i] & 15u]; k++)
            sim_midi_put(p[i + 1u + k]);
}

static void sim_miq_put(uint32_t cin, const uint8_t *d, uint32_t n)
{
    uint32_t p = cin & 15u;
    if (n > 0u) p |= (uint32_t)d[0] << 8;
    if (n > 1u) p |= (uint32_t)d[1] << 16;
    if (n > 2u) p |= (uint32_t)d[2] << 24;
    if (sim_miq_w - sim_miq_r >= SIM_MIQ) {
        sim_miq_lost++;
        return;
    }
    sim_miq[sim_miq_w++ % SIM_MIQ] = p;
}

/* raw MIDI bytes -> USB-MIDI packets (cable 0): running status, realtime anywhere, SysEx in CIN 4 / 5 / 6 / 7 */
static void sim_midi_pack(const uint8_t *m, uint32_t len)
{
    static uint8_t run;
    uint8_t d[3];
    uint32_t i = 0;
    while (i < len) {
        uint8_t b = m[i];
        if (b >= 0xF8u) {
            sim_miq_put(0xFu, &b, 1);
            i++;
        } else if (b == 0xF0u) {
            uint32_t n = 0;
            run = 0;
            for (; i < len; i++) {
                b = m[i];
                if (b >= 0xF8u) {
                    sim_miq_put(0xFu, &b, 1);
                    continue;
                }
                if (n && (b & 0x80u) && b != 0xF7u)
                    break;                        /* (another status ends it, unfinished) */
                d[n++] = b;
                if (b == 0xF7u) {
                    sim_miq_put(4u + n, d, n);
                    n = 0;
                    i++;
                    break;
                }
                if (n == 3u) {
                    sim_miq_put(4u, d, 3);
                    n = 0;
                }
            }
            if (n)
                sim_miq_put(4u, d, n);            /* (cut short: what came, as a continuation) */
        } else {
            uint32_t st = b, need, n = 0;
            if (b & 0x80u) {
                i++;
                if (b < 0xF0u)
                    run = b;
                else
                    run = 0;
            } else if (run) {
                st = run;
            } else {
                i++;                              /* a stray data byte */
                continue;
            }
            need = st >= 0xF0u ? (st == 0xF2u ? 2u : (st == 0xF1u || st == 0xF3u) ? 1u : 0u)
                               : ((st & 0xF0u) == 0xC0u || (st & 0xF0u) == 0xD0u) ? 1u : 2u;
            d[0] = (uint8_t)st;
            while (n < need && i < len && !(m[i] & 0x80u))
                d[1u + n++] = m[i++];
            if (n < need)
                continue;                         /* (incomplete: dropped) */
            if (st < 0xF0u)
                sim_miq_put(st >> 4, d, 1u + need);
            else
                sim_miq_put(need == 2u ? 3u : need == 1u ? 2u : 5u, d, 1u + need);
        }
    }
    sim_rx1_stage();
}

/* -------------------------------------------------------------------------------------- the panel --- */
static void sim_in_push(uint32_t id, uint32_t down)
{
    if (sim_inq_w - sim_inq_r < SIM_INQ)
        sim_inq[sim_inq_w++ % SIM_INQ] = (uint8_t)(id | (down ? 0x80u : 0u));
}

static uint8_t sim_led_v[FM1_NKEY];

static uint32_t sim_led_of(uint32_t id)           /* 0 off, 1 dim, 2 lit */
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                uint32_t bit = 1u << r, dim = fm1_led_dim[p];
#if FELUCCA_LIGHTS
                dim |= fm1_led_bg[p];
#endif
                return (fm1_led[p] & bit) ? 2u : (dim & bit) ? 1u : 0u;
            }
    return 0;
}

/* ---------------------------------------------------------------------------------------- exports --- */
static uint8_t sim_booted, sim_in_frame;
static uint64_t sim_base, sim_req;

SIM_EXPORT(sim_flash) uint8_t *sim_flash(void)
{
    sim_flash_init();
    return sim_flash_img;
}
SIM_EXPORT(sim_flash_size) uint32_t sim_flash_size(void) { return SIM_FLASH_SIZE; }
SIM_EXPORT(sim_flash_rev) uint32_t sim_flash_rev_get(void) { return sim_flash_rev; }

/* fm1_cstart's part that is not the chip's (main.c), then the start-up (fm1_main: up to its loop), then the host configures
 * the USB device (as a SET_CONFIGURATION: usb.c ep1_config) */
SIM_EXPORT(sim_boot) void sim_boot(void)
{
    if (sim_booted || sim_status_v)
        return;
    sim_flash_init();
    sim_al_due = sim_half_end(0);
    sim_next_set();
    if (setjmp(sim_jb)) {
        sim_jb_on = 0;
        return;
    }
    sim_jb_on = 1;
    fm1_time_init();
    fm1_reset_reason();
    bootguard_clear(&bootguard);                  /* a power-on */
    (void)bootguard_begin(&bootguard);
    fm1_irq_init();
    fm1_main();
    usb.config = 1;
    ep1_config();
    sim_base = sim_t;
    sim_req = 0;
    sim_booted = 1;
    sim_jb_on = 0;
}

SIM_EXPORT(sim_status) uint32_t sim_status(void) { return sim_status_v; }

SIM_EXPORT(sim_run) uint32_t sim_run(uint32_t frames)
{
    uint64_t target;
    if (!sim_booted || sim_status_v)
        return sim_status_v;
    sim_req += frames;
    target = sim_base + sim_frames_to_ticks(sim_req);
    if (setjmp(sim_jb)) {
        sim_jb_on = 0;
        return sim_status_v;
    }
    sim_jb_on = 1;
    while (sim_t < target) {
        if (!sim_in_frame) {                      /* a pass of the main loop */
            fm1_main_frame();
            sim_in_frame = 1;
            continue;
        }
        if (fm1_ms - main_m >= 15u) {           /* (main.c: while (fm1_ms - m < 15u)) */
            sim_in_frame = 0;
            continue;
        }
        fm1_main_idle_step();
        if (sim_next < target)                    /* fm1_idle: asleep until the next interrupt */
            sim_idle();
        else
            sim_advance(target - sim_t);
    }
    sim_jb_on = 0;
    return sim_status_v;
}

SIM_EXPORT(sim_audio) int16_t *sim_audio(void) { return sim_aout; }
SIM_EXPORT(sim_audio_take) uint32_t sim_audio_take(void)
{
    uint32_t n = sim_aout_n;
    sim_aout_n = 0;
    return n;
}

SIM_EXPORT(sim_pixels) uint16_t *sim_pixels(void) { return sim_fb; }
SIM_EXPORT(sim_frame_rev) uint32_t sim_frame_rev(void) { return sim_fb_rev; }
SIM_EXPORT(sim_dirty) uint32_t sim_dirty(void)
{
    uint32_t d = sim_fb_dirty;
    sim_fb_dirty = 0;
    return d;
}

/* i: note key 0..26 (F3 .. G5) */
SIM_EXPORT(sim_key) void sim_key(uint32_t i, uint32_t down)
{
    if (i < FM1_NKEY - 14u)
        sim_in_push(14u + i, down);
}
/* i: a label (ui/panel.c B_FX .. B_OCTUP, sim_button_names), to its matrix button through the panel's table */
SIM_EXPORT(sim_button) void sim_button(uint32_t i, uint32_t down)
{
    if (i < NB)
        sim_in_push(panel.btn[i] < 14u ? panel.btn[i] : i, down);
}
/* i: a role (ui/panel.c EN_SELECT .. EN_K4, sim_enc_names), + = clockwise */
SIM_EXPORT(sim_enc) void sim_enc(uint32_t i, int32_t delta)
{
    uint32_t e;
    if (i >= NE)
        return;
    e = panel.enc[i] < FM1_NENC ? panel.enc[i] : i;
    fm1_in.enc_steps[e] = (int16_t)(fm1_in.enc_steps[e] + delta * (panel.dir[i] < 0 ? -1 : 1));
}
SIM_EXPORT(sim_master) void sim_master(uint32_t v) { sim_master_pot = (int32_t)(v > 1023u ? 1023u : v); }

/* [0..13] the buttons by label (as sim_button), [14..40] the note keys: 0 off, 1 dim, 2 lit */
SIM_EXPORT(sim_leds) uint8_t *sim_leds(void)
{
    uint32_t i;
    for (i = 0; i < NB; i++)
        sim_led_v[i] = (uint8_t)sim_led_of(panel.btn[i]);
    for (i = 14u; i < FM1_NKEY; i++)
        sim_led_v[i] = (uint8_t)sim_led_of(i);
    return sim_led_v;
}
SIM_EXPORT(sim_led_count) uint32_t sim_led_count(void) { return FM1_NKEY; }

SIM_EXPORT(sim_midi_buf) uint8_t *sim_midi_buf(void) { return sim_midi_inb; }
SIM_EXPORT(sim_midi_in) void sim_midi_in(uint32_t len)
{
    sim_midi_pack(sim_midi_inb, len > SIM_MIDI_N ? SIM_MIDI_N : len);
}
SIM_EXPORT(sim_midi_out_buf) uint8_t *sim_midi_out_buf(void) { return sim_midi_outb; }
SIM_EXPORT(sim_midi_out) uint32_t sim_midi_out(void)
{
    uint32_t n = sim_midi_out_n;
    sim_midi_out_n = 0;
    return n;
}

/* 0 keys, 1 buttons, 2 encoders, 3 frames a half buffer, 4 the sample rate, 5 the LCD's backlight (0..16),
 * 6 MIDI bytes lost (either way), 7 audio frames lost (the page did not take them), 8 fm1_ms */
SIM_EXPORT(sim_info) uint32_t sim_info(uint32_t what)
{
    switch (what) {
    case 0: return FM1_NKEY - 14u;
    case 1: return NB;
    case 2: return NE;
    case 3: return HALF_FRAMES;
    case 4: return FS;
    case 5: return sim_lcd_bl;
    case 6: return sim_miq_lost + sim_midi_out_lost;
    case 7: return sim_aout_lost;
    case 8: return fm1_ms;
    default: return 0;
    }
}

static char sim_names_b[160], sim_names_e[96];
static const char *sim_names(char *o, uint32_t cap, const char *const *nm, uint32_t n)
{
    uint32_t i, k = 0;
    for (i = 0; i < n; i++) {
        const char *s = nm[i];
        while (*s && k + 2u < cap)
            o[k++] = *s++;
        if (i + 1u < n)
            o[k++] = '\n';
    }
    o[k] = 0;
    return o;
}
SIM_EXPORT(sim_button_names) const char *sim_button_names(void) { return sim_names(sim_names_b, sizeof sim_names_b, B_NAME, NB); }
SIM_EXPORT(sim_enc_names) const char *sim_enc_names(void) { return sim_names(sim_names_e, sizeof sim_names_e, E_NAME, NE); }
