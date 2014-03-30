# $FreeBSD: src/sbin/camcontrol/Makefile,v 1.3.2.5 2002/04/19 13:46:38 ru Exp $
# $DragonFly: src/sbin/camcontrol/Makefile,v 1.7 2008/03/07 08:44:24 pavalos Exp $

PROG=	camcontrol
SRCS=	camcontrol.c util.c
.if !defined(RELEASE_CRUNCH)
SRCS+=	modeedit.c
.else
CFLAGS+= -DMINIMALISTIC
.endif
DPADD=	${LIBCAM} ${LIBSBUF} ${LIBUTIL}
LDADD=	-lcam -lsbuf -lutil
MAN=	camcontrol.8

.include <bsd.prog.mk>
