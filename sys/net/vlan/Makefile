# $FreeBSD: src/sys/modules/if_vlan/Makefile,v 1.1.2.1 2001/12/04 20:09:32 brooks Exp $
# $DragonFly: src/sys/net/vlan/Makefile,v 1.3 2005/02/18 11:41:42 corecode Exp $

.PATH: ${.CURDIR}/../../net

KMOD=	if_vlan
SRCS=	if_vlan.c opt_inet.h
CFLAGS+= -DNVLAN=0
NOMAN=

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
