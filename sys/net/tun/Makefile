# $FreeBSD: src/sys/modules/if_tun/Makefile,v 1.19.2.1 2001/07/24 09:49:42 dd Exp $
# $DragonFly: src/sys/net/tun/Makefile,v 1.6 2005/02/18 11:41:42 corecode Exp $

.PATH:  ${.CURDIR}/../../net
KMOD=   if_tun
SRCS=   if_tun.c opt_inet.h opt_inet6.h opt_atalk.h opt_ipx.h
NOMAN=

NBPF?=	1
NTUN?=		2

CFLAGS+= ${PROTOS}

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > opt_inet.h

opt_inet6.h:
	echo "#define INET6 1" > opt_inet6.h

opt_atalk.h:
	echo "#define NETATALK 1" > opt_atalk.h

opt_ipx.h:
	echo "#define IPX 1" > opt_ipx.h
.endif

.include <bsd.kmod.mk>
