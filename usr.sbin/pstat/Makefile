#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/pstat/Makefile,v 1.5.6.1 2001/04/25 12:10:38 ru Exp $
# $DragonFly: src/usr.sbin/pstat/Makefile,v 1.2 2003/06/17 04:30:01 dillon Exp $

PROG=	pstat
CFLAGS+=-I${.CURDIR}/../../sys
BINGRP=	kmem
BINMODE=2555
DPADD=	${LIBKVM}
LDADD=	-lkvm
MAN=	pstat.8
LINKS=	${BINDIR}/pstat ${BINDIR}/swapinfo
MLINKS=	pstat.8 swapinfo.8

.include <bsd.prog.mk>
