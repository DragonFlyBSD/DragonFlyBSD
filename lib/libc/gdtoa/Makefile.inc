# $FreeBSD: src/lib/libc/gdtoa/Makefile.inc,v 1.10 2008/04/12 03:11:36 das Exp $

# netlib gdtoa sources
.PATH: ${.CURDIR}/../libc/gdtoa

CMAPS+=	${.CURDIR}/gdtoa/Symbol.map

MISRCS+=_hdtoa.c _hldtoa.c _ldtoa.c glue.c
GDTOASRCS+=dmisc.c dtoa.c gdtoa.c gethex.c gmisc.c \
	hd_init.c hexnan.c misc.c smisc.c \
	strtod.c strtodg.c strtof.c strtord.c sum.c ulp.c

CFLAGS+=-I${.CURDIR}/../../contrib/gdtoa

.for src in ${GDTOASRCS}
MISRCS+=gdtoa_${src}
CLEANFILES+=gdtoa_${src}
gdtoa_${src}:
	${LN} -sf ${.CURDIR}/../../contrib/gdtoa/${src} ${.TARGET}
.endfor
