#	@(#)Makefile	8.1 (Berkeley) 6/2/93
# $FreeBSD: src/bin/ls/Makefile,v 1.26 2004/05/25 14:53:47 pjd Exp $

PROG=	ls
SRCS=	cmp.c ls.c print.c util.c

DPADD=	${LIBUTIL} ${LIBNCURSES}
LDADD=	-lutil -lprivate_ncurses
CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

CFLAGS+= -DCOLORLS

.include <bsd.prog.mk>
