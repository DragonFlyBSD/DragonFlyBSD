.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	strip
SRCS=	objcopy.c is-strip.c
MAN=	strip221.1
WARNS=	1

CLEANFILES+=	maybe_stripped

all: maybe_stripped

maybe_stripped: strip
	cp strip maybe_stripped
.if defined(STRIP)
.if ${STRIP:M-s} != ""
	-strip maybe_stripped
.endif
.endif

strip221.1: ${SRCDIR}/binutils/doc/strip.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	strip221.1

realinstall:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    maybe_stripped ${DESTDIR}${BINDIR}/strip

.include <bsd.prog.mk>
