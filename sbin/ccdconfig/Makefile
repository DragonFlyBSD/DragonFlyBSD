# $FreeBSD: src/sbin/ccdconfig/Makefile,v 1.2.10.1 2001/04/25 10:58:12 ru Exp $
# $DragonFly: src/sbin/ccdconfig/Makefile,v 1.2 2003/06/17 04:27:32 dillon Exp $

PROG=	ccdconfig
MAN=	ccdconfig.8

CFLAGS+=	-I${.CURDIR}/../../sys
LDADD+=		-lkvm
DPADD+=		${LIBKVM}
BINGRP=		kmem
BINMODE=	2555

.include <bsd.prog.mk>
