#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD: src/sbin/dmesg/Makefile,v 1.2.14.2 2001/08/01 06:20:49 obrien Exp $
# $DragonFly: src/sbin/dmesg/Makefile,v 1.3 2005/01/14 06:38:41 cpressey Exp $

PROG=	dmesg
MAN=	dmesg.8
LDADD=	-lkvm
DPADD=	${LIBKVM}
WARNS?=	6

.include <bsd.prog.mk>
