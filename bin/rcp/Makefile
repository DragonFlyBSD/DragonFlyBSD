#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/bin/rcp/Makefile,v 1.16.2.2 2002/07/19 07:54:51 jmallett Exp $
# $DragonFly: src/bin/rcp/Makefile,v 1.5 2004/11/19 19:01:52 eirikn Exp $

PROG=	rcp
SRCS=	rcp.c util.c
CFLAGS+=-DBINDIR=${BINDIR}
WARNS=	6
WFORMAT=0

BINOWN=	root
BINMODE=4555
.if !defined(NOFSCHG)
INSTALLFLAGS=-fschg
.endif

.include <bsd.prog.mk>
