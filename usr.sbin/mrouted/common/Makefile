# $FreeBSD: src/usr.sbin/mrouted/common/Makefile,v 1.5.2.1 2002/07/19 18:46:29 ru Exp $
# $DragonFly: src/usr.sbin/mrouted/common/Makefile,v 1.2 2003/06/17 04:29:57 dillon Exp $

LIB=		mrouted
INTERNALLIB=	YES

S=	${.CURDIR}/..
.PATH:	$S
CFLAGS+= -I$S

SRCS=	igmp.c inet.c kern.c

.include <bsd.lib.mk>
