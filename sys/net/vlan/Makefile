# $FreeBSD: src/sys/modules/if_vlan/Makefile,v 1.1.2.1 2001/12/04 20:09:32 brooks Exp $
# $DragonFly: src/sys/net/vlan/Makefile,v 1.2 2003/06/17 04:28:44 dillon Exp $

.PATH: ${.CURDIR}/../../net

KMOD=	if_vlan
SRCS=	if_vlan.c opt_inet.h
CFLAGS+= -DNVLAN=0
NOMAN=

opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

.include <bsd.kmod.mk>
