# $FreeBSD: src/sys/modules/if_faith/Makefile,v 1.1.2.1 2001/12/15 01:07:54 brooks Exp $
# $DragonFly: src/sys/net/faith/Makefile,v 1.4 2006/10/25 20:56:02 dillon Exp $

.PATH: ${.CURDIR}/../../net

KMOD=	if_faith
SRCS=	if_faith.c opt_inet.h opt_inet6.h bus_if.h device_if.h
KCFLAGS+= -DNFAITH=0

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_inet6.h:
	echo "#define INET6 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
