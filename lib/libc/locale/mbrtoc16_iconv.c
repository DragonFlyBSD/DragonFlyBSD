/* $FreeBSD: head/lib/libc/locale/mbrtoc16_iconv.c 251314 2013-06-03 17:17:56Z ed $ */
#define	charXX_t	char16_t
#define	mbrtocXX	mbrtoc16
#define	mbrtocXX_l	mbrtoc16_l
#define	DSTBUF_LEN	2
#define	UTF_XX_INTERNAL	"UTF-16-INTERNAL"

#include "mbrtocXX_iconv.h"
