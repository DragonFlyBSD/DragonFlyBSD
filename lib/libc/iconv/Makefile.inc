# $DragonFly: src/lib/libc/iconv/Makefile.inc,v 1.2 2005/04/29 21:23:42 joerg Exp $

.PATH: ${.CURDIR}/../libc/iconv

CMAPS+=	${.CURDIR}/iconv/Symbol.map

SRCS+=	iconv.c
.if ${LIB} == "c"
MAN+=	iconv.3
MLINKS+=iconv.3 iconv_open.3 iconv.3 iconv_close.3
.endif
