# $FreeBSD: src/sys/modules/if_sl/Makefile,v 1.13 1999/11/28 18:52:53 bde Exp $

.PATH:  ${.CURDIR}/../ppp_layer
KMOD=   if_sl
SRCS=   if_sl.c slcompress.c opt_inet.h use_sl.h

NSL?=		2

KCFLAGS+=	-DINET

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

use_sl.h:
	echo "#define NSL ${NSL}" > ${.TARGET}

.include <bsd.kmod.mk>
