# $FreeBSD: src/usr.sbin/ndiscvt/Makefile,v 1.5 2004/02/23 20:21:21 johan Exp $
# $DragonFly: src/usr.sbin/ndiscvt/Makefile,v 1.2 2005/02/04 21:30:42 cpressey Exp $

.PATH:	${.CURDIR}/../../sys/emulation/ndis

PROG=	ndiscvt
SRCS=	ndiscvt.c
SRCS+=	subr_pe.c
SRCS+=	inf.c inf-token.l inf-parse.y y.tab.h

MAN8=	ndiscvt.8

WARNS?=	6

DPADD=  ${LIBL}
LDADD=  -ll

YFLAGS+=-v

CFLAGS+=-I. -I${.CURDIR} -I${.CURDIR}/../../sys

CLEANFILES= y.output

.include <bsd.prog.mk>
