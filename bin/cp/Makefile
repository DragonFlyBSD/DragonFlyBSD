#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/bin/cp/Makefile,v 1.7 1999/08/27 23:13:38 peter Exp $
# $DragonFly: src/bin/cp/Makefile,v 1.3 2004/09/26 15:32:56 asmodai Exp $

PROG=	cp
SRCS=	cp.c utils.c

WARNS?=	6

CFLAGS+=	 -W -DVM_AND_BUFFER_CACHE_SYNCHRONIZED

.include <bsd.prog.mk>
