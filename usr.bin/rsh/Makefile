#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.bin/rsh/Makefile,v 1.17.2.1 2002/07/17 19:08:22 ru Exp $
# $DragonFly: src/usr.bin/rsh/Makefile,v 1.6 2007/08/27 16:50:58 pavalos Exp $

PROG=	rsh
CFLAGS+=-I${.CURDIR}/../../libexec/rlogind

BINMODE=4555
.if !defined(NOFSCHG)
INSTALLFLAGS=-fschg
.endif

.include <bsd.prog.mk>
