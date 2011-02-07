#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/finger/Makefile,v 1.1.1.1.14.2 2001/04/25 11:29:20 ru Exp $
# $DragonFly: src/usr.bin/finger/Makefile,v 1.3 2007/08/27 16:50:53 pavalos Exp $

.PATH: ${.CURDIR}/../../usr.bin/who

PROG=	finger
SRCS=	finger.c lprint.c net.c sprint.c util.c utmpentry.c
MAN=	finger.1 finger.conf.5

DPADD+=	${LIBUTIL}
LDADD+=	-lutil

CFLAGS+= -I${.CURDIR}/../../usr.bin/who -DSUPPORT_UTMP -DSUPPORT_UTMPX

.include <bsd.prog.mk>
