# $FreeBSD: src/sbin/vinum/Makefile,v 1.12.2.2 2001/08/01 09:13:24 obrien Exp $

PROG=	vinum
SRCS=	v.c list.c vinumparser.c vinumutil.c vext.h commands.c
MAN=	vinum.8
WARNS?=	2

# for <dev/raid/vinum/vinumhdr.h>
CFLAGS+=	-I${.CURDIR}/../../sys
DPADD=		${LIBDEVSTAT}
LDADD=		-ldevstat

CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD+=		${LIBEDIT} ${LIBNCURSES}
LDADD+=		-lprivate_edit -lprivate_ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
