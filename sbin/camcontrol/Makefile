# $FreeBSD: src/sbin/camcontrol/Makefile,v 1.3.2.5 2002/04/19 13:46:38 ru Exp $

PROG=	camcontrol
SRCS=	camcontrol.c util.c
SRCS+=	modeedit.c
DPADD=	${LIBCAM} ${LIBSBUF} ${LIBUTIL}
LDADD=	-lcam -lsbuf -lutil
MAN=	camcontrol.8

.include <bsd.prog.mk>
