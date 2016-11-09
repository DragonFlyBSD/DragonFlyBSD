# $FreeBSD: src/usr.sbin/pppctl/Makefile,v 1.5.2.1 2001/04/25 12:10:34 ru Exp $

PROG=	pppctl
MAN=	pppctl.8
WARNS?=	2
LDADD+= -lprivate_edit -lprivate_ncurses
DPADD+= ${LIBEDIT} ${LIBNCURSES}
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
