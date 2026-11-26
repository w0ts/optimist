/* SPDX-License-Identifier: GPL-3.0-only */
/* The C library the Launchpad core calls: memset, memcpy and memcmp, declared here instead of <string.h> (owner 2026-11-26: the FM-1's toolchain has no <string.h>, <stdio.h>
 * or <stdarg.h>, only memset, memcpy, memmove and memcmp to link). The names in parentheses: a C library that makes them macros (_FORTIFY_SOURCE) leaves these alone. */
#ifndef JANK_LPSTR_H
#define JANK_LPSTR_H
#include <stddef.h>

void *(memset)(void *s, int c, size_t n);
void *(memcpy)(void *dst, const void *src, size_t n);
int (memcmp)(const void *a, const void *b, size_t n);
#endif
