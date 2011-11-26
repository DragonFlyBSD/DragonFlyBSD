#	From: @(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/lpr/pac/Makefile,v 1.7.2.3 2002/06/30 03:59:16 gad Exp $
# $DragonFly: src/usr.sbin/lpr/pac/Makefile,v 1.2 2003/06/17 04:29:57 dillon Exp $

.PATH: ${.CURDIR}/../common_source

PROG=	pac
MAN=	pac.8

CFLAGS+= -I${.CURDIR}/../common_source

DPADD=	${LIBLPR}
LDADD=	${LIBLPR}

.include <bsd.prog.mk>
