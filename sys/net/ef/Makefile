# $FreeBSD: src/sys/modules/if_ef/Makefile,v 1.2.2.2 2001/04/25 11:24:03 ru Exp $

KMOD=   if_ef
SRCS=   if_ef.c opt_ipx.h opt_inet.h opt_ef.h

# If you need only limited number of frames comment out unneeded ones
# this will reduce number of visible devices
ETHER_II=
ETHER_8023=
ETHER_8022=
ETHER_SNAP=

.if defined(EFDEBUG)
CFLAGS+= -DEF_DEBUG
.endif

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_ipx.h:
	echo "#define IPX 1" > ${.TARGET}
.endif

opt_ef.h:
.for frame in ETHER_II ETHER_8023 ETHER_8022 ETHER_SNAP
.if defined(${frame})
	echo "#define ${frame} 1" >> ${.TARGET}
.endif
.endfor

.include <bsd.kmod.mk>
