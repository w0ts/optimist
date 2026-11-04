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
#define SD_FAST() fm1_sd_baud(FM1_SD_BAUD_FAST)   /* the card is up: the full clock */
#define SD_SET_OVER(on) fm1_sd_baud((on) ? FM1_SD_BAUD_OVER : FM1_SD_BAUD_FAST)   /* the faster clock (30 MHz) or 20 */
#define SD_NOW() fm1_ticks()
#define SD_TICKS_MS (FM1_TICKS_PER_US * 1000u)
#define SD_RXBLK(p, n) fm1_sd_rx_block(p, n)
#define SD_DMA_OK() (fm1_sd_dma && fm1_sd_mode != FM1_SD_BITBANG)
#define SD_RXDMA(p, n) fm1_sd_rx_dma(p, n)
#define SD_TXDMA(p, n) fm1_sd_tx_dma(p, n)
#define SD_DMA_DONE() fm1_sd_rx_dma_done()
#define SD_DMA_END() fm1_sd_rx_dma_end()
#endif

static DWORD sd_ms_since(DWORD t0) { return (DWORD)(SD_NOW() - t0) / SD_TICKS_MS; }   /* (wraps cleanly) */

/* Definitions for MMC/SDC command */
#define CMD0	(0x40+0)	/* GO_IDLE_STATE */
#define CMD1	(0x40+1)	/* SEND_OP_COND (MMC) */
#define	ACMD41	(0xC0+41)	/* SEND_OP_COND (SDC) */
#define CMD6	(0x40+6)	/* SWITCH_FUNC */
#define CMD8	(0x40+8)	/* SEND_IF_COND */
#define CMD9	(0x40+9)	/* SEND_CSD */
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
static BYTE sd_crc_on;			/* the card checks the CRCs (CMD59 accepted) */
static BYTE sd_over;			/* the clock the card proved: 1 the faster one (SD_SET_OVER, 30 MHz), 0 the floor (20) */
static DWORD sd_crc_fails, sd_demotions;	/* blocks whose CRC16 failed; drops from the faster clock to the floor */
static BYTE sd_crc_bad;			/* the last sd_readp failed on the block's CRC16 (not a timeout) */

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

/* CRC7 (x^7 + x^3 + 1) of a command's first five bytes, as the packet's last byte: CRC << 1 | the stop bit */
static BYTE sd_crc7_end(const BYTE *p)
{
	BYTE c = 0, b, i, n;
	for (n = 0; n < 5; n++)
		for (b = p[n], i = 0; i < 8; i++, b <<= 1) {
			c <<= 1;
			if ((b ^ c) & 0x80) c ^= 0x09;
		}
	return (BYTE)(c << 1 | 1);
}

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

/* a block's CRC16 failed (fails: this block's failures so far): 1 = read it again. A second failure at the faster
 * clock drops the session to the floor (20 MHz) and the read goes on there; at the floor a second failure is an error */
static BYTE sd_crc_retry(BYTE *fails)
{
	sd_crc_fails++;
	if (++*fails < 2) return 1;
	if (!sd_over) return 0;
	sd_over = 0;
	sd_demotions++;
	SD_SET_OVER(0);
	*fails = 0;
	return 1;
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
		if (sd_ms_since(sd_rd.t0) >= 100) sd_rd.state = SDR_ERR;
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

/* the clock the card proves (the user, 2026-10-25): the floor (20 MHz) always works by the spec; the faster clock (30)
 * only when the card shows it on this board. In a session at the floor after the mount: CMD6 asks an SDv2 card for high
 * speed (some refuse clocks over 25 MHz without it; SDv1 and MMC answer illegal command and stay at the floor), then
 * n blocks from sector 0 by one CMD18 at the floor and again at the faster clock into buf (512 B, aligned), each CRC16
 * checked, their CRCs summed: the faster clock kept only when every block read right and the sums agree. force: 20 or
 * 30 (the console's sd clock), 0 auto. sd_over set, the clock left set; returns the blocks that failed at 30 (n: not
 * tried, or the card refused the switch) */
static BYTE sd_switch_hs(void)
{
	BYTE st[64], ok = 0;
	WORD c, got;
	if (!(CardType & CT_SD2)) return 0;
	if (send_cmd(CMD6, 0x80FFFFF1UL) == 0 && wait_token(100) == 0xFE) {	/* mode 1 (switch), group 1 = 1 (high speed) */
		c = sd_rx_crc(st, 64, 0);
		got = (WORD)(SD_RX() << 8);
		got |= SD_RX();
		ok = got == c && (st[16] & 15) == 1;	/* bits 379:376, group 1's function now: 1 = high speed */
	}
	release_spi();
	return ok;
}

static UINT sd_rd_sum(BYTE *buf, UINT n, WORD *sum)	/* n blocks from 0 by one CMD18 into buf: the blocks that failed */
{
	UINT i, bad = 0;
	BYTE st;
	*sum = 0;
	if (sd_rd_open(0, n) != RES_OK) return n;
	for (i = 0; i < n; i++) {
		sd_rd_next(buf);
		while ((st = sd_rd_poll()) == SDR_BUSY)
			;
		if (st != SDR_READY) { sd_rd.state = SDR_ERR; bad += n - i; break; }
		bad += !sd_rd.crc_ok;
		*sum = sd_crc16_up(sd_crc16_up(*sum, (BYTE)(sd_rd.crc >> 8)), (BYTE)sd_rd.crc);
	}
	if (sd_rd_close() != RES_OK && !bad) bad = 1;
	return bad;
}

static UINT sd_clock_probe(BYTE *buf, UINT n, BYTE force)
{
	WORD s20, s30;
	UINT bad = n, bad20;
	sd_over = 0;
	SD_SET_OVER(0);
	if (force == 20 || !n || (!(CardType & CT_SD2) && force != 30)) return n;	/* (SDv1, MMC: the floor) */
	(void)sd_switch_hs();
	if (force == 30) {
		sd_over = 1;
	} else {
		bad20 = sd_rd_sum(buf, n, &s20);
		SD_SET_OVER(1);
		bad = sd_rd_sum(buf, n, &s30);
		sd_over = !bad20 && !bad && s20 == s30;
		if (!bad && s20 != s30) bad = 1;
	}
	SD_SET_OVER(sd_over);
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
