#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $DragonFly: src/games/primes/Makefile,v 1.2 2005/04/25 16:10:24 liamfoy Exp $

PROG=	primes
SRCS=	pattern.c pr_tbl.c primes.c
NOMAN=	noman
DPADD=	${LIBM}
LDADD=	-lm
WARNS?=	6

.include <bsd.prog.mk>
