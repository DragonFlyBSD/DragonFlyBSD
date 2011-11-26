# $FreeBSD: src/libexec/atrun/Makefile,v 1.15 2007/10/01 18:17:23 ru Exp $

MAINSRC=${.CURDIR}/../../usr.bin/at

.include "${MAINSRC}/Makefile.inc"

PROG=	atrun
SRCS=	atrun.c gloadavg.c
MAN=	atrun.8

BINDIR=	${ATLIB_DIR}

CFLAGS+=-I${MAINSRC} -I${.CURDIR}
CFLAGS+=-DLOGIN_CAP -DPAM

DPADD=	${LIBPAM} ${LIBUTIL}
LDADD=	${MINUSLPAM} -lutil

.include <bsd.prog.mk>
