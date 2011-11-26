#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/dm/Makefile,v 1.3.6.1 2001/04/25 09:28:52 ru Exp $
# $DragonFly: src/games/dm/Makefile,v 1.4 2006/10/08 16:22:35 pavalos Exp $

# -DLOG		log games
.PATH: ${.CURDIR}/../../usr.bin/who

PROG=	dm
SRCS=	dm.c utmpentry.c
MAN=	dm.8 dm.conf.5
BINGRP= games
BINMODE=2555

DPADD=	${LIBUTIL}
LDADD=	-lutil

CFLAGS+= -I${.CURDIR}/../../usr.bin/who -DSUPPORT_UTMP -DSUPPORT_UTMPX

.include <bsd.prog.mk>
