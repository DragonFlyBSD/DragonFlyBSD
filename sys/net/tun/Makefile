# $FreeBSD: src/sys/modules/if_tun/Makefile,v 1.19.2.1 2001/07/24 09:49:42 dd Exp $
# $DragonFly: src/sys/net/tun/Makefile,v 1.3 2003/09/15 23:38:14 hsu Exp $

.PATH:  ${.CURDIR}/../../net
KMOD=   if_tun
SRCS=   if_tun.c opt_inet.h vnode_if.h
NOMAN=

NBPF?=	1
NTUN?=		2

CFLAGS+= ${PROTOS}

opt_inet.h:
	echo "#define INET 1" > opt_inet.h
	echo "#define INET6 1" > opt_inet6.h
	echo "#define NETATALK 1" > opt_atalk.h
	echo "#define IPX 1" > opt_ipx.h

.include <bsd.kmod.mk>
