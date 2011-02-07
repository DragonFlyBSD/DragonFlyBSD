.PATH: ${.CURDIR}/../../net ${.CURDIR}/../../netinet 

KMOD=	if_gre
SRCS=	if_gre.c ip_gre.c opt_inet.h opt_atalk.h
SRCS+=	device_if.h bus_if.h

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_atalk.h:
	echo "#define NETATALK 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
