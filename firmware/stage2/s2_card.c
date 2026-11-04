/* SPDX-License-Identifier: GPL-3.0-only */
/* The card for MENU.BIN: Optimist's SPI driver (storage/sd/sd_mmc.c on hal/fm1_sd_bus.h, the code that runs on the SD
 * boards) under Petit FatFs. The panel shares the bus, so each call below is a bus session of its own, and the screen
 * draws between them. Reads stay at the driver's floor (6 MHz): every board built reads there, and the job is 1 MB. */
#include "s2_card.h"
#include "s2_flash.h"
#include "fm1_sd_bus.h"
#include "fm1_time.h"
#include "pff.h"

#define PANEL_BAUD 4u                              /* as m-bootloader's display.c sets it */
#define SD_TX(b) fm1_sd_tx(b)
#define SD_RX() fm1_sd_rx()
#define SD_SEL() fm1_sd_sel()
#define SD_DESEL() fm1_sd_desel()
#define SD_FAST() fm1_sd_baud(FM1_SD_BAUD_FAST)
#define SD_SLOW() fm1_sd_baud(FM1_SD_BAUD_SLOW)
#define SD_NSTEP FM1_SD_NSTEP
#define SD_NSTEP_HS FM1_SD_NSTEP_HS
#define SD_SET_STEP(k) fm1_sd_baud(FM1_SD_STEP_BAUD[k])
#define SD_STEP_KHZ(k) FM1_SD_STEP_KHZ(k)
#define SD_CLOCK_FIXED() (fm1_sd_mode == FM1_SD_BITBANG)
#define SD_BAUD() ((uint32_t)FM1_SD_STEP_BAUD[sd_step])
#define SD_NOW() fm1_ticks()
#define SD_TICKS_MS (FM1_TICKS_PER_US * 1000u)
#define SD_RXBLK(p, n) fm1_sd_rx_block(p, n)
#define SD_DMA_OK() 0                              /* the CPU reads: nothing else runs here */
#define SD_RXDMA(p, n) fm1_sd_rx_dma(p, n)
#define SD_TXDMA(p, n) fm1_sd_tx_dma(p, n)
#define SD_DMA_DONE() fm1_sd_rx_dma_done()
#define SD_DMA_END() fm1_sd_rx_dma_end()
#include "sd_mmc.c"

static FATFS fs;
static struct s2_lfn lfn;
static char folder[S2_NAME_MAX + 2];  /* "/FIRMWA~1" */

/* The panel's NOP ends any memory write, so what the card's session clocks past it is ignored, then the bus is the
 * card's (src/storage/sd/sdcard.c sd_begin). */
static void session_begin(uint32_t baud)
{
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_PC_OUT &= ~FM1_LCD_DC;
    FM1_LCD_SPI_CON = FM1_SD_CON_LCD;
    FM1_LCD_SPI_BAUD = PANEL_BAUD;
    FM1_LCD_SPI_BUF = 0;
    fm1_sd_spi_done();
    FM1_LCD_PC_OUT |= FM1_LCD_DC;
    fm1_sd_bus_begin(baud);
}

static void session_end(void)
{
    fm1_sd_bus_end();
    FM1_LCD_SPI_BAUD = PANEL_BAUD;
}

bool s2_card_mount(void)
{
    session_begin(FM1_SD_BAUD_SLOW);
    bool ok = pf_mount(&fs) == FR_OK;
    session_end();
    return ok;
}

static void copy_name(char *dst, const char *src)
{
    unsigned i = 0;
    for (; src[i] && i < S2_NAME_MAX; ++i)
        dst[i] = src[i];
    dst[i] = 0;
}

/* Petit FatFs reads 8.3 names only, and the folder's name is longer: its short name is found in the root. */
static bool find_folder(void)
{
    static DIR dir;
    static FILINFO info;
    if (pf_opendir(&dir, "/") != FR_OK)
        return false;
    while (pf_readdir(&dir, &info) == FR_OK && info.fname[0])
        if ((info.fattrib & AM_DIR) && s2_is_folder(info.fname)) {
            folder[0] = '/';
            copy_name(folder + 1, info.fname);
            return true;
        }
    return false;
}

void s2_dir_hook(const BYTE *entry);
void s2_dir_hook(const BYTE *entry)
{
    s2_lfn_feed(&lfn, entry);
}

unsigned s2_card_list(struct s2_file *files, unsigned max)
{
    static DIR dir;
    static FILINFO info;
    unsigned n = 0;
    session_begin(FM1_SD_BAUD_FAST);
    if (find_folder() && pf_opendir(&dir, folder) == FR_OK)
        while (n < max && pf_readdir(&dir, &info) == FR_OK && info.fname[0])
            if (!(info.fattrib & AM_DIR) && s2_is_firmware(info.fname)) {
                copy_name(files[n].name, info.fname);
                s2_label(files[n++].label, &lfn, info.fname);
            }
    session_end();
    return n;
}

bool s2_card_open(const char *name, uint32_t *size)
{
    static char path[2 * S2_NAME_MAX + 3];
    unsigned n = 0;
    for (const char *p = folder; *p; ++p)
        path[n++] = *p;
    path[n++] = '/';
    copy_name(path + n, name);
    session_begin(FM1_SD_BAUD_FAST);
    bool ok = pf_open(path) == FR_OK;
    session_end();
    *size = fs.fsize;
    return ok;
}

bool s2_card_read(void *ctx, uint32_t offset, void *dst, uint32_t size)
{
    UINT got = 0;
    (void)ctx;
    session_begin(FM1_SD_BAUD_FAST);
    bool ok = pf_lseek(offset) == FR_OK && pf_read(dst, size, &got) == FR_OK && got == size;
    session_end();
    return ok;
}
