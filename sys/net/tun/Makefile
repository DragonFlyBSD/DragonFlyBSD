# $FreeBSD: src/sys/modules/if_tun/Makefile,v 1.19.2.1 2001/07/24 09:49:42 dd Exp $

.PATH:  ${.CURDIR}/../../net
KMOD=   if_tun
SRCS=   if_tun.c opt_inet.h opt_inet6.h opt_ipx.h use_tun.h

NTUN?=	2

CFLAGS+= ${PROTOS}
CLEANFILES=	use_tun.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > opt_inet.h

opt_inet6.h:
	echo "#define INET6 1" > opt_inet6.h

opt_ipx.h:
	echo "#define IPX 1" > opt_ipx.h
.endif

use_tun.h:
	echo "#define NTUN ${NTUN}" > use_tun.h

.include <bsd.kmod.mk>
