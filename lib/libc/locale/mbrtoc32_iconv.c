/* $FreeBSD: head/lib/libc/locale/mbrtoc32_iconv.c 251314 2013-06-03 17:17:56Z ed $ */
#define	charXX_t	char32_t
#define	mbrtocXX	mbrtoc32
#define	mbrtocXX_l	mbrtoc32_l
#define	DSTBUF_LEN	1
#define	UTF_XX_INTERNAL	"UTF-32-INTERNAL"

#include "mbrtocXX_iconv.h"
