# $FreeBSD: head/usr.bin/stat/Makefile 216206 2010-12-05 21:33:05Z dougb $

PROG=	stat
LINKS=	${BINDIR}/stat ${BINDIR}/readlink
MLINKS=	stat.1 readlink.1

.if defined(BOOTSTRAPPING)
CFLAGS+= -DBOOTSTRAPPING
.endif

.include <bsd.prog.mk>
