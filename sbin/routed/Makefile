# $FreeBSD: src/sbin/routed/Makefile,v 1.8.2.1 2001/04/25 10:58:50 ru Exp $

PROG=   routed
SRCS=   if.c input.c main.c output.c parms.c radix.c rdisc.c table.c trace.c
MAN=   routed.8
SUBDIR= rtquery

CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD=		${LIBCRYPTO}
LDADD=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
