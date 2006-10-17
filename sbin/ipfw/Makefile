# $FreeBSD: src/sbin/ipfw/Makefile,v 1.6.6.3 2002/07/24 03:21:23 luigi Exp $
# $DragonFly: src/sbin/ipfw/Makefile,v 1.3 2006/10/17 00:55:41 pavalos Exp $

PROG=	ipfw
MAN=	ipfw.8
.if defined(IPFW2)
SRCS=	ipfw2.c
CFLAGS+= -DIPFW2
.endif
WARNS?=	2

.include <bsd.prog.mk>
