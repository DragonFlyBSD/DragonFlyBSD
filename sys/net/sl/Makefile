# $FreeBSD: src/sys/modules/if_sl/Makefile,v 1.13 1999/11/28 18:52:53 bde Exp $
# $DragonFly: src/sys/net/sl/Makefile,v 1.3 2003/08/15 07:03:11 dillon Exp $

.PATH:  ${.CURDIR}/../ppp_layer
KMOD=   if_sl
SRCS=   if_sl.c slcompress.c opt_inet.h use_sl.h
NOMAN=

NSL?=		2
PROTOS?=	-DINET

CFLAGS+= ${PROTOS}
CLEANFILES=	use_sl.h

opt_inet.h:
	echo "#define INET 1" > opt_inet.h

use_sl.h:
	echo "#define NSL ${NSL}" > use_sl.h

.include <bsd.kmod.mk>
