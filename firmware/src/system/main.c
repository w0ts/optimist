/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA boot and main loop. Boot order: WDT first, OCT- + OCT+ held (UBOOT), boot-loop guard, fatal
 * vectors, guards; then LCD, input (TIMER5 IRQ, 10 kHz), audio (ALNK0 IRQ). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[], _rh_start[], _rh_end[], _rh_load[];
extern uint32_t _rh2_start[], _rh2_end[], _rh2_load[];


void fm1_timer5_irq(void)
{
    static uint32_t sub;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    if (felucca_dbg.in_audio)
        felucca_dbg.nested++;                      /* a tick inside the audio render (it outranks ALNK0) */
    fm1_input_tick();
#if FELUCCA_USB_AUDIO
    {   /* (Melodee) USB work can span several 100 us ticks: count elapsed time, not serviced
         * interrupts, or the USB work itself stretches the next isochronous deadline */
        static uint32_t last_poll;
        uint32_t start = fm1_ticks();
        if (!last_poll || start - last_poll >= 250u * FM1_TICKS_PER_US) {
            last_poll = start;
            usb_poll();                         /* at most 4 kHz: all USB SIE traffic lives here */
        } else {
            usb_rx_peek();                      /* the other ticks: a MIDI packet timed within 0.1 ms (clock) */
        }
    }
#else
    if (sub % 5u == 0u)
        usb_poll();                             /* 2 kHz: all USB SIE traffic lives here */
    else
        usb_rx_peek();                          /* 10 kHz: a MIDI packet timed within 0.1 ms (clock) */
#endif
#if FELUCCA_UART
    uart_midi_peek();                           /* 10 kHz: a TRS byte timed within 0.1 ms (clock) */
    if (sub % 5u == 2u)
        uart_midi_poll();                       /* 2 kHz: the UART's pendings */
#endif
#if FELUCCA_BLE
    if (sub % 5u == 4u)
        ble_midi_poll();                        /* 2 kHz: BLE MIDI in -> the router (midi_ble.c) */
#endif
#if FELUCCA_BRIGHT
    fm1_lcd_bl_tick(BL_DUTY[bl_dim & 7u]);      /* MENU > BRIGHT: the backlight PWM (bright.c) */
#endif
    if (++sub == 10u)
        sub = 0;
    {   /* milliseconds from the 24 MHz TIMER4: TIMER5 ticks coalesce while ALNK0 renders */
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}
extern void isr_timer5(void);

static void timer5_start(void)                 /* OSC /4 = 6 MHz, PRD 600 -> 10 kHz */
{
    /* above ALNK0 (3): the tick may nest into the audio render (a few us; plain registers, the
     * MIDI rings are single-producer / single-consumer). Below it, a tick waited for the render
     * and the column it had lit stayed on longer: the LEDs shimmered, beating against the audio
     * blocks (1378 / s against the 909 / s scan) */
    fm1_timer5_start(isr_timer5, 4);
}

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "OPTIMIST CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

/* power-on: three parts with their default sounds (TRK_DEF), the drum track, empty patterns */
static void felucca_init(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, trk_def_engine(i));
            apply_preset_to(t, trk_def_preset(i));   /* with its sends */
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);              /* the sequencers start empty */
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;        /* the 808 kit */
    song.sel = 0;
    song.master_q12 = 2048;
    autosave_resume();                        /* the project as it was left (project.c) */
    song.g[G_VIEW] = (int16_t)settings.view;  /* (a setting, not the project's: ui_overview.c view_sync) */
    song.g[G_SYNC] = sync_boot;               /* (the same: the HOME menu's SYNC, project.c bp23_word) */
    layers_init();                            /* the panel's layer buttons for the keys (ui_layers.c) */
    go_home();
    ui.force = 1;
}

static uint8_t boot_cal_req;                   /* OCT- + OCT+ held at power-on, let go before UBOOT (boot_hold) */

/* The first thing a start-up does (after the timer and the watchdog): OCT- + OCT+ held at power-on for
 * BOOTGUARD_HOLD_MS -> the chip's own UBOOT, whatever the rest of this build does; let go sooner ->
 * HARDWARE CALIBRATION. Bare pins, stack only: .data/.bss are not set up yet */
static BOOT_ORDER uint32_t boot_hold(void)
{
    bootguard_hold_t h = {0, 0, 0};
    uint32_t r;
    do {
        uint32_t t = fm1_ticks();
        fm1_wdt_feed();
        r = bootguard_hold_step(&h, fm1_input_oct_raw());
        while ((uint32_t)(fm1_ticks() - t) < 1000u * FM1_TICKS_PER_US)
            ;
    } while (r == HOLD_WAIT);
    return r;
}

/* felucca_dbg.stage while the start-up runs (the main loop's are 1..9): a watchdog reset or a hang before the UI's
 * first frame leaves the step it was in, for 'dbg' (prev_stage) and the rescue screen (recovery.c) */
enum { BOOT_STAGE_PERSIST = 0x41, BOOT_STAGE_STORES, BOOT_STAGE_SETTINGS, BOOT_STAGE_LCD, BOOT_STAGE_INPUT,
       BOOT_STAGE_SYNTH, BOOT_STAGE_AUDIO, BOOT_STAGE_USB, BOOT_STAGE_BLE, BOOT_STAGE_IRQS, BOOT_STAGE_SPLASH };
#define BOOT_STAGE(s) (felucca_dbg.stage = BOOT_STAGE_##s)

static BOOT_ORDER void fm1_main(void)
{
    int32_t knob = 512 * 16;
    uint32_t healthy_since;
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.max_us = 0;
    felucca_dbg.prev_stage = felucca_dbg.stage;     /* a WDT reset leaves the last breadcrumb here: a main-loop
                                                     * stage (1..9) or the start-up step it stopped in (BOOT_STAGE) */
    felucca_dbg.prev_page = felucca_dbg.page;
    felucca_dbg.prev_home = felucca_dbg.home;
    felucca_dbg.prev_frames = felucca_dbg.ui_frames;
    felucca_dbg.prev_rst = fm1_boot.p3_rst;
    felucca_dbg.ui_frames = 0;                      /* (this boot's frames: 0 in a crumb = the UI never ran) */
#if FELUCCA_BLE && BLE_HW_WL82
    fm1_ble_crumb_boot();                           /* where a BLUETOOTH ON was when the watchdog reset: 'dbg' */
#endif
    BOOT_STAGE(PERSIST);
    persist_boot();
    BOOT_STAGE(STORES);
    fm6_boot();                                         /* the FM6 user bank (fm6_store.c) */
#if CZ_NUSER
    czb_find();                                         /* the CZ collection (nbank.c) */
#endif
#if FELUCCA_OTA
    if (flash_ok)
        ota_boot_cleanup();                             /* staging area left by an update */
#endif
    BOOT_STAGE(SETTINGS);
    settings_init();
    BOOT_STAGE(LCD);
    lcd_init();
#if FELUCCA_SPLASH
    boot_splash();                                      /* the Optimist logo and version (splash.c) */
#else
    lcd_fill(0, 0, 240, 240, C_BLACK);                  /* (no logo: a dark screen until the UI's first frame) */
#endif
#if FELUCCA_SIMD
    sine_pk_init();                                     /* the packed sine of hal/fm1_simd.h (dsp.c) */
#endif
#if FELUCCA_SIMD_PROBE
    simd_probe_boot();                                  /* EXPERIMENTAL: may reset once (simd_probe.c) */
#endif
    BOOT_STAGE(INPUT);
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    BOOT_STAGE(SYNTH);
    felucca_init();
    cpu_khz = fm1_cpu_khz();                            /* (before the audio: no ISR in the timed loop) */
#if FELUCCA_BENCH
    bench_setup();                                      /* measurement scenario (bench.c) */
#endif
#if FELUCCA_DUAL
    dual_boot();                                        /* CPU1 (dual.c): before the audio runs (a failed start
                                                         * waits 50 ms) and before the vectors are locked */
    fm1_guard_enable(FM1_GUARD_BUS | FM1_GUARD_PC);
#endif
    BOOT_STAGE(AUDIO);
    audio_init();
    BOOT_STAGE(USB);
    usb_start();
#if FELUCCA_UART
    uart_midi_init();
#endif
#if FELUCCA_BLE
    BOOT_STAGE(BLE);
    ble_midi_init();                                    /* the radio, then advertising from boot, as stock
                                                         * (midi_ble.c): after the audio and USB are set up, its
                                                         * IRQs (45, 29 at priority 2, below the audio) on with the
                                                         * rest just below */
#endif
    BOOT_STAGE(IRQS);
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    BOOT_STAGE(SPLASH);
    fm1_delay_ms(30);
    if (boot_cal_req || (fm1_in.buttons & 3u) == 3u) {
        panel_setup();                        /* OCT- + OCT+ held at power-on */
        settings_save();
    }
#if FELUCCA_SPLASH
    fm1_delay_ms(900);                                  /* (the logo stays a moment) */
#endif
    lcd_fill(0, 0, 240, 240, C_BLACK);

    healthy_since = fm1_ms;
    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if ((uint32_t)(m - healthy_since) >= 30000u && bootguard.pending) {
            bootguard_clear(&bootguard);     /* 30 s of this boot, even if TIMER4 survived reset */
        }
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);     /* battery: slow IIR */
            if (b > 0)
                song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0 && !FELUCCA_BENCH) {             /* (a bench keeps its level: main-loop timing) */
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                song.master_q12 = (k10 * k10) >> 8;            /* 0 .. ~4096 */
            }
        }
        {   /* OCT- + OCT+ held 5 s, stopped: enter UBOOT with RAM intact (debug / update); a countdown
             * shows from 2 s, letting go cancels it. Not while playing: on the drum track these are the
             * ghost / hard modifiers, held for a long time */
            static uint32_t t0, shown;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both || song.playing) {
                if (shown)
                    ui_say("UPDATE MODE ", "CANCELLED");
                shown = 0;
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != shown) {
                    char d[4] = {(char)('0' + left), '.', '.', 0};
                    ui_say("UPDATE MODE IN ", d);
                    shown = left;
                }
            } else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
#if FELUCCA_ARRANGER
                persist_flush_now();                    /* (live sections, song, project: RAM-only so far) */
#endif
                lcd_fill(0, 0, 240, 240, C_BLACK);
                draw_text_box(0, 110, 240, &FONT_S, "UBOOT", RGB(80, 120, 255), 1);
                usb_detach();
                fm1_delay_ms(30);
                bootguard.pending = 0;                  /* intentional reset: not a failed boot */
                fm1_enter_uboot();
            }
        }
        meter_tap();                                    /* the level meters (meters.c) */
        fm6_service();                                  /* DX7 SysEx for FM6 (fm6_store.c) */
#if FELUCCA_OTA
        ed_service();                                   /* web editor SysEx */
#if FELUCCA_FLASH && FELUCCA_BACKUP
        if (bk.reboot && fm1_ms - bk.reboot_ms > 150u) { /* a restore done (ed_backup.c): its reply out, restart */
            fm1_audio_stop();
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 110, 240, &FONT_S, "RESTORED: RESTART", C_WHITE, 1);
            fm1_delay_ms(50);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;                      /* intentional reset: not a failed boot */
            fm1_reboot();
        }
#endif
        ota_service();                                  /* M-UPGRADE handshake */
        if (usb.ota_req) {                              /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            panic_req = (1u << NTRK) - 1u;               /* every track (bit per track) */
#if FELUCCA_ARRANGER
            persist_flush_now();                        /* an update ends in a reset: RAM-only work first */
#endif
            if (flash_ok)
                ota_session();                          /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, C_BLACK);
            ui.force = 1;
        }
#endif
        if (usb.uboot_req) {                            /* SysEx F0 22 24 35 7D F7 from the host */
            fm1_audio_stop();
#if FELUCCA_ARRANGER
            persist_flush_now();                    /* (live sections, song, project: RAM-only so far) */
#endif
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 110, 240, &FONT_S, "UBOOT (USB)", C_WHITE, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
#if FELUCCA_CDC
        cdc_task();
#endif
        felucca_dbg.ui_frames++;
        felucca_dbg.page = ui.page;
        felucca_dbg.home = 0;                       /* (SLOOP's HOME screen is gone; the field keeps the layout) */
        felucca_dbg.stage = 1;
        ui_input();
        felucca_dbg.stage = 2;
        ui_leds();
        ui_draw();
        felucca_dbg.stage = 8;
        autosave_tick();                                /* the working project into flash, when quiet */
#if FELUCCA_BLE
        ble_bond_poll();                                /* a central bonded: saved with the settings (midi_ble.c) */
        ble_devices_poll();                             /* the scan's reports into the DEVICES list (ble_devices.c) */
#endif
#if BP23_SET
        settings_poll();                                /* a setting changed from a page or the editor (project.c) */
#endif
#if FELUCCA_ARRANGER
        sections_flush();                               /* live sections / the recorded song, when quiet */
#if SEC_LOGGED
        sec_service();                                  /* the next section staged for the audio ISR (sections.c) */
#endif
#endif
        felucca_dbg.stage = 9;
#if FELUCCA_BENCH
        bench_frame();
#endif
#if FELUCCA_DUAL
        dual_frame();                                   /* liveness ping; stage 1: the counter on screen */
#endif
        while (fm1_ms - m < 15u) {                               /* ~60 UI frames/s at most */
            ui_input();
            meter_tap();                        /* (once per audio half: the meters see every one) */
#if FELUCCA_OTA
            ed_service();                       /* editor replies without waiting for the next frame */
#endif
#if FELUCCA_IDLE
            /* Nothing to do until an interrupt: sleep. Every input arrives by one: TIMER5 (10 kHz)
             * scans the keys and encoders, polls USB and the UART (2 kHz) and counts fm1_ms;
             * ALNK0 renders the audio. So this waits 100 us at most, and what an interrupt brought
             * in just before the idle is taken at the next wake. */
            fm1_idle();
#endif
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt, boot_mode, hold;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    hold = boot_hold();
    if (hold == HOLD_UBOOT)
        fm1_enter_uboot();
    if ((p3 & 1u) && !(p3 & (4u | 0x40u)))
        bootguard_clear(&bootguard);           /* a power-on (not a watchdog or soft reset): no failed boot to count */
    boot_mode = bootguard_begin(&bootguard);
    if (boot_mode == BOOT_ROM) {
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    boot_cal_req = (uint8_t)(hold == HOLD_CAL);
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
    for (s = _rh_load, d = _rh_start; d < _rh_end; s++, d++)
        *d = *s;                                /* the audio path (core.h HOT), before any IRQ */
    for (s = _rh2_load, d = _rh2_start; d < _rh2_end; s++, d++)
        *d = *s;                                /* its overflow into RAM (core.h HOT2) */
    fm1_mailbox_clear();
#if FELUCCA_DUAL
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE);   /* the bus and PC guards after CPU1's start
                                                             * (dual_boot): whether they cover CPU1 and its
                                                             * ROM start is not known */
#else
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
#endif
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
#if FELUCCA_OTA
    if (boot_mode == BOOT_RECOVERY || recovery_key())
        recovery_main();
#else
    if (boot_mode == BOOT_RECOVERY) fm1_enter_uboot();
#endif
    fm1_main();
    for (;;)
        ;
}
