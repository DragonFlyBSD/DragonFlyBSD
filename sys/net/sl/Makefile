# $FreeBSD: src/sys/modules/if_sl/Makefile,v 1.13 1999/11/28 18:52:53 bde Exp $
# $DragonFly: src/sys/net/sl/Makefile,v 1.4 2005/02/18 11:41:42 corecode Exp $

.PATH:  ${.CURDIR}/../ppp_layer
KMOD=   if_sl
SRCS=   if_sl.c slcompress.c opt_inet.h use_sl.h

NSL?=		2
PROTOS?=	-DINET

CFLAGS+= ${PROTOS}
CLEANFILES=	use_sl.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > opt_inet.h
.endif

use_sl.h:
	echo "#define NSL ${NSL}" > use_sl.h

.include <bsd.kmod.mk>
