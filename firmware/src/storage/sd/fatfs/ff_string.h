/* SPDX-License-Identifier: GPL-3.0-only */
/* Optimist (2026-10-26): what ff.c takes from <string.h>, for the freestanding firmware: memset / memcpy / memcmp are
 * system/libc.c's (felucca.c's unit, linked), strchr (ff.c's name checks only) is here. A host build includes
 * <string.h> instead (ff.c). */
#ifndef FF_STRING_H
#define FF_STRING_H
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);

static char *strchr(const char *s, int c)
{
	for (; *s; s++)
		if (*s == (char)c) return (char *)s;
	return c ? 0 : (char *)s;
}
#endif
