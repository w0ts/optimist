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
 console. The command sequence is ChaN's.
/-------------------------------------------------------------------------*/

#include "diskio.h"

#ifndef SD_TX                               /* the FM-1's bus (a host test defines its own) */
#include "fm1_sd_bus.h"
#include "fm1_time.h"
#define SD_TX(b) fm1_sd_tx(b)
#define SD_RX() fm1_sd_rx()
#define SD_SEL() fm1_sd_sel()
#define SD_DESEL() fm1_sd_desel()
#define SD_FAST() fm1_sd_baud(FM1_SD_BAUD_FAST)   /* the card is up: the full clock */
#define SD_NOW() fm1_ticks()
#define SD_TICKS_MS (FM1_TICKS_PER_US * 1000u)
#endif

static DWORD sd_ms_since(DWORD t0) { return (DWORD)(SD_NOW() - t0) / SD_TICKS_MS; }   /* (wraps cleanly) */

/* Definitions for MMC/SDC command */
#define CMD0	(0x40+0)	/* GO_IDLE_STATE */
#define CMD1	(0x40+1)	/* SEND_OP_COND (MMC) */
#define	ACMD41	(0xC0+41)	/* SEND_OP_COND (SDC) */
#define CMD8	(0x40+8)	/* SEND_IF_COND */
#define CMD9	(0x40+9)	/* SEND_CSD */
#define CMD16	(0x40+16)	/* SET_BLOCKLEN */
#define CMD17	(0x40+17)	/* READ_SINGLE_BLOCK */
#define CMD24	(0x40+24)	/* WRITE_BLOCK */
#define CMD55	(0x40+55)	/* APP_CMD */
#define CMD58	(0x40+58)	/* READ_OCR */

/* Card type flags (CardType) */
#define CT_MMC				0x01	/* MMC ver 3 */
#define CT_SD1				0x02	/* SD ver 1 */
#define CT_SD2				0x04	/* SD ver 2 */
#define CT_SDC				(CT_SD1|CT_SD2)	/* SD */
#define CT_BLOCK			0x08	/* Block addressing */

static BYTE CardType;			/* b0:MMC, b1:SDv1, b2:SDv2, b3:Block addressing */
static DWORD sd_sectors;		/* the card's capacity in 512-byte sectors (CSD; 0: not read) */

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

	/* Select the card */
	SD_DESEL(); (void)SD_RX();
	SD_SEL(); (void)SD_RX();

	/* Send a command packet */
	SD_TX(cmd);					/* Start + Command index */
	SD_TX((BYTE)(arg >> 24));	/* Argument[31..24] */
	SD_TX((BYTE)(arg >> 16));	/* Argument[23..16] */
	SD_TX((BYTE)(arg >> 8));	/* Argument[15..8] */
	SD_TX((BYTE)arg);			/* Argument[7..0] */
	n = 0x01;					/* Dummy CRC + Stop */
	if (cmd == CMD0) n = 0x95;	/* Valid CRC for CMD0(0) */
	if (cmd == CMD8) n = 0x87;	/* Valid CRC for CMD8(0x1AA) */
	SD_TX(n);

	/* Receive a command response */
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
DSTATUS disk_initialize(void)
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
	CardType = ty;
	release_spi();
	if (ty) SD_FAST();
	sd_sectors = ty ? sd_read_capacity() : 0;

	return ty ? 0 : STA_NOINIT;
}

/* Read partial sector */
DRESULT disk_readp(
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
			bc = 514 - offset - count;

			/* Skip leading bytes */
			if (offset) skip_mmc(offset);

			/* Receive a part of the sector */
			if (buff) {
				do
					*buff++ = SD_RX();
				while (--count);
			} else {
				skip_mmc(count);
			}

			/* Skip trailing bytes and CRC */
			skip_mmc(bc);

			res = RES_OK;
		}
	}

	release_spi();

	return res;
}

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

	res = RES_ERROR;

	if (buff) {		/* Send data bytes */
		bc = (UINT)sc;
		while (bc && wc) {		/* Send data bytes to the card */
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
				res = RES_OK;
			}
		} else {	/* Finalize sector write transaction */
			bc = wc + 2;
			while (bc--) SD_TX(0);	/* Fill left bytes and CRC with zeros */
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
