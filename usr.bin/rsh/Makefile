#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.bin/rsh/Makefile,v 1.24 2005/01/27 14:52:45 delphij Exp $
# $DragonFly: src/usr.bin/rsh/Makefile,v 1.6 2007/08/27 16:50:58 pavalos Exp $

PROG=	rsh
CFLAGS+=-I${.CURDIR}/../../libexec/rlogind

BINOWN=	root
BINMODE=4555
.if !defined(NOFSCHG)
INSTALLFLAGS=-fschg
.endif

.include <bsd.prog.mk>
