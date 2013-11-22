# $DragonFly: src/sys/net/bridge/Makefile,v 1.4 2005/12/21 16:40:25 corecode Exp $
#

KMOD=	if_lagg
SRCS=	if_lagg.c ieee8023ad_lacp.c
SRCS+=	opt_inet.h opt_inet6.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_inet6.h:
	echo "#define INET6 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
