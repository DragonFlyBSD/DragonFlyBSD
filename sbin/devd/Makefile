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
# XXX gcc50 (at least) warns about its own C++ headers with
#     -Wunused-local-typedefs, so disable until fixed
#
.if (${CCVER:Mgcc4[89]} || ${CCVER:Mgcc5*})
CXXFLAGS+=-Wno-unused-local-typedefs
.endif

CLEANFILES= y.output

.include <bsd.prog.mk>
