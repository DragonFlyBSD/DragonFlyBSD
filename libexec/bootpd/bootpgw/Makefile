# Makefile
# $FreeBSD: src/libexec/bootpd/bootpgw/Makefile,v 1.4 1999/08/28 00:09:22 peter Exp $
# $DragonFly: src/libexec/bootpd/bootpgw/Makefile,v 1.2 2003/06/17 04:27:07 dillon Exp $

PROG=	bootpgw
NOMAN=  true
SRCS=	bootpgw.c getif.c hwaddr.c report.c rtmsg.c

SRCDIR=	${.CURDIR}/..
CFLAGS+=-I${SRCDIR}
.PATH:	${SRCDIR}

.include <bsd.prog.mk>
