# $FreeBSD: src/sbin/vinum/Makefile,v 1.12.2.2 2001/08/01 09:13:24 obrien Exp $

PROG=	vinum
SRCS=	v.c list.c vinumparser.c vinumutil.c vext.h commands.c
MAN=	vinum.8
WARNS?=	2

CFLAGS+=	-I${.CURDIR}/../../lib/libedit -I${.CURDIR}/../../sys
DPADD=	${LIBEDIT} ${LIBNCURSES} ${LIBDEVSTAT}
LDADD=	-lprivate_edit -lprivate_ncurses -ldevstat
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
