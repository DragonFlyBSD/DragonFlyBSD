# $FreeBSD$
# $DragonFly: src/sys/net/gre/Makefile,v 1.2 2005/02/18 11:41:42 corecode Exp $

.PATH: ${.CURDIR}/../../net ${.CURDIR}/../../netinet 

KMOD=	if_gre
SRCS=	if_gre.c ip_gre.c opt_inet.h opt_ns.h opt_atalk.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_ns.h:
	echo "#define NS 1" > ${.TARGET}

opt_atalk.h:
	echo "#define NETATALK 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
