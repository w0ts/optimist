/*------------------------------------------------------------------------/
/  MMCv3/SDv1/SDv2 (in SPI mode) control module for PFF
/-------------------------------------------------------------------------/
/
/  Copyright (C) 2014, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/--------------------------------------------------------------------------/
 From Petit FatFs' generic/mmcbbp.c (pfsample, elm-chan.org). Optimist (2026-10-25, GPL-3.0-only as a whole): the byte
 I/O is the FM-1's SD bus (hal/fm1_sd_bus.h, builder item SDCARD) or a host test's card (tests/sd_test.c), through the
 SD_* macros; the waits are timed (SD_NOW) instead of counted 100 us sleeps; the card's capacity (CSD) is read for the
 console. The command sequence is ChaN's. Added (2026-10-25): whole blocks by CMD17 / CMD18 (stopped by CMD12, as
 ChaN's full FatFs mmc driver does) through a polled reader (sd_rd_*: the data by DMA where the bus has it, so the CPU
 is free while 512 bytes come in), a block write (CMD24, the data by DMA), the blocks' CRC16 checked. Later (the second
 2026-10-25 review): every command carries its real CRC7 and CMD59 turns the card's CRC checking on, so a garbled
 command (or a stray 0x4C on DI during a CMD18 run: a false CMD12) is refused, and every block written carries its
 CRC16; every block read (disk_readp too) has its CRC16 checked (SD_RD_CRC 1, a nibble table). 2026-10-26: under
 full FatFs too (FELUCCA_SD_FATFS: fatfs/, the glue in sd_diskio.c): the start-up is sd_card_init, the partial read
 sd_readp; Petit FatFs' own entry points (disk_initialize, disk_readp, disk_writep) are at the end, Petit builds only.
 2026-11-04: the clock a step of hal/fm1_sd_clock.h (sd_step: 30 .. 6 MHz), found at the mount (sd_clock_probe), one
 step down at run time on a CRC failure (sd_crc_retry).
/-------------------------------------------------------------------------*/

#if FELUCCA_SD_FATFS
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#else
#include "diskio.h"
#endif

#ifndef SD_TX                               /* the FM-1's bus (a host test defines its own) */
#include "fm1_sd_bus.h"
#include "fm1_time.h"
#define SD_TX(b) fm1_sd_tx(b)
#define SD_RX() fm1_sd_rx()
#define SD_SEL() fm1_sd_sel()
#define SD_DESEL() fm1_sd_desel()
#define SD_FAST() fm1_sd_baud(FM1_SD_BAUD_FAST)   /* the card is up: the floor (6 MHz) */
#define SD_SLOW() fm1_sd_baud(FM1_SD_BAUD_SLOW)   /* the start-up's 400 kHz (the probe's recovery: sd_card_init again) */
#define SD_NSTEP FM1_SD_NSTEP                     /* the clocks, fastest first, the floor last (hal/fm1_sd_clock.h) */
#define SD_NSTEP_HS FM1_SD_NSTEP_HS
#define SD_SET_STEP(k) fm1_sd_baud(FM1_SD_STEP_BAUD[k])
#define SD_STEP_KHZ(k) FM1_SD_STEP_KHZ(k)
#define SD_CLOCK_FIXED() (fm1_sd_mode == FM1_SD_BITBANG)   /* (bit-bang: the GPIO's own pace, no clock to pick) */
#define SD_BAUD() ((uint32_t)FM1_SD_STEP_BAUD[sd_step])   /* every session's clock (sdcard.c) */
#define SD_NOW() fm1_ticks()
#define SD_TICKS_MS (FM1_TICKS_PER_US * 1000u)
#define SD_RXBLK(p, n) fm1_sd_rx_block(p, n)
#define SD_DMA_OK() (fm1_sd_dma && fm1_sd_mode != FM1_SD_BITBANG)
#define SD_RXDMA(p, n) fm1_sd_rx_dma(p, n)
#define SD_TXDMA(p, n) fm1_sd_tx_dma(p, n)
#define SD_DMA_DONE() fm1_sd_rx_dma_done()
#define SD_DMA_END() fm1_sd_rx_dma_end()
#define SD_WDT() fm1_wdt_feed()                   /* (the clock probe: a step at a time, at most SD_NSTEP of them) */
#endif

#ifndef SD_WDT
#define SD_WDT() ((void)0)                        /* (a host test: no watchdog) */
#endif
static DWORD sd_ms_since(DWORD t0) { return (DWORD)(SD_NOW() - t0) / SD_TICKS_MS; }   /* (wraps cleanly) */

/* Definitions for MMC/SDC command */
#define CMD0	(0x40+0)	/* GO_IDLE_STATE */
#define CMD1	(0x40+1)	/* SEND_OP_COND (MMC) */
#define	ACMD41	(0xC0+41)	/* SEND_OP_COND (SDC) */
#define CMD6	(0x40+6)	/* SWITCH_FUNC */
#define CMD8	(0x40+8)	/* SEND_IF_COND */
#define CMD9	(0x40+9)	/* SEND_CSD */
#define CMD10	(0x40+10)	/* SEND_CID */
#define CMD12	(0x40+12)	/* STOP_TRANSMISSION */
#define CMD16	(0x40+16)	/* SET_BLOCKLEN */
#define CMD17	(0x40+17)	/* READ_SINGLE_BLOCK */
#define CMD18	(0x40+18)	/* READ_MULTIPLE_BLOCK */
#define CMD24	(0x40+24)	/* WRITE_BLOCK */
#define CMD25	(0x40+25)	/* WRITE_MULTIPLE_BLOCK */
#define CMD55	(0x40+55)	/* APP_CMD */
#define CMD58	(0x40+58)	/* READ_OCR */
#define CMD59	(0x40+59)	/* CRC_ON_OFF */

/* Card type flags (CardType) */
#define CT_MMC				0x01	/* MMC ver 3 */
#define CT_SD1				0x02	/* SD ver 1 */
#define CT_SD2				0x04	/* SD ver 2 */
#define CT_SDC				(CT_SD1|CT_SD2)	/* SD */
#define CT_BLOCK			0x08	/* Block addressing */

static BYTE CardType;			/* b0:MMC, b1:SDv1, b2:SDv2, b3:Block addressing */
static DWORD sd_sectors;		/* the card's capacity in 512-byte sectors (CSD; 0: not read) */
#ifndef SD_RD_CRC
#define SD_RD_CRC 1						/* every block's CRC16 checked (a nibble table: ~1 K operations a block) */
#endif
static WORD sd_wait_ms = 100;	/* a read's token: 100 ms; the clock probe's SD_CLK_WAIT_MS */
static BYTE sd_crc_on;			/* the card checks the CRCs (CMD59 accepted) */
#define SD_FLOOR (SD_NSTEP - 1u)		/* the last step: the clock that always reads (6 MHz on the FM-1) */
static BYTE sd_step = SD_FLOOR;	/* the clock every session runs at: a step, 0 the fastest (sd_clock_probe, sd_crc_retry) */
static DWORD sd_crc_fails, sd_demotions;	/* blocks whose CRC16 failed; steps down for them at run time */
static BYTE sd_crc_bad;			/* the last sd_readp failed on the block's CRC16 (not a timeout) */
#define SD_CLOCK_KHZ() ((DWORD)SD_STEP_KHZ(sd_step))
#define SD_CLOCK_RESET() (sd_step = SD_FLOOR)	/* (sdcard.c: a mount starts at the floor) */

/* CRC16-CCITT (x^16 + x^12 + x^5 + 1, from 0), the SD data block's: a nibble at a time (a 32-byte table) */
static const WORD SD_CRC16_NIB[16] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
	0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF
};
static WORD sd_crc16_up(WORD c, BYTE b)
{
	c = (WORD)((c << 4) ^ SD_CRC16_NIB[(c >> 12) ^ (b >> 4)]);
	return (WORD)((c << 4) ^ SD_CRC16_NIB[(c >> 12) ^ (b & 15)]);
}
static WORD sd_crc16(const BYTE *p, UINT n)
{
	WORD c = 0;
	while (n--) c = sd_crc16_up(c, *p++);
	return c;
}

/* CRC7 (x^7 + x^3 + 1) of n bytes, as the byte after them: CRC << 1 | the stop bit (a command's sixth byte; the CSD's
 * and the CID's last) */
static BYTE sd_crc7n(const BYTE *p, UINT k)
{
	BYTE c = 0, b, i;
	UINT n;
	for (n = 0; n < k; n++)
		for (b = p[n], i = 0; i < 8; i++, b <<= 1) {
			c <<= 1;
			if ((b ^ c) & 0x80) c ^= 0x09;
		}
	return (BYTE)(c << 1 | 1);
}
static BYTE sd_crc7_end(const BYTE *p) { return sd_crc7n(p, 5); }

static WORD sd_rx_crc(BYTE *p, UINT n, WORD c)	/* n bytes in (to p, or skipped: p NULL), the CRC16 updated */
{
	BYTE b;
	while (n--) {
		b = SD_RX();
		if (p) *p++ = b;
		c = sd_crc16_up(c, b);
	}
	return c;
}

static void skip_mmc(UINT n)		/* n bytes of 0xFF clocked out */
{
	do SD_TX(0xFF); while (--n);
}

static void release_spi(void)		/* Deselect the card and release SPI bus */
{
	SD_DESEL();
	(void)SD_RX();
}

/* wait for a byte other than 0xFF, up to ms milliseconds; returns it (0xFF on timeout) */
static BYTE wait_token(UINT ms)
{
	DWORD t0 = SD_NOW();
	BYTE d;
	do {
		d = SD_RX();
		if (d != 0xFF) break;
	} while (sd_ms_since(t0) < ms);
	return d;
}

/* Send a command packet to MMC */
static BYTE send_cmd(BYTE cmd, DWORD arg)
{
	BYTE n, res;

	if (cmd & 0x80) {	/* ACMD<n> is the command sequense of CMD55-CMD<n> */
		cmd &= 0x7F;
		res = send_cmd(CMD55, 0);
		if (res > 1) return res;
	}

	/* Select the card (CMD12 goes into the running multi-block read: the card stays selected) */
	if (cmd != CMD12) {
		SD_DESEL(); (void)SD_RX();
		SD_SEL(); (void)SD_RX();
	}

	/* Send a command packet, with its real CRC7 (the card checks it once CMD59 turned the CRCs on) */
	{
		BYTE pk[5] = {cmd, (BYTE)(arg >> 24), (BYTE)(arg >> 16), (BYTE)(arg >> 8), (BYTE)arg};
		for (n = 0; n < 5; n++) SD_TX(pk[n]);
		SD_TX(sd_crc7_end(pk));
	}

	/* Receive a command response */
	if (cmd == CMD12) (void)SD_RX();	/* the stuff byte */
	n = 10;						/* Wait for a valid response in timeout of 10 attempts */
	do {
		res = SD_RX();
	} while ((res & 0x80) && --n);

	return res;			/* Return with the response value */
}

/* the capacity from the CSD register (v1: C_SIZE, C_SIZE_MULT, READ_BL_LEN; v2: C_SIZE x 1024); 0 when unread */
static DWORD sd_read_capacity(void)
{
	BYTE csd[16], n;
	DWORD cs = 0;

	if (send_cmd(CMD9, 0) == 0 && wait_token(100) == 0xFE) {
		for (n = 0; n < 16; n++) csd[n] = SD_RX();
		skip_mmc(2);				/* CRC */
		if ((csd[0] >> 6) == 1) {	/* CSD v2 (SDHC / SDXC) */
			cs = ((DWORD)(csd[7] & 0x3F) << 16 | (DWORD)csd[8] << 8 | csd[9]) + 1;
			cs <<= 10;
		} else {					/* CSD v1 (SDSC, MMC) */
			n = (BYTE)((csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2);
			cs = ((DWORD)(csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1) << (n - 9);
		}
	}
	release_spi();
	return cs;
}

/* Initialize Disk Drive (the caller has the bus at the start-up clock, at most 400 kHz) */
static DSTATUS sd_card_init(void)
{
	BYTE n, cmd, ty, buf[4];
	DWORD t0;

	SD_DESEL();
	skip_mmc(10);			/* Dummy clocks */

	ty = 0;
	if (send_cmd(CMD0, 0) == 1) {			/* Enter Idle state */
		if (send_cmd(CMD8, 0x1AA) == 1) {	/* SDv2 */
			for (n = 0; n < 4; n++) buf[n] = SD_RX();	/* Get trailing return value of R7 resp */
			if (buf[2] == 0x01 && buf[3] == 0xAA) {			/* The card can work at vdd range of 2.7-3.6V */
				t0 = SD_NOW();
				while (send_cmd(ACMD41, 1UL << 30) != 0 && sd_ms_since(t0) < 1000)
					;					/* Wait for leaving idle state (ACMD41 with HCS bit), 1 s */
				if (sd_ms_since(t0) < 1000 && send_cmd(CMD58, 0) == 0) {	/* Check CCS bit in the OCR */
					for (n = 0; n < 4; n++) buf[n] = SD_RX();
					ty = (buf[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;	/* SDv2 (HC or SC) */
				}
			}
		} else {							/* SDv1 or MMCv3 */
			if (send_cmd(ACMD41, 0) <= 1) 	{
				ty = CT_SD1; cmd = ACMD41;	/* SDv1 */
			} else {
				ty = CT_MMC; cmd = CMD1;	/* MMCv3 */
			}
			t0 = SD_NOW();
			while (send_cmd(cmd, 0) != 0 && sd_ms_since(t0) < 1000)
				;						/* Wait for leaving idle state */
			if (sd_ms_since(t0) >= 1000 || send_cmd(CMD16, 512) != 0)	/* Set R/W block length to 512 */
				ty = 0;
		}
	}
	sd_crc_on = ty && send_cmd(CMD59, 1) == 0;	/* the CRCs checked from here on (refused: the card works without) */
	CardType = ty;
	release_spi();
	if (ty) SD_FAST();
	sd_sectors = ty ? sd_read_capacity() : 0;

	return ty ? 0 : STA_NOINIT;
}

/* Read partial sector */
static DRESULT sd_readp1(
	BYTE *buff,		/* Pointer to the read buffer (NULL: the bytes are skipped) */
	DWORD sector,	/* Sector number (LBA) */
	UINT offset,	/* Byte offset to read from (0..511) */
	UINT count		/* Number of bytes to read (ofs + cnt mus be <= 512) */
)
{
	DRESULT res;
	UINT bc;

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* Convert to byte address if needed */

	res = RES_ERROR;
	if (send_cmd(CMD17, sector) == 0) {		/* READ_SINGLE_BLOCK */
		if (wait_token(100) == 0xFE) {		/* A data packet arrived (within 100 ms) */
			WORD c = 0, got;
			bc = 512 - offset - count;

			/* the leading bytes (their CRC counted) */
			c = sd_rx_crc(0, offset, c);

			/* Receive a part of the sector */
			if (buff && count == 512 && SD_DMA_OK() && !((UINT)(uintptr_t)buff & 3)) {
				SD_RXDMA(buff, 512);		/* a whole sector into an aligned buffer: by DMA (offset is 0) */
				if (!SD_DMA_END()) { release_spi(); return RES_ERROR; }
				c = sd_crc16(buff, 512);
			} else if (buff && !SD_RD_CRC) {
				SD_RXBLK(buff, count);
			} else {
				c = sd_rx_crc(buff, count, c);
			}

			/* the trailing bytes, then the block's CRC16, checked */
			c = sd_rx_crc(0, bc, c);
			got = (WORD)(SD_RX() << 8);
			got |= SD_RX();
			sd_crc_bad = SD_RD_CRC && got != c;
			res = sd_crc_bad ? RES_ERROR : RES_OK;
		}
	}

	release_spi();

	return res;
}

/* a block's CRC16 failed (fails: this block's failures so far): 1 = read it again. Above the floor the clock drops one
 * step at once (counted: sd_demotions; this session and every later one) and the block is read again there, so nothing
 * is lost; at the floor it is read once more as before, a second failure an error */
static BYTE sd_crc_retry(BYTE *fails)
{
	sd_crc_fails++;
	if (sd_step < SD_FLOOR) {
		sd_step++;
		sd_demotions++;
		SD_SET_STEP(sd_step);
		*fails = 0;
		return 1;
	}
	return ++*fails < 2;
}

/* Read partial sector (a CRC failure read again: sd_crc_retry) */
static DRESULT sd_readp(BYTE *buff, DWORD sector, UINT offset, UINT count)
{
	DRESULT r;
	BYTE fails = 0;
	sd_crc_bad = 0;
	while ((r = sd_readp1(buff, sector, offset, count)) != RES_OK && sd_crc_bad && sd_crc_retry(&fails))
		sd_crc_bad = 0;
	return r;
}

/* ---- whole blocks (Optimist) ---------------------------------------------------------------------------------------
 * A polled reader, main loop only, inside one bus session (sdcard.c sd_begin / sd_end; the screen waits meanwhile):
 *   sd_rd_open(sector, n)   CMD18 for n > 1 blocks (CMD17 for one): RES_OK, or RES_ERROR (the card refused)
 *   sd_rd_next(buf)         the next block into buf (512 B, 4-byte aligned for the DMA), then poll:
 *   sd_rd_poll()            SDR_BUSY (the data token not seen yet, or the DMA running: do other work and poll again),
 *                           SDR_READY (buf holds the block; sd_rd.crc_ok says whether its CRC16 matched, when
 *                           SD_RD_CRC is on), SDR_ERR (no data token in 100 ms, or the DMA never ended)
 *   sd_rd_close()           CMD12 after a multi-block read (the card stops at once; the block it had begun is lost),
 *                           the busy end waited for, the card released; RES_OK or RES_ERROR
 * Each poll in the token wait reads at most SD_RD_TOKEN_TRIES bytes, so a poll is short (a few us). With DMA the CPU
 * spends ~ the token's bytes, the DMA's start and the 2 CRC bytes on a block; without it, 512 byte transfers.
 * disk_readblocks(buf, sector, n): the same, blocking (n blocks to buf, n x 512 B). disk_writeblock(buf, sector):
 * one block written (CMD24), the data by DMA where the bus has it, the card's busy end waited for (500 ms). */
enum { SDR_IDLE = 0, SDR_TOKEN, SDR_DMA, SDR_READY, SDR_ERR };
#define SDR_BUSY SDR_TOKEN
#ifndef SD_RD_TOKEN_TRIES
#define SD_RD_TOKEN_TRIES 8u
#endif
static struct {
	BYTE *buf;
	DWORD t0;							/* the token wait's start */
	WORD crc;							/* the CRC16 the card sent with the last block */
	BYTE state, multi, crc_ok;
	BYTE dma;							/* the block in flight comes by DMA */
} sd_rd;

static DRESULT sd_rd_open(DWORD sector, UINT n)
{
	if (!(CardType & CT_BLOCK)) sector *= 512;
	sd_rd.multi = n > 1;
	sd_rd.state = SDR_IDLE;
	if (send_cmd(n > 1 ? CMD18 : CMD17, sector) != 0) {
		release_spi();
		sd_rd.multi = 0;
		return RES_ERROR;
	}
	return RES_OK;
}

static void sd_rd_next(BYTE *buf)
{
	sd_rd.buf = buf;
	sd_rd.state = SDR_TOKEN;
	sd_rd.t0 = SD_NOW();
}

static void sd_rd_crc(void)
{
	sd_rd.crc = (WORD)(SD_RX() << 8);
	sd_rd.crc |= SD_RX();
	sd_rd.crc_ok = !SD_RD_CRC || sd_crc16(sd_rd.buf, 512) == sd_rd.crc;
	sd_rd.state = SDR_READY;
}

static BYTE sd_rd_poll(void)
{
	UINT k;
	BYTE d;
	if (sd_rd.state == SDR_TOKEN) {
		for (k = 0; k < SD_RD_TOKEN_TRIES; k++) {
			d = SD_RX();
			if (d == 0xFE) {
				sd_rd.dma = SD_DMA_OK() && !((UINT)(uintptr_t)sd_rd.buf & 3);	/* (FatFs reads straight into a
										 * caller's buffer: an unaligned one by bytes) */
				if (sd_rd.dma) {
					SD_RXDMA(sd_rd.buf, 512);
					sd_rd.state = SDR_DMA;
					return SDR_BUSY;
				}
				SD_RXBLK(sd_rd.buf, 512);
				sd_rd_crc();
				return SDR_READY;
			}
			if (d != 0xFF) { sd_rd.state = SDR_ERR; return SDR_ERR; }	/* an error token */
		}
		if (sd_ms_since(sd_rd.t0) >= sd_wait_ms) sd_rd.state = SDR_ERR;
		return sd_rd.state;
	}
	if (sd_rd.state == SDR_DMA) {
		if (!SD_DMA_DONE()) return SDR_BUSY;
		if (!SD_DMA_END()) { sd_rd.state = SDR_ERR; return SDR_ERR; }
		sd_rd_crc();
	}
	return sd_rd.state;
}

static DRESULT sd_rd_close(void)
{
	DRESULT r = sd_rd.state == SDR_ERR ? RES_ERROR : RES_OK;
	DWORD t0;
	if (sd_rd.state == SDR_DMA && !SD_DMA_END()) r = RES_ERROR;	/* (a DMA still running: let it end first) */
	if (sd_rd.multi) {
		if (send_cmd(CMD12, 0) & 0x80) r = RES_ERROR;			/* (R1's busy bit clear: answered) */
		t0 = SD_NOW();
		while (SD_RX() != 0xFF)									/* R1b: busy until the card lets DO go high */
			if (sd_ms_since(t0) >= 100) { r = RES_ERROR; break; }
	}
	sd_rd.state = SDR_IDLE;
	sd_rd.multi = 0;
	release_spi();
	return r;
}

static DRESULT disk_readblocks(BYTE *buf, DWORD sector, UINT n)
{
	BYTE st = SDR_ERR, fails = 0;
	UINT done = 0;
	if (!n) return RES_ERROR;
	for (;;) {									/* a run from the first block not read yet */
		if (sd_rd_open(sector + done, n - done) != RES_OK) return RES_ERROR;
		for (; done < n; done++) {
			sd_rd_next(buf + done * 512);
			while ((st = sd_rd_poll()) == SDR_BUSY)
				;
			if (st != SDR_READY || !sd_rd.crc_ok) { sd_rd.state = SDR_ERR; break; }
			fails = 0;
		}
		if (done == n) return sd_rd_close();
		(void)sd_rd_close();
		if (st != SDR_READY || !sd_crc_retry(&fails)) return RES_ERROR;	/* (a timeout, an error token: not retried) */
	}
}

/* ---- the clock detection (the user, 2026-11-04: "this is sandisk ultra, it should go higher, we should have a speed
 * detection feature"; docs/SD-CARD-DESIGN.md "The clock"). The start-up (CMD0, CMD8, ACMD41, CMD58) is always at
 * 400 kHz and the first reads at the floor; only data moves faster. In a session at the floor after the mount:
 *   1. a reference pass at the floor (sd_clk_pass: the CSD and the CID, each block's CRC16 and the register's CRC7,
 *      SD_CLK_SINGLES blocks by CMD17 spread over the card, n blocks from 0 by one CMD18, every block's CRC16): its
 *      fingerprint (a CRC16 over every byte the registers gave and every block's CRC16). The floor failing: the floor.
 *   2. CMD6 asks an SDv2 card for high speed (the HS steps, 30 MHz, only when it took it). SDv1, MMC: the floor.
 *   3. from the fastest step down: two passes at the step, each with no CRC failure, no timeout and the reference's
 *      fingerprint (a line stuck low reads zeros with a right CRC16: the fingerprint catches it). A step that fails:
 *      the clock back to the floor (a CMD18 stopped there), the card checked by a pass at the floor, started again
 *      (400 kHz, sd_card_init, CMD6 again) when that fails too; the walk goes on one step slower.
 *   4. the first step that passes is the fastest; the one below it must pass too (the margin) and is the clock kept
 *      (the floor when that is the floor). It failing: the walk goes on from the step after it.
 * sd_clk_res[k] says what each step did (SDK_*: the console's sd diag), sd_clk_top the fastest that passed, sd_clk_ms
 * the probe's time. force (the console's sd clock): 0 the walk, else the step force - 1, set as it is.
 * The time (the watchdog: 8 s, fed once a main loop pass): every wait here is SD_CLK_WAIT_MS, so a pass takes at most
 * 26 x 20 ms + 2 busy ends ~ 0.56 s however slowly the card answers; no step starts after SD_CLK_BUDGET_MS; a step that
 * times out or is refused (SDK_ERR) ends the walk at the floor; the watchdog fed at every pass and before a start-up
 * again: ~5 s at the very worst, ~1.7 s between feeds (device/sd.c in fm1-base has the same walk and the sum). */
#define SD_CLK_WAIT_MS 20u
#define SD_CLK_BUDGET_MS 1000u
enum { SDK_UNTRIED = 0, SDK_PASS, SDK_CRC, SDK_DIFF, SDK_ERR, SDK_NOHS };
#ifndef SD_CLK_SINGLES
#define SD_CLK_SINGLES 8u							/* CMD17 blocks a pass reads, spread over the card */
#endif
static BYTE sd_clk_res[SD_NSTEP];
static BYTE sd_clk_top = SD_FLOOR;
static DWORD sd_clk_ms;

static BYTE sd_cmd6(DWORD arg, BYTE *st)		/* CMD6's switch status: 1 = its 64 bytes in, their CRC16 right */
{
	BYTE ok = 0;
	WORD c, got;
	if (send_cmd(CMD6, arg) == 0 && wait_token(sd_wait_ms) == 0xFE) {
		c = sd_rx_crc(st, 64, 0);
		got = (WORD)(SD_RX() << 8);
		got |= SD_RX();
		ok = got == c;
	}
	release_spi();
	return ok;
}

/* 1 = the card is at high speed now. Asked first (mode 0, group 1 = 1): switched (mode 1) only when that status says group
 * 1 supports function 1 (bit 401: byte 13 bit 1) and would switch to it (bits 379:376 = 1: byte 16); then the switch's own
 * status says it did. Every status's CRC16 checked */
static BYTE sd_switch_hs(void)
{
	BYTE st[64];
	if (!(CardType & CT_SD2)) return 0;
	return sd_cmd6(0x00FFFFF1UL, st) && (st[13] & 2) && (st[16] & 15) == 1 && sd_cmd6(0x80FFFFF1UL, st) &&
	       (st[16] & 15) == 1;
}

static WORD sd_sum_bytes(WORD s, const BYTE *p, UINT n)
{
	while (n--) s = sd_crc16_up(s, *p++);
	return s;
}

/* the CSD (CMD9) or the CID (CMD10): 16 bytes in a data block, its CRC16 checked (SDK_CRC), no token (SDK_ERR); the
 * register's own CRC7 (its last byte) goes into the fingerprint as a flag beside the bytes */
static BYTE sd_reg_read(BYTE cmd, BYTE *r, WORD *sum)
{
	BYTE res = SDK_ERR;
	WORD c, got;
	if (send_cmd(cmd, 0) == 0 && wait_token(sd_wait_ms) == 0xFE) {
		c = sd_rx_crc(r, 16, 0);
		got = (WORD)(SD_RX() << 8);
		got |= SD_RX();
		res = got == c ? SDK_PASS : SDK_CRC;
		*sum = sd_crc16_up(sd_sum_bytes(*sum, r, 16), (BYTE)(sd_crc7n(r, 15) == r[15]));
	}
	release_spi();
	return res;
}

/* n blocks from sector s by one CMD18 (CMD17 for one) into buf, each CRC16 checked, the CRCs into *sum: SDK_PASS,
 * SDK_CRC, SDK_ERR (refused, a timeout, an error token). A failure: the floor's clock before the CMD12 */
static BYTE sd_rd_crcs(BYTE *buf, DWORD s, UINT n, WORD *sum)
{
	UINT i;
	BYTE st, res = SDK_PASS;
	if (sd_rd_open(s, n) != RES_OK) return SDK_ERR;
	for (i = 0; i < n; i++) {
		sd_rd_next(buf);
		while ((st = sd_rd_poll()) == SDR_BUSY)
			;
		if (st != SDR_READY) { sd_rd.state = SDR_ERR; res = SDK_ERR; break; }
		if (!sd_rd.crc_ok) { res = SDK_CRC; break; }
		*sum = sd_crc16_up(sd_crc16_up(*sum, (BYTE)(sd_rd.crc >> 8)), (BYTE)sd_rd.crc);
	}
	if (res != SDK_PASS) SD_SET_STEP(SD_FLOOR);
	if (sd_rd_close() != RES_OK && res == SDK_PASS) res = SDK_ERR;
	return res;
}

/* one pass at the clock set (step 1's list); *sum its fingerprint; the first failure's SDK_* */
static BYTE sd_clk_pass(BYTE *buf, UINT n, WORD *sum)
{
	BYTE reg[16], r;
	UINT i;
	DWORD span = sd_sectors / SD_CLK_SINGLES;
	SD_WDT();									/* (a pass: 0.56 s at most) */
	*sum = 0;
	if ((r = sd_reg_read(CMD9, reg, sum)) != SDK_PASS) return r;
	if ((r = sd_reg_read(CMD10, reg, sum)) != SDK_PASS) return r;
	for (i = 0; i < SD_CLK_SINGLES; i++)
		if ((r = sd_rd_crcs(buf, (span * i + i * 7u) % sd_sectors, 1, sum)) != SDK_PASS) return r;
	if (n && (r = sd_rd_crcs(buf, 0, n, sum)) != SDK_PASS) return r;
	return SDK_PASS;
}

/* step k tried: two passes, each the reference's fingerprint; the clock back at the floor after */
static BYTE sd_clk_try(BYTE *buf, UINT n, BYTE k, WORD ref)
{
	WORD s;
	BYTE r, i;
	SD_SET_STEP(k);
	for (r = SDK_PASS, i = 0; i < 2 && r == SDK_PASS; i++)
		if ((r = sd_clk_pass(buf, n, &s)) == SDK_PASS && s != ref) r = SDK_DIFF;
	SD_SET_STEP(SD_FLOOR);
	return r;
}

/* after a failed step, at the floor: any CMD18 left running stopped, the card checked by a pass; failing, started
 * again (400 kHz, then the floor) and checked once more. 0: the card no longer reads right at the floor (the probe ends) */
static BYTE sd_clk_recover(BYTE *buf, UINT n, WORD ref, BYTE *hs)
{
	WORD s;
	DWORD t0;
	SD_DESEL(); (void)SD_RX();
	SD_SEL(); (void)SD_RX();
	(void)send_cmd(CMD12, 0);
	t0 = SD_NOW();
	while (SD_RX() != 0xFF && sd_ms_since(t0) < sd_wait_ms)
		;
	release_spi();
	if (sd_clk_pass(buf, n, &s) == SDK_PASS && s == ref) return 1;
	SD_WDT();									/* (the start-up again: ACMD41's 1 s at most) */
	SD_SLOW();
	if (sd_card_init() != 0) return 0;			/* (the floor's clock after it) */
	*hs = sd_switch_hs();
	return sd_clk_pass(buf, n, &s) == SDK_PASS && s == ref;
}

static UINT sd_clock_probe(BYTE *buf, UINT n, BYTE force)
{
	WORD ref;
	BYTE k, hs, r;
	UINT bad = 0;
	DWORD t0 = SD_NOW();
	for (k = 0; k < SD_NSTEP; k++) sd_clk_res[k] = SDK_UNTRIED;
	sd_step = SD_FLOOR;
	sd_clk_top = SD_FLOOR;
	SD_SET_STEP(SD_FLOOR);
	if (force) {								/* (the console's choice, unchecked) */
		sd_step = (BYTE)(force - 1u < SD_NSTEP ? force - 1u : SD_FLOOR);
		if (sd_step < SD_NSTEP_HS) (void)sd_switch_hs();
		SD_SET_STEP(sd_step);
		return 0;
	}
	if (!(CardType & CT_SD2) || !sd_sectors || SD_CLOCK_FIXED()) return 0;	/* (SDv1, MMC, bit-bang: the floor) */
	sd_wait_ms = SD_CLK_WAIT_MS;
	r = sd_clk_pass(buf, n, &ref);
	sd_clk_res[SD_FLOOR] = r;
	if (r != SDK_PASS) { sd_wait_ms = 100; sd_clk_ms = sd_ms_since(t0); return 0; }
	hs = sd_switch_hs();
	for (k = 0; k < SD_FLOOR && sd_ms_since(t0) < SD_CLK_BUDGET_MS; k++) {
		if (k < SD_NSTEP_HS && !hs) { sd_clk_res[k] = SDK_NOHS; continue; }
		r = sd_clk_res[k] = sd_clk_try(buf, n, k, ref);
		if (r == SDK_PASS) {					/* the fastest: the margin step below it */
			sd_clk_top = k;
			if (k + 1u == SD_FLOOR) break;
			r = sd_clk_res[k + 1u] = sd_clk_try(buf, n, (BYTE)(k + 1u), ref);
			if (r == SDK_PASS) { sd_step = (BYTE)(k + 1u); break; }
			sd_clk_top = SD_FLOOR;
			k++;								/* (that step failed: the walk on after it) */
		}
		bad++;
		if (!sd_clk_recover(buf, n, ref, &hs) || r == SDK_ERR) break;	/* (a timeout or a refusal: the floor) */
	}
	sd_wait_ms = 100;
	SD_SET_STEP(sd_step);
	sd_clk_ms = sd_ms_since(t0);
	return bad;
}

static DRESULT disk_writeblock(const BYTE *buf, DWORD sector)
{
	DRESULT r = RES_ERROR;
	DWORD t0;
	WORD c = sd_crc16(buf, 512);
	UINT i;
	if (!(CardType & CT_BLOCK)) sector *= 512;
	if (send_cmd(CMD24, sector) == 0) {
		SD_TX(0xFF); SD_TX(0xFE);						/* a byte gap, the data token */
		if (SD_DMA_OK() && !((UINT)(uintptr_t)buf & 3)) {
			SD_TXDMA(buf, 512);
			if (!SD_DMA_END()) { release_spi(); return RES_ERROR; }
		} else {
			for (i = 0; i < 512; i++) SD_TX(buf[i]);
		}
		SD_TX((BYTE)(c >> 8)); SD_TX((BYTE)c);			/* (checked by the card: CMD59) */
		if ((SD_RX() & 0x1F) == 0x05) {					/* data accepted; busy until the card has written it */
			t0 = SD_NOW();
			while (SD_RX() != 0xFF)
				if (sd_ms_since(t0) >= 500) break;
			if (sd_ms_since(t0) < 500) r = RES_OK;
		}
	}
	release_spi();
	return r;
}

/* ---- a polled multi-block writer (Optimist: the recorder, storage/sd/sd_rec.c; docs/SD-CARD-DESIGN.md 7) ----------
 * CMD25 from a sector, the blocks one at a time; each call selects the card and lets it go before it returns, so the
 * screen keeps the bus between blocks and while the card programs (SPI mode: deselecting during a multi-block write's
 * busy, or between its blocks, leaves the write open; the spec's, not yet seen on an FM-1). Never a wait on busy:
 *   sd_wr_open(sector)   CMD25 at sector: RES_OK, RES_ERROR (refused)
 *   sd_wr_poll()         SDW_READY (the next block may go), SDW_BUSY (the card programs), SDW_IDLE (stopped: the
 *                        write is over), SDW_ERR (busy past 500 ms, a block refused)
 *   sd_wr_block(buf)     on SDW_READY: the token 0xFC, the block (by DMA where the bus has it), its CRC16 (checked by
 *                        the card: CMD59); accepted -> busy; refused (a CRC, a write error) -> SDW_ERR
 *   sd_wr_stop()         on SDW_READY: the stop token 0xFD, then busy until SDW_IDLE
 *   sd_wr_abort()        after SDW_ERR: the stop sent once the card is ready (waits up to 500 ms: an error's path) */
enum { SDW_IDLE = 0, SDW_READY, SDW_BUSY, SDW_STOP, SDW_ERR };
static struct {
	DWORD t0;							/* the busy's start */
	BYTE state;							/* SDW_IDLE, SDW_BUSY (a block programs), SDW_STOP (the stop programs), SDW_ERR */
	DWORD blocks;						/* sent since the open */
} sd_wr;

static DRESULT sd_wr_open(DWORD sector)
{
	if (!(CardType & CT_BLOCK)) sector *= 512;
	if (send_cmd(CMD25, sector) != 0) {
		release_spi();
		sd_wr.state = SDW_ERR;
		return RES_ERROR;
	}
	release_spi();
	sd_wr.state = SDW_READY;
	sd_wr.blocks = 0;
	return RES_OK;
}

static BYTE sd_wr_poll(void)
{
	BYTE d;
	if (sd_wr.state == SDW_IDLE || sd_wr.state == SDW_ERR || sd_wr.state == SDW_READY) return sd_wr.state;
	SD_SEL();
	d = SD_RX();						/* busy: DO held low */
	release_spi();
	if (d == 0xFF) return sd_wr.state = sd_wr.state == SDW_STOP ? SDW_IDLE : SDW_READY;
	if (sd_ms_since(sd_wr.t0) >= 500) sd_wr.state = SDW_ERR;
	return sd_wr.state == SDW_ERR ? SDW_ERR : SDW_BUSY;
}

static BYTE sd_wr_block(const BYTE *buf)
{
	WORD c;
	UINT i;
	BYTE r;
	if (sd_wr.state != SDW_READY) return sd_wr.state == SDW_ERR ? SDW_ERR : SDW_BUSY;
	c = sd_crc16(buf, 512);
	SD_SEL();
	(void)SD_RX();						/* a byte gap */
	SD_TX(0xFC);
	if (SD_DMA_OK() && !((UINT)(uintptr_t)buf & 3)) {
		SD_TXDMA(buf, 512);
		if (!SD_DMA_END()) { release_spi(); return sd_wr.state = SDW_ERR; }
	} else {
		for (i = 0; i < 512; i++) SD_TX(buf[i]);
	}
	SD_TX((BYTE)(c >> 8)); SD_TX((BYTE)c);
	r = SD_RX() & 0x1F;					/* the data response: 0x05 accepted, 0x0B CRC, 0x0D write error */
	release_spi();
	if (r != 0x05) return sd_wr.state = SDW_ERR;
	sd_wr.blocks++;
	sd_wr.t0 = SD_NOW();
	return sd_wr.state = SDW_BUSY;
}

static BYTE sd_wr_stop(void)
{
	if (sd_wr.state != SDW_READY) return sd_wr.state == SDW_ERR ? SDW_ERR : SDW_BUSY;
	SD_SEL();
	(void)SD_RX();
	SD_TX(0xFD);
	(void)SD_RX();						/* (a byte, then the busy) */
	release_spi();
	sd_wr.t0 = SD_NOW();
	return sd_wr.state = SDW_STOP;
}

static void sd_wr_abort(void)
{
	DWORD t0 = SD_NOW();
	SD_SEL();
	while (SD_RX() != 0xFF && sd_ms_since(t0) < 500)
		;
	SD_TX(0xFD);
	(void)SD_RX();
	while (SD_RX() != 0xFF && sd_ms_since(t0) < 1000)
		;
	release_spi();
	sd_wr.state = SDW_IDLE;
}

#if !FELUCCA_SD_FATFS
/* Petit FatFs' entry points (diskio.h): the start-up, a partial sector read, the partial sector write below */
DSTATUS disk_initialize(void) { return sd_card_init(); }
DRESULT disk_readp(BYTE *buff, DWORD sector, UINT offset, UINT count) { return sd_readp(buff, sector, offset, count); }

/* Write partial sector */
DRESULT disk_writep(
	const BYTE *buff,	/* Pointer to the bytes to be written (NULL:Initiate/Finalize sector write) */
	DWORD sc			/* Number of bytes to send, Sector number (LBA) or zero */
)
{
	DRESULT res;
	UINT bc;
	DWORD t0;
	static UINT wc;
	static WORD wcrc;		/* the CRC16 of the bytes sent so far */

	res = RES_ERROR;

	if (buff) {		/* Send data bytes */
		bc = (UINT)sc;
		while (bc && wc) {		/* Send data bytes to the card */
			wcrc = sd_crc16_up(wcrc, *buff);
			SD_TX(*buff++);
			wc--; bc--;
		}
		res = RES_OK;
	} else {
		if (sc) {	/* Initiate sector write transaction */
			if (!(CardType & CT_BLOCK)) sc *= 512;	/* Convert to byte address if needed */
			if (send_cmd(CMD24, sc) == 0) {			/* WRITE_SINGLE_BLOCK */
				SD_TX(0xFF); SD_TX(0xFE);		/* Data block header */
				wc = 512;							/* Set byte counter */
				wcrc = 0;
				res = RES_OK;
			}
		} else {	/* Finalize sector write transaction */
			bc = wc;
			while (bc--) { SD_TX(0); wcrc = sd_crc16_up(wcrc, 0); }	/* the left bytes zeros, then the CRC16 */
			SD_TX((BYTE)(wcrc >> 8)); SD_TX((BYTE)wcrc);
			if ((SD_RX() & 0x1F) == 0x05) {	/* Receive data resp and wait for end of write process, 500 ms */
				t0 = SD_NOW();
				while (SD_RX() != 0xFF)
					if (sd_ms_since(t0) >= 500) break;
				if (sd_ms_since(t0) < 500) res = RES_OK;
			}
			release_spi();
		}
	}

	return res;
}
#endif
