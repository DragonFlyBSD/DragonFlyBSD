# $FreeBSD: src/usr.bin/ldd/Makefile,v 1.11 1999/08/28 01:02:42 peter Exp $

PROG=	ldd

# DO NOT USE -static, dlopen() for shared libs
.if !defined(REALLY_NOSHARED)
.undef NOSHARED
.endif

.include <bsd.prog.mk>
