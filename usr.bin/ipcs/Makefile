# $FreeBSD: src/usr.bin/ipcs/Makefile,v 1.5 1999/08/28 01:02:15 peter Exp $
# $DragonFly: src/usr.bin/ipcs/Makefile,v 1.2 2003/06/17 04:29:27 dillon Exp $

PROG=	ipcs
BINGRP=	kmem
BINMODE= 2555
DPADD+=	${LIBKVM}
LDADD+=	-lkvm

.include <bsd.prog.mk>
