# $DragonFly: src/lib/libc/iconv/Makefile.inc,v 1.1 2005/03/11 23:33:53 joerg Exp $

.PATH: ${.CURDIR}/../libc/iconv

SRCS+=	iconv.c
MAN+=	iconv.3
MLINKS+=iconv.3 iconv_open.3 iconv.3 iconv_close.3
