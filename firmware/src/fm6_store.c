/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * FM6 engine: Kerem Kilic (Melodee, github.com/keremimo/melodee, fm6_store.c at e459da5), GPL-3.0-only;
 * ported to SLOOP: the bank in a USR sample slot, no second 4 KiB buffer */
/* FM6 voices outside the engine (eng_fm6.c): the user bank in flash, DX7 SysEx from USB-MIDI and back
 * (fm6_service: main loop), and the actions of the STORE page (ui_fm6.c GLO > store).
 *
 * DX7 SysEx accepted on any channel n:
 *   F0 43 0n 00 01 1B <155 bytes> <checksum> F7    a voice (VCED) -> the FM6 part's buffer
 *   F0 43 0n 09 20 00 <4096 bytes> <checksum> F7   32 voices (VMEM) -> the user bank, saved
 *   F0 43 1n gg pp dd F7                           a voice parameter pp + 128 gg (155: the six
 *                                                  operator switches, OP1 = bit 5) -> the part
 *   F0 43 1n 08 pp dd F7                           a function parameter (64 mono, 65 bend range,
 *                                                  66 step, 68 glissando, 69 portamento time,
 *                                                  70..77 wheel / foot / breath / aftertouch
 *                                                  range and target) -> the part
 *   F0 43 2n 00 F7 / F0 43 2n 09 F7                dump requests: the part's voice / the bank
 * The FM6 part: the selected track when it plays FM6, else part n + 1, else the first FM6 part.
 * Dexed or any DX7 librarian can so edit a part live and keep the banks.
 *
 * The user bank in flash (SLOOP-plus): in the banks area at the end of the old USR3 range (eng_sample.c
 * SMP_BANKS), next to the user drum kits: a smp_user_hdr_t with magic "FM6B" at 0xD8000 and the 4096 bank
 * bytes (VMEM, 7-bit) at 0xD9000. VOICE U01..U32 read the bank where it is in flash (no RAM copy:
 * eng_fm6.c fm6_bank_xip); no bank: the init voice. Saved as erase, data, header last: a save cut short
 * loses the bank (the init voices come back). Older builds kept it in a USR sample slot (the same header
 * at the slot's start, the data in its second sector): fm6_boot moves such a bank here (data, header, then
 * the slot's header erased: the slot is free for a sample again; a move cut short finishes next boot). */
#define FM6_MAGIC 0x42364D46u                            /* "FM6B" */
#define FM6_BANK_OFF 0x1000u                             /* the bank: its header's next sector (old slots too) */
#define FM6_HDR SMP_BANKS                                /* 0xD8000 */
#define FM6_DATA (SMP_BANKS + FM6_BANK_OFF)              /* 0xD9000 */
#define FM6_XIP(off) (smp_user_xip(2) + ((off) - (SMP_USER_BASE + 2u * SMP_USER_SIZE)))   /* (the host's slot image too) */
#define FM6_BANK_N (FM6_NUSER * 128u)
#if FELUCCA_FLASH
#define FM6_FLASH_OK flash_ok
#else
#define FM6_FLASH_OK 0
#endif
static uint32_t fm6_bank_gen = ~0u;                      /* smp_user_gen the bank was looked up at */

static void fm6_bank_copy(uint8_t *d)                    /* the bank as VOICE U01..U32 has it -> d */
{
    static int16_t ed[FM6_NP];
    uint32_t k;
    for (k = 0; k < FM6_NUSER; k++) {
        fm6_user(ed, k);
        fm6_pack(d + k * 128u, ed);
    }
}

#if FELUCCA_FLASH
static int fm6_has_bank(const uint8_t *hdr)              /* a valid bank: its header here, the data a sector on */
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)hdr;
    return h->magic == FM6_MAGIC && h->version == 1 && h->nz == FM6_NUSER && h->data_len == FM6_BANK_N &&
           h->crc == st_crc32(hdr + FM6_BANK_OFF, FM6_BANK_N);
}
#endif

static void fm6_bank_find(void)                          /* main loop: the bank (boot, a save) */
{
    const uint8_t *b = 0;
#if FELUCCA_FLASH
    if (flash_ok && fm6_has_bank(FM6_XIP(FM6_HDR)))
        b = FM6_XIP(FM6_DATA);
#endif
    fm6_bank_xip = b;
    fm6_bank_gen = smp_user_gen;
}

static int fm6_bank_save(const uint8_t *src);

/* boot: a bank an older build left in USR slot k -> the banks area, then the slot's header erased. A
 * move cut short (the new copy whole, the old header still there) is finished; a damaged old bank stays. */
static void fm6_bank_move(void)
{
#if FELUCCA_FLASH
    uint32_t k, took;
    if (!flash_ok)
        return;
    for (k = 0; k < SMP_USER_SLOTS; k++) {
        uint32_t base = SMP_USER_BASE + k * SMP_USER_SIZE;
        if (!fm6_has_bank(smp_user_xip(k)))
            continue;
        if (!fm6_has_bank(FM6_XIP(FM6_HDR)) ||
            memcmp(FM6_XIP(FM6_DATA), smp_user_xip(k) + FM6_BANK_OFF, FM6_BANK_N)) {
            memcpy(fm6_rx, smp_user_xip(k) + FM6_BANK_OFF, FM6_BANK_N);   /* (RAM: flash writes cannot read XIP) */
            if (fm6_bank_save(fm6_rx))
                return;                                  /* (not moved: the old copy stays in charge) */
        }
        fl_erase4k_quiet(base, &took);                   /* the slot is empty again */
        fm1_irq_off();
        fl_inval(base, 0x1000u);
        fm1_irq_on();
        smp_user_scan(k);
    }
#endif
}

static void fm6_boot(void)                               /* main.c, after persist_boot */
{
    fm6_tables_init();                                   /* the engine's RAM tables (eng_fm6.c) */
    fm6_bank_find();                                     /* the user bank */
    fm6_bank_move();                                     /* (an older build's, from a USR slot) */
}

/* the bank (FM6_BANK_N bytes in RAM, 7-bit) -> flash; 0 = saved, -1 = no flash or a write failed */
static int fm6_bank_save(const uint8_t *src)
{
    if (!FELUCCA_FM6_STORE)                              /* (registry.h: no user bank store in this build) */
        return -1;
#if FELUCCA_FLASH
    static smp_user_hdr_t h;
    uint32_t base = FM6_HDR, took;
    int rc;
    if (!flash_ok)
        return -1;
    memset(&h, 0, sizeof h);
    h.magic = FM6_MAGIC;
    h.version = 1;
    h.nz = (uint8_t)FM6_NUSER;
    memcpy(h.name, "FM6 BANK", sizeof h.name);
    h.data_len = FM6_BANK_N;
    h.crc = st_crc32(src, FM6_BANK_N);
    fm6_bank_xip = 0;                                    /* (meanwhile VOICE U.. load the init voice) */
    rc = fl_erase4k_quiet(base, &took) || fl_erase4k_quiet(base + FM6_BANK_OFF, &took) ||
         fl_write(base + FM6_BANK_OFF, src, FM6_BANK_N) || fl_write(base, (const uint8_t *)&h, sizeof h);
    fm1_irq_off();
    fl_inval(base, 2u * 0x1000u);
    fm1_irq_on();
    fm6_bank_find();
    return !rc && fm6_bank_xip ? 0 : -1;
#else
    (void)src;
    return -1;
#endif
}

static int fm6_is(uint32_t k) { return k < NPART && ENG_IS(ENGINES[trk[k].eng_req % NENGINES], FM6); }

static int fm6_part(uint32_t ch)                         /* the FM6 part a DX7 message on channel ch is for, -1 */
{
    uint32_t k;
    if (fm6_is(song.sel))
        return (int)song.sel;
    if (fm6_is(ch))
        return (int)ch;
    for (k = 0; k < NPART; k++)
        if (fm6_is(k))
            return (int)k;
    return -1;
}

static uint8_t fm6_chk(const uint8_t *p, uint32_t n)    /* DX7 checksum: data + it = 0 mod 128 */
{
    uint32_t s = 0;
    while (n--)
        s += *p++;
    return (uint8_t)(-s & 0x7Fu);
}

/* ------------------------------------------------------------- out --- */
/* a dump goes out of fm6_rx, the receive buffer, while the main loop holds it (fm6_rx_ready set: the ISR
 * leaves it alone): during fm6_service (the request's frame is read already), or claimed here */
static int fm6_tx_claim(void)
{
    int ok;
    fm1_irq_off();
    ok = !fm6_rx_ready;
    if (ok) {
        fm6_rx_ready = 1;
        fm6_rx_on = 0;                                   /* (a frame coming in now is dropped) */
    }
    fm1_irq_on();
    return ok;
}

#if FELUCCA_OTA
static void fm6_send_voice(uint32_t p)                   /* VCED: the part's voice (fm6_rx held) */
{
    uint32_t i;
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
    memcpy(fm6_rx, H, 6);
    for (i = 0; i < 155u; i++)
        fm6_rx[6 + i] = (uint8_t)fm6_ed[p][i];
    fm6_rx[161] = fm6_chk(fm6_rx + 6, 155);
    fm6_rx[162] = 0xF7;
    ota_wire_send(fm6_rx, 163);
}

static void fm6_send_bank(void)                          /* VMEM: the user bank (fm6_rx held) */
{
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    memcpy(fm6_rx, H, 6);
    fm6_bank_copy(fm6_rx + 6);
    fm6_rx[4102] = fm6_chk(fm6_rx + 6, 4096);
    fm6_rx[4103] = 0xF7;
    ota_wire_send(fm6_rx, FM6_RX);
}
#endif

/* ------------------------------------------------------------- in --- */
static void fm6_sysex(const uint8_t *b, uint32_t n)
{
    uint32_t i, st = b[2] & 0xF0u, ch = b[2] & 15u;
    int p = fm6_part(ch);
    char nm[12];
    if (n == 163u && st == 0x00u && b[3] == 0x00 && b[4] == 0x01 && b[5] == 0x1B &&
        fm6_chk(b + 6, 155) == b[161]) {                  /* one voice */
        if (p < 0) {
            ui_message("FM6: NO FM6 TRACK");
            return;
        }
        fm1_irq_off();
        for (i = 0; i < 155u; i++)
            fm6_set(fm6_ed[p], i, b[6 + i]);
        for (i = 0; i < 6u; i++)
            fm6_ed[p][FV_ON + i] = 1;
        fm6_pt[p].panic = 1;                             /* a new voice: the notes stop, as in Dexed */
        fm1_irq_on();
        fm6_name(nm, fm6_ed[p]);
        ui_say("FM6 VOICE ", nm);
        ui.force = 1;
    } else if (n == FM6_RX && st == 0x00u && b[3] == 0x09 && b[4] == 0x20 && b[5] == 0x00 &&
               fm6_chk(b + 6, 4096) == b[4102]) {         /* 32 voices (b is fm6_rx, the main loop's now) */
        for (i = 0; i < 4096u; i++)
            fm6_rx[6 + i] &= 0x7Fu;
        ui_message(!fm6_bank_save(fm6_rx + 6) ? "FM6 BANK SAVED" : FM6_FLASH_OK ? "FM6 BANK: WRITE FAILED" : "FM6 BANK: NO FLASH");
        ui.force = 1;
    } else if (n == 7u && st == 0x10u && !(b[3] >> 2) && p >= 0) {   /* a voice parameter */
        uint32_t k = (uint32_t)(b[3] & 3u) << 7 | b[4];
        if (k < 155u) {
            fm6_set(fm6_ed[p], k, b[5]);
        } else if (k == 155u) {
            for (i = 0; i < 6u; i++)
                fm6_ed[p][FV_ON + i] = (int16_t)((b[5] >> (5u - i)) & 1u);
        }
        ui.force = 1;
    } else if (n == 7u && st == 0x10u && b[3] == 0x08u && p >= 0) {   /* a function parameter */
        int16_t *ed = fm6_ed[p];
        uint32_t v = b[5];
        fm1_irq_off();
        switch (b[4]) {
        case 64:                                         /* MONO: Dexed's (legato, the highest key) */
            trk[p].p[P_VOICE] = v ? V_LEGATO : V_POLY;
            if (v)
                trk[p].p[P_PRIO] = 2;
            break;
        case 65:
            fm6_set(ed, FN_PBUP, (int32_t)v);
            fm6_set(ed, FN_PBDN, (int32_t)v);
            break;
        case 66: fm6_set(ed, FN_PBSTEP, (int32_t)v); break;
        case 68: fm6_set(ed, FN_GLISS, (int32_t)v); break;
        case 69: fm6_set(ed, FN_PTIME, (int32_t)(v > 99u ? 99u : v) * 127 / 99); break;   /* 0..99 -> CC 5 */
        default:
            if (b[4] >= 70u && b[4] <= 77u)              /* range, target: wheel, foot, breath, aftertouch */
                fm6_set(ed, FN_MWR + (uint32_t)(b[4] - 70u), (int32_t)v);
            break;
        }
        fm1_irq_on();
        ui.force = 1;
    } else if (n == 5u && st == 0x20u) {                 /* dump requests (fm6_rx is the main loop's now) */
#if FELUCCA_OTA
        if (b[3] == 0x00 && p >= 0)
            fm6_send_voice((uint32_t)p);
        else if (b[3] == 0x09)
            fm6_send_bank();
#endif
    }
}

static void fm6_service(void)                            /* main loop: a DX7 frame from USB-MIDI */
{
    if (fm6_bank_gen != smp_user_gen)
        fm6_bank_find();                                 /* a sample slot changed (an upload over the bank?) */
    if (!FELUCCA_FM6_SYSEX || !fm6_rx_ready)
        return;
    RING_PUBLISH();                                      /* read the frame only after the flag */
    fm6_sysex(fm6_rx, fm6_rx_n);
    RING_PUBLISH();
    fm6_rx_ready = 0;
}

/* --------------------------------------------------------- STORE --- */
/* STORE: the selected part's voice -> user slot k, and VOICE follows it (edits are kept). The bank is
 * rebuilt in fm6_rx (held meanwhile) and written to flash; no USR slot free: nothing is stored */
static int fm6_tx_claim(void);
static void fm6_store(uint32_t k)
{
    uint32_t p = song.sel;
    char nm[12];
    int rc;
    if (!fm6_is(p) || k >= FM6_NUSER)
        return;
    if (!FELUCCA_FM6_STORE) {
        ui_message("NO USER BANK");
        return;
    }
    if (!fm6_tx_claim()) {
        ui_message("SYSEX BUSY");
        return;
    }
    fm6_bank_copy(fm6_rx);
    fm6_pack(fm6_rx + k * 128u, fm6_ed[p]);
    rc = fm6_bank_save(fm6_rx);
    RING_PUBLISH();
    fm6_rx_ready = 0;
    fm6_name(nm, fm6_ed[p]);
    if (rc) {
        ui_message(FM6_FLASH_OK ? "STORE: WRITE FAILED" : "STORE: NO FLASH");
        return;
    }
    fm1_irq_off();
    trk[p].p[P_E0] = (int16_t)(FM6_NROM + k);
    fm6_cur[p] = (int16_t)(FM6_NROM + k + 1u);
    fm1_irq_on();
    ui_say("STORED ", nm);
}

static void fm6_init_voice(void)                         /* INIT: the selected part starts from the init voice */
{
    if (!fm6_is(song.sel))
        return;
    fm1_irq_off();
    fm6_from_rom(fm6_ed[song.sel], &FM6_INIT);
    fm6_pt[song.sel].panic = 1;
    fm1_irq_on();
    ui_message("INIT VOICE");
}

static void fm6_send(void)                               /* SEND: the selected part's voice as a DX7 dump */
{
#if FELUCCA_OTA
    if (!FELUCCA_FM6_SYSEX)
        ui_message("NO SYSEX OUT");
    else if (fm6_is(song.sel)) {
        if (!fm6_tx_claim()) {
            ui_message("SYSEX BUSY");
            return;
        }
        fm6_send_voice(song.sel);
        RING_PUBLISH();
        fm6_rx_ready = 0;
        ui_message("VOICE SENT");
    }
#else
    ui_message("NO SYSEX OUT");
#endif
}
