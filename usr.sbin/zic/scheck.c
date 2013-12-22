/*
 * $FreeBSD: src/usr.sbin/zic/scheck.c,v 1.4 1999/08/28 01:21:19 peter Exp $
 */
/*LINTLIBRARY*/

#include "private.h"

const char *
scheck(const char * const string, const char * const format)
{
	char *fbuf;
	const char *fp;
	char *tp;
	int c;
	const char *result;
	char dummy;

	result = "";
	if (string == NULL || format == NULL)
		return result;
	fbuf = malloc(2 * strlen(format) + 4);
	if (fbuf == NULL)
		return result;
	fp = format;
	tp = fbuf;

	/*
	** Copy directives, suppressing each conversion that is not
	** already suppressed.  Scansets containing '%' are not
	** supported; e.g., the conversion specification "%[%]" is not
	** supported.  Also, multibyte characters containing a
	** non-leading '%' byte are not supported.
	*/
	while ((*tp++ = c = *fp++) != '\0') {
		if (c != '%')
			continue;
		if (is_digit(*fp)) {
			char const *f = fp;
			char *t = tp;
			do {
				*t++ = c = *f++;
			} while (is_digit(c));
			if (c == '$') {
				fp = f;
				tp = t;
			}
		}
		*tp++ = '*';
		if (*fp == '*')
			++fp;
		if ((*tp++ = *fp++) == '\0')
			break;
	}

	*(tp - 1) = '%';
	*tp++ = 'c';
	*tp = '\0';
	if (sscanf(string, fbuf, &dummy) != 1)
		result = format;
	free(fbuf);
	return result;
}
