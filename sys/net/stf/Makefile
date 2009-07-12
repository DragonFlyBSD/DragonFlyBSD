# $FreeBSD: src/sys/modules/if_stf/Makefile,v 1.1.2.1 2001/07/24 19:10:17 brooks Exp $
# $DragonFly: src/sys/net/stf/Makefile,v 1.3 2005/02/18 11:41:42 corecode Exp $

.PATH: ${.CURDIR}/../../net

KMOD=	if_stf
SRCS=	if_stf.c opt_inet.h opt_inet6.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_inet6.h:
	echo "#define INET6 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
