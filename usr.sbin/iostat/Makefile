#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/iostat/Makefile,v 1.5.2.1 2001/04/25 12:09:40 ru Exp $
# $DragonFly: src/usr.sbin/iostat/Makefile,v 1.4 2004/12/22 11:01:49 joerg Exp $

PROG=	iostat
MAN=	iostat.8
CFLAGS+=-I${.CURDIR}/../../sys
DPADD=	${LIBDEVSTAT} ${LIBKINFO}
LDADD=	-ldevstat -lkinfo

.include <bsd.prog.mk>
