#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/basename/Makefile,v 1.2.6.1 2001/08/02 00:59:28 obrien Exp $
# $DragonFly: src/usr.bin/basename/Makefile,v 1.4 2005/09/27 22:35:51 corecode Exp $

PROG=	basename
WARNS?=	6
MLINKS=	basename.1 dirname.1

.if defined(BOOTSTRAPPING)
CFLAGS+= -DNO_WCHAR
.endif

.include <bsd.prog.mk>
