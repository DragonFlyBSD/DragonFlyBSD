#	$NetBSD: Makefile,v 1.7 1995/03/18 14:56:21 cgd Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/5/93
#	$FreeBSD: src/usr.sbin/rip6query/Makefile,v 1.1.2.2 2001/04/25 12:10:43 ru Exp $
#	$DragonFly: src/usr.sbin/rip6query/Makefile,v 1.3 2005/01/17 01:49:24 cpressey Exp $

PROG=	rip6query
WARNS?=	6
MAN=	rip6query.8

CFLAGS+=-DINET6 -I${.CURDIR}/../route6d

.include <bsd.prog.mk>
