# $FreeBSD: src/libexec/atrun/Makefile,v 1.15 2007/10/01 18:17:23 ru Exp $
# $DragonFly: src/libexec/atrun/Makefile,v 1.3 2004/09/20 19:32:19 joerg Exp $

MAINSRC=${.CURDIR}/../../usr.bin/at

.include "${MAINSRC}/Makefile.inc"

PROG=	atrun
SRCS=	atrun.c gloadavg.c
MAN=	atrun.8
WARNS?=	6

BINDIR=	${ATLIB_DIR}

CFLAGS+=-I${MAINSRC} -I${.CURDIR}
CFLAGS+=-DLOGIN_CAP -DPAM
WFORMAT=0

DPADD=	${LIBPAM} ${LIBUTIL}
LDADD=	${MINUSLPAM} -lutil

.include <bsd.prog.mk>
