#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/vmstat/Makefile,v 1.5.6.1 2001/04/25 11:29:44 ru Exp $
# $DragonFly: src/usr.bin/vmstat/Makefile,v 1.3 2004/12/22 11:01:49 joerg Exp $

PROG=	vmstat
MAN=	vmstat.8
CFLAGS+=-I${.CURDIR}/../../sys
BINGRP=	kmem
BINMODE=2555
DPADD=	${LIBKINFO} ${LIBKVM} ${LIBDEVSTAT}
LDADD=	-lkinfo -lkvm -ldevstat

.include <bsd.prog.mk>
