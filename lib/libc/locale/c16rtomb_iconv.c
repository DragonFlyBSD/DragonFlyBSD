/* $FreeBSD: head/lib/libc/locale/c16rtomb_iconv.c 251314 2013-06-03 17:17:56Z ed $ */
#define	charXX_t	char16_t
#define	cXXrtomb	c16rtomb
#define	cXXrtomb_l	c16rtomb_l
#define	SRCBUF_LEN	2
#define	UTF_XX_INTERNAL	"UTF-16-INTERNAL"

#include "cXXrtomb_iconv.h"
