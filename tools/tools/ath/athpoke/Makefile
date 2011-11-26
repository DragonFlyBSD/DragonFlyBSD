# $FreeBSD: src/tools/tools/ath/athpoke/Makefile,v 1.3 2009/06/24 18:24:37 sam Exp $

PROG=	athpoke
LINKS=	${BINDIR}/${PROG} ${BINDIR}/athpeek

.PATH.c: ${.CURDIR}/../common

SRCS=	athpoke.c
SRCS+=	dumpregs_5210.c
SRCS+=	dumpregs_5211.c
SRCS+=	dumpregs_5212.c
SRCS+=	dumpregs_5416.c

.include <../Makefile.inc>

.include <bsd.prog.mk>
