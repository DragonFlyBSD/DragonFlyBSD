#	@(#)Makefile	8.1 (Berkeley) 6/2/93
# $FreeBSD: src/bin/ls/Makefile,v 1.9.2.4 2002/04/19 13:22:45 ru Exp $
# $DragonFly: src/bin/ls/Makefile,v 1.3 2004/09/26 15:28:02 asmodai Exp $

PROG=	ls
SRCS=	cmp.c ls.c print.c util.c

WARNS?=	3

DPADD=	${LIBM}
LDADD=	-lm

.if !defined(RELEASE_CRUNCH)
CFLAGS+= -DCOLORLS
DPADD+=	${LIBTERMCAP}
LDADD+=	-ltermcap
.endif

.include <bsd.prog.mk>
