/* SPDX-License-Identifier: GPL-3.0-only */
/* The card, as MENU.BIN needs it: mount, list /FIRMWARES, read one package. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "s2_flash.h"

#define S2_NAME_MAX 12u            /* 8.3 */

struct s2_file {
    char name[S2_NAME_MAX + 1];    /* the 8.3 name: what opens it */
    char label[S2_LABEL_MAX + 1];  /* what the list shows */
};

bool s2_card_mount(void);
/* The packages in /FIRMWARES, in directory order; returns how many, at most max. */
unsigned s2_card_list(struct s2_file *files, unsigned max);
bool s2_card_open(const char *name, uint32_t *size);
/* struct s2_src's read, on the open file. */
bool s2_card_read(void *ctx, uint32_t offset, void *dst, uint32_t size);
