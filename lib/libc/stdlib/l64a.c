/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 *
 * $NetBSD: l64a.c,v 1.13 2003/07/26 19:24:54 salo Exp $
 * $FreeBSD: src/lib/libc/stdlib/l64a.c,v 1.1 2005/12/24 22:37:59 trhodes Exp $
 */

#include <stdlib.h>

char *
l64a(long value)
{
	static char buf[8];

	l64a_r(value, buf, sizeof(buf));
	return (buf);
}

int
l64a_r(long value, char *buffer, int buflen)
{
	long v;
	int digit;

	v = value & (long)0xffffffff;
	for (; v != 0 && buflen > 1; buffer++, buflen--) {
		digit = v & 0x3f;
		if (digit < 2)
			*buffer = digit + '.';
		else if (digit < 12)
			*buffer = digit + '0' - 2;
		else if (digit < 38)
			*buffer = digit + 'A' - 12;
		else
			*buffer = digit + 'a' - 38;
		v >>= 6;
	}
	return (v == 0 ? 0 : -1);
}
