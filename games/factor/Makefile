#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/factor/Makefile,v 1.2.6.2 2002/10/23 14:59:14 fanf Exp $

PROG=	factor
SRCS=	factor.c pr_tbl.c
MAN=	factor.6
MLINKS+=factor.6 primes.6
GAMESCRYPTO=

CFLAGS+=	-I${.CURDIR}/../primes
CFLAGS+=	-DHAVE_OPENSSL

.PATH:	${.CURDIR}/../primes

.include <bsd.prog.mk>
