#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/canfield/canfield/Makefile,v 1.5.2.3 2002/08/07 16:31:41 ru Exp $
# $DragonFly: src/games/canfield/canfield/Makefile,v 1.3 2005/11/19 09:50:30 swildner Exp $

PROG=	canfield
MAN=	canfield.6
DPADD=	${LIBCURSES}
LDADD=	-lcurses
HIDEGAME=hidegame
MLINKS=	canfield.6 cfscores.6
WARNS?=	6

beforeinstall:
.if !exists(${DESTDIR}/var/games/cfscores)
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 664 /dev/null \
	    ${DESTDIR}/var/games/cfscores
.endif

.include "../../Makefile.inc"
.include <bsd.prog.mk>
