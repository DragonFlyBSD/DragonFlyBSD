# Makefile
# $FreeBSD: src/libexec/bootpd/tools/bootptest/Makefile,v 1.6.2.1 2001/04/25 10:40:50 ru Exp $
# $DragonFly: src/libexec/bootpd/tools/bootptest/Makefile,v 1.2 2003/06/17 04:27:07 dillon Exp $

PROG=	bootptest
MAN=	bootptest.8
SRCS=	bootptest.c getether.c getif.c print-bootp.c report.c

SRCDIR=	${.CURDIR}/../..
CFLAGS+=-I${SRCDIR}
.PATH:	${SRCDIR}

.include <bsd.prog.mk>
