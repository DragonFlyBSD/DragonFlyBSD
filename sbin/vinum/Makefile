# $FreeBSD: src/sbin/vinum/Makefile,v 1.12.2.2 2001/08/01 09:13:24 obrien Exp $
# $DragonFly: src/sbin/vinum/Makefile,v 1.6 2005/02/03 22:05:27 joerg Exp $

PROG=	vinum
SRCS=	v.c list.c vinumparser.c vinumutil.c vext.h commands.c
MAN=	vinum.8

CFLAGS+=	-I${.CURDIR}/../../sys -Wall
DPADD=	${LIBUTIL} ${LIBREADLINE} ${LIBTERMCAP}
LDADD=	-lreadline -ltermcap -ldevstat

.include <bsd.prog.mk>
