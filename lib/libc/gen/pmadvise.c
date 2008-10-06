/*
 * The contents of this file are in the public domain.
 * Written by Garrett A. Wollman, 2000-10-07.
 *
 * $FreeBSD: src/lib/libc/gen/pmadvise.c,v 1.3 2003/08/09 03:23:24 bms Exp $
 * $DragonFly: src/lib/libc/gen/pmadvise.c,v 1.1 2008/10/06 21:01:37 swildner Exp $
 */

#include <sys/mman.h>

int
posix_madvise(void *address, size_t size, int how)
{
	return madvise(address, size, how);
}
