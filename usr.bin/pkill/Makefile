#	$NetBSD: Makefile,v 1.1 2002/03/01 11:21:58 ad Exp $
#	$DragonFly: src/usr.bin/pkill/Makefile,v 1.2 2004/12/20 20:09:23 cpressey Exp $

PROG=	pkill
SRCS=	pkill.c
MAN=	pkill.1

LDADD+=	-lkvm
DPADD+=	${LIBKVM}

LINKS+=	${BINDIR}/pkill ${BINDIR}/pgrep
MLINKS+=pkill.1 pgrep.1

WARNS?=	6

.include <bsd.prog.mk>
