# $FreeBSD: src/sys/modules/if_tun/Makefile,v 1.19.2.1 2001/07/24 09:49:42 dd Exp $
# $DragonFly: src/sys/net/tun/Makefile,v 1.4 2003/09/16 09:14:40 hsu Exp $

.PATH:  ${.CURDIR}/../../net
KMOD=   if_tun
SRCS=   if_tun.c vnode_if.h opt_inet.h opt_inet6.h opt_atalk.h opt_ipx.h
NOMAN=

NBPF?=	1
NTUN?=		2

CFLAGS+= ${PROTOS}

opt_inet.h:
	echo "#define INET 1" > opt_inet.h

opt_inet6.h:
	echo "#define INET6 1" > opt_inet6.h

opt_atalk.h:
	echo "#define NETATALK 1" > opt_atalk.h

opt_ipx.h:
	echo "#define IPX 1" > opt_ipx.h

.include <bsd.kmod.mk>
