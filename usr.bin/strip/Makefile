#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/strip/Makefile,v 1.10.6.3 2002/08/07 16:32:01 ru Exp $
# $DragonFly: src/usr.bin/strip/Attic/Makefile,v 1.4 2004/01/28 16:41:18 joerg Exp $

PROG=	strip
MAN=	strip.1aout
BINDIR= /usr/libexec/binutils212/aout
CLEANFILES += maybe_stripped

all: maybe_stripped

maybe_stripped: strip
	cp -p strip maybe_stripped
.if defined(STRIP)
.if ${STRIP:M-s} != ""
	-strip maybe_stripped
.endif
.endif

realinstall:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    maybe_stripped ${DESTDIR}${BINDIR}/strip

.include <bsd.prog.mk>
