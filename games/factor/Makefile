#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/factor/Makefile,v 1.2.6.2 2002/10/23 14:59:14 fanf Exp $
# $DragonFly: src/games/factor/Makefile,v 1.3 2005/03/16 06:18:56 cpressey Exp $

PROG=	factor
WARNS?=	6
SRCS=	factor.c pr_tbl.c
CFLAGS+=-I${.CURDIR}/../primes

.if exists(${.CURDIR}/../../crypto) && !defined(NO_OPENSSL)
CFLAGS+=-DHAVE_OPENSSL
LDADD+=	-lcrypto
DPADD+=	${LIBCRYPTO}
.endif

MAN=	factor.6
MLINKS+=factor.6 primes.6
.PATH:	${.CURDIR}/../primes

.include <bsd.prog.mk>
