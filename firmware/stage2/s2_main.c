/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU.BIN: m-bootloader's stage 2 (its docs/SPEC.md 11.5) that lists the packages in /FIRMWARES on the card and writes
 * the one picked over the app slot. m-bootloader stays in RAM below 0x01C0A800 and this runs on its stack, so BACK simply
 * returns to its menu; its code has no fixed addresses, so the flash, screen and key code here are the same sources
 * compiled in. It resets after a write, and a write that broke off leaves m-bootloader's menu and the card to try again:
 * the boot area is never touched. */
#include <string.h>
#include "board.h"
#include "core.h"
#include "display.h"
#include "fm1_time.h"
#include "platform.h"
#include "s2_card.h"
#include "s2_flash.h"

#define BOARD_FM1 1u
#define WHITE 0xffffu
#define YELLOW 0xffe0u
#define RED 0xf800u
#define GREEN 0x07e0u
#define BLACK 0u
#define COLUMNS 20u
#define ROW_TITLE 0u
#define ROW_LIST 2u
#define ROWS_LIST 11u
#define ROW_HINT 14u
#define ROWS_ALL 15u
#define MAX_FILES 32u
#define POLL_MS 20u
#define STABLE_POLLS 2u
#define CONFIRM_MS 2000u
#define KEY_ERR 0xffffu            /* a unit without its efuse key */

static struct s2_file files[MAX_FILES];
static uint8_t sector_a[S2_SECTOR], sector_b[S2_SECTOR];
static uint8_t flash_tail[256];
static struct core_nor nor;
static struct core_app app;
static uint16_t unit_key;
static unsigned last_percent;

/* The bootloader's font has neither '_' nor '~'. */
static void show(unsigned row, uint16_t fg, uint16_t bg, const char *text)
{
    char line[COLUMNS];
    unsigned n = 0;
    for (; text[n] && n < COLUMNS; ++n) {
        char c = text[n];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        line[n] = c == '_' || c == '~' ? '-' : c;
    }
    display_rect(0, row * 16, 240, 16, bg);
    display_row(row, fg, bg, line, n);
}

static void screen(const char *title)
{
    display_rect(0, 0, 240, ROWS_ALL * 16, BLACK);
    show(ROW_TITLE, YELLOW, BLACK, title);
}

static void wait_release(void)
{
    while (board_keys())
        board_wait(POLL_MS, false);
}

/* The next key pressed, debounced. */
static unsigned next_key(void)
{
    unsigned stable = 0, seen = 0;
    wait_release();
    while (stable < STABLE_POLLS) {
        unsigned k = board_keys();
        stable = k && k == seen ? stable + 1 : 0;
        seen = k;
        board_wait(POLL_MS, false);
    }
    return seen;
}

static void progress(void *ctx, uint32_t done, uint32_t total)
{
    unsigned percent = done / (total / 100u + 1u);
    (void)ctx;
    if (percent != last_percent) {
        last_percent = percent;
        display_progress(percent, GREEN);
    }
}

static void stop_on(enum s2_err e)
{
    show(6, RED, BLACK, s2_err_text(e));
    show(ROW_HINT, WHITE, BLACK, "ANY KEY: BACK");
    next_key();
}

static void draw_list(unsigned count, unsigned at)
{
    unsigned first = at >= ROWS_LIST ? at - ROWS_LIST + 1 : 0;
    for (unsigned r = 0; r < ROWS_LIST; ++r) {
        unsigned i = first + r;
        uint16_t bg = i == at ? WHITE : BLACK, fg = i == at ? BLACK : WHITE;
        show(ROW_LIST + r, fg, bg, i < count ? files[i].label : i == count ? "BACK" : "");
    }
}

/* The index picked; count is BACK. */
static unsigned pick(unsigned count)
{
    unsigned at = 0;
    screen("FIRMWARE FROM CARD");
    show(ROW_HINT, WHITE, BLACK, count ? "SELECT: CHOOSE" : "NO FILES IN /FIRMWARES");
    for (;;) {
        draw_list(count, at);
        unsigned k = next_key();
        if (k & KEY_SELECT)
            return at;
        if (k & KEY_NEXT)
            at = at == count ? 0 : at + 1;
        if (k & KEY_PREV)
            at = at ? at - 1 : count;
    }
}

static uint32_t installed_end(void)
{
    return core_app_mount(&nor.flash, unit_key, &app) ? s2_slot_end(app.base + app.extent) : 0;
}

/* Everything that can refuse, before a byte is erased. */
static enum s2_err check(const char *name, const struct s2_src *src, struct s2_pkg *pkg)
{
    uint32_t size = 0;
    uint16_t key = 0;
    if (!s2_card_open(name, &size))
        return S2_E_READ;
    enum s2_err e = s2_pkg_open(src, size, pkg);
    if (e == S2_OK) {
        last_percent = 101u;
        e = s2_pkg_verify(src, pkg, sector_a, progress, 0);
    }
    if (e == S2_OK)
        e = s2_pkg_key(src, pkg, sector_a, &key);
    return e == S2_OK ? s2_pkg_fits(pkg, key, unit_key, installed_end()) : e;
}

static bool confirmed(const char *label, const struct s2_pkg *pkg)
{
    screen("WRITE THIS FIRMWARE?");
    show(2, WHITE, BLACK, label);
    show(3, WHITE, BLACK, pkg->id);
    show(ROW_HINT - 1, WHITE, BLACK, "HOLD SELECT: WRITE");
    show(ROW_HINT, WHITE, BLACK, "PREV: BACK");
    wait_release();
    for (;;) {
        if (board_keys() & KEY_PREV)
            return false;
        if (board_hold(KEY_SELECT, CONFIRM_MS))
            return true;
        board_wait(POLL_MS, false);
    }
}

/* Returns only when nothing was written, or the write failed. */
static void flash_one(const struct s2_file *file)
{
    const struct s2_src src = {0, s2_card_read};
    struct s2_pkg pkg;
    screen("CHECKING");
    show(2, WHITE, BLACK, file->label);
    enum s2_err e = check(file->name, &src, &pkg);
    if (e != S2_OK) {
        stop_on(e);
        return;
    }
    if (!confirmed(file->label, &pkg))
        return;
    screen("WRITING: KEEP POWER ON");
    show(2, WHITE, BLACK, file->label);
    last_percent = 101u;
    e = s2_install(&src, &pkg, &nor, sector_a, sector_b, progress, 0);
    if (e != S2_OK) {
        stop_on(e);
        return;
    }
    show(6, GREEN, BLACK, "DONE");
    board_wait(CONFIRM_MS, false);
    hal_reset();
}

static void identify(void)
{
    nor.flash.ctx = 0;
    nor.flash.capacity = flash_capacity();
    nor.flash.read = flash_read;
    nor.program = flash_program;
    nor.erase = flash_erase;
    unit_key = (uint16_t)(hal_efuse(1, 3) >> 8);
    if (unit_key != KEY_ERR || nor.flash.capacity < sizeof flash_tail)
        return;
    if (!core_read(&nor.flash, nor.flash.capacity - sizeof flash_tail, flash_tail, sizeof flash_tail))
        return;
    bool nonzero = false;
    for (unsigned i = 0; i < 8; ++i)
        nonzero = nonzero || flash_tail[i];
    if (nonzero && core_crc16(0, flash_tail, 8) == core_le16(flash_tail + 8))
        unit_key = core_le16(flash_tail + 5);
}

static void browse(void)
{
    for (;;) {
        if (!s2_card_mount()) {
            screen("FIRMWARE FROM CARD");
            stop_on(S2_E_READ);
            return;
        }
        unsigned count = s2_card_list(files, MAX_FILES);
        unsigned at = pick(count);
        if (at == count)
            return;
        flash_one(&files[at]);
    }
}

static void s2_main(unsigned lsb_mhz)
{
    hal_timer_clock(lsb_mhz);
    fm1_time_init();
    display_init(BOARD_FM1);
    board_init(false);
    if (flash_init(1, 1)) {
        identify();
        wait_release();
        browse();
    } else {
        screen("FIRMWARE FROM CARD");
        stop_on(S2_E_FLASH);
    }
    display_rect(0, 0, 240, ROWS_ALL * 16, BLACK);
}

/* First in the image. The file holds no .bss, and m-bootloader's stack is below us. */
__attribute__((section(".entry"))) void s2_entry(unsigned lsb_mhz)
{
    extern char __bss_start[], __bss_end[];
    memset(__bss_start, 0, (size_t)(__bss_end - __bss_start));
    s2_main(lsb_mhz);
}
