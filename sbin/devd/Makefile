# $FreeBSD: src/sbin/devd/Makefile,v 1.9 2007/11/19 00:19:01 jb Exp $
# $DragonFly: src/sbin/devd/Makefile,v 1.2 2008/10/28 16:56:56 swildner Exp $

PROG_CXX=devd
SRCS=	devd.cc token.l parse.y y.tab.h
MAN=	devd.8 devd.conf.5

WARNS?=	5

NO_SHARED?=YES

DPADD=	${LIBL} ${LIBUTIL}
LDADD=	-ll -lutil

YFLAGS+=-v
CFLAGS+=-I. -I${.CURDIR}

CLEANFILES= y.output

.include <bsd.prog.mk>
