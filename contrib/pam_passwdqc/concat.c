/*
 * concat() - allocate memory and safely concatenate strings in portable C
 * (and C++ if you like).
 *
 * This code deals gracefully with potential integer overflows (perhaps when
 * input strings are maliciously long), as well as with input strings changing
 * from under it (perhaps because of misbehavior of another thread).  It does
 * not depend on non-portable functions such as snprintf() and asprintf().
 *
 * Written by Solar Designer <solar at openwall.com> and placed in the
 * public domain.
 */

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include "concat.h"

char *concat(const char *s1, ...)
{
	va_list args;
	const char *s;
	char *p, *result;
	size_t l, m, n;

	m = n = strlen(s1);
	va_start(args, s1);
	while ((s = va_arg(args, char *))) {
		l = strlen(s);
		if ((m += l) < l)
			break;
	}
	va_end(args);
	if (s || m >= INT_MAX)
		return NULL;

	result = (char *)malloc(m + 1);
	if (!result)
		return NULL;

	memcpy(p = result, s1, n);
	p += n;
	va_start(args, s1);
	while ((s = va_arg(args, char *))) {
		l = strlen(s);
		if ((n += l) < l || n > m)
			break;
		memcpy(p, s, l);
		p += l;
	}
	va_end(args);
	if (s || m != n || p != result + n) {
		free(result);
		return NULL;
	}

	*p = 0;
	return result;
}

#ifdef TEST
#include <stdio.h>

int main(int argc, char **argv)
{
	puts(concat(argv[0], argv[1], argv[2], argv[3], NULL));
	return 0;
}
#endif
