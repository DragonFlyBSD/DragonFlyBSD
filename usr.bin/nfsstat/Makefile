# $FreeBSD: src/usr.bin/nfsstat/Makefile,v 1.1.1.1.14.1 2001/05/25 23:42:08 tmm Exp $
# $DragonFly: src/usr.bin/nfsstat/Makefile,v 1.3 2007/08/27 16:50:57 pavalos Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/6/93

PROG=	nfsstat
CFLAGS+=-DNFS
DPADD=  ${LIBKVM}
LDADD= -lkvm
WARNS?=	1

.include <bsd.prog.mk>
