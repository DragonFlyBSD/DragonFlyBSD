#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/bin/rcp/Makefile,v 1.16.2.2 2002/07/19 07:54:51 jmallett Exp $
# $DragonFly: src/bin/rcp/Makefile,v 1.3 2003/08/05 07:45:39 asmodai Exp $

PROG=	rcp
SRCS=	rcp.c util.c
CFLAGS+=-DBINDIR=${BINDIR}
WARNS=	0
WFORMAT=0

BINOWN=	root
BINMODE=4555
INSTALLFLAGS=-fschg

.include <bsd.prog.mk>
