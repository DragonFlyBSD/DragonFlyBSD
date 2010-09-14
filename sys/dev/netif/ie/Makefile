# $FreeBSD: src/sys/modules/rl/Makefile,v 1.6 2000/01/28 11:26:34 bde Exp $
# $DragonFly: src/sys/dev/netif/ie/Makefile,v 1.4 2006/06/25 11:02:38 corecode Exp $

KMOD= if_ie
SRCS= if_ie.c
SRCS+= use_ie.h opt_inet.h opt_ipx.h

use_ie.h:
	echo "#define NIE 1" > ${.TARGET}

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_ipx.h:
	echo "" > ${.TARGET}
.endif

.include <bsd.kmod.mk>

