#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.sbin/sliplogin/Makefile,v 1.4.6.1 2001/04/25 12:10:56 ru Exp $
# $DragonFly: src/usr.sbin/sliplogin/Makefile,v 1.3 2004/11/17 19:16:12 dillon Exp $

PROG=	sliplogin
MAN=	sliplogin.8
BINGRP=	network
BINMODE=4550
.if !defined(NOFSCHG)
INSTALLFLAGS=-fschg
.endif

.include <bsd.prog.mk>
