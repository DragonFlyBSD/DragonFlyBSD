#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/pstat/Makefile,v 1.5.6.1 2001/04/25 12:10:38 ru Exp $
# $DragonFly: src/usr.sbin/pstat/Makefile,v 1.6 2005/02/03 17:28:40 joerg Exp $

PROG=	pstat
CFLAGS+=-I${.CURDIR}/../../sys -g
CFLAGS+=-DUSE_KCORE
BINGRP=	kmem
BINMODE=2555
DPADD=	${LIBKCORE} ${LIBKINFO} ${LIBKVM}
LDADD=	-lkcore -lkinfo -lkvm
MAN=	pstat.8
LINKS=	${BINDIR}/pstat ${BINDIR}/swapinfo
MLINKS=	pstat.8 swapinfo.8
WARNS?=	6

.include <bsd.prog.mk>
