#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/basename/Makefile,v 1.2.6.1 2001/08/02 00:59:28 obrien Exp $
# $DragonFly: src/usr.bin/basename/Makefile,v 1.5 2007/08/27 16:50:51 pavalos Exp $

PROG=	basename
MLINKS=	basename.1 dirname.1

.if defined(BOOTSTRAPPING)
CFLAGS+= -DNO_WCHAR
.endif

.include <bsd.prog.mk>
