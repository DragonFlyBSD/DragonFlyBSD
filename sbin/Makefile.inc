#	@(#)Makefile.inc	8.1 (Berkeley) 6/8/93
# $FreeBSD: src/sbin/Makefile.inc,v 1.5.6.1 2001/08/01 05:53:59 obrien Exp $
# $DragonFly: src/sbin/Makefile.inc,v 1.3 2006/10/17 00:55:39 pavalos Exp $

BINDIR?=	/sbin
.if defined(NO_DYNAMICROOT)
NOSHARED?=	YES
.endif
WARNS?=		6
