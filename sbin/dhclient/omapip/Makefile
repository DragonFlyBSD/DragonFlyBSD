# omapip
# $FreeBSD: src/sbin/dhclient/omapip/Makefile,v 1.2.2.2 2002/07/19 18:46:27 ru Exp $
# $DragonFly: src/sbin/dhclient/omapip/Attic/Makefile,v 1.3 2005/01/04 19:58:54 joerg Exp $

.include "${.CURDIR}/../Makefile.common"

LIB=	 omapi

.if !defined(BUILDING_SERVER)		# only install with server
INTERNALLIB=	true
NOMAN=		true
.endif

SRCS=	alloc.c array.c auth.c buffer.c connection.c convert.c dispatch.c \
	errwarn.c generic.c handle.c hash.c inet_addr.c listener.c \
	message.c mrtrace.c protocol.c result.c support.c toisc.c trace.c

MAN=	omapi.3

.include <bsd.lib.mk>
