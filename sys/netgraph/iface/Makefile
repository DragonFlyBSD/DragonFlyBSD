# $FreeBSD: src/sys/modules/netgraph/iface/Makefile,v 1.5.2.4 2001/12/21 09:00:48 ru Exp $
# $DragonFly: src/sys/netgraph/iface/Makefile,v 1.4 2005/02/18 11:41:42 corecode Exp $
# $Whistle: Makefile,v 1.2 1999/01/19 19:39:21 archie Exp $

KMOD=		ng_iface
SRCS= 		ng_iface.c opt_atalk.h opt_inet.h opt_inet6.h opt_ipx.h
KMODDEPS=	netgraph

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
