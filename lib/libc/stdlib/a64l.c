/*-
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * $NetBSD: a64l.c,v 1.8 2000/01/22 22:19:19 mycroft Exp $
 * $FreeBSD: src/lib/libc/stdlib/a64l.c,v 1.2 2006/05/19 19:06:38 jkim Exp $
 */

#include <stdlib.h>
#include <inttypes.h>

long
a64l(const char *s)
{
	long shift;
	int digit, i, value;

	value = 0;
	shift = 0;
	for (i = 0; *s != '\0' && i < 6; i++, s++) {
		if (*s <= '/')
			digit = *s - '/' + 1;
		else if (*s <= '0' + 9)
			digit = *s - '0' + 2;
		else if (*s <= 'A' + 25)
			digit = *s - 'A' + 12;
		else
			digit = *s - 'a' + 38;

		value |= digit << shift;
		shift += 6;
	}
	return (value);
}
