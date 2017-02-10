# $FreeBSD: head/sbin/devd/Makefile 203916 2010-02-15 14:07:40Z uqs $

PROG_CXX=devd
SRCS=	devd.cc token.l parse.y y.tab.h
MAN=	devd.8 devd.conf.5

WARNS?=	5

NO_SHARED?=YES

DPADD=	${LIBL} ${LIBUTIL}
LDADD=	-ll -lutil

YFLAGS+=-v
CFLAGS+=-I. -I${.CURDIR}

# c++ headers are very noisy under -flto (-Wodr)
.if (${CCVER:Mgcc5*} && ${CFLAGS:M-flto})
CXXFLAGS+=-Wno-error=odr
.endif


CLEANFILES= y.output

.include <bsd.prog.mk>
