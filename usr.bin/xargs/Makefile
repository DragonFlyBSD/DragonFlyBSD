#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/xargs/Makefile,v 1.1.1.1.14.1 2002/06/17 04:44:46 jmallett Exp $
# $DragonFly: src/usr.bin/xargs/Makefile,v 1.3 2005/01/02 01:45:20 cpressey Exp $

PROG=	xargs
SRCS=	xargs.c strnsubst.c
WARNS?=	6

.if defined(BOOTSTRAPPING)
CFLAGS+=-DBOOTSTRAPPING
.endif

.include <bsd.prog.mk>
