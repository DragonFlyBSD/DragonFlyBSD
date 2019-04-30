#	$OpenBSD: Makefile,v 1.2 2003/11/03 19:51:42 otto Exp $

PROG=		bc
SRCS=		bc.y scan.l tty.c y.tab.h
CFLAGS+=	-I. -I${.CURDIR}
WARNS?=		3

CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD=		${LIBEDIT} ${LIBNCURSES}
LDADD=		-lprivate_edit -lprivate_ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.if !defined(NO_SHARE)
FILES=		bc.library
FILESDIR=	/usr/share/misc
.endif

.include <bsd.prog.mk>
