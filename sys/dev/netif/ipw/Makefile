# $DragonFly: src/sys/dev/netif/ipw/Attic/Makefile,v 1.1 2005/03/06 18:25:12 dillon Exp $
#
KMOD    = if_ipw
SRCS    = if_ipw.c \
          device_if.h \
          bus_if.h \
          pci_if.h \
          opt_inet.h

CFLAGS	+=  -DNBPFILTER=1 -DALTQ

#WARNS?=6

opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

.include <bsd.kmod.mk>
