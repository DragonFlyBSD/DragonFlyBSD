#	$OpenBSD: Makefile,v 1.2 2003/11/03 19:51:42 otto Exp $

PROG=		bc
SRCS=		bc.y scan.l tty.c y.tab.h
CFLAGS+=	-I. -I${.CURDIR}
WARNS?=		3

DPADD=		${LIBEDIT} ${LIBTERMCAP}
LDADD=		-ledit -ltermcap

SCRIPTS=	bc.library
SCRIPTSMODE=	444
SCRIPTSDIR=	/usr/share/misc
SCRIPTSNAME_bc.library= ${SCRIPTS}

.include <bsd.prog.mk>
