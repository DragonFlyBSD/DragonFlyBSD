# $FreeBSD: src/sbin/ipfw/Makefile,v 1.6.6.3 2002/07/24 03:21:23 luigi Exp $
# $DragonFly: src/sbin/ipfw/Makefile,v 1.2 2003/06/17 04:27:33 dillon Exp $

PROG=	ipfw
MAN=	ipfw.8
CFLAGS+=-Wall
.if defined(IPFW2)
SRCS=	ipfw2.c
CFLAGS+= -DIPFW2
.endif

.include <bsd.prog.mk>
