.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	strip
SRCS=	objcopy.c is-strip.c
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

realinstall:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    maybe_stripped ${DESTDIR}${BINDIR}/strip

.include <bsd.prog.mk>
