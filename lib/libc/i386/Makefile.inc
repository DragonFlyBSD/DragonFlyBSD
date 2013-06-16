# $FreeBSD: src/lib/libc/i386/Makefile.inc,v 1.3 2007/12/03 07:17:32 das Exp $

CMAPS+=	${.CURDIR}/i386/Symbol.map

# Long double is 80 bits
GDTOASRCS+=strtorx.c
MDSRCS+=machdep_ldisx.c
