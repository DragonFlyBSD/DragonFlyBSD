#	@(#)Makefile	8.1 (Berkeley) 6/2/93
# $FreeBSD: src/bin/ls/Makefile,v 1.9.2.4 2002/04/19 13:22:45 ru Exp $
# $DragonFly: src/bin/ls/Makefile,v 1.5 2005/02/06 06:16:40 okumoto Exp $

PROG=	ls
SRCS=	cmp.c ls.c print.c util.c

DPADD=	${LIBM}
LDADD=	-lm

.if !defined(RELEASE_CRUNCH)
CFLAGS+= -DCOLORLS
DPADD+=	${LIBTERMCAP}
LDADD+=	-ltermcap
.endif

.include <bsd.prog.mk>
