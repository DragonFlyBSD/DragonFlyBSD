# $FreeBSD: src/usr.bin/smbutil/Makefile,v 1.1.2.3 2001/12/20 16:28:30 sheldonh Exp $
# $DragonFly: src/usr.bin/smbutil/Makefile,v 1.3 2007/08/27 16:50:58 pavalos Exp $

PROG=	smbutil
SRCS=	smbutil.c dumptree.c login.c lookup.c view.c print.c
WARNS?=	2

DPADD=	${LIBSMB}
LDADD=	-lsmb

CONTRIBDIR=	${.CURDIR}/../../contrib/smbfs
CFLAGS+=	-I${CONTRIBDIR}/include

.PATH:	${CONTRIBDIR}/smbutil

.include <bsd.prog.mk>
