#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/which/Makefile,v 1.5.2.3 2002/08/07 16:32:01 ru Exp $
# $DragonFly: src/usr.bin/which/Makefile,v 1.2 2003/06/17 04:29:33 dillon Exp $

MAINTAINER= wosch

MAN=	which.1

beforeinstall:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    ${.CURDIR}/which.pl ${DESTDIR}${BINDIR}/which

.include <bsd.prog.mk>
