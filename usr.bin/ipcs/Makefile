# $FreeBSD: src/usr.bin/ipcs/Makefile,v 1.5 1999/08/28 01:02:15 peter Exp $
# $DragonFly: src/usr.bin/ipcs/Makefile,v 1.3 2007/08/27 16:50:54 pavalos Exp $

PROG=	ipcs
BINGRP=	kmem
BINMODE= 2555
DPADD+=	${LIBKVM}
LDADD+=	-lkvm
WARNS?=	1

.include <bsd.prog.mk>
