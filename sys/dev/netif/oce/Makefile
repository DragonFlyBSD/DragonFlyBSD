#
# $FreeBSD: src/sys/modules/oce/Makefile,v 1.3 2012/11/17 01:52:59 svnexp Exp $
#

KMOD	= if_oce
SRCS	= oce_if.c oce_hw.c oce_mbox.c oce_util.c oce_queue.c oce_sysctl.c
SRCS	+= bus_if.h device_if.h pci_if.h opt_inet.h opt_inet6.h

# uncomment for lock profiling statistics
#CFLAGS	+= -DLOCK_PROFILING

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_inet6.h:
	echo "#define INET6 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
