#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/iostat/Makefile,v 1.5.2.1 2001/04/25 12:09:40 ru Exp $
# $DragonFly: src/usr.sbin/iostat/Makefile,v 1.5 2005/01/02 02:40:20 cpressey Exp $

PROG=	iostat
MAN=	iostat.8
CFLAGS+=-I${.CURDIR}/../../sys
DPADD=	${LIBDEVSTAT} ${LIBKINFO}
LDADD=	-ldevstat -lkinfo
WARNS?= 6

.include <bsd.prog.mk>
